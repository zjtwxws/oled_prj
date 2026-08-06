# OLED 串口直连控制项目 (v3.1)

## 项目结构

```
oled_prj/
├── 需求描述.md              # 需求描述 (v3.1)
├── 最终需求文档.md          # 详细需求规格文档 (v3.1)
├── README.md               # 本文件
├── stm32f407/               # STM32F407 端代码 (裸机 HAL)
│   ├── Makefile
│   ├── inc/                 # 头文件
│   │   ├── display_mgr.h    # 本地/远程双模式显示管理器
│   │   ├── menu_mgr.h       # 多级菜单管理器
│   │   ├── protocol.h       # 二进制帧协议
│   │   ├── key_drv.h        # 按键驱动 (含长按+重复)
│   │   └── ...
│   ├── src/                 # 源文件
│   │   ├── display_mgr.c    # 本地渲染文字+7种特效 / 远程帧缓冲刷屏
│   │   ├── menu_mgr.c       # 菜单导航状态机+渲染
│   │   ├── menu_items.c     # 菜单树静态数据 (Flash)
│   │   ├── user_app.c       # 主循环编排
│   │   └── ...
│   └── tools/               # 字模生成工具
├── pc_host/                 # Windows 上位机 (VS2019 Win32 对话框 + C++17)
│   ├── oled_control.sln
│   └── oled_control/
│       ├── oled_control.vcxproj
│       ├── protocol.h/cpp       # 二进制帧协议（与 STM32 端一致）
│       ├── serial_port.h/cpp    # 串口驱动
│       ├── frame_queue.h        # 线程安全环形队列 + TxTask
│       ├── oled_preview.h/cpp   # OLED GDI 本地模拟预览
│       ├── oled_preview_gen.h   # 字模数据 (316汉字)
│       ├── main.cpp             # WinMain + DialogProc
│       ├── resource.h           # 控件 ID 定义
│       └── oled_control.rc      # 对话框资源
├── rk3506/                  # RK3506 网关 (预留, 已实现基础代码)
├── docs/                    # 项目文档
│   ├── protocol-uart.md     # UART 协议说明
│   ├── menu-design.md       # 菜单系统方案设计
│   ├── pc-host-code-reference.md  # 上位机代码参考
│   └── stm32cubemx-f407-hal-setup.md  # CubeMX 操作文档
└── oled_cubemx/             # STM32CubeMX HAL 工程
```

## 系统架构 (v3.1)

```
Windows PC (VS2019 Win32 对话框) → STM32F407 → OLED (128x64 SSD1306)
        ↑ USB-TTL 串口              ↑ USART1      ↑ I2C
                                     PA9/PA10      PB10/PB11
```

**双模式**: 
- **远程模式**: PC 渲染帧缓冲 → 分段下发 → STM32 仅刷屏 (支持文字/时间/天气/日期)
- **本地模式**: STM32 自行渲染文字 + 7 种特效 (静态/左右上下滚/翻页/淡入淡出), 可脱离 PC 独立运行
- **模式切换**: 上位机命令 + 板载按键 KEY4 长按进入菜单 + 菜单中切换

STM32 端集成**多级菜单系统** (menu_mgr), 8 项主菜单含工作模式/显示内容/显示特效/设置/LED控制/上电文字/系统信息/预留, 通过 4 键导航。

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

KEY1~4 → STM32F407 PE1~PE4 (上拉输入)
LED    → STM32F407 PF9  (推挽输出)
```

### 启动上位机
运行编译后的 `oled_control.exe`，选择 COM 口，点击"打开"即可。

## 通信协议

PC ↔ STM32F407 使用自定义二进制帧协议（详见 `docs/protocol-uart.md`）。

## 版本历史

- v1.4: 三级架构 PC→RK3506→STM32→OLED
- v2.0: 两级架构 PC→STM32→OLED, 上位机为 VS2019 Win32 对话框应用
- v3.0: 增加本地/远程双模式, 帧缓冲分段下发
- v3.1: 集成多级菜单系统 (menu_mgr), 按键长按重复, 状态栏, 串口断开检测
