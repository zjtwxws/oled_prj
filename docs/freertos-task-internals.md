# FreeRTOS 任务内核原理（TCB / 调度器 / 上下文切换）

> 版本：V1.0
> 日期：2026-09-02
> 适用工程：`E:\BaiduNetdiskDownload\code\oled_prj`
> 内核版本：`FreeRTOS Kernel V11.1.0`
> 配套文档：[FreeRTOS 学习文档](freertos-learning.md)、[队列深度原理](freertos-queue.md)、[信号量与互斥锁](freertos-semaphore-mutex.md)、[移植设计](freertos-port-design.md)

## 1. 一句话理解

任务 = **一段独立的执行流**。FreeRTOS 为每个任务保存一套完整的 CPU 寄存器现场，
通过周期性的 tick 中断（SysTick）和 PendSV 中断在这些现场之间来回切换，
让多个任务"看起来"在同时运行。

任务不是线程（没有 MMU 隔离），也不是进程（没有独立的地址空间）。所有任务共享
同一份内存，靠**任务栈 + TCB** 区分彼此的执行状态。

---

## 2. 任务控制块 TCB

### 2.1 结构定义

TCB 定义见 [tasks.c](../stm32f407/thirdparty/freertos/tasks.c:357) 附近：

```c
typedef struct tskTaskControlBlock
{
    volatile StackType_t * pxTopOfStack;  /* 当前任务栈顶（第一个成员，port 汇编直接取它） */

    ListItem_t xStateListItem;            /* 状态链表节点：任务在 Ready/Blocked 等哪个链表里 */
    ListItem_t xEventListItem;            /* 事件链表节点：任务在等哪个队列/信号量 */

    UBaseType_t uxPriority;               /* 当前优先级（可能被互斥锁临时提升） */
    StackType_t * pxStack;                /* 栈底（低地址端，Cortex-M 向下生长） */

    char pcTaskName[ configMAX_TASK_NAME_LEN ];

    #if ( configUSE_MUTEXES == 1 )
        UBaseType_t uxBasePriority;       /* 基础优先级（优先级继承用的"原值"） */
        UBaseType_t uxMutexesHeld;        /* 当前持有几把互斥锁 */
    #endif

    #if ( configUSE_TASK_NOTIFICATIONS == 1 )
        volatile uint32_t ulNotifiedValue[ configTASK_NOTIFICATION_ARRAY_ENTRIES ];
        volatile uint8_t  ucNotifyState[ configTASK_NOTIFICATION_ARRAY_ENTRIES ];
    #endif

    #if ( configUSE_TRACE_FACILITY == 1 )
        UBaseType_t uxTCBNumber;          /* 递增序号，调试用 */
    #endif
    ...
} TCB_t;
```

### 2.2 为什么 pxTopOfStack 必须是第一个成员

Cortex-M4F 移植层（[port.c](../stm32f407/thirdparty/freertos/portable/GCC/ARM_CM4F/port.c)）
的 PendSV 处理器直接用汇编操作 TCB：

```asm
ldr r3, pxCurrentTCBConst   ; 取 pxCurrentTCB 地址
ldr r2, [r3]                ; r2 = pxCurrentTCB（TCB 指针）
str r0, [r2]                ; 把新栈顶写进 TCB 第一个成员（pxTopOfStack）
```

所以 `pxTopOfStack` 必须位于偏移 0，汇编代码才能零偏移存取。

### 2.3 任务句柄就是 TCB 指针

```c
typedef struct tskTaskControlBlock * TaskHandle_t;
```

`xTaskCreate` 返回的句柄、`NULL` 语义（表示"当前任务"）都由 `prvGetTCBFromHandle`
宏统一处理：

```c
#define prvGetTCBFromHandle( pxHandle )  ( ( ( pxHandle ) == NULL ) ? pxCurrentTCB : ( pxHandle ) )
```

---

## 3. 调度器数据结构

tasks.c 中的关键全局（[tasks.c:470](../stm32f407/thirdparty/freertos/tasks.c:470) 附近）：

```c
static List_t pxReadyTasksLists[ configMAX_PRIORITIES ];  /* 8 个 Ready 链表，按优先级 */
static List_t xDelayedTaskList1;                          /* 延时/超时任务链表 A */
static List_t xDelayedTaskList2;                          /* 延时/超时任务链表 B（防溢出双缓冲） */
static List_t * pxDelayedTaskList;                        /* 当前正使用的延时链表 */
static List_t * pxOverflowDelayedTaskList;                /* tick 溢出时使用的另一条 */
static List_t xPendingReadyList;                          /* 调度器挂起期间被唤醒的任务 */
static List_t xTasksWaitingTermination;                   /* 已删除但内存未回收的任务 */
static List_t xSuspendedTaskList;                         /* 被 vTaskSuspend 挂起的任务 */

static volatile UBaseType_t uxTopReadyPriority;   /* 最高就绪优先级缓存 */
static volatile TickType_t xTickCount;            /* tick 计数 */
static volatile BaseType_t xSchedulerRunning;     /* 调度器是否已启动 */
static TCB_t * volatile pxCurrentTCB;             /* 当前运行任务 */
```

### 3.1 Ready 链表组：调度核心

```text
pxReadyTasksLists[7]  →  任务A(高优先)
pxReadyTasksLists[6]  →  （空）
pxReadyTasksLists[5]  →  comm_task → ...
pxReadyTasksLists[4]  →  key_task
pxReadyTasksLists[3]  →  display_task
pxReadyTasksLists[2]  →  cli_task
pxReadyTasksLists[1]  →  led_task, storage_task
pxReadyTasksLists[0]  →  IDLE, watchdog_task
```

调度器每次切换时：

1. 从 `uxTopReadyPriority` 往下找第一个非空链表；
2. 用 `listGET_OWNER_OF_NEXT_ENTRY` 取链表中"下一个"任务（轮转，实现同优先级
   时间片轮转）；
3. 把 `pxCurrentTCB` 指向它。

### 3.2 延时链表（Delayed List）与双缓冲

`vTaskDelay`/超时等待的任务按**唤醒时刻排序**插入延时链表。为处理 tick 计数回绕，
使用**两个链表轮换**（见 [xTaskIncrementTick](../stm32f407/thirdparty/freertos/tasks.c:4670)）：

```text
xTickCount 递增到回绕 (0) 时：
    taskSWITCH_DELAYED_LISTS() 交换 pxDelayedTaskList / pxOverflowDelayedTaskList
```

`xNextTaskUnblockTime` 缓存"下一个要唤醒的任务的时刻"，tick 处理时先比较
`xTickCount >= xNextTaskUnblockTime`，避免每个 tick 都遍历整条延时链表。

### 3.3 为什么任务只有一个 xStateListItem

任务的**状态 = 它在哪条链表上**：

| 状态 | 所在链表 |
|------|----------|
| Ready | `pxReadyTasksLists[uxPriority]` |
| Blocked（延时/超时） | `pxDelayedTaskList` 或 `pxOverflowDelayedTaskList` |
| Blocked（无限期，portMAX_DELAY） | `xSuspendedTaskList` |
| Suspended | `xSuspendedTaskList` |
| Deleted（待清理） | `xTasksWaitingTermination` |
| 调度器挂起期间被唤醒 | `xPendingReadyList` |

`xStateListItem` 只能同时挂在一条链表上，`xEventListItem` 用于挂在队列/信号量的
等待链表上（只与"等对象"相关，与状态链表解耦）。

---

## 4. 任务的五种状态

| 状态 | 含义 | 进入方式 | 离开方式 |
|------|------|----------|----------|
| Running | 正在占用 CPU | 调度器选中 | 被抢占/阻塞/主动让出 |
| Ready | 就绪，等待调度 | 创建/唤醒/超时 | 调度器选中 |
| Blocked | 因等待对象或延时阻塞 | 队列/信号量/延时/通知 | 对象就绪/超时 |
| Suspended | 被显式挂起，不参与调度 | `vTaskSuspend` | `vTaskResume`/`xTaskResumeFromISR` |
| Deleted | 删除但内存未回收 | `vTaskDelete` | IDLE 任务清理 |

本项目 `tasks_info` CLI（见 [freertos_app.c](../stm32f407/src/freertos_app.c:444)）
用 `uxTaskGetSystemState` 打印各任务状态：`RUN`/`RDY`/`BLK`/`SUS`/`DEL`。

---

## 5. 调度器启动流程

### 5.1 vTaskStartScheduler

[vTaskStartScheduler](../stm32f407/thirdparty/freertos/tasks.c:3665)：

```text
1. prvCreateIdleTasks()       创建 IDLE 任务（本项目静态内存由内核提供）
2. portDISABLE_INTERRUPTS()   关中断，防止 tick 早于调度启动
3. xSchedulerRunning = pdTRUE
4. xPortStartScheduler()      port 层接管：
     ├─ 校验 SVC/PendSV 向量已正确安装（configCHECK_HANDLER_INSTALLATION=1）
     ├─ 配置 SysTick（vPortSetupTimerInterrupt，1kHz）
     ├─ 使能 FPU、设置 PendSV/SysTick 为最低优先级
     └─ prvPortStartFirstTask()：置 MSP、开中断、svc 0
5. 永不返回（除非出错）
```

### 5.2 prvPortStartFirstTask 与 SVC

```asm
svc 0                    ; 触发 SVCall
```

`vPortSVCHandler`（[port.c:265](../stm32f407/thirdparty/freertos/portable/GCC/ARM_CM4F/port.c:265)）：

```asm
ldr r0, [r1]             ; r0 = pxCurrentTCB->pxTopOfStack
ldmia r0!, {r4-r11, r14} ; 弹出"非自动保存"寄存器 + EXC_RETURN
msr psp, r0              ; 设置进程栈指针
msr basepri, r0(0)       ; 清 BASEPRI（解除临界区屏蔽）
bx r14                   ; 返回到任务上下文
```

这就是**第一个任务如何开始运行**：任务创建时 `pxPortInitialiseStack` 已经在
任务栈里伪造好了一套"仿佛刚被中断打断"的寄存器现场，SVC 处理程序把它原样弹出来，
CPU 就从任务函数的入口开始执行。

### 5.3 IDLE 任务

- 调度器启动时自动创建，优先级最低（0）；
- 主要职责：回收被删除任务的 TCB/栈（`prvCheckTasksWaitingTermination`）；
- 可挂 `vApplicationIdleHook`（本项目 `configUSE_IDLE_HOOK=0`）；
- `configIDLE_SHOULD_YIELD=1` 时，若同优先级有就绪任务会让出 CPU；
- IDLE 任务运行时间间接反映系统空闲度（配合 run-time stats）。

---

## 6. 上下文切换（Context Switch）

### 6.1 触发时机

| 触发源 | 机制 |
|--------|------|
| tick 中断（SysTick） | 每 1ms 一次，处理超时任务、时间片轮转 |
| 主动让出（`taskYIELD`/API 内部） | 直接置 PendSV 位 |
| API 唤醒更高优先级任务 | `portYIELD` → 置 PendSV |
| ISR 中唤醒任务 | `portYIELD_FROM_ISR` → 置 PendSV |

### 6.2 PendSV 为什么用最低优先级

Cortex-M 规定 PendSV 与 SysTick 的优先级可配置。FreeRTOS 把它们设为**最低**
（数值 15），保证：

1. 高优先级中断（USART 等）随时可以打断任务切换，切换不会被延迟；
2. 多个中断同时在等待时，PendSV 最后执行，避免"切换做到一半又被另一个中断打断"
   的竞态。

### 6.3 xPortPendSVHandler 完整流程

[xPortPendSVHandler](../stm32f407/thirdparty/freertos/portable/GCC/ARM_CM4F/port.c:505)：

```asm
; ---- 保存当前任务现场 ----
mrs r0, psp                    ; r0 = 当前任务的 PSP
tst r14, #0x10                 ; 检查 EXC_RETURN 是否使用 FPU
vstmdbeq r0!, {s16-s31}        ; 若用了 FPU，先压 VFP 高寄存器
stmdb r0!, {r4-r11, r14}       ; 压 r4~r11 和 EXC_RETURN（r0-r3,r12,LR,PC,xPSR 由硬件自动压栈）
str r0, [r2]                   ; 新栈顶写回 pxCurrentTCB->pxTopOfStack

; ---- 选下一个任务（C 函数） ----
bl vTaskSwitchContext          ; 内部执行 taskSELECT_HIGHEST_PRIORITY_TASK()

; ---- 恢复新任务现场 ----
ldr r0, [r1]                   ; r0 = 新任务 pxTopOfStack
ldmia r0!, {r4-r11, r14}       ; 弹出非自动保存寄存器
msr psp, r0                    ; 设置 PSP
bx r14                         ; 退出中断 → 硬件自动弹出剩余寄存器 → 进入新任务
```

关键点：

- Cortex-M 硬件在**进入中断时自动压栈** `r0-r3, r12, LR, PC, xPSR`（8 个字），
  软件只需手动保存 `r4-r11` 和 EXC_RETURN（以及 FPU 寄存器）；
- FPU 采用**惰性保存**（lazy stacking），只有任务确实用过 FPU 才压 16 个 VFP 寄存器；
- 切换发生在 PendSV 里，所以"当前运行到哪一行"的信息完整保留在任务栈中，
  恢复现场后任务从被打断的指令继续执行，用户代码无感知。

### 6.4 vTaskSwitchContext 与任务选择

[vTaskSwitchContext](../stm32f407/thirdparty/freertos/tasks.c:5056)（单核分支）：
1. 调度器挂起则置 `xYieldPendings` 并返回（不切换）；
2. 栈溢出检测 `taskCHECK_FOR_STACK_OVERFLOW()`；
3. `taskSELECT_HIGHEST_PRIORITY_TASK()` 选任务。

本项目 `configUSE_PORT_OPTIMISED_TASK_SELECTION=0`，走通用 C 算法：
从 `uxTopReadyPriority` 向下找非空链表，用 `listGET_OWNER_OF_NEXT_ENTRY`
轮转取出下一个任务。若开启（=1），ARM CM4F port 会用 CLZ（前导零）指令
在位图 `uxTopReadyPriority` 上 O(1) 找最高优先级，见
[portmacro.h](../stm32f407/thirdparty/freertos/portable/GCC/ARM_CM4F/portmacro.h:148)。

### 6.5 时间片轮转

`configUSE_PREEMPTION=1` + `configUSE_TIME_SLICING=1` 时，每个 SysTick 中断里
`xTaskIncrementTick` 检查：**当前优先级的 Ready 链表里是否不止一个任务**，
是则请求切换（见 [tasks.c:4794](../stm32f407/thirdparty/freertos/tasks.c:4794) 附近）。
这就是同优先级任务的"公平轮转"。

---

## 7. 任务创建原理

### 7.1 动态创建 xTaskCreate

[xTaskCreate](../stm32f407/thirdparty/freertos/tasks.c:1718) 流程：

```text
1. 分配 TCB：pvPortMalloc(sizeof(TCB_t))
2. 分配栈：  pvPortMalloc(uxStackDepth * sizeof(StackType_t))   // 单位 word！
3. prvInitialiseNewTask()：
     ├─ 栈用 0xa5 填充（供栈水位检测）
     ├─ 计算 pxTopOfStack（栈顶向下找 8 字节对齐）
     ├─ 填 TCB 字段：优先级/名字/链表项
     ├─ pxPortInitialiseStack()：在栈顶伪造"初始上下文"
     └─ prvAddTaskToReadyList()：挂到对应优先级 Ready 链表
4. 若调度器已运行且新任务优先级更高 → 立即 yield
```

### 7.2 pxPortInitialiseStack：伪造初始现场

[pxPortInitialiseStack](../stm32f407/thirdparty/freertos/portable/GCC/ARM_CM4F/port.c:202)：

```text
栈顶（高地址）
  ├─ xPSR = 0x01000000     （Thumb 位，必须置 1）
  ├─ PC   = 任务函数地址
  ├─ LR   = prvTaskExitError （任务返回时"无处可去"，触发断言）
  ├─ R12/R3/R2/R1
  ├─ R0   = pvParameters    （任务参数从这进函数）
  ├─ EXC_RETURN = 0xFFFFFFFD （表示"从线程模式+PSP 返回"，让任务用 PSP）
  └─ R11..R4
```

所以任务函数看起来就像"刚从一次中断返回"：`bx lr` 用 EXC_RETURN 切到 PSP，
弹出伪现场，PC 跳到任务入口，R0 = 参数。

### 7.3 任务返回会怎样

任务函数如果 `return`，LR 里是 `prvTaskExitError`：
`prvTaskExitError` 会 `configASSERT` 失败并死循环，提醒你任务不能返回。
所以任务要么 `for(;;)`，要么 `vTaskDelete(NULL)` 自杀。

### 7.4 静态创建 xTaskCreateStatic

本项目 [freertos_app.c](../stm32f407/src/freertos_app.c) 全部使用静态创建：

```c
static StackType_t comm_stack[COMM_TASK_STACK_WORDS];  /* 静态栈数组 */
static StaticTask_t comm_tcb;                          /* 静态 TCB 存储 */

comm_task_handle = xTaskCreateStatic(comm_task, "comm",
                                     COMM_TASK_STACK_WORDS, NULL,
                                     COMM_TASK_PRIORITY,
                                     comm_stack, &comm_tcb);
```

`StaticTask_t` 的大小与 `TCB_t` 完全相同（内核用 `configASSERT(sizeof(StaticTask_t)
== sizeof(TCB_t))` 校验），编译期就分配好，不占用 FreeRTOS heap。

### 7.5 静态 vs 动态对比

| 项目 | xTaskCreate | xTaskCreateStatic |
|------|-------------|-------------------|
| 内存来源 | FreeRTOS heap（`configTOTAL_HEAP_SIZE`） | 编译期静态数组 |
| 失败表现 | 返回 `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY` | 返回 NULL |
| 可删除回收 | 是（IDLE 任务 vPortFree） | 内存不回收（`ucStaticallyAllocated` 标记） |
| RAM 利用率 | 按需分配 | 始终占用 |
| 适用场景 | 数量动态变化 | 数量固定、要确定性 |

本项目 7 个应用任务全部静态创建（数量固定、要确定性），IDLE 任务由内核
以静态方式创建（`configKERNEL_PROVIDED_STATIC_MEMORY=1`，见
[vApplicationGetIdleTaskMemory](../stm32f407/thirdparty/freertos/tasks.c:8584)）。

---

## 8. vTaskDelay 与 vTaskDelayUntil 原理

### 8.1 vTaskDelay（相对延时）

[vTaskDelay](../stm32f407/thirdparty/freertos/tasks.c:2435)：

```text
if (xTicksToDelay > 0)
{
    vTaskSuspendAll();          // 挂起调度器
    prvAddCurrentTaskToDelayedList(xTicksToDelay, pdFALSE);
    //   ├─ 从 Ready 链表摘除当前任务
    //   ├─ 计算唤醒时刻 = xTickCount + xTicksToDelay
    //   └─ 按唤醒时刻排序插入延时链表（溢出则插入溢出链表）
    xTaskResumeAll();           // 恢复调度器 → 触发一次切换
}
else
{
    taskYIELD();                // 延时 0 只是让出 CPU
}
```

注意：**延时从调用时刻开始算**。如果任务因为高优先级任务抢占而晚于预期到达
`vTaskDelay`，总周期会漂移。

### 8.2 vTaskDelayUntil（绝对延时/周期任务）

[vTaskDelayUntil](../stm32f407/thirdparty/freertos/tasks.c:2343)：

```c
xTimeToWake = *pxPreviousWakeTime + xTimeIncrement;   /* 下一次要醒的时刻 = 上次+周期 */

if (xTimeToWake > xTickCount)                          /* 还没到点 */
{
    prvAddCurrentTaskToDelayedList(xTimeToWake - xTickCount, pdFALSE);
}
else
{
    /* 已经过点：本次不睡，下个周期再睡（防漂移） */
    *pxPreviousWakeTime = xTimeToWake;
    xShouldDelay = pdFALSE;
}
```

用法固定模式：

```c
TickType_t last_wake = xTaskGetTickCount();
for (;;)
{
    /* ... 周期工作 ... */
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(50U));
}
```

优点：即使某次执行超时（被抢占/阻塞），周期仍以**绝对时刻**对齐，长时间运行
不累积漂移。本项目 `key_task`（20ms）、`display_task`、`led_task`（50ms）
都用了 `vTaskDelayUntil`。

### 8.3 两者对比

| 项目 | vTaskDelay | vTaskDelayUntil |
|------|-----------|-----------------|
| 语义 | 相对：睡 N tick | 绝对：睡到 last+N tick |
| 是否漂移 | 会累积漂移 | 不累积（漏拍会跳过一拍） |
| 用途 | 一次延时、节流 | 固定频率周期任务 |
| 参数 | tick 数 | `TickType_t*` 上次唤醒时刻 + 周期 |

### 8.4 延时任务如何被唤醒

SysTick 中断 → [xTaskIncrementTick](../stm32f407/thirdparty/freertos/tasks.c:4670)：

```text
xTickCount++
if (xTickCount == 0) 交换延时/溢出链表
if (xTickCount >= xNextTaskUnblockTime)   // 有任务到点
{
    for (;;)
    {
        取延时链表头（唤醒时刻最早）
        if (xTickCount < 头任务唤醒时刻) { 更新 xNextTaskUnblockTime; break; }
        从延时链表摘除
        if (挂在事件链表上) 也从事件链表摘除     // 超时唤醒
        prvAddTaskToReadyList()                // 放回 Ready
        if (优先级 > 当前) 请求切换
    }
}
```

---

## 9. 任务通知（Task Notification）

### 9.1 本质

每个任务的 TCB 里内嵌：

```c
volatile uint32_t ulNotifiedValue[ N ];  /* 32 位通知值（可用作计数） */
volatile uint8_t  ucNotifyState[ N ];    /* 通知状态：未等待/等待中/已收到 */
```

任务通知**不需要创建对象、不需要额外的 RAM**，直接把"信号"写进目标任务的 TCB。
`configUSE_TASK_NOTIFICATIONS=1`（本项目已开启），默认 N=1。

### 9.2 常用 API

| API | 作用 |
|-----|------|
| `xTaskNotifyGive(task)` | 通知值 +1（类计数信号量 give） |
| `vTaskNotifyGiveFromISR(task, &woken)` | ISR 中 +1 |
| `ulTaskNotifyTake(clear, timeout)` | 等待通知值非 0，可清零/减 1（类 take） |
| `xTaskNotify(task, value, eAction)` | 带值通知：置位/覆盖/加 1 等 |
| `xTaskNotifyWait(...)` | 等待并取回通知值 |

### 9.3 本项目用法：通知驱动的串口任务

见 [app_ipc.c](../stm32f407/src/app_ipc.c) 与 [freertos_app.c](../stm32f407/src/freertos_app.c)：

```c
/* ISR 里（USART1 收到数据） */
app_ipc_notify_comm_from_isr(&woken);   // vTaskNotifyGiveFromISR
portYIELD_FROM_ISR(woken);

/* comm_task 里 */
for (;;)
{
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2U));  // 等通知，最多 2ms
    if (uart_drv_available() > 0U) { user_app_comm_process(); }
    ...
}
```

用任务通知替代"空转轮询"，任务在没有串口数据时休眠，省 CPU 且响应及时。

### 9.4 任务通知 vs 信号量/队列

| 项目 | 任务通知 | 信号量 | 队列 |
|------|----------|--------|------|
| 内存 | 无额外开销（TCB 内嵌） | 一个 Queue_t | Queue_t + 数据区 |
| 速度 | 最快（无链表遍历对象） | 较快 | 较慢（memcpy） |
| 多对一 | 支持（多人 notify 一人） | 支持 | 支持 |
| 一对多（广播） | 不支持 | 不支持 | 不支持 |
| 携带数据 | 一个 32 位值 | 不携带 | 任意大小 |
| 在 TCB 中的状态 | `ucNotifyState` | 事件链表 | 事件链表 |

---

## 10. vTaskSuspend / vTaskResume / vTaskDelete

### 10.1 vTaskSuspend

[vTaskSuspend](../stm32f407/thirdparty/freertos/tasks.c:3121)：

```text
1. 从当前状态链表摘除（Ready/延时链表都可）
2. 若挂在事件链表上，一并摘除        // 挂起时放弃等待
3. 清掉"等待通知"标记
4. 挂到 xSuspendedTaskList
5. 若挂起的是当前任务 → 立即切换
```

Suspended 任务**不参与调度、不被超时唤醒**，只有 `vTaskResume` /
`xTaskResumeFromISR` 能恢复。

### 10.2 vTaskResume

[vTaskResume](../stm32f407/thirdparty/freertos/tasks.c:3353)：

```text
若任务确实在 Suspended 链表：
    摘除 → prvAddTaskToReadyList → 若优先级更高则 yield
```

注意：Suspended 与"Blocked 无限期"（`portMAX_DELAY`）都挂 `xSuspendedTaskList`，
`vTaskResume` 通过检查 `xEventListItem` 的容器区分二者。

### 10.3 vTaskDelete

[vTaskDelete](../stm32f407/thirdparty/freertos/tasks.c:2199)：

- 删除**非当前任务**：立即 `prvDeleteTCB` 回收 TCB/栈（`INCLUDE_vTaskDelete=1`）；
- 删除**当前任务**：挂到 `xTasksWaitingTermination`，由 IDLE 任务下次运行时
  `prvCheckTasksWaitingTermination` 回收（任务不能在运行中释放自己的栈）；
- 句柄随之失效，切勿再使用。

### 10.4 动态内存回收提醒

只有 `xTaskCreate` 动态创建的任务，`vTaskDelete` 才会真正 `vPortFree`；
静态创建的任务内存**不会回收**，删除只是让 TCB 和栈永久闲置（或成为内存泄漏）。

---

## 11. 优先级继承的调度侧机制

互斥锁（见信号量文档）触发优先级继承时，内核做的是**把持有者 TCB 从当前优先级的
Ready 链表搬到更高优先级的 Ready 链表**，并同步更新事件链表项值。相关内核函数：

- [xTaskPriorityInherit](../stm32f407/thirdparty/freertos/tasks.c:6580)：
  提升持有者到当前任务优先级，移动 Ready 链表归属；
- [xTaskPriorityDisinherit](../stm32f407/thirdparty/freertos/tasks.c:6683)：
  give 时把持有者优先级还原为 `uxBasePriority`；
- [vTaskPriorityDisinheritAfterTimeout](../stm32f407/thirdparty/freertos/tasks.c:6777)：
  等待者超时放弃时，只还原到"仍在等待同一把锁的最高优先级任务"的水平。

TCB 里两个字段分工：

| 字段 | 含义 |
|------|------|
| `uxPriority` | 当前生效优先级（可能被临时提升） |
| `uxBasePriority` | 用户设定的基础优先级（继承结束后要回到的值） |

所以 `tasks_info` 打印的是 `uxCurrentPriority`（`TaskStatus_t` 也同时提供
`uxBasePriority`，可观察是否有任务正处于继承状态）。

---

## 12. 任务栈管理

### 12.1 栈分配与方向

- 单位是 **word**（Cortex-M4 上 4 字节），`xTaskCreate` 参数即 word 数；
- Cortex-M 栈向下生长（`portSTACK_GROWTH = -1`）；
- 创建时用 `0xa5` 填充整片栈（`tskSTACK_FILL_BYTE`）；
- 栈顶按 8 字节对齐（AAPCS 要求）。

### 12.2 栈溢出检测

`configCHECK_FOR_STACK_OVERFLOW`：

| 值 | 检测方式 | 触发点 |
|----|----------|--------|
| 1 | 比较 `pxTopOfStack` 是否越过栈底 | 上下文切换时（vTaskSwitchContext） |
| 2 | 额外检查栈底 4 个 word 是否仍为 0xa5a5a5a5 | 同上（本项目为 2） |

触发后调用 `vApplicationStackOverflowHook(xTask, pcName)`。注意：
方式 2 只在切换时检查，某些溢出可能漏报，仅作辅助手段。

### 12.3 栈高水位

`uxTaskGetStackHighWaterMark(task)`：从栈底往高处数还有多少 word 保持 0xa5，
返回的是**历史最低剩余量**（高水位是单调不增的），见
[tasks.c:6371](../stm32f407/thirdparty/freertos/tasks.c:6371)。
本项目 `tasks_info` CLI 即用它观察各任务 `Free(W)` 列。

调栈步骤：

1. 先给保守值（本项目 256~1024 words）；
2. 跑最长路径（最大帧、最深菜单、最坏负载）；
3. 查高水位，留 30%~50% 余量收栈。

### 12.4 本项目任务栈配置速查

| 任务 | 栈(words) | 字节 | 优先级 | 周期/驱动 |
|------|-----------|------|--------|-----------|
| comm | 1024 | 4096 | 5 | 通知驱动 + 2ms 轮询兜底 |
| key | 512 | 2048 | 4 | 20ms 周期 |
| display | 1024 | 4096 | 3 | 50ms 周期 |
| cli | 512 | 2048 | 2 | 2ms 周期 |
| led | 256 | 1024 | 1 | 50ms 周期 |
| storage | 512 | 2048 | 1 | 阻塞等待命令 |
| watchdog | 256 | 1024 | 0 | 1ms 喂狗监控 |

合计静态栈约 17 KB（不含 IDLE）。`display_task` 栈大是因为 `disp_cmd_t` 含
256 字节文本数组 + `apply_disp_cmd` 调用链深。

---

## 13. 调度器挂起（SuspendAll / ResumeAll）与临界区

### 13.1 两个层次

| 机制 | 调用 | 作用 |
|------|------|------|
| 临界区 | `taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()` | 关中断（BASEPRI 屏蔽可调 API 的中断），禁止任务切换 |
| 调度器挂起 | `vTaskSuspendAll()` / `xTaskResumeAll()` | 不关中断，但禁止任务切换 |

### 13.2 Cortex-M4F 上的实现

临界区通过写 **BASEPRI** 寄存器实现（[portmacro.h:213](../stm32f407/thirdparty/freertos/portable/GCC/ARM_CM4F/portmacro.h:213)）：

```c
msr basepri, configMAX_SYSCALL_INTERRUPT_PRIORITY   // = 5<<4 = 0x50
```

BASEPRI=0x50 表示：屏蔽优先级**数值 ≥ 0x50**（即 CMSIS 数值 5~15）的中断，
而数值 0~4 的更紧急中断**不受影响**——这正是"临界区内仍允许紧急中断"的原因。

### 13.3 调度器挂起期间发生了什么

- tick 中断仍会触发，但 `xTaskIncrementTick` 检测到 `uxSchedulerSuspended != 0`
  就只做 `xPendedTicks++`，把"欠下的 tick"记下来（见 [tasks.c:4905](../stm32f407/thirdparty/freertos/tasks.c:4905) 附近）；
- ISR 唤醒的任务先挂 `xPendingReadyList`；
- `xTaskResumeAll()` 时：补跑欠的 tick、把 `xPendingReadyList` 的任务搬进 Ready 链表，
  最后统一决定是否需要切换。

所以内核 API 内部（如 `xQueueReceive` 的阻塞路径）都用
"挂起调度器 → 操作链表 → 恢复调度器"保证操作原子性，而不是长期关中断。

---

## 14. 调度相关配置与调优建议

### 14.1 本项目 FreeRTOSConfig.h 关键调度配置

| 宏 | 值 | 含义 |
|----|----|------|
| `configUSE_PREEMPTION` | 1 | 抢占式调度 |
| `configUSE_TIME_SLICING` | 1 | 同优先级时间片轮转 |
| `configTICK_RATE_HZ` | 1000 | 1ms tick |
| `configMAX_PRIORITIES` | 8 | 优先级 0~7 |
| `configUSE_PORT_OPTIMISED_TASK_SELECTION` | 0 | 通用 C 算法选任务（可用 CLZ 优化） |
| `configCHECK_FOR_STACK_OVERFLOW` | 2 | 栈溢出检测（切换时） |
| `configUSE_TICKLESS_IDLE` | 0 | 不启用低功耗 tickless |
| `configUSE_TASK_NOTIFICATIONS` | 1 | 任务通知可用 |
| `configKERNEL_PROVIDED_STATIC_MEMORY` | 1 | IDLE 任务静态内存由内核提供 |

### 14.2 调整任务数量时的检查清单

1. 优先级是否冲突：先想清楚抢占关系（本项目 comm>key>display>cli>led=storage>watchdog=IDLE）；
2. 同优先级任务共享时间片：`configUSE_TIME_SLICING=1` 时每个 tick 轮转一次；
3. 每个任务的高水位是否稳定：用 `tasks_info` 观察；
4. 周期任务必须用 `vTaskDelayUntil`，避免漂移；
5. 交互型任务（等事件）优先用任务通知/信号量阻塞，而不是轮询。

---

## 15. 常见问题排查

| 症状 | 可能原因 | 排查手段 |
|------|----------|----------|
| 系统卡死 / HardFault | 某任务栈溢出 | `vApplicationStackOverflowHook` 打印任务名；看 `tasks_info` 高水位 |
| 高优先级任务饿死低优先级 | 高优先任务死循环不阻塞 | 高优先任务加 `vTaskDelay` 或阻塞等待 |
| 周期任务越来越慢 | 用了 `vTaskDelay` 做周期 | 换 `vTaskDelayUntil` |
| 删除任务后崩溃 | 句柄还被人使用 | 删除前通知所有使用者，句柄置 NULL |
| 任务不运行 | 创建失败 / 优先级过低 / 被挂起 | 检查 `xTaskCreate` 返回值；`tasks_info` 看状态 |
| 中断里调 API 死机 | 用了非 FromISR API 或优先级过高 | 确认中断数值优先级 ≥5；只调 `...FromISR` |
| 删除任务后内存不降 | 静态任务不回收 | 属于正常现象，注意别误以为是泄漏 |
