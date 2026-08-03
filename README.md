# OLED 串口直连控制项目 (v2.0)

## 项目结构

```
oled_prj/
├── 需求描述.md              # 需求描述 (v2.0)
├── 最终需求文档.md          # 详细需求规格文档 (v2.0)
├── README.md               # 本文件
├── stm32f407/               # STM32F407 端代码（裸机 HAL）
│   ├── Makefile
│   ├── inc/                 # 头文件
│   ├── src/                 # 源文件
│   └── tools/               # 字模生成工具
├── pc_host/                 # Windows 上位机 (VS2019 Win32 + C++17)
│   ├── oled_control.sln
│   └── oled_control/
│       ├── oled_control.vcxproj
│       ├── protocol.h/cpp       # 二进制帧协议（与 STM32 端一致）
│       ├── serial_port.h/cpp    # 串口驱动
│       ├── frame_queue.h        # 线程安全队列 + TxTask
│       ├── oled_preview.h/cpp   # OLED GDI 本地模拟预览
│       ├── oled_preview_gen.h   # 字模数据 (316汉字)
│       ├── main.cpp             # WinMain + DialogProc
│       ├── resource.h           # 控件 ID 定义
│       └── oled_control.rc      # 对话框资源
├── rk3506/                  # 预留（不再使用）
├── docs/                    # 协议说明文档
└── oled_cubemx/             # STM32CubeMX 配置
```

## 系统架构 (v2.0)

```
Windows PC (VS2019 Win32 对话框) → STM32F407 → OLED (128x64 SSD1306)
        ↑ USB-TTL 串口              ↑ UART       ↑ I²C
```

## STM32F407 编译

```bash
cd stm32f407
make
make flash
```

## Windows 上位机编译

1. VS2019 打开 `pc_host/oled_control.sln`（纯 Win32 对话框，无 MFC 依赖）
2. 选择 Debug/Release + Win32/x64
3. 生成解决方案

## 运行

### 设备连接
```
USB-TTL TX → STM32F407 PA10 (USART1 RX)
USB-TTL RX → STM32F407 PA9  (USART1 TX)
USB-TTL GND → STM32F407 GND

OLED SCL → STM32F407 PB10 (I2C2 SCL)
OLED SDA → STM32F407 PB11 (I2C2 SDA)
OLED VCC → 3.3V, GND → GND
```

### 启动上位机
运行编译后的 `oled_control.exe`，选择 COM 口，点击"打开"即可。

## 通信协议

PC ↔ STM32F407 使用自定义二进制帧协议（详见 `docs/protocol-uart.md`）。

## 版本历史

- v1.4: 三级架构 PC→RK3506→STM32→OLED
- v2.0: 两级架构 PC→STM32→OLED，上位机为 VS2019 Win32 对话框应用
