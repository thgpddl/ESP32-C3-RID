#include "crid_wifi.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CN_C-RID_WIFI";

esp_err_t crid_wifi_init(uint8_t channel) {
    // WiFi already initialized by app_main in AP mode
    // Just set channel and enable promiscuous
    esp_err_t ret = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "TX WiFi ready: channel=%u", channel);
    return ESP_OK;
}

esp_err_t crid_wifi_send_raw_frame(const uint8_t *frame, uint16_t len) {
    if (frame == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // 关键设计：原始帧一律通过 STA 接口(WIFI_IF_STA)发送，且 en_sys_seq=false。
    // 原因：
    //   1) AP 接口负责站点的连接管理(Beacon / Probe Response / 认证/关联握手帧)。
    //      若在其上注入原始帧，会抢占 AP 的发送队列与序列号空间，导致站点
    //      关联/握手时好时坏（表现为手机报"密码错误"），并挤占网页的 TCP
    //      流量（网页卡顿、偶尔打不开）。
    //   2) 本固件使用 APSTA 模式（见 app_main.c）：AP 管网页与连接，
    //      STA 管原始帧注入与混杂监听。STA 未连接任何网络，en_sys_seq=false
    //      不会对任何已管理连接产生副作用，这正是 ESP-IDF / OpenDroneID
    //      推荐的原始帧注入方式（也不会打印 en_sys_seq 警告）。
    esp_err_t ret = esp_wifi_80211_tx(WIFI_IF_STA, frame, len, false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TX failed: %s", esp_err_to_name(ret));
        return ret;
    }
    // 节流打印累计发送成功计数（每 20 次一条），
    // 便于在串口确认原始帧确实在发出，辅助排查“接收不到”问题。
    static uint32_t tx_ok = 0;
    if ((++tx_ok % 20) == 1) {
        ESP_LOGI(TAG, "TX ok: total %lu raw frames (len=%u)",
                 (unsigned long)tx_ok, (unsigned)len);
    }
    return ret;
}
