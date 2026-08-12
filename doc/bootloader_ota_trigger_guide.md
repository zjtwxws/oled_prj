# STM32 嵌入式 Bootloader OTA 升级触发方案对比

> 整理自 OLED Gateway 项目（STM32F407）bootloader 重构实践。
> 日期：2026-08-12

---

## 一、背景问题

典型的双槽位（Slot A/B）IAP 架构中，APP 运行期间用户触发"固件升级"后，系统需要复位进入 Bootloader，
Bootloader 启动后必须**区分**"正常启动"和"升级请求"两种场景。

核心矛盾：复位会丢失 RAM 中的普通变量，而 Flash 不适合做频繁改写的标志位。

---

## 二、五种常见方案对比

### 方案 1：SRAM NOINIT 区域（本项目采用）

**原理**

STM32 的 SRAM 在系统复位（`NVIC_SystemReset` / `SCB->AIRCR` 写 SYSRESETREQ）后内容保持不丢失。
利用这一特性，在 SRAM 末端划出 16 字节，通过 scatter file / linker script 将其从 RW/ZI 区域排除，
确保 `__scatterload` 启动代码不会清零该区域。

APP 复位前写入魔数 + 请求类型；Bootloader 启动后第一时间检查该区域。

**实现要点**

```
┌──────────────────────────────────────────────┐
│  SRAM1: 0x20000000                           │
│  ┌──────────────────────────────────────┐    │
│  │ RW_IRAM1 (DATA + BSS + STACK + HEAP) │    │
│  │ size: 128KB - 16B = 0x1BFF0          │    │
│  └──────────────────────────────────────┘    │
│  0x2001BFF0 ┌──────────────────────┐         │
│             │ NOINIT 区域 (16字节)   │         │
│             │ magic: 0x4F544152     │         │
│             │ request: UPDATE/NONE  │         │
│             │ slot: 目标槽位         │         │
│             │ reserved: 保留         │         │
│             └──────────────────────┘         │
│  0x2001C000  (SRAM1 末端)                      │
└──────────────────────────────────────────────┘
```

- **地址选择**：SRAM 末端，远离堆栈，避免意外覆盖
- **魔数保护**：使用非 0x00/0xFF 的唯一值（如 `0x4F544152` "OTAR"），降低 POR 后随机值误触发概率
- **复位语义**：系统复位保留 / 上电复位丢失 → 恰好匹配"断电后正常启动"的预期

**Keil scatter file 配置**

```c
RW_IRAM1 0x20000000 0x0001BFF0  {  ; 128KB - 16B
   .ANY (+RW +ZI)
}
; 0x2001BFF0 ~ 0x2001BFFF 不在任何 execution region
; __scatterload 不会触碰
```

**GCC linker script 配置**

```ld
MEMORY
{
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 128K - 16
}
```

**优点**
- 零硬件成本
- 无需额外外设或引脚
- 语义清晰：断电 = 清除请求
- 工业界广泛验证（MCUBoot、Zephyr、OpenBLT 均使用此方案）

**缺点**
- 同时需要修改 Bootloader 和 APP 的 linker 配置
- 不同编译器（Keil/GCC/IAR）配置方式不同
- 需要魔数防抖，避免 SRAM 上电随机值误触发
- 若看门狗复位触发 POR，NOINIT 内容可能丢失

---

### 方案 2：备份寄存器（Backup Registers）

**原理**

STM32F4 有 20 个 32 位备份寄存器（`BKP_DRx`，位于备份域），由 `VBAT` 供电。
只要 `VBAT` 有电（纽扣电池或 VDD 供电），系统复位后内容保持。

**用法**

```c
// APP 侧 — 设置升级请求
RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
PWR_BackupAccessCmd(ENABLE);
BKP_WriteBackupRegister(BKP_DR1, 0x4F544152);  // 魔数

// Bootloader 侧 — 检查
if (BKP_ReadBackupRegister(BKP_DR1) == 0x4F544152)
{
    BKP_WriteBackupRegister(BKP_DR1, 0);  // 清除
    enter_update_mode();
}
```

**优点**
- 硬件保证的保持能力，不依赖 linker 配置
- 不受 SRAM 初始化影响
- 实现极其简单

**缺点**
- 依赖 VBAT 供电（若无电池，系统完全断电后丢失）
- 备份寄存器数量有限（F4 有 20 个，F1 只有 10 个）
- 多任务共用时需要协调寄存器分配
- 某些低功耗模式下备份域可能不供电

---

### 方案 3：GPIO + 外部拨码 / 按键

**原理**

Bootloader 启动时检查指定 GPIO 引脚电平。APP 无法软件触发，需人工操作。

**用法**

```c
// Bootloader 侧
if (HAL_GPIO_ReadPin(BOOT_KEY_GPIO_Port, BOOT_KEY_Pin) == GPIO_PIN_RESET)
{
    enter_update_mode();
}
```

**优点**
- 极简实现，代码量最小
- 可靠，不依赖任何存储介质
- 常用于量产烧录和现场救砖场景

**缺点**
- 不支持 APP 内软件触发（需人工干预）
- 占用 GPIO 引脚
- 不适合远程 OTA 场景

---

### 方案 4：Flash 专用标记区

**原理**

在 Flash 中划出一个独立扇区存放升级标志，APP 擦除后写入。

**用法**

```c
// APP 侧
erase_sector(OTA_FLAG_SECTOR);
program_word(OTA_FLAG_ADDR, OTA_MAGIC);

// Bootloader 侧
if (*(uint32_t *)OTA_FLAG_ADDR == OTA_MAGIC)
{
    erase_sector(OTA_FLAG_SECTOR);  // 清除
    enter_update_mode();
}
```

**优点**
- 数据永久保存，不受供电影响
- 不占用 SRAM

**缺点**
- Flash 擦除寿命有限（典型 10K~100K 次），频繁 OTA 会磨损
- 擦除操作耗时（数十 ms~数百 ms），复位时序中需等待
- 如果标记区和 `fw_info` 共用扇区，改写标记会破坏其他数据
- STM32F4 Flash 编程只能将 bit 从 1 写为 0，反向操作必须整扇区擦除
- **本项目旧方案即属此类**，因 Flash 编程方向违规和 CRC 破坏问题被废弃

---

### 方案 5：共享内存 + 不初始化段（`.noinit` 段）

**原理**

与方案 1 同源，但不使用固定地址，而是通过编译器指令将变量放入 `.noinit` 段：

**GCC**

```c
__attribute__((section(".noinit"))) volatile uint32_t g_ota_request;

// linker script
.noinit (NOLOAD) :
{
    *(.noinit)
    *(.noinit*)
} > RAM
```

**Keil**

```c
// scatter file
RW_NOINIT 0x2001BFF0 UNINIT 0x10 {
    *(.noinit)
}

// C 代码
__attribute__((section(".noinit"))) volatile ota_req_t g_ota_req;
```

**优点**
- 不需要手动管理地址，链接器自动分配
- 支持直接按变量名访问，代码更可读

**缺点**
- `UNINIT` 属性的 execution region 在 Keil 中需要特殊配置
- 不同编译器语法差异大，移植成本高

---

## 三、方案选型决策矩阵

| 维度 | SRAM NOINIT | 备份寄存器 | GPIO 按键 | Flash 标记 | .noinit 段 |
|------|:-----------:|:----------:|:---------:|:----------:|:----------:|
| 软件触发（远程 OTA） | ✓ | ✓ | ✗ | ✓ | ✓ |
| 断电后自动清除 | ✓ | 依赖 VBAT | N/A | ✗ | ✓ |
| 实现复杂度 | 中 | 低 | 低 | 高 | 中 |
| 硬件成本 | 零 | 零（需 VBAT）| 1 GPIO | 零 | 零 |
| 可靠性 | 高 | 高 | 极高 | 中（磨损）| 高 |
| 工业成熟度 | 极高 | 高 | 极高 | 中 | 高 |
| 代表产品 | MCUBoot, Zephyr, OpenBLT | ST 官方 IAP 示例 | 大部分量产方案 | 早期方案 | Zephyr, ESP-IDF |

---

## 四、本项目的选择：SRAM NOINIT + GPIO 按键双通道

**最终方案**

Bootloader 进入升级模式的条件（优先级从高到低）：

1. **SRAM NOINIT 检测到 OTA 请求** → APP 通过 CLI `update` 命令或菜单"升级固件"触发
2. **KEY1 按键按下** → 人工强制进入（工厂烧录、救砖场景）
3. **两个 APP 槽均无效** → 全新芯片或固件损坏，自动进入升级模式等待固件

**为什么不选其他方案**

- **备份寄存器**：本项目没有 VBAT 电池，完全断电后丢失，但"断电丢失"本身就是 SRAM NOINIT 的语义优势
- **Flash 标记**：旧方案已验证存在致命缺陷（CRC 破坏、Flash 编程方向违规）
- **纯 GPIO**：无法支持通过串口命令远程 OTA

**数据流**

```
┌──────────┐  ota_req_set_update()  ┌──────────────┐
│   APP    │ ─────────────────────→ │ SRAM NOINIT   │
│ (Slot A) │   写入 0x2001BFF0      │ 0x2001BFF0    │
└────┬─────┘                        └──────┬───────┘
     │ NVIC_SystemReset()                  │
     ▼                                     │
┌──────────┐  ota_req_is_update()          │
│Bootloader│ ←─────────────────────────────┘
│ 0x08000000│ 读取并检查魔数
└────┬─────┘
     │ 检测到请求 → enter_update_mode()
     │ 未检测到  → 正常跳转 APP
     ▼
```

---

## 五、参考实现（开源项目）

| 项目 | 方案 | 平台 | 特点 |
|------|------|------|------|
| [MCUBoot](https://github.com/mcu-tools/mcuboot) | 共享内存（image swap state） | 多平台 | Zephyr 默认 bootloader，retained memory |
| [OpenBLT](https://github.com/feaser/openblt) | 后备寄存器 + GPIO | 多平台 | 商业级，支持多种触发源 |
| [STM32 IAP 官方示例](https://www.st.com) | USART/YMODEM + 按键 | STM32 | 简单，适合学习 |
| Zephyr RTOS | `.noinit` 段 | 多平台 | `CONFIG_RETAINED_MEM` |

---

## 六、注意事项

1. **看门狗复位**：部分 STM32 系列的独立看门狗（IWDG）复位可能触发 POR，导致 NOINIT 内容丢失。若需在 APP 中使用 IWDG，应在设置 OTA 请求前刷新看门狗，复位时确保窗口足够。
2. **调试器连接**：连接调试器时，某些调试器会在连接时复位 MCU，可能导致 SRAM 内容被破坏。测试时应断开调试器。
3. **魔数选择**：避免 `0x00000000`（SRAM 上电默认值可能是 0 或随机值）、`0xFFFFFFFF`（Flash 擦除态），选择有明显 ASCII 含义的值便于调试。
4. **地址对齐**：NOINIT 区域建议 4 字节对齐，且区域大小应为 4 的倍数，避免非对齐访问。
