# ESP32-C3 RID 新手入门指南

> 面向第一次接触本项目的开发者。本指南按“**先自查环境 → 不满足再按方案处理 → 编译 → 烧录 → 验证**”的顺序编写，尽量让你不踩版本坑。
>
> 范围：以合并固件 `main/` 为准（RID 模拟发射 + 接收嗅探 + Web 后台）。若你还需要更详细的开发踩坑记录，见 `esp32-c3-rid-tutorial.md`。

---

## 一、项目简介

| 项 | 说明 |
|---|---|
| 功能 | **RID 模拟发射**（模拟无人机广播）+ **RID 接收解码**（嗅探周围无人机） |
| 协议 | GB 42590-2023 / ASTM F3411-22a（兼容两种） |
| 硬件 | ESP32-C3（本指南以 **ESP32-C3-DevKitM-1** 为例，SuperMini 亦可；均板载 USB 串口） |
| WiFi 热点 | SSID `rid`，密码 `12345678` |
| Web 后台 | 手机连接热点后访问 `http://192.168.4.1` |
| 串口调试 | 115200，`idf.py monitor` |

---

## 二、环境要求自查表（先看这张表）

**规则**：依次检查每一项。**满足**就跳过；**不满足**就按“不满足时怎么办”处理，处理完再继续。

| # | 工具/环境 | 本项目要求 | 自查命令 | 不满足时怎么办 |
|---|-----------|-----------|----------|----------------|
| 1 | 操作系统 | Linux / macOS / Windows(WSL2) | `uname -a` | 推荐 Ubuntu 20.04/22.04/24.04 或 WSL2 |
| 2 | Git | ≥ 2.0 | `git --version` | `sudo apt install git` |
| 3 | Python | ≥ 3.8（官方 CI 用 3.9） | `python3 --version` | `sudo apt install python3 python3-pip` |
| 4 | CMake | 系统版不要求高，**激活 IDF 后使用其自带 CMake**（项目根最低要求 3.16） | `cmake --version` | 见 [4.1 版本注意](#41-esp-idf-版本最关键) |
| 5 | **ESP-IDF** | **≥ v5.2**（官方/CI 用 v5.2.1，已在 v6.2 验证可编译） | `git -C ~/esp-idf describe --tags` | 见 [4.1](#41-esp-idf-版本最关键) |
| 6 | 目标芯片 | `esp32c3` | `idf.py set-target esp32c3` | 按提示设置即可 |
| 7 | 串口设备权限 | 当前用户可读写 `/dev/ttyUSB*` 或 `/dev/ttyACM*` | `ls -l /dev/ttyUSB*` | 见 [4.2 串口权限](#42-串口权限) |
| 8 | 固件子模块 | `components/opendroneid/` 必须存在 | `ls components/opendroneid/opendroneid.h` | 见 [3. 获取代码](#三获取代码)，用 `--recursive` |

> 自查到不满足项时，先只处理这一项；没有遇到的不必提前安装，避免多余操作。

---

## 三、获取代码

```bash
cd <你的工作目录>
git clone --recursive https://github.com/BG5VYX/esp32-c3-rid.git
cd esp32-c3-rid
```

⚠️ **必须加 `--recursive`**：`components/opendroneid/` 是 Git 子模块，漏掉会导致编译时找不到解码库。

自查：`ls components/opendroneid/opendroneid.h` 能列出文件即正常。

---

## 四、安装 / 激活 ESP-IDF 工具链

### 4.1 ESP-IDF 版本（最关键）

**要求**：`≥ v5.2`。检查当前是否有 IDF 以及版本：

```bash
# 没有 ~/esp-idf 目录 → 未安装，跳到“安装”
ls -d ~/esp-idf 2>/dev/null || echo "未安装 ESP-IDF"
# 已安装则查版本
git -C ~/esp-idf describe --tags
```

**情况 A：未安装或版本 < v5.2**

```bash
git clone --recursive https://github.com/espressif/esp-idf.git ~/esp-idf
cd ~/esp-idf
./install.sh esp32c3        # 只装 ESP32-C3 工具链，避免下载其他芯片工具
. ./export.sh               # 激活环境（建议把该行写入 ~/.bashrc）
```

**情况 B：版本在 v5.2 ~ v6.x（满足）**

直接激活即可，无需改动任何构建文件：

```bash
cd ~/esp-idf && . ./export.sh
```

**情况 C：版本高于 v6.x 或其它发行版**

可能遇到 ESP-IDF 组件拆分/变更导致的编译差异。本项目 `main/CMakeLists.txt` 已按当前适配（`REQUIRES` 使用 `driver`、不含 `json`），若在其它 IDF 版本上编译报错，常见对照见[六、编译](#六编译)中的自查表。

**CMake 版本注意**：`export.sh` 激活后，IDF 自带的较新 CMake 会优先于系统版。若激活后 `which cmake` 仍指向 `/usr/bin/cmake`（常见于 conda 环境抢占 PATH），先执行：

```bash
conda deactivate
# 再重新 source export.sh
cd ~/esp-idf && . ./export.sh && which cmake   # 应指向 ~/.espressif/tools/cmake/...
```

### 4.2 串口权限

ESP32-C3-DevKitM-1 板载 USB 串口，插入后一般出现 `/dev/ttyACM0` 或 `/dev/ttyUSB0`。

自查：`ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null`

- 若能看到设备但**无权限**（权限位无 `rw`）：
  ```bash
  sudo usermod -aG dialout $USER   # 永久方案，注销重登录生效
  # 临时方案：sudo chmod 666 /dev/ttyACM0
  ```
- 若**看不到设备**：检查 USB 线是否支持数据、换个接口/线。

---

## 五、编译

```bash
cd ~/esp-idf && . ./export.sh    # 若已激活可跳过
cd <你的工作目录>/esp32-c3-rid
idf.py set-target esp32c3
idf.py build
```

成功产物（默认合并固件）：

- `build/bootloader/bootloader.bin`
- `build/partition_table/partition-table.bin`
- `build/esp32_crid.bin` ← 主固件

**编译报错自查表**

| 报错 | 原因 | 处理 |
|---|---|---|
| `CMake 3.22 or higher is required` | 用了系统旧版 CMake，而非 IDF 自带版 | `conda deactivate` 后重新 `source export.sh`，确认 `which cmake` 指向 `~/.espressif/tools/cmake/...` |
| `xtensa-esp32c3-elf-gcc: command not found` | 工具链未安装 | `cd ~/esp-idf && ./install.sh esp32c3 && . ./export.sh` |
| `unknown component 'json'` | 旧版 README 曾要求，当前仓库已不含 `json` | 无需处理；若你改了 `main/CMakeLists.txt`，请恢复为仓库原样 |
| `driver/xxx.h: No such file or directory` | 使用较新 IDF（6.x）时 driver 被拆分 | 当前仓库已适配；若仍报，把对应新组件名加入 `main/CMakeLists.txt` 的 `REQUIRES`（见实践记录 5.2 对照表） |
| `missing terminating " character`（index_html.h） | 重新生成网页头时转义错误 | 报告开发者，勿手动改 |

---

## 六、烧录与串口监视

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

- Windows 用 COM 口：`idf.py -p COM3 flash monitor`
- 波特率 **115200**（`monitor` 默认一致）
- 退出 `monitor`：按 `Ctrl+]`
- 若报端口打不开/忙：按 [4.2](#42-串口权限) 处理权限

---

## 七、使用验证

1. 设备上电，手机/电脑搜索热点 **`rid`**，密码 **`12345678`**
2. 连接后浏览器访问 **`http://192.168.4.1`**（一般会自动弹出）
3. 后台两个核心功能：

| 页面 | 作用 |
|---|---|
| 仪表盘 / 无人机列表 | **RID 接收**：显示嗅探到的无人机、在线数、详情（坐标/高度/速度/距上次更新） |
| 模拟器 | **RID 模拟发射**：设置无人机数量、中心经纬度(WGS84)，开关“模拟无人机发送” |

**快速自检流程**：进“模拟器”页 → 点“应用设置”（保存数量/经纬度）→ 打开“模拟无人机发送”开关 → 用外部标准 RID 接收机/测试 App 验证能否收到模拟无人机。

> ⚠️ 本设备**无法在自带后台看到自己发射的模拟无人机**（发射走注入链路，不经过本机接收）。验证发射请用外部接收端（另一台 ESP32 接收设备，或支持监听模式的 USB 网卡 + `tools/` 下的接收脚本）。

---

## 八、使用注意点（务必了解）

### 8.1 模拟数量变化时的“超时延迟”

- **增加数量**：新目标几乎立即出现（≤1 秒）
- **减少数量**：被移除的目标延迟约 5 秒才消失

**原因**：RID 广播没有“下线帧”，标准接收机用“邻居表 + 超时剔除”（连续约 5 秒收不到即判定消失）。属标准固有设计，**不是固件问题**。想快速反应需调接收端/测试 App 的超时参数。

### 8.2 关闭“模拟无人机发送”后的秒级延迟

关闭开关后，接收端同样延迟约 5 秒才移除目标，原因同上（超时剔除），非固件问题。

### 8.3 模拟参数的生效时机

- **发送开关关闭时**修改参数（数量/中心经纬度）：配置立即保存，**下次开启发送时生效**
- **发送开关开启时**修改参数：**立即生效**（下一轮广播即用新配置）

界面会约每 5 秒轮询刷新一次输入框；正在聚焦编辑的输入框不会被覆盖（已内置焦点保护），编辑时无需担心被重置。

### 8.4 本设备无法监听到自身发射的 RID

发射的原始帧走注入链路、不经本机接收，因此**后台看不到自己发射的模拟无人机**。验证发射必须用外部接收端（见第七节）。

### 8.5 本设备自身的“离线”判定为 15 秒

设备对嗅探到的目标采用 **15 秒离线阈值**（`UAV_TIMEOUT_MS = 15000`）：

- 依据：RID 规范要求 ≥1Hz 广播；连续 15 秒无帧即可判定离线
- 表现：距上次更新 ≤15s 为在线；连续 15s 无帧标记离线，约 15–20s 从列表移除
- 仪表盘每 5 秒刷新一次，离线状态最多延迟 1 个轮询周期反映

> ⚠️ 区分：**外部标准接收机**通常约 5 秒邻居表超时；**本设备自身**离线判定 15 秒——两套独立机制。

---

## 九、常见问题速查

| 问题 | 原因 | 解决 |
|---|---|---|
| 连接热点后打不开 `192.168.4.1` | 网页内嵌固件，需先烧录最新版；或手机未真正连上 | 重新 `flash`；确认已连上 `rid` 热点 |
| 连接 WiFi 偶尔报“密码错误” | 固件版本过旧（AP BSSID 与模拟 MAC 冲突） | 更新到当前版本（已修复为独立 LAA MAC） |
| `CMake 3.22 or higher is required` | 用了系统旧 CMake | `conda deactivate` + 重新 `source export.sh` |
| `xtensa-esp32c3-elf-gcc: command not found` | 工具链未安装 | `./install.sh esp32c3` 后重新 `export.sh` |
| 烧录时端口忙/无权限 | 权限不足 | 加入 `dialout` 组或 `chmod` |
| 网页数值总被重置 | 未聚焦的输入框被 5 秒轮询同步 | 编辑时保持该输入框聚焦；改完点“应用设置” |
| 收不到自己发的模拟 RID | 发射不经本机接收 | 用外部接收机验证（见第七/8.4 节） |

---

## 十、合规提醒

模拟发射 RID 仅限**屏蔽房 / 自管测试场地**使用，请勿在公共空域冒充合法无人机广播。
