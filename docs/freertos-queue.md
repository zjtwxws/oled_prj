# FreeRTOS 队列深度原理与实践

> 版本：V1.0
> 日期：2026-09-02
> 适用工程：`E:\BaiduNetdiskDownload\code\oled_prj`
> 内核版本：`FreeRTOS Kernel V11.1.0`
> 配套文档：[FreeRTOS 学习文档](freertos-learning.md)、[信号量与互斥锁](freertos-semaphore-mutex.md)、[任务内核原理](freertos-task-internals.md)、[移植设计](freertos-port-design.md)

## 1. 一句话理解

队列是 FreeRTOS 中**任务与任务、任务与中断之间传递数据**的内核对象。

它本质是一个**带阻塞/唤醒机制的有界环形缓冲区**：生产者把数据"按值拷贝"进队列，
消费者把数据"按值拷贝"出来，数据只在队列里暂存，不经过任何全局变量。

信号量、互斥锁、队列集、软件定时器内部全部复用的是同一个 `Queue_t` 结构，因此把
队列原理彻底搞懂，整个 IPC 体系就通了一大半。

---

## 2. 队列的数据结构（Queue_t）

### 2.1 结构定义

`Queue_t` 定义见
[queue.c](../stm32f407/thirdparty/freertos/queue.c:103)：

```c
typedef struct QueueDefinition
{
    int8_t * pcHead;            /* 存储区起始地址 */
    int8_t * pcWriteTo;         /* 下一次写入位置 */

    union
    {
        QueuePointers_t xQueue;      /* 作为普通队列时的指针 */
        SemaphoreData_t xSemaphore;  /* 作为信号量/互斥锁时的数据 */
    } u;

    List_t xTasksWaitingToSend;     /* 因队列满而阻塞等待写入的任务链表 */
    List_t xTasksWaitingToReceive;  /* 因队列空而阻塞等待读取的任务链表 */

    volatile UBaseType_t uxMessagesWaiting; /* 当前队列中数据项数量 */
    UBaseType_t uxLength;                   /* 队列容量（数据项个数） */
    UBaseType_t uxItemSize;                 /* 每个数据项的大小（字节） */

    volatile int8_t cRxLock;        /* 接收锁计数，队列被锁定时由 ISR 记录 */
    volatile int8_t cTxLock;        /* 发送锁计数 */
    ...
} Queue_t;
```

### 2.2 关键字段解读

| 字段 | 作用 |
|------|------|
| `pcHead` / `pcWriteTo` / `pcTail` / `pcReadFrom` | 四个指针维护环形缓冲区读写位置 |
| `uxMessagesWaiting` | 当前已存数据项数；信号量场景下就是计数 |
| `uxLength` | 队列容量，创建时指定，运行期不变 |
| `uxItemSize` | 单个数据项字节数；**信号量/互斥锁为 0** |
| `xTasksWaitingToSend` | 队列满时阻塞的写者链表，按优先级排序 |
| `xTasksWaitingToReceive` | 队列空时阻塞的读者链表，按优先级排序 |
| `cRxLock` / `cTxLock` | 队列锁机制，见下文"队列锁" |

### 2.3 队列类型

创建队列时通过 `ucQueueType` 区分类型，宏定义见
[queue.h](../stm32f407/thirdparty/freertos/include/queue.h:149)：

```c
#define xQueueCreate( uxQueueLength, uxItemSize ) \
    xQueueGenericCreate( ( uxQueueLength ), ( uxItemSize ), ( queueQUEUE_TYPE_BASE ) )
```

队列可被用作：

| 类型 | 说明 |
|------|------|
| `queueQUEUE_TYPE_BASE` | 普通数据队列 |
| `queueQUEUE_TYPE_MUTEX` / `queueQUEUE_TYPE_RECURSIVE_MUTEX` | 互斥锁 |
| `queueQUEUE_TYPE_COUNTING_SEMAPHORE` | 计数信号量 |
| `queueQUEUE_TYPE_BINARY_SEMAPHORE` | 二值信号量 |
| `queueQUEUE_TYPE_SET` | 队列集 |

### 2.4 存储区布局

创建普通队列时，内核在堆（或静态数组）中分配两块内存：

```text
┌─────────────────────────────┐
│ Queue_t 控制块              │  ← 队列句柄指向这里
│   (pcHead 等指针 + 链表)     │
├─────────────────────────────┤
│ 数据存储区 (环形缓冲)         │
│   uxLength × uxItemSize     │
│   = uxQueueLength × 数据项大小 │
└─────────────────────────────┘
```

环形缓冲区的初始状态由 `xQueueGenericReset` 设置，见
[queue.c](../stm32f407/thirdparty/freertos/queue.c:303)：

```c
pxQueue->u.xQueue.pcTail = pxQueue->pcHead + ( pxQueue->uxLength * pxQueue->uxItemSize );
pxQueue->uxMessagesWaiting = 0;
pxQueue->pcWriteTo = pxQueue->pcHead;
pxQueue->u.xQueue.pcReadFrom = pxQueue->pcHead + ( ( pxQueue->uxLength - 1U ) * pxQueue->uxItemSize );
```

读指针初始指向**最后一个槽位**，这是环形缓冲"先移动指针、再读写"惯例的体现：
`prvCopyDataFromQueue` 先 `pcReadFrom += uxItemSize`，回绕到 `pcHead` 后再 memcpy，
从而让第一次读取恰好命中第 0 个槽位。

### 2.5 内存需求计算

```text
总内存 = sizeof(Queue_t) + uxQueueLength × uxItemSize
```

其中 `sizeof(Queue_t)` 与链表、追踪功能配置有关，一般在几十字节量级；数据区才是大头。
例如本项目 `g_disp_cmd_queue`（长度 4，元素 `disp_cmd_t` 含 256 字节联合体）：

```text
数据区 = 4 × sizeof(disp_cmd_t) ≈ 4 × 300+ 字节 ≈ 1.2 KB+
```

因此 IPC 结构体里的大数组（如 `DISP_TEXT_MAX = 256`）会直接吃掉 RAM，设计 IPC
消息结构时，大块数据应尽量**传指针或引用**而不是按值拷贝。

---

## 3. 核心语义：按值拷贝（Copy by Value）

FreeRTOS 队列**传递的是数据的副本**，不是引用（官方文档明确
"Items are queued by copy, not by reference"）。

### 3.1 写队列

`xQueueSend` → `xQueueGenericSend(..., queueSEND_TO_BACK)`，最终调用
[prvCopyDataToQueue](../stm32f407/thirdparty/freertos/queue.c:2390) 完成数据搬移：

```c
( void ) memcpy( ( void * ) pxQueue->pcWriteTo, pvItemToQueue, ( size_t ) pxQueue->uxItemSize );
pxQueue->pcWriteTo += pxQueue->uxItemSize;

if ( pxQueue->pcWriteTo >= pxQueue->u.xQueue.pcTail )
{
    pxQueue->pcWriteTo = pxQueue->pcHead;   /* 环形回绕 */
}
```

### 3.2 读队列

`xQueueReceive` 调用 [prvCopyDataFromQueue](../stm32f407/thirdparty/freertos/queue.c:2473)：

```c
pxQueue->u.xQueue.pcReadFrom += pxQueue->uxItemSize;

if ( pxQueue->u.xQueue.pcReadFrom >= pxQueue->u.xQueue.pcTail )
{
    pxQueue->u.xQueue.pcReadFrom = pxQueue->pcHead;
}

( void ) memcpy( ( void * ) pvBuffer, ( void * ) pxQueue->u.xQueue.pcReadFrom, ( size_t ) pxQueue->uxItemSize );
```

### 3.3 拷贝语义带来的三个结论

1. **发送端可以立即复用发送缓冲区**。`memcpy` 完成后，源缓冲区与队列无关，
   发送方的栈变量可以安全销毁。
2. **接收端必须提供足够大的缓冲区**。接收缓冲区小了会越界写坏内存
   （`memcpy` 不检查目标大小），`xQueueReceive` 只会检查缓冲区非 NULL。
3. **大结构体按值拷贝有性能开销**。每次 send/receive 都是
   `uxItemSize` 字节的 memcpy。若数据量大，应改为在队列中传递指针，让数据本体
   由共享内存（或动态分配）持有，并配合互斥锁保护生命周期。

### 3.4 传指针 vs 传值的取舍

| 方式 | 优点 | 缺点 |
|------|------|------|
| 按值传递小结构（≤ 几十字节） | 无共享变量，天然无竞态 | 多一次 memcpy |
| 传指针（8 字节） | memcpy 开销极小 | 指针指向的对象生命周期与互斥保护需自行管理 |

本项目 `app_ipc` 采用的是**按值传小命令结构体**，各任务之间不共享可变全局变量，
是最稳妥的多任务通信方式。

---

## 4. 发送与接收的完整流程

### 4.1 xQueueSend（任务上下文）

宏展开见 [queue.h](../stm32f407/thirdparty/freertos/include/queue.h:513)，实现在
[xQueueGenericSend](../stm32f407/thirdparty/freertos/queue.c:939)。简化流程：

```text
for (;;)
{
    进入临界区
    if (队列未满 或 允许覆盖)
    {
        prvCopyDataToQueue()         // memcpy 写入环形缓冲，uxMessagesWaiting++
        if (有任务阻塞在 xTasksWaitingToReceive)
        {
            把最高优先级等待任务放回 Ready 链表
            if (被唤醒任务优先级更高) 触发 PendSV
        }
        退出临界区
        return pdPASS
    }

    if (xTicksToWait == 0)           // 不等待
    {
        退出临界区
        return errQUEUE_FULL
    }

    退出临界区
    挂起调度器 + 锁队列
    if (还没超时 且 队列仍满)
    {
        vTaskPlaceOnEventList(&xTasksWaitingToSend, xTicksToWait)  // 进入 Blocked
        解锁队列
    }
    恢复调度器
    if (没被唤醒) 让出 CPU（taskYIELD_WITHIN_API）
}
```

### 4.2 xQueueReceive（任务上下文）

实现在 [xQueueReceive](../stm32f407/thirdparty/freertos/queue.c:1502)。简化流程：

```text
for (;;)
{
    进入临界区
    if (uxMessagesWaiting > 0)       // 队列有数据
    {
        prvCopyDataFromQueue()       // memcpy 读出，uxMessagesWaiting--
        if (有任务阻塞在 xTasksWaitingToSend)   // 队列腾出空间
        {
            把最高优先级等待写者放回 Ready 链表
            if (优先级更高) 触发 PendSV
        }
        退出临界区
        return pdPASS
    }

    if (xTicksToWait == 0)
    {
        退出临界区
        return errQUEUE_EMPTY
    }

    退出临界区
    挂起调度器 + 锁队列
    if (还没超时 且 队列仍空)
    {
        vTaskPlaceOnEventList(&xTasksWaitingToReceive, xTicksToWait)  // 进入 Blocked
        解锁队列
    }
    恢复调度器
    if (没被唤醒) 让出 CPU
}
```

### 4.3 阻塞时发生了什么

当队列空/满且指定了等待时间，任务调用
[vTaskPlaceOnEventList](../stm32f407/thirdparty/freertos/tasks.c:5243)：

1. 把任务从 Ready 链表摘下；
2. 把任务的 `xEventListItem` 挂到队列的 `xTasksWaitingToReceive` / `xTasksWaitingToSend`
   （链表按优先级降序排列，`uxItemValue = configMAX_PRIORITIES - uxPriority`）；
3. 任务进入 Blocked 状态，不再占用 CPU；
4. 超时定时器同时生效——如果等待时间到达仍没被唤醒，任务被放回 Ready 链表，
   本次 API 返回超时错误码。

唤醒时，对端调用
[xTaskRemoveFromEventList](../stm32f407/thirdparty/freertos/tasks.c:5341)，
把等待任务移回 Ready 链表；若被唤醒任务优先级高于当前任务，通过 PendSV 立即切换。

### 4.4 一个典型生产者-消费者时序

```text
时间轴     生产者任务                         消费者任务
  │        xQueueSend(队列, &data, 0)
  │          ├─ 队列有空间 → memcpy
  │          └─ return pdPASS
  │                                            xQueueReceive(队列, &buf, portMAX_DELAY)
  │                                              ├─ 队列空 → 挂到 xTasksWaitingToReceive
  │                                              └─ Blocked（不占 CPU）
  │        xQueueSend(队列, &data2, 0)
  │          ├─ memcpy 写入
  │          ├─ 唤醒最高优先级消费者
  │          └─ (若消费者优先级更高) PendSV 切换
  │                                            ← 被唤醒，从队列读出 data2
  ▼                                            ← 继续执行
```

---

## 5. 原子性与队列锁（Locking）

### 5.1 为什么"检查+修改"是原子的

单核 Cortex-M4 上，FreeRTOS 通过两个层次保证队列操作不被破坏：

1. **临界区**：`taskENTER_CRITICAL()` 屏蔽可调用 FreeRTOS API 的中断，并禁止任务切换，
   保证"读计数 → 判断 → 修改计数 → 搬数据"一气呵成。
2. **队列锁**：当任务因为队列空/满需要阻塞时，不能长时间待在临界区里（否则调度器
   无法工作），于是先 `vTaskSuspendAll()` 挂起调度器，再用 `prvLockQueue` 把队列锁定，
   之后才操作事件链表。

### 5.2 队列锁解决的问题

问题场景：任务 A 在 `xQueueReceive` 中检查到队列空，准备把任务挂到事件链表，
此时 ISR 给队列发了一条数据。若 ISR 直接去事件链表里唤醒任务，而任务还没挂上去，
这条唤醒就丢失了。

解决方式：

```text
任务上下文：                     ISR：
  挂起调度器                       ┌─ 若队列未锁：直接改事件链表并唤醒
  锁队列 (cRxLock=queueLOCKED)    ├─ 若队列已锁：只搬数据，
  把任务挂到事件链表                   并把 cRxLock 加 1（记录"有数据在锁定期间到达"）
  解锁队列 (prvUnlockQueue)       ┘
     └─ 检查 cRxLock：有 ISR 在锁定期间写入了数据
         → 重新扫描事件链表，把等待任务唤醒
```

`prvUnlockQueue` 的实现在 [queue.c](../stm32f407/thirdparty/freertos/queue.c:2590) 附近，
它会循环处理 `cTxLock` / `cRxLock` 计数，直到把锁定期间积压的"需要唤醒的任务"全部处理完。

因此**队列锁不是互斥锁**，它不阻止 ISR 读写数据，只延迟事件链表的更新，
保证"任务挂链表"与"ISR 唤醒"这两个动作不会交错出错。

---

## 6. ISR 安全 API

### 6.1 为什么需要 FromISR 版本

ISR 中不能阻塞、不能切换任务，也不能长时间关中断。因此内核提供一套独立的
FromISR API，它们：

- 不等待、不阻塞，队列满/空立即返回错误码；
- 不直接切换上下文，而是通过 `pxHigherPriorityTaskWoken` 输出"是否有更高优先级任务
  被唤醒"，由 ISR 末尾的 `portYIELD_FROM_ISR()` 决定是否切换；
- 若队列被锁，只更新锁计数，由解锁方统一处理事件链表。

### 6.2 API 对照

| 任务上下文 | ISR 版本 | 差异要点 |
|-----------|----------|----------|
| `xQueueSend` / `xQueueSendToBack` | `xQueueSendToBackFromISR` | ISR 版带 `pxHigherPriorityTaskWoken` |
| `xQueueSendToFront` | `xQueueSendToFrontFromISR` | 同上 |
| `xQueueOverwrite` | `xQueueOverwriteFromISR` | 仅限长度 1 的队列 |
| `xQueueReceive` | `xQueueReceiveFromISR` | ISR 版不阻塞 |
| `xQueuePeek` | `xQueuePeekFromISR` | 窥视不取走 |
| `xSemaphoreGive` | `xSemaphoreGiveFromISR` | 见信号量文档 |
| `xSemaphoreTake` | `xSemaphoreTakeFromISR` | 仅限非互斥锁 |

### 6.3 标准 ISR 使用模式

```c
static void uart_rx_isr(void)
{
    BaseType_t x_higher_priority_task_woken = pdFALSE;
    uint8_t byte;

    while (uart_hw_rx_byte(&byte) == true)
    {
        if (xQueueSendToBackFromISR(g_rx_queue, &byte,
                                    &x_higher_priority_task_woken) != pdPASS)
        {
            /* 队列满，数据丢弃或计数统计 */
            rx_drop_count++;
            break;
        }
    }

    portYIELD_FROM_ISR(x_higher_priority_task_woken);
}
```

注意：`portYIELD_FROM_ISR` 会检查标志，只有确实唤醒了更高优先级任务才触发 PendSV，
避免每次中断都做无谓切换。

### 6.4 ISR 中断优先级约束

只有 CMSIS 数值优先级 ≥ `configMAX_SYSCALL_INTERRUPT_PRIORITY`（本项目为 5）的中断
才能调用 FromISR API。本项目 USART1/USART2 为 14、TIM6 为 15，均符合。
若中断优先级数值小于 5（更紧急），在其中调用 FreeRTOS API 会破坏内核临界区，
导致不可预期的错误，需用 `portASSERT_IF_INTERRUPT_PRIORITY_INVALID()` 在调试期捕获。

---

## 7. 队列高级 API

### 7.1 xQueueSendToFront / xQueueSendToBack

| API | 行为 |
|-----|------|
| `xQueueSendToFront` | 插入队首，后到者先出（类似"高优先级消息"） |
| `xQueueSendToBack` | 插入队尾，FIFO，最常用 |
| `xQueueSend` | 宏，等价于 `xQueueSendToBack` |

`xQueueSendToFront` 底层通过 `prvCopyDataToQueue` 的 `queueSEND_TO_FRONT` 分支实现：
把新数据写进 `pcReadFrom` 前面的槽位，并让读指针前移。

### 7.2 xQueueOverwrite

- 只能用于**长度 1**的队列（否则 `configASSERT` 失败）；
- 队列满时直接覆盖旧数据，不等待、不返回失败；
- 适合"最新状态覆盖旧状态"的场景：如保存"最近一次按键值"、OLED 当前对比度等，
  旧数据没有保留价值。

```c
static QueueHandle_t g_last_key_queue;

/* 创建：长度 1 */
g_last_key_queue = xQueueCreate(1U, sizeof(uint8_t));

/* 覆盖写入（任何时候都成功） */
xQueueOverwrite(g_last_key_queue, &key_value);

/* 读取（空时返回 pdFAIL） */
if (xQueueReceive(g_last_key_queue, &key_value, 0) == pdPASS) { ... }
```

### 7.3 xQueuePeek

- 读取数据但**不移除**，队列中的数据保持不变；
- 常用于"先看看再决定"，或发送方确认队列里是否已有同类型数据；
- 阻塞语义与 `xQueueReceive` 一致。

### 7.4 查询与重置

| API | 作用 |
|-----|------|
| `uxQueueMessagesWaiting` | 当前数据项数（等价于信号量计数） |
| `uxQueueSpacesAvailable` | 剩余空间 = `uxLength - uxMessagesWaiting` |
| `uxQueueMessagesWaitingFromISR` | ISR 中查询，不加临界区（更快） |
| `uxQueueGetQueueLength` | 队列容量 |
| `uxQueueGetQueueItemSize` | 数据项大小 |
| `xQueueReset` | 清空队列并解除阻塞写者（注意：不解除阻塞读者，见 `xQueueGenericReset` 注释） |

---

## 8. 队列集（Queue Set）

> 本项目 `configUSE_QUEUE_SETS = 0`，未启用。以下为扩展知识。

### 8.1 解决的问题

多个队列/信号量需要"谁先有数据就处理谁"的场景。例如一个任务同时等待串口数据队列、
按键队列、网络队列。若只在一个队列上阻塞，其他队列的数据会延迟处理。

### 8.2 用法

```c
QueueSetHandle_t xSet = xQueueCreateSet( 4 );        /* 容量 = 所有成员队列长度之和 */
xQueueAddToSet( g_rx_queue, xSet );
xQueueAddToSet( g_key_queue, xSet );

/* 阻塞等待任意成员就绪，返回的是"哪个队列有数据" */
QueueSetMemberHandle_t xReadyQueue =
    xQueueSelectFromSet( xSet, portMAX_DELAY );

/* 从实际就绪的队列中读取 */
BaseType_t got = xQueueReceive( xReadyQueue, &data, 0 );
```

### 8.3 注意事项

- 队列集容量必须 ≥ 所有成员队列的长度之和；
- 互斥锁不能加入队列集；
- 从 ISR 发送数据到成员队列时，队列集容器也会收到通知；
- 读数据必须用 `xQueueSelectFromSet` 返回的句柄，不能直接读原队列。

---

## 9. 与信号量、互斥锁、任务通知的关系

| 对象 | 底层 | 数据区 | 关键区别 |
|------|------|--------|----------|
| 数据队列 | `Queue_t` | 有（`uxItemSize > 0`） | 传递数据 |
| 信号量 | `Queue_t` | 无（`uxItemSize = 0`） | 只计数，不传数据；无持有者 |
| 互斥锁 | `Queue_t` | 无 | 记录持有者，支持优先级继承 |
| 任务通知 | 任务 TCB 内嵌 | 无 | 不占 RAM、更快，但只能一对一 |

- 信号量 give/take 本质是 `xQueueGenericSend` / `xQueueSemaphoreTake`
  （见 [queue.c](../stm32f407/thirdparty/freertos/queue.c:1652)），计数就是
  `uxMessagesWaiting`；
- 互斥锁 give 时还会做优先级去继承
  （见 [prvCopyDataToQueue](../stm32f407/thirdparty/freertos/queue.c:2390) 的
  `queueQUEUE_IS_MUTEX` 分支）；
- **任务通知可以替代"只发信号不带数据"的队列/信号量**，更快且省内存，
  但不能像队列那样传递数据，也不支持多对一（每个任务只有一个通知值）。

详细对比见 [信号量与互斥锁](freertos-semaphore-mutex.md) 第 5 节。

---

## 10. 项目实际应用：app_ipc 静态队列体系

### 10.1 队列清单

全部队列在 [app_ipc.c](../stm32f407/src/app_ipc.c:39) 用 `xQueueCreateStatic` 创建，
采用**静态分配**（不占用 FreeRTOS 堆）：

| 队列 | 长度 | 元素类型 | 生产者 | 消费者 |
|------|------|----------|--------|--------|
| `g_disp_cmd_queue` | 4 | `disp_cmd_t` | 菜单/CLI/协议处理 | `display_task` |
| `g_led_cmd_queue` | 8 | `led_cmd_t` | 菜单/CLI/协议处理 | `led_task` |
| `g_storage_cmd_queue` | 4 | `storage_cmd_t` | 菜单/CLI | `storage_task` |
| `g_proto_tx_queue` | 8 | `proto_tx_req_t` | 各任务上报 | `comm_task` |
| `g_cmd_result_queue` | 8 | `cmd_result_t` | display/led/storage 任务 | `comm_task`（经 `comm_wait_results`） |
| `g_debug_log_queue` | 16 | `debug_log_item_t` | 各模块日志 | `cli_task` |

### 10.2 静态队列的声明方式

```c
/* 数据存储区：静态数组 */
static uint8_t disp_cmd_storage[DISP_CMD_QUEUE_LEN * sizeof(disp_cmd_t)];

/* 控制块：StaticQueue_t */
static StaticQueue_t disp_cmd_ctrl;

/* 句柄：全局导出 */
QueueHandle_t g_disp_cmd_queue = NULL;

/* 创建 */
g_disp_cmd_queue = xQueueCreateStatic(DISP_CMD_QUEUE_LEN,
                                      sizeof(disp_cmd_t),
                                      disp_cmd_storage,
                                      &disp_cmd_ctrl);
configASSERT(g_disp_cmd_queue != NULL);
```

优点：编译期确定内存、不依赖 `configTOTAL_HEAP_SIZE`、可 `vQueueDelete` 时不会误释放
（内核通过 `ucStaticallyAllocated` 标记识别）。

### 10.3 一条完整命令链：LED 控制

```text
PC 上位机 / RK3506
   │ 协议帧 CMD_LED_CTRL
   ▼
USART1 中断 → uart_drv 环形缓冲
   ▼
comm_task（被任务通知唤醒）
   ├─ user_app_comm_process() → process_frame()
   │    ├─ 组装 led_cmd_t，app_ipc_send_led_cmd(&cmd, 20ms)  ──► g_led_cmd_queue
   │    └─ comm_wait_results(seq, cmd, 1, 100ms)
   │         └─ 阻塞轮询 g_cmd_result_queue，按 seq/cmd 匹配结果
   ▼
led_task（50ms 周期，先收完队列）
   ├─ xQueueReceive(g_led_cmd_queue) → apply_led_cmd()
   ├─ 成功且 need_result → app_ipc_send_apply_result()  ──► g_cmd_result_queue
   └─ 成功且 report_status → app_ipc_send_proto_tx()     ──► g_proto_tx_queue
   ▼
comm_task 再次被唤醒 → xQueueReceive(g_proto_tx_queue) → 发送应答帧
```

要点：

1. `app_ipc_send_led_cmd` 等待 20 ms，避免队列满时无限阻塞；
2. `comm_wait_results` 用**超时 + seq/cmd 匹配**处理"等待异步结果"，不阻塞在单一
   队列上（避免死锁），见 [user_app.c](../stm32f407/src/user_app.c:264)；
3. `app_ipc_send_proto_tx` 发送后调用 `app_ipc_notify_comm()` 唤醒 `comm_task`，
   这是"队列 + 任务通知"组合的典型用法——队列传数据，通知减少轮询延迟。

### 10.4 本项目任务通知与队列的分工

```c
/* ISR 中：通知 comm_task 有串口数据（任务通知，不经过队列） */
app_ipc_notify_comm_from_isr(&woken);
portYIELD_FROM_ISR(woken);

/* comm_task 主体：先收通知再取数据 */
for (;;)
{
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2U));

    if (uart_drv_available() > 0U)
    {
        user_app_comm_process();
    }

    proto_tx_req_t tx;
    while (xQueueReceive(g_proto_tx_queue, &tx, 0) == pdPASS)
    {
        user_app_send_proto_tx(&tx);
    }
}
```

设计意图：串口数据本身在 `uart_drv` 的环形缓冲里，ISR 只需要"有数据"这个事件信号，
用任务通知开销最小；`g_proto_tx_queue` 这类需要携带数据的才用队列。

### 10.5 阻塞与轮询的取舍

| 队列 | 消费方式 | 理由 |
|------|----------|------|
| `g_storage_cmd_queue` | `portMAX_DELAY` 阻塞 | 存储操作不频繁，任务平时完全休眠 |
| `g_disp_cmd_queue` 等 | 周期轮询（0 超时） | display/led 任务本身有 50ms 刷新周期，顺带收队列 |
| `g_proto_tx_queue` | 0 超时 + 通知 | comm_task 由通知驱动，队列只做兜底 |

---

## 11. 常见错误与排查

### 11.1 常见错误

| 错误 | 症状 | 原因与对策 |
|------|------|-----------|
| 接收缓冲区过小 | 内存踩踏、任务栈被破坏 | `xQueueReceive` 不检查目标大小，确保缓冲区 ≥ `uxItemSize` |
| 发送后立即改数据 | 队列中数据"变了" | 按值拷贝后源缓冲区已无关，此问题不应出现；若出现说明传了指针 |
| ISR 中调用阻塞 API | HardFault / 死机 | ISR 只能调 FromISR 版本 |
| 队列满丢数据 | 协议无响应 | 检查长度、消费者是否被饿死、wait 时间是否够 |
| 消费者被饿死 | 低优先级任务长期不运行 | 高优先级任务循环里加 `vTaskDelay` 或阻塞 |
| 静态队列和动态队列混淆 | 删除队列后崩溃 | 静态队列不能被 `vQueueDelete` 释放内存 |
| 互斥锁 give 后他人 take | 保护失效 | 互斥锁只有持有者能 give；信号量则无此限制 |

### 11.2 调试手段

- 用 `uxQueueMessagesWaiting` / `uxQueueSpacesAvailable` 打印队列水位；
- 本项目已注册 `tasks_info` CLI 命令（见 [freertos_app.c](../stm32f407/src/freertos_app.c:444)），
  可观察各任务状态与栈余量，间接判断队列消费是否正常；
- 启用 `configQUEUE_REGISTRY_SIZE > 0` + `vQueueAddToRegistry` 后，可用调试器
  直接观察队列名称与状态（本项目当前为 0）；
- `configASSERT` 开启时，`xQueueSend` 对非法参数（如长度非 1 却 overwrite）会在
  调试期直接报错。

---

## 12. 设计建议

1. **优先按值传小结构体**，IPC 消息控制在几十字节内；
2. **队列长度宁大勿小**，但不要盲目放大——每个槽位都占 RAM
   （`长度 × 元素大小`）；
3. **发送方设置合理等待时间**（如 `pdMS_TO_TICKS(20U)`），避免无限阻塞；
4. **消费任务不要在循环里长时间空转**，用阻塞或延时让出 CPU；
5. **大块数据传指针**，并配合互斥锁保护共享缓冲区的生命周期；
6. **ISR → 任务**用 FromISR API，输出 `woken` 并 `portYIELD_FROM_ISR`；
7. **事件通知优先用任务通知**，需要携带数据才用队列；
8. **多路等待**用队列集，**最新值覆盖**用 `xQueueOverwrite`；
9. 队列只解决"数据搬运与同步"，**不解决共享资源互斥**，共享变量仍需互斥锁。
