#ifndef CRID_NVS_H
#define CRID_NVS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// NVS 仅保留“模拟器配置持久化”。
// 说明：原先接收到的无人机会被写入 NVS 做历史缓存，但 NVS 写操作会
// 阻塞共享 Flash，在单核 C3 上导致网页响应卡顿；且“实时接收”本就不需要
// 历史记录，故已移除无人机历史缓存（save_uav / 计数 / 按索引查询 / 清空）。

void crid_nvs_init(void);
bool crid_nvs_save_sim_config(const void *config, size_t len);
size_t crid_nvs_load_sim_config(void *config, size_t max_len);

#ifdef __cplusplus
}
#endif
#endif
