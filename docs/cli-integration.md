# CLI 调试接口 — nr_micro_shell 移植文档

## 概述

基于 [nr_micro_shell](https://gitee.com/nrush/nr_micro_shell) (MIT, v2.0.0) 为 STM32F407 OLED Gateway
项目新增命令行调试接口，通过 UART2 连接 PC 终端。

**支持特性:**
- 上下键历史记录 (8 条)
- Tab 自动补全
- 光标编辑 (左右/退格/Delete)
- ANSI 转义序列 (清屏/颜色)

---

## 架构

```
main.c (MX_NVIC_Init -> USART2_IRQn)
  └─ while(1) → user_app_handle()
                   ├─ cli_poll()       ← 每帧喂字符
                   └─ 原有协议/显示逻辑

HAL_UART_RxCpltCallback (uart_drv.c)
  ├─ UART1 → uart_drv_rx_callback     ← RK3506 协议
  └─ UART2 → debug_console_rx_callback ← CLI

debug_console.c
  ├─ UART2 RX ISR → cli_rx_buf[64] 环形缓冲区
  ├─ shell_putc()  → HAL_UART_Transmit (TX)
  ├─ cli_init()    → 启动中断 + shell_init()
  └─ cli_poll()    → 取字节 → shell(c)

nr_micro_shell_core.c
  └─ shell(c): 逐字符状态机
       ├─ ESC 序列解析 (上下左右/Home/End/Delete)
       ├─ 历史记录循环队列
       ├─ Tab 自动补全
       └─ 回车 → 命令匹配 → 执行

cli_cmds.c
  └─ cmd_table[]: help/clear/info/led/rd/reboot/mode
```

---

## 硬件接线

UART2: PA2(TX) + PA3(RX), 115200-8-N-1, 无硬件流控

| STM32F407 | USB-TTL 模块 |
|-----------|-------------|
| PA2 | RXD |
| PA3 | TXD |
| GND | GND |

终端软件: MobaXterm / SecureCRT (需支持 ANSI escape 序列)

---

## 文件清单

### 新增文件

| 文件 | 行数 | 说明 |
|------|------|------|
| `inc/nr_micro_shell.h` | ~80 | 引擎 API 头文件 (原始复制) |
| `inc/nr_micro_shell_port.h` | ~40 | 移植配置: 缓冲区大小/历史条数/提示符 |
| `src/nr_micro_shell_core.c` | ~560 | 引擎实现 (原始复制) |
| `inc/cli_cmds.h` | ~15 | 命令注册接口声明 |
| `src/cli_cmds.c` | ~260 | 命令表 + 7 条调试命令实现 |

### 修改文件

| 文件 | 变更 |
|------|------|
| `inc/debug_console.h` | 新增 cli_init/cli_poll/debug_console_rx_callback 声明 |
| `src/debug_console.c` | shell_putc/shell_get_ts_ns 实现, UART2 RX 环形缓冲区, CLI 启停 |
| `src/uart_drv.c` | HAL_UART_RxCpltCallback 扩展: 判断 USART2 分发给 CLI |
| `oled_cubemx/Src/stm32f4xx_it.c` | 新增 USART2_IRQHandler |
| `oled_cubemx/Src/main.c` | MX_NVIC_Init 使能 USART2_IRQn |
| `src/user_app.c` | user_app_init 增加 cli_init(), user_app_handle 首行增加 cli_poll() |

---

## 移植步骤

### Step 1: 复制 nr_micro_shell 核心 (2 个文件)

```bash
cp nr_micro_shell_core.c  ->  stm32f407/src/
cp nr_micro_shell.h       ->  stm32f407/inc/
```

保留原始版权声明，无需修改内容。

### Step 2: 创建移植配置文件

创建 `inc/nr_micro_shell_port.h`:

```c
#define NR_SHELL_HISTORY_CMD_SUPPORT   // 启用上下键历史
#define NR_SHELL_HISTORY_CMD_NUM  8    // 历史记录条数
#define NR_SHELL_HISTORY_CMD_SZ   64   // 每条命令最大长度
#define NR_SHELL_AUTO_COMPLETE_SUPPORT // 启用 Tab 补全
#define NR_SHELL_MAX_LINE_SZ      80   // 命令行最大长度
#define NR_SHELL_MAX_PARAM_NUM    8    // 最大参数个数
#define NR_SHELL_PROMPT           "oled"  // 提示符 "oled: "
#define NR_SHELL_SHOW_LOGO              // 启动显示 logo

// 必须实现的移植函数
void shell_putc(char c);
uint64_t shell_get_ts_ns(void);
```

### Step 3: 实现移植层 (`debug_console.c`)

#### (a) shell_putc — 输出单字符

```c
void shell_putc(char c)
{
    if (p_debug_uart)
        HAL_UART_Transmit(p_debug_uart, (uint8_t *)&c, 1, HAL_MAX_DELAY);
}
```

#### (b) shell_get_ts_ns — 纳秒时间戳

```c
uint64_t shell_get_ts_ns(void)
{
    return (uint64_t)HAL_GetTick() * 1000000UL;  // ms -> ns 近似
}
```

#### (c) UART2 RX 环形缓冲区

```c
#define CLI_RX_BUF_SIZE 64  // 必须是 2 的幂
static uint8_t  cli_rx_buf[CLI_RX_BUF_SIZE];
static volatile uint16_t cli_rx_head = 0;
static volatile uint16_t cli_rx_tail = 0;
static uint8_t  cli_rx_byte;
```

> **关键规则:** head 和 tail 不截断，自然溢出 (uint16_t)。
> 数组访问时用 `& (SIZE-1)` 取模。
> 判断空/满必须用 `(head - tail) & mask`，不能直接 `==` 比较。

#### (d) ISR 回调

```c
void debug_console_rx_callback(void)
{
    uint16_t next = cli_rx_head + 1;
    if (((next - cli_rx_tail) & (CLI_RX_BUF_SIZE - 1)) != 0)
    {
        cli_rx_buf[cli_rx_head & (CLI_RX_BUF_SIZE - 1)] = cli_rx_byte;
        cli_rx_head = next;
    }
    // ★ 必须重新启动中断接收
    if (p_debug_uart)
    {
        HAL_StatusTypeDef ret = HAL_UART_Receive_IT(p_debug_uart, &cli_rx_byte, 1);
        if (ret != HAL_OK) { /* 清标志后重试 */ }
    }
}
```

#### (e) cli_poll — 主循环轮询

```c
void cli_poll(void)
{
    while (((cli_rx_head - cli_rx_tail) & (CLI_RX_BUF_SIZE - 1)) != 0)
    {
        uint8_t c = cli_rx_buf[cli_rx_tail & (CLI_RX_BUF_SIZE - 1)];
        cli_rx_tail++;
        shell(c);  // 逐字符喂给引擎
    }
}
```

#### (f) cli_init — 初始化

```c
void cli_init(void)
{
    cli_rx_head = cli_rx_tail = 0;
    HAL_UART_Receive_IT(p_debug_uart, &cli_rx_byte, 1);  // 启动首次接收
    shell_init();                                         // 初始化引擎
}
```

### Step 4: 扩展中断回调 (`uart_drv.c`)

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (p_uart && huart->Instance == p_uart->Instance)
    {
        uart_drv_rx_callback(rx_byte);     // UART1 → RK3506
    }
    else if (huart->Instance == USART2)
    {
        debug_console_rx_callback();        // UART2 → CLI
    }
}
```

> 每个 UART 有自己的接收 buffer。UART2 的数据在 `cli_rx_byte` 中，不能交叉传 UART1 的 `rx_byte`。

### Step 5: 使能 UART2 中断

`oled_cubemx/Src/stm32f4xx_it.c`:
```c
extern UART_HandleTypeDef huart2;
void USART2_IRQHandler(void) { HAL_UART_IRQHandler(&huart2); }
```

`oled_cubemx/Src/main.c` (MX_NVIC_Init):
```c
HAL_NVIC_SetPriority(USART2_IRQn, 14, 0);
HAL_NVIC_EnableIRQ(USART2_IRQn);
```

### Step 6: 集成到主程序 (`user_app.c`)

```c
int user_app_init(void)
{
    debug_console_init(&huart2);
    cli_init();              // ← 新增
    // ...
}

int user_app_handle(void)
{
    cli_poll();              // ← 新增 (首行)
    // ... 原有逻辑 ...
}
```

### Step 7: 创建命令表 (`cli_cmds.c`)

```c
struct cmd cmd_table[] =
{
    { .name = "help",   .func = cmd_help,   .desc = "显示所有命令" },
    { .name = "clear",  .func = cmd_clear,  .desc = "清屏" },
    { .name = "info",   .func = cmd_info,   .desc = "系统信息" },
    { .name = "led",    .func = cmd_led,    .desc = "LED 控制 (led 0|1|2)" },
    { .name = "rd",     .func = cmd_rd,     .desc = "读内存 (rd <addr> [size])" },
    { .name = "reboot", .func = cmd_reboot, .desc = "软件复位" },
    { .name = "mode",   .func = cmd_mode,   .desc = "显示模式 (mode [local|remote])" },
};
const uint16_t cmd_table_size = sizeof(cmd_table) / sizeof(cmd_table[0]);
```

---

## 如何新增命令

**示例: 添加 `uptime` 命令**

1. 在 `cli_cmds.c` 中实现命令函数:

```c
static int cmd_uptime(uint8_t argc, char **argv)
{
    (void)argc;
    (void)argv;
    shell_printf("System uptime: %lu ms\r\n", sys_tick_ms());
    return 0;
}
```

2. 在 `cmd_table[]` 末尾追加:

```c
{ .name = "uptime", .func = cmd_uptime, .desc = "显示系统运行时间" },
```

3. (可选) 将参数候选词加入 `auto_complete_words[]`。

无需修改任何其他文件。`cmd_table_size` 自动计算。

**命令函数签名:**
```c
static int cmd_xxx(uint8_t argc, char **argv);
//   argc: 参数个数 (含命令名本身)
//   argv: 参数字符串数组, argv[0] 即命令名
//   return: 0=成功, 非0=失败 (非0时该命令不会加入历史记录)
```

**可用输出 API:**

| 函数 | 说明 |
|------|------|
| `shell_printf(fmt, ...)` | 格式化输出到终端 |
| `show_all_cmds()` | 打印所有已注册命令 |

**可调用的系统接口** (遵循驱动层隔离规范，禁止直接调用 HAL):

| 头文件 | 接口 |
|--------|------|
| `display_mgr.h` | is_remote / get_sub_mode / set_remote |
| `led_mgr.h` | get_state / set_state |
| `sys_config.h` | reset |
| `sys_tick.h` | sys_tick_ms |
| `menu_mgr.h` | is_active |
| `user_app.h` | FW_VERSION / FW_AUTHOR / FW_BUILD_TIME |

---

## 现有命令速查

| 命令 | 用法 | 功能 |
|------|------|------|
| **help** | `help` | 列出所有命令 |
| **clear** | `clear` | 清屏 (ANSI) |
| **info** | `info` | 固件版本/编译时间/运行时间/当前状态 |
| **led** | `led 0` / `led 1` / `led 2` | LED: 关/开/闪烁 |
| **rd** | `rd 0x20000000 16` | 内存 hex dump |
| **reboot** | `reboot` | 软件复位 |
| **mode** | `mode` / `mode local` / `mode remote` | 显示模式 |

---

## 终端配置

| 终端 | 设置 |
|------|------|
| **SecureCRT** | Session Options → Terminal → **取消** "New line mode" |
| **MobaXterm** | 默认可用 |
| **PuTTY** | Terminal → Implicit CR in every LF → 选 "CR" |

**原理:** Shell 引擎已发送 `\r\n`。如果终端再自动加 CR，会出现 `\r\r\n` 导致每行重复打印提示符。

---

## 踩坑记录

| 现象 | 根因 | 修复 |
|------|------|------|
| 终端无响应 (TX 有显示, RX 无反应) | 中断回调中 `HAL_UART_Receive_IT` 只调一次, 未重启 | 每次回调末尾重新 `HAL_UART_Receive_IT` |
| 同上 | UART2 分支传了 UART1 的 `rx_byte` | 改为无参回调, 内部读 `cli_rx_byte` |
| 输入命令后死循环 | 环形缓冲区用 `head != tail` 判断空; tail 溢出后永不为空 | 改用 `(head - tail) & mask` |
| help 输出阶梯状偏移 | `show_all_cmds` 用 `\n` 缺 `\r` | 改为 `\r\n` |
| 敲回车显示两行提示符 | 终端开启了 New Line Mode | 关闭终端自动 CR 插入 |
