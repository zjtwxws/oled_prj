# OTA 上位机工具设计说明书

> 版本: V1.0 | 日期: 2026-08-10

---

## 1. 概述

独立的上位机工具, 通过串口 (USART1, 115200 8N1) 向 STM32 Bootloader 发送固件升级数据。

**与现有 `pc_host/` 的关系**: 完全独立, 不复用。现有 `pc_host/` 是 Windows C++ GUI 程序 (OledControl), 负责日常 OLED 显示内容下发。OTA 工具是 Python CLI, 仅在固件升级场景使用。

---

## 2. 技术选型

| 项 | 选择 | 理由 |
|----|------|------|
| 语言 | Python 3.8+ | 跨平台, 零编译, 串口库成熟 |
| 串口库 | `pyserial` (pySerial) | 广泛使用, 稳定可靠 |
| 打包 | 可选 `pyinstaller` 生成单文件 .exe | 方便 Windows 用户免安装 Python |
| 进度显示 | `tqdm` (可选) | 实时进度条 |

---

## 3. 目录结构

```
tools/ota_tool/
├── ota_tool.py      主入口 (CLI)
├── proto.py         协议帧构建/解析 (复现 C 端逻辑)
├── ota_client.py    OTA 升级流程控制
├── crc32.py         CRC32 计算 (IEEE 802.3)
└── requirements.txt Python 依赖
```

---

## 4. 命令行接口

```
usage: ota_tool.py [-h] [--baud BAUD] [--force-slot {0,1}]
                   [--timeout TIMEOUT] [--retry RETRY]
                   port firmware

STM32F407 OTA 固件升级工具 (A/B 双槽位)

positional arguments:
  port                 串口号 (Windows: COM3, Linux: /dev/ttyUSB0)
  firmware             固件 .bin 文件路径

optional arguments:
  --baud BAUD          波特率 (默认: 115200)
  --force-slot {0,1}   强制指定目标槽 (0=A, 1=B, 默认: 自动选择非活跃槽)
  --timeout TIMEOUT    帧超时(秒) (默认: 5)
  --retry RETRY        最大重试次数 (默认: 3)
```

### 使用示例

```bash
# 基础用法: 自动选择非活跃槽升级
python ota_tool.py COM3 app_slot_b.bin

# 强制升级到 Slot A
python ota_tool.py COM3 app_slot_a.bin --force-slot 0

# 非标准波特率
python ota_tool.py /dev/ttyUSB0 firmware.bin --baud 460800
```

---

## 5. 升级流程

### 5.1 主机序 (Python 端)

```
1. 解析 .bin 文件 → 固件数据 + 文件大小
2. 计算 CRC32 (全文件)
3. 打开串口 (115200 8N1, timeout=5s)
4. 触发 Bootloader 进入更新模式:
   ├─ 方案A: 发送 CMD_OTA_RESERVED(0x07) → 等待 ACK → 等待 2s
   └─ 方案B: 用户手动 KEY1 长按上电 (无需上位机操作)
5. 发送 CMD_OTA_START(slot, size, crc32, version)
   → 等待 ACK (5s 超时) → 否则 NAK 错误退出
6. 分块发送 CMD_OTA_DATA:
   chunk_size = 200 字节
   for offset in 0, 200, 400, ...:
       发送 OTA_DATA(offset, chunk)
       等待 ACK → 否则重试 (最多 3 次)
       更新进度条
7. 发送 CMD_OTA_FINISH
   → 等待 ACK → 升级成功!
   → NAK(CRC 错误) → 升级失败
```

### 5.2 触发 Bootloader 方式

| 方式 | 说明 | 适用场景 |
|------|------|----------|
| **APP 命令触发** | 上位机先发 `CMD_OTA_RESERVED(0x07)` 给运行中的 APP, APP 写入 `ota_request` 后复位进入 Bootloader | 设备已正常运行 |
| **手动触发** | 用户按住 KEY1 上电, Bootloader 直接进入更新模式 | 设备无固件 / APP 崩溃 |

工具默认先尝试 APP 命令触发, 超时 3 秒后提示用户手动触发。

---

## 6. 协议帧构建 (Python 端)

### 6.1 帧格式

```python
def build_frame(cmd: int, data: bytes) -> bytes:
    """构建: SOF(0xA5) + LEN + CMD + DATA + CRC8 + EOF(0x5A)"""
    buf = bytearray()
    buf.append(0xA5)                # SOF
    buf.append(len(data))           # LEN
    buf.append(cmd)                 # CMD
    buf.extend(data)                # DATA
    crc = crc8(bytes(buf))          # CRC-8-ATM
    buf.append(crc)
    buf.append(0x5A)                # EOF
    return bytes(buf)
```

### 6.2 OTA 帧构建

```python
# CMD_OTA_START: slot(1) + size(4 LE) + crc32(4 LE) + version(4 LE)
start_data = struct.pack('<BIII', slot, size, crc32_val, version)
frame = build_frame(0x07, start_data)

# CMD_OTA_DATA: offset(4 LE) + payload(N)
data_data = struct.pack('<I', offset) + payload
frame = build_frame(0x08, data_data)

# CMD_OTA_FINISH: 无数据
frame = build_frame(0x09, b'')
```

### 6.3 CRC-8-ATM

```python
CRC8_TABLE = [
    0x00,0x07,0x0E,0x09,0x1C,0x1B,0x12,0x15,0x38,0x3F,0x36,0x31,
    0x24,0x23,0x2A,0x2D,0x70,0x77,0x7E,0x79,0x6C,0x6B,0x62,0x65,
    0x48,0x4F,0x46,0x41,0x54,0x53,0x5A,0x5D,0xE0,0xE7,0xEE,0xE9,
    # ... (完整 256 字节表)
]

def crc8(data: bytes) -> int:
    crc = 0x00
    for b in data:
        crc = CRC8_TABLE[crc ^ b]
    return crc
```

### 6.4 CRC32 (IEEE 802.3)

```python
def crc32_ieee(data: bytes) -> int:
    """CRC32 IEEE 802.3, 多项式 0xEDB88320"""
    crc = 0xFFFFFFFF
    for b in data:
        crc = (crc >> 8) ^ CRC32_TABLE[(crc ^ b) & 0xFF]
    return ~crc & 0xFFFFFFFF
```

---

## 7. 版本号编码

工具从 `.bin` 文件名或 `user_app.h` 中的 `FW_VERSION` 提取版本号, 编码为 `uint32_t`:

```
"V1.0.3" → 0x00010003
"V2.15.0" → 0x00020F00
```

若无法从文件名提取, 使用默认值 `0x00000001` 或通过命令行参数 `--version` 指定。

---

## 8. 错误处理

| 错误 | 处理 |
|------|------|
| 串口打开失败 | 列出可用串口, 提示用户 |
| OTA_START → NAK | 显示 NAK 错误码含义, 退出 |
| OTA_DATA → 超时 | 重试 3 次, 仍失败则 ABORT |
| OTA_DATA → NAK | 显示错误, 发送 ABORT → 退出 |
| OTA_FINISH → NAK (CRC 错) | 提示固件可能损坏, 退出 |
| 用户 Ctrl+C | 发送 ABORT → 关闭串口 → 退出 |

---

## 9. 输出示例

```
$ python ota_tool.py COM3 app_slot_a.bin

STM32F407 OTA Tool V1.0
========================
Firmware: app_slot_a.bin
Size:     58100 bytes (56.7 KB)
CRC32:    0xA3F2C81D
Version:  V1.0.3 → 0x00010003
Target:   Slot A (auto-detected)

Opening COM3 (115200 8N1)...
[OK] Serial port opened

Triggering bootloader via CMD_OTA_RESERVED...
[OK] Device entered bootloader mode

Sending OTA_START... [OK]

Uploading firmware:
[████████████████████████████████] 100%  58100/58100 bytes

Sending OTA_FINISH...
[OK] CRC32 verified, firmware activated!

Device will now boot from Slot A.
```

---

## 10. 依赖项 (requirements.txt)

```
pyserial>=3.5
tqdm>=4.64  # 可选: 进度条
```

---

## 11. 打包为独立 .exe (Windows)

```bash
pip install pyinstaller
cd tools/ota_tool
pyinstaller --onefile --name ota_tool ota_tool.py
```

生成 `dist/ota_tool.exe`, 可在无 Python 环境的 Windows 机器上运行。
