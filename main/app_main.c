/**
 * ESP32-C3 RID Combined Firmware
 *
 * WiFi AP: SSID "rid", Password "12345678"
 *
 * Features:
 *   - C-RID Wi-Fi sniffer + decoder (GB 42590, ASTM F3411)
 *   - C-RID simulated transmitter（原始帧经 STA 接口注入）
 *   - Web management interface（AP 接口，192.168.4.1）
 *
 * 工作模式说明 (WIFI_MODE_APSTA)：
 *   - AP 接口：承载 192.168.4.1 网页 + 站点连接管理。
 *     不再从 AP 接口注入原始帧，保证连接稳定、网页流畅。
 *   - STA 接口：用于发送模拟无人机的原始 802.11 Beacon 帧（en_sys_seq=false），
 *     并配合混杂模式(Promiscuous)抓取信道上的 RID 帧。
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"

#include "opendroneid.h"
#include "crid_rx_types.h"
#include "crid_sniffer.h"
#include "crid_parser.h"
#include "crid_tracker.h"
#include "crid_json.h"
#include "crid_web.h"
#include "crid_nvs.h"

// Simulator
#include "crid_config.h"
#include "crid_messages.h"
#include "crid_patrol.h"
#include "crid_wifi.h"

static const char *TAG = "APP";

/* ================================================================
 * WiFi AP Config
 * ================================================================ */
#define AP_SSID     "rid"
#define AP_PASSWORD "12345678"
#define AP_CHANNEL  6

/* ================================================================
 * Simulator State（定义在 crid_web.c，通过访问器共享）
 * ================================================================ */
#define g_sim (*crid_web_get_sim())
static uint8_t g_beacon_frame[1024];
static uint16_t g_beacon_frame_len = 0;

/* ================================================================
 * WiFi AP + STA 初始化
 * ================================================================ */
static void wifi_init_apsta(void) {
    // 同时创建 AP 与 STA 网络接口：
    //   AP  -> 网页 + 站点连接管理
    //   STA -> 原始帧注入 + 混杂监听（不连接任何网络）
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // APSTA 模式：AP 正常连接管理，STA 不连接、只用于注入/监听
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // AP 配置：信道固定为 AP_CHANNEL
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASSWORD,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .channel = AP_CHANNEL,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    // STA 配置留空：确保不发起扫描/连接，避免切信道干扰 AP 站点
    wifi_config_t sta_cfg = {0};
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(AP_CHANNEL, WIFI_SECOND_CHAN_NONE));

    // 关闭无线省电：STA 注入原始帧（尤其仅 1 架无人机、每秒 1 帧的稀疏发送）
    // 时，避免无线因省电进入休眠/降功耗状态导致注入帧偶发丢失。
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGI(TAG, "WiFi APSTA started: SSID=%s CH=%d", AP_SSID, AP_CHANNEL);
}

/* ================================================================
 * Tasks
 * ================================================================ */

// 嗅探任务：从队列取帧 -> 解码 -> 更新追踪表
static void sniffer_task(void *pv) {
    crid_sniffer_init();
    QueueHandle_t q = crid_sniffer_get_queue();
    SemaphoreHandle_t mtx = crid_tracker_get_mutex();
    sniffer_msg_t msg;
    uint32_t last_cleanup = 0;

    while (1) {
        if (xQueueReceive(q, &msg, pdMS_TO_TICKS(1000)) != pdTRUE) continue;
        if (msg.msg_type != MSG_TYPE_RID) continue;

        if (xSemaphoreTake(mtx, pdMS_TO_TICKS(100)) != pdTRUE) continue;

        uav_track_t *uav = crid_tracker_find_or_create(msg.src_mac);
        if (!uav) {
            uint32_t now = esp_log_timestamp();
            if (now - last_cleanup >= 10000) {
                crid_tracker_cleanup(UAV_TIMEOUT_MS);
                last_cleanup = now;
                uav = crid_tracker_find_or_create(msg.src_mac);
            }
            if (!uav) { xSemaphoreGive(mtx); continue; }
        }

        bool was_new = (uav->msg_count == 0);
        uav->last_rssi = msg.rssi;
        uav->last_channel = msg.channel;
        memcpy(uav->oui, msg.oui, 3);
        uav->oui_type = msg.oui_type;
        uav->transport = (uint8_t)GET_RID_TRANSPORT(msg.oui[0], msg.oui[1], msg.oui[2]);

        rid_protocol_t proto = crid_parser_decode(uav, msg.data, msg.data_len);
        if (proto != RID_PROTOCOL_UNKNOWN) uav->protocol = (uint8_t)proto;

        crid_parser_extract_layered(uav);
        xSemaphoreGive(mtx);

        if (was_new && uav->basic_id.valid) {
            json_uav_discovery(uav);
        }
        if (uav->basic_id.valid) {
            json_uav_update(uav);
        }
    }
}

// 模拟任务：每 ~1s 为每架无人机构建并发送一次 C-RID Beacon 帧
static void simulator_task(void *pv) {
    crid_wifi_init(AP_CHANNEL);
    TickType_t lastBeacon = xTaskGetTickCount();
    TickType_t lastPatrol = xTaskGetTickCount();
    const TickType_t bInt = pdMS_TO_TICKS(1000);  // 广播周期 1s
    const TickType_t pInt = pdMS_TO_TICKS(1000);  // 位置随机游走更新周期 1s（与广播同频，位置更平滑）

    while (1) {
        vTaskDelayUntil(&lastBeacon, bInt);
        if (!g_sim.running) continue;

        TickType_t now = xTaskGetTickCount();

        // 随机游走更新位置
        if ((now - lastPatrol) >= pInt) {
            for (int i = 0; i < g_sim.count; i++) {
                crid_patrol_random_step(&g_sim.drones[i]);
            }
            lastPatrol = now;
        }

        // 逐架无人机发送 Beacon（帧间加 5ms 延时让出 CPU/无线电，避免突发抢占网页）
        for (int i = 0; i < g_sim.count; i++) {
            cn_crid_config_t *cfg = &g_sim.drones[i];
            if (crid_build_beacon_frame(cfg, g_beacon_frame, sizeof(g_beacon_frame), &g_beacon_frame_len)) {
                crid_wifi_send_raw_frame(g_beacon_frame, g_beacon_frame_len);
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

// 监控任务：定期打印在线无人机数量与内存，并清理超时(离线)目标
static void monitor_task(void *pv) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        // 定期清理超过离线阈值(15s)未更新的目标，释放追踪槽位
        SemaphoreHandle_t mtx = crid_tracker_get_mutex();
        if (xSemaphoreTake(mtx, pdMS_TO_TICKS(100)) == pdTRUE) {
            crid_tracker_cleanup(UAV_TIMEOUT_MS);
            xSemaphoreGive(mtx);
        }
        int online = crid_tracker_get_active_count();
        ESP_LOGI(TAG, "Status: online=%d sim=%s heap=%lu",
            online, crid_web_is_sim_running() ? "ON" : "OFF", esp_get_free_heap_size());
    }
}

/* ================================================================
 * Main
 * ================================================================ */
void app_main(void) {
    ESP_LOGI(TAG, "=== ESP32-C3 RID Scanner + Simulator ===");
    ESP_LOGI(TAG, "WiFi AP: %s / %s", AP_SSID, AP_PASSWORD);
    ESP_LOGI(TAG, "Target: ESP32-C3 DevKitM-1");

    // Init NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Init TCP/IP
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Init WiFi APSTA（AP 管网页/连接，STA 管原始帧注入/监听）
    wifi_init_apsta();

    // Init tracker + NVS
    crid_tracker_init();
    crid_nvs_init();

    // Init web server（内部会加载 NVS 中保存的模拟器配置到 g_sim）
    crid_web_init();

    // Create tasks
    xTaskCreate(sniffer_task, "sniffer", 4096, NULL, 5, NULL);
    xTaskCreate(simulator_task, "sim", 4096, NULL, 4, NULL);
    xTaskCreate(monitor_task, "monitor", 2048, NULL, 2, NULL);

    // Start web server（httpd 在独立任务中运行，此处立即返回）
    crid_web_start();
}
