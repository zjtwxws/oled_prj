# FreeRTOS 学习文档（oled_prj / STM32F407）

> 版本：V2.0  
> 日期：2026-09-02  
> 适用工程：`E:\BaiduNetdiskDownload\code\oled_prj`  
> 配套详细移植设计：[freertos-port-design.md](freertos-port-design.md)
> 配套深度文档：[任务内核原理](freertos-task-internals.md)、[队列深度原理](freertos-queue.md)、[信号量与互斥锁](freertos-semaphore-mutex.md)

> 说明：本文档 V2.0 起按当前工程实际代码更新。当前 `freertos_app.c` 已从早期
> “单用户任务（app_task）”模型演进为 **7 个静态任务** 的多任务模型，任务之间通过
> `app_ipc` 静态队列通信。文中涉及旧模型的示例仅作入门讲解保留。

## 1. 当前工程中的 FreeRTOS

当前 STM32F407 应用固件已经接入 FreeRTOS，相关文件如下：

| 文件 | 作用 |
|------|------|
| [stm32f407/thirdparty/freertos/include/FreeRTOS.h](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/thirdparty/freertos/include/FreeRTOS.h) | FreeRTOS 主头文件 |
| [stm32f407/thirdparty/freertos/include/task.h](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/thirdparty/freertos/include/task.h) | 任务创建、延时、调度接口 |
| [stm32f407/thirdparty/freertos/tasks.c](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/thirdparty/freertos/tasks.c) | 内核任务调度实现 |
| [stm32f407/thirdparty/freertos/portable/GCC/ARM_CM4F/port.c](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/thirdparty/freertos/portable/GCC/ARM_CM4F/port.c) | Cortex-M4F 移植层 |
| [stm32f407/thirdparty/freertos/portable/MemMang/heap_4.c](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/thirdparty/freertos/portable/MemMang/heap_4.c) | 动态内存管理 |
| [stm32f407/inc/FreeRTOSConfig.h](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/inc/FreeRTOSConfig.h) | FreeRTOS 配置 |
| [stm32f407/src/freertos_app.c](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/src/freertos_app.c) | 当前应用任务集成层 |
| [stm32f407/inc/freertos_app.h](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/inc/freertos_app.h) | 当前应用任务接口 |

注意：当前工作区内核源码文件头显示 `FreeRTOS Kernel V11.1.0`，而
`stm32f407/inc/FreeRTOSConfig.h` 的模板注释显示 `V11.3.0`。学习文档以当前实际源码中的
`xTaskCreate` 原型为准；后续如需统一到 V11.3.0，应同步替换整个 `thirdparty/freertos`
源码，而不是只替换 `FreeRTOSConfig.h`。

## 2. xTaskCreate 使用说明

### 2.1 原型

当前工程使用的 [task.h](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/thirdparty/freertos/include/task.h)
中，`xTaskCreate` 原型如下：

```c
BaseType_t xTaskCreate( TaskFunction_t pxTaskCode,
                        const char * const pcName,
                        const configSTACK_DEPTH_TYPE uxStackDepth,
                        void * const pvParameters,
                        UBaseType_t uxPriority,
                        TaskHandle_t * const pxCreatedTask ) PRIVILEGED_FUNCTION;
```

其中任务函数类型定义为：

```c
typedef void (* TaskFunction_t)( void * arg );
```

### 2.2 参数说明

| 参数 | 类型 | 说明 |
|------|------|------|
| `pxTaskCode` | `TaskFunction_t` | 任务入口函数。函数通常不能返回，必须使用 `for (;;)`，或末尾调用 `vTaskDelete(NULL)` |
| `pcName` | `const char * const` | 任务名，主要用于调试。长度受 `configMAX_TASK_NAME_LEN` 限制，本项目为 16，包含字符串结束符 |
| `uxStackDepth` | `configSTACK_DEPTH_TYPE` | 任务栈深度，单位是 word，不是 byte。本项目 Cortex-M4F 上 1 word = 4 byte |
| `pvParameters` | `void *` | 传给任务入口的参数。该对象生命周期必须覆盖任务运行期，不能传一个会失效的栈变量地址 |
| `uxPriority` | `UBaseType_t` | 任务优先级。0 最低，`configMAX_PRIORITIES - 1` 最高。本项目 `configMAX_PRIORITIES = 8`，可用值为 0~7 |
| `pxCreatedTask` | `TaskHandle_t *` | 输出参数，用于保存任务句柄。当前项目用它保存 `app_task_handle`。可传 `NULL`，传句柄后可执行挂起、恢复、删除、查栈等操作 |

### 2.3 返回值

| 返回值 | 含义 | 处理建议 |
|--------|------|----------|
| `pdPASS` | 任务创建成功，并进入 Ready 状态 | 正常继续初始化或调度 |
| `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY` | 动态内存不足，无法分配 TCB 或任务栈 | 增大 `configTOTAL_HEAP_SIZE` 或减小栈深，或改用静态创建 |

本项目当前的调用方式：

```c
BaseType_t ret = pdFAIL;

ret = xTaskCreate(app_task,
                  "app_task",
                  APP_TASK_STACK_WORDS,
                  NULL,
                  APP_TASK_PRIORITY,
                  &app_task_handle);

configASSERT(ret == pdPASS);
```

### 2.4 关键注意事项

1. `xTaskCreate` 依赖动态内存分配，必须确保 `configSUPPORT_DYNAMIC_ALLOCATION = 1`。
   本项目 [FreeRTOSConfig.h](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/inc/FreeRTOSConfig.h)
   中已配置为 1。

2. 必须先包含 `FreeRTOS.h`，再包含 `task.h`：

```c
#include "FreeRTOS.h"
#include "task.h"
```

`task.h` 内部会检查 `FreeRTOS.h` 是否已经包含，顺序错误会直接编译报错。

3. 栈参数是 word 数，不是 byte 数。当前项目 `APP_TASK_STACK_WORDS = 1024`，实际占用
   约 `1024 * 4 = 4096` byte。增大任务数量时，不要只看任务个数，还要算总栈内存。

4. `configTOTAL_HEAP_SIZE` 当前为 `12288` byte。每增加一个动态任务，都会从这 12 KB
   heap 中分配任务控制块和栈。堆不足时 `xTaskCreate` 会返回错误码。

5. 任务优先级不能随意高于业务优先级。当前 `app_task` 为 `tskIDLE_PRIORITY + 1`，即
   优先级 1；新增任务时先明确它和 `app_task`、IDLE 任务的抢占关系。

6. 任务函数内如果要周期性运行，应使用 `vTaskDelay` 或 `vTaskDelayUntil`，不要让一个
   高优先级任务空转占用 CPU。

### 2.5 最小示例

```c
#include "FreeRTOS.h"
#include "task.h"

#define DEMO_TASK_STACK_WORDS     256U
#define DEMO_TASK_PRIORITY        (tskIDLE_PRIORITY + 1U)

static TaskHandle_t demo_task_handle = NULL;

static void demo_task(void *argument)
{
    (void)argument;

    for (;;)
    {
        /* 在这里填写实际业务逻辑 */

        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

void demo_task_create(void)
{
    BaseType_t ret = pdFAIL;

    ret = xTaskCreate(demo_task,
                      "demo_task",
                      DEMO_TASK_STACK_WORDS,
                      NULL,
                      DEMO_TASK_PRIORITY,
                      &demo_task_handle);

    configASSERT(ret == pdPASS);
}
```

### 2.6 当前项目实际示例（早期模型，已演进）

早期 [freertos_app.c](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/src/freertos_app.c)
曾把原来的 `user_app_handle()` 放入 `app_task`：

```c
static void app_task(void *argument)
{
    (void)argument;

    for (;;)
    {
        user_app_handle();
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
}
```

> ⚠️ **注意**：当前工程已不再使用该单任务模型。现在 `freertos_app.c` 使用
> `xTaskCreateStatic` 创建 7 个静态任务（comm/key/display/cli/led/storage/watchdog），
> 职责拆分与优先级安排见下面 2.7/2.8。上面的代码仅用于理解“任务函数 + for(;;) + 延时”
> 的基本形态。

### 2.7 任务句柄（TaskHandle_t）的用途与用法

任务句柄用于对任务执行挂起、恢复、删除、改优先级、查栈等操作。以 `comm_task` 为例，
当前工程中的定义与创建见 [freertos_app.c](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/src/freertos_app.c)：

```c
static TaskHandle_t comm_task_handle = NULL;

/* 用静态内存创建 */
comm_task_handle = xTaskCreateStatic(comm_task,
                                     "comm",
                                     COMM_TASK_STACK_WORDS,
                                     NULL,
                                     COMM_TASK_PRIORITY,
                                     comm_stack,
                                     &comm_tcb);
```

拿到句柄后可以这样使用（接口已启用）：

```c
/* 挂起任务 */
vTaskSuspend(comm_task_handle);

/* 恢复任务 */
vTaskResume(comm_task_handle);

/* 删除任务（静态任务内存不会回收） */
vTaskDelete(comm_task_handle);

/* 修改任务优先级 */
vTaskPrioritySet(comm_task_handle, tskIDLE_PRIORITY + 2U);

/* 查询任务栈高水位，单位 word */
UBaseType_t stack_high_water = uxTaskGetStackHighWaterMark(comm_task_handle);
```

如果 API 在任务内部操作自己，也可以把任务句柄参数写成 `NULL`，例如：

```c
uxTaskGetStackHighWaterMark(NULL);
vTaskDelete(NULL);
```

当前工程在 [app_ipc.c](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/src/app_ipc.c)
中保存了 `comm_task` 句柄并用它做任务通知（`app_ipc_set_comm_task`），
这是任务句柄在项目中的实际用途之一。

### 2.8 当前工程任务总览（V2.0 起）

当前 `freertos_app.c` 创建的任务如下：

| 任务 | 静态创建 | 栈 (word) | 优先级 | 主要职责 | 运行模式 |
|------|----------|-----------|--------|----------|----------|
| `comm` | 是 | 1024 | 5（最高） | USART1 协议收发、结果回包 | 任务通知 + 2ms 轮询 |
| `key` | 是 | 512 | 4 | 按键扫描与菜单事件 | 20ms 周期 |
| `display` | 是 | 1024 | 3 | 消费显示命令、刷新 OLED | 50ms 周期 + 收队列 |
| `cli` | 是 | 512 | 2 | USART2 调试 CLI、异步日志 | 2ms 周期 |
| `led` | 是 | 256 | 1 | 消费 LED 命令、状态机 | 50ms 周期 + 收队列 |
| `storage` | 是 | 512 | 1 | Flash 配置读写 | 阻塞等命令 |
| `watchdog` | 是 | 256 | 0 | 应用级看门狗监控 | 1ms 周期 |

任务之间的数据流全部通过 [app_ipc.c](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/src/app_ipc.c)
中的 6 条静态队列（disp/led/storage/proto_tx/cmd_result/debug_log）传递，
详见 [队列深度原理](freertos-queue.md) 第 10 节。

## 3. 在当前工程中新增一个任务

### 3.1 先回答四个问题

| 问题 | 需要确定的内容 |
|------|----------------|
| 任务做什么 | 明确任务循环体，避免任务之间职责重叠 |
| 多长时间运行一次 | 使用 `vTaskDelay` 还是 `vTaskDelayUntil` |
| 优先级多少 | 插入到现有优先级阶梯（comm 5 > key 4 > display 3 > cli 2 > led/storage 1 > watchdog 0）的哪个位置 |
| 栈用多少 | 先给一个保守值，再用 `uxTaskGetStackHighWaterMark` 实测 |

当前工程已经是 7 个静态任务的多任务模型。新增任务前要注意：任务职责应单一，需要
与其他任务共享数据的场景应通过 [app_ipc](freertos-queue.md) 队列通信，避免直接操作
其他任务拥有的外设（OLED 归 `display_task`、USART1 归 `comm_task` 等）。

### 3.2 新增任务的标准步骤（静态创建）

当前工程任务全部为**静态创建**（`xTaskCreateStatic`），与早期文档的 `xTaskCreate`
不同，新增任务应按以下步骤：

1. 在 `freertos_app.c` 顶部增加栈深、优先级宏：
   `#define XXX_TASK_STACK_WORDS` / `#define XXX_TASK_PRIORITY`。
2. 增加静态栈与 TCB：`static StackType_t xxx_stack[...]; static StaticTask_t xxx_tcb;`。
3. 增加句柄：`static TaskHandle_t xxx_task_handle = NULL;`。
4. 编写 `static void xxx_task(void *argument)` 任务函数（`for(;;)` + 延时/阻塞）。
5. 在 `tasks_init()` 中调用 `create_static_task(...)`（封装了 `xTaskCreateStatic`）。
6. 检查 `configMAX_PRIORITIES` 是否够用；静态任务不消耗 `configTOTAL_HEAP_SIZE`。
7. 编译三个 Keil target，用 `tasks_info` CLI 观察状态与高水位。

> 若想改回动态创建（从 heap 分配），沿用 `xTaskCreate` 即可，但注意
> `configTOTAL_HEAP_SIZE=12288` 字节要同时容纳任务栈+TCB。

### 3.3 示例：增加一个周期任务

下面用**动态创建**演示最小增量（仅示意，当前工程风格为静态创建，见上方步骤）：

```c
/* 新增宏 */
#define HEARTBEAT_TASK_STACK_WORDS     256U
#define HEARTBEAT_TASK_PRIORITY        (tskIDLE_PRIORITY + 1U)
#define HEARTBEAT_TASK_PERIOD_MS       500U

/* 新增句柄 */
static TaskHandle_t heartbeat_task_handle = NULL;

/* 新增任务函数 */
static void heartbeat_task(void *argument)
{
    TickType_t last_wake_time = xTaskGetTickCount();

    (void)argument;

    for (;;)
    {
        /* 在这里调用项目驱动层接口，不要直接调用 HAL */

        vTaskDelayUntil(&last_wake_time,
                        pdMS_TO_TICKS(HEARTBEAT_TASK_PERIOD_MS));
    }
}

/* 在 freertos_app_init() / tasks_init() 中追加创建逻辑（此处演示动态创建） */
void demo_task_create(void)
{
    BaseType_t ret = pdFAIL;

    ret = xTaskCreate(heartbeat_task,
                      "heartbeat",
                      HEARTBEAT_TASK_STACK_WORDS,
                      NULL,
                      HEARTBEAT_TASK_PRIORITY,
                      &heartbeat_task_handle);

    configASSERT(ret == pdPASS);
}
```

如果新任务要操作 LED、串口、菜单或显示，应先确认这些模块是否允许从多个任务同时调用。
本项目当前职责模型下，更安全的方式是让任务通过 [app_ipc](freertos-queue.md) 的
`g_xxx_cmd_queue` 把命令投递给对应消费任务（`display_task` / `led_task` 等），由拥有
外设的任务统一操作硬件。

### 3.4 增加任务后的检查项

| 检查项 | 方法 |
|--------|------|
| 编译是否通过 | 依次编译 `oled_cubemx`、`oled_cubemx_slota`、`oled_cubemx_slotb` |
| 任务创建是否成功 | 静态创建检查返回句柄非 NULL，动态创建检查 `pdPASS` |
| 栈是否足够 | `configCHECK_FOR_STACK_OVERFLOW = 2` + `tasks_info` 观察高水位 |
| heap 是否足够 | 动态任务确认 `xTaskCreate` 不再返回内存错误（静态任务不占 heap） |
| 是否饿死其他任务 | 长时间观察 OLED、菜单、串口协议、看门狗是否正常 |
| 队列是否够深 | 用 `uxQueueMessagesWaiting` / `uxQueueSpacesAvailable` 检查是否溢出丢命令 |

## 4. 基于当前 STM32F407 开发板的移植说明

### 4.1 当前硬件与时钟

当前 CubeMX 工程 [oled_cubemx.ioc](/E:/BaiduNetdiskDownload/code/oled_prj/oled_cubemx/oled_cubemx.ioc)
中的关键参数如下：

| 项目 | 值 |
|------|-----|
| MCU | `STM32F407ZGT6` |
| 封装 | `LQFP144` |
| HSE | 8 MHz |
| PLL | `M=4`, `N=168`, `P=2`, `Q=4` |
| SYSCLK | 168 MHz |
| AHB | 168 MHz |
| APB1 | 42 MHz |
| APB2 | 84 MHz |
| SYS Timebase | TIM6 |
| NVIC Priority Group | `NVIC_PRIORITYGROUP_4` |
| FreeRTOS Middleware | 未启用，`Mcu.ThirdPartyNb=0` |

当前外设连接：

| 外设 | 引脚 | 说明 |
|------|------|------|
| USART1 | PA9 / PA10 | PC 上位机通信 |
| USART2 | PA2 / PA3 | 调试 CLI |
| I2C2 | PB10 / PB11 | SSD1306 OLED |
| KEY1~KEY4 | PE1~PE4 | 按键 |
| LED | PF9 | 状态灯 |

### 4.2 当前 FreeRTOS 源码位置

当前工程实际使用小写路径 `stm32f407/thirdparty/freertos`，不是旧文档中的
`ThirdParty/FreeRTOS-Kernel`。移植或修改工程时以实际目录为准。

最小必须参与编译的文件：

```text
stm32f407/thirdparty/freertos/list.c
stm32f407/thirdparty/freertos/tasks.c
stm32f407/thirdparty/freertos/queue.c
stm32f407/thirdparty/freertos/portable/GCC/ARM_CM4F/port.c
stm32f407/thirdparty/freertos/portable/MemMang/heap_4.c
```

头文件搜索路径至少包含：

```text
stm32f407/inc
stm32f407/thirdparty/freertos/include
stm32f407/thirdparty/freertos/portable/GCC/ARM_CM4F
```

### 4.3 FreeRTOSConfig.h 关键配置

当前 [FreeRTOSConfig.h](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/inc/FreeRTOSConfig.h)
已经包含 STM32F407 所需的关键宏：

| 宏 | 当前值 | 说明 |
|----|--------|------|
| `configCPU_CLOCK_HZ` | `168000000UL` | 必须与 SYSCLK 一致 |
| `configTICK_RATE_HZ` | `1000` | 1 ms 一个 tick |
| `configUSE_PREEMPTION` | `1` | 抢占式调度 |
| `configUSE_TIME_SLICING` | `1` | 同优先级时间片轮转 |
| `configMAX_PRIORITIES` | `8` | 可用优先级 0~7 |
| `configMINIMAL_STACK_SIZE` | `128` | IDLE 任务栈基准，单位 word |
| `configTOTAL_HEAP_SIZE` | `12288` | FreeRTOS 动态 heap 总大小，单位 byte |
| `configSUPPORT_DYNAMIC_ALLOCATION` | `1` | 允许 `xTaskCreate` |
| `configSUPPORT_STATIC_ALLOCATION` | `1` | 允许 `xTaskCreateStatic` |
| `configCHECK_FOR_STACK_OVERFLOW` | `2` | 开启栈溢出检测 |
| `configPRIO_BITS` | `4` | STM32F4 NVIC 使用 4 位抢占优先级 |

### 4.4 SysTick 与 TIM6 时基分离

STM32F407 只有一个 SysTick，FreeRTOS 和 HAL 都需要周期时基，所以当前工程采用：

```text
SysTick -> FreeRTOS tick / 调度
TIM6    -> HAL_IncTick() / HAL_Delay()
```

这意味着：

- `SysTick_Handler` 交给 FreeRTOS port 层，不能在业务代码中再调用 `HAL_IncTick()`。
- HAL 的 1 ms tick 由 TIM6 提供。
- 当前 CubeMX 已配置 `NVIC.TimeBase=TIM6_DAC_IRQn`，并生成
  `oled_cubemx/Src/stm32f4xx_hal_timebase_tim.c`。

### 4.5 中断向量映射

当前 [FreeRTOSConfig.h](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/inc/FreeRTOSConfig.h)
使用 Cortex-M Direct Routing：

```c
#define vPortSVCHandler      SVC_Handler
#define xPortPendSVHandler   PendSV_Handler
#define xPortSysTickHandler  SysTick_Handler
```

因此 [stm32f4xx_it.c](/E:/BaiduNetdiskDownload/code/oled_prj/oled_cubemx/Src/stm32f4xx_it.c)
中的 `SVC_Handler`、`PendSV_Handler`、`SysTick_Handler` 已经被注释掉，避免重复定义。

保留的业务中断包括：

| 中断 | 当前作用 |
|------|----------|
| `USART1_IRQHandler` | 调用 `HAL_UART_IRQHandler(&huart1)` |
| `USART2_IRQHandler` | 调用 `HAL_UART_IRQHandler(&huart2)` |
| `TIM6_DAC_IRQHandler` | 调用 `HAL_TIM_IRQHandler(&htim6)` |

### 4.6 main 启动流程

当前 [main.c](/E:/BaiduNetdiskDownload/code/oled_prj/oled_cubemx/Src/main.c)
中的启动顺序已经符合 FreeRTOS 要求：

```c
HAL_Init();
SystemClock_Config();

sys_config_set_vector_table(APP_VTOR_ADDR);

MX_GPIO_Init();
MX_I2C2_Init();
MX_USART1_UART_Init();
MX_USART2_UART_Init();

user_app_init();
freertos_app_init();
freertos_task_start();
Error_Handler();
```

要点：

- `user_app_init()` 仍在调度器启动前执行，因为它内部包含 HAL 延时依赖的启动流程。
- `freertos_app_init()` 只负责创建任务，不启动调度器。
- `freertos_task_start()` 内部调用 `vTaskStartScheduler()`。
- 调度器正常启动后不会返回；如果返回，说明创建任务失败或内存不足，应进入
  `Error_Handler()`。

### 4.7 Keil AC6 工程配置

当前 Keil 工程 [oled_cubemx.uvprojx](/E:/BaiduNetdiskDownload/code/oled_prj/oled_cubemx/MDK-ARM/oled_cubemx.uvprojx)
已经为三个 target 配置了 FreeRTOS include path 和源文件。移植到另一台机器或重建工程时，
需要确认以下项：

1. 三个 target 均包含：

```text
..\..\stm32f407\inc
..\..\stm32f407\thirdparty\freertos\include
..\..\stm32f407\thirdparty\freertos\portable\GCC\ARM_CM4F
```

2. FreeRTOS 分组至少包含 `list.c`、`tasks.c`、`queue.c`、`port.c`、`heap_4.c`。
   当前工程还加入了 `croutine.c`、`event_groups.c`、`stream_buffer.c`、`timers.c`，
   只要编译通过可以保留。

3. 应用分组包含：

```text
..\..\stm32f407\src\freertos_app.c
```

4. 三个 target 的 `APP_SLOT_A` / `APP_SLOT_B` 宏、scatter 文件和启动文件不能混用。

### 4.8 移植验证清单

| 验证项 | 通过判据 |
|--------|----------|
| 编译 | `oled_cubemx`、`oled_cubemx_slota`、`oled_cubemx_slotb` 三个 target 零错误 |
| 链接符号 | `.map` 中出现 `xTaskCreate`、`vTaskStartScheduler`、`SVC_Handler`、`PendSV_Handler`、`SysTick_Handler` |
| OLED | 启动画面后进入正常显示 |
| 菜单与按键 | 4 键导航、长按激活、VALUE/TOGGLE 均正常 |
| 串口协议 | USART1 帧收发正常，超时重传不误触发 |
| CLI | USART2 `help` / `info` 正常 |
| 看门狗 | 长时间运行不误复位 |
| OTA | A/B 槽升级流程仍可用 |

### 4.9 常见移植问题

| 问题 | 原因与处理 |
|------|------------|
| 重复定义 `SVC_Handler` | `stm32f4xx_it.c` 中仍存在未注释的 `SVC_Handler`，删除或注释它 |
| 编译找不到 `FreeRTOS.h` | Keil include path 没有包含 `stm32f407/thirdparty/freertos/include` |
| 找不到 `portmacro.h` | 没有包含 `portable/GCC/ARM_CM4F` |
| `xTaskCreate` 返回错误码 | `configTOTAL_HEAP_SIZE` 不足，或任务栈过大 |
| HAL_Delay 不工作 | TIM6 HAL 时基没有启用，或 `HAL_TIM_MODULE_ENABLED` 被注释 |
| `vTaskStartScheduler()` 后程序复位 | 栈溢出、heap 不足、中断向量错误或启动文件不一致 |

## 5. 推荐阅读顺序

1. 先看 `xTaskCreate` 参数和当前 `app_task`，理解一个任务如何创建和进入循环。
2. 再看 `vTaskDelay` / `vTaskDelayUntil`，理解任务为什么不能空转。
3. 再看 `freertos_app_init()` 和 `freertos_task_start()`，理解初始化与调度启动边界。
4. 最后看 STM32F407 移植章节，理解 SysTick/TIM6、中断映射和 Keil 工程配置。
