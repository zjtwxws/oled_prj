# FreeRTOS 学习文档（oled_prj / STM32F407）

> 版本：V1.0  
> 日期：2026-08-18  
> 适用工程：`E:\BaiduNetdiskDownload\code\oled_prj`  
> 配套详细移植设计：[freertos-port-design.md](freertos-port-design.md)

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

### 2.6 当前项目实际示例

当前 [freertos_app.c](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/src/freertos_app.c)
中已经把原来的 `user_app_handle()` 放入 `app_task`：

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

这表示当前业务仍保持“单用户任务”模型。`vTaskDelay(pdMS_TO_TICKS(1U))` 让
`app_task` 每轮至少阻塞 1 ms，从而给 IDLE 任务和后续低优先级任务运行机会。

### 2.7 当前 app_task_handle 的用途与用法

在 [freertos_app.c](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/src/freertos_app.c:14)
中，`app_task_handle` 的定义如下：

```c
static TaskHandle_t app_task_handle = NULL;
```

创建 `app_task` 时，把 `&app_task_handle` 作为 `xTaskCreate` 的第 6 个参数传入：

```c
ret = xTaskCreate(app_task,
                  "app_task",
                  APP_TASK_STACK_WORDS,
                  NULL,
                  APP_TASK_PRIORITY,
                  &app_task_handle);

configASSERT(ret == pdPASS);
```

创建成功后，`app_task_handle` 就是 `app_task` 的任务句柄。后续要操作这个任务时，把它
传给 FreeRTOS 对应 API。当前项目已经启用了 `vTaskSuspend`、`vTaskResume`、
`vTaskDelete`、`vTaskPrioritySet` 和 `uxTaskGetStackHighWaterMark` 等接口，因此可以这样
使用：

```c
/* 挂起 app_task */
vTaskSuspend(app_task_handle);

/* 恢复 app_task */
vTaskResume(app_task_handle);

/* 删除 app_task */
vTaskDelete(app_task_handle);

/* 修改 app_task 优先级 */
vTaskPrioritySet(app_task_handle, tskIDLE_PRIORITY + 2U);

/* 查询 app_task 栈高水位，单位 word */
UBaseType_t stack_high_water = uxTaskGetStackHighWaterMark(app_task_handle);
```

当前项目暂时只保存 `app_task_handle`，还没有实际调用这些操作。它属于任务句柄预留，
后续如果要管理 `app_task` 的生命周期、优先级或栈使用情况，可以直接使用。

如果 API 是在 `app_task` 内部操作当前任务自己，也可以把任务句柄参数写成 `NULL`，例如：

```c
uxTaskGetStackHighWaterMark(NULL);
vTaskDelete(NULL);
```

## 3. 在当前工程中新增一个任务

### 3.1 先回答四个问题

| 问题 | 需要确定的内容 |
|------|----------------|
| 任务做什么 | 明确任务循环体，避免任务之间职责重叠 |
| 多长时间运行一次 | 使用 `vTaskDelay` 还是 `vTaskDelayUntil` |
| 优先级多少 | 是否必须比 `app_task` 更高，会不会抢占现有协议处理 |
| 栈用多少 | 先给一个保守值，再用 `uxTaskGetStackHighWaterMark` 实测 |

当前工程先保持“最小多任务”，新增任务前不建议直接把 `user_app_handle()` 拆散。原因
是它内部串行处理协议、CLI、按键、显示、LED 和看门狗，拆散会引入共享资源与竞态问题。

### 3.2 新增任务的标准步骤

1. 在 `freertos_app.c` 顶部增加栈深、优先级和周期宏。
2. 增加 `static TaskHandle_t xxx_task_handle = NULL;`。
3. 编写 `static void xxx_task(void *argument)` 任务函数。
4. 在 `freertos_app_init()` 中调用 `xTaskCreate`。
5. 检查 `configMAX_PRIORITIES` 和 `configTOTAL_HEAP_SIZE` 是否足够。
6. 编译三个 Keil target，观察高水位和 heap 余量。

### 3.3 示例：增加一个周期任务

下面的示例只是展示如何新增任务，不直接操作共享外设，避免和当前单任务业务产生竞态：

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

/* 在 freertos_app_init() 中追加创建逻辑 */
void freertos_app_init(void)
{
    BaseType_t ret = pdFAIL;

    ret = xTaskCreate(app_task,
                      "app_task",
                      APP_TASK_STACK_WORDS,
                      NULL,
                      APP_TASK_PRIORITY,
                      &app_task_handle);

    configASSERT(ret == pdPASS);

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
本项目当前业务模型下，更安全的方式是只让 `app_task` 继续拥有外设操作权，新任务只做
独立计算或通过队列/互斥锁与 `app_task` 通信。

### 3.4 增加任务后的检查项

| 检查项 | 方法 |
|--------|------|
| 编译是否通过 | 依次编译 `oled_cubemx`、`oled_cubemx_slota`、`oled_cubemx_slotb` |
| `xTaskCreate` 是否成功 | 确认 `configASSERT` 未触发 |
| 栈是否足够 | 开启 `configCHECK_FOR_STACK_OVERFLOW = 2`，观察 `uxTaskGetStackHighWaterMark` |
| heap 是否足够 | 增大任务后确认 `xTaskCreate` 不再返回内存错误 |
| 是否饿死原业务 | 长时间观察 OLED、菜单、串口协议、看门狗是否正常 |

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
