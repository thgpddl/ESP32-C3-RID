#include "crid_nvs.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "CRID_NVS";
static const char *NVS_NS = "crid_cache";
static nvs_handle_t g_nvs;

void crid_nvs_init(void) {
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &g_nvs);
    if (err != ESP_OK) { ESP_LOGE(TAG, "NVS open fail: %d", err); return; }
    ESP_LOGI(TAG, "NVS init OK");
}

bool crid_nvs_save_sim_config(const void *cfg, size_t len) {
    if (!g_nvs) return false;
    return nvs_set_blob(g_nvs, "sim_cfg", cfg, len) == ESP_OK && nvs_commit(g_nvs) == ESP_OK;
}

size_t crid_nvs_load_sim_config(void *cfg, size_t max) {
    if (!g_nvs) return 0;
    size_t len = max;
    if (nvs_get_blob(g_nvs, "sim_cfg", cfg, &len) == ESP_OK) return len;
    return 0;
}
