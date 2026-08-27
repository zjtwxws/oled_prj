# STM32 内存区域与变量存储笔记

本文以 STM32（常用 STM32F407 为例）整理嵌入式 C 语言的内存分段概念，说明代码、常量、变量、堆和栈分别存放在哪里，并给出一份完整的直观示例。

## 1. 核心概念

STM32 的存储器主要分成两类：

| 存储器 | 特点 | 主要用途 |
|---|---|---|
| Flash | 掉电不丢失，只读为主 | 代码、常量、中断向量表、`.data` 初值镜像 |
| SRAM | 掉电丢失，可读可写 | 变量、堆、栈、运行时数据 |
| CCM RAM | 紧耦合 RAM，速度快 | 需要高性能的数据，但通常不能用于 DMA |

程序运行时，代码和常量存放在 Flash 中，变量和临时数据存放在 SRAM 中。

## 2. STM32 内存空间分布

以 STM32F407 为例，不同型号的 Flash、SRAM 大小和地址可能不同，但“代码放 Flash、变量放 RAM”的规律一致。

### 2.1 Flash 区域

```text
Flash: 0x0800 0000 开始

0x0800 0000
┌──────────────────────────────┐
│ .isr_vector 中断向量表         │
├──────────────────────────────┤
│ .text 代码段                  │
├──────────────────────────────┤
│ .rodata 只读数据段             │
│   const 变量、字符串字面量       │
├──────────────────────────────┤
│ .data 的初值镜像               │
│   非零全局变量的初始值           │
└──────────────────────────────┘
```

### 2.2 SRAM 区域

```text
SRAM: 0x2000 0000 开始

低地址
┌──────────────────────────────┐
│ .data 已初始化全局/静态变量      │
├──────────────────────────────┤
│ .bss 未初始化或零初始化变量      │
├──────────────────────────────┤
│ heap 堆                       │
│   向上增长（高地址）：↑          │
├──────────────────────────────┤
│ ... 空闲区域 ...               │
├──────────────────────────────┤
│ stack 栈                      │
│   向下增长（低地址）：↓          │
├──────────────────────────────┤
│ .noinit 可选保留区             │
└──────────────────────────────┘
高地址
```

栈和堆的准确位置由链接脚本决定。一般情况下，堆从低地址向高地址增长，栈从高地址向低地址增长，两者之间的空间是空闲区。

### 2.3 CCM RAM

```text
CCM RAM: 0x1000 0000 开始，STM32F407 上为 64 KB
```

CCM 是紧耦合内存，访问速度快，但通常不能作为 DMA 缓冲区。普通变量如果明确指定也可以放到 CCM 中。

## 3. 各内存段详解

### 3.1 `.text` 代码段

存放编译后的机器指令。

```c
void delay(void)
{
    for (int i = 0; i < 1000; i++)
    {
    }
}
```

`delay()` 的指令存放在 Flash 的 `.text` 段。

### 3.2 `.rodata` 只读数据段

存放 `const` 变量、字符串字面量、常量数组。

```c
const uint8_t version = 0x01;
const char name[] = "STM32";
```

`version` 和 `"STM32"` 都放在 Flash 的 `.rodata` 段，运行时只读。

### 3.3 `.data` 已初始化数据段

存放有非零初值的全局变量或 `static` 变量。

```c
int g_count = 100;               /* 全局变量 */
static uint8_t g_mode = 0x55;    /* 静态变量 */
```

这些变量的运行空间在 RAM 中，但初始值会先保存在 Flash 中。启动时，初始化代码会把这些初值从 Flash 复制到 RAM 的 `.data` 段。

所以 `.data` 有两个地址：

- VMA：运行时地址，在 RAM 中。
- LMA：加载地址，在 Flash 中。

### 3.4 `.bss` 未初始化数据段

存放未初始化或初始化为 0 的全局变量、静态变量。

```c
int g_result;                    /* 未初始化，默认 0 */
static uint8_t rx_buf[128];      /* 未初始化 */
static int g_flag = 0;           /* 显式初始化为 0 */
```

这些变量位于 RAM 中，不占 Flash 空间。启动代码会把整块 `.bss` 清零。

常见误区：`static int flag = 0;` 虽然写了初值，但因为初值是 0，编译器通常仍把它放入 `.bss`，而不是 `.data`。

### 3.5 栈 Stack

栈由系统自动管理，用于保存：

- 函数调用返回地址。
- 局部变量。
- 函数参数。
- 中断现场和寄存器。

```c
void process(void)
{
    int local_var = 5;      /* 局部变量放在栈里 */
    uint8_t buf[64];        /* 局部数组也放在栈里 */
}
```

栈通常从高地址向低地址增长，函数返回后自动释放。局部数组不能开得太大，否则容易栈溢出。

### 3.6 堆 Heap

堆由程序手动申请和释放。

```c
uint8_t *p = malloc(128);   /* 从堆中分配 128 字节 */
free(p);                    /* 释放 */
```

在 STM32 中，`malloc` 使用 C 库堆；如果使用 FreeRTOS，任务栈、队列、信号量通常从 `pvPortMalloc` 管理的 FreeRTOS 堆中分配，两者不是同一个内存池。

### 3.7 `.noinit` 不初始化段

`.noinit` 中的变量不会被启动代码清零，也不会被初始化，适合存放复位后希望保持原值的数据。

```c
__attribute__((section(".noinit"))) uint32_t g_reset_reason;
```

注意：软复位后 SRAM 内容可能保持，但上电复位后内容一般不可靠。`.noinit` 常用于 OTA 请求标志、软件复位原因、调试信息等场景。

## 4. 变量存放位置速查

| 写法 | 存放位置 |
|---|---|
| `int a = 5;` 全局变量 | `.data` |
| `static int b = 5;` | `.data` |
| `int c;` 或 `int c = 0;` 全局变量 | `.bss` |
| `const int d = 5;` 全局变量 | `.rodata`（Flash） |
| `"hello"` 字符串字面量 | `.rodata`（Flash） |
| 函数内局部变量、局部数组 | 栈 Stack |
| `malloc()` 返回的空间 | 堆 Heap |
| 函数代码 | `.text`（Flash） |
| `__attribute__((section(".noinit")))` | `.noinit` |

## 5. 启动过程对内存的分配

1. CPU 复位后，从向量表读取第一个字作为初始栈顶 MSP。
2. 执行 `Reset_Handler`。
3. 把 `.data` 段初值从 Flash 复制到 RAM。
4. 把 `.bss` 段清零。
5. 初始化系统时钟和外设。
6. 调用 `main()`，之后局部变量开始使用栈。

## 6. 完整直观例子

下面的代码覆盖了主要内存区域。地址数值只是示意，不代表任何具体芯片的绝对地址。

```c
#include <stdint.h>
#include <stdlib.h>

/* Flash .rodata：const 变量 */
const uint8_t version = 0x01;

/* Flash .rodata：常量数组 */
const char greeting[] = "hello";

/* RAM .data：有非零初值的全局变量 */
int g_count = 100;

/* RAM .data：有非零初值的静态变量 */
static uint8_t g_mode = 0x55;

/* RAM .bss：未初始化全局变量 */
int g_result;

/* RAM .bss：未初始化静态数组 */
static uint8_t rx_buf[128];

/* RAM .bss：初始化为 0 的静态变量 */
static int g_flag = 0;

/* RAM .noinit：复位后不清零、不初始化 */
__attribute__((section(".noinit"))) uint32_t g_reset_reason;

void demo(void)
{
    /* 栈 Stack：局部变量和局部数组 */
    int local_sum = 0;
    uint8_t local_buf[32];

    /* 指针变量 p_text 在栈中；它指向的 "tmp" 在 Flash .rodata */
    const char *p_text = "tmp";

    /* 堆 Heap：malloc 动态分配的空间 */
    uint8_t *p_heap = (uint8_t *)malloc(64);

    local_sum = g_count + g_mode;

    if (p_heap != NULL)
    {
        p_heap[0] = (uint8_t)local_sum;
        free(p_heap);
    }
}
```

示例中的存放位置：

| 变量 | 所在区域 | 说明 |
|---|---|---|
| `version` | Flash `.rodata` | `const` 变量，只读 |
| `greeting` | Flash `.rodata` | 常量数组，只读 |
| `g_count` | RAM `.data` | 初值 `100` 从 Flash 复制到 RAM |
| `g_mode` | RAM `.data` | 初值 `0x55` 从 Flash 复制到 RAM |
| `g_result` | RAM `.bss` | 启动时清零 |
| `rx_buf` | RAM `.bss` | 启动时清零 |
| `g_flag` | RAM `.bss` | 初值为 0，通常归入 `.bss` |
| `g_reset_reason` | RAM `.noinit` | 复位后保持原值 |
| `local_sum` | 栈 Stack | 函数返回后自动释放 |
| `local_buf` | 栈 Stack | 局部数组，注意栈大小 |
| `p_text` | 栈 Stack | 指针变量本身在栈中 |
| `"tmp"` | Flash `.rodata` | 字符串字面量在只读段 |
| `p_heap` 指向的空间 | 堆 Heap | `malloc` 动态分配，需 `free` |
| `demo()` 的指令 | Flash `.text` | 代码段 |

对应内存分布示意：

```text
Flash 0x0800 0000
┌──────────────────────────────┐
│ .isr_vector 中断向量表         │
├──────────────────────────────┤
│ .text                        │  demo()、main() 等代码
├──────────────────────────────┤
│ .rodata                      │  version、greeting、"tmp"
├──────────────────────────────┤
│ .data 初值镜像                 │  g_count=100、g_mode=0x55
└──────────────────────────────┘

RAM 0x2000 0000
┌──────────────────────────────┐ 低地址
│ .data                        │  g_count、g_mode
├──────────────────────────────┤
│ .bss                         │  g_result、rx_buf、g_flag
├──────────────────────────────┤
│ heap                         │  p_heap 指向的 malloc 空间
│        ↑ 增长                 │
├──────────────────────────────┤
│ ... 空闲区域 ...               │
├──────────────────────────────┤
│ stack                        │  local_sum、local_buf、p_text
│        ↓ 增长                 │
├──────────────────────────────┤
│ .noinit                      │  g_reset_reason
└──────────────────────────────┘ 高地址
```

## 7. 常见易混淆点

- 栈和堆不是一回事：栈自动、快、小；堆手动、慢、大。
- `.data` 和 `.bss` 都在 RAM 中，但 `.data` 会占用 Flash 保存初值，`.bss` 不占 Flash。
- `const` 变量一般在 Flash 中，运行中修改它会出错。
- 字符串字面量放在 Flash 的 `.rodata`，不要把它当可写缓冲区使用。
- FreeRTOS 的堆与 C 库 `malloc` 堆不同，嵌入式项目中要区分开。
