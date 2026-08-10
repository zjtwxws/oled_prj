# STM32F407 A/B 双分区 Bootloader 设计说明书

> 版本: V1.0 | 日期: 2026-08-10 | 适用: oled_prj 项目

---

## 1. 概述

为 STM32F407VG 实现支持 OTA 固件升级的 Bootloader, 采用 A/B 双槽位架构:

- 每次升级写入**非活跃槽**, 校验通过后切换活跃槽
- 升级失败 (断电 / CRC 错误) 时自动保持旧版本运行, **永不"变砖"**
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
S11     0x080E0000 - 0x080FFFFF   128KB     sys_config (不动)
─────────────────────────────────────────────────────
总计                            1024KB
```

**容量验证**: APP 57KB, Slot A/B 均有 6× 以上余量。S10 整体划给 fw_info 仅因扇区是最小擦除单元, 实际仅用 ~64 字节。

---

## 3. 固件信息区 (S10, 0x080C0000)

```c
typedef struct {
    uint32_t magic;          // 0x4657494E "FWIN"
    uint8_t  active_slot;    // 0=A, 1=B
    uint8_t  slot_a_state;   // 0x01=有效, 0xFF=无效
    uint8_t  slot_b_state;
    uint8_t  ota_request;    // APP 设 1 请求进入 Bootloader
    uint32_t slot_a_size;    // A 槽固件大小
    uint32_t slot_a_crc;     // A 槽 CRC32 (IEEE 802.3)
    uint32_t slot_a_version; // A 槽版本号 (如 0x00010003 = V1.0.3)
    uint32_t slot_b_size;
    uint32_t slot_b_crc;
    uint32_t slot_b_version;
    uint32_t crc32;          // 结构体自身 CRC32
} fw_info_t;
```

**设计要点**:
- `slot_x_state = 0xFF` (Flash 擦除后默认值) 表示无效, `0x01` 表示有效
- `ota_request`: APP 在收到上位机 OTA 命令时写入 `1`, Bootloader 读取后清除
- Bootloader 独占管理 S10, APP 仅能读和写 `ota_request` 单字节

---

## 4. 启动流程

```
上电/复位
  │
  ▼
Bootloader (0x08000000)
  │
  ├─ 初始化: 时钟 168MHz, GPIO, USART1 (115200)
  ├─ 读取 S10 fw_info
  │   ├─ magic 无效 → 初始化 fw_info, 两槽标记无效
  │   └─ magic 正确 → 继续
  │
  ├─ KEY1 长按 ≥3s? ──→ 进入更新模式
  ├─ ota_request == 1? ──→ 清除标志 → 进入更新模式
  │
  ├─ active_slot 有效且 CRC32 正确? ──→ 跳转 APP
  ├─ 备用槽有效且 CRC32 正确? ──→ 切换 active_slot → 跳转 APP
  └─ 两槽均无效 ──→ 进入更新模式 (LED 快闪, 等待 PC 下发)

进入更新模式:
  ├─ 等待 CMD_OTA_START → 擦除目标槽 → ACK
  ├─ 循环接收 CMD_OTA_DATA → 写入 Flash → ACK
  ├─ 收到 CMD_OTA_FINISH → CRC32 校验
  │   ├─ 通过 → 更新 fw_info, 标记新槽有效, 切换 active → 跳转
  │   └─ 失败 → NAK, 旧槽仍是 active, 等待重试
  └─ 收到 CMD_OTA_ABORT → 回到启动决策
```

---

## 5. OTA 协议

### 5.1 帧格式 (与 APP 协议一致)

```
SOF(0xA5) | LEN(1B) | CMD(1B) | DATA(0~251B) | CRC8(1B) | EOF(0x5A)
```

LEN 仅计 DATA 段字节数, 不含 CMD。CRC-8-ATM (多项式 0x07), 覆盖 SOF~DATA。

### 5.2 OTA 专用命令

**PC → Bootloader**:

| CMD | 名称 | DATA 格式 | 说明 |
|-----|------|-----------|------|
| `0x07` | `CMD_OTA_START` | `[slot:1B][size:4B LE][crc32:4B LE][ver:4B LE]` | 开始升级, 13 字节 |
| `0x08` | `CMD_OTA_DATA` | `[offset:4B LE][payload:≤200B]` | 数据块, N+4 字节 |
| `0x09` | `CMD_OTA_FINISH` | 无 (LEN=0) | 传输完成 |
| `0x0A` | `CMD_OTA_ABORT` | 无 (LEN=0) | 取消升级 |

**Bootloader → PC**:

| CMD | 名称 | 说明 |
|-----|------|------|
| `0xF0` | ACK | 成功 |
| `0xFF` | NAK + `[code:1B]` | 失败: `0x06`=偏移越界, `0x07`=CRC 失败, `0x08`=擦除失败 |

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
 │◄── ACK (成功)                      │ 更新 fw_info → 跳转
```

### 5.4 CRC32 算法

IEEE 802.3, 多项式 0xEDB88320 (反射), 初始值 0xFFFFFFFF, 结果取反。
计算范围: APP 槽全部固件字节 (size 由 OTA_START 指定)。

---

## 6. Bootloader 代码结构

```
stm32f407/bootloader/
├── boot_main.c         主入口, 启动决策, OTA 状态机, APP 跳转
├── boot_fw_info.h/c    S10 fw_info 管理 (读/写/CRC)
├── boot_flash.h/c      Flash 擦写 + CRC32 计算
├── boot_proto.h/c      简化协议帧解析/构建
├── bootloader.sct      Keil 散列文件 (ROM 0x08000000, 32KB)
├── STM32F407VGTx_FLASH_BOOT.ld   GCC 链接脚本
├── STM32F407VGTx_FLASH_SLOTA.ld  Slot A GCC 链接脚本
├── STM32F407VGTx_FLASH_SLOTB.ld  Slot B GCC 链接脚本
├── oled_cubemx_slota.sct         Slot A Keil 散列文件
├── oled_cubemx_slotb.sct         Slot B Keil 散列文件
├── startup_stm32f407xx.s         启动文件 (CubeMX 复制)
└── system_stm32f4xx.c            系统初始化 (CubeMX 复制)
```

**Bootloader 依赖**: 仅 HAL 库, 不引用任何 `stm32f407/src/` 下的 APP 代码。

---

## 7. APP 侧改动

| 文件 | 改动 |
|------|------|
| `user_app.h` | 新增 `APP_VTOR_ADDR` 宏 (由 `APP_SLOT_B` 控制) |
| `main.c` (USER CODE SysInit) | 新增 `SCB->VTOR = APP_VTOR_ADDR` |
| `user_app.c` | 新增 `CMD_OTA_RESERVED` 处理 — 写 ota_request → ACK → 复位 |
| `app_fw_info.h/c` | APP 侧 fw_info 操作 — 写 ota_request 单字节 |
| `protocol.h` | 新增 `CMD_OTA_DATA/FINISH/ABORT` 和 `NAK_OTA_*` 常量 |
| `Makefile` | 新增 `bootloader`, `app_slot_a`, `app_slot_b` 三个 target |

**编译命令**:
```bash
make bootloader    # 编译 Bootloader → build/bootloader.bin
make app_slot_a    # 编译 Slot A APP → build/app_slot_a.bin
make app_slot_b    # 编译 Slot B APP → build/app_slot_b.bin
make               # 编译全部
```

---

## 8. 进入 Bootloader 的条件

| 优先级 | 条件 | 实现 |
|--------|------|------|
| 1 | KEY1 (PE1) 上电长按 ≥3s | Bootloader 在跳转前轮询 GPIO |
| 2 | APP 收到 `CMD_OTA_RESERVED (0x07)` | APP 写 S10 `ota_request=1` → `NVIC_SystemReset()` |
| 3 | 两槽均无效 | Bootloader 自动进入更新模式 |

---

## 9. 异常处理

| 场景 | 行为 |
|------|------|
| OTA 写入途中断电 | 旧槽不变, 下次上电自动启动旧版本 |
| OTA CRC32 校验失败 | NAK → fw_info 不变 → 旧槽仍是 active → 等待重试 |
| 新固件可启动但有逻辑 bug | 可通过 KEY1 长按强制进入 Bootloader 重刷 |
| S10 fw_info 损坏 (CRC32 错) | Bootloader 初始化默认值 → 两槽标记无效 → 进入更新模式 |

---

## 10. 上位机 OTA 工具

独立的上位机工具 `tools/ota_tool/`, 详见 [ota-tool-design.md](ota-tool-design.md)。

---

## 11. 测试用例

| 编号 | 场景 | 预期 |
|------|------|------|
| T01 | 全新芯片首次上电 | 初始化 fw_info → 两槽无效 → 进入更新模式 |
| T02 | 正常 OTA Slot A→B | 擦除 B → 写入 → CRC 通过 → active=B → 跳转 B |
| T03 | OTA 写入途中断电 | 重上电 → A 槽仍有效 → 跳转 A 正常 |
| T04 | OTA FINISH 后 CRC 错 | fw_info 不变 → A 仍是 active → 重上电跳转 A |
| T05 | KEY1 长按上电 | 强制进入更新模式 |
| T06 | APP 收到 OTA 命令 | 写 ota_request → 复位 → Bootloader 识别 → 更新模式 |
| T07 | 两槽均无效 | Bootloader 等待固件 |
| T08 | 活跃槽 CRC 损坏 | 自动切换到备用槽 |

---

## 12. 构建与烧录

```bash
# GCC 编译 (推荐)
cd stm32f407
make                    # 编译全部 3 个目标

# 烧录 (ST-Link)
make flash_boot         # 烧录 Bootloader 到 0x08000000
make flash_slot_a       # 烧录 APP 到 Slot A (0x08008000)
make flash_slot_b       # 烧录 APP 到 Slot B (0x08060000)

# OTA 升级 (通过上位机)
python tools/ota_tool/ota_tool.py COM3 app_slot_b.bin
```

---

## 附录: Keil 工程配置参考

### Bootloader Target 配置

| 项 | 值 |
|----|-----|
| 芯片 | STM32F407VG |
| ROM Start | 0x08000000, Size: 0x8000 |
| RAM Start | 0x20000000, Size: 0x1C000 |
| Preprocessor | `STM32F407xx, USE_HAL_DRIVER, BOOTLOADER` |
| Scatter File | `bootloader/bootloader.sct` |

### APP Slot B Target 配置

| 项 | 值 |
|----|-----|
| Preprocessor | 追加 `APP_SLOT_B` |
| Scatter File | `bootloader/oled_cubemx_slotb.sct` |
