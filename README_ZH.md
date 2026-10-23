# CIY X79 RGB macOS 控制软件与 USB-HID 协议文档

适用于 **CIY X79**（及同款 65% 配列、68 键/67 键机械键盘）的原生 macOS 驱动与灯效控制套件，支持 Apple Silicon（M1–M4）与 Intel 双架构。支持全局动态灯效、速度/亮度调节，以及真正独立的单键自定义 RGB 灯效（如 WASD 游戏专属背光）。

**语言版本:** [🇩🇪 Deutsch](README.md) | [🇬🇧 English](README_EN.md) | [🇨🇳 简体中文](README_ZH.md) | [🇷🇺 Русский](README_RU.md)

---

## 目录
1. [项目概述与快速上手](#1-项目概述与快速上手)
2. [安装与 macOS Gatekeeper 安全提示](#2-安装与-macos-gatekeeper-安全提示)
3. [硬件协议逆向核心要点](#3-硬件协议逆向核心要点)
4. [USB-HID 协议规范详解](#4-usb-hid-协议规范详解)
5. [灯效模式与调色板定义](#5-灯效模式与调色板定义)
6. [macOS 原生应用 (CIYX79Control.app)](#6-macos-原生应用-ciyx79controlapp)
7. [终端命令行实用工具 (ciy_ctl)](#7-终端命令行实用工具-ciy_ctl)
8. [编译构建与自动化测试](#8-编译构建与自动化测试)
9. [安全性与性能保障](#9-安全性与性能保障)
10. [开源协议、商标声明与免责条款](#10-开源协议商标声明与免责条款)

---

## 1. 项目概述与快速上手

* **支持设备:** CIY X79 机械键盘（65% 紧凑配列，热插拔轴座）
* **主控芯片:** Sinowealth（盛芯微 8051 架构 USB 微控制器）
* **USB 识别标识:**
  * **Vendor ID (VID):** `0x258A` (Sino Wealth)
  * **Product ID (PID):** `0x0049`
  * **USB 接口编号:** Interface `1` (Vendor-Defined HID Class)

### 配列说明：德语 ISO-DE（68键）与 国际 ANSI-US（67键）
本程序图形界面的矢量画布默认以 **德语 ISO-DE 68 键配列** 为基准开发（完整呈现大回车 Enter 与左 Shift 旁按键）。
* **ANSI 兼容性:** ANSI-US 版本为 67 键（横向单行 Enter）。底层的 USB-HID 控制协议、Report 格式以及内存槽位偏移完全通用！ANSI 用户使用图形端或命令行均可完美生效。

### 快速开始：

* **图形客户端 (GUI):**
  直接打开 `CIYX79Control.app`。可以在交互式画布上为任意单个按键指定 HEX 颜色、一键切换预设（WASD、赛博朋克、全红、纯白办公），或导入/导出 JSON 配置。
* **命令行终端 (`ciy_ctl`):**
  ```bash
  # 查看键盘连接状态与当前灯效
  ./ciy_ctl status
  ./ciy_ctl status --json

  # 全键填充指定十六进制颜色与亮度 (0..5)
  ./ciy_ctl fill "#FF5500" 5

  # 一键应用经典预设
  ./ciy_ctl wasd
  ./ciy_ctl wasd-arrows
  ./ciy_ctl all-red

  # 载入导出的 JSON 配置文件
  ./ciy_ctl load profile.json
  ```

---

## 2. 安装与 macOS Gatekeeper 安全提示

由于本项目为开源社区项目，未购买年费 99 美元的 Apple 开发者证书，初次在未签名环境中打开时，macOS **Gatekeeper** 会弹出安全拦截提示（*“无法验证开发者”* 或 *“应用已损坏”*）。

### 一次性信任方法：

* **方法一（访达右键打开 - 推荐）：**
  1. 将解压后的 `CIYX79Control.app` 移动至 `/Applications`（应用程序）文件夹。
  2. 在访达中对该应用单击**右键**（或按住 `Control` 单击），选择**“打开”**。
  3. 在弹出的系统对话框中点击**“仍要打开”**。此后即可直接双击运行。

* **方法二（终端一键清除隔离标记）：**
  ```bash
  xattr -cr /Applications/CIYX79Control.app
  ```

---

## 3. 硬件协议逆向核心要点

为了在 macOS 下实现无缝兼容（遵循德国版权法 § 69e UrhG 与商业秘密法 § 3 GeschGehG 互操作性规定），我们深入分析了该键盘的 HID 通讯协议，发现了四个至关重要的硬件特性：

### A. 自定义模式与彩虹波浪的区分
* **模式 19 (`0x13`):** 官方真正的 `Customize` 模式。只有在该模式下，盛芯微 MCU 才会读取并显示其内部 XRAM 缓冲区中的自定义单键 RGB 颜色！
* **模式 20 (`0x14`):** 彩虹波浪动态动画。若以往的第三方工具误设为模式 20，键盘将强制进入动画效果。
* **模式 15 (`0x0F`):** `Solid` 静态全局纯色模式。

### B. 必须严格执行的发包顺序
若在颜色数据完全同步前切入模式 19，固件将回滚至预设动画。正确的原子发包顺序为：
1. 发送 **`Report 0x0B` (379 字节)**: 将平面 RGB 颜色写入 XRAM。
2. 延时 **50 毫秒** (`usleep(50000)`): 等待 8051 MCU 锁存缓冲区。
3. 发送 **`Report 0x0A` (42 字节)** 设置模式 19: 瞬间激活自定义背光，无任何屏闪。

### C. 内存偏移 +1 规则
在固件内部，硬件矩阵槽位 1 (`Esc`) 在内存中被镜像到槽位 0 (`XRAM[0x0A60] = XRAM[0x0A61]`)。因此所有 LED 实际物理控制地址整体向右**偏移 1 个单位**:
$$\text{LED 索引} = \text{矩阵槽位} + 1$$

* **W 键:** 槽位 `14` $\rightarrow$ **索引 `15`**
* **A 键:** 槽位 `9` $\rightarrow$ **索引 `10`**
* **S 键:** 槽位 `15` $\rightarrow$ **索引 `16`**
* **D 键:** 槽位 `21` $\rightarrow$ **索引 `22`**
* **上方向键:** 槽位 `88` $\rightarrow$ **索引 `89`**

### D. 易失性 XRAM 与硬件快捷键 (`Fn + ;`)
单键自定义 RGB 仅保存在键盘控制器的易失性 **XRAM** 中，未写入 Flash ROM。若通过板载硬件快捷键（`Fn + ;`）切换动画，固件动画引擎会覆盖该 RAM 区。通过 macOS 客户端或 CLI 可在 50ms 内一键恢复您的自定义布局。

---

## 4. USB-HID 协议规范详解

与键盘的通信完全基于 Endpoint 0 (Interface 1) 的 **USB Control Transfers**:
* **Set Feature:** `bmRequestType = 0x21`, `bRequest = 0x09` (`kHIDRqSetReport`)
* **Get Feature:** `bmRequestType = 0xA1`, `bRequest = 0x01` (`kHIDRqGetReport`)
* **wValue:** `(0x03 << 8) | ReportID`
* **wIndex:** `1` (Interface 编号)

| 报告编号 | 数据包长度 | 数据流向 | 用途 |
|---|---|---|---|
| **`0x0A`** | 42 字节 | 读 / 写 (Get/Set) | 全局参数（模式、调色板、速度、亮度） |
| **`0x0B`** | 379 字节 | 写入 (Set Feature) | 平面 RGB 矩阵（3 色层 $\times$ 126 字节） |
| **`0x09`** | 505 字节 | 读 / 写 (Get/Set) | 按键重映射配置表（126 个槽位） |

### `Report 0x0A` 结构 (42 字节):
* `Byte 0`: `0x0A` (Report ID)
* `Byte 1`: 模式 (`0` = 关闭, `1..18` = 动态特效, `15` = 静态单色, `19` = 自定义单键模式)
* `Byte 2`: 调色板索引 (`0..10`)
* `Byte 3`: 倒序速度值 (`4 - speed`，取值 `0` 最慢 .. `4` 最快)
* `Byte 4`: 亮度 (`0` = 熄灭 .. `5` = 最高亮度)
* `Bytes 5..41`: 保留字节（填 `0x00`）

### `Report 0x0B` 平面 RGB 结构 (379 字节):
* `Byte 0`: `0x0B` (Report ID)
* `Bytes 1..126`: **红色通道 (R)**（按键 0..125 的红色亮度 `0x00`..`0xFF`）
* `Bytes 127..252`: **绿色通道 (G)**（按键 0..125 的绿色亮度 `0x00`..`0xFF`）
* `Bytes 253..378`: **蓝色通道 (B)**（按键 0..125 的蓝色亮度 `0x00`..`0xFF`）

---

## 5. 灯效模式与调色板定义

### 动态模式列表 (0 .. 20):
| 模式 | 官方名称 | 说明 |
|---|---|---|
| **0** | `Off` | 关闭全部灯光 |
| **1** | `Wave` | 彩虹横向流动波浪 |
| **2** | `Breathing` | 呼吸灯渐隐渐现 |
| **3** | `Ripple` | 击键涟漪扩散 |
| **4** | `Raindrops` | 随机雨滴散落 |
| **5** | `Neon Stream` | 霓虹光流 |
| **6** | `Stars` | 星光闪烁 |
| **7** | `Snake` | 贪吃蛇穿梭 |
| **8** | `Spectrum Cycle` | 全局色彩循环渐变 |
| **9** | `Vortex` | 旋风光涡 |
| **10** | `Meteor` | 横向流星拖尾 |
| **11** | `Aurora` | 极光微光波浪 |
| **12** | `Windmill` | 风车旋转 |
| **13** | `Ripple Bloom` | 环形向外绽放 |
| **14** | `Laser` | 击键横向激光发射 |
| **15** | **`Solid`** | **全局静态单一纯色** |
| **16** | `Wave Break` | 汇聚对冲光波 |
| **17** | `Glow` | 恒光点缀 |
| **18** | `Sparks` | 火花飞舞 |
| **19** | **`Customize`** | **真正的 XRAM 单键独立自定义 RGB** |
| **20** | `Rainbow Ripple` | 高速彩虹涟漪 |

### 调色板索引:
`0: 红` | `1: 橙` | `2: 黄` | `3: 绿` | `4: 青` | `5: 蓝` | `6: 紫` | `7: 白` | `8: 炫彩` | `9: 动态变换1` | `10: 动态变换2`

---

## 6. macOS 原生应用 (`CIYX79Control.app`)

采用纯原生 Objective-C 与 Cocoa 框架打造，零第三方重量级运行时依赖：
* **解耦编辑机制:** 在画布上点击调色时，所有变动仅停留在 UI 内存层，完全不产生无意义的 USB 刷写通信，直至点击发送。
* **丰富预设:** WASD 游戏高亮、方向键高亮、全红、赛博朋克、纯白办公等。
* **原生 macOS 取色板 (`NSColorWell`):** 支持无级 24 位 1677 万色精确定制。
* **JSON 配置文件导出与导入 (`⌘E` / `⌘O`):** 轻松备份灯效配置并在多台 Mac 间同步。
* **IOKit 热插拔实时监听:** 拔出再插上 USB 线后，驱动将自动重新建立连接并重新同步当前灯效。
* **应用焦点自动切换与 macOS 状态栏托盘 (Auto-Trigger & Menu Bar Extra):**
  * **菜单栏托盘图标 (`NSStatusItem`):** 程序在系统状态栏右上角常驻原生自适应键盘图标。关闭主界面（`⌘W` 或关闭红点）后，应用无感常驻后台（0.0% CPU 占用）。
  * **菜单栏快捷控制:**
    * 实时连接状态显示 (*CIY X79: Verbunden / Getrennt*)
    * *“打开主窗口...”* 随时一键唤回图形界面（如窗口已最小化到程序坞，将自动解除最小化 `deminiaturize`）
    * 快速点选切换所有灯效配置（当前生效配置带有勾选标记，显示如 `[Steam]` 等联动标签）
    * 快速开启/停用应用联动 (`⚡ Auto-Trigger`)
    * 完全退出应用程序 (`⌘Q`)
  * **应用绑定与冲突自动消除:** 可将任意灯效配置绑定到具体游戏或软件（如 Steam、CS:GO、VS Code）。若将已绑定的应用指定给新配置，系统会自动清理旧配置上的绑定，确保 1:1 唯一联动。
  * **100% 纯用户态与安全保障:** 基于 macOS 官方 `NSWorkspaceDidActivateApplicationNotification` 机制，**无需** 辅助功能与输入监控权限，零键盘监听。
  * **250ms 智能防抖 (Debouncing):** 防止 `⌘ + Tab` 快速切屏时反复刷写 USB 通道与按键闪烁。
  * **高容灾默认回退 (Fallback):** 切回桌面或未绑定的常用软件时，自动恢复默认设定（如“纯白办公”）。启动时会自动校验 Fallback 有效性并修复失效 ID。
  * **安全释放与退出 (`applicationWillTerminate:`):** 退出应用时完全注销 IOKit 热插拔监听端口并安全断开 USB 连接。

---

## 7. 终端命令行实用工具 (`ciy_ctl`)

适用于编写自动化脚本、Menubar 菜单栏小工具（SketchyBar、SwiftBar）以及 Alfred / Raycast 快捷插件：

```bash
# 查询当前状态
./ciy_ctl status
./ciy_ctl status --json

# 一键全局着色
./ciy_ctl fill "#0088FF" 5

# 载入 JSON 格式配置文件 (语法: ./ciy_ctl load <文件.json> [配置名称] [亮度 0..5])
./ciy_ctl load my_setup.json
./ciy_ctl load my_setup.json "Cyberpunk" 5 --json

# 快捷模式
./ciy_ctl wasd
./ciy_ctl wasd-arrows
./ciy_ctl all-red

# 原始参数底层配置: ./ciy_ctl set <模式> [颜色] [速度] [亮度]
./ciy_ctl set 15 0 1 5
```

---

## 8. 编译构建与自动化测试

### 1. 运行自动化测试套件
```bash
./scripts/run_tests.sh
```

### 2. 编译原生双架构应用（Apple Silicon & Intel）与 DMG 打包
```bash
# 编译 Universal Binary 应用与 ciy_ctl 工具
./scripts/build_app.sh

# 生成 DMG 安装镜像 (CIYX79Control-1.0.1.dmg)
./scripts/create_dmg.sh
```

---

## 9. 安全性与性能保障

* **免辅助功能与输入监控权限:** 本软件**不请求、不需要**任何辅助功能（Accessibility）或键盘输入监控权限，绝不拦截或记录用户的击键内容。
* **纯用户空间运行:** 仅使用 Apple 官方用户态 IOKit USB 接口，无任何内核扩展（Kext）或系统底层插件。
* **100% 离线安全:** 零网络套接字，零遥测上报，零第三方数据分析追踪。
* **0.0% CPU 待机占用:** 完全基于事件驱动，发包完毕后即刻休眠，不占用任何后台 CPU 资源。

---

## 10. 开源协议、商标声明与免责条款

### 开源许可证
本项目基于 **[MIT License](LICENSE)** 开源，允许任何个人或机构自由使用、修改和分发，但须保留原始版权声明。

### 商标声明 (Trademark Notice)
“CIY”、“Sinowealth（盛芯微）”、“Apple”、“macOS” 以及所有本文中提及的品牌名称、商标与硬件型号均为其各自权利人所有。在本文档与软件中对其进行引用，仅用于客观标明该程序所适用的硬件兼容性与互操作性范围（符合德国商标法 § 23 Abs. 1 Nr. 3 MarkenG 及相关合理使用原则）。本项目为开源社区独立自发研究项目，与上述硬件厂商及 Apple 公司**无任何商业合作、从属或背书关系**。

### 免责声明 (Liability Disclaimer)
使用本软件的风险由使用者自行承担。本软件直接通过 USB-HID 接口对键盘微控制器的易失性存储区（XRAM）进行写入配置。在法律允许的最大范围内（依据德国民法典 § 521 BGB 赠与法规定），作者责任仅限于故意和重大过失。作者不对因使用本软件可能导致的任何硬件故障或数据损失承担任何形式的保证或法律连带责任。
