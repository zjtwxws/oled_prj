# FreeRTOS 信号量与互斥锁笔记

> 版本：V1.0  
> 日期：2026-08-25  
> 适用工程：`E:\BaiduNetdiskDownload\code\oled_prj`  
> 内核版本：`FreeRTOS Kernel V11.1.0`  
> 配套文档：[FreeRTOS 学习文档](freertos-learning.md)、[FreeRTOS 移植设计](freertos-port-design.md)、
> [队列深度原理](freertos-queue.md)、[任务内核原理](freertos-task-internals.md)

## 1. 一句话理解

FreeRTOS 的信号量和互斥锁都不是 STM32F407 的硬件外设，而是内核在 RAM 中创建的同步对象。

信号量是一个“计数器”，用于事件通知和资源计数。

互斥锁是一个“带持有者信息的计数器”，用于多个任务保护共享资源，并且支持优先级继承。

在单核 Cortex-M4 上，它们能正确工作的基础是：

- `taskENTER_CRITICAL()` 屏蔽当前任务被切换和低优先级中断打断
- 任务进入等待状态时，从 Ready 链表挂到对应事件链表
- give 时把等待任务放回 Ready 链表
- 需要立即切换时，通过 `portYIELD()` 触发 PendSV

## 2. 共同底层原理

### 2.1 本质都是 Queue_t

`Queue_t` 既可以表示普通队列，也可以表示信号量和互斥锁，定义见
[queue.c](../stm32f407/thirdparty/freertos/queue.c:103)。

与同步对象相关的关键字段：

| 字段 | 含义 |
|------|------|
| `uxMessagesWaiting` | 信号量当前计数，也代表队列中已有数据项数量 |
| `uxLength` | 最大计数，也代表队列长度 |
| `uxItemSize` | 数据项大小。信号量和互斥锁为 0，表示不保存真实数据 |
| `xTasksWaitingToReceive` | 等待 take 的任务链表 |
| `xTasksWaitingToSend` | 等待 give 的任务链表 |
| `u.xSemaphore.xMutexHolder` | 互斥锁当前持有任务句柄 |
| `u.xSemaphore.uxRecursiveCallCount` | 递归互斥锁的递归次数 |

`SemaphoreData_t` 定义见
[queue.c](../stm32f407/thirdparty/freertos/queue.c:77)。

### 2.2 三个关键机制

1. 计数机制  
   `uxMessagesWaiting` 就是信号量计数。take 成功减 1，give 成功加 1。

2. 原子性机制  
   对计数的“检查并修改”发生在 FreeRTOS 临界区内。临界区期间当前任务不能被切换，
   所以不会出现两个任务同时把同一个计数减两次。

3. 等待和唤醒机制  
   计数为 0 时，任务通过
   [vTaskPlaceOnEventList](../stm32f407/thirdparty/freertos/tasks.c:5243)
   把自己挂到信号量的等待链表；give 时通过
   [xTaskRemoveFromEventList](../stm32f407/thirdparty/freertos/tasks.c:5341)
   把等待任务放回 Ready 链表。

### 2.3 为什么不需要硬件锁

Cortex-M4 是单核，同一时刻只能执行一条指令流。只要 FreeRTOS 保证：

- 不发生任务切换
- 可调用 RTOS API 的中断不进来

那么“读计数、判断计数、修改计数”就是原子的。

本项目 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`，临界区会把优先级 5 到
15 的中断屏蔽，优先级 0 到 4 的中断仍然可以打断，但那些高优先级中断不应调用 FreeRTOS
API，见 [FreeRTOSConfig.h](../stm32f407/inc/FreeRTOSConfig.h:319)。

## 3. 信号量

### 3.1 概念

信号量是一个计数器，不负责“哪个任务可以释放”。

二值信号量：

```c
xSemaphoreCreateBinary()
```

内部等价于创建长度 1、项大小 0 的队列，见
[semphr.h](../stm32f407/thirdparty/freertos/include/semphr.h:167)。

二值信号量初始计数为 0：

- give：计数变为 1
- take：计数变为 0

计数信号量：

```c
xSemaphoreCreateCounting(uxMaxCount, uxInitialCount)
```

内部创建长度为 `uxMaxCount` 的队列，并把初始计数设为 `uxInitialCount`，见
[semphr.h](../stm32f407/thirdparty/freertos/include/semphr.h:1025)。

### 3.2 原理

take 的简化流程见
[xQueueSemaphoreTake](../stm32f407/thirdparty/freertos/queue.c:1652)：

```text
进入临界区
if (计数 > 0)
{
    计数减 1
    返回成功
}
else if (不等待)
{
    返回失败
}
else
{
    退出临界区
    挂起调度器
    把当前任务挂到 xTasksWaitingToReceive
    让出 CPU
}
```

give 的简化流程见
[xQueueGenericSend](../stm32f407/thirdparty/freertos/queue.c:939)：

```text
进入临界区
if (计数 < 最大计数)
{
    计数加 1
}

if (有任务在 xTasksWaitingToReceive)
{
    把最高优先级等待任务放回 Ready 链表
}

if (被唤醒任务优先级更高)
{
    触发 PendSV
}
```

所以信号量能起作用是因为：

- take 成功时，计数减 1，表示一个信号被消费
- give 成功时，计数加 1，表示一个信号产生
- 没有信号时，任务不是死循环，而是进入 Blocked 状态
- give 时由内核唤醒等待任务

### 3.3 使用场景

二值信号量：

- ISR 通知任务“发生了某个事件”
- 一个任务通知另一个任务“可以去处理一件事”
- 事件只关心“有没有”，不关心“多少次”

计数信号量：

- 统计未处理事件的数量
- 表示资源池中当前可用的资源数量
- 生产者、消费者模型

信号量不适合保护共享变量，因为它没有持有者概念，任何任务都能 give。

### 3.4 使用方法

| API | 说明 |
|-----|------|
| `xSemaphoreCreateBinary()` | 创建二值信号量，初始计数 0 |
| `xSemaphoreCreateCounting(max, initial)` | 创建计数信号量 |
| `xSemaphoreTake(sem, ticks)` | 任务上下文获取信号量，可阻塞 |
| `xSemaphoreGive(sem)` | 任务上下文释放信号量 |
| `xSemaphoreGiveFromISR(sem, &woken)` | ISR 中释放信号量 |
| `xSemaphoreTakeFromISR(sem, &woken)` | ISR 中尝试获取信号量，不阻塞 |

注意事项：

- ISR 中不能使用 `xSemaphoreTake(..., portMAX_DELAY)`，ISR 不能阻塞
- 任务中阻塞时间可以用 `pdMS_TO_TICKS(1000U)` 或 `portMAX_DELAY`
- `xSemaphoreTakeFromISR()` 只能用于非互斥锁信号量

### 3.5 示例：ISR 通知任务

```c
#include "FreeRTOS.h"
#include "semphr.h"

static SemaphoreHandle_t g_uart_rx_sem = NULL;

void app_rtos_init(void)
{
    g_uart_rx_sem = xSemaphoreCreateBinary();
    configASSERT(g_uart_rx_sem != NULL);
}

void uart_rx_driver_isr(void)
{
    BaseType_t x_higher_priority_task_woken = pdFALSE;

    xSemaphoreGiveFromISR(g_uart_rx_sem, &x_higher_priority_task_woken);
    portYIELD_FROM_ISR(x_higher_priority_task_woken);
}

static void uart_parser_task(void *argument)
{
    (void)argument;

    for (;;)
    {
        if (xSemaphoreTake(g_uart_rx_sem, portMAX_DELAY) == pdTRUE)
        {
            uart_parse_rx_data();
        }
    }
}
```

说明：如果 ISR 先给了一次信号量，任务后 take，信号量计数仍为 1，事件不会丢。

### 3.6 示例：计数信号量管理资源数量

```c
#include "FreeRTOS.h"
#include "semphr.h"

#define BUFFER_COUNT 3U

static SemaphoreHandle_t g_free_buffer_sem = NULL;
static uint8_t g_buffers[BUFFER_COUNT][64U];
static uint8_t g_next_buffer = 0U;

void buffer_pool_init(void)
{
    g_free_buffer_sem = xSemaphoreCreateCounting(BUFFER_COUNT, BUFFER_COUNT);
    configASSERT(g_free_buffer_sem != NULL);
}

uint8_t *buffer_alloc(void)
{
    uint8_t *buffer = NULL;

    if (xSemaphoreTake(g_free_buffer_sem, pdMS_TO_TICKS(1000U)) == pdTRUE)
    {
        taskENTER_CRITICAL();
        buffer = g_buffers[g_next_buffer];
        g_next_buffer = (uint8_t)((g_next_buffer + 1U) % BUFFER_COUNT);
        taskEXIT_CRITICAL();
    }

    return buffer;
}

void buffer_free(uint8_t *buffer)
{
    if (buffer != NULL)
    {
        xSemaphoreGive(g_free_buffer_sem);
    }
}
```

注意：计数信号量只保证“有多少资源可用”，分配索引本身仍需临界区或互斥锁保护。

## 4. 互斥锁

### 4.1 概念

互斥锁也是计数 0/1 的同步对象，但多保存了一个字段：当前持有任务。

创建宏：

```c
xSemaphoreCreateMutex()
```

见 [semphr.h](../stm32f407/thirdparty/freertos/include/semphr.h:735)。

内部创建互斥锁时，[prvInitialiseMutex](../stm32f407/thirdparty/freertos/queue.c:614)
会：

1. 把 `xMutexHolder` 置为 `NULL`
2. 把递归计数置为 0
3. 给一次信号，使计数为 1，表示锁当前可用

### 4.2 原理

take 成功时，互斥锁除了把计数减 1，还会执行：

```c
pxQueue->u.xSemaphore.xMutexHolder = pvTaskIncrementMutexHeldCount();
```

见 [queue.c](../stm32f407/thirdparty/freertos/queue.c:1732)。

`pvTaskIncrementMutexHeldCount()` 会：

- 返回当前任务 `pxCurrentTCB`
- 把当前任务的 `uxMutexesHeld` 加 1

见 [tasks.c](../stm32f407/thirdparty/freertos/tasks.c:7589)。

所以互斥锁与任务绑定体现在两个方向：

- 锁对象保存“谁持有”：`xMutexHolder`
- 任务 TCB 保存“持有几把锁”：`uxMutexesHeld`
- 任务 TCB 还保存“原始优先级”：`uxBasePriority`

give 时，[prvCopyDataToQueue](../stm32f407/thirdparty/freertos/queue.c:2385)
对互斥锁执行：

```c
xTaskPriorityDisinherit(pxQueue->u.xSemaphore.xMutexHolder);
pxQueue->u.xSemaphore.xMutexHolder = NULL;
```

也就是：

- 先恢复可能被提升的优先级
- 清空持有者
- 计数加 1，锁恢复可用

### 4.3 优先级继承

普通信号量没有优先级继承。

互斥锁有：

- 高优先级任务等待低优先级任务持有的互斥锁时，调用
  [xTaskPriorityInherit](../stm32f407/thirdparty/freertos/tasks.c:6580)
- 低优先级持有者临时提升到高优先级
- 持有者尽快执行完并释放锁
- 释放时调用
  [xTaskPriorityDisinherit](../stm32f407/thirdparty/freertos/tasks.c:6683)
  恢复原优先级

这样可以避免“低优先级任务持有锁，却被中优先级任务抢走 CPU，导致高优先级任务一直等锁”的优先级反转问题。

### 4.4 使用场景

互斥锁适合保护：

- 多个任务共享的全局变量或结构体
- I2C、SPI、Flash、OLED 等一次只允许一个任务使用的资源
- 必须“一个任务完整执行完一段操作，另一个任务才能进入”的代码段

互斥锁不能从 ISR 中 take 或 give。

### 4.5 使用方法

| API | 说明 |
|-----|------|
| `xSemaphoreCreateMutex()` | 创建互斥锁，初始可用 |
| `xSemaphoreCreateRecursiveMutex()` | 创建递归互斥锁 |
| `xSemaphoreTake(mutex, ticks)` | 获取互斥锁 |
| `xSemaphoreGive(mutex)` | 释放互斥锁，只能由持有者调用 |
| `xSemaphoreTakeRecursive(mutex, ticks)` | 递归获取 |
| `xSemaphoreGiveRecursive(mutex)` | 递归释放 |

### 4.6 示例：保护共享数据

```c
#include "FreeRTOS.h"
#include "semphr.h"

static SemaphoreHandle_t g_display_mutex = NULL;

void display_rtos_init(void)
{
    g_display_mutex = xSemaphoreCreateMutex();
    configASSERT(g_display_mutex != NULL);
}

void display_write_safe(const char *line1, const char *line2)
{
    if (xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(100U)) != pdTRUE)
    {
        return;
    }

    display_write_line(0U, line1);
    display_write_line(1U, line2);
    display_refresh();

    xSemaphoreGive(g_display_mutex);
}
```

说明：互斥锁只提供“任务间协议”，不能自动保护数据。如果某个任务不 take 就写共享数据，
仍然会破坏数据一致性。

### 4.7 示例：递归互斥锁

```c
#include "FreeRTOS.h"
#include "semphr.h"

static SemaphoreHandle_t g_config_mutex = NULL;

static void config_write_inner(uint32_t value)
{
    xSemaphoreTakeRecursive(g_config_mutex, portMAX_DELAY);

    config_store_value(value);

    xSemaphoreGiveRecursive(g_config_mutex);
}

void config_write(uint32_t value)
{
    xSemaphoreTakeRecursive(g_config_mutex, portMAX_DELAY);

    config_write_inner(value);

    xSemaphoreGiveRecursive(g_config_mutex);
}
```

普通互斥锁如果同一任务嵌套 take，会因为计数已经是 0 而死锁。递归互斥锁允许持有者重复
take，但 take 和 give 次数必须匹配。

## 5. 信号量与互斥锁对比

> 补充阅读：信号量与互斥锁底层的完整队列机制见
> [队列深度原理](freertos-queue.md)；任务优先级继承/调度器对锁的处理见
> [任务内核原理](freertos-task-internals.md) 第 11 节。

| 项目 | 信号量 | 互斥锁 |
|------|--------|--------|
| 底层对象 | `Queue_t` | `Queue_t` |
| 初始计数 | 二值信号量为 0；计数信号量为指定值 | 1，表示可用 |
| 是否记录持有者 | 不记录 | 记录 `xMutexHolder` |
| 谁能 give | 任意任务或 ISR | 通常只有持有者 |
| 优先级继承 | 无 | 有 |
| ISR 中使用 | 支持 `GiveFromISR` / `TakeFromISR` | 不支持 |
| 递归使用 | 不适用 | 有递归互斥锁 |
| 典型用途 | 事件通知、资源计数 | 保护共享资源 |

## 6. 选择建议

- 只通知“事件发生了”：优先二值信号量，也可以考虑任务通知
- 需要统计多个未处理事件：使用计数信号量
- 保护共享变量或外设：使用互斥锁
- 同一个函数会被嵌套调用，且需要反复加锁：使用递归互斥锁
- 只保护几条语句，且不能阻塞：可以使用临界区，但临界区不能长时间占用

## 7. 常见错误

1. 用二值信号量当互斥锁  
   二值信号量没有持有者，任何任务都能 give，容易掩盖错误。

2. take 成功后忘记 give  
   会导致其他任务永远阻塞，或互斥锁永远不可用。

3. 在 ISR 中 take 带阻塞时间的信号量  
   ISR 不能阻塞，应使用 `xSemaphoreTakeFromISR()`。

4. 在 ISR 中 give 互斥锁  
   互斥锁持有者是任务，优先级继承对 ISR 没有意义，内核也不允许。

5. 认为互斥锁能自动保护变量  
   只有所有访问共享数据的任务都遵守同一把锁时，锁才有效。

6. 多个互斥锁顺序不一致  
   任务 A 先锁 1 再锁 2，任务 B 先锁 2 再锁 1，可能死锁。应固定加锁顺序。

7. 不检查创建返回值  
   动态内存不足时创建函数会返回 `NULL`，使用前应检查。

## 8. 当前工程状态

当前工程的 [FreeRTOSConfig.h](../stm32f407/inc/FreeRTOSConfig.h:654) 已经启用：

```c
#define configUSE_MUTEXES                      1
#define configUSE_RECURSIVE_MUTEXES            1
#define configUSE_COUNTING_SEMAPHORES          1
```

动态内存、堆大小和任务优先级配置：

```c
#define configSUPPORT_DYNAMIC_ALLOCATION       1
#define configTOTAL_HEAP_SIZE                  12288
#define configMAX_PRIORITIES                   8
```

> **状态更新（2026-09-02）**：当前 `stm32f407/src` 中尚未使用 `xSemaphore` /
> `SemaphoreHandle_t`，本文示例属于接入模板。不过工程已大量使用同属 `Queue_t` 体系的
> **队列**（见 [app_ipc.c](../stm32f407/src/app_ipc.c)，6 条静态队列），任务间通信与
> 唤醒由队列 + 任务通知承担。若后续需要互斥锁保护共享资源（例如多任务访问 Flash），
> 可直接按本文第 4.6 节模板接入。

## 9. 核心源码位置

| 文件 | 位置 | 内容 |
|------|------|------|
| `thirdparty/freertos/queue.c` | `Queue_t` 定义 | 队列、信号量、互斥锁共用结构 |
| `thirdparty/freertos/queue.c` | `xQueueGenericCreate` | 信号量/互斥锁内存创建 |
| `thirdparty/freertos/queue.c` | `xQueueSemaphoreTake` | take 核心实现 |
| `thirdparty/freertos/queue.c` | `xQueueGenericSend` | give 核心实现 |
| `thirdparty/freertos/queue.c` | `prvInitialiseMutex` | 互斥锁初始化 |
| `thirdparty/freertos/queue.c` | `prvCopyDataToQueue` | 互斥锁释放时清空持有者 |
| `thirdparty/freertos/tasks.c` | `xTaskPriorityInherit` | 优先级继承 |
| `thirdparty/freertos/tasks.c` | `xTaskPriorityDisinherit` | 取消优先级继承 |
| `thirdparty/freertos/tasks.c` | `vTaskPlaceOnEventList` | 任务进入等待链表 |
| `thirdparty/freertos/tasks.c` | `xTaskRemoveFromEventList` | 任务从等待链表唤醒 |
| `thirdparty/freertos/include/semphr.h` | 所有 `xSemaphore...` 宏 | 应用层 API |
| `portable/GCC/ARM_CM4F/portmacro.h` | `portYIELD()` | 触发 PendSV |

> 队列（含信号量/互斥锁本质）的源码级逐函数分析，见 [队列深度原理](freertos-queue.md) 第 2~9 节；
> 调度与任务通知/延时机制见 [任务内核原理](freertos-task-internals.md)。
