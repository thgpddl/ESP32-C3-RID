# ESP32-C3 RID 项目完整搭建教程

> 目标：在 Ubuntu 上基于 ESP-IDF 6.2 编译、烧录 `BG5VYX/esp32-c3-rid` 固件。
> 本文只保留**验证可用的步骤**，踩坑细节与解决方案一并记录。

***

## 一、项目简介

- **仓库**：<https://github.com/BG5VYX/esp32-c3-rid>
- **硬件**：ESP32-C3 SuperMini
- **功能**：Remote ID **嗅探（Scanner）** + **模拟发射（Simulator）**，兼容 GB 42590-2023 与 ASTM F3411-22a
- **WiFi AP**：`rid` / `12345678`，连上后自动弹出 Captive Portal 后台

***

## 二、环境准备（Ubuntu 20.04）

### 2.1 升级 CMake

系统自带 CMake 3.16.3 太老（ESP-IDF 6.2 要求 ≥ 3.22）。直接升级到 4.x：

```bash
sudo apt update
sudo apt install -y gpg wget

# 添加 Kitware 签名密钥
wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc \
  2>/dev/null | gpg --dearmor - | \
  sudo tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null

# 添加 Kitware APT 源（Ubuntu 20.04 = focal）
echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ focal main' | \
  sudo tee /etc/apt/sources.list.d/kitware.list

sudo apt update
sudo apt install -y cmake

# 验证
cmake --version   # 应显示 4.x
```

> 其他版本：`focal`(20.04) / `jammy`(22.04) / `noble`(24.04) / `resolute`(26.04)，按系统替换。

### 2.2 基础依赖

有python3环境可以跳过

```bash
sudo apt install -y git python3 python3-pip ninja-build
```

***

## 三、获取代码

```bash
cd ~/WorkSpace/Lab/RID-Simulator
git clone --recursive https://github.com/BG5VYX/esp32-c3-rid.git
git clone https://github.com/espressif/esp-idf.git
```

> ⚠️ **必须** **`--recursive`**：`components/opendroneid/` 是子模块，漏掉会导致解码库缺失。

***

## 四、安装 ESP-IDF 工具链

```bash
cd ~/WorkSpace/Lab/RID-Simulator/esp-idf
./install.sh esp32c3     # 下载编译器、cmake、ninja 等到 ~/.espressif/tools/
. ./export.sh
```

***

## 五、适配 IDF 6.2（关键修改）

IDF 6.x 相比 5.x 做了两项破坏性改动，本项目需要手动适配。

### 5.1 删除已移除的 `json` 组件

编辑 `main/CMakeLists.txt`，在 `REQUIRES` 中**删除** **`json`**（JSON 功能由 `cjson` 提供，而 `cjson` 已是依赖项）：

```cmake
# 修改前
REQUIRES cjson json

# 修改后
REQUIRES cjson
```

### 5.2 添加拆分的 UART 驱动组件

IDF 6.x 把 `driver/` 拆分成了独立组件，`driver/uart.h` 现在属于 `esp_driver_uart`。在 `PRIV_REQUIRES` 中加入它：

```cmake
# 修改前
PRIV_REQUIRES opendroneid

# 修改后
PRIV_REQUIRES opendroneid esp_driver_uart
```

### 5.3 完整 `main/CMakeLists.txt` 示例

```cmake
idf_component_register(
    SRCS
        app_main.c
        crid_sniffer.c
        crid_parser.c
        crid_parser_astm.c
        crid_parser_gb.c
        crid_tracker.c
        crid_nvs.c
        crid_web.c
        crid_serial.c
        crid_config.c
        crid_messages.c
        crid_wifi.c
        crid_patrol.c
    INCLUDE_DIRS
        "."
    REQUIRES
        cjson
    PRIV_REQUIRES
        opendroneid
        esp_driver_uart
)
```

> 💡 **可能遇到的后续拆分组件**（若编译继续报 `driver/xxx.h` 找不到，按表追加到 `PRIV_REQUIRES`）：
>
> | 老头文件                  | 新组件名              |
> | --------------------- | ----------------- |
> | `driver/uart.h`       | `esp_driver_uart` |
> | `driver/gpio.h`       | `esp_driver_gpio` |
> | `driver/spi_master.h` | `esp_driver_spi`  |
> | `driver/i2c.h`        | `esp_driver_i2c`  |

***

## 六、编译

```bash
cd ~/WorkSpace/Lab/RID-Simulator/esp32-c3-rid
rm -rf build            # 改过 CMakeLists 后务必清理缓存
idf.py set-target esp32c3
idf.py build
```

成功后产物：

- `build/bootloader/bootloader.bin`
- `build/partition_table/partition-table.bin`
- `build/esp32_crid.bin`

***

## 七、烧录与监视

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

### 权限问题

若报错 `Could not open /dev/ttyUSB0, the port is busy or doesn't exist`：

**临时解决：**

```bash
sudo chmod 777 /dev/ttyUSB0
```

**永久方案（推荐）：**

```bash
sudo usermod -aG dialout $USER
# 注销重登录后生效
```

***

## 八、运行验证

1. 设备上电，手机/电脑搜索 WiFi：`rid`，密码 `12345678`
2. 连接后手动访问 `http://192.168.4.1`
3. 后台可查看嗅探到的无人机列表、模拟器配置等。

### 使用注意：模拟数量变化时的“超时延迟”

在模拟器页调整无人机数量时，接收端（RID探测设备）表现会**不对称**：

- **增加数量**：新目标几乎立即出现（≤1 秒）
- **减少数量**：被移除的目标会**延迟约 5 秒**才从列表消失

**原因**：RID 广播（Beacon）没有“我要下线”的结束帧，标准接收机采用**邻居表 + 超时剔除**机制——连续约 5 秒收不到某目标即判定其消失。这是 RID 标准的固有设计，**不是固件问题**；仍在广播的那架无人机始终实时可见。

**结论与建议**：

- 数量=1 时接收端稳定显示 1 个目标即为正常发射
- 若希望“减少数量”也能快速反应，请调整**接收端/测试 App 的超时参数**（如 5s → 2s），该项在接收工具侧配置，不在本固件内

### 使用注意：关闭“模拟无人机发送”后的秒级延迟

关闭“模拟无人机发送”开关后，接收端同样会**延迟约 5 秒**才把目标移除。原因同上——广播没有“下线帧”，接收端只能靠超时剔除目标。**不是固件问题**；发送开启时目标才会被实时保持显示。

### 使用注意：模拟参数的生效时机

- **发送开关关闭时**修改参数（数量 / 中心经纬度）：配置会立即保存，**下次开启发送时生效**
- **发送开关开启时**修改参数：**立即生效**（下一轮广播即使用新配置）

**界面轮询刷新说明**：发送开启时，界面会低频轮询（约每 5 秒）读取内部参数并刷新到输入框。正在聚焦编辑的输入框不会被覆盖（已内置焦点保护），因此编辑时无需担心被重置；未聚焦时轮询会同步为内部最新值。

### 使用注意：本设备无法监听到自身发射的 RID

模拟发射的原始帧通过 WiFi 驱动直接注入（走发射链路，不经过本机接收/嗅探链路），因此**设备自带的网页后台看不到自己发射的模拟无人机**。

要验证模拟发射是否生效，请使用**外部标准 RID 接收机/测试 App**，例如：

- 另一台 ESP32 接收设备
- 支持监听（Monitor）模式的 USB 网卡 + 解码脚本（如 `tools/` 下的接收脚本）

### 使用注意：本设备自身的“离线”判定为 15 秒

设备对自己嗅探到的目标采用 **15 秒离线阈值**（`UAV_TIMEOUT_MS = 15000`）：

- 依据：RID 规范要求无人机 **≥1Hz（每秒 1 次）** 广播；连续 15 秒未收到某目标的帧，即可判定其离线
- 表现：距上次更新 ≤15s 判定为**在线**；连续 15s 无帧即标记**离线**，并约在 15–20 秒内从仪表盘列表移除
- 仪表盘刷新周期仍为 **5 秒**，因此“离线”状态最多延迟 1 个轮询周期反映到界面

> ⚠️ 注意区分：**外部标准接收机**通常用约 5 秒的邻居表超时（见上文“超时延迟”），而**本设备自身**的离线判定为 15 秒——两者是不同设备的独立机制。

***

## 九、常见问题速查

| 问题                                          | 原因                     | 解决                                            |
| ------------------------------------------- | ---------------------- | --------------------------------------------- |
| `CMake 3.22 or higher is required`          | 系统 CMake 太老            | 升级系统 CMake（第二章）或 `source export.sh` 用 IDF 自带版 |
| `which cmake` 仍指向 `/usr/bin/cmake`          | conda `(base)` 抢占 PATH | `conda deactivate` 后再 `source export.sh`      |
| `xtensa-esp32c3-elf-gcc: command not found` | 工具链未安装                 | 运行 `./install.sh esp32c3`                     |
| `unknown component 'json'`                  | IDF 6.2 已移除 `json`     | `REQUIRES` 删掉 `json`，只留 `cjson`               |
| `driver/uart.h: No such file or directory`  | IDF 6.2 拆分 driver      | `PRIV_REQUIRES` 加 `esp_driver_uart`           |
| 烧录时端口忙                                      | 权限不足                   | `sudo chmod 777 /dev/ttyUSB0` 或加入 `dialout` 组 |

***

## 十、版本说明与合规提醒

- **IDF 版本**：README 推荐 v5.2.1，本教程使用 **6.2**（需做第五章适配）。如需严格对齐文档：`cd esp-idf && git checkout v5.2.1 && git submodule update --init --recursive`。
- **合规**：模拟发射 RID 仅限屏蔽房/自管场地测试，不要用于公共空域冒充合法无人机。

附：一键环境激活

# 加入 \~/.bashrc

```bash
alias get_idf='conda deactivate 2>/dev/null; source ~/WorkSpace/Lab/RID-Simulator/esp-idf/export.sh'
```

之后开新终端只需 get\_idf 即可开始开发。
