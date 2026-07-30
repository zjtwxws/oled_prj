# OLED 三级联动嵌入式项目

## 项目结构

```
oled_prj/
├── 需求描述.md              # 原始需求描述
├── 最终需求文档.md          # 详细需求规格文档
├── README.md               # 本文件
├── stm32f407/               # STM32F407 端代码
│   ├── Makefile
│   ├── inc/                 # 头文件
│   │   ├── ssd1306.h        # OLED 底层驱动 (SSD1306 I²C)
│   │   ├── i2c_drv.h        # I²C 抽象层 (超时保护)
│   │   ├── font.h           # 字库管理 (ASCII + GB2312)
│   │   ├── display_mgr.h    # 显示管理器 (7 种特效)
│   │   ├── uart_drv.h       # UART 驱动 (中断接收 + 环形缓冲)
│   │   ├── protocol.h       # 二进制帧协议 (超时恢复)
│   │   ├── led_mgr.h        # LED 状态机 (关/开/闪烁)
│   │   ├── key_drv.h        # 按键驱动 (消抖 + 长按, 真实时间)
│   │   ├── iwdg_drv.h       # 看门狗 (条件编译)
│   │   ├── sys_config.h     # 系统配置 (Flash 存储)
│   │   └── debug_console.h  # 调试串口 (编译开关)
│   ├── src/
│   │   ├── main.c           # 主程序
│   │   ├── ssd1306.c        # SSD1306 驱动 (参照厂家示例)
│   │   ├── i2c_drv.c        # I²C 驱动 (超时保护)
│   │   ├── font.c           # 字库 (ASCII + 中文 + LRU 缓存)
│   │   ├── display_mgr.c    # 显示管理器
│   │   ├── uart_drv.c       # UART 驱动 (IT 错误恢复)
│   │   ├── protocol.c       # 协议引擎
│   │   ├── led_mgr.c        # LED 管理器
│   │   ├── key_drv.c        # 按键驱动
│   │   ├── iwdg_drv.c       # 看门狗
│   │   ├── sys_config.c     # Flash 配置
│   │   └── debug_console.c  # 调试串口
│   └── tools/
│       └── gen_font.py      # 字模生成工具
├── rk3506/                  # RK3506 端代码
│   ├── Makefile
│   ├── inc/
│   │   ├── protocol.h       # 二进制帧协议 (C)
│   │   ├── uart_adapter.h   # 串口适配器
│   │   ├── cmd_dispatcher.h # 命令分发器
│   │   ├── web_server.h     # Web 服务器 (mongoose)
│   │   └── tcp_server.h     # TCP 服务器
│   ├── src/
│   │   ├── main.c
│   │   ├── protocol.c
│   │   ├── uart_adapter.c
│   │   ├── cmd_dispatcher.c
│   │   ├── web_server.c
│   │   └── tcp_server.c
│   └── web/
│       └── index.html       # Web 前端单页应用
└── docs/
    └── protocol-uart.md     # 串口协议详细说明
```

## STM32F407 编译

### 前置条件
1. STM32CubeMX 生成初始化代码 (HAL 库)
2. ARM GNU Toolchain (`arm-none-eabi-gcc`)
3. stlink 工具 (`st-flash`)

### 步骤
```bash
# 1. 用 CubeMX 生成基础工程 (配置 I²C1, USART1, USART2, GPIO)
# 2. 将本项目的 inc/ 和 src/ 文件合并到 CubeMX 工程
# 3. 修改 Makefile 中的 HAL_SRC / HAL_LIB 路径
# 4. 修改 led_mgr.c 中的 LED_PORT/LED_PIN 为实际板卡引脚
cd stm32f407
make
make flash
```

### CubeMX 配置要点
| 外设 | 模式 | 参数 |
|------|------|------|
| I2C1 | I2C | **Fast Mode 400kHz** (SSD1306 完全支持; 参照厂家示例) |
| USART1 | Asynchronous | 115200, 8N1 (与 RK3506 通信) |
| USART2 | Asynchronous | 115200, 8N1, **仅 TX** (调试串口, 可选) |
| GPIO (LED) | Output | 推挽, 无上拉 (PB0, 根据实际板卡修改) |
| GPIO (KEY1~KEY4) | Input | 上拉, PA1~PA4 (参照 HelTec OLED+按键模组接线) |

### 调试串口
- 通过 `debug_console.h` 中的 `DEBUG_UART_ENABLE` 宏控制, 默认为 `0` (关闭)
- 开启时需在 Makefile 中取消 `PROJ_SRC += src/debug_console.c` 的注释
- 关闭时零开销, 所有 `DEBUG_PRINTF` 等宏编译为空

### 看门狗
- 通过 `iwdg_drv.h` 中的 `IWDG_ENABLE` 宏控制, 默认**未定义** (关闭)
- 发布时取消注释 `#define IWDG_ENABLE 1` 启用
- 复位周期约 1s, 主循环末尾喂狗

### 按键引脚映射 (参照厂家模组接线)
| 按键 | 引脚 | 说明 |
|------|------|------|
| KEY1 | PA1 | 切换显示模式 |
| KEY2 | PA2 | LED 开/关/闪烁切换 |
| KEY3 | PA3 | 预留 (需将 key_drv.h 中 `KEY_COUNT` 改为 4) |
| KEY4 | PA4 | 预留 |

## RK3506 编译

> **开发语言**: 纯 C (C11 标准), 无 C++ 运行时依赖。

### 前置条件
1. Buildroot 生成的交叉编译工具链 (`arm-linux-gnueabihf-gcc`)
2. mongoose 单文件库 (mongoose.c / mongoose.h)

### 步骤
```bash
# 1. 下载 mongoose: wget https://raw.githubusercontent.com/cesanta/mongoose/master/mongoose.c
# 2. 将 mongoose.c 和 mongoose.h 放入 rk3506/ 目录
cd rk3506
make
make deploy
```

## 运行

### 设备连接
```
RK3506 UART TX → STM32F407 UART RX (PA10)
RK3506 UART RX → STM32F407 UART TX (PA9)
RK3506 GND     → STM32F407 GND
STM32F407 I2C  → OLED (SCL→PB6, SDA→PB7, VCC→3.3V, GND→GND)
```

### 启动 RK3506
```bash
./oled_gateway /dev/ttyFIQ0 8080 9527
```

### 访问 Web
浏览器打开 `http://<RK3506_IP>:8080/`

### TCP 调试
```bash
nc <RK3506_IP> 9527
> {"cmd":"led","data":{"state":1}}
```

## 架构说明

详细需求文档见 `最终需求文档.md`

三层架构: PC浏览器 ↔ RK3506(Linux网关) ↔ STM32F407(执行控制) ↔ OLED(显示)
通信协议: WebSocket/JSON (PC↔RK) + 自定义二进制帧 (RK↔STM) + I²C (STM↔OLED)

## 固件健壮性设计

| 机制 | 实现 | 位置 |
|------|------|------|
| I²C 总线超时保护 | HAL_I2C_Mem_Write 超时 100ms, 避免总线挂死永久阻塞 | `i2c_drv.c` |
| 协议帧间超时恢复 | 500ms 未收到完整帧自动复位接收状态机 | `protocol.c` + `main.c` |
| 发送临界区保护 | build → send 之间关中断, 防止 ISR 覆盖全局 tx_buf | `main.c:safe_send()` |
| UART IT 接收恢复 | HAL_UART_Receive_IT 重启失败时 AbortReceive + 重试 | `uart_drv.c` |
| 独立看门狗 | 1s 复位周期, 条件编译开关 | `iwdg_drv.c/h` |
| 汉字查询 LRU 缓存 | 5 条目 round-robin, 高频字命中率 > 90% | `font.c` |
