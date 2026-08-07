# AGENTS.md — oled_prj 项目约定

## C 语言编码规范

以下规范适用于 `stm32f407/` 目录下所有 `.c` 和 `.h` 源代码文件。
其他目录（如 `rk3506/`、`tools/` 等）原有代码不做强制修改，但新增代码应遵循本规范。

---

### 1. 大括号 — Allman 风格（括号必须另起一行）

所有大括号 `{` 必须**另起一行**，包括函数体、`if`/`else`、`for`/`while`/`do-while`、
`switch`/`case`、`struct`/`union`/`enum` 及数组/结构体初始化。

**正确示例：**
```c
void function_name(void)
{
    // 函数体
}

if (condition)
{
    // if 分支
}
else
{
    // else 分支
}

for (int i = 0; i < n; i++)
{
    // 循环体
}

typedef struct
{
    uint8_t cursor;
    uint8_t scroll_offset;
} menu_context_t;

static const menu_item_t item_demo_a =
{
    .text = "选项A",
    .type = MENU_TYPE_ACTION,
    .action = cb_demo_a,
};

switch (value)
{
case 0:
    do_something();
    break;
case 1:
    do_other();
    break;
default:
    break;
}
```

**错误示例：**
```c
void function_name(void) {   // 禁止：括号跟在声明同行
if (condition) {              // 禁止：括号跟在 if 同行
```

**适用范围：** 全体 `stm32f407/src/*.c` 和 `stm32f407/inc/*.h`

---

### 2. if 必须使用大括号（禁止无括号单行语句）

所有 `if`、`else if`、`else` 后必须跟 `{ }`，即使只有一条语句。

**正确示例：**
```c
if (x == 0)
{
    return;
}

if (ptr != NULL)
{
    *ptr = 1;
}
else
{
    return -1;
}
```

**错误示例：**
```c
if (x == 0) return;          // 禁止：缺少大括号
if (p == NULL) return -1;    // 禁止：缺少大括号
```

---

### 3. 文件头注释

每个 `.c` 和 `.h` 文件必须有文件头注释，描述文件内容。

**格式：**
```c
/**
 * @file    文件名
 * @brief   文件功能简述
 */
```

---

### 4. 函数头注释

每个函数（含 `static` 函数）必须有函数头注释。

**格式：**
```c
/**
 * @brief  功能简述
 * @param  参数名 参数说明
 * @return 返回值说明
 * @note   使用注意事项（可选）
 * @date   创建时间 YYYY-MM-DD
 */
```

---

### 5. 缩进：4 空格，禁止 Tab 字符

所有缩进使用 **4 个空格**，禁止使用 Tab 字符（`\t`）。

---

### 6. 行宽 ≤ 120 字符

每行代码不超过 120 个字符。超长行应合理断行。

---

### 7. 变量命名

- 全局变量：`g_` 前缀（如 `g_menu`、`g_remote_mode`）
- 静态变量：下划线分隔小写（如 `rx_buf`、`last_key_scan`）
- 局部变量：下划线分隔小写（如 `item_idx`、`scroll_offset`）
- 宏常量和枚举：全大写 + 下划线（如 `MENU_MAX_DEPTH`、`KEY_LONG_PRESS_MS`）
- 类型定义：小写 + 下划线 + `_t` 后缀（如 `menu_item_t`、`key_event_t`）

---

### 中文字符：永远使用真正的 UTF-8 汉字，禁止 \x 转义序列

所有 C 源代码文件（`.c`、`.h`）中的中文字符串必须使用真正的 UTF-8 编码汉字，**严禁**使用 `\xNN` 十六进制转义序列。

**正确示例：**
```c
.text = "设置",
.text = "对比度",
.text = "1.工作模式",
```

**错误示例：**
```c
.text = "4.\xe8\xae\xbe\xe7\xbd\xae",           // 禁止
.text = "1.\xe5\xb7\xa5\xe4\xbd\x9c\xe6\xa8\xa1\xe5\xbc\x8f",  // 禁止
```

**原因：** 真正的 UTF-8 汉字在支持 UTF-8 的编辑器中可直接阅读和编辑，而 `\x` 转义序列完全不可读，增加维护成本。

**适用范围：**
- C 字符串字面量（`.text = "..."`）
- 代码注释中的中文
- 所有本项目新增或修改的文本内容

## 技术栈

- MCU: STM32F407 (ARM Cortex-M4)
- HAL: STM32F4xx HAL Driver
- 显示: SSD1306 OLED 128x64 (I2C)
- 编译器: ARMCC (Keil MDK)

## 驱动层隔离

### 应用层禁止直接调用 HAL/CMSIS 接口

所有应用层代码（`menu_items.c`、`user_app.c`、`display_mgr.c` 等）不得直接调用
STM32 HAL 库函数或 CMSIS 内核函数（如 `NVIC_SystemReset`、`HAL_GPIO_WritePin` 等）。
所有硬件操作必须通过驱动层模块封装：

- 复位操作 → `sys_config_reset()` (sys_config.h)
- GPIO 操作 → key_drv / led_mgr / i2c_drv
- 看门狗 → iwdg_drv
- Flash 操作 → sys_config

**禁止示例：**
```c
#include "stm32f4xx_hal.h"   // 应用层禁止 include HAL 头文件
NVIC_SystemReset();             // 应用层禁止直接调用 CMSIS
```

**正确示例：**
```c
#include "sys_config.h"       // 应用层只 include 驱动层头文件
sys_config_reset();            // 通过驱动层接口调用
```


## 字库维护

### 新增菜单文本时的字库检查

每次新增菜单项、对话框提示文字、或任何需要在 OLED 上显示的中文文本时，必须自动检查所用汉字是否已存在于 `stm32f407/src/font.c` 的 `chinese_font_table` 中。若存在缺失字，调用 `.skills/gen-font-cn/` skill 生成字模并插入字库表。

使用方法：
```bash
python .skills/gen-font-cn/scripts/gen_glyph.py stm32f407/src/font.c <缺失汉字>
```

脚本会自动跳过已存在的字符，仅生成缺失字的 16×16 点阵字模，并追加到 `chinese_font_table` 末尾。

## 菜单系统

菜单定义文件: `stm32f407/src/menu_items.c`
菜单引擎: `stm32f407/src/menu_mgr.c`

菜单项类型: SUBMENU / TOGGLE / VALUE / ACTION / INFO
最大深度: `MENU_MAX_DEPTH = 8`