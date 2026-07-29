# OLED 三级联动嵌入式项目

## 项目结构

```
oled_prj/
├── 需求描述.md              # 需求文档
├── 最终需求文档.md          # 最终需求文档
├── stm32f407/               # STM32F407 端代码
│   ├── Makefile
│   ├── inc/                 # 头文件
│   │   ├── ssd1306.h        # OLED 底层驱动
│   │   ├── i2c_drv.h        # I²C 抽象层
│   │   ├── font.h           # 字库管理
│   │   ├── display_mgr.h    # 显示管理器(7种特效)
│   │   ├── uart_drv.h       # UART 驱动(环形缓冲+DMA)
│   │   ├── protocol.h       # 二进制帧协议
│   │   ├── led_mgr.h        # LED 状态机
│   │   ├── key_drv.h        # 按键驱动(消抖+长按)
│   │   ├── iwdg_drv.h       # 看门狗(条件编译)
│   │   └── sys_config.h     # 系统配置(Flash存储)
│   └── src/
│       ├── main.c           # 主程序
│       ├── ssd1306.c
│       ├── i2c_drv.c
│       ├── font.c
│       ├── display_mgr.c
│       ├── uart_drv.c
│       ├── protocol.c
│       ├── led_mgr.c
│       ├── key_drv.c
│       ├── iwdg_drv.c
│       └── sys_config.c
├── rk3506/                  # RK3506 端代码
│   ├── Makefile
│   ├── inc/
│   │   ├── protocol.h       # 二进制帧协议(C)
│   │   ├── uart_adapter.h   # 串口适配器
│   │   ├── cmd_dispatcher.h # 命令分发器
│   │   ├── web_server.h     # Web服务器(mongoose)
│   │   └── tcp_server.h     # TCP服务器
│   ├── src/
│   │   ├── main.c
│   │   ├── protocol.c
│   │   ├── uart_adapter.c
│   │   ├── cmd_dispatcher.c
│   │   ├── web_server.c
│   │   └── tcp_server.c
│   └── web/
│       └── index.html       # Web前端单页应用
└── docs/                    # 文档
```

## STM32F407 编译

### 前置条件
1. STM32CubeMX 生成初始化代码 (HAL 库)
2. ARM GNU Toolchain (`arm-none-eabi-gcc`)
3. stlink 工具 (`st-flash`)

### 步骤
```bash
# 1. 用 CubeMX 生成基础工程 (配置 I²C1, USART1, GPIO)
# 2. 将本项目的 inc/ 和 src/ 文件合并到 CubeMX 工程
# 3. 修改 Makefile 中的 HAL_SRC / HAL_LIB 路径
# 4. 修改各驱动中 GPIO 引脚定义 (LED, KEY) 为实际板卡引脚
cd stm32f407
make
make flash
```

### CubeMX 配置要点
| 外设 | 模式 | 参数 |
|------|------|------|
| I2C1 | I2C | 100kHz |
| USART1 | Asynchronous | 115200, 8N1 |
| GPIO (LED) | Output | 推挽, 无上拉 |
| GPIO (KEY1/KEY2) | Input | 上拉, 外部中断 |

## RK3506 编译

> **开发语言**: 纯 C (C11 标准)，无 C++ 运行时依赖。

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
STM32F407 I2C  → OLED (SCL→PB6, SDA→PB7)
```

### 启动 RK3506
```bash
./oled_gateway /dev/ttyS1 80 9527
```

### 访问 Web
浏览器打开 `http://<RK3506_IP>/`

### TCP 调试
```bash
nc <RK3506_IP> 9527
> {"cmd":"led","data":{"state":1}}
```

## 架构说明

详细需求文档见 `最终需求文档.md`

三层架构: PC浏览器 ↔ RK3506(Linux网关) ↔ STM32F407(执行控制) ↔ OLED(显示)
通信协议: WebSocket/JSON (PC↔RK) + 自定义二进制帧 (RK↔STM) + I²C (STM↔OLED)
