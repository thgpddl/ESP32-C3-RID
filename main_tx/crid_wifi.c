#include "crid_wifi.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CN_C-RID_WIFI";

esp_err_t crid_wifi_init(uint8_t channel) {
    esp_err_t ret;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    ret = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_promiscuous(true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_promiscuous failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Wi-Fi initialized: channel=%u, promiscuous mode", channel);
    return ESP_OK;
}

esp_err_t crid_wifi_send_raw_frame(const uint8_t *frame, uint16_t len) {
    if (frame == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // 本固件（main_tx）以 STA 模式运行（见上方 crid_wifi_init），
    // AP 接口未初始化，不应尝试，只使用 STA 接口发送。
    //
    // 主路径使用 en_sys_seq=true：让 WiFi 协议栈统一管理 802.11 序列号，
    // 避免 en_sys_seq=false 时打印
    // "en_sys_seq should be true to avoid side-effect to WiFi connection"
    // 并防止未受管理的序列号干扰连接（副作用）。
    esp_err_t ret = esp_wifi_80211_tx(WIFI_IF_STA, frame, len, true);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "TX OK (%u bytes, STA with seq)", (unsigned)len);
        return ESP_OK;
    }

    // 兜底：个别驱动对 raw beacon 的 with-seq 支持不佳时，
    // 退回 no-seq（仅作最后手段，正常路径不会走到这里）。
    ESP_LOGW(TAG, "STA with-seq TX failed: %s, falling back to no-seq", esp_err_to_name(ret));
    ret = esp_wifi_80211_tx(WIFI_IF_STA, frame, len, false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "All TX methods failed: %s", esp_err_to_name(ret));
    }
    return ret;
}
