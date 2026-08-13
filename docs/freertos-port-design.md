# STM32F407 FreeRTOS 手动移植 需求与设计文档

> 版本: V2.0 | 日期: 2026-08-13 | 适用: oled_prj 项目 (stm32f407 应用固件)

---

## 一、需求规格

### 1.1 目标

在 STM32F407 应用固件中引入 FreeRTOS，将现有「超级循环」模型升级为「单用户任务 + RTOS 调度器」模型。本次采用**手动下载 FreeRTOS 内核源码并手动集成**，不使用 STM32CubeMX 的 FreeRTOS Middleware，也不使用 CMSIS-RTOS V1/V2 包装层。

CubeMX 只用于生成和维护与 STM32 硬件相关的配置及 HAL 驱动，例如 GPIO、I2C、USART、RCC、NVIC、TIM6 HAL 时基。

### 1.2 范围

| 项 | 范围 | 说明 |
|----|------|------|
| **移植对象** | 仅 APP 应用固件（slotA / slotB） | Bootloader 保持裸机，不引入 RTOS |
| **内核版本** | 首选 FreeRTOS-Kernel V11.3.0 | 官方 GitHub release 手动下载；本地 FW 包中的 V10.3.1 仅作离线参考 |
| **接口标准** | 原生 FreeRTOS API | `xTaskCreate` / `vTaskStartScheduler`，不引入 `cmsis_os2.h` |
| **任务模型** | 最小改造：单任务 | 先跑通，避免大规模线程安全改造 |
| **内存方案** | heap_4 动态分配 | 任务栈和内核对象由 FreeRTOS 堆分配 |
| **HAL 时基** | TIM6 | SysTick 让给 FreeRTOS，HAL 的 1 ms tick 由 TIM6 提供 |

### 1.3 非目标（本期不做）

- 多任务拆分（uart_rx / cli / display / led 各自独立任务）
- USART1/2 改为 RTOS 队列接收
- 软件定时器（`configUSE_TIMERS = 0`）
- Bootloader 的 RTOS 化
- 低功耗 tickless idle
- GCC Makefile 构建路径验收

### 1.4 约束

| 约束 | 来源 | 要求 |
|------|------|------|
| **CubeMX 不生成 FreeRTOS** | 用户要求 | 不勾选 Middleware → FreeRTOS，`Mcu.ThirdPartyNb` 保持 `0` |
| **应用层禁止直接调 HAL/CMSIS** | `AGENTS.md` | RTOS 集成层只调 FreeRTOS API 和驱动层接口，不直接调用 HAL |
| **C 编码规范** | `AGENTS.md` | `stm32f407/src/*.c` 和 `stm32f407/inc/*.h` 遵循 Allman 大括号、4 空格缩进、文件/函数头注释 |
| **双槽位兼容** | OTA 设计 | slotA / slotB 均需编译通过并运行 |
| **编译工具链** | 当前 Keil 工程 | 三个 target 均启用 AC6（`uAC6=1`），应使用官方 GCC/ARMClang port |
| **不改动既有行为** | 用户要求 | OLED、菜单、按键、LED、USART 协议、CLI、OTA、看门狗表现保持一致 |

### 1.5 验收标准

1. `oled_cubemx` / `oled_cubemx_slota` / `oled_cubemx_slotb` 三个 Keil target 编译零错误。
2. slotA、slotB 烧录后 OLED 正常显示、4 键菜单导航正常、LED 控制正常。
3. USART1 协议：RK3506 帧收发正常，超时重传机制不失效。
4. USART2 调试串口：`help` / `info` 等 CLI 命令正常交互。
5. 看门狗 IWDG 不产生误复位。
6. OTA 升级流程完整可用，bootloader 不受影响。
7. `.map` 文件中出现 `xTaskCreate` / `vTaskStartScheduler` / `SVC_Handler` 等 FreeRTOS 符号，且不存在 `osKernelStart` / `MX_FREERTOS_Init`。

---

## 二、总体设计

### 2.1 架构演进

```text
【移植前 — 超级循环】
main()
  ├─ HAL_Init()
  ├─ SystemClock_Config()
  ├─ MX_*_Init()
  ├─ user_app_init()
  └─ while (1)
       └─ user_app_handle()

【移植后 — 单任务，原生 FreeRTOS】
main()
  ├─ HAL_Init()
  ├─ SystemClock_Config()
  ├─ SCB->VTOR = APP_VTOR_ADDR
  ├─ MX_GPIO_Init()
  ├─ MX_I2C2_Init()
  ├─ MX_USART1_UART_Init()
  ├─ MX_USART2_UART_Init()
  ├─ MX_NVIC_Init()
  ├─ user_app_init()
  ├─ freertos_app_init()      // 创建 AppTask
  └─ vTaskStartScheduler()    // 不返回

AppTask
  └─ for (;;)
       └─ user_app_handle()

USART1_IRQHandler ──► uart_drv 512B 环形缓冲 ──► AppTask 取走喂协议
USART2_IRQHandler ──► debug_console 环形缓冲 ──► AppTask 取走做 CLI
TIM6_DAC_IRQHandler ──► HAL_TIM_IRQHandler ──► HAL_IncTick()
SysTick_Handler ──► xPortSysTickHandler ──► FreeRTOS tick
```

### 2.2 FreeRTOS 源码布局

将官方 `FreeRTOS-Kernel` 复制到业务区，避免 CubeMX 重新生成时覆盖：

```text
stm32f407/
├── ThirdParty/
│   └── FreeRTOS-Kernel/
│       ├── include/                 # 官方 include 目录
│       ├── list.c
│       ├── tasks.c
│       ├── queue.c
│       ├── portable/
│       │   ├── GCC/ARM_CM4F/
│       │   │   ├── port.c
│       │   │   └── portmacro.h
│       │   └── MemMang/
│       │       └── heap_4.c
│       └── LICENSE.md
├── inc/
│   ├── FreeRTOSConfig.h
│   └── freertos_app.h
└── src/
    └── freertos_app.c
```

最小文件集为：

| 文件 | 作用 |
|------|------|
| `list.c` | 内核链表，必须加入 |
| `tasks.c` | 任务调度、TCB、空闲任务 |
| `queue.c` | 队列、信号量、互斥锁基础实现 |
| `portable/GCC/ARM_CM4F/port.c` | Cortex-M4F 移植层 |
| `portable/GCC/ARM_CM4F/portmacro.h` | 移植层头文件 |
| `portable/MemMang/heap_4.c` | 支持动态分配和释放的堆实现 |
| `include/*.h` | FreeRTOS API 头文件 |

`timers.c`、`event_groups.c`、`stream_buffer.c`、`croutine.c` 本期均不需要；后续使用对应 API 时再加入。

### 2.3 时基方案（关键设计）

Cortex-M4 只有一个 SysTick。FreeRTOS 与 HAL 都需要周期时基，因此必须分离：

```text
SysTick ──► FreeRTOS xPortSysTickHandler  ──► RTOS tick / 调度
TIM6    ──► HAL_TIM_IRQHandler            ──► HAL_IncTick() / HAL_Delay()
```

| 定时器 | 用途 | 频率 | 优先级 |
|--------|------|------|--------|
| SysTick | FreeRTOS 内核 tick | 1 kHz | 最低优先级 15 |
| TIM6 | HAL 1 ms 时基 | 1 kHz | 最低优先级 15 |

当前 [stm32f4xx_hal_conf.h](/E:/BaiduNetdiskDownload/code/oled_prj/oled_cubemx/Inc/stm32f4xx_hal_conf.h:151) 中 `TICK_INT_PRIORITY` 为 `15U`。本次保持 `15U`，不改为 0。TIM6 只递增 HAL tick，不需要高优先级抢占业务；高优先级 TIM6 反而会增加移植阶段的不确定因素。

注意：`configMAX_SYSCALL_INTERRUPT_PRIORITY = 5` 意味着 FreeRTOS 临界区会屏蔽优先级 ≥5 的中断，TIM6（15）也在其中，`uwTick` 在临界区期间会短暂停摆，`HAL_Delay` 精度略有下降。单任务、短临界区场景可忽略，但不应把 `HAL_Delay` 用于精确计时。

### 2.4 任务划分

| 任务 | 优先级 | 栈 | 职责 | 入口 |
|------|--------|-----|------|------|
| `app_task` | `tskIDLE_PRIORITY + 1` | 1024 words（4 KB） | 原 `user_app_handle()` 全量逻辑 | `freertos_app.c` |
| IDLE 任务 | 最低（内核） | 内核默认 | 空闲兜底 | 内核自动 |

`user_app_handle()` 内部已经串行完成协议轮询、CLI、按键、LED、菜单、显示、看门狗喂狗。第一期整体放入一个任务，天然规避多任务竞态。

栈初值按 1024 words（4 KB）预留。调试阶段必须周期打印或调试观察 `uxTaskGetStackHighWaterMark(NULL)`，记录高水位，不能只凭经验判断栈是否足够。

### 2.5 中断与优先级设计

STM32F407 使用 4 位抢占优先级，来源于 [oled_cubemx.ioc](/E:/BaiduNetdiskDownload/code/oled_prj/oled_cubemx/oled_cubemx.ioc) 中的 `NVIC.PriorityGroup=NVIC_PRIORITYGROUP_4`。

| 中断 | CMSIS 数值优先级 | 是否调用 FreeRTOS FromISR API | 说明 |
|------|------------------|--------------------------------|------|
| TIM6 | 15 | 否 | HAL 1 ms tick，只调 `HAL_IncTick()` |
| USART1 | 14 | 否 | 字节写入 `uart_drv` 512B 环形缓冲 |
| USART2 | 14 | 否 | 字节写入 `debug_console` 环形缓冲 |
| PendSV | 15 | 内核 | 上下文切换 |
| SysTick | 15 | 内核 | FreeRTOS tick |

FreeRTOS 的优先级换算：

```text
configPRIO_BITS                              = 4
configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY  = 5
configMAX_SYSCALL_INTERRUPT_PRIORITY          = 5 << 4 = 80
```

只有 CMSIS 数值优先级大于等于 5 的中断，才允许调用 `...FromISR()` API。USART1/USART2 为 14，符合条件；TIM6 为 15，也符合条件，但本期不调用 FreeRTOS API，进一步降低风险。

---

## 三、CubeMX 硬件侧变更

### 3.1 明确不做什么

- **不要**在 Middleware 中启用 FreeRTOS。
- **不要**选择 `CMSIS_V1` 或 `CMSIS_V2` 接口。
- **不要**让 CubeMX 生成 `freertos.c`、`FreeRTOSConfig.h`、`cmsis_os2.c`。
- `Mcu.ThirdPartyNb` 应保持为 `0`。

### 3.2 需要修改的硬件配置

| # | 位置 | 参数 | 当前值 | 目标值 |
|---|------|------|--------|--------|
| 1 | System Core → SYS | Timebase Source | SysTick | **TIM6** |
| 2 | `stm32f4xx_hal_conf.h` | `HAL_TIM_MODULE_ENABLED` | 已注释 | **取消注释** |
| 3 | `stm32f4xx_hal_conf.h` | `TICK_INT_PRIORITY` | 15U | **保持 15U** |
| 4 | `stm32f4xx_hal_conf.h` | `USE_RTOS` | 0U | **保持 0U，禁止改为 1** |

说明：

- 当前 [stm32f4xx_hal_conf.h](/E:/BaiduNetdiskDownload/code/oled_prj/oled_cubemx/Inc/stm32f4xx_hal_conf.h:66) 中 `HAL_TIM_MODULE_ENABLED` 是注释状态，使用 TIM6 HAL 时基前必须启用。
- [stm32f4xx_hal_def.h](/E:/BaiduNetdiskDownload/code/oled_prj/oled_cubemx/Drivers/STM32F4xx_HAL_Driver/Inc/stm32f4xx_hal_def.h:89) 明确要求当前 HAL 版本中 `USE_RTOS` 必须为 0，否则编译报错。
- CubeMX 将 SYS Timebase 改为 TIM6 后，应生成 `oled_cubemx/Src/stm32f4xx_hal_timebase_tim.c`。该文件实现 `HAL_InitTick`、`HAL_SuspendTick`、`HAL_ResumeTick`、`HAL_TIM_PeriodElapsedCallback` 和 `TIM6_DAC_IRQHandler`。

### 3.3 生成后检查

1. 确认 `oled_cubemx/Src/stm32f4xx_hal_timebase_tim.c` 已生成。
2. 确认 [stm32f4xx_it.c](/E:/BaiduNetdiskDownload/code/oled_prj/oled_cubemx/Src/stm32f4xx_it.c:184) 中不再在 `SysTick_Handler` 里调用 `HAL_IncTick()`。
3. 确认 `oled_cubemx/Src/stm32f4xx_it.c` 中没有重复定义 `TIM6_DAC_IRQHandler`。
4. 确认 Keil HAL 分组中已加入 `stm32f4xx_hal_tim.c`；如果 CubeMX 未自动加入，则手动加入。

---

## 四、获取并放置 FreeRTOS 源码

### 4.1 推荐版本

首选官方 [FreeRTOS-Kernel V11.3.0](https://github.com/FreeRTOS/FreeRTOS-Kernel/releases/tag/V11.3.0)。

依据：

- 当前日期为 2026-08-13，查询到官方最新 tag 为 `V11.3.0`。
- 官方 `portable/ARMClang/Use-the-GCC-ports.txt` 明确说明：ArmClang 编译器应编译 `portable/GCC` 目录中的 port 文件。
- 当前 Keil 工程三个 target 均使用 AC6，即 ArmClang，因此选择 `portable/GCC/ARM_CM4F`，不选择 RVDS/Keil port。

### 4.2 离线备选

本机已有 STM32Cube FW_F4 V1.28.3：

```text
C:\Users\joey\STM32Cube\Repository\STM32Cube_FW_F4_V1.28.3\Middlewares\Third_Party\FreeRTOS\Source
```

该目录提供 FreeRTOS V10.3.1，可作为离线参考或备用来源。如果使用它，路径结构是 `Source/portable/GCC/ARM_CM4F`，本设计中的源文件路径需要按此调整。

> 注意：V10.3.1 与 V11.3.0 的配置宏有差异，不能直接套用第五节基线。例如 V10.3.1 的 Cortex-M4F 需要额外定义 `configUSE_TASK_FPU_SUPPORT = 1`（V11 已移除该宏、FPU 恒开），`configUSE_16_BIT_TICKS` 在 V10.3.1 仍有效而 V11 已由 `configTICK_TYPE_WIDTH_IN_BITS` 取代。若采用 V10.3.1，务必以其自带 demo 的 `FreeRTOSConfig.h` 为基底。

### 4.3 复制方式

只复制本设计需要的文件，不要复制 CMSIS-RTOS 旧适配层。复制完成后，目录结构应与 2.2 节一致。

---

## 五、FreeRTOSConfig.h

### 5.1 文件位置

```text
stm32f407/inc/FreeRTOSConfig.h
```

该路径已经位于 Keil include path 和 Makefile `INC_APP` 中，不需要额外调整。

### 5.2 关键宏基线

| 宏 | 值 | 说明 |
|----|-----|------|
| `configCPU_CLOCK_HZ` | `168000000UL` | 与 `.ioc` 中 `RCC.SYSCLKFreq_VALUE=168000000` 一致 |
| `configTICK_RATE_HZ` | `1000` | 1 kHz RTOS tick |
| `configUSE_PREEMPTION` | `1` | 抢占式调度 |
| `configUSE_TIME_SLICING` | `1` | 同优先级时间片轮转 |
| `configMAX_PRIORITIES` | `8` | 单任务学习阶段足够；拆多任务再按需增大 |
| `configMINIMAL_STACK_SIZE` | `128` | 内核最小栈基准 |
| `configTOTAL_HEAP_SIZE` | `12288` | heap_4，12 KB；需实测栈高水位和堆余量 |
| `configSUPPORT_DYNAMIC_ALLOCATION` | `1` | heap_4 / `xTaskCreate` 依赖动态分配（V11 默认即为 1，保持开启） |
| `configUSE_MUTEXES` | `1` | 预留互斥锁能力 |
| `configCHECK_FOR_STACK_OVERFLOW` | `2` | 调试阶段开启 |
| `configUSE_TIMERS` | `0` | 本期不用软件定时器 |
| `configTICK_TYPE_WIDTH_IN_BITS` | `TICK_TYPE_WIDTH_32_BITS` | 使用 32 位 tick（V11 已用此宏取代 `configUSE_16_BIT_TICKS`） |
| `configIDLE_SHOULD_YIELD` | `1` | 常规配置 |
| `configPRIO_BITS` | `4` | STM32F4 使用 4 位抢占优先级 |
| `configLIBRARY_LOWEST_INTERRUPT_PRIORITY` | `15` | CMSIS 最低优先级 |
| `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` | `5` | FromISR 阈值 |
| `configKERNEL_INTERRUPT_PRIORITY` | `15 << (8 - 4)` | 内核使用最低优先级 |
| `configMAX_SYSCALL_INTERRUPT_PRIORITY` | `5 << (8 - 4)` | 即 80 |

> 由于 FreeRTOS V11.3.0 的配置模板和 V10.x 有差异，应以 V11.3.0 官方模板或官方 Cortex-M4 demo 的 `FreeRTOSConfig.h` 为基底，再覆盖上表数值。不要直接照抄旧版本配置的每一项。

### 5.3 中断向量映射

在 `FreeRTOSConfig.h` 中使用 Cortex-M Direct Routing，把 FreeRTOS port 函数映射到启动文件中的向量名：

```c
#define vPortSVCHandler      SVC_Handler
#define xPortPendSVHandler   PendSV_Handler
#define xPortSysTickHandler  SysTick_Handler
```

不要用普通 C 包装函数去调用 `vPortSVCHandler` 或 `xPortPendSVHandler`。这两个函数是 naked 汇编实现，普通包装会破坏异常进入/退出上下文。

### 5.4 断言与 Hook 函数

`configASSERT` 与栈溢出 / 分配失败两个 Hook 必须由应用显式提供，否则相关检查会静默失效或在链接期报错：

```c
/* FreeRTOSConfig.h */
#define configASSERT(x) do { if ((x) == 0) { taskDISABLE_INTERRUPTS(); for (;;) {} } } while (0)
```

- `configASSERT` 未定义时 FreeRTOS 退化为空宏，[freertos_app.c](#62-freertos_appc) 里的 `configASSERT(ret == pdPASS)` 将不生效，必须在 `FreeRTOSConfig.h` 中显式定义。
- `vApplicationStackOverflowHook`：`configCHECK_FOR_STACK_OVERFLOW = 2` 需要应用提供，否则链接报错。
- `vApplicationMallocFailedHook`：仅当 `configUSE_MALLOC_FAILED_HOOK = 1` 时需要，见 §10.3。

实现见 §6.4。

---

## 六、应用集成层

### 6.1 freertos_app.h

建议文件：

```text
stm32f407/inc/freertos_app.h
```

内容：

```c
/**
 * @file    freertos_app.h
 * @brief   FreeRTOS 应用任务集成接口
 */

#ifndef FREERTOS_APP_H
#define FREERTOS_APP_H

/**
 * @brief  创建 FreeRTOS 应用任务
 * @param  无
 * @return 无
 */
void freertos_app_init(void);

#endif
```

### 6.2 freertos_app.c

建议文件：

```text
stm32f407/src/freertos_app.c
```

内容：

```c
/**
 * @file    freertos_app.c
 * @brief   FreeRTOS 应用任务集成
 */

#include "FreeRTOS.h"
#include "task.h"
#include "freertos_app.h"
#include "user_app.h"

#define APP_TASK_STACK_WORDS      1024U
#define APP_TASK_PRIORITY         (tskIDLE_PRIORITY + 1U)

static TaskHandle_t g_app_task_handle = NULL;

/**
 * @brief  APP 主任务，承载原有 user_app_handle 逻辑
 * @param  argument 任务参数，本任务不使用
 * @return 无
 */
static void app_task(void *argument)
{
    (void)argument;

    for (;;)
    {
        user_app_handle();
    }
}

/**
 * @brief  创建 APP 主任务
 * @param  无
 * @return 无
 */
void freertos_app_init(void)
{
    BaseType_t ret = pdFAIL;

    ret = xTaskCreate(app_task,
                      "app_task",
                      APP_TASK_STACK_WORDS,
                      NULL,
                      APP_TASK_PRIORITY,
                      &g_app_task_handle);

    configASSERT(ret == pdPASS);
}
```

该文件位于 `stm32f407/src/`，必须遵守 `AGENTS.md` 中 Allman 大括号、4 空格缩进和中文 UTF-8 规范。

### 6.3 原 USART 路径保持现状

第一期不把 USART1/USART2 改为 RTOS 队列。原因：

- 现有 [uart_drv.c](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/src/uart_drv.c:143) 的单字节中断重装和 512B 环形缓冲已经包含错误恢复逻辑。
- [user_app.c](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/src/user_app.c:90) 的 `bytes_read_this_loop` 依赖实际收到的协议字节数，保留原路径可避免破坏 5 秒串口断开判断。
- 阻塞型单任务中，512B 静态环形缓冲比 16B RTOS 队列更适合当前模型。

### 6.4 Hook 兜底函数

新建 `stm32f407/src/freertos_hooks.c`（或并入 `freertos_app.c`），提供 FreeRTOS 要求的两个 Hook：

```c
/**
 * @file    freertos_hooks.c
 * @brief   FreeRTOS 应用 Hook 兜底函数
 */

#include "FreeRTOS.h"
#include "task.h"

/**
 * @brief  栈溢出 Hook，configCHECK_FOR_STACK_OVERFLOW=2 时由内核调用
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}

/**
 * @brief  堆分配失败 Hook，configUSE_MALLOC_FAILED_HOOK=1 时由内核调用
 */
void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}
```

若启用该文件，需在三个 target 的应用分组中同步加入 `..\..\stm32f407\src\freertos_hooks.c`（同 §8.2）。

---

## 七、main.c 和中断处理

### 7.1 main.c

保留 CubeMX 生成的外设初始化，只修改用户代码区：

```c
/* USER CODE BEGIN Includes */
#include "user_app.h"
#include "freertos_app.h"
/* USER CODE END Includes */

/* USER CODE BEGIN 2 */
user_app_init();
freertos_app_init();
vTaskStartScheduler();
Error_Handler();
/* USER CODE END 2 */

while (1)
{
    /* vTaskStartScheduler() 正常不返回；Error_Handler() 亦不返回，此循环仅作防御性死循环 */
}
```

注意：

- `user_app_init()` 仍然放在 `vTaskStartScheduler()` 之前，因为其内部包含启动画面延时等 HAL 延时依赖。
- `user_app_handle()` 不再放在 `main()` 的 `while (1)` 中，改由 `app_task` 调用。
- 如果 `vTaskStartScheduler()` 因堆不足等原因返回，应进入 `Error_Handler()`，而不是继续运行裸机逻辑。

### 7.2 stm32f4xx_it.c

由于 `FreeRTOSConfig.h` 已经做了中断向量映射，[stm32f4xx_it.c](/E:/BaiduNetdiskDownload/code/oled_prj/oled_cubemx/Src/stm32f4xx_it.c:145) 中应删除或清空以下重复定义：

```c
void SVC_Handler(void)
void PendSV_Handler(void)
void SysTick_Handler(void)
```

FreeRTOS 的 `port.c` 会通过宏映射提供 `SVC_Handler`、`PendSV_Handler`、`SysTick_Handler` 符号。

保留：

- `NMI_Handler`
- `HardFault_Handler`
- `MemManage_Handler`
- `BusFault_Handler`
- `UsageFault_Handler`
- `DebugMon_Handler`
- `USART1_IRQHandler`
- `USART2_IRQHandler`

TIM6 的 `TIM6_DAC_IRQHandler` 由 CubeMX 生成的 `stm32f4xx_hal_timebase_tim.c` 提供，不要在 `stm32f4xx_it.c` 中再写一份，避免重复定义。

---

## 八、Keil 工程配置

当前工程文件：[oled_cubemx.uvprojx](/E:/BaiduNetdiskDownload/code/oled_prj/oled_cubemx/MDK-ARM/oled_cubemx.uvprojx)

三个 target：

- `oled_cubemx`：`USE_HAL_DRIVER,STM32F407xx`
- `oled_cubemx_slota`：`USE_HAL_DRIVER,STM32F407xx,APP_SLOT_A`
- `oled_cubemx_slotb`：`USE_HAL_DRIVER,STM32F407xx,APP_SLOT_B`

### 8.1 Include Paths

在三个 target 的 `Options for Target → C/C++ → Include Paths` 中，除现有路径外增加：

```text
../../stm32f407/inc
../../stm32f407/ThirdParty/FreeRTOS-Kernel/include
../../stm32f407/ThirdParty/FreeRTOS-Kernel/portable/GCC/ARM_CM4F
```

其中 `../../stm32f407/inc` 当前已经存在，只需补齐后两项。

### 8.2 源文件

新增 FreeRTOS 分组，加入：

```text
..\..\stm32f407\ThirdParty\FreeRTOS-Kernel\list.c
..\..\stm32f407\ThirdParty\FreeRTOS-Kernel\tasks.c
..\..\stm32f407\ThirdParty\FreeRTOS-Kernel\queue.c
..\..\stm32f407\ThirdParty\FreeRTOS-Kernel\portable\GCC\ARM_CM4F\port.c
..\..\stm32f407\ThirdParty\FreeRTOS-Kernel\portable\MemMang\heap_4.c
```

在应用分组中加入：

```text
..\..\stm32f407\src\freertos_app.c
```

如果 CubeMX 没有自动加入 TIM HAL 源文件，还需在 HAL 分组中加入：

```text
..\Drivers\STM32F4xx_HAL_Driver\Src\stm32f4xx_hal_tim.c
```

### 8.3 三个 target 同步

CubeMX 通常只维护第一个 target。`oled_cubemx_slota` 和 `oled_cubemx_slotb` 必须手动补齐相同的 include path 和 FreeRTOS 源文件分组。

同步时不要改动两个槽位 target 的：

- `APP_SLOT_A` / `APP_SLOT_B` define
- scatter 文件
- 启动文件

---

## 九、Makefile 可选说明

本期验收以 Keil AC6 为准。若后续要启用 [stm32f407/Makefile](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/Makefile:1)，需要额外处理：

1. 在 `INC_APP` 中增加：

```make
-IThirdParty/FreeRTOS-Kernel/include
-IThirdParty/FreeRTOS-Kernel/portable/GCC/ARM_CM4F
```

2. 在 APP 编译目标中增加 FreeRTOS 源文件：

```make
FREERTOS_SRC = ThirdParty/FreeRTOS-Kernel/list.c \
               ThirdParty/FreeRTOS-Kernel/tasks.c \
               ThirdParty/FreeRTOS-Kernel/queue.c \
               ThirdParty/FreeRTOS-Kernel/portable/GCC/ARM_CM4F/port.c \
               ThirdParty/FreeRTOS-Kernel/portable/MemMang/heap_4.c \
               src/freertos_app.c
```

3. 当前 Makefile 的 `STARTUP` 指向 `oled_cubemx/MDK-ARM/startup_stm32f407xx.s`，这是 MDK 汇编文件。若要走 arm-none-eabi-gcc 构建，还需要替换为 GCC 兼容 startup 文件。该问题与本移植主题相关，但不在本期 Keil 验收范围内。

---

## 十、验证方案

### 10.1 编译验证

1. 打开 `oled_cubemx/MDK-ARM/oled_cubemx.uvprojx`。
2. 依次编译三个 target。
3. 检查：
   - `HAL_TIM_MODULE_ENABLED` 已启用。
   - `USE_RTOS` 仍为 `0U`。
   - `TICK_INT_PRIORITY` 仍为 `15U`。
   - `stm32f4xx_hal_timebase_tim.c` 已参与编译。
   - `portable/GCC/ARM_CM4F/port.c` 被 AC6 成功编译。
4. 打开 `.map` 文件，确认出现 `xTaskCreate`、`vTaskStartScheduler`、`SVC_Handler`、`PendSV_Handler`、`SysTick_Handler`、`TIM6_DAC_IRQHandler`。

### 10.2 功能回归

| 验证项 | 方法 | 通过判据 |
|--------|------|----------|
| OLED 显示 | 上电观察 | 启动画面后进入正常显示 |
| 菜单 | 4 键导航 | 主菜单逐级可进、可退、可编辑 |
| 按键/LED | 短按/长按 | KEY2 切 LED，KEY4 长按进菜单 |
| 协议 | PC 上位机连接 | 帧收发正常，超时重传不误触发 |
| CLI | USART2 发 `help` / `info` | 命令回显正确 |
| 看门狗 | 启用 `IWDG_ENABLE` 后长时间运行 | 无 IWDG 误复位 |
| OTA | `ota_tool.py` 升级 | 双槽交替升级成功 |

### 10.3 调试辅助

- 开启 `configCHECK_FOR_STACK_OVERFLOW = 2`。
- 周期观察 `uxTaskGetStackHighWaterMark(NULL)`，确认 4 KB 栈仍有安全余量。
- 临时开启 `configUSE_MALLOC_FAILED_HOOK`，观察 12 KB heap 是否足够。
- 用 USART2 日志确认任务循环仍在执行。

---

## 十一、风险与对策

| 风险 | 影响 | 对策 |
|------|------|------|
| AC6 与 port 层汇编不兼容 | 编译失败 | 优先使用官方 V11.3.0 的 `portable/GCC/ARM_CM4F`；官方 ARMClang 目录已说明使用 GCC port |
| CubeMX 重新生成覆盖 `stm32f4xx_it.c` / `main.c` | 重复定义或丢失调度器启动 | 重新生成后重新核对 7.1、7.2 节的手工修改 |
| `SVC_Handler` / `PendSV_Handler` / `SysTick_Handler` 重复定义 | 链接错误 | 只保留 port.c 通过宏映射后的符号，删除 `stm32f4xx_it.c` 中的同名函数 |
| TIM6 时基未生效 | HAL_Delay / 启动画面异常 | 确认 `stm32f4xx_hal_timebase_tim.c` 已生成，`HAL_TIM_MODULE_ENABLED` 已启用 |
| TIM6 与未来外设冲突 | 移植后续功能受影响 | 当前工程未使用 TIM6；若未来冲突，改用 TIM7 |
| USART 环形缓冲溢出 | 极端突发丢字节 | 本期保持 512B 静态环形缓冲，后续拆任务再评估 RTOS 队列 |
| 栈溢出 | 任务崩溃 | 1024 words 起步，开启 `CHECK_FOR_STACK_OVERFLOW=2` 并记录高水位 |
| IWDG 饿死 | 误复位 | 单任务循环内喂狗，长时间验证 |

---

## 十二、手动移植操作步骤

### 12.1 准备

1. 备份 `oled_cubemx/`、`stm32f407/` 和本设计文档。
2. 阅读 [main.c](/E:/BaiduNetdiskDownload/code/oled_prj/oled_cubemx/Src/main.c:67)、[stm32f4xx_it.c](/E:/BaiduNetdiskDownload/code/oled_prj/oled_cubemx/Src/stm32f4xx_it.c:145)、[user_app.c](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/src/user_app.c:90)。

### 12.2 CubeMX 只生成硬件配置

1. 打开 `oled_cubemx/oled_cubemx.ioc`。
2. 不启用 FreeRTOS Middleware。
3. 将 SYS Timebase Source 从 SysTick 改为 TIM6。
4. 点击 Generate Code。
5. 按 3.3 节检查生成产物。

### 12.3 下载并复制 FreeRTOS

1. 下载官方 FreeRTOS-Kernel V11.3.0。
2. 在 `stm32f407/ThirdParty/` 下创建 `FreeRTOS-Kernel/`。
3. 按 2.2 节复制最小文件集。

### 12.4 添加配置和应用层

1. 创建 `stm32f407/inc/FreeRTOSConfig.h`。
2. 按第五节配置关键宏，并加入中断向量映射。
3. 创建 `stm32f407/inc/freertos_app.h`。
4. 创建 `stm32f407/src/freertos_app.c`。
5. 按第七节修改 `main.c` 和 `stm32f4xx_it.c`。

### 12.5 配置 Keil 三个 target

1. 按第八节为三个 target 添加 include path 和源文件。
2. 先编译 `oled_cubemx`。
3. 修复编译错误，确认 `port.c` 能通过 AC6。
4. 再编译 `oled_cubemx_slota` 和 `oled_cubemx_slotb`。

### 12.6 烧录与回归

1. 优先烧录 slotA 或 slotB，不要只测默认 target。
2. 按 10.2 逐项回归。
3. 最后启用 IWDG 并长时间运行。
4. 基础功能通过后再做 OTA。

---

## 十三、学习重点

本次手动移植应重点理解以下内容：

1. FreeRTOS 内核最小文件集：`list.c`、`tasks.c`、`queue.c`、port 层和 heap 层各做什么。
2. Cortex-M4 的 SVC / PendSV / SysTick 三种异常在 RTOS 中的作用。
3. `FreeRTOSConfig.h` 中 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 与 CMSIS NVIC 数值优先级的换算。
4. 为什么 SysTick 让给 FreeRTOS 后，HAL 需要改用 TIM6。
5. `xTaskCreate` 的栈单位是 word 而不是 byte，栈高水位是必须实测的数据。
6. CubeMX 只负责硬件初始化，FreeRTOS 是独立中间件；手动集成后，CubeMX 重新生成时必须核对用户代码边界。
