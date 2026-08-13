# OLED Gateway 项目学习笔记 — 总索引

> 适用项目：`oled_prj`（STM32F407 + SSD1306 OLED 128×64 + 4 按键 + 串口上位机 + OTA 升级）
> 整理时间：2026-08-13 ｜ 目的：知识沉淀，便于复习与培训复用
> 项目代码：`stm32f407/`（固件）、`pc_host/`（上位机）、`rk3506/`（网关）、`tools/ota_tool/`（OTA 工具）

---

## 一、学习路线（建议顺序）

```
① STM32CubeMX 建工程与配置        ← 环境与地基
        ↓
② 开发板硬件资源与开发流程         ← 认识硬件，理解全链路
        ↓
③ 按键及菜单功能开发              ← 交互层，学习状态机
        ↓
④ 命令行（CLI）开发与使用          ← 调试手段，学习移植第三方库
        ↓
⑤ Bootloader 开发与使用           ← 进阶：分区、OTA、容错
```

> 建议先通读 ①② 建立整体认知，再按 ③④⑤ 深入实现细节。

---

## 二、分篇索引

| 序号 | 主题 | 文档 | 核心收获 |
|:---:|------|------|----------|
| 01 | STM32CubeMX 使用及技巧 | [01-stm32cubemx.md](./01-stm32cubemx.md) | 时钟树、外设配置、代码生成、USER CODE 保护 |
| 02 | Bootloader 开发及使用 | [02-bootloader.md](./02-bootloader.md) | A/B 双槽位、启动决策、OTA 协议、NOINIT 请求 |
| 03 | 命令行 CLI 开发及使用 | [03-cli.md](./03-cli.md) | nr_micro_shell 移植、环形缓冲区、命令表 |
| 04 | 开发板硬件资源及开发流程 | [04-hardware-and-workflow.md](./04-hardware-and-workflow.md) | 硬件清单、引脚表、分层架构、全链路流程 |
| 05 | 按键及菜单功能开发 | [05-keys-and-menu.md](./05-keys-and-menu.md) | 消抖、长按检测、菜单状态机、导航栈 |

---

## 三、核心速查卡

### 3.1 引脚速查

| 外设 | 引脚 | 功能 |
|------|------|------|
| USART1 | PA9 / PA10 | 与 PC 上位机通信（115200 8N1） |
| USART2 | PA2 / PA3 | 调试串口 / CLI 终端 |
| I2C2 | PB10 / PB11 | SSD1306 OLED（400kHz，地址 0x3C） |
| KEY1~KEY4 | PE1~PE4 | 菜单按键（上拉输入，按下低电平） |
| LED | PF9 | 状态指示（低电平点亮） |

### 3.2 Flash 分区速查（1024KB）

| 区域 | 地址 | 大小 |
|------|------|------|
| Bootloader | 0x08000000 | 32KB（S0~S1） |
| Slot A | 0x08008000 | 352KB（S2~S6） |
| Slot B | 0x08060000 | 384KB（S7~S9） |
| fw_info | 0x080C0000 | 128KB 扇区（S10） |
| sys_config | 0x080E0000 | 128KB 扇区（S11） |

### 3.3 关键技术点速查

| 技术点 | 要点 |
|--------|------|
| 系统时钟 | HSE 8MHz → PLL(M8, N336, P2) → 168MHz，APB1=42MHz，APB2=84MHz，Flash 5WS |
| 按键消抖 | 20ms 采样 × 3 次确认 = 60ms；长按 2000ms；连发间隔 150ms |
| OTA 触发 | SRAM NOINIT（0x2001BFF0，魔数 0x4F544152）+ KEY1 上电 + 双槽失效 |
| APP 跳转 | 校验 SP/PC 合法 → 关中断 → 清 SysTick → 设 MSP → 改 VTOR → 跳转 |
| 帧协议 | SOF(0xA5)+LEN+CMD+SEQ+DATA+CRC8+EOF(0x5A)，最大 257B |
| 菜单结构 | `menu_item_t` 静态 const 树，深度 ≤ 8，导航栈保存每层光标 |

---

## 四、各篇通用学习建议

1. **边读边动手**：每篇末尾有「自测题」，建议先做题再对照答案（答案就在正文中）。
2. **对照源码**：正文中标注了对应源码路径（如 `src/key_drv.c`），请打开文件同步阅读。
3. **实验优先**：CLI 篇和 Bootloader 篇强烈建议在真实硬件上复现，纸上谈兵效果减半。
4. **踩坑即记录**：本项目通过大量踩坑沉淀出经验，请重视每篇的「常见坑与经验」章节。

---

## 五、进阶资料（项目 docs/ 下的设计文档）

| 设计文档 | 内容 |
|----------|------|
| [docs/bootloader-design.md](../../docs/bootloader-design.md) | Bootloader 完整设计手册 |
| [doc/bootloader_ota_trigger_guide.md](../bootloader_ota_trigger_guide.md) | OTA 升级触发 5 种方案对比 |
| [docs/menu-design.md](../../docs/menu-design.md) | 菜单系统方案设计 |
| [docs/menu-key-design.md](../../docs/menu-key-design.md) | 菜单与按键详细设计 |
| [docs/cli-integration.md](../../docs/cli-integration.md) | CLI 移植文档 |
| [docs/stm32cubemx-f407-hal-setup.md](../../docs/stm32cubemx-f407-hal-setup.md) | CubeMX 建工程操作文档 |
| [docs/oled-display-tutorial.md](../../docs/oled-display-tutorial.md) | OLED 显示原理教程 |
| [docs/protocol-uart.md](../../docs/protocol-uart.md) | UART 二进制帧协议 v3.2 |
| [docs/ota-tool-design.md](../../docs/ota-tool-design.md) | OTA 上位机工具设计 |
