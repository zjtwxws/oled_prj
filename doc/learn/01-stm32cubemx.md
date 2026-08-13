# 学习笔记 01 — STM32CubeMX 使用及技巧

> 结合 `oled_prj` 项目实战经验，总结 STM32CubeMX 的完整使用流程、关键配置项含义，以及大量踩坑后沉淀的使用技巧。
> 配套参考：[docs/stm32cubemx-f407-hal-setup.md](../../docs/stm32cubemx-f407-hal-setup.md)（详细操作文档）
> 目标芯片：STM32F407VGTx（LQFP100，1MB Flash，192KB RAM）

---

## 一、STM32CubeMX 是什么

### 1.1 定位

STM32CubeMX 是 ST 官方提供的**图形化工程生成工具**：

- 可视化配置引脚（Pinout）、外设（Peripheral）、时钟树（Clock）
- 自动生成**初始化代码**（main.c、gpio.c、usart.c、i2c.c 等）
- 与 STM32Cube HAL 库配合，显著降低开发门槛

### 1.2 与 CubeIDE / Keil 的关系

| 工具 | 定位 | 本项目用法 |
|------|------|-----------|
| **STM32CubeMX** | 图形化配置 + 代码生成 | 生成 `oled_cubemx/` 基础工程 |
| **STM32CubeIDE** | 集成开发环境（含编译调试） | 未使用 |
| **Keil MDK** | 集成开发环境 | 编译 Bootloader（`stm32f407/iap/`） |
| **GCC + Makefile** | 命令行编译 | 编译 APP 双槽固件（`stm32f407/Makefile`） |

> 关键认知：CubeMX **只负责生成初始化代码**，编译工具链（Keil/GCC/Makefile）是另一套东西，两者通过「生成代码 → 加入工程编译」衔接。

### 1.3 本项目的工程组织（重要技巧）

本项目 CubeMX 生成的工程放在独立的 `oled_cubemx/` 目录，APP 源码放在 `stm32f407/src/`：

```
oled_cubemx/                    ← CubeMX 生成区（可反复重新生成）
├── oled_cubemx.ioc             ← CubeMX 工程文件（配置的唯一真源）
├── Src/main.c gpio.c i2c.c usart.c ...   ← 外设初始化代码
└── Drivers/STM32F4xx_HAL_Driver/          ← HAL 库

stm32f407/                      ← 手写业务代码区（与生成区分离）
├── src/*.c                     ← 驱动层 + 应用层源码
├── inc/*.h
└── Makefile                    ← GCC 编译脚本
```

**为什么这样组织？**
1. CubeMX 重新生成代码时只动 `oled_cubemx/`，不会影响手写的业务代码
2. 多编译目标（Bootloader / SlotA / SlotB）可以共用同一份外设初始化代码
3. 不同工具链（Keil/GCC）可以共用同一份 CubeMX 输出

---

## 二、环境准备

| 工具 | 用途 |
|------|------|
| STM32CubeMX 6.18.0+ | 图形化配置 & 代码生成 |
| STM32Cube FW_F4 固件包 | HAL 库（CubeMX → Help → Manage embedded software packages 安装） |
| arm-none-eabi-gcc | 交叉编译（Makefile 方案） |
| st-flash / ST-Link | 烧录 |

**首次配置注意**：CubeMX → Help → Updater Settings 可设置固件包下载仓库路径（网络不好时改为 ST 国内镜像）。

---

## 三、新建工程流程

### 3.1 选择芯片

File → New Project → MCU/MPU Selector → 搜索 `STM32F407VG` → 选择 `STM32F407VGTx` → Start Project。

> 本项目实际使用 F407VGTx（LQFP100）。若用 ZETx（144 脚）或 VETx（512KB Flash），**代码兼容**，只需调整链接脚本中的 Flash/RAM 大小。

### 3.2 四大选项卡

| 选项卡 | 作用 |
|--------|------|
| **Pinout & Configuration** | 配置外设、引脚、中断、DMA |
| **Clock Configuration** | 配置时钟树（HSE/PLL/总线分频） |
| **Project Manager** | 工程名、工具链、代码生成选项 |
| **Tools** | 功耗计算、引脚报表等辅助工具 |

---

## 四、时钟配置 — 理解时钟树（重点）

### 4.1 为什么时钟这么重要

STM32 的每个外设都挂在**总线**上（AHB/APB1/APB2），外设时钟频率由**时钟树**决定。配置错误轻则外设不工作，重则芯片超频死机。本项目统一采用 **168MHz 主频**。

### 4.2 本项目时钟树参数（对应 main.c 的 SystemClock_Config）

| 参数 | 值 | 含义 |
|------|-----|------|
| HSE | 8 MHz（外部晶振） | 时钟源头 |
| PLLM | /8 | 8MHz → 1MHz（PLL 输入） |
| PLLN | ×336 | VCO = 336MHz |
| PLLP | /2 | **SYSCLK = 168MHz** |
| PLLQ | /7 | 48MHz（USB/SDIO/RNG 用） |
| AHB Prescaler | /1 | HCLK = 168MHz |
| APB1 Prescaler | /4 | PCLK1 = **42MHz**（上限） |
| APB2 Prescaler | /2 | PCLK2 = **84MHz**（上限） |
| Flash Latency | 5 WS | 168MHz 必须 ≥5 等待周期 |

```
HSE 8MHz ──PLLM(/8)──> 1MHz ──PLLN(×336)──> VCO 336MHz ──PLLP(/2)──> SYSCLK 168MHz
                                                            ──PLLQ(/7)──> 48MHz (USB)
SYSCLK ──AHB(/1)──> HCLK 168MHz ──APB1(/4)──> PCLK1 42MHz
                              └──────APB2(/2)──> PCLK2 84MHz
```

### 4.3 总线频率上限速记（F4 系列）

| 总线 | 上限 | 挂载的外设示例 |
|------|------|---------------|
| AHB | 168MHz | GPIO、Flash、DMA |
| APB1 | **42MHz** | USART2、I2C2、PWR、IWDG |
| APB2 | **84MHz** | USART1、SPI1、ADC |

> **踩坑经验**：USART1 在 APB2（84MHz），USART2 在 APB1（42MHz）。如果自行修改总线分频，**串口波特率会跟着变**（HAL 库按当前总线时钟计算 BRR），曾因改分频导致调试串口乱码。

### 4.4 时钟配置技巧

- 在 Clock Configuration 页直接**输入目标频率**（如 168），CubeMX 自动计算分频组合
- 配置非法时界面红色提示，CubeMX 会自动求解可行方案
- Flash Latency 必须匹配 SYSCLK，否则**程序跑飞或 Flash 读错数据**

---

## 五、外设配置要点（本项目清单）

### 5.1 SYS（系统核心）

| 配置 | 值 | 说明 |
|------|-----|------|
| Debug | Serial Wire | SWD 调试（SWDIO+SWCLK） |
| Timebase Source | SysTick | HAL 滴答时钟源 |

> **为什么选 SysTick 而非 TIM？** 简单工程用 SysTick 即可；若 SysTick 被 RTOS 占用，可改用 TIM 作为 HAL Timebase。

### 5.2 IWDG 独立看门狗

| 参数 | 值 | 含义 |
|------|-----|------|
| Prescaler | 256 | LSI 32kHz / 256 = 125Hz（8ms/tick） |
| Reload | 125 | 超时 = 125 × 8ms = **1000ms** |
| Window | 4095（最大） | 不使能窗口模式 |

**看门狗理解**：一旦使能，软件必须周期喂狗（`iwdg_drv.c` 封装），否则芯片自动复位。它是防程序跑飞的最后防线，但**调试时容易误触发复位**，本项目用 `IWDG_ENABLE` 宏控制开关。

### 5.3 USART1 — PC 上位机通信（协议口）

- Mode: Asynchronous；引脚 PA9(TX)/PA10(RX)
- 115200-8-N-1，**使能全局中断**（接收用中断方式）
- 可选 DMA：`USART1_TX`，Memory To Peripheral

### 5.4 USART2 — 调试串口（CLI 口）

- Mode: Asynchronous；引脚 PA2(TX)/PA3(RX)，115200-8-N-1
- 全双工（CLI 需要同时收发）
- 使能 `USART2_IRQn`（NVIC），中断回调中把数据分发给 CLI 引擎

> **踩坑经验**：早期按键占用 PA2/PA3 导致 USART2 无法全双工，后来把 4 个按键全部移到 PE1~PE4 才解决。**引脚冲突排查**是 CubeMX 配置阶段的必修课——引脚图上有冲突会直接红色标记。

### 5.5 I2C2 — OLED 显示

| 参数 | 值 |
|------|-----|
| I2C Speed Mode | Fast Mode |
| I2C Speed Frequency | 400kHz |

- 引脚 PB10(SCL)/PB11(SDA)
- OLED 从机地址 `0x3C`（7-bit），HAL 内部自动左移 1 位

### 5.6 GPIO — 按键与 LED

| 引脚 | 标签 | 模式 |
|------|------|------|
| PE1~PE4 | KEY1~KEY4 | GPIO_Input，**Pull-up**（外部上拉+按下接地=active low） |
| PF9 | LED | GPIO_Output，Push-Pull |

> **按键为什么用上拉？** 按键一端接地一端接引脚，未按下时引脚必须被拉高，按下才读到低电平。内部上拉省去外部电阻。

---

## 六、Project Manager 设置（决定代码怎么生成）

### 6.1 关键选项

| 选项 | 推荐值 | 说明 |
|------|--------|------|
| Toolchain / IDE | Makefile（本项目） | 也可选 MDK-ARM / STM32CubeIDE |
| Copy only necessary library files | 勾选 | 只复制用到的 HAL 文件，减小工程 |
| Generate peripheral initialization as .c/.h pairs | 勾选 | 每个外设独立生成（gpio.c、i2c.c...） |
| Set all free pins as analog | 勾选 | 未用引脚设为模拟，降低功耗 |
| **Keep User Code when re-generating** | **必勾** | 重新生成不覆盖 `USER CODE BEGIN/END` 之间的代码 |

### 6.2 USER CODE 保护机制（重点技巧）

CubeMX 生成的文件中带有**用户代码保护区**：

```c
  /* USER CODE BEGIN 0 */
  // 这里写自定义代码，重新生成时会被保留
  /* USER CODE END 0 */
```

**铁律**：
1. 自定义代码**必须写在 BEGIN/END 之间**，否则重新生成会被覆盖
2. 不要在生成区直接改外设初始化逻辑——改配置应回 CubeMX 改，再重新生成
3. 生成区与手写区分离（本项目 `oled_cubemx/` vs `stm32f407/src/`）是最稳妥的做法

---

## 七、代码生成与工程衔接

### 7.1 生成目录结构

```
oled_cubemx/
├── oled_cubemx.ioc          ← 配置真源（工程文件）
├── Inc/                     ← 头文件（main.h、gpio.h、usart.h...）
├── Src/                     ← 源文件（main.c、gpio.c、i2c.c...）
├── Drivers/                 ← HAL 库 + CMSIS
└── MDK-ARM/                 ← Keil 工程（如选 Makefile 则无此目录）
```

### 7.2 本项目如何把生成代码接入 Makefile

`stm32f407/Makefile` 中通过变量引用 CubeMX 输出：

```makefile
CORE_DIR  = ../oled_cubemx

HAL_SRC = $(CORE_DIR)/Src/main.c $(CORE_DIR)/Src/stm32f4xx_it.c \
          $(CORE_DIR)/Src/stm32f4xx_hal_msp.c $(CORE_DIR)/Src/system_stm32f4xx.c
HAL_LIB = $(CORE_DIR)/Drivers/STM32F4xx_HAL_Driver/Src/*.c
INC_APP = -Iinc -I$(CORE_DIR)/Inc -I$(CORE_DIR)/Drivers/...
```

**技巧**：`HAL_LIB` 用通配符 `*.c` 一次性引入所有 HAL 驱动，配合编译器的 `-ffunction-sections -Wl,--gc-sections` 把没用的函数裁掉，避免代码膨胀。

---

## 八、实战踩坑与技巧汇总

### 8.1 常见坑

| 坑 | 现象 | 解决 |
|----|------|------|
| 未勾选 Keep User Code | 重新生成后自定义代码丢失 | 勾选该选项；或把业务代码移到 `stm32f407/src/` |
| Flash Latency 不足 | 高主频下程序异常/死机 | 168MHz 必须 5WS |
| 修改总线分频 | 串口乱码 | 波特率按总线时钟重算，改分频后重新生成 |
| 引脚冲突 | 一个引脚被两个外设占用 | 用 CubeMX 引脚图排查，把外设换到其他引脚 |
| 按键未配 Pull-up | 按键电平不稳定/悬空 | GPIO Input 模式选 Pull-up |
| 看门狗忘记喂 | 程序周期性复位 | 主循环周期喂狗；调试期用宏关闭 |
| 重新生成覆盖手写初始化 | 外设配置被还原 | 遵循「改配置回 CubeMX，业务代码独立目录」原则 |

### 8.2 提升效率的技巧

1. **工程与代码分离**：CubeMX 生成区（`oled_cubemx/`）与手写代码（`stm32f407/src/`）分离，是反复迭代的基础
2. **一个 CubeMX 工程多目标复用**：Bootloader、SlotA、SlotB 共用 `oled_cubemx/` 的初始化代码，通过 Makefile 不同链接脚本区分
3. **.ioc 文件是配置真源**：改配置永远改 .ioc 再生成，不要手工改生成代码
4. **生成后第一时间 git commit**：记录每个外设配置的版本
5. **善用 Pinout 视图**：点击引脚直接配置模式，冲突会标红

---

## 九、自测题

1. STM32F407 系统时钟 168MHz 的完整链路是什么？APB1/APB2 各是多少？
2. USART1 和 USART2 分别挂在哪条总线上？最大波特率受什么限制？
3. 为什么按键输入要配置为上拉？按下时读到的电平是什么？
4. `Keep User Code when re-generating` 选项的作用是什么？自定义代码应该写在哪里？
5. IWDG 超时时间是怎么计算的？（本项目 256 分频 + Reload=125）
6. 若把 HSE 从 8MHz 换成 12MHz 晶振，时钟树需要调整哪些参数？
7. 为什么本项目把 CubeMX 生成代码和业务代码放在不同目录？

<details>
<summary>参考答案</summary>

1. HSE 8MHz → PLLM/8 → 1MHz → PLLN×336 → 336MHz → PLLP/2 → SYSCLK 168MHz；APB1=42MHz，APB2=84MHz
2. USART1 在 APB2（84MHz），USART2 在 APB1（42MHz）；波特率上限受总线时钟限制
3. 上拉保证未按下时引脚为高电平（释放态）；按下接地读到低电平（0）
4. 重新生成代码时保留 USER CODE BEGIN/END 之间的内容；自定义代码写在这两个标记之间
5. LSI 32kHz / 256 = 125Hz → 每 tick 8ms → 125 × 8ms = 1000ms
6. 需重新计算 PLLM/PLLN 使 VCO 仍在允许范围且输出 168MHz；例如 PLLM=12 保持 1MHz 输入，PLLN 不变
7. 便于 CubeMX 反复重新生成而不影响业务代码；也便于多编译目标复用同一套外设初始化

</details>
