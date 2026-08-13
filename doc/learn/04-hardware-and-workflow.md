# 学习笔记 04 — 开发板硬件资源介绍及开发流程

> 以 `oled_prj` 项目为实例，系统梳理 STM32F407 开发板的硬件资源、引脚分配、系统架构与完整开发流程。
> 配套参考：[docs/stm32cubemx-f407-hal-setup.md](../../docs/stm32cubemx-f407-hal-setup.md)、README.md

---

## 一、硬件资源总览

### 1.1 核心芯片

| 项目 | 参数 |
|------|------|
| 芯片 | STM32F407VGTx |
| 内核 | ARM Cortex-M4F（带 FPU），最高 168MHz |
| Flash | 1MB |
| SRAM | 192KB（SRAM1 128KB + SRAM2 16KB + CCM 64KB） |
| 封装 | LQFP100 |

### 1.2 板载外设清单

| 外设 | 型号/规格 | 接口 | 在本项目中的角色 |
|------|-----------|------|------------------|
| OLED 显示屏 | 0.96" SSD1306，128×64 单色 | I2C | 主要显示设备 |
| 按键 ×4 | 轻触开关，按下接地 | GPIO（PE1~PE4） | 菜单导航 |
| LED ×1 | 用户指示灯 | GPIO（PF9） | 状态指示 |
| USB-TTL 串口 | 外部模块 | USART1（PA9/PA10） | 与 PC 上位机通信 |
| 调试串口 | 板载/外部 | USART2（PA2/PA3） | CLI 调试终端 |
| ST-Link | 调试器 | SWD（SWDIO/SWCLK） | 下载、调试 |

### 1.3 完整引脚分配表

| 外设 | 引脚 | 功能 | 备注 |
|------|------|------|------|
| USART1_TX | PA9 | UART 发送 → PC 上位机 | 115200 8N1 |
| USART1_RX | PA10 | UART 接收 ← PC 上位机 | |
| USART2_TX | PA2 | 调试 TX（CLI） | 全双工 |
| USART2_RX | PA3 | 调试 RX（CLI） | |
| I2C2_SCL | PB10 | OLED SCL | 400kHz |
| I2C2_SDA | PB11 | OLED SDA | 从机地址 0x3C |
| KEY1 | PE1 | GPIO Input，Pull-up | 按下低电平 |
| KEY2 | PE2 | GPIO Input，Pull-up | |
| KEY3 | PE3 | GPIO Input，Pull-up | |
| KEY4 | PE4 | GPIO Input，Pull-up | |
| LED | PF9 | GPIO Output，Push-Pull | 低电平点亮 |

> **硬件设计要点回顾**：按键用上拉（按下接地）；LED 低电平点亮（共阳接法）；4 个按键全部放 PE 端口是为了给 USART2 让出 PA2/PA3 实现全双工调试。

---

## 二、软件架构（分层设计）

### 2.1 为什么分层

裸机工程如果不分层，所有代码堆在一起：驱动、业务、协议互相纠缠，改一个功能牵一发动全身。本项目经过多轮迭代形成了清晰的分层架构：

```
┌─────────────────────────────────────────────┐
│          user_app (应用入口/编排层)            │  ← 主循环、初始化、事件分发
├─────────────────────────────────────────────┤
│  display_mgr │ led_mgr │ menu_mgr │ sys_config│  ← 应用模块层
│  debug_console                                 │
├─────────────────────────────────────────────┤
│  protocol │ font                              │  ← 协议/中间件层
├─────────────────────────────────────────────┤
│  ssd1306 │ key_drv │ uart_drv │ i2c_drv      │  ← 硬件驱动层
│  iwdg_drv │ sys_tick                          │
├─────────────────────────────────────────────┤
│              STM32 HAL                        │
└─────────────────────────────────────────────┘
```

### 2.2 各层职责

| 层 | 职责 | 代表文件 | 依赖 |
|----|------|---------|------|
| **应用入口** | 初始化、主循环、事件分发 | `user_app.c` | 所有下层 |
| **应用模块** | 业务逻辑（显示管理、菜单、LED） | `display_mgr.c`、`menu_mgr.c` | 驱动层 + 中间件 |
| **协议/中间件** | 帧协议、字库 | `protocol.c`、`font.c` | 驱动层 |
| **硬件驱动** | 直接操作外设寄存器/HAL | `ssd1306.c`、`key_drv.c`、`uart_drv.c` | STM32 HAL |
| **HAL 库** | 芯片抽象层 | ST 官方 | CMSIS |

### 2.3 分层铁律（AGENTS.md 中的强制约定）

- **应用层禁止直接调用 HAL/CMSIS 接口**（如 `NVIC_SystemReset`、`HAL_GPIO_WritePin`）
- 所有硬件操作必须经过驱动层封装：

| 操作 | 必须通过 |
|------|---------|
| 复位 | `sys_config_reset()`（sys_config.h） |
| GPIO 操作 | key_drv / led_mgr / i2c_drv |
| 看门狗 | iwdg_drv |
| Flash 操作 | sys_config |

**为什么？** 硬件替换时只需改驱动层，应用层零改动。例如 OLED 从 SSD1306 换成其他屏，只改 `ssd1306.c`，`menu_mgr` 完全不受影响。

---

## 三、开发流程（完整链路）

### 3.1 总体流程

```
① 需求分析 → ② 硬件接线 → ③ CubeMX 配置生成 → ④ 驱动开发 → ⑤ 业务开发 → ⑥ 上位机联调 → ⑦ 固件升级 → ⑧ 测试验收
```

### 3.2 本项目各阶段做了什么

| 阶段 | 具体工作 |
|------|---------|
| ① 需求分析 | 需求文档 v3.0→v3.1：新增本地/远程双模式 |
| ② 硬件接线 | STM32F407 + OLED(I2C) + 4 按键(PE1~4) + LED(PF9) + USB-TTL |
| ③ CubeMX 配置 | 时钟 168MHz、USART1/2、I2C2、GPIO、IWDG（见笔记 01） |
| ④ 驱动开发 | i2c_drv → ssd1306 → font → key_drv → uart_drv → led_mgr |
| ⑤ 业务开发 | display_mgr 双模式 → menu_mgr 菜单 → CLI → sys_config |
| ⑥ 上位机联调 | Win32 上位机：协议、GDI 预览、串口通信 |
| ⑦ 固件升级 | Bootloader A/B 槽 + OTA 工具（见笔记 02） |
| ⑧ 测试验收 | 按键/菜单/特效/OTA 各功能回归 |

### 3.3 驱动开发顺序（自底向上，每层可独立验证）

```
1. i2c_drv（总线驱动）      ← 先保证总线通：扫描设备地址
2. ssd1306（显示驱动）      ← 验证：点亮、清屏、画点
3. font（字库）             ← 验证：显示中文/ASCII
4. uart_drv（串口驱动）     ← 验证：回环收发
5. key_drv（按键驱动）      ← 验证：消抖、长按
6. led_mgr（LED 管理）     ← 验证：开关/闪烁
7. sys_tick（时间基准）     ← 供各模块计时
8. iwdg_drv（看门狗）       ← 最后使能，防干扰调试
```

**原则**：每写完一层驱动，立即写最小测试代码验证，**不要等全部写完再联调**——问题越早发现越好定位。

---

## 四、通信链路全景

### 4.1 本地模式（STM32 独立运行）

```
STM32F407 ──I²C── OLED 0.96" 128×64
    本地渲染文字 + 7 种特效（静态/左滚/右滚/上滚/下滚/翻页/淡入淡出）
    PC 断开时独立工作
```

### 4.2 远程模式（PC 全控）

```
Windows PC (Win32 上位机) ──USB-TTL── STM32F407 ──I²C── OLED
     PC 渲染帧缓冲（1024B）     USART1           SSD1306
     分段下发 ≤200B/段          115200 8N1       STM32 仅刷屏
```

### 4.3 可选：RK3506 Linux 网关

```
PC ──网络── RK3506 网关 ──UART── STM32F407 ──I²C── OLED
    HTTP/WebSocket/TCP      帧协议不变
```

网关作为**协议中继**，帧格式保持不变——上位机协议设计为与传输介质解耦的收益。

### 4.4 三条串口链路职责划分

| 串口 | 用途 | 数据特征 |
|------|------|---------|
| USART1 (PA9/PA10) | PC 上位机协议 / OTA | 二进制帧协议 |
| USART2 (PA2/PA3) | CLI 调试终端 | ASCII 文本 |
| （Bootloader 内） | 调试日志 | 同 USART2 |

---

## 五、构建与烧录流程

### 5.1 构建

```bash
cd stm32f407
make              # 默认构建 app_slot_a + app_slot_b + bootloader
```

### 5.2 首次烧录

```bash
make flash_boot     # Bootloader → 0x08000000
make flash_slot_a   # Slot A → 0x08008000
```

### 5.3 日常升级

```bash
python tools/ota_tool/ota_tool.py COM3 build/app_slot_b.bin
```

### 5.4 开发调试工具链

| 工具 | 用途 |
|------|------|
| STM32CubeMX | 配置外设生成初始化代码 |
| arm-none-eabi-gcc + Makefile | 编译 |
| st-flash | 命令行烧录 |
| Keil MDK | Bootloader 编译（备用） |
| MobaXterm/SecureCRT | CLI 终端（ANSI 支持） |
| VS2019 | 上位机开发 |

---

## 六、踩坑与经验总结

1. **引脚冲突是硬件规划第一课**：按键和串口抢引脚导致 UART 无法全双工——**先画完整引脚分配表再接线**
2. **驱动自底向上逐层验证**：不要一次性写完所有驱动再调，每层写最小验证
3. **协议与传输介质解耦**：帧协议在 USB-TTL、网关中继下都能用
4. **分层是"铁律"不是"建议"**：应用层直接调 HAL 短期内省事，长期是灾难
5. **低电平点亮/按下是常见硬件设计**：LED 共阳、按键接地，代码里注意取反逻辑

---

## 七、自测题

1. 本项目 4 个按键为什么全部放在 PE 端口？
2. 分层架构中，应用层能否直接调用 `HAL_GPIO_WritePin`？为什么？
3. 驱动开发推荐的自底向上顺序是什么？为什么？
4. 本地模式和远程模式的本质区别是什么？
5. RK3506 网关在系统中扮演什么角色？为什么帧格式可以不变？
6. OLED 从机 I2C 地址是多少？为什么 HAL 函数里地址要左移一位？

<details>
<summary>参考答案</summary>

1. 为给 USART2（PA2/PA3）腾出引脚实现全双工调试串口
2. 不能。应用层必须通过驱动层封装（如 led_mgr、key_drv），保证硬件替换时应用零改动
3. i2c_drv → ssd1306 → font → uart_drv → key_drv → led_mgr → sys_tick → iwdg_drv；每层可独立验证，问题早发现
4. 本地模式 STM32 自行渲染（脱离 PC 独立运行）；远程模式 PC 渲染帧缓冲下发，STM32 仅刷屏
5. 协议中继（HTTP/WebSocket/TCP ↔ UART），帧格式与传输介质解耦，所以不变
6. 0x3C（7-bit）；HAL 库内部按 8-bit 地址处理，需要 `0x3C << 1 = 0x78`

</details>
