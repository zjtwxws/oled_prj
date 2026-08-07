# OLED Serial Direct Control

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: STM32F407](https://img.shields.io/badge/Platform-STM32F407-orange)](stm32f407/)
[![Language: C/C++](https://img.shields.io/badge/Language-C%2FC%2B%2B-green)]()

A desktop OLED control system that connects a Windows PC to an STM32F407 microcontroller via USB-TTL serial, driving a 0.96" SSD1306 OLED display (128×64) over I²C. Supports both **remote mode** (PC-rendered framebuffer streaming) and **local mode** (STM32 standalone rendering with 7 visual effects).

## Features

- **Dual Operating Modes**
  - **Remote Mode** — PC renders full 128×64 framebuffer and streams it to STM32 via UART. Supports 4 display modes: Text, Clock, Weather, Date.
  - **Local Mode** — STM32 runs independently without a PC. Renders user text with 7 built-in effects.
- **7 Visual Effects** — Static / Scroll Left / Scroll Right / Scroll Up / Scroll Down / Page Flip / Fade In-Out
- **Multi-Level Menu System** — 8-item main menu navigated via 4 onboard buttons, with TOGGLE / VALUE / ACTION / INFO / SUBMENU item types
- **Windows Desktop App** — VS2019 Win32 dialog application with GDI OLED preview (128×64 at 4x scale)
- **Custom Binary Protocol** — CRC-8-ATM protected frame protocol with 500ms timeout retransmission
- **Debug CLI** — On-chip debug console via USART2 with command-line interface (nr_micro_shell)
- **Linux Gateway (optional)** — RK3506-based HTTP/WebSocket + TCP server with web-based control panel

## Hardware

| Component | Specification |
|-----------|--------------|
| MCU | STM32F407VGTx (ARM Cortex-M4, 168 MHz) |
| Display | 0.96" SSD1306 OLED, 128×64 pixels, I²C |
| Connection | USB-TTL serial (115200 8N1) |
| Input | 4 push buttons (PE1~PE4) |
| LED | PF9 (push-pull output) |

**Pin Connections:**

| Function | STM32 Pin | Description |
|----------|-----------|-------------|
| USART1 TX | PA9 | PC communication |
| USART1 RX | PA10 | PC communication |
| I²C2 SCL | PB10 | OLED clock |
| I²C2 SDA | PB11 | OLED data |
| KEY1~KEY4 | PE1~PE4 | Menu navigation |
| LED | PF9 | Status indicator |

## System Architecture

```
┌─────────────────┐     USB-TTL (115200 8N1)     ┌──────────────┐     I²C     ┌────────────┐
│  Windows PC      │ ◄──────────────────────────► │  STM32F407   │ ◄─────────► │ SSD1306    │
│  (Win32 Dialog)  │     Custom Binary Protocol   │  (bare metal) │            │ 128×64 OLED│
└─────────────────┘                              └──────────────┘            └────────────┘
                                                         ▲
                                                         │ UART (optional)
                                                  ┌──────┴──────┐
                                                  │  RK3506      │
                                                  │  Gateway     │
                                                  │ (HTTP/WS/TCP)│
                                                  └──────────────┘
```

### Remote Mode (PC → STM32)
PC renders framebuffer → segments into ≤200B chunks → streams via `CMD_FRAME_SYNC` → STM32 assembles and writes to SSD1306.

### Local Mode (STM32 standalone)
STM32 reads boot text from Flash → renders with selected effect → updates OLED at 20 FPS. No PC required.

## Quick Start

### Prerequisites

- **STM32**: `arm-none-eabi-gcc` (Arm GNU Toolchain), `st-flash` (optional for flashing)
- **PC Host**: Visual Studio 2019 with C++17 support
- **Hardware**: STM32F407 board, SSD1306 OLED, USB-TTL adapter, 4 push buttons

### Build STM32 Firmware

```bash
cd stm32f407
make          # builds oled_f407.elf, .bin, .hex into build/
make flash    # flash via ST-Link
```

### Build Windows Host

1. Open `pc_host/oled_control.sln` in Visual Studio 2019
2. Select `Debug` or `Release`, `x64` or `Win32`
3. Build → `oled_control.exe`

### Run

1. Connect hardware per pin table above
2. Launch `oled_control.exe`
3. Select COM port and click "Open"
4. Use the control panel or onboard buttons (KEY4 long-press activates the menu)

## Project Structure

```
oled_prj/
├── stm32f407/              # STM32F407 firmware (bare metal, GCC)
│   ├── inc/                # Headers (driver layer + application layer)
│   ├── src/                # Sources (16 modules, ~6200 lines C)
│   │   ├── ssd1306.c       # SSD1306 OLED driver
│   │   ├── display_mgr.c   # Dual-mode display manager + 7 effects
│   │   ├── menu_mgr.c      # Multi-level menu state machine
│   │   ├── menu_items.c    # Menu tree static data
│   │   ├── protocol.c      # Binary frame protocol (CRC-8-ATM)
│   │   ├── font.c          # ASCII 8×16 + Chinese 16×16 font engine
│   │   ├── key_drv.c       # Key driver (long-press + repeat)
│   │   ├── user_app.c      # Main loop orchestration
│   │   └── ...
│   ├── Makefile
│   └── tools/              # Font generation scripts
├── pc_host/                # Windows desktop application (C++17, Win32)
│   └── oled_control/
│       ├── main.cpp        # WinMain + DialogProc
│       ├── protocol.cpp    # Binary frame protocol (C++ port)
│       ├── serial_port.cpp # Win32 serial port driver
│       ├── oled_preview.cpp# GDI OLED preview (128×64 at 4x)
│       └── ...
├── rk3506/                 # Linux gateway (optional, C11)
│   ├── src/                # UART adapter, TCP/Web server, command dispatcher
│   ├── web/index.html      # Web control panel
│   └── mongoose.c/h        # Embedded HTTP/WebSocket library
├── oled_cubemx/            # STM32CubeMX HAL project files
├── docs/                   # Detailed documentation
│   ├── protocol-uart.md    # Binary protocol specification
│   ├── menu-design.md      # Menu system design
│   ├── menu-key-design.md  # Key mapping and navigation rules
│   ├── pc-host-code-reference.md  # PC host code reference
│   ├── stm32cubemx-f407-hal-setup.md  # CubeMX setup guide
│   └── ...
└── .skills/                # Codex AI assistant skills
```

## Communication Protocol

The system uses a custom binary frame protocol over UART (115200 8N1):

```
┌──────┬──────┬──────┬──────┬────────────┬──────┬──────┐
│ SOF  │ LEN  │ CMD  │ SEQ  │   DATA     │ CRC8 │ EOF  │
│ 0xA5 │ 1B   │ 1B   │ 1B   │  0~251B    │ 1B   │ 0x5A │
└──────┴──────┴──────┴──────┴────────────┴──────┴──────┘
```

- CRC-8-ATM (polynomial 0x07)
- Master-slave: PC initiates, STM32 replies ACK/NAK
- Timeout retransmission: 500ms × 3 attempts
- Framebuffer streaming: 1024 bytes in ≤200B segments via `CMD_FRAME_SYNC`

Full specification: [docs/protocol-uart.md](docs/protocol-uart.md)

## Documentation

| Document | Description |
|----------|-------------|
| [Protocol Specification](docs/protocol-uart.md) | Complete binary frame protocol with state machine |
| [Menu System Design](docs/menu-design.md) | Multi-level menu architecture and navigation |
| [Key Mapping](docs/menu-key-design.md) | Button assignments and input handling |
| [PC Host Reference](docs/pc-host-code-reference.md) | Windows application module breakdown |
| [CubeMX Setup](docs/stm32cubemx-f407-hal-setup.md) | STM32CubeMX project creation guide |
| [AGENTS.md](AGENTS.md) | Coding conventions and project rules |

## License

This project is licensed under the MIT License — see [LICENSE](LICENSE) for details.

The STM32F4xx HAL Driver and CMSIS components under `oled_cubemx/Drivers/` are provided by STMicroelectronics under their respective license terms.
