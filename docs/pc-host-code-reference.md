# Windows 上位机代码参考文档 (v3.1)

> 本文档梳理 `pc_host/` 目录下全部源文件的架构、类职责、接口定义与数据流向 (v3.2)。  
> 配合阅读需求文档、协议文档与 STM32 端代码分层文档，可形成完整的项目知识体系。  
> **更新代码后请同步更新本文档。**

---

## 一、项目概要

| 项目 | 取值 |
|------|------|
| IDE | VS2019 (v142) |
| 语言 | C++17 |
| 字符集 | Unicode (`UNICODE` + `_UNICODE`) |
| 界面 | Win32 对话框 (`DialogBox`) — 无 MFC |
| 串口 | Win32 API (`CreateFile`/`ReadFile`/`WriteFile` + Overlapped I/O) |
| 链接库 | `comctl32.lib`, `setupapi.lib` |
| 产物 | `pc_host/oled_control/Debug/oled_control.exe` |

---

## 二、文件清单与分层

```
pc_host/oled_control/
├── resource.h                    # 控件 ID 宏定义
├── oled_control.rc               # 对话框资源 (布局用 .rc 文件定义)
│
├── protocol.h / .cpp             # 【协议层】二进制帧编解码 + CRC-8-ATM
├── serial_port.h / .cpp          # 【驱动层】Win32 串口枚举/打开/收发
├── frame_queue.h                 # 【工具层】无锁环形队列 + TxTask 结构体
├── oled_preview_gen.h            # 【数据层】字模数据 (ASCII + 中文 + 天气图标)
├── oled_preview.h / .cpp         # 【渲染层】128×64 GDI 模拟预览 + 帧缓冲
└── main.cpp                      # 【应用层】WinMain + DialogProc + 全局状态机
```

### 依赖关系

```
main.cpp
  ├── protocol.h       ← 协议编解码
  ├── serial_port.h    ← 串口通信
  ├── frame_queue.h    ← TxTask 结构体
  └── oled_preview.h   ← GDI 预览 & 帧缓冲
        └── oled_preview_gen.h  ← 字模数据
```

**无跨模块头文件依赖** — `protocol`、`serial_port`、`oled_preview`、`frame_queue` 彼此独立，仅由 `main.cpp` 组装。

---

## 三、模块详解

### 3.1 `protocol.h` / `protocol.cpp` — 二进制帧协议引擎

#### 3.1.1 职责
与 STM32 端 `protocol.c` 完全对接的二进制帧编解码，保证两端帧格式、CRC 算法、命令码定义一致。

#### 3.1.2 常量定义

| 宏/常量 | 值 | 说明 |
|---------|-----|------|
| `PROTO_SOF` | `0xA5` | 帧头 |
| `PROTO_EOF` | `0x5A` | 帧尾 |
| `PROTO_MAX_DATA` | `251` | 单帧 payload 最大字节数 |
| `PROTO_RX_TIMEOUT_MS` | `500` | 接收超时 (ms) |

#### 3.1.3 命令码全集

| 命令码 | 宏名 | 方向 | 说明 |
|--------|------|------|------|
| `0x01` | `CMD_LED_CTRL` | PC→STM | LED 控制: 0=关, 1=开, 2=闪烁 |
| `0x02` | `CMD_DISPLAY_MODE` | PC→STM | 模式切换: bit7=远程, 低7位=子模式 |
| `0x03` | `CMD_TEXT_CONTENT` | PC→STM | UTF-8 文字内容 |
| `0x04` | `CMD_TIME_SYNC` | PC→STM | 时间同步 (7 字节) |
| `0x05` | `CMD_WEATHER_DATA` | PC→STM | 天气数据 (4 字节) |
| `0x06` | `CMD_BOOT_TEXT` | PC→STM | 上电默认文字 (写 Flash) |
| `0x10` | `CMD_LED_STATUS` | STM→PC | LED 状态上报 |
| `0x11` | `CMD_MODE_STATUS` | STM→PC | 模式上报: `[is_remote][sub_mode]` |
| `0x12` | `CMD_KEY_EVENT` | STM→PC | 按键事件: `[key_id][event_type]` |
| `0x20` | `CMD_FRAME_SYNC` | PC→STM | 帧缓冲分段下发 (仅远程模式) |
| `0xF0` | `CMD_ACK` | STM→PC | 应答确认 |
| `0xFF` | `CMD_NAK` | STM→PC | 应答否定 + 错误码 |

#### 3.1.4 核心类型

```cpp
struct ProtoFrame {
    uint8_t cmd;                          // 命令码
    uint8_t seq;                          // 帧序号
    uint8_t len;                          // payload 长度 (0~251)
    uint8_t data[PROTO_MAX_DATA];         // payload 数据
};
```

#### 3.1.5 `ProtocolParser` 类

```
ProtocolParser
├── FeedByte(uint8_t byte) → bool     // 逐字节喂入, 返回 true=帧完成
├── GetFrame() → const ProtoFrame&    // 获取最新完成的帧
├── ResetRx()                          // 复位接收状态机
├── CheckTimeout(uint64_t ms) → bool  // 超时复位 (500ms)
├── BuildFrame(cmd,seq,data,len) → vector<uint8_t>  // 静态: 构建发送帧
└── CRC8(data, len) → uint8_t          // 静态: CRC-8-ATM 查表计算
```

**接收状态机** (7 状态):  
`WAIT_SOF → WAIT_LEN → WAIT_CMD → WAIT_SEQ → WAIT_DATA → WAIT_CRC → WAIT_EOF → (完成)`

**CRC-8-ATM**: 多项式 `0x07`, 初始值 `0x00`, 查表法。CRC 覆盖 SOF ~ DATA (不含 CRC 和 EOF 自身)。

**BuildFrame 构建流程**:
1. `push_back(SOF)` → `push_back(len)` → `push_back(cmd)` → `push_back(seq)`
2. `insert(data...)` 如 len>0
3. `CRC8(全量)` → `push_back(crc)` → `push_back(EOF)`

---

### 3.2 `serial_port.h` / `serial_port.cpp` — 串口驱动

#### 3.2.1 职责
封装 Win32 串口 API，提供枚举、打开、收发、错误回调。

#### 3.2.2 `SerialPort` 类

```
SerialPort
├── 静态 Enumerate() → vector<ComInfo>   // 枚举 COM1~COM16
├── Open(comNumber, baudRate) → bool     // 打开串口
├── Close()                               // 关闭 + 等待接收线程退出
├── IsOpen() → bool
├── Send(data, len) → int                // 同步发送
├── onDataReceived(data, len)             // 回调: 收到数据
├── onError(msg)                          // 回调: 串口错误
└── GetComNumber() → int
```

#### 3.2.3 枚举逻辑 — `Enumerate()`

```
1. SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS) 获取设备信息集
2. for COM1~COM16:
     CreateFile("\\\\.\\COM%d") 探测端口是否存在
     若存在 → SetupDiEnumDeviceInfo 查 SPDRP_FRIENDLYNAME
     结果格式: "COM3 - USB-SERIAL CH340 (COM3)"
3. SetupDiDestroyDeviceInfoList 释放
```

#### 3.2.4 串口参数 — `Open()`

| 参数 | 值 |
|------|-----|
| 波特率 | `CBR_115200` (默认) |
| 数据位 | 8 |
| 校验 | `NOPARITY` |
| 停止位 | `ONESTOPBIT` |
| DTR | `DTR_CONTROL_ENABLE` |
| RTS | `RTS_CONTROL_ENABLE` |
| 读超时 | `ReadIntervalTimeout=50ms`, `ReadTotalTimeoutConstant=100ms` |
| 写超时 | `WriteTotalTimeoutConstant=100ms` |
| 打开标志 | `FILE_FLAG_OVERLAPPED` (异步 I/O) |

#### 3.2.5 线程模型

```
主线程 (UI)                   接收线程 (RecvThreadProc)
    │                              │
    ├─ SerialPort::Open()          │
    │   └─ CreateThread() ────────→ 进入 RecvLoop()
    │                              │
    │   定时器 WM_TIMER            ├─ while(m_running):
    │   └─ CheckRetransmit()       │   ReadFile(OVERLAPPED) ← 100ms 超时
    │   └─ SendFrameBuffer()       │   有数据 → onDataReceived(buf, len)
    │   └─ g_parser.CheckTimeout() │   出错   → onError(msg)
    │                              │
    ├─ SerialPort::Close()         │
    │   └─ m_running = false ─────→ 线程退出, WaitForSingleObject(2s)
```

- **发送**: 同步 `WriteFile` + Overlapped (数据量极小，不阻塞 UI)
- **接收**: 独立线程 + Overlapped `ReadFile`, 通过 `std::function` 回调通知主线程处理
- **线程安全**: 回调在接收线程中执行，但 `ProcessFrame()` 仅操作 UI 无关数据 (日志队列)，UI 更新通过 `SetWindowText` 等线程安全的 API

---

### 3.3 `frame_queue.h` — 无锁环形队列

#### 3.3.1 职责
提供线程安全的环形队列模板，用于解耦。当前项目中实际使用 `std::deque<TxTask>` 而非此队列，但保留以备扩展。

#### 3.3.2 `LockFreeQueue<T, Capacity>`

```cpp
template<typename T, size_t Capacity>
class LockFreeQueue {
    bool Push(const T& item);   // 入队, 满返回 false
    bool Pop(T& item);          // 出队, 空返回 false
    bool IsEmpty() const;
    // 实现: atomic<size_t> head/tail + array<T, Capacity>
};
```

采用单生产者单消费者 (SPSC) 模型, `memory_order_release/acquire` 保证可见性。

#### 3.3.3 `TxTask` 结构体

```cpp
struct TxTask {
    vector<uint8_t> frame;     // 完整帧 (SOF~EOF)
    uint8_t  cmd;              // 命令码
    uint8_t  seq;              // 帧序号
    uint64_t sendTimeMs;       // 发送时间戳 (GetTickCount64)
    int      retries;          // 已重试次数
};
```

---

### 3.4 `oled_preview_gen.h` — 字模数据

#### 3.4.1 职责
提供 ASCII 8×16 与中文字模的 C++ 常量数组，与 STM32 端 `font.c` 数据完全一致。

#### 3.4.2 接口

```cpp
const uint8_t* get_ascii_glyph(char c);       // 获取 ASCII 字符 8×16 字形
const uint8_t* get_chinese_glyph(const char* utf8_3bytes);  // 获取中文 16×16 字形
```

- ASCII: 95 个可打印字符 (0x20~0x7E), 每个 16 字节 (8×16/8)
- 中文: 316 个汉字, 每个 32 字节 (16×16/8)
- 天气图标: `weather_icons_8x8[7][8]` 8×8 点阵 (晴/多云/阴/小雨/大雨/雷雨/雪)
- 中文字形格式: STM32 交错格式 `[col0_upper][col0_lower][col1_upper][col1_lower]...`

> **注意**: 此文件由工具 `tools/gen_font.py` 从 STM32 `font.c` 自动生成，**不要手动编辑**。

---

### 3.5 `oled_preview.h` / `oled_preview.cpp` — GDI 模拟预览

#### 3.5.1 职责
在 PC 端用 GDI 渲染 128×64 OLED 模拟预览，支持本地/远程双模式，承担全部渲染 + 帧缓冲生成职责。

#### 3.5.2 `OledPreview` 类 — 完整接口

```
OledPreview
├── 注册
│   └── static RegisterClass(HINSTANCE)       // 注册窗口类 "OledPreviewWnd"
├── 生命周期
│   ├── Attach(HWND)                           // 绑定窗口, 创建内存 DC + 位图
│   └── GetHwnd() → HWND
├── 状态设置 (触发重新渲染)
│   ├── SetText(const char* utf8)              // 设置文字内容
│   ├── SetMode(int mode)                      // 0~6 特效模式
│   ├── SetTime(h,m,s,y,mo,d,wd)              // 设置时钟/日期数据
│   ├── SetWeather(type,temp,hum,wind)         // 设置天气数据
│   ├── SetLedState(int state)                 // LED 状态 (0/1/2)
│   ├── SetRemote(bool remote)                 // 本地/远程模式
│   └── SetSubMode(int sm)                     // 远程子模式: 0=TEXT,1=TIME,2=WEATHER,3=DATE
├── 查询
│   ├── IsRemote() → bool
│   ├── GetSubMode() → int
│   └── GetFrameBuffer() → const uint8_t*     // 返回 1024 字节帧缓冲
├── 动画
│   └── Tick()                                 // 50ms 定时驱动
└── 内部渲染
    ├── Render()                               // 入口: 按 isRemote 分发
    ├── RenderLocal()                          // 本地: 文字+特效 → GDI
    ├── RenderRemote()                         // 远程: 子模式渲染 → 帧缓冲 + GDI
    ├── RenderTimeMode()                       // 远程 TIME: 大字时钟+日期
    ├── RenderWeatherMode()                    // 远程 WEATHER: 图标+温湿度+风向
    └── RenderDateMode()                       // 远程 DATE: 年月日+星期
```

#### 3.5.3 渲染管线

```
SetXxx() 调用                     WM_TIMER(50ms)
    │                                  │
    └→ Render()                        └→ Tick() → 动画参数更新 → Render()
         │
         ├─ isRemote == false ──→ RenderLocal()
         │   ├─ 清屏 (BG_COLOR=RGB(32,32,32))
         │   ├─ 特效处理 (mode 0~6):
         │   │   0: DrawStrWrap (静态+自动换行)
         │   │   1-2: DrawStr + scrollX 偏移 (左右滚)
         │   │   3-4: DrawStr + scrollY 偏移 (上下滚)
         │   │   5: DrawStrWrap + flipPhase 位移 (翻页)
         │   │   6: DrawStrWrap + fadeStep 亮度 (淡入淡出)
         │   └─ GDI SetPixel → InvalidateRect
         │
         └─ isRemote == true ──→ RenderRemote()
              ├─ 按 subMode 分发:
              │   0: RenderLocal() → GDI → GetPixel 同步到 m_frameBuf
              │   1: RenderTimeMode() → GDI → m_frameBuf
              │   2: RenderWeatherMode() → GDI → m_frameBuf
              │   3: RenderDateMode() → GDI → m_frameBuf
              └─ m_frameBuf → GDI SetPixel 显示
```

#### 3.5.4 动画 Tick 参数

| 特效 | 触发间隔 | 步进量 | 说明 |
|------|---------|--------|------|
| 左右滚 | 40ms (~25fps) | scrollX += 2 | 溢出后复位 |
| 上下滚 | 40ms (~25fps) | scrollY += 2 | 溢出后复位 |
| 翻页 | 3000ms | flipPhase ^= 1 | 两相切换 |
| 淡入淡出 | 30ms | fadeStep ±1 | 0↔32 循环, 线性插值颜色 |

#### 3.5.5 帧缓冲格式

- 大小: 1024 字节 (128×64÷8)
- 排序: **SSD1306 page 格式** — `m_frameBuf[page][col]`, page=0~7
  - 字节 `m_frameBuf[page*128 + col]` 的 bit0~bit7 对应 `(col, page*8+0)` ~ `(col, page*8+7)`
- `SetPixelToBuf(x, y, on)`: `page = y>>3`, `bit = 1<<(y&7)`

#### 3.5.6 GDI 实现细节

- 内存 DC: `CreateCompatibleDC` + `CreateCompatibleBitmap(128, 64)`
- 像素色: 深灰底 `RGB(32,32,32)` / 亮黄绿 `RGB(180,255,80)`
- 显示: `WM_PAINT` → `StretchBlt` 4x 放大 (由 WndProc 处理)
- 中文字形: 从 `oled_preview_gen.h` 获取, 按 STM32 交错格式解码

---

### 3.6 `main.cpp` — 应用入口与 UI 逻辑

#### 3.6.1 全局状态

```cpp
static SerialPort       g_serial;         // 串口实例
static ProtocolParser   g_parser;         // 协议解析器
static deque<TxTask>    g_pendingAcks;    // 待确认队列 (FIFO)
static uint8_t          g_seq;            // 发送帧序号 (递增)
static bool             g_connected;      // 串口连接标志
static bool             g_userIsRemote;   // 用户 UI 意图 (防竞态)
static bool             g_userIsRemote;   // 用户 UI 意图 (防竞态)
static OledPreview      g_preview;        // OLED 预览实例
```

#### 3.6.2 核心函数

| 函数 | 职责 |
|------|------|
| `SendCommand(cmd, data, len)` | 构建帧 → 发送 → 加入 `g_pendingAcks` 待确认队列 |
| `CheckRetransmit()` | 遍历 `g_pendingAcks`, 500ms 超时重传, 最多 3 次 |
| `ProcessFrame(frame)` | 根据 `cmd` 分发: ACK 消队列, LED_STATUS/MODE_STATUS/KEY_EVENT 更新 UI |
| `OnSerialData(data, len)` | 串口接收回调: 逐字节喂入 `g_parser.FeedByte()`, 完成帧调用 `ProcessFrame()` |
| `OnSerialError(msg)` | 串口错误回调: 关闭串口, 更新 UI 状态 |
| `SendFrameBuffer()` | 远程模式帧缓冲分段发送 (6 段 × 200B) |
| `SyncTime()` | 获取系统时间 → `CMD_TIME_SYNC` + 更新预览 |
| `PopulateComPorts()` | 枚举串口填充下拉列表 |
| `DlgProc()` | 主对话框消息处理 |

#### 3.6.3 消息处理流程 — `DlgProc`

```
WM_INITDIALOG
  ├─ 绑定所有控件句柄
  ├─ 初始化控件默认值 (本地模式、LED 关、特效 0)
  ├─ 初始化天气/风向/远程子模式下拉
  ├─ PopulateComPorts()
  ├─ g_preview.Attach()
  ├─ LayoutMainDialog()    ← 计算控件位置
  ├─ 注册串口回调
  └─ SetTimer(1, 50ms)

WM_COMMAND
  ├─ IDC_BTN_OPEN        → 打开串口 + SyncTime()
  ├─ IDC_BTN_CLOSE       → 关闭串口 + 清空待确认队列
  ├─ IDC_RADIO_LED_*     → CMD_LED_CTRL
  ├─ IDC_BTN_SEND_TEXT   → CMD_TEXT_CONTENT + 更新预览
  ├─ IDC_BTN_SAVE_BOOT   → CMD_BOOT_TEXT
  ├─ IDC_BTN_SEND_WEATHER → CMD_WEATHER_DATA + 更新预览
  ├─ IDC_BTN_SYNC_TIME   → SyncTime()
  ├─ IDC_RADIO_LOCAL/REMOTE → CMD_DISPLAY_MODE (bit7 标记远程)
  ├─ IDC_CBO_REMOTE_SUB  → CMD_DISPLAY_MODE + 更新预览
  ├─ IDC_RADIO_MODE_BASE* → CMD_DISPLAY_MODE + 更新预览特效
  ├─ IDC_EDIT_TEXT (EN_CHANGE) → UpdatePreviewText()
  └─ IDC_EDIT_TEMP/HUMIDITY, CBO_WEATHER/WIND → UpdatePreviewWeather()

WM_TIMER (wParam=1) 每 50ms:
  ├─ g_preview.Tick()         ← 动画驱动
  ├─ SendFrameBuffer()        ← 远程模式帧缓冲发送
  ├─ CheckRetransmit()        ← 超时重传检查
  └─ g_parser.CheckTimeout()  ← 协议超时复位

WM_SIZE → LayoutMainDialog()  ← 响应式布局
WM_CLOSE → KillTimer + Close + EndDialog
```

#### 3.6.4 发送确认与重传机制

```
SendCommand()
  │
  ├─ ProtocolParser::BuildFrame(cmd, seq, data, len)
  ├─ g_serial.Send(frame)
  └─ g_pendingAcks.push_back({frame, cmd, seq, now, 0})
       │
       ▼
CheckRetransmit()  ← 每 50ms 调用
  │
  └─ while front 超时:
       ├─ retries < 3 → 重发, retries++, 更新 sendTimeMs
       └─ retries >= 3 → 日志告警, 丢弃该任务

ProcessFrame(ACK)
  │
  └─ 按 seq 匹配 → g_pendingAcks.erase(it)
```

#### 3.6.5 帧缓冲分段发送 — `SendFrameBuffer()`

```
仅在 g_connected && g_preview.IsRemote() 时执行

帧率策略:
  TEXT 模式: 40ms (25fps)
  TIME/WEATHER/DATE: 1000ms (1fps)

分段:
  payload = 200 字节/段, total = 6 段 (1024 / 200 向上取整)
  每段数据: [seg(1B)][total(1B)][frame_data(≤200B)]
  段间 Sleep(2ms) 防止 STM32 RX 溢出
```

#### 3.6.6 布局计算 — `LayoutMainDialog()`

使用 `MapDialogRect` 将 DLU (对话单位) 转换为像素，响应式调整右侧面板 (OLED 预览、设备状态、按键日志) 的大小和位置。最小窗口尺寸: 620×560 DLU。

---

## 四、数据流全景

```
┌──────────────────────────────────────────────────────────────────┐
│  main.cpp (UI 线程, 50ms 定时器)                                   │
│                                                                   │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌───────────────┐ │
│  │ 控件事件  │   │ 帧缓冲   │   │ 动画 Tick │   │ 重传检查      │ │
│  │ WM_CMD   │   │ 发送     │   │           │   │               │ │
│  └────┬─────┘   └────┬─────┘   └────┬─────┘   └────┬──────────┘ │
│       │              │              │              │             │
│       ▼              │              ▼              │             │
│  ┌──────────┐        │         ┌──────────┐        │             │
│  │SendCmd() │        │         │ g_preview│        │             │
│  │BuildFrame│        │         │ .Tick()  │        │             │
│  └────┬─────┘        │         │ .Render()│        │             │
│       │              │         └────┬─────┘        │             │
│       ▼              │              │              │             │
│  ┌──────────┐        │         ┌──────────┐        │             │
│  │g_serial  │        │         │g_preview │        │             │
│  │.Send()   │        │         │.GetFrame │        │             │
│  └────┬─────┘        │         │Buffer()  │        │             │
│       │              │         └────┬─────┘        │             │
│       │              │              │              │             │
│       │   ┌──────────┘              │              │             │
│       │   │                         │              │             │
│       ▼   ▼                         ▼              ▼             │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │               g_pendingAcks (deque<TxTask>)               │   │
│  │  入队: SendCommand() / SendFrameBuffer()                   │   │
│  │  出队: ACK 匹配 或 超时 3 次丢弃                           │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                   │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │        g_parser (ProtocolParser 状态机)                    │   │
│  │  FeedByte() ← OnSerialData()                              │   │
│  │  完成 → ProcessFrame(frame)                                │   │
│  │    ├─ ACK   → g_pendingAcks.erase()                        │   │
│  │    ├─ LED_STATUS → SetWindowText + g_preview.SetLedState() │   │
│  │    ├─ MODE_STATUS → 更新状态栏 (不覆盖用户意图)             │   │
│  │    └─ KEY_EVENT → AppendLog()                              │   │
│  └──────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
         │                              ▲
         │ SerialPort::Send()           │ onDataReceived 回调
         ▼                              │
┌──────────────────────────────────────────────────────────────────┐
│  SerialPort (串口驱动)                                            │
│                                                                   │
│  主线程: Send() = WriteFile(OVERLAPPED)                           │
│  接收线程: RecvLoop() → ReadFile(OVERLAPPED) → onDataReceived()   │
└──────────────────────────────────────────────────────────────────┘
         │                              ▲
         │ UART TX                     │ UART RX
         ▼                              │
    ═══════════ USB-TTL ══════════════════
         │                              ▲
         ▼                              │
    ┌─────────────────────────────────────────┐
    │  STM32F407 (USART1 PA9/PA10, 115200/8N1) │
    └─────────────────────────────────────────┘
```

---

## 五、控件 ID 速查表

| 控件 | ID | 类型 |
|------|-----|------|
| 串口下拉 | `IDC_COMBO_PORT` (1001) | ComboBox |
| 打开/关闭 | `IDC_BTN_OPEN` (1002) / `IDC_BTN_CLOSE` (1003) | Button |
| 连接状态 | `IDC_STC_CONN_STATUS` (1004) | Static |
| 远程/本地 | `IDC_RADIO_REMOTE` (1005) / `IDC_RADIO_LOCAL` (1006) | Radio |
| 远程子模式 | `IDC_CBO_REMOTE_SUB` (1007) | ComboBox |
| LED 关/开/闪烁 | `IDC_RADIO_LED_OFF/ON/BLINK` (1010-1012) | Radio |
| 特效模式 | `IDC_RADIO_MODE_BASE` (1020) + 0~6 | Radio |
| 文字编辑 | `IDC_EDIT_TEXT` (1030) | Edit |
| 发送文字 | `IDC_BTN_SEND_TEXT` (1031) | Button |
| 启动文字 | `IDC_EDIT_BOOT_TEXT` (1032) | Edit |
| 保存启动文字 | `IDC_BTN_SAVE_BOOT` (1033) | Button |
| 天气类型/温度/湿度/风向 | 1040-1043 | Combo+Edit |
| 发送天气 | `IDC_BTN_SEND_WEATHER` (1044) | Button |
| 同步时间 | `IDC_BTN_SYNC_TIME` (1050) | Button |
| LED 状态/模式状态 | `IDC_STC_LED_STATUS` (1060) / `IDC_STC_MODE_STATUS` (1061) | Static |
| 设备状态组/日志组 | 1063 / 1071 | Group Box |
| 按键日志列表 | `IDC_LST_KEY_LOG` (1070) | ListBox |
| OLED 预览 | `IDC_OLED_PREVIEW` (1080) | Custom ("OledPreviewWnd") |
| 远程子模式标签 | IDC_STC_REMOTE_MODE (1085) | Static |
| 天气分组框 | IDC_GRP_WEATHER (1090) | Group Box |
| 底部状态栏 | IDC_STATUS_BAR (1099) | StatusBar (v3.2) |
| 远程子模式标签 | `IDC_STC_REMOTE_MODE` (1085) | Static |

---

## 六、常见修改指南

### 6.1 新增命令码

1. 在 `protocol.h` 中同时添加 PC 端和 STM32 端 `protocol.h` 的命令码宏
2. 在 `main.cpp` 的 `ProcessFrame()` 中添加 `case CMD_XXX:` 处理
3. 如需发送，在对应控件事件中调用 `SendCommand(CMD_XXX, data, len)`

### 6.2 新增远程子模式

1. `oled_preview.h`: 在 `OledPreview` 中添加 `RenderXxxMode()` 私有方法声明
2. `oled_preview.cpp`: 实现渲染方法 (GDI → m_frameBuf)
3. `oled_preview.cpp`: `RenderRemote()` 的 `switch(m_subMode)` 中添加新 case
4. `main.cpp`: `g_remoteSubNames[]` 数组追加名称
5. `resource.h` + `oled_control.rc`: 如需要新 UI 控件则添加

### 6.3 调整帧缓冲分段大小

修改 `main.cpp` 中 `SendFrameBuffer()` 的 `PAYLOAD` 常量 (当前 200)。注意 STM32 端 `protocol.h` 的 `PROTO_MAX_DATA` (251) 是上限。

### 6.4 添加新字模

1. 修改 `stm32f407/src/font.c` 添加新字形
2. 运行 `tools/gen_font.py` 重新生成 `pc_host/oled_control/oled_preview_gen.h`

---

## 七、编译

```
# VS2019 IDE
打开 pc_host/oled_control.sln → 生成 → Debug/Release + Win32/x64

# 命令行
MSBuild pc_host/oled_control.sln /t:Build /p:Configuration=Debug /p:Platform=Win32
```

产物路径: `pc_host/oled_control/Debug/oled_control.exe` (或 `x64/Debug/`)
