# 学习笔记 02 — Bootloader 开发及使用

> 以 `oled_prj` 项目的 A/B 双槽位 Bootloader（`stm32f407/iap/`）为实例，系统学习 Bootloader 的原理、设计、实现与使用。
> 配套参考：[docs/bootloader-design.md](../../docs/bootloader-design.md)（完整设计手册）、[doc/bootloader_ota_trigger_guide.md](../bootloader_ota_trigger_guide.md)（OTA 触发方案对比）

---

## 一、什么是 Bootloader，为什么需要

### 1.1 概念

Bootloader（引导程序）是烧录在 Flash 起始地址的一段**常驻程序**，上电后先于 APP 运行，负责：

- 校验 APP 是否有效
- 决定启动哪个 APP（或进入升级模式）
- 接收固件并写入 Flash（OTA 升级）
- 跳转到 APP 执行

### 1.2 为什么需要

| 场景 | 无 Bootloader | 有 Bootloader |
|------|--------------|---------------|
| 固件升级 | 必须拆机用烧录器 | 串口/网络 OTA，无需拆机 |
| 升级失败 | 直接变砖 | 回滚到旧版本，永不砖 |
| 现场维护 | 派人上门 | 远程下发固件 |

### 1.3 本项目为什么用 A/B 双槽位

**A/B 双槽位 = 同一份代码编译出两个不同链接地址的固件，交替升级，互为备份**：

```
Slot A (0x08008000)  ←→  Slot B (0x08060000)
   正常运行的固件          升级写入的目标槽
```

- 升级时**不覆盖正在运行的固件**，而是写入另一个槽
- 下载断电 / CRC 校验失败 → 旧槽仍 active，设备正常启动
- 新固件有问题 → KEY1 开机强制进 Bootloader 重刷
- 结论：**永不"变砖"**

---

## 二、Flash 分区设计

### 2.1 STM32F407 的 Flash 扇区结构

F407 的 1MB Flash 不是均匀分块的，扇区大小递增：

| 扇区 | 地址范围 | 大小 |
|------|---------|------|
| S0 | 0x08000000 - 0x08003FFF | 16KB |
| S1 | 0x08004000 - 0x08007FFF | 16KB |
| S2 | 0x08008000 - 0x0800BFFF | 16KB |
| S3 | 0x0800C000 - 0x0800FFFF | 16KB |
| S4 | 0x08010000 - 0x0801FFFF | 64KB |
| S5~S11 | 每扇区 128KB | 128KB × 7 |

> **关键**：**Flash 只能按扇区擦除**（最小擦除单位），擦除后为 0xFF。这决定了分区规划必须以扇区为边界。

### 2.2 本项目分区规划

```
扇区     地址                   大小      用途
─────────────────────────────────────────────────────
S0~S1    0x08000000             32KB    Bootloader
S2~S6    0x08008000            352KB    Slot A (base=0x08008000)
S7~S9    0x08060000            384KB    Slot B (base=0x08060000)
S10      0x080C0000            128KB    固件信息区 fw_info
S11      0x080E0000            128KB    sys_config (APP 使用)
```

**设计要点**：
- Slot A/B 必须从扇区边界开始，且大小是扇区整数倍
- fw_info 独占一个 128KB 扇区——**不是因为要用 128KB**，而是擦除最小单位是扇区，为防误擦其他数据而整体划出
- 当前 APP 规模约 57KB，Slot A/B 都有 6 倍以上余量

### 2.3 链接脚本的作用

每个固件（Bootloader / SlotA / SlotB）有独立的链接脚本，把代码放到不同地址：

| 固件 | 链接脚本 | ROM 起始 |
|------|---------|---------|
| Bootloader | `bootloader/STM32F407VGTx_FLASH_BOOT.ld` | 0x08000000 |
| Slot A | `bootloader/STM32F407VGTx_FLASH_SLOTA.ld` | 0x08008000 |
| Slot B | `bootloader/STM32F407VGTx_FLASH_SLOTB.ld` | 0x08060000 |

Makefile 中通过 `-DAPP_SLOT_A` / `-DAPP_SLOT_B` 宏 + 不同 `.ld` 文件编译出两个固件，代码完全一样，仅链接地址不同。

---

## 三、固件信息区 fw_info（S10）

### 3.1 数据结构

```c
typedef struct {
    uint32_t magic;          // 0x4657494E "FWIN" — 校验记录有效性
    uint8_t  active_slot;    // 0=A, 1=B
    uint8_t  slot_a_state;   // 0x01=有效, 0xFF=无效
    uint8_t  slot_b_state;
    uint8_t  ota_request;    // 兼容保留，不再用于启动决策
    uint32_t slot_a_size;    // A 槽固件大小
    uint32_t slot_a_crc;     // A 槽 CRC32 (IEEE 802.3)
    uint32_t slot_a_version;
    uint32_t slot_b_size;
    uint32_t slot_b_crc;
    uint32_t slot_b_version;
    uint32_t crc32;          // 结构体自身 CRC32
} fw_info_t;
```

### 3.2 设计要点

- `slot_x_state = 0xFF`（擦除态）表示无效，`0x01` 表示有效
- **状态 + CRC32 + 版本号**三件套：状态判断是否有效，CRC 判断内容是否损坏，版本用于展示
- 保存策略：每次先擦除 S10 整扇区，再写 Slot 0，并**逐字读回校验**
- fw_info 由 Bootloader 独占管理，APP 不直接写

> **学习点**：为什么 fw_info 自身还要 CRC？因为 Flash 可能位翻转，只有 magic 不够可靠，CRC 保证读到的一定是完整一致的数据。

---

## 四、启动流程与择优启动决策（核心）

### 4.1 上电启动流程

```
上电/复位
   │
   ▼
Bootloader (0x08000000)
   │  HAL_Init → 时钟 168MHz → GPIO → USART2(调试) → OLED → USART1
   │  LED 闪烁 2 次指示启动
   │
   ├─ 读取 S10 fw_info（magic 无效则初始化）
   │
   ├─ SRAM NOINIT 区域有 OTA 请求？ ──是──→ 清除标志 → 进入升级模式
   │
   ├─ KEY1 按下？ ──是──→ 进入升级模式
   │
   ├─ active_slot 有效且 CRC 正确？ ──是──→ 跳转 APP
   ├─ 备用槽有效且 CRC 正确？ ──是──→ 切换 active → 跳转 APP
   └─ 两槽都无效 ──→ 进入升级模式（等固件）
```

### 4.2 择优启动逻辑（重点理解）

Bootloader **不是简单记住"上次用哪个槽"**，而是每次上电对两个槽独立校验：

```
1. active_slot 状态 VALID 且全量 CRC32 通过  → 直接跳转 active_slot
2. active_slot 校验失败                       → 尝试备用槽
   a. 备用槽 VALID 且 CRC 通过                → 自动切 active 为备用槽 → 跳转
   b. 备用槽也失败                            → 进入升级模式
```

**核心结论**：
- "上次升级成功的槽就是 active_slot"（OTA 完成时同时标记 VALID + active）
- "active_slot 不是铁饭碗"（Flash 数据退化时自动切备用槽）
- "两槽同坏才变砖"（只要有一个槽 CRC 正确就能启动）

### 4.3 APP 跳转实现（boot_main.c 的 boot_jump_to_app）

```c
static void boot_jump_to_app(uint32_t app_base)
{
    uint32_t app_sp = *((volatile uint32_t *)app_base);      // 向量表[0]: 初始栈指针
    uint32_t app_pc = *((volatile uint32_t *)(app_base + 4)); // 向量表[1]: 复位向量

    // ① 校验 SP/PC 合法性，防止跳到非法地址死机
    if (app_sp < 0x20000000UL || app_sp > 0x2001C000UL) return;
    if (app_pc < 0x08000000UL || app_pc > 0x080FFFFFUL) return;

    // ② 关闭全局中断
    __disable_irq();
    // ③ 关闭 SysTick（防止跳转后 SysTick 中断异常）
    SysTick->CTRL = 0; SysTick->LOAD = 0; SysTick->VAL = 0;
    // ④ 关闭 Bootloader 占用的外设时钟（如 USART2）
    __HAL_RCC_USART2_CLK_DISABLE();
    // ⑤ 设置 MSP 为 APP 的初始栈指针
    __set_MSP(app_sp);
    // ⑥ 重定位中断向量表到 APP 起始地址（VTOR）
    SCB->VTOR = app_base;
    // ⑦ 重新使能中断，跳到 APP 复位向量
    __enable_irq();
    ((void (*)(void))app_pc)();
    while (1) {}
}
```

**跳转的六个步骤是 Bootloader 的标准动作，每一步都有原因**：
1. **校验向量表**：SP 必须在 RAM 范围，PC 必须在 Flash 范围——防止空/坏固件导致跳飞
2. **关中断**：跳转瞬间不允许被打断
3. **清 SysTick**：Bootloader 的 SysTick 配置不能带进 APP
4. **关外设时钟**：外设状态复位，避免 APP 初始化冲突
5. **设 MSP**：APP 用自己的栈
6. **改 VTOR**：**中断向量表重定位**——APP 的中断入口在 0x08008000（而非 0x08000000），不改 VTOR 则中断全部进 Bootloader 的向量表，APP 中断全失效（这是最常见的"跳转后死机"原因）

---

## 五、OTA 升级协议（Bootloader 与 PC 通信）

### 5.1 帧格式（与 APP 协议一致的二进制帧）

```
┌──────┬──────┬──────┬──────┬───────────┬──────┬──────┐
│ SOF  │ LEN  │ CMD  │ DATA │  PAYLOAD  │ CRC8 │ EOF  │
│ 0xA5 │ 1B   │ 1B   │ 0~251B│          │ 1B   │ 0x5A │
└──────┴──────┴──────┴──────┴───────────┴──────┴──────┘
```

### 5.2 OTA 命令

| 命令 | 作用 | DATA |
|------|------|------|
| CMD_OTA_START (0x07) | 开始升级：指定槽位/大小/CRC32/版本 | slot(1)+size(4)+crc32(4)+version(4) |
| CMD_OTA_DATA (0x08) | 数据块（每块 200 字节） | offset(4)+payload |
| CMD_OTA_FINISH (0x09) | 结束：Bootloader 做全量 CRC 校验 | 无 |
| CMD_OTA_ABORT | 中止升级 | 无 |
| ACK / NAK | 每帧应答 | NAK 带错误码 |

### 5.3 升级握手流程（Bootloader 侧状态机）

```
UPD_IDLE ──CMD_OTA_START──> 擦除目标槽 ──ACK──> UPD_RECEIVING
UPD_RECEIVING ──CMD_OTA_DATA──> 写 Flash ──ACK──> 继续接收
UPD_RECEIVING ──CMD_OTA_FINISH──> CRC 校验
    ├─ 通过 → fw_info_activate_slot() → OLED"升级完成" → 跳转 APP
    └─ 失败 → NAK(CRC 错误) → 回到 UPD_IDLE 等待重试
UPD_IDLE ──CMD_OTA_ABORT──> 退出升级模式，重新启动决策
```

**关键设计**：
- 每帧必答（ACK/NAK），发送方超时重试（最多 3 次）→ 可靠传输
- 升级过程 OLED 显示进度条（每 64KB 刷新）
- **只有收到 FINISH 且 CRC 通过才激活新槽**——之前任何时刻断电，旧槽仍然有效

### 5.4 OTA 工具（Python）

```bash
# 当前运行 Slot A → 升级到 Slot B
python tools/ota_tool/ota_tool.py COM3 build/app_slot_b.bin
```

工具自动从固件**向量表检测槽位**（读前 4 字节判断链接地址），防止固件与槽位不匹配。

> **重要规则：固件二进制与槽位强绑定。** `app_slot_a.bin`（VTOR=0x08008000）只能写 Slot A；若强行 `--force-slot` 写错槽位，VTOR 指向的位置没有有效向量表，APP 启动即卡死。

---

## 六、OTA 升级触发方案（SRAM NOINIT）

### 6.1 核心问题

APP 运行中用户触发升级 → 需要复位进入 Bootloader → 但复位后 Bootloader 必须区分"正常启动"和"升级请求"。

**矛盾**：普通 RAM 变量复位即丢失；Flash 不适合频繁改写。

### 6.2 方案对比

| 方案 | 原理 | 优点 | 缺点 |
|------|------|------|------|
| **SRAM NOINIT**（本项目） | 系统复位 SRAM 内容保持；用链接脚本把 SRAM 末端 16B 排除出 RW/ZI 区 | 零硬件成本、断电自动清除请求 | 需同时改 Bootloader 和 APP 的链接脚本 |
| 备份寄存器 BKP | VBAT 供电保持 | 实现简单、可靠 | 依赖 VBAT；寄存器数量有限 |
| GPIO/按键 | Bootloader 启动时检测引脚 | 极简、可靠 | 无法软件触发 |
| Flash 标记区 | 写专用扇区标记 | 断电保持 | 频繁擦写损耗 Flash、需管理擦除 |
| 双 Bank 交换 | 硬件级切换 | 最可靠 | 需要芯片支持（F4 部分型号无） |

### 6.3 SRAM NOINIT 实现细节（本项目方案）

**原理**：STM32 的 SRAM 在系统复位（`NVIC_SystemReset`）后**内容保持**，上电复位才丢失——恰好匹配"断电后正常启动"的语义。

**布局**：SRAM1 末端 16 字节（0x2001BFF0），远离栈顶，避免意外覆盖：

```
SRAM1: 0x20000000 ~ 0x2001C000 (128KB)
┌─────────────────────────────────────────────┐
│ RW_IRAM1 (DATA + BSS + STACK + HEAP)         │
│ 大小 = 128KB - 16B = 0x1BFF0                 │
├─────────────────────────────────────────────┤
│ 0x2001BFF0  NOINIT 区域 (16B)                │
│  magic:   0x4F544152  ("OTAR")              │
│  request: UPDATE / NONE                      │
│  slot:    目标槽位                            │
│  reserved: 保留                              │
└─────────────────────────────────────────────┘
0x2001C000 (SRAM1 末端)
```

**链接脚本（GCC）**：

```ld
MEMORY
{
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 128K - 16
}
```

**Keil scatter file**：

```
RW_IRAM1 0x20000000 0x0001BFF0  {  ; 128KB - 16B
   .ANY (+RW +ZI)
}
; 0x2001BFF0 ~ 0x2001BFFF 不在任何 execution region
; __scatterload 不会清零该区域
```

**魔数防抖**：使用非 0x00/0xFF 的唯一值 `0x4F544152`，降低 SRAM 上电随机值误触发的概率。

**代码（APP 侧写，Bootloader 侧读）**：

```c
// APP 侧: 设置升级请求后复位
int ota_req_set_update(void)
{
    volatile ota_req_t *req = (volatile ota_req_t *)OTA_REQ_ADDR;
    req->magic   = OTA_REQ_MAGIC;   // 0x4F544152
    req->request = OTA_REQ_UPDATE;  // 0x1
    NVIC_SystemReset();
}

// Bootloader 侧: 检查并清除
int ota_req_is_update(void)
{
    volatile ota_req_t *req = (volatile ota_req_t *)OTA_REQ_ADDR;
    return (req->magic == OTA_REQ_MAGIC && req->request == OTA_REQ_UPDATE) ? 1 : 0;
}
```

**本项目还保留两种辅助触发方式**：
1. **KEY1 上电按住** → 强制进升级模式（救砖用）
2. **两槽都无效** → 自动进升级模式（全新芯片）

---

## 七、Bootloader 编译与使用

### 7.1 编译三个固件

```bash
cd stm32f407
make app_slot_a     # → build/app_slot_a.bin（链接 0x08008000）
make app_slot_b     # → build/app_slot_b.bin（链接 0x08060000）
make bootloader     # → build/bootloader.bin（链接 0x08000000）
```

### 7.2 首次烧录

```bash
make flash_boot     # 先烧 Bootloader 到 0x08000000
make flash_slot_a   # 再烧 Slot A
```

### 7.3 日常 OTA 升级（规律：交替升级）

| 当前运行 | 升级命令 |
|---------|---------|
| Slot A | `python ota_tool.py COM3 build/app_slot_b.bin` |
| Slot B | `python ota_tool.py COM3 build/app_slot_a.bin` |

查看调试日志 `active=slotA / active=slotB` 确定当前槽。

### 7.4 常见错误

| 错误操作 | 现象 | 原因 |
|---------|------|------|
| B 固件写入 Slot A（或反之） | APP 启动卡死 | VTOR 指向位置没有有效向量表 |
| 只烧 APP 没烧 Bootloader | 上电无反应 | 0x08000000 无程序 |
| 升级中途断电 | 下次正常启动旧固件 | A/B 容错设计，无需担心 |

---

## 八、踩坑与经验总结

1. **改 VTOR 是跳转成败的关键**：忘记重定位向量表，APP 一进中断就死
2. **跳转前必须清理外设状态**：SysTick、USART 等要关掉再跳，否则 APP 初始化冲突
3. **扇区擦除边界**：分区规划时，所有区域起始地址必须对齐扇区边界
4. **固件与槽位强绑定**：链接地址决定 VTOR，烧错位置 = 卡死
5. **NOINIT 区域要同时改 Bootloader 和 APP 的链接脚本**：两边都保留 16B，任何一边忘记都会互相覆盖
6. **调试串口与协议串口分离**：Bootloader 用 USART2 打印日志、USART1 跑 OTA 协议，互不干扰
7. **自定义 vsnprintf**：Bootloader 里不用标准 printf（避免 semihosting 在无调试器时挂死），自己实现轻量版
8. **看门狗在升级模式下要小心**：升级过程耗时长，擦写 Flash 期间不能喂狗，需评估超时或关闭

---

## 九、自测题

1. 为什么 A/B 双槽位能"永不砖"？升级过程断电会发生什么？
2. STM32F407 的 Flash 最小擦除单位是什么？分区规划要遵守什么规则？
3. 跳转 APP 前为什么要改 SCB->VTOR？不改会怎样？
4. fw_info 中 state 和 CRC32 各起什么作用？
5. OTA 升级协议的每帧应答机制解决什么问题？
6. SRAM NOINIT 方案为什么能区分"系统复位"和"上电复位"？
7. 什么情况下 Bootloader 会"自动切换 active_slot 到备用槽"？
8. 为什么 Bootloader 要自定义 vsnprintf 而不用标准库 printf？

<details>
<summary>参考答案</summary>

1. 升级写入非活跃槽，旧槽保持有效；断电只是新槽没写完，旧槽仍可启动
2. 扇区（S0~S3 为 16KB，S4 为 64KB，S5+ 为 128KB）；区域起始必须对齐扇区边界
3. VTOR 决定中断向量表位置；不改则中断仍进 Bootloader 向量表，APP 中断全失效
4. state 标记有效/无效，CRC32 校验内容完整性（防位翻转）
5. 确认每帧可靠到达，发送方超时重传，实现可靠传输
6. 系统复位（NVIC_SystemReset）SRAM 保持；上电复位 SRAM 丢失——正好匹配"断电后正常启动"
7. active_slot 全量 CRC 校验失败但备用槽有效时
8. 标准 printf 可能触发 semihosting，无调试器连接时挂死

</details>
