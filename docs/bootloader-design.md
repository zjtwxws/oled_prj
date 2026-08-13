# STM32F407 A/B 双分区 Bootloader 详细设计与工程使用手册

> 版本: V3.0 | 日期: 2026-08-10 | 最后更新: 2026-08-13 | 适用: oled_prj 项目
---

## 0. A/B 双槽位使用指南（必读）

### 0.1 核心原理

A/B 双槽位是**同一份代码编译出两个不同链接地址的固件**，用于实现容错升级：

```
Slot A (0x08008000) <- app_slot_a.bin  (编译时 -DAPP_SLOT_A, VTOR=0x08008000)
Slot B (0x08060000) <- app_slot_b.bin  (编译时 -DAPP_SLOT_B, VTOR=0x08060000)
```

两个固件功能完全相同，仅链接地址不同。**不要把 app_slot_b.bin 写入 Slot A，反之亦然。**

### 0.2 为什么需要 A/B？

- 升级时**不覆盖正在运行的固件**，而是写入另一个槽
- 下载过程断电、CRC 校验失败 -> 旧槽仍是 active，设备正常启动
- 新固件有问题 -> KEY1 开机可强制进入 Bootloader 重刷
- **永不"变砖"**

### 0.3 Slot A vs Slot B 什么时候用？

| 场景 | 当前 active | 升级目标 | 使用命令 |
|------|------------|---------|---------|
| 首次烧录 | 无 | Slot A | `make flash_slot_a` |
| 日常升级 | Slot A | Slot B | `python ota_tool.py COM3 app_slot_b.bin` |
| 下次升级 | Slot B | Slot A | `python ota_tool.py COM3 app_slot_a.bin` |
| 再次升级 | Slot A | Slot B | `python ota_tool.py COM3 app_slot_b.bin` |

**规律**：每次 OTA 的目标槽与当前运行槽相反，交替进行。

### 0.4 正确升级流程

**步骤 1：编译两个槽的固件**

```bash
cd stm32f407
make app_slot_a    # -> build/app_slot_a.bin (链接 0x08008000)
make app_slot_b    # -> build/app_slot_b.bin (链接 0x08060000)
```

**步骤 2：确定当前运行槽**

查看 USART2 调试日志，如：`active=slotA` 或 `active=slotB`。

**步骤 3：升级到非活跃槽**

| 当前运行 | 升级命令 |
|---------|---------|
| Slot A | `python ota_tool.py COM3 build/app_slot_b.bin` |
| Slot B | `python ota_tool.py COM3 build/app_slot_a.bin` |

> **关键规则：工具自动从固件向量表检测槽位，只需用对应槽位的 .bin 文件即可。**
> 若自动检测失败（如裸 .bin 不含向量表），可用 `--force-slot 0` 或 `--force-slot 1` 手动指定。

### 0.5 常见错误

| 错误操作 | 现象 | 原因 |
|---------|------|------|
| `ota_tool.py COM3 app_slot_a.bin --force-slot 1` | APP 启动卡死 | A 固件（VTOR=0x08008000）被强行写入 Slot B（0x08060000），中断向量不匹配 |
| `ota_tool.py COM3 app_slot_b.bin --force-slot 0` | APP 启动卡死 | B 固件（VTOR=0x08060000）被强行写入 Slot A（0x08008000），中断向量不匹配 |

> **V2.2 起工具已自动从固件向量表检测槽位，不再出现固件与槽位不匹配的情况。以上错误仅在 `--force-slot` 强行指定错误槽位时发生。**

> **两种错误的本质是一样的：固件的链接地址与实际烧录地址不匹配，导致 VTOR 指向的位置没有有效中断向量表。**


## 1. 概述

为 STM32F407VG 实现支持 OTA 固件升级的 Bootloader, 采用 A/B 双槽位架构:

- 每次升级写入**非活跃槽**, 校验通过后切换活跃槽
- 升级失败 (断电 / CRC 错误) 时自动保持旧版本运行, **永不"变砖"**
- 固件二进制与槽位强绑定：A 固件（VTOR=0x08008000）只能写 Slot A，B 固件（VTOR=0x08060000）只能写 Slot B
- Bootloader 与 APP 通过 USART1 (115200 8N1) 使用二进制帧协议通信

**当前 APP 规模**: ROM 56.74 KB, SRAM 9.13 KB (含 ZI)。

---

## 2. Flash 分区 (STM32F407VG, 1024KB)

```
扇区    地址范围                   大小      用途
─────────────────────────────────────────────────────
S0      0x08000000 - 0x08003FFF    16KB   ┐
S1      0x08004000 - 0x08007FFF    16KB   ├ Bootloader (32KB)
─────────────────────────────────────────────────────
S2      0x08008000 - 0x0800BFFF    16KB   ┐
S3      0x0800C000 - 0x0800FFFF    16KB   │
S4      0x08010000 - 0x0801FFFF    64KB   ├ Slot A (352KB)
S5      0x08020000 - 0x0803FFFF   128KB   │ base = 0x08008000
S6      0x08040000 - 0x0805FFFF   128KB   ┘
─────────────────────────────────────────────────────
S7      0x08060000 - 0x0807FFFF   128KB   ┐
S8      0x08080000 - 0x0809FFFF   128KB   ├ Slot B (384KB)
S9      0x080A0000 - 0x080BFFFF   128KB   ┘ base = 0x08060000
─────────────────────────────────────────────────────
S10     0x080C0000 - 0x080DFFFF   128KB     固件信息区 (fw_info)
S11     0x080E0000 - 0x080FFFFF   128KB     sys_config (APP 使用, 不动)
─────────────────────────────────────────────────────
总计                            1024KB
```

**容量验证**: APP 57KB, Slot A/B 均有 6x 以上余量。S10 整体划给 fw_info 仅因扇区是最小擦除单元, 实际仅用 `sizeof(fw_info_t)` 字节。

---

## 3. 固件信息区 (S10, 0x080C0000)

```c
typedef struct {
    uint32_t magic;          // 0x4657494E "FWIN"
    uint8_t  active_slot;    // 0=A, 1=B
    uint8_t  slot_a_state;   // 0x01=有效, 0xFF=无效
    uint8_t  slot_b_state;
    uint8_t  ota_request;    // (V3.0: 不再用于启动决策, 保留兼容)
    uint32_t slot_a_size;    // A 槽固件大小
    uint32_t slot_a_crc;     // A 槽 CRC32 (IEEE 802.3)
    uint32_t slot_a_version; // A 槽版本号 (如 0x00010003 = V1.0.3)
    uint32_t slot_b_size;
    uint32_t slot_b_crc;
    uint32_t slot_b_version;
    uint32_t crc32;          // 结构体自身 CRC32 (sizeof(fw_info_t) - 4 字节)
} fw_info_t;
```

> **注意**: `ota_request` 字段在当前 `fw_info_t` 结构体中保留，但 Bootloader
> 启动决策的"进入升级模式"触发已迁移为 **SRAM NOINIT 方案**（见 §8.1）。
> 该字段不再用于 Bootloader 的启动路径决策。

**设计要点**:
- `slot_x_state = 0xFF` (Flash 擦除后默认值) 表示无效, `0x01` 表示有效
- Bootloader 独占管理 S10, APP 不应直接写入 S10 扇区
- **当前写入策略**: `fw_info_save()` 每次先擦除 S10，再写入 Slot 0，并逐字读回校验
- `fw_info_load()` 扫描 S10，取 magic + crc32 均正确的记录。为兼容旧数据，扫描逻辑仍保留，但正常保存路径只写 Slot 0

---

## 4. 启动流程

```
上电/复位
   │
   ▼
Bootloader (0x08000000)
   │
   ├─ HAL_Init → SystemClock_Config (168MHz) → MX_GPIO_Init
   ├─ MX_USART2_UART_Init                         (调试串口)
   ├─ boot_oled_init()                            (GPIO 位控 I2C 初始化 SSD1306)
   ├─ MX_USART1_UART_Init                         (与 PC 通信, 115200)
   ├─ LED 闪烁 2 次 (500ms 周期) 指示启动
   │
   ├─ 读取 S10 fw_info
   │   ├─ magic 无效 → 先擦除 S10 → 初始化 fw_info → 两槽标记无效
   │   └─ magic 正确 → 加载到 RAM
   │
   ├─ 检查 SRAM NOINIT 区域 OTA 升级请求? ──是──→ 清除请求标志 → enter_update_mode()
   │
   ├─ KEY1 (PE1) 是否按下? ──是──→ enter_update_mode()
   │
   ├─ active_slot 状态有效且 CRC32 正确? ──→ 跳转 APP
   ├─ 备用槽有效且 CRC32 正确? ──→ 切换 active_slot → 跳转 APP
   └─ 两槽均无效 ──→ enter_update_mode() (LED 快闪 4 次)

enter_update_mode():
   ├─ OLED 显示"进入升级模式", LED 快闪 3 次 (150ms)
   ├─ 等待 CMD_OTA_START → 擦除目标槽 → OLED 显示"擦除中..." → ACK
   ├─ 循环接收 CMD_OTA_DATA → 写入 Flash → OLED 显示进度条 (每 64KB 刷新) → ACK
   ├─ 收到 CMD_OTA_FINISH:
   │   ├─ CRC32 校验, OLED 显示"校验中..."
   │   ├─ 通过 → fw_info_activate_slot() 写入有效 magic/槽信息 → OLED"升级完成"→"启动APP"→ 跳转
   │   └─ 失败 → NAK(NAK_OTA_CRC), 状态回到 UPD_IDLE, 等待重试
   └─ 收到 CMD_OTA_ABORT → 退出更新模式, 重新执行启动决策
```

### 4.1 启动决策逻辑（择优启动）

Bootloader 不是简单"记住上次用哪个槽"，而是**每次上电对两个槽独立校验，择优启动**：

```
1. active_slot 状态 = VALID 且全量 CRC32 通过 -> 直接跳转 active_slot
2. active_slot 校验失败                  -> 尝试备用槽
   a. 备用槽状态 = VALID 且 CRC32 通过   -> 自动切 active_slot 为备用槽 -> 跳转
   b. 备用槽也失败                       -> 进入升级模式等固件
```

- **"上次升级成功的槽就是 active_slot"** — OTA 完成时调用 `fw_info_activate_slot()` 同时标记 VALID 和设为 active
- **"active_slot 不是铁饭碗"** — 每次上电全量 CRC32 校验，Flash 数据退化或意外改写时自动切到备用槽
- **"两槽同坏才变砖"** — 只要有一个槽 CRC32 正确就能启动，只有两个都坏了才进入升级模式等待固件

此逻辑在 `boot_main.c` 的 `main()` 函数中实现，对应的 OTA 完成后跳转也调用同样的校验流程。


---

## 5. OTA 协议

### 5.1 帧格式 (自包含实现, 与 APP 协议帧格式一致但无 SEQ 字段)

```
SOF(0xA5) | LEN(1B) | CMD(1B) | DATA(0~251B) | CRC8(1B) | EOF(0x5A)
```

LEN 仅计 DATA 段字节数, 不含 CMD。CRC-8-ATM (多项式 0x07), 覆盖 SOF~DATA。
Bootloader 协议引擎是独立实现 (`boot_proto.c`), 6 状态机 (无 SEQ 状态), 不与 APP 的 `protocol.c` 共享代码。

### 5.2 OTA 专用命令

**PC → Bootloader**:

| CMD | 名称 | DATA 格式 | 说明 |
|-----|------|-----------|------|
| `0x07` | `CMD_OTA_START` | `[slot:1B][size:4B LE][crc32:4B LE][ver:4B LE]` | 开始升级, 13 字节 |
| `0x08` | `CMD_OTA_DATA` | `[offset:4B LE][payload:≤200B]` | 数据块, N+4 字节 |
| `0x09` | `CMD_OTA_FINISH` | 无 (LEN=0) | 传输完成, Bootloader 校验 CRC32 |
| `0x0A` | `CMD_OTA_ABORT` | 无 (LEN=0) | 取消升级 |

**Bootloader → PC**:

| CMD | 名称 | 说明 |
|-----|------|------|
| `0xF0` | ACK | 成功 |
| `0xFF` | NAK + `[code:1B]` | 失败: 见下方错误码 |

**NAK 错误码全集**:

| 错误码 | 宏名 | 含义 |
|:-----:|------|------|
| `0x01` | `NAK_CRC_ERROR` | CRC 校验失败 |
| `0x02` | `NAK_UNKNOWN_CMD` | 未知命令或当前状态下不允许 |
| `0x03` | `NAK_PARAM_ERROR` | 参数错误 (长度不足/槽号无效) |
| `0x04` | `NAK_FLASH_ERROR` | Flash 写入失败 |
| `0x05` | `NAK_BUSY` | 系统忙 (UPD_DONE 状态下收到命令) |
| `0x06` | `NAK_OTA_OFFSET` | 偏移越界 |
| `0x07` | `NAK_OTA_CRC` | OTA CRC32 校验失败 |
| `0x08` | `NAK_OTA_ERASE` | Flash 擦除失败 |

### 5.3 OTA 时序

```
PC                               Bootloader
 │                                    │
 ├─ OTA_START(slot,size,crc,ver) ────► 擦除目标槽 → ACK
 │                                    │
 ├─ OTA_DATA(offset=0, payload) ─────► 写 Flash → ACK
 ├─ OTA_DATA(offset=200, payload) ───► 写 Flash → ACK
 │         ... 循环 ...                │
 ├─ OTA_DATA(offset=N, payload) ─────► 写 Flash → ACK
 │                                    │
 ├─ OTA_FINISH ──────────────────────► CRC32 校验
 │◄── ACK (成功)                      │ fw_info_activate_slot() → 跳转
```

### 5.4 CRC32 算法

IEEE 802.3, 多项式 0xEDB88320 (反射), 初始值 0xFFFFFFFF, 结果取反。
计算范围: APP 槽全部固件字节 (size 由 OTA_START 指定)。

---

## 6. Bootloader 代码结构

```
stm32f407/iap/
├── src/
│   ├── boot_main.c         主入口, 启动决策, OTA 状态机, APP 跳转, boot_vsnprintf
│   ├── boot_fw_info.c      S10 fw_info 管理 (整扇擦除/写入/CRC)
│   ├── boot_flash.c        Flash 擦写 + CRC32 计算 (IEEE 802.3)
│   ├── boot_proto.c        简化协议帧解析/构建 (6 状态机, 无 SEQ)
│   ├── boot_oled.c         GPIO 位控 I2C SSD1306 驱动 + 精简字库 + 进度条
│   ├── gpio.c              CubeMX 复制: MX_GPIO_Init
│   ├── usart.c             CubeMX 复制: MX_USART1_UART_Init, MX_USART2_UART_Init
│   ├── stm32f4xx_hal_msp.c CubeMX 复制: HAL_MspInit
│   ├── stm32f4xx_it.c      CubeMX 复制: 中断服务 (SysTick/USARTx)
│   └── system_stm32f4xx.c  CubeMX 复制: SystemInit
├── inc/
│   ├── boot_fw_info.h      fw_info 结构体 + 分区常量 + API
│   ├── boot_flash.h        Flash 操作 API + 扇区宏
│   ├── boot_proto.h        帧常量 + 命令码 + 错误码 + 帧结构 + API
│   ├── boot_oled.h         OLED 显示 API (status / progress)
│   ├── ota_req.h           SRAM NOINIT OTA 请求结构体 + API 声明
│   └── main.h, gpio.h, usart.h, stm32f4xx_hal_conf.h, stm32f4xx_it.h
├── startup/
│   └── startup_stm32f407xx.s   启动文件 (CubeMX 复制)
├── bootloader.uvprojx       Keil MDK 工程文件
├── build/                   GCC Makefile 编译产物
├── DebugConfig/             Keil 调试配置
├── Listings/                Keil map 文件
└── Objects/                 Keil 编译产物
```

**Bootloader 依赖**: 仅 HAL 库 + oled_cubemx 硬件初始化文件副本, 不引用任何 `stm32f407/src/` 下的 APP 代码。

另外 `stm32f407/` 根目录下包含链接脚本和散列文件:

```
stm32f407/
├── Makefile                     GCC 编译 (含 bootloader, app_slot_a, app_slot_b)
├── oled_cubemx_slota.sct        Slot A Keil 散列文件 (ROM 0x08008000)
├── oled_cubemx_slotb.sct        Slot B Keil 散列文件 (ROM 0x08060000)
├── src/app_fw_info.c            APP 侧 OTA 请求入口 (→ ota_req_set_update)
├── src/ota_req.c                APP 侧 SRAM NOINIT OTA 请求实现
├── inc/app_fw_info.h            APP 侧 fw_info 头文件
├── inc/ota_req.h                SRAM NOINIT OTA 请求公共头文件
└── bootloader/
    ├── STM32F407VGTx_FLASH_BOOT.ld   Bootloader 链接脚本 (RAM: 128K-16)
    ├── STM32F407VGTx_FLASH_SLOTA.ld  APP Slot A 链接脚本 (RAM: 128K-16)
    └── STM32F407VGTx_FLASH_SLOTB.ld  APP Slot B 链接脚本 (RAM: 128K-16)
```

---

## 7. APP 侧改动

| 文件 | 改动 |
|------|------|
| `user_app.h` | 新增 `APP_VTOR_ADDR` 宏 (由 `APP_SLOT_A` / `APP_SLOT_B` 控制) |
| `main.c` (USER CODE SysInit) | 新增 `SCB->VTOR = APP_VTOR_ADDR` |
| `user_app.c` | 新增 `CMD_OTA_RESERVED` 处理 — 写 NOINIT ota_req → ACK → 复位 |
| `app_fw_info.h/c` | APP 侧 OTA 请求入口，调用 `ota_req_set_update()` |
| `ota_req.h` | SRAM NOINIT OTA 请求公共头文件 |
| `ota_req.c` | APP 侧 OTA 请求实现 — 写入 SRAM 0x2001BFF0 |
| `protocol.h` | 新增 `CMD_OTA_DATA/FINISH/ABORT` 和 `NAK_OTA_*` 常量 |
| `Makefile` | 新增 `bootloader`, `app_slot_a`, `app_slot_b` 三个 target (见 §12) |

**编译命令**:
```bash
make bootloader    # 编译 Bootloader → build/bootloader.bin
make app_slot_a    # 编译 Slot A APP → build/app_slot_a.bin
make app_slot_b    # 编译 Slot B APP → build/app_slot_b.bin
make               # 编译全部
```

---

## 8. 进入 Bootloader 的条件

### 8.1 SRAM NOINIT OTA 请求机制 (V3.0 新增)

V3.0 将 APP 到 Bootloader 的 OTA 升级请求从 Flash fw_info 区迁移到 **SRAM NOINIT 保留区域**：

- **原理**: STM32F407 SRAM 在系统复位 (`NVIC_SystemReset`) 后内容保持，但上电复位 (POR) 后会丢失。
  这与 OTA 请求语义完全匹配——断电后重新上电极应正常启动 APP，不进入升级模式。
- **地址**: `0x2001BFF0`，位于 SRAM1 末端 16 字节，已从链接脚本的 RAM 范围中排除。
- **结构体** (16 字节, 4 字节对齐):

```c
typedef struct {
    uint32_t magic;    // 魔数 0x4F544152 ("OTAR") — 防误触发
    uint32_t request;  // 请求类型: 0=无请求, 1=请求升级
    uint32_t slot;     // 目标槽位 (预留)
    uint32_t reserved; // 保留扩展
} ota_req_t;
```

- **APP 侧流程**: 上位机通过 USART1 发送 `CMD_OTA_RESERVED (0x07)` →
  APP `user_app.c` 收到后调用 `app_fw_info_set_ota_request()` →
  `ota_req_set_update()` 写入魔数和请求 →
  `sys_config_reset()` 触发系统复位
- **Bootloader 侧流程**: `main()` 先调用 `fw_info_load()` 初始化 RAM 中的 fw_info，
  再调用 `ota_req_is_update()` 检查 SRAM 0x2001BFF0 地址 → 若魔数 & 请求均合法 →
  `ota_req_clear()` 清除标志 → 直接进入升级模式。
  Bootloader 侧的 `ota_req_is_update()` / `ota_req_clear()` 为内联实现
  （`boot_main.c` 内），避免 Keil 工程额外添加 `.c` 文件
- **链接脚本**: 三个链接脚本 (BOOT / SLOTA / SLOTB) 的 RAM 均缩小 16 字节
  (`LENGTH = 128K - 16`)，确保 0x2001BFF0~0x2001BFFF 不被栈/堆/全局变量覆盖
- **参考方案**: MCUBoot / OpenBLT / Zephyr 等开源 Bootloader 均采用 retained memory 传递复位标志

### 8.2 触发条件与优先级

| 优先级 | 条件 | 实现 |
|--------|------|------|
| 1 | **SRAM NOINIT OTA 请求** | APP 写 `ota_req_set_update()` → `sys_config_reset()` → Bootloader 检测合法请求 → 进入升级模式 |
| 2 | **KEY1 (PE1) 上电时按下** (低电平) | Bootloader 初始化后检测 GPIO, 不需要长按, 开机时按住即可 |
| 3 | **两槽均无效** | Bootloader 自动进入更新模式 (LED 快闪 4 次, 等待固件下发) |

> **注意**: 原设计文档中的 "KEY1+KEY2 同时长按 3s" 已简化为单一 KEY1 开机检测。

---

## 9. OLED 显示 (Bootloader 内置)

Bootloader 内置了精简的 SSD1306 OLED 驱动, 通过 GPIO 位控 I2C 操作 PB10(SCL)/PB11(SDA)。
不依赖 HAL I2C 模块, 约 ~100kHz 软件模拟 I2C 时钟。

### 9.1 字库

- **ASCII 8x16**: 95 个可打印字符 (0x20~0x7E)
- **中文 16x16**: 19 个升级状态所需汉字:
  进、入、升、级、模、式、擦、除、中、正、在、下、载、完、成、启、动、校、验

### 9.2 显示 API

```c
void boot_oled_init(void);                              // 初始化 SSD1306 OLED
void boot_oled_clear(void);                              // 清空显存缓冲区
void boot_oled_flush(void);                              // 刷新缓冲区到 OLED
void boot_oled_status(const char *text);                 // 居中显示一行文本并刷新
void boot_oled_progress(uint32_t done, uint32_t total);  // 显示进度条 + 百分比
```

### 9.3 升级过程 OLED 显示

| 阶段 | 说明 |
|------|------|
| 进入升级模式 | OLED 居中显示"进入升级模式", LED 快闪 3 次 (150ms) |
| 擦除目标槽 | OLED 显示"擦除中..." |
| 数据下载 | OLED 显示"正在下载" + 进度条 + 百分比 (每 64KB 刷新一次) |
| 完成校验 | OLED 显示"校验中..." |
| 升级成功 | OLED 显示"升级完成" (LED 亮 1s) → "启动APP" (500ms) → LED 灭 → 跳转 APP |

---

## 10. 异常处理

| 场景 | 行为 |
|------|------|
| OTA 写入途中断电 | 旧槽不变, 下次上电自动启动旧版本 (NOINIT 数据在 POR 后丢失) |
| OTA CRC32 校验失败 | NAK → fw_info 不变 → 状态回到 UPD_IDLE → 等待重试 |
| 新固件可启动但有逻辑 bug | 可通过 KEY1 上电进入 Bootloader 重刷 |
| S10 fw_info 损坏 (CRC32 错) | Bootloader 初始化默认值 → 两槽标记无效 → 进入更新模式 |
| APP 跳转前: SP 不在 SRAM 范围 | 放弃跳转, 继续尝试备用槽 |
| APP 跳转前: PC 不在 Flash 范围 | 放弃跳转, 继续尝试备用槽 |

> **跳转安全校验**: 跳转前检查 SP ∈ [0x20000000, 0x2001C000] 和 PC ∈ [0x08000000, 0x080FFFFF], 非法则拒绝跳转并尝试备用槽或进入更新模式。

---

## 11. 上位机 OTA 工具

独立的上位机工具 `tools/ota_tool/`, 基于 Python + pySerial, 详见 [ota-tool-design.md](ota-tool-design.md)。

```bash
# 工具自动从固件向量表检测槽位
python tools/ota_tool/ota_tool.py COM3 app_slot_b.bin
```

---

## 12. 构建与烧录

### 12.1 Keil MDK (当前使用)

**Bootloader 工程**: 打开 `stm32f407/iap/bootloader.uvprojx`

| 项 | 值 |
|----|-----|
| 芯片 | STM32F407VG |
| ROM Start | 0x08000000, Size: 0x8000 (32KB) |
| RAM Start | 0x20000000, Size: 0x1C000 (112KB) |
| Preprocessor | `STM32F407xx, USE_HAL_DRIVER, BOOTLOADER` |
| Scatter File | `build/bootloader.sct` (自动生成) |
| 优化 | `-O2` |

**编译输出**: `build/bootloader.axf` / `build/bootloader.hex`

**APP 工程 (Slot A/B)**: 打开 `oled_cubemx/MDK-ARM/oled_cubemx.uvprojx`

| Target | ROM Start | Size | Preprocessor | Scatter File |
|--------|-----------|------|--------------|--------------|
| Slot A (默认) | 0x08008000 | 0x58000 | 无额外宏 | `oled_cubemx_slota.sct` |
| Slot B | 0x08060000 | 0x60000 | 加 `APP_SLOT_B` | `oled_cubemx_slotb.sct` |

### 12.2 GCC Makefile

```bash
cd stm32f407
make                    # 编译全部 3 个目标

make bootloader         # → build/bootloader.bin
make app_slot_a         # → build/app_slot_a.bin
make app_slot_b         # → build/app_slot_b.bin

make flash_boot         # 烧录 Bootloader 到 0x08000000
make flash_slot_a       # 烧录 APP 到 Slot A (0x08008000)
make flash_slot_b       # 烧录 APP 到 Slot B (0x08060000)
```

### 12.3 首次烧录流程

全新芯片首次烧录完整流程:

1. **烧录 Bootloader**: `st-flash write build/bootloader.bin 0x08000000`
2. **烧录 APP (Slot A)**: `st-flash write build/app_slot_a.bin 0x08008000`
3. **上电**: Bootloader 检测到两槽无效 → 初始化 fw_info → 进入更新模式
4. **用 OTA 工具升级到 Slot A**: `python tools/ota_tool/ota_tool.py COM3 build/app_slot_a.bin`
5. **正常启动**: 设备从 Slot A 启动运行

> **备选**: 如果使用 J-Link 或 ST-Link Utility, 可以直接烧录 Bootloader + APP 到对应地址, 无需 OTA 工具初始化。

### 12.4 OTA 升级流程 (日常使用)

> **V2.2：工具自动从固件向量表检测槽位，固件名对应的 .bin 直接使用即可。**
> 详见 [0 A/B 双槽位使用指南](#0-ab-双槽位使用指南必读)。

```bash
# === 从 Slot A 升级到 Slot B ===
# 1. PC 连接设备串口, 设备正常运行中
# 2. 编译两个槽的固件
make app_slot_a app_slot_b
# 3. 发送 OTA 命令让设备进入 Bootloader 模式, 然后执行升级
python tools/ota_tool/ota_tool.py COM3 build/app_slot_b.bin

# === 从 Slot B 升级到 Slot A (下次升级) ===
python tools/ota_tool/ota_tool.py COM3 build/app_slot_a.bin

# === 其他可选参数 ===
python tools/ota_tool/ota_tool.py COM3 firmware.bin --baud 460800    # 非标波特率
python tools/ota_tool/ota_tool.py COM3 firmware.bin --version 1.2.0  # 指定版本号
```

---

## 13. 测试用例

| 编号 | 场景 | 操作 | 预期 |
|------|------|------|------|
| T01 | 全新芯片首次上电 | 烧录 Bootloader 后上电 | 初始化 fw_info → 两槽无效 → 进入更新模式 |
| T02 | 正常 OTA Slot A→B | OTA 升级到 Slot B | 擦除 B → 写入 → CRC 通过 → active=B → 跳转 B |
| T03 | OTA 写入途中断电 | OTA 中拔电再上电 | A 槽仍有效 → 跳转 A 正常（NOINIT 数据在 POR 后丢失, 不进入升级模式） |
| T04 | OTA FINISH 后 CRC 错 | 固件 CRC 不匹配 | fw_info 不变 → 状态回到 UPD_IDLE → 可重试 |
| T05 | KEY1 上电进入 | 按住 KEY1 上电 | 强制进入更新模式（NOINIT 检查优先但此时无请求） |
| T06 | APP 收到 OTA 命令 | PC 发 CMD_OTA_RESERVED | APP 写 NOINIT ota_req (0x2001BFF0) → 复位 → Bootloader 识别 → 进入升级模式 |
| T07 | 两槽均无效 | 擦除 S2~S9 后上电 | Bootloader 等待固件, LED 快闪 4 次 |
| T08 | 活跃槽 CRC 损坏 | 手动破坏部分 Flash | 活跃槽 CRC 校验失败 → 自动切换到备用槽 |
| T09 | SP 非法跳过 | 人为损坏向量表 | 拒绝跳转, 尝试备用槽 |

---

## 14. 设计亮点与实现细节

### 14.0 SRAM NOINIT OTA 请求 (V3.0)

V3.0 将 OTA 请求通道从 Flash 迁移到 SRAM NOINIT 区域，解决了 Flash 方案的两个致命缺陷：

1. **Flash 编程方向违规**: Flash 只能从 1→0 编程，擦除后为 0xFF。`ota_request` 字段从 0x00→0x01 需要先擦除扇区，而 APP 不应擦除 Bootloader 管理的 S10。SRAM 无此约束，任意写入。
2. **CRC 破坏**: 写入 `ota_request` 单字节会破坏 `fw_info_t` 的 CRC32，需要重新计算整结构体 CRC 并写入，在 APP 侧实现复杂且易出错。SRAM 结构体独立于 fw_info，完全解耦。

另外，复位后 NOINIT 数据会在上电复位 (POR) 时自动丢失——这意味着断电重开不会误进入升级模式，符合预期行为。

参考方案: MCUBoot / OpenBLT / Zephyr 等开源项目均采用 retained memory / .noinit section 在复位间传递标志。

### 14.1 fw_info 存储与首次初始化

当前版本采用**单记录 + 整扇擦除**策略：

- `fw_info_save()` 先擦除 S10，再写入 Slot 0，并逐字读回校验
- `fw_info_load()` 仍扫描 S10 内可能存在的有效记录，以兼容历史数据；正常保存路径只写 Slot 0
- `fw_info_load()` 检测到 S10 无有效记录时，先擦除 S10，再写入默认 fw_info，避免在非空扇区直接编程导致失败

早期曾采用日志结构追加写入以减少擦除次数。后续 OTA 掉电排查中发现：
进入升级模式时若 RAM 中的 fw_info 尚未初始化，`fw_info_activate_slot()` 会保存
`magic == 0` 的记录，重启后 `fw_info_load()` 无法识别。为降低复杂度并保证启动一致性，
现改为每次保存都整扇擦除后写 Slot 0。

### 14.2 自定义 vsnprintf

ARMCC semihosting 在无调试器连接时, `printf` 会挂死 MCU。Bootloader 实现了轻量
`boot_vsnprintf`, 支持 `%s %d %u %x %X %02X %08X %% %c`, 避免依赖标准库的 I/O 实现。

### 14.3 OLED 位控 I2C

Bootloader 不依赖 HAL I2C 模块, 直接 GPIO 位控 PB10/PB11 模拟 I2C 时序。优势:
- 减少 HAL 模块依赖, 降低 Bootloader 体积
- 不依赖 CubeMX 生成的 I2C 初始化
- 约 100kHz 的软件 I2C 足够驱动 SSD1306

### 14.4 APP 跳转前安全校验

跳转前校验 SP 和 PC 地址范围, 防止跳转到损坏固件导致不可恢复的死机:

```c
if (app_sp < 0x20000000UL || app_sp > 0x2001C000UL) return;  // SP 不在 SRAM
if (app_pc < 0x08000000UL || app_pc > 0x080FFFFFUL) return;  // PC 不在 Flash
```

跳转前还执行: `__disable_irq()` → 停 SysTick → 关 USART2 时钟 → `__set_MSP(app_sp)` → `SCB->VTOR = app_base` → `__enable_irq()` → 跳转。

### 14.5 Flash 写入的未对齐尾部处理

`CMD_OTA_DATA` 的 payload 长度为 N 字节。代码先将 N/4 个完整 4 字节字写入, 剩余 `N%4` 字节采用**读-改-写**方式: 先读取目标地址所在字的当前值, 修改对应字节后再整字写回。确保非 4 字节对齐的尾部数据也能正确写入。

### 14.6 HAL 擦除 API 的使用

Flash 擦除使用标准 HAL API (`HAL_FLASH_Unlock` → `HAL_FLASHEx_Erase` → `HAL_FLASH_Lock`)。
当前 Keil AC6 工程中 `__RAM_FUNC` 实际为空宏，链接结果中 Flash 操作函数仍位于内部 Flash，
但 OTA 对 Slot A/Slot B 的擦写已验证可用。

### 14.7 fw_info_activate_slot — 原子激活

`fw_info_activate_slot()` 是 V3.0 新增的合并 API，将 `fw_info_set_slot_info()` +
`fw_info_set_slot_state()` + `fw_info_set_active_slot()` 三步操作合并为一次 Flash 保存，
减少 Flash 写入次数并保证原子性。

该函数会在保存前显式设置 `g_fw_info.magic = FW_INFO_MAGIC`。调用前必须先完成
`fw_info_load()`，确保 RAM 中的 fw_info 已初始化；当前启动流程已把 `fw_info_load()`
提前到 NOINIT/KEY1 检查之前。

### 14.8 OTA 后掉电无法启动的修复记录

问题现象：OTA 完成时 `fw_info_save()` 日志显示擦除和读回成功，APP 能运行，但 reboot 后
Bootloader 扫描 S10 显示 `no valid record`，随后首次初始化打印 `write FAILED`。

根因：

1. 通过 SRAM NOINIT 或 KEY1 进入升级模式时，`fw_info_load()` 尚未执行，
   `g_fw_info` 还是零初始化的 BSS。
2. 旧 `fw_info_activate_slot()` 未设置 `magic`，因此 OTA 完成时保存的是 `magic == 0` 的无效记录。
3. 重启后扫描只接受 `FW_INFO_MAGIC`，因此拒绝该记录。
4. 首次初始化在未擦除的 S10 上直接编程，旧数据中已有 0 位，无法按新数据改写，导致写失败。

修复内容：

- `main()` 先调用 `fw_info_load()`，再检查 NOINIT/KEY1
- `fw_info_activate_slot()` 显式设置 `magic`
- `fw_info_load()` 首次初始化前先擦除 S10

---

## 15. 调试

Bootloader 通过 USART2 (PA2/PA3, 115200 8N1) 输出调试日志。格式:
`[BOOT] function:line message`

在 `boot_main.c` 中定义 `BOOT_DEBUG_ENABLE` 启用调试输出。生产版本注释此行。

使用终端软件 (MobaXterm / SecureCRT), 波特率 115200 8N1, 连接 PA2(TX)/PA3(RX)/GND。

**示例输出**:
```
[BOOT] main:113 ========================================
[BOOT] main:114 STM32F407 Bootloader V3.0
[BOOT] main:115 build: Aug 11 2026 10:30:00
[BOOT] main:116 ========================================
[BOOT] main:124 IAP: Slot A (0x08008000) / Slot B (0x08060000)
[BOOT] main:128 loading fw_info from S10 (0x080C0000)...
[BOOT] main:130 fw_info_load: loaded OK
[BOOT] main:134 fw_info: active=slotA (0x08008000) a_state=0x01 a_ver=0x00010003 ...
[BOOT] main:155 IAP: Slot A (0x08008000) CRC OK, jumping...
[BOOT] boot_jump_to_app:250 jumping to APP at 0x08008000 (SP=0x20001000, PC=0x08008004)
```

---

**文档版本**: V3.0 | **创建日期**: 2026-08-10 | **最后更新**: 2026-08-13
