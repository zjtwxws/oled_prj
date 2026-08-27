# 学习笔记 03 — 命令行（CLI）开发及使用

> 以 `oled_prj` 项目移植 nr_micro_shell 到 STM32F407 为例，学习嵌入式 CLI 的完整开发流程：
> 第三方库移植、环形缓冲区、中断接收、命令表设计、状态机。
> 配套参考：[docs/cli-integration.md](../../docs/cli-integration.md)（移植文档）
> 源码：`stm32f407/src/nr_micro_shell_core.c`、`cli_cmds.c`、`debug_console.c`

---

## 一、为什么嵌入式设备需要 CLI

### 1.1 CLI 的价值

| 场景 | 传统做法 | CLI 做法 |
|------|---------|---------|
| 查看固件版本 | 看代码/重新编译 | 终端输入 `info` |
| 控制 LED | 改代码烧录 | 终端输入 `led 1` |
| 调试内部状态 | 接调试器打断点 | 终端输入命令实时查询 |
| 复位设备 | 断电重启 | 终端输入 `reboot` |

**核心价值**：把"改代码-编译-烧录"的慢循环，变成"敲命令"的快循环，极大提升调试效率。

### 1.2 技术选型：为什么用 nr_micro_shell

| 对比项 | 自己写 CLI | nr_micro_shell |
|--------|-----------|----------------|
| 工作量 | 要处理 ANSI 转义、历史记录、补全 | 开箱即用（MIT 协议） |
| 代码量 | 数百行 | ~560 行核心引擎 |
| 内存占用 | 不可控 | 极小（适合 MCU） |
| 功能 | 基础 | 上下键历史、Tab 补全、光标编辑、ANSI 颜色 |

**结论**：嵌入式 CLI 引擎首选成熟轻量库，把精力放在命令业务实现上。

---

## 二、系统架构

### 2.1 整体数据流

```
PC 终端 (MobaXterm/SecureCRT)
   │  串口 USART2 (PA2/PA3, 115200 8N1)
   ▼
uart_drv.c: HAL_UART_RxCpltCallback  ← 中断每收到 1 字节
   │  判断是 USART2 → debug_console_rx_callback()
   ▼
debug_console.c: ISR 把字节写入环形缓冲区 cli_rx_buf[64]
   │
   ▼
主循环: user_app_handle() → cli_poll()  ← 每帧取字节喂给引擎
   │
   ▼
nr_micro_shell_core.c: shell(c) 逐字符状态机
   ├─ 解析 ANSI 转义序列 (上下左右/Home/End/Delete)
   ├─ 历史记录 / Tab 补全
   └─ 回车 → 命令匹配 → 执行 cmd_table[]
   │
   ▼
shell_putc(c) → HAL_UART_Transmit → 终端回显
```

**理解要点**：中断收字节（快，实时性要求高）→ 环形缓冲暂存（隔离速度）→ 主循环处理（慢，逐字符喂状态机）。这是嵌入式"中断 + 缓冲 + 轮询"的经典模式。

### 2.2 分层职责

| 文件 | 职责 |
|------|------|
| `nr_micro_shell_core.c` | CLI 引擎（状态机、历史、补全）——**第三方库，不修改** |
| `nr_micro_shell_port.h` | 移植配置（缓冲区大小、历史条数、提示符） |
| `debug_console.c` | 移植层：串口收发、环形缓冲、`shell_putc`、`shell_get_ts_ns` |
| `cli_cmds.c` | 命令表 + 具体命令实现 |
| `uart_drv.c` | 串口中断分发（UART1→协议，UART2→CLI） |

---

## 三、移植步骤详解（可复用方法论）

### 3.1 Step 1：确认引擎对外接口

nr_micro_shell 引擎只依赖两个**必须由用户实现**的函数：

```c
void shell_putc(char c);        // 输出单字符（引擎回显/打印用）
uint64_t shell_get_ts_ns(void); // 纳秒时间戳（引擎内部计时用）
```

> **移植任何第三方库的第一步**：先看它 require 哪些外部符号，这就是你的"移植面"。

### 3.2 Step 2：实现移植函数（debug_console.c）

```c
/* 输出单字符 */
void shell_putc(char c)
{
    if (p_debug_uart)
        HAL_UART_Transmit(p_debug_uart, (uint8_t *)&c, 1, HAL_MAX_DELAY);
}

/* 时间戳: ms → ns 近似 */
uint64_t shell_get_ts_ns(void)
{
    return (uint64_t)HAL_GetTick() * 1000000UL;
}
```

> 注意：`HAL_UART_Transmit` 是阻塞发送，逐字符发送速度慢但实现简单，满足 CLI 交互需求即可。若追求性能可换 DMA + 环形 TX 缓冲。

### 3.3 Step 3：UART2 中断接收 + 环形缓冲区（重点）

**环形缓冲区**是嵌入式接收数据的标准结构。本项目实现：

```c
#define CLI_RX_BUF_SIZE 64          /* 必须是 2 的幂! */
static uint8_t  cli_rx_buf[CLI_RX_BUF_SIZE];
static volatile uint16_t cli_rx_head = 0;   /* 写指针 (ISR 中更新) */
static volatile uint16_t cli_rx_tail = 0;   /* 读指针 (主循环更新) */
static uint8_t  cli_rx_byte;                /* HAL 中断接收缓冲 */
```

**四个关键规则（易错）**：

```c
// ① 判满/判空必须用差值 & 掩码，不能直接 ==
//    head/tail 不截断、自然溢出，用 (head - tail) & mask 判断
uint16_t next = cli_rx_head + 1;
if (((next - cli_rx_tail) & (CLI_RX_BUF_SIZE - 1)) != 0)
{
    cli_rx_buf[cli_rx_head & (CLI_RX_BUF_SIZE - 1)] = cli_rx_byte;
    cli_rx_head = next;
}

// ② 数组下标用 & (SIZE-1) 取模（SIZE 是 2 的幂时等价于 % SIZE，更快）
// ③ head/tail 用 volatile，因为跨 ISR 与主循环共享
// ④ 缓冲区满时静默丢弃（本次数据丢失，但不阻塞中断）
```

**为什么缓冲区大小必须是 2 的幂？** 因为 `& (SIZE-1)` 取模比 `% SIZE` 快得多，且无符号溢出行为可预测。

### 3.4 Step 4：ISR 回调中必须重新启动接收

```c
void debug_console_rx_callback(void)
{
    // ... 写入环形缓冲 ...

    /* ★ 必须重新启动中断接收，否则只收 1 字节就停 */
    HAL_UART_Receive_IT(p_debug_uart, &cli_rx_byte, 1);
}
```

> **踩坑经验**：HAL 的 `HAL_UART_Receive_IT` 是一次性的——接收完成回调后，**必须再次调用才能接收下一字节**。忘记重新启动 = 串口只响应一次。这是 HAL 库新手最常犯的错误之一。

### 3.5 Step 5：主循环轮询 cli_poll()

```c
void cli_poll(void)
{
    while (((cli_rx_head - cli_rx_tail) & (CLI_RX_BUF_SIZE - 1)) != 0)
    {
        uint8_t c = cli_rx_buf[cli_rx_tail & (CLI_RX_BUF_SIZE - 1)];
        cli_rx_tail++;
        shell(c);   /* 逐字符喂给引擎状态机 */
    }
}
```

**为什么不在中断里直接调 shell()？** 引擎内有大量字符串处理和状态操作，放中断会拉长中断响应时间。中断只做"快速入队"，主循环"慢速处理"，两者解耦。

### 3.6 Step 6：NVIC 使能 USART2 中断

`stm32f4xx_it.c`：

```c
extern UART_HandleTypeDef huart2;
void USART2_IRQHandler(void) { HAL_UART_IRQHandler(&huart2); }
```

`main.c` 的 `MX_NVIC_Init`：

```c
HAL_NVIC_SetPriority(USART2_IRQn, 14, 0);
HAL_NVIC_EnableIRQ(USART2_IRQn);
```

### 3.7 Step 7：中断分发（多串口并存）

`uart_drv.c` 的 `HAL_UART_RxCpltCallback` 是所有 UART 中断的公共入口，按实例分发：

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (p_uart && huart->Instance == p_uart->Instance)
    {
        uart_drv_rx_callback(rx_byte);      // UART1 → PC 上位机协议
    }
    else if (huart->Instance == USART2)
    {
        debug_console_rx_callback();        // UART2 → CLI
    }
}
```

> **注意**：每个 UART 的接收字节变量是独立的（UART1 用 `rx_byte`，UART2 用 `cli_rx_byte`），不能混用。

---

## 四、命令表设计（cli_cmds.c）

### 4.1 命令注册机制

命令表由引擎的 `struct cmd` 定义，本项目采用**运行时动态注册**：
`cmd_table` 是定长数组，命令通过 `cli_cmd_register()` 逐个注册到表中。

```c
#define CLI_CMD_ITEMS_MAX  (30)   /* 命令表容量上限 */

struct cmd cmd_table[CLI_CMD_ITEMS_MAX] = {};
uint16_t cmd_table_size = 0;      /* 已注册命令数（运行时自增） */

typedef int (*fn_cli_cmd_t)(uint8_t argc, char **argv);

/* 注册接口: 返回 0 成功 / -1 参数空 / -2 表满 / -3 命令重复 */
int cli_cmd_register(char *name, fn_cli_cmd_t pfunc, char *desc);
```

> **注意**：原 nr_micro_shell 的 `cmd_table_size` 是 `const` 自动计算值，
> 动态注册要求其可变，因此 `nr_micro_shell.h` 中去掉了 `const` 声明。

内置命令统一在 `cli_cmds_init()` 中注册（由 `user_app_init()` 在
`cli_init()` 之后调用）：

```c
void cli_cmds_init(void)
{
    cli_cmd_register("help",     cmd_help,     "显示所有命令");
    cli_cmd_register("clear",    cmd_clear,    "清屏");
    cli_cmd_register("info",     cmd_info,     "系统信息");
    cli_cmd_register("led",      cmd_led,      "LED 控制 (led 0|1|2)");
    cli_cmd_register("rd",       cmd_rd,       "读内存 (rd <addr> [size])");
    cli_cmd_register("reboot",   cmd_reboot,   "软件复位");
    cli_cmd_register("mode",     cmd_mode,     "显示模式 (mode [local|remote])");
    cli_cmd_register("update",   cmd_update,   "进入固件更新模式");
    cli_cmd_register("cli_info", cmd_cli_info, "显示 CLI 命令信息");
}
```

**命令函数签名**：

```c
static int cmd_led(uint8_t argc, char **argv)
{
    if (argc < 2)
    {
        shell_printf("Usage: led <0|1|2>\r\n");
        return -1;
    }
    int state = atoi(argv[1]);
    // ... 调用 led_mgr 执行 ...
}
```

### 4.2 命令实现示例（led）

```c
static int cmd_led(uint8_t argc, char **argv)
{
    if (argc < 2)
    {
        shell_printf("Usage: led <0|1|2>\r\n");
        shell_printf("  0: off\r\n  1: on\r\n  2: blink\r\n");
        return -1;
    }
    int state = atoi(argv[1]);
    if (state < 0 || state > 2)
    {
        shell_printf("Error: state must be 0~2\r\n");
        return -1;
    }
    led_mgr_set_state((led_state_t)state);
    shell_printf("LED -> %d\r\n", state);
    return 0;
}
```

**设计要点**：
- **命令只做参数校验 + 调用业务模块**，不直接操作硬件（遵循分层架构）
- 参数不足/非法时打印 Usage 并返回 -1
- 命令实现文件 `cli_cmds.c` 可自由 include 各业务模块头文件

### 4.3 命令清单（本项目）

| 命令 | 功能 |
|------|------|
| `help` | 显示所有命令 |
| `clear` | ANSI 清屏 |
| `info` | 固件版本、编译时间、运行时长、当前状态 |
| `led <0|1|2>` | LED 关/开/闪烁 |
| `rd <addr>` | 读取内存（带地址合法性校验，防死机） |
| `reboot` | 软件复位 |
| `mode` | 切换显示模式 |
| `update` | 进入固件更新模式 |
| `cli_info` | 显示 CLI 命令信息（数量/名称/描述/补全词） |

### 4.4 地址合法性校验（安全技巧）

内存读取类命令（`rd`）必须校验地址范围，否则一条命令就能让设备死机：

```c
if (addr < 0x08000000UL || addr > 0x2001C000UL)
{
    shell_printf("Error: invalid address 0x%08X\r\n", addr);
    return -1;
}
```

---

## 五、CLI 移植配置（nr_micro_shell_port.h）

```c
#define NR_SHELL_HISTORY_CMD_SUPPORT      // 启用上下键历史
#define NR_SHELL_HISTORY_CMD_NUM  8       // 历史条数
#define NR_SHELL_HISTORY_CMD_SZ   64      // 每条命令最大长度
#define NR_SHELL_AUTO_COMPLETE_SUPPORT    // 启用 Tab 补全
#define NR_SHELL_MAX_LINE_SZ      80      // 命令行最大长度
#define NR_SHELL_MAX_PARAM_NUM    8       // 最大参数个数
#define NR_SHELL_PROMPT           "oled"  // 提示符 "oled: "
#define NR_SHELL_SHOW_LOGO                // 启动显示 logo
```

> 配置项全部集中在 port 头文件，**改配置不动引擎源码**，这是"可移植性"的体现。

---

## 六、踩坑与经验总结

1. **`HAL_UART_Receive_IT` 一次性机制**：每收 1 字节都要重新调用，否则串口罢工
2. **环形缓冲 2 的幂 + & 取模**：判空判满用 `(head-tail) & mask`，不要用 `==`
3. **跨中断共享变量必须 volatile**：head 在 ISR 写、tail 在主循环写，两者都是 volatile
4. **中断里只入队，主循环处理**：CLI 引擎处理放中断会导致响应时间不可控
5. **多串口分发靠 Instance 判断**：一个 RxCpltCallback 按串口实例分发到不同业务
6. **内存读取命令必须做地址校验**：防一条命令打崩系统
7. **终端需支持 ANSI 转义**：MobaXterm/SecureCRT 可以，Windows 自带超级终端不行
8. **阻塞式逐字符发送够用但慢**：命令输出较长时可考虑换 DMA 或缓冲输出

---

## 七、自测题

1. nr_micro_shell 引擎要求移植者必须实现哪两个函数？各负责什么？
2. 环形缓冲区为什么大小必须是 2 的幂？判空/判满的公式是什么？
3. 为什么"接收完成"后必须再次调用 `HAL_UART_Receive_IT`？
4. 为什么不在串口中断里直接调用 shell() 处理字符？
5. `rd` 命令为什么要校验地址？
6. 本项目 USART1 和 USART2 的接收数据如何分流？
7. 新增一条 CLI 命令需要做哪些事？

<details>
<summary>参考答案</summary>

1. `shell_putc(char c)`（输出单字符）和 `shell_get_ts_ns()`（纳秒时间戳）
2. 可以用 `& (SIZE-1)` 代替 `% SIZE` 取模；判空/满用 `(head - tail) & (SIZE-1)`（tail 是读指针）
3. HAL 的 `HAL_UART_Receive_IT` 是一次性的，回调后需重新启动才继续接收
4. 引擎处理含大量字符串操作，放中断会拉长中断响应；应中断入队、主循环处理
5. 防止非法地址访问导致 HardFault 死机
6. 在 `HAL_UART_RxCpltCallback` 中用 `huart->Instance` 判断：USART1→协议，USART2→CLI
7. 在 `cli_cmds.c` 中实现命令函数，然后在 `cli_cmds_init()` 里调用
   `cli_cmd_register("命令名", 函数, "帮助文本")` 注册（命令总数不得超过 `CLI_CMD_ITEMS_MAX`）

</details>
