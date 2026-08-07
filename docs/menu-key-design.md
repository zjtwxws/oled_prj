# 菜单与按键处理逻辑 — 详细方案设计

## 1. 概述

本文档梳理 STM32F407 固件中菜单系统（`menu_mgr` / `menu_items`）与按键驱动（`key_drv`）的完整设计，涵盖架构分层、数据结构、状态机、事件流与渲染管线。

### 1.1 涉及文件

| 模块 | 头文件 | 实现文件 | 所属层次 |
|------|--------|----------|----------|
| 按键驱动 | `inc/key_drv.h` | `src/key_drv.c` | 硬件驱动层 |
| 菜单管理器 | `inc/menu_mgr.h` | `src/menu_mgr.c` | 应用模块层 |
| 菜单项定义 | —（接口通过 `menu_mgr.h`） | `src/menu_items.c` | 应用模块层（数据） |
| 应用入口 | `inc/user_app.h` | `src/user_app.c` | 应用入口层 |
| 显示管理器 | `inc/display_mgr.h` | `src/display_mgr.c` | 应用模块层 |
| SSD1306 驱动 | `inc/ssd1306.h` | `src/ssd1306.c` | 硬件驱动层 |
| 字库 | `inc/font.h` | `src/font.c` | 中间件层 |

### 1.2 分层关系

```
user_app (编排层)
  ├─ 按键分发: key_drv → menu_mgr / display_mgr / led_mgr
  ├─ 显示调度: menu_mgr (菜单激活时) 或 display_mgr (正常运行时)
  └─ 协议通信: protocol → uart_drv → PC 上位机 (USART1)

menu_mgr (菜单引擎)
  ├─ 依赖: ssd1306 (显存操作), font (字模渲染)
  ├─ 引用: menu_items_get_root() (菜单数据)
  └─ 调用: display_mgr_set_menu_suppress() (抑制冲突)
```

---

## 2. 按键驱动设计 (key_drv)

### 2.1 硬件接线

| 按键 | GPIO 引脚 | 模式 | 按下电平 |
|------|-----------|------|----------|
| KEY1 | PE1 | 上拉输入 | 低电平 (0) |
| KEY2 | PE2 | 上拉输入 | 低电平 (0) |
| KEY3 | PE3 | 上拉输入 | 低电平 (0) |
| KEY4 | PE4 | 上拉输入 | 低电平 (0) |

按键数量通过 `KEY_COUNT` 宏控制（当前为 4），编译期可裁剪。

### 2.2 消抖机制

```
          raw 采样 (每 20ms 一次)
              │
    ┌─ 与上次相同? ──否──→ debounce_cnt = 0（复位计数器）
    │         │
    │        是
    │         │
    │    debounce_cnt++
    │         │
    │    cnt >= 3 ?  ──否──→ 继续等待
    │         │
    │        是 (连续 3 次相同 = 60ms 消抖完成)
    │         │
    │    raw != stable_state ?
    │         │
    │    ┌────┴────┐
    │  是          否
    │    │          └──→ 无变化
    │ 更新 stable_state
    │    │
    │ raw==0 (按下)       raw==1 (释放)
    │    │                    │
    │ 记录 press_start_tick   │
    │ long_press_fired=0      ├─ 之前是长按? → 产生 RELEASE 事件
    │    │                    └─ 之前是短按? → 产生 SHORT_PRESS 事件
    └────┘
```

**关键参数：**

| 参数 | 值 | 说明 |
|------|-----|------|
| `KEY_DEBOUNCE_MS` | 20ms | 采样间隔 |
| `DEBOUNCE_SAMPLES` | 3 | 连续确认次数 |
| 消抖总时长 | 60ms | 3 × 20ms |
| `KEY_LONG_PRESS_MS` | 2000ms | 长按判定阈值 |
| `KEY_LONG_REPEAT_MS` | 150ms | 长按后连发间隔 |

### 2.3 事件类型

```c
typedef enum {
    KEY_EVENT_NONE             = 0,  // 无事件
    KEY_EVENT_SHORT_PRESS      = 1,  // 短按（按下→释放 < 2s）
    KEY_EVENT_LONG_PRESS       = 2,  // 长按（按下持续 ≥ 2s，仅触发一次）
    KEY_EVENT_RELEASE          = 3,  // 长按后的释放
    KEY_EVENT_LONG_PRESS_REPEAT = 4  // 长按连发（长按触发后每 150ms 一次）
} key_event_t;
```

### 2.4 长按检测（基于真实时间）

```c
// 每轮扫描用 HAL_GetTick() 计算真实时间差
if (stable_state == 0 && !long_press_fired) {
    if (now - press_start_tick >= KEY_LONG_PRESS_MS) {
        long_press_fired = 1;
        // 产生 KEY_EVENT_LONG_PRESS
    }
}

// 长按已触发后，周期性产生 REPEAT
if (stable_state == 0 && long_press_fired) {
    if (now - last_repeat_tick >= KEY_LONG_REPEAT_MS) {
        // 产生 KEY_EVENT_LONG_PRESS_REPEAT
    }
}
```

**设计要点：** 使用 `HAL_GetTick()` 而非扫描轮次计数，确保长按检测不受主循环负载波动影响。

### 2.5 事件返回机制

`key_drv_scan()` 每次调用只返回 **一个** 事件。优先返回已缓存的待处理事件（`event_pending`），无缓存时才扫描 GPIO。这确保了上层不会遗漏事件。

---

## 3. 菜单项数据结构 (menu_items)

### 3.1 菜单项类型与联合体

```c
typedef enum {
    MENU_TYPE_SUBMENU,  // 进入子菜单
    MENU_TYPE_TOGGLE,   // ON/OFF 开关
    MENU_TYPE_VALUE,    // 数值调节
    MENU_TYPE_ACTION,   // 执行回调
    MENU_TYPE_INFO,     // 纯信息显示
} menu_item_type_t;

typedef struct menu_item {
    const char *text;          // 显示文本 (UTF-8)
    menu_item_type_t type;
    union {
        // SUBMENU: 子菜单数组指针 + 项数
        struct { const struct menu_item **items; uint8_t count; } submenu;

        // TOGGLE: 指向共享状态变量, checked_value 决定何时显示 [x]
        struct { uint8_t *value_ptr; uint8_t checked_value;
                 void (*on_change)(uint8_t); } toggle;

        // VALUE: 数值范围 + 步长 + 变化回调
        struct { uint8_t *value_ptr; uint8_t min, max, step;
                 void (*on_change)(uint8_t); } value;

        // ACTION: 回调函数指针
        void (*action)(void);

        // INFO: 详情文本 (全屏显示)
        struct { const char *detail_text; } info;
    };
} menu_item_t;
```

**存储策略：** 全部 `static const`，存放于 Flash（`.rodata` 段），RAM 占用极小。

### 3.2 菜单树结构

```
root (虚拟 SUBMENU)
├── 1.工作模式 → [本地] [远程]                          ← TOGGLE (共享 g_remote_mode)
├── 2.显示内容 → 时间 / 天气 / 日期 / 自定义文字           ← ACTION ×4
├── 3.显示特效 → 静态/左滚/右滚/上滚/下滚/翻页/淡入淡出    ← ACTION ×7
├── 4.设置 → 对比度设置 → 对比度 [000~255]              ← SUBMENU → VALUE
├── 5.LED控制 → 关闭 / 常亮 / 闪烁                       ← ACTION ×3
├── 6.上电文字                                           ← INFO (预留)
├── 7.系统信息 → 固件版本 / 运行时间                       ← SUBMENU → INFO ×2
└── 8.预留 → 三级示例 → 选项A / 选项B / 选项C             ← SUBMENU → ACTION ×3
```

**最大深度：** `MENU_MAX_DEPTH = 8`

### 3.3 状态变量与回调桥接

菜单不直接操作硬件，而是通过回调函数桥接到现有系统模块：

| 菜单项 | 操作变量 | 回调函数 | 桥接目标 |
|--------|----------|----------|----------|
| 工作模式 (TOGGLE) | `g_remote_mode` | `cb_mode_changed()` | `display_mgr_set_remote()` |
| 对比度 (VALUE) | `g_contrast` | `cb_contrast_changed()` | `ssd1306_set_contrast()` |
| 显示内容 (ACTION) | — | `cb_disp_time()` 等 | `display_mgr_set_sub_mode()` |
| 显示特效 (ACTION) | — | `cb_effect_static()` 等 | `display_mgr_set_mode()` |
| LED控制 (ACTION) | — | `cb_led_off()` 等 | `led_mgr_set_state()` |

### 3.4 状态同步

`menu_items_init_state()` 在菜单激活时被调用，将外部系统状态同步回菜单变量：

```c
void menu_items_init_state(void) {
    g_remote_mode = display_mgr_is_remote() ? 1 : 0;
    // g_contrast 保持当前值不变，不硬编码重置
}
```

---

## 4. 菜单引擎设计 (menu_mgr)

### 4.1 内部状态机

```c
typedef struct {
    const menu_item_t **current_menu;    // 当前层菜单项数组
    uint8_t            current_menu_count;
    uint8_t            cursor;           // 当前选中项索引
    uint8_t            scroll_offset;    // 可视区起始偏移
    menu_context_t     stack[MENU_MAX_DEPTH];  // 导航栈 (每层: cursor + scroll_offset)
    uint8_t            depth;            // 当前深度
    bool               active;           // 菜单是否激活
    bool               dirty;            // 是否需要重绘
    bool               value_editing;    // 是否在 VALUE 编辑模式
    uint8_t            value_backup;     // 编辑前备份值 (KEY4 取消时恢复)
    bool               info_showing;     // 是否在 INFO 详情页
    const char        *info_detail;      // INFO 详情文本
} menu_state_t;
```

**状态子模式：**

```
                    ┌─────────────┐
          KEY4 长按  │   INACTIVE   │  KEY4 短按(根层) / KEY4长按(根层)
        ┌───────────│  (正常显示)   │◄──────────────────────────┐
        │            └──────┬──────┘                            │
        │                   │ KEY4 长按                         │
        │                   ▼                                   │
        │            ┌─────────────┐                            │
        │            │   ACTIVE    │                            │
        │            │ (菜单导航)   │                            │
        │            └──────┬──────┘                            │
        │                   │                                   │
        │     ┌─────────────┼─────────────┐                     │
        │     │ KEY3        │ KEY3        │ KEY3                │
        │     ▼             ▼             ▼                     │
        │ ┌────────┐  ┌───────────┐  ┌──────────┐              │
        │ │  INFO  │  │  VALUE    │  │ SUBMENU  │──────────────┘
        │ │ 详情页  │  │  编辑模式  │  │ (深度+1) │   KEY4 返回
        │ └───┬────┘  └─────┬─────┘  └──────────┘
        │     │ 任意键       │ KEY3(确认)
        │     └──────────────┤ KEY4(取消恢复)
        │                    │
        └────────────────────┘
```

### 4.2 按键映射

#### 4.2.1 正常导航模式

| 按键 | 短按 | 长按 | 长按连发 |
|------|------|------|----------|
| KEY1 | 光标下移 | — | 连续下移 |
| KEY2 | 光标上移 | — | 连续上移 |
| KEY3 | 确认（进入子菜单/切换TOGGLE/进入VALUE编辑/执行ACTION/显示INFO） | — | — |
| KEY4 | 返回上级（根层则退出菜单） | 回到根菜单（根层则退出菜单） | — |

#### 4.2.2 VALUE 编辑模式

| 按键 | 短按 | 长按 / 长按连发 |
|------|------|-----------------|
| KEY1 | 减小 1 步长 | 快速减小（5×步长） |
| KEY2 | 增大 1 步长 | 快速增大（5×步长） |
| KEY3 | 确认当前值，退出编辑 | — |
| KEY4 | 取消，恢复备份值，退出编辑 | — |

#### 4.2.3 INFO 详情页

| 按键 | 行为 |
|------|------|
| 任意键短按 | 退出详情页，返回菜单 |

#### 4.2.4 非菜单模式（正常显示）

| 按键 | 事件 | 功能 |
|------|------|------|
| KEY1 | SHORT_PRESS | 切换显示特效 (`display_mgr_next_mode()`) |
| KEY2 | SHORT_PRESS | 切换 LED 状态 (OFF→ON→BLINK→OFF) |
| KEY4 | LONG_PRESS | 激活菜单 (`menu_mgr_activate()`) |

所有按键事件（除 LONG_PRESS_REPEAT）通过 UART 串口上报给上位机。

### 4.3 导航栈与返回机制

```
进入子菜单 (enter_submenu):
  1. stack[depth] ← {cursor, scroll_offset}  // 保存当前层状态
  2. depth++
  3. current_menu ← submenu.items
  4. cursor = 0, scroll_offset = 0

返回上级 (exit_submenu):
  1. depth--
  2. 从 root 开始，沿 stack[0..depth-1] 的 cursor 值逐层遍历
     重新定位 current_menu (因为菜单数据是 const，无法存指针)
  3. cursor ← stack[depth].cursor
  4. scroll_offset ← stack[depth].scroll_offset
  5. adjust_scroll()

返回根菜单 (goto_root): depth=0, cursor=0, 直接定位 root
```

**设计要点：** 由于菜单项全部是 `const` 指针，栈中不能保存 `current_menu` 指针，退栈时需从根节点沿栈逐层遍历重新定位。

### 4.4 滚动逻辑

OLED 只能显示 4 行，菜单项可能超过 4 项：

```c
static void adjust_scroll(void) {
    if (current_menu_count <= 4) { scroll_offset = 0; return; }

    if (cursor < scroll_offset)
        scroll_offset = cursor;             // 光标在可视区上方 → 上滚
    else if (cursor >= scroll_offset + 4)
        scroll_offset = cursor - 4 + 1;     // 光标在可视区下方 → 下滚

    // 防止滚过头
    if (scroll_offset + 4 > current_menu_count)
        scroll_offset = current_menu_count - 4;
}
```

滚动指示器（`▲` `↓`）代码已写但当前通过 `#if 0` 禁用。

### 4.5 渲染管线

```
menu_mgr_tick()  (每 ~50ms 调用)
  │
  ├─ !active ? → return
  │
  ├─ dirty ?
  │   ├─ info_showing ? → render_info()
  │   └─ else → render_menu()
  │       │
  │       ├─ memset(buffer, 0)  全清屏
  │       │
  │       ├─ for row in 0..3:
  │       │   item = current_menu[scroll_offset + row]
  │       │   │
  │       │   ├─ item_idx == cursor ?  (选中行)
  │       │   │   fill_row_white(row)  // 整行反白
  │       │   │   draw_text(">", inverted)  // 箭头
  │       │   │   draw_text(item->text, inverted)  // 文本右对齐
  │       │   │   draw_suffix(item, inverted)  // [x] / [NNN]
  │       │   │
  │       │   └─ else  (普通行)
  │       │       draw_text(item->text, normal)  // 缩进 16px
  │       │       draw_suffix(item, normal)
  │       │
  │       └─ (可选) 滚动指示器
  │
  ├─ ssd1306_update_screen()  全屏刷新到 OLED
  └─ dirty = false
```

**渲染优化：**
- 仅当 `dirty` 标志为 true 时才执行渲染（包括 OLED I²C 刷新）
- 任何按键操作都会设置 `dirty = true`
- 避免无操作的周期性无效刷新

**文本对齐策略：**
- 选中行（反白）：长文本自动左移，使文本右端贴齐屏幕右侧（右侧为 suffix 留空间）
- 普通行：默认缩进 16px，长文本同样右对齐，但不会比缩进 16px 更小

**后缀渲染：**
- TOGGLE：`[x]`（选中）/ `[ ]`（未选中），固定占 24px
- VALUE：`[NNN]`（3位数字），占 24~40px（根据数值位数）

### 4.6 中英文混排

```c
static uint8_t draw_text(page, x, text, inverted) {
    while (*p) {
        if ((first & 0xF0) == 0xE0)  // 3字节 UTF-8 中文
            draw_chinese_*(page, x, p);  p += 3;  x += 16;
        else if ((first & 0xE0) == 0xC0)  // 无效 2字节序列
            p += 2;  // 跳过
        else  // ASCII
            draw_ascii_*(page, x, *p);  p++;  x += 8;
    }
}
```

- ASCII 字符：8×16 像素，占用 1 列
- 中文字符：16×16 像素（UTF-8 3字节 → 查表 → 32字节字模），占用 2 列
- 绘制粒度：每行 2 pages（16px 高）
- 超出 `SSD1306_WIDTH (128)` 自动截断

### 4.7 INFO 详情页渲染

全屏显示 `detail_text`，从 page 0 开始逐字绘制，自动换行（每行 128px 满时换到下一个双 page）。

---

## 5. 应用层集成 (user_app)

### 5.1 初始化流程

```
user_app_init()
  ├─ i2c_drv_init()
  ├─ uart_drv_init()
  ├─ debug_console_init()
  ├─ ssd1306_init()
  ├─ sys_config_init()
  ├─ led_mgr_init()
  ├─ key_drv_init()
  ├─ iwdg_drv_init()
  ├─ display_mgr_init(boot_text)
  └─ menu_mgr_init()
       ├─ menu_items_init_state()      // 同步外部状态
       ├─ current_menu = root.submenu  // 进入根菜单
       ├─ active = true
       ├─ dirty = true
       └─ display_mgr_set_menu_suppress(true)  // 抑制 display_mgr 刷屏
```

**注意：** 上电后菜单默认激活，OLED 首先显示主菜单。这意味着开机后可通过 KEY4 退出菜单进入正常显示，或直接在菜单中操作。

### 5.2 主循环调度

```
user_app_handle()  (在 main() 的 while(1) 中每轮调用)
  │
  ├─ [每轮] 协议帧接收 + 超时检测
  ├─ [每轮] 远程模式串口断开检测 (5s 超时)
  │
  ├─ [每 20ms] 按键扫描
  │   ├─ key_drv_scan(&key_info)
  │   ├─ menu_mgr_is_active() ?
  │   │   是 → menu_mgr_handle_key(key_id, event)  // 全部交给菜单
  │   │   否 → 原有按键逻辑:
  │   │        KEY1 → display_mgr_next_mode()
  │   │        KEY2 → led_mgr 状态循环
  │   │        KEY4 长按 → menu_mgr_activate()
  │   └─ send_key_event()  (上报上位机，REPEAT 除外)
  │
  ├─ [每 50ms] LED tick
  │
  └─ [每 50ms] 显示刷新
      ├─ menu_mgr_is_active() ?
      │   是 → menu_mgr_tick()    (仅在 dirty 时实际刷新)
      │   否 → display_mgr_tick() (特效动画调度)
      └─ iwdg_drv_feed()
```

### 5.3 菜单与 display_mgr 互斥

菜单激活期间，`display_mgr` 的刷屏被抑制：

```c
// menu_mgr_activate() 中:
display_mgr_set_menu_suppress(true);

// display_mgr 中:
static void update_screen_if_allowed(void) {
    if (!menu_suppress) ssd1306_update_screen();
}

// menu_mgr_deactivate() 中:
display_mgr_redraw();  // 强制恢复显示
```

这避免了 `display_mgr` 的定时特效渲染与菜单渲染之间的冲突。

---

## 6. 事件流完整路径

### 6.1 按键 → 硬件中断 → 轮询扫描

```
GPIO 电平变化
  │
  ▼
key_drv_scan() (每 20ms, user_app 主循环调用)
  ├─ 读 GPIO 电平
  ├─ 消抖 (3次连续确认, 60ms)
  ├─ 状态变化检测
  ├─ 长按计时 (HAL_GetTick, 2s 阈值)
  └─ 返回 key_info_t {key_id, event}
```

### 6.2 按键 → 菜单操作

```
user_app_handle()
  │
  ├─ key_drv_scan() → key_info
  │
  ├─ menu_mgr_is_active() == true
  │   └─ menu_mgr_handle_key(key_id, event)
  │        │
  │        ├─ info_showing? → 任意键退出
  │        ├─ value_editing?
  │        │   ├─ KEY1/KEY2 SHORT → 增减调节
  │        │   ├─ KEY1/KEY2 LONG/REPEAT → 快速增减
  │        │   ├─ KEY3 SHORT → 确认退出
  │        │   └─ KEY4 SHORT → 取消恢复退出
  │        │
  │        └─ 正常导航
  │            ├─ KEY1 → cursor_down()
  │            ├─ KEY2 → cursor_up()
  │            ├─ KEY3 → handle_confirm()
  │            │   ├─ SUBMENU → enter_submenu()
  │            │   ├─ TOGGLE → 翻转值 + on_change()
  │            │   ├─ VALUE → value_editing=true (备份值)
  │            │   ├─ ACTION → action()
  │            │   └─ INFO → info_showing=true
  │            └─ KEY4
  │                ├─ SHORT → exit_submenu() / deactivate()
  │                └─ LONG → goto_root() / deactivate()
  │
  └─ dirty=true → 下次 menu_mgr_tick() 重绘
```

### 6.3 菜单 → 硬件效果

```
menu_mgr_handle_key()
  └─ handle_confirm() → ACTION 回调
       │
       ├─ cb_effect_static() → display_mgr_set_mode(DISP_MODE_STATIC)
       ├─ cb_led_on() → led_mgr_set_state(LED_STATE_ON)
       ├─ cb_contrast_changed(v) → ssd1306_set_contrast(v)
       └─ ...
            │
            └─ GPIO / I2C → 实际硬件操作
```

---

## 7. 设计要点总结

| 设计方面 | 要点 |
|----------|------|
| **分层解耦** | 按键驱动只产生事件，不感知菜单；菜单只处理事件和渲染，不直接读写 GPIO |
| **事件驱动** | 所有操作由按键事件触发，通过 `dirty` 标志延迟渲染，避免不必要的 OLED I²C 通信 |
| **状态同步** | 菜单激活时通过 `menu_items_init_state()` 从外部系统拉取当前状态 |
| **编辑保护** | VALUE 编辑前备份原始值，KEY4 取消时完整恢复 |
| **真实时间** | 长按检测使用 `HAL_GetTick()` 而非扫描计数，不受主循环负载影响 |
| **Flash 存储** | 菜单项全部 `const`，内存仅存运行时状态（约 64 字节） |
| **可裁剪** | `KEY_COUNT` 宏控制按键数量，`MENU_MAX_DEPTH` 控制菜单深度 |
| **互斥渲染** | 菜单激活期间 `display_mgr` 刷屏被抑制，避免画面冲突 |
| **UTF-8 原生** | 菜单文本使用原生 UTF-8 中文，禁止 `\x` 转义序列（见 AGENTS.md） |
| **滚动策略** | 光标移动时自动调整 `scroll_offset`，始终确保光标在可视区域内 |

---

## 8. 使用说明：新增菜单项操作步骤

本章以 **"新增一个'背光亮度'调节菜单（含三级结构）"** 为完整案例，演示从头到尾新增一个菜单项需要的所有步骤。掌握此案例后，可按相同模式新增任意类型的菜单项。

### 8.1 需求描述

在主菜单新增第 9 项 **"9.背光设置"**，进入后包含两个子项：

| 子项 | 类型 | 说明 |
|------|------|------|
| 背光开关 | TOGGLE | 控制 OLED 背光开/关，当前无硬件背光控制，先用 LED 代替演示 |
| 背光亮度 | VALUE | 0~100，步长 10，调节 OLED 对比度（间接模拟背光效果） |

> **注意：** 本案例仅演示菜单框架的扩展方法。实际 OLED 模组无独立背光引脚，此处用已有功能桥接模拟。

---

### 8.2 步骤一：在 `menu_items.c` 顶部新增状态变量

在文件顶部状态变量区（`g_contrast` 之后）新增：

```c
/* ---- 新增: 背光状态变量 ---- */
static uint8_t g_backlight_on = 1;      /* 1=开, 0=关 */
static uint8_t g_backlight_brightness = 80;  /* 0~100, 默认 80 */
```

**操作位置：** 打开 `stm32f407/src/menu_items.c`，找到：

```c
static uint8_t g_contrast    = 255;
```

在其下方添加上述两行。

**原则：** 所有被 `value_ptr` 指向的变量必须是 **全局生命周期** 的（`static` 或全局），不能是栈上的局部变量。否则菜单激活时指针悬空。

---

### 8.3 步骤二：编写回调函数

在回调函数区（`cb_contrast_changed` 之后）新增：

```c
/* ---- 背光 TOGGLE 回调 ---- */
static void cb_backlight_toggle(uint8_t new_val)
{
    if (new_val) {
        led_mgr_set_state(LED_STATE_ON);   /* 背光开 → LED 亮 (演示) */
    } else {
        led_mgr_set_state(LED_STATE_OFF);  /* 背光关 → LED 灭 (演示) */
    }
}

/* ---- 背光亮度 VALUE 回调 ---- */
static void cb_backlight_brightness(uint8_t val)
{
    /* 将 0~100 的亮度值映射到 SSD1306 对比度 0~255 */
    uint8_t contrast = (uint8_t)((uint16_t)val * 255 / 100);
    ssd1306_set_contrast(contrast);
}
```

**要点：** 必须 include `ssd1306.h` 和 `led_mgr.h`（已存在），如果引入了新的依赖模块，需在文件头部添加 `#include`。

---

### 8.4 步骤三：定义菜单项（自底向上）

在 `menu_items.c` 的菜单项定义区域，按照 **自底向上** 的顺序添加。

#### 8.4.1 先定义最底层的叶子项（Level 2）

```c
/* ---- 背光开关 TOGGLE (Level 2) ---- */
static const menu_item_t item_backlight_on = {
    .text = "背光开关",
    .type = MENU_TYPE_TOGGLE,
    .toggle = {
        .value_ptr     = &g_backlight_on,
        .checked_value = 1,
        .on_change     = cb_backlight_toggle,
    },
};

/* ---- 背光亮度 VALUE (Level 2) ---- */
static const menu_item_t item_backlight_brightness = {
    .text = "背光亮度",
    .type = MENU_TYPE_VALUE,
    .value = {
        .value_ptr = &g_backlight_brightness,
        .min       = 0,
        .max       = 100,
        .step      = 10,
        .on_change = cb_backlight_brightness,
    },
};
```

#### 8.4.2 定义子菜单指针数组（Level 2）

```c
static const menu_item_t *menu_backlight_items[] = {
    &item_backlight_on,
    &item_backlight_brightness,
};
```

#### 8.4.3 定义父级子菜单项（Level 1）

```c
/* ---- 背光设置 二级菜单 (Level 1) ---- */
static const menu_item_t item_backlight_menu = {
    .text = "背光设置",
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_backlight_items, .count = 2 },
};
```

#### 8.4.4 将新菜单添加到"设置"子菜单中（或创建新的主菜单项）

**方案 A（推荐）：** 将"背光设置"放入已有的"4.设置"子菜单中。修改 `menu_setting_items` 数组：

```c
/* ---- 设置 二级菜单 (Level 1) ---- */
static const menu_item_t *menu_setting_items[] = {
    &item_contrast_menu,
    &item_backlight_menu,   /* ← 新增这一行 */
};
```

**方案 B：** 创建独立的第 9 个主菜单项。在 Level 0 区域新增：

```c
static const menu_item_t item_main_9 = {
    .text = "9.背光设置",
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_backlight_items, .count = 2 },
};
```

然后在 `menu_main_items` 数组中增加：

```c
static const menu_item_t *menu_main_items[] = {
    &item_main_1, &item_main_2, &item_main_3,
    &item_main_4, &item_main_5, &item_main_6,
    &item_main_7, &item_main_8,
    &item_main_9,   /* ← 新增这一行 */
};
```

同时更新 `menu_root.count`：

```c
static const menu_item_t menu_root = {
    .text = NULL,
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_main_items, .count = 9 },  /* 8 → 9 */
};
```

**本节案例采用方案 A**（放入"设置"子菜单），以避免主菜单超过 4 行需要滚动。

---

### 8.5 步骤四：（可选）在 `menu_items_init_state()` 中同步外部状态

如果新增的变量需要与外部系统保持一致（比如上电时根据硬件实际状态初始化），在 `menu_items_init_state()` 中添加同步代码：

```c
void menu_items_init_state(void)
{
    g_remote_mode = display_mgr_is_remote() ? 1 : 0;
    /* g_contrast 保持当前值不变，不硬编码重置 */

    /* 新增: 同步背光状态 */
    led_state_t led = led_mgr_get_state();
    g_backlight_on = (led == LED_STATE_OFF) ? 0 : 1;
}
```

---

### 8.6 步骤五：编译验证

在 Keil MDK 中重新编译项目：

```
Project → Rebuild all target files
```

预期结果：**0 Error(s), 0 Warning(s)**。

如果出现以下错误，按对应方式处理：

| 错误信息 | 原因 | 解决 |
|----------|------|------|
| `undefined symbol` | 新增类型需要的模块头文件未 include | 在 `menu_items.c` 顶部添加相应的 `#include` |
| `incompatible pointer type` | `value_ptr` 指向的变量类型与 `uint8_t *` 不匹配 | 确保变量声明为 `uint8_t` 类型 |
| Flash 空间不足 | 菜单项常量数据超出 Flash 容量 | 检查是否添加了过多冗余文本 |

---

### 8.7 操作验证

烧录固件后，在 OLED 上验证：

```
按键操作流程 (方案 A):
  上电 → 主菜单显示
  KEY1/KEY2 → 移动光标到 "4.设置"
  KEY3 → 进入 "设置" 子菜单
  看到: 对比度设置
        背光设置          ← 新增项
  KEY1 → 光标移到 "背光设置"
  KEY3 → 进入
  看到: 背光开关 [x]      ← TOGGLE, 当前为开
        背光亮度 [080]     ← VALUE, 当前为 80
  KEY3 → 切换 "背光开关" (LED 随之亮/灭)
  KEY1 移到 "背光亮度" → KEY3 进入编辑
  KEY2 短按 → 亮度 +10 → OLED 对比度变化
  KEY2 长按 → 快速 +50
  KEY3 → 确认
  KEY4 短按 → 返回上级
```

---

### 8.8 五种菜单类型的模板速查

以下给出五种菜单项类型的 **最小定义模板**，直接复制修改即可使用。

#### SUBMENU（子菜单）

```c
/* Step 1: 定义叶子菜单项 */
static const menu_item_t item_xxx_a = {
    .text = "子项A",
    .type = MENU_TYPE_ACTION,
    .action = cb_xxx_a,
};
/* Step 2: 子菜单指针数组 */
static const menu_item_t *menu_xxx_items[] = {
    &item_xxx_a,  /* 可放多个 */
};
/* Step 3: 父级 SUBMENU */
static const menu_item_t item_xxx_menu = {
    .text = "XXX菜单",
    .type = MENU_TYPE_SUBMENU,
    .submenu = { .items = menu_xxx_items, .count = 1 },  /* count 要准确 */
};
```

#### TOGGLE（开关）

```c
static uint8_t g_xxx_enabled = 1;   /* 状态变量 (必须全局生命周期) */

static void cb_xxx_toggle(uint8_t val) {
    /* val == checked_value 时表示"选中"状态 */
}

static const menu_item_t item_xxx_toggle = {
    .text = "XXX开关",
    .type = MENU_TYPE_TOGGLE,
    .toggle = {
        .value_ptr     = &g_xxx_enabled,   /* 指向共享状态 */
        .checked_value = 1,                /* 此值 → 显示 [x] */
        .on_change     = cb_xxx_toggle,    /* 可为 NULL */
    },
};
```

#### VALUE（数值调节）

```c
static uint8_t g_xxx_value = 50;

static void cb_xxx_value(uint8_t val) {
    /* 每次值变化时回调 */
}

static const menu_item_t item_xxx_value = {
    .text = "XXX数值",
    .type = MENU_TYPE_VALUE,
    .value = {
        .value_ptr = &g_xxx_value,
        .min       = 0,
        .max       = 100,
        .step      = 5,
        .on_change = cb_xxx_value,         /* 可为 NULL */
    },
};
```

#### ACTION（执行动作）

```c
static void cb_xxx_action(void) {
    /* 执行具体操作，无参数无返回值 */
}

static const menu_item_t item_xxx_action = {
    .text = "XXX操作",
    .type = MENU_TYPE_ACTION,
    .action = cb_xxx_action,
};
```

#### INFO（信息展示）

```c
static const menu_item_t item_xxx_info = {
    .text = "XXX信息",
    .type = MENU_TYPE_INFO,
    .info = { .detail_text = "这是详情文本，支持中文。" },
};
```

---

### 8.9 常见问题

#### Q1：新增菜单项不显示？

**检查清单：**
1. 菜单项是否已加入某个子菜单的 `items[]` 数组中？
2. 子菜单的 `.count` 是否与数组实际长度一致？
3. 该子菜单是否在菜单树中可达（从 root 沿路径能走到）？
4. 编译是否成功？Flash 中的 `const` 数据是否正确链接？

#### Q2：TOGGLE 两个选项共享一个变量，如何工作？

典型例子是"本地/远程"模式：

```c
static uint8_t g_remote_mode = 0;

// 选中此项时，把 g_remote_mode 设为 0
static const menu_item_t item_mode_local = {
    .toggle = { .value_ptr = &g_remote_mode, .checked_value = 0, ... }
};
// 选中此项时，把 g_remote_mode 设为 1
static const menu_item_t item_mode_remote = {
    .toggle = { .value_ptr = &g_remote_mode, .checked_value = 1, ... }
};
```

两个 TOGGLE 项的 `value_ptr` 指向 **同一个变量**，但 `checked_value` 不同。当用户选择某一项按 KEY3 确认时：
- 如果当前 `*value_ptr == checked_value`，则翻转 *value_ptr（导致两个都变）
- 如果当前 `*value_ptr != checked_value`，则将 *value_ptr 设为 checked_value

> **提示：** 对于排他互斥的多个选项（如 A/B/C），当前 TOGGLE 机制仅支持 2 选 1。如需 3 选 1，建议用 ACTION 类型配合自定义逻辑。

#### Q3：为什么 VALUE 编辑时 KEY4 会恢复原始值？

这是设计上的保护机制。进入 VALUE 编辑模式时，原始值被备份到 `value_backup`。如果用户调节过程中想放弃修改，按 KEY4 即可恢复进入编辑前的值。

#### Q4：主菜单超过 4 项怎么办？

OLED 只能显示 4 行，超过 4 项的菜单会自动支持滚动。用 KEY1/KEY2 移动光标，超出可视区时自动滚屏。当前最长子菜单"显示特效"有 7 项，已验证滚动正常。

#### Q5：如何禁用上电自动进入菜单？

修改 `user_app.c` 中 `user_app_init()` 的最后部分，在 `menu_mgr_init()` 之后调用：

```c
menu_mgr_init();
menu_mgr_deactivate();  /* ← 添加: 初始化后立即退出菜单 */
```

这样上电后直接进入正常显示模式，用户可长按 KEY4 进入菜单。

