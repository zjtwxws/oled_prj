# STM32CubeMX 创建 STM32F407 HAL 工程操作文档

**适用版本**: STM32CubeMX 6.18.0+ | **目标芯片**: STM32F407VGTx | **固件包**: STM32Cube FW_F4

本文档参考 [STM32CubeMX 在线用户手册](https://dev.st.stm32cube-docs/stm32cubemx/6.18.0/en/docs/markup/CubeMX_UserManual/CubeMX_UserManual_toc.html)（UM1718），结合本工程 `stm32f407/` 目录下的实际代码，给出从零创建 HAL 工程、配置外设、生成代码、到集成应用逻辑的完整操作流程。

## 1. 环境准备

| 工具 | 用途 | 获取方式 |
|------|------|----------|
| **STM32CubeMX** 6.18.0+ | 图形化外设配置 & 代码生成 | [官网下载](https://www.st.com/en/development-tools/stm32cubemx.html) |
| **STM32Cube FW_F4** | STM32F4 HAL 库 | CubeMX → Help → Manage embedded software packages |
| **arm-none-eabi-gcc** | 交叉编译器 | [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) |
| **st-flash** (可选) | 命令行烧录 | `stlink-tools` |

CubeMX 初次配置: 打开 CubeMX → Help → Updater Settings → 设置固件包仓库路径。然后在 Help → Manage embedded software packages 中安装 `STM32Cube MCU Package for STM32F4 Series`（最新版）。

## 2. 新建工程

参考手册章节: CubeMX_UserManual §3.1 Creating a new STM32 project

**步骤**:

1. 启动 STM32CubeMX，点击 **File → New Project**（或首页 "ACCESS TO MCU SELECTOR"）
2. 在 **MCU/MPU Selector** 选项卡中:
   - **Commercial Part Number** 输入 `STM32F407VG`
   - 列表中选择 `STM32F407VGTx` (LQFP100, 1MB Flash, 192KB RAM)
   - 点击右上角 **Start Project**

3. 进入 **Pinout & Configuration** 主界面，左侧有四个选项卡: Pinout & Configuration / Clock Configuration / Project Manager / Tools

> 如果实际开发板使用 STM32F407ZETx (144 pin) 或 STM32F407VETx (512KB Flash)，选择对应型号。代码兼容，仅需调整链接脚本中的 Flash/RAM 大小。

## 3. 时钟配置 (Clock Configuration)

参考手册章节: CubeMX_UserManual §3.4.2 Clock configuration

STM32F407 最高主频 168MHz。本工程使用外部高速晶振 (HSE) + PLL:

切换到 **Clock Configuration** 选项卡，按以下参数配置（对应 `main.c` 中 `SystemClock_Config()` 的数值）:

| 参数 | 值 | 含义 |
|------|-----|------|
| **HSE** | `Crystal/Ceramic Resonator` (8 MHz) | 外部晶振输入 |
| **PLL Source Mux** | HSE | PLL 时钟源选 HSE |
| **PLLM** | `/8` | HSE 8MHz → 1MHz 进 PLL VCO |
| **PLLN** | `x336` | VCO = 336MHz |
| **PLLP** | `/2` | SYSCLK = 336/2 = **168 MHz** |
| **PLLQ** | `/7` | 48MHz 供 USB OTG FS / SDIO / RNG |
| **SYSCLK Source** | PLLCLK | 系统时钟选 PLL 输出 |
| **AHB Prescaler** | `/1` | HCLK = 168 MHz |
| **APB1 Prescaler** | `/4` | PCLK1 = 42 MHz (max) |
| **APB2 Prescaler** | `/2` | PCLK2 = 84 MHz (max) |

确保 **Flash Latency** = `5 WS`（168MHz 对应 5 等待周期）。按 Enter 确认，CubeMX 自动解析并提示异常。

## 4. 引脚与外设配置 (Pinout & Configuration)

参考手册章节: CubeMX_UserManual §3.4.1 Pinout view

回到 **Pinout & Configuration** 选项卡, 在左侧 **Categories** 列表中逐一配置:

### 4.1 系统核心 (SYS)

| 配置项 | 值 | 说明 |
|--------|-----|------|
| **Debug** | Serial Wire | SWD 调试接口 (SWDIO + SWCLK) |
| **Timebase Source** | SysTick | HAL 滴答时钟源 |

### 4.2 独立看门狗 (IWDG)

Categories → **IWDG → 勾选 Activated**

| 参数 | 值 | 含义 |
|------|-----|------|
| Prescaler | **256** | LSI 32kHz / 256 = 125Hz (8ms/tick) |
| Window | **4095** (最大值) | 不使能窗口 |
| Reload | **125** | 超时 = 125 x 8ms = **1000ms** |

对应代码 `iwdg_drv.c`，通过 `IWDG_ENABLE` 编译宏控制是否启用。

### 4.3 串口1 — USART1 (与 PC上位机通信)

Categories → **USART1 → Mode: Asynchronous**

Pinout 自动分配: PA9 → USART1_TX, PA10 → USART1_RX

**NVIC Settings**: `USART1 global interrupt` → **启用**

**DMA Settings** (可选): Add → `USART1_TX`, Direction: Memory To Peripheral, Priority: Low

**Parameter Settings**:

| 参数 | 值 |
|------|-----|
| Baud Rate | **115200** |
| Word Length | 8 Bits |
| Parity | None |
| Stop Bits | 1 |

### 4.4 串口2 — USART2 (调试串口, TX+RX)

Categories → **USART2 → Mode: Asynchronous**

Pinout 自动分配: PA2 → USART2_TX, PA3 → USART2_RX

**Parameter Settings**: 同 USART1, Baud Rate = 115200

> 调试串口使用全双工 (PA2 TX, PA3 RX)。按键已全部移至 PE 端口，PA3 无冲突。

### 4.5 I2C2 (驱动 OLED)

Categories → **I2C2 → Mode: I2C**

| 参数 | 值 |
|------|-----|
| I2C Speed Mode | **Fast Mode** |
| I2C Speed Frequency | **400 KHz** |

Pinout 自动分配: PB10 → I2C2_SCL, PB11 → I2C2_SDA。OLED 地址 `0x3C` (7-bit)，HAL 库内部会 `<< 1` 处理。

### 4.6 GPIO 输入 (按键)

在 Pinout 视图中直接点击引脚配置（外部上拉 + 按下接地 = active low）:

| 引脚 | 标签 | 模式 |
|------|------|------|
| **PE1** | KEY1 | GPIO_Input, Pull-up |
| **PE2** | KEY2 | GPIO_Input, Pull-up |
| **PE3** | KEY3 | GPIO_Input, Pull-up |
| **PE4** | KEY4 | GPIO_Input, Pull-up |

> 4 个按键全部使用 PE 端口，与串口引脚 (PA2/PA3) 不再冲突。UART2 可实现全双工调试。按键启用数量由 `key_drv.h` 中 `KEY_COUNT` 宏控制，默认 4。

### 4.7 GPIO 输出 (LED)

| 引脚 | 标签 | 模式 |
|------|------|------|
| **PF9** | LED | GPIO_Output, Push-Pull, No Pull-up/Pull-down |

> 本工程 `led_mgr.c` 中使用 PF9。初始化由 CubeMX 生成的 `MX_GPIO_Init()` 完成。

**完整 Pinout 一览** (本项目当前配置):

| 外设 | 引脚 | 功能 |
|------|------|------|
| USART1_TX | PA9 | UART 发送 → PC上位机 |
| USART1_RX | PA10 | UART 接收 ← PC上位机 |
| USART2_TX | PA2 | 调试 TX (全双工) |
| USART2_RX | PA3 | 调试 RX (全双工) |
| I2C2_SCL | PB10 | OLED SCL |
| I2C2_SDA | PB11 | OLED SDA |
| KEY1 | PE1 | GPIO Input, Pull-up |
| KEY2 | PE2 | GPIO Input, Pull-up |
| KEY3 | PE3 | GPIO Input, Pull-up |
| KEY4 | PE4 | GPIO Input, Pull-up |
| LED | PF9 | GPIO Output |

## 5. 工程设置 (Project Manager)

参考手册章节: CubeMX_UserManual §3.5 Project Manager

切换到 **Project Manager** 选项卡:

**Project 页面**:

| 字段 | 值 | 说明 |
|------|-----|------|
| Project Name | `oled_f407` | 工程名称 |
| Project Location | 选择工作目录 | 如 `E:\BaiduNetdiskDownload\code\oled_prj\stm32f407` |
| Application Structure | **Basic** (裸机) | |
| Toolchain / IDE | **Makefile** | 用于 arm-none-eabi-gcc 命令行编译 |

> 如果使用 STM32CubeIDE，此处选 STM32CubeIDE。如果使用 Keil MDK，选 MDK-ARM。本文档以 Makefile 为例。

**Code Generator 页面**:

| 选项 | 推荐值 | 说明 |
|------|--------|------|
| Copy only the necessary library files | 勾选 | 只复制用到的 HAL 驱动文件 |
| Generate peripheral initialization as a pair of .c/.h files | 勾选 | 每个外设单独生成 init 文件 |
| Set all free pins as analog | 勾选 | 未用引脚设为模拟模式 (降低功耗) |
| Enable Full Assert | 按需 | 调试阶段建议开启 |

> **重要**: 勾选 **"Keep User Code when re-generating"**，重新生成代码时不覆盖 `/* USER CODE BEGIN */` 和 `/* USER CODE END */` 之间的自定义代码。

**Advanced Settings 页面**: 确认各外设使用 HAL 驱动（非 LL）。

## 6. 生成代码

参考手册章节: CubeMX_UserManual §3.6 Code generation

点击工具栏 **GENERATE CODE** 按钮（齿轮图标），或 `Alt+G`。生成完成后弹出提示。

生成的目录结构如下:

```
stm32f407/
├── oled_f407.ioc                    ← CubeMX 工程文件
├── Core/
│   ├── Inc/
│   │   ├── main.h
│   │   ├── stm32f4xx_hal_conf.h    ← HAL 模块裁剪
│   │   ├── stm32f4xx_it.h
│   │   ├── gpio.h
│   │   ├── usart.h
│   │   └── i2c.h
│   ├── Src/
│   │   ├── main.c                   ← 主程序入口
│   │   ├── stm32f4xx_it.c          ← 中断服务函数
│   │   ├── stm32f4xx_hal_msp.c     ← HAL 底层初始化
│   │   ├── system_stm32f4xx.c      ← CMSIS 系统初始化
│   │   ├── gpio.c
│   │   ├── usart.c                 ← MX_USARTx_UART_Init()
│   │   └── i2c.c                   ← MX_I2C2_Init()
│   └── Startup/
│       └── startup_stm32f407xx.s   ← 启动文件 (向量表)
├── Drivers/
│   ├── STM32F4xx_HAL_Driver/
│   └── CMSIS/
└── STM32F407VGTx_FLASH.ld           ← 链接脚本
```

## 7. 生成代码解读

### 7.1 `main.c` — 生成后的框架

```c
int main(void)
{
    HAL_Init();                    // HAL 库初始化 (SysTick 配置)
    SystemClock_Config();          // 时钟树配置 (HSE + PLL → 168MHz)
    MX_GPIO_Init();                // GPIO 初始化
    MX_USART1_UART_Init();         // USART1 初始化
    MX_USART2_UART_Init();         // USART2 初始化
    MX_I2C2_Init();                // I2C2 初始化
    MX_IWDG_Init();                // IWDG 初始化

    // ★ 此处 (USER CODE BEGIN 2) 添加应用初始化代码

    while (1)
    {
        // ★ 此处 (USER CODE BEGIN 3) 添加主循环代码
    }
}
```

生成后的 `main.c` 是应用逻辑的**起点**。本工程在 `MX_*` 初始化之后插入应用层初始化，在 `while(1)` 中加入主循环。

### 7.2 `stm32f4xx_it.c` — 中断服务

```c
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);    // HAL 库内部处理中断标志
}
```

本工程 `uart_drv.c` 依赖 HAL 中断回调 `HAL_UART_RxCpltCallback()`，在此回调中将接收到的字节写入环形缓冲区，实现中断驱动接收。

### 7.3 `usart.c` — USART 初始化函数

```c
static void MX_USART1_UART_Init(void)
{
    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = 115200;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);    // → 内部调用 HAL_UART_MspInit
}
```

对应的 `HAL_UART_MspInit` 在 `stm32f4xx_hal_msp.c` 中生成，负责使能 USART1 时钟、配置 PA9/PA10 复用、使能 NVIC 中断及 DMA。

### 7.4 `i2c.c` — I2C 初始化函数

```c
static void MX_I2C2_Init(void)
{
    hi2c2.Instance             = I2C2;
    hi2c2.Init.ClockSpeed      = 400000;
    hi2c2.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c2.Init.OwnAddress1     = 0;
    hi2c2.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    HAL_I2C_Init(&hi2c2);
}
```

本工程 `i2c_drv.c` 封装了 `HAL_I2C_Mem_Write`，在 OLED 初始化序列 (`ssd1306.c`) 中通过命令/数据寄存器地址区分命令(0x00)和数据(0x40)发送。

## 8. 集成应用代码

核心原则: **应用代码通过 `extern` 声明引用 HAL 句柄，不修改 CubeMX 自动生成文件**。所有自定义逻辑写在独立的模块 (`src/` / `inc/`) 中，通过 Makefile 编译链接。

### 8.1 在 `main()` 中接入应用层

生成的 `main.c` 已包含 `MX_*_Init()` 调用。在 `while(1)` 之前插入应用初始化（`/* USER CODE BEGIN 2 */` 区域内）:

```c
/* USER CODE BEGIN 2 */
// --- 应用层初始化 ---
i2c_drv_init(&hi2c2);          // 传入 HAL 句柄给驱动封装层
uart_drv_init(&huart1);        // 启动 USART1 中断接收
debug_console_init(&huart2);   // 调试串口初始化

ssd1306_init();                // OLED 上电初始化 (100ms 延时 + 命令序列)
sys_config_init();             // 从 Flash 加载配置 (上电默认文字)

led_mgr_init();                // LED 管理器
key_drv_init();                // 按键驱动 (轮询扫描)
iwdg_drv_init();               // 看门狗 (IWDG_ENABLE 宏控制)


    menu_mgr_init();              /* 菜单系统初始化 */

    display_mgr_init(
    sys_config_get_boot_text()  // 显示上电欢迎画面
);

DEBUG_PRINTF("SYSTEM: Boot complete, entering main loop");
/* USER CODE END 2 */
```

`main.c` 开头需要 `extern` 声明 HAL 句柄:

```c
/* USER CODE BEGIN Includes */
#include "i2c_drv.h"
#include "ssd1306.h"
#include "uart_drv.h"
#include "protocol.h"
#include "display_mgr.h"
#include "led_mgr.h"
#include "key_drv.h"
#include "iwdg_drv.h"
#include "sys_config.h"
#include "debug_console.h"
#include "menu_mgr.h"

extern I2C_HandleTypeDef  hi2c2;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
/* USER CODE END Includes */
```

### 8.2 主循环集成

```c
while (1)
{
    /* USER CODE BEGIN 3 */

    // 1. 帧间超时恢复 (500ms 无完整帧则重置状态机)
    if (proto_get_last_byte_tick() > 0 &&
        HAL_GetTick() - proto_get_last_byte_tick() > PROTO_RX_TIMEOUT_MS) {
        proto_reset_rx();
    }

    // 2. 解析串口接收的二进制帧 → 命令分发
    while (uart_drv_available()) {
        uint8_t byte;
        uart_drv_read_byte(&byte);
        if (proto_feed_byte(byte)) {
            const proto_frame_t *f = proto_get_frame();
            DEBUG_PRINTF("RX: cmd=0x%02X seq=%d len=%d", f->cmd, f->seq, f->len);
            process_frame(f);
        }
    }

    // 3. 按键扫描 (20ms 周期, 消抖 + 长按检测)
    if (HAL_GetTick() - last_key_scan >= 20) {
        last_key_scan = HAL_GetTick();
        if (key_drv_scan(&key_info)) {
            // KEY1: 切换显示模式, KEY2: 切换 LED 状态
            ...
        }
    }

    // 4. LED 闪烁 tick (50ms 累加)
    // 5. 显示管理器 tick (50ms 特效调度 + 帧同步)
    // 6. 喂狗
    iwdg_drv_feed();

    /* USER CODE END 3 */
}
```

完整实现见 [`src/user_app.c`](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/src/user_app.c)。
实际 `main.c` 通过 `user_app_init()` / `user_app_handle()` 两个函数调用应用逻辑, 保持生成的 `main.c` 极简。

### 8.3 Makefile 集成

参考本工程 [`Makefile`](/E:/BaiduNetdiskDownload/code/oled_prj/stm32f407/Makefile)，编译目标由三部分组成:

```
$(CC) $(CFLAGS) $(LDFLAGS) \
    $(PROJ_SRC)     \   ← 应用模块源文件 (src/*.c)
    $(HAL_SRC)      \   ← CubeMX 生成的文件 (Core/Src/*.c)
    $(HAL_LIB)      \   ← HAL 库源文件 (Drivers/STM32F4xx_HAL_Driver/Src/*.c)
    $(STARTUP)          ← 启动文件 (startup_stm32f407xx.s)
```

**编译宏定义** (`-D` flags):

| 宏 | 含义 |
|----|------|
| `-DSTM32F407xx` | 芯片型号宏, HAL 库条件编译依赖 |
| `-DUSE_HAL_DRIVER` | 启用 HAL 驱动 |

**条件编译控制** (在 Makefile `CFLAGS` 中按需添加):

| 宏 | 默认 | 含义 |
|----|------|------|
| `-DIWDG_ENABLE` | 不定义=关闭 | 定义后启用 IWDG 看门狗 |
| `-DDEBUG_UART_ENABLE=1` | 不定义=关闭 | 定义后启用调试串口输出 |

**Include 路径**:

| 路径 | 内容 |
|------|------|
| `-Iinc` | 应用模块头文件 |
| `-ICore/Inc` | CubeMX 生成的头文件 |
| `-IDrivers/STM32F4xx_HAL_Driver/Inc` | HAL 库头文件 |
| `-IDrivers/CMSIS/Device/ST/STM32F4xx/Include` | CMSIS 设备头文件 |
| `-IDrivers/CMSIS/Include` | CMSIS 核心头文件 |

## 9. 编译与烧录

### 9.1 编译

```bash
cd stm32f407/
make                             # 输出 build/oled_f407.elf, .bin, .hex
arm-none-eabi-size build/oled_f407.elf
```

### 9.2 烧录 (ST-Link)

```bash
# st-link 命令行工具
make flash

# 或 STM32CubeProgrammer CLI
STM32_Programmer_CLI -c port=SWD -w build/oled_f407.bin 0x08000000 -rst
```

### 9.3 调试

```bash
st-util -p 4242                # 启动 GDB server
arm-none-eabi-gdb build/oled_f407.elf
(gdb) target extended-remote :4242
(gdb) load
(gdb) continue
```

## 10. 与本工程代码的对应关系

下表列出了 CubeMX 生成的 HAL 接口与本工程应用层模块的映射:

| CubeMX 生成 | HAL API | 应用模块 |
|-------------|---------|----------|
| `MX_GPIO_Init()` | `HAL_GPIO_WritePin` / `HAL_GPIO_ReadPin` | `led_mgr.c` / `key_drv.c` |
| `MX_USART1_UART_Init()` | `HAL_UART_Receive_IT` / `HAL_UART_Transmit` / `HAL_UART_RxCpltCallback` | `uart_drv.c` |
| `MX_USART2_UART_Init()` | `HAL_UART_Transmit` (阻塞) | `debug_console.c` |
| `MX_I2C2_Init()` | `HAL_I2C_Mem_Write` / `HAL_I2C_IsDeviceReady` | `i2c_drv.c` |
| `MX_IWDG_Init()` | `HAL_IWDG_Init` / `HAL_IWDG_Refresh` | `iwdg_drv.c` |
| `SystemClock_Config()` | `HAL_RCC_OscConfig` / `HAL_RCC_ClockConfig` | `main.c` |

**架构要点** (更新于 v1.1):

应用层在 HAL 之上采用 **依赖注入模式**: 每个驱动模块暴露 `xxx_init(void *handle)` 接口接收 HAL 句柄，不直接引用 CubeMX 生成的全局变量（除 `main.c` 中的 `extern` 声明）。优点:

1. **解耦**: 驱动层不依赖生成文件的全局变量名
2. **可测试**: 可以 mock HAL 句柄进行单元测试
3. **可移植**: 重新生成代码时，只需调整 `main.c` 中的 `extern` 声明和 `_init` 调用

**重新生成代码后的操作清单**:

1. CubeMX 再次 **GENERATE CODE** 后，核对 `main.c` 中 `USER CODE` 区域的应用代码未被覆盖
2. 若修改了引脚，检查 `key_drv.h` 中的 `KEY_PORTx`/`KEY_PINx` 和 `led_mgr.c` 中的 `LED_PORT`/`LED_PIN` 是否匹配
3. 新增外设时更新 Makefile 的 `HAL_SRC` 列表
4. 调整 `stm32f4xx_hal_conf.h` 确保所需 HAL 模块已 `#define HAL_XXX_MODULE_ENABLED`

---

**参考资源**:

- [STM32CubeMX 在线用户手册 (v6.18.0)](https://dev.st.stm32cube-docs/stm32cubemx/6.18.0/en/docs/markup/CubeMX_UserManual/CubeMX_UserManual_toc.html) — ST 官方文档
- [STM32CubeF4 MCU Package](https://github.com/STMicroelectronics/STM32CubeF4) — HAL 库源码 (GitHub)
- [STM32F407VGTx 数据手册](https://www.st.com/en/microcontrollers-microprocessors/stm32f407vg.html)
- [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)

**文档版本**: v1.1 | **创建日期**: 2026-07-30 | **最后更新**: 2026-08-05 | **适用工程**: OLED 串口直连控制系统
