# C 语言标签联合体（Tagged Union）设计模式

## 概述

**标签联合体** 是 C 语言中实现"多态"的经典设计手法，通过 `enum`（标签）+ `union`（联合体）组合，让一个数据结构在不同状态下持有不同类型的字段，从而大幅节约内存。

本项目 `menu_mgr.h` 中的 `menu_item` 结构体就是一个典型应用。

---

## 联合体的基本概念

### 什么是联合体（`union`）？

联合体是 C 语言中的一种数据类型，它和结构体（`struct`）看起来很相似，但内存分配方式完全不同：

| 特性 | `struct`（结构体） | `union`（联合体） |
|------|-------------------|-------------------|
| 内存分配 | 每个成员有独立的内存空间 | 所有成员共享同一块内存 |
| 大小 | **所有成员大小之和**（含对齐填充） | **最大成员的大小** |
| 同时有效 | 所有成员同时有效 | 同一时刻只有一个成员有效 |
| 写入一个成员 | 不影响其他成员 | **覆盖**其他成员的值 |

直观对比：

```c
/* 结构体：各成员独占空间，互不干扰 */
struct ExampleStruct {
    uint8_t  a;   /* 1 字节 */
    uint32_t b;   /* 4 字节（对齐到偏移 4） */
    uint8_t  c;   /* 1 字节 */
};
/* sizeof(ExampleStruct) = 12（含对齐填充） */
/*
内存布局:
┌───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┐
│   a   │  pad  │  pad  │  pad  │          b (4 bytes)        │   c   │  pad  │  pad  │  pad  │
└───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┘
*/
```

```c
/* 联合体：所有成员共用同一块空间 */
union ExampleUnion {
    uint8_t  a;   /* 1 字节 */
    uint32_t b;   /* 4 字节 */
    uint8_t  c;   /* 1 字节 */
};
/* sizeof(ExampleUnion) = 4（取最大成员 b 的大小） */
/*
内存布局:
┌───────────────────────────────┬───────────────────────────────┐
│  a (byte 0) │  c (byte 0)    │          b (4 bytes)          │
└───────────────────────────────┴───────────────────────────────┘
       ↑ a 和 c 都从 byte 0 开始，b 覆盖全部 4 字节
*/
```

### 联合体的核心行为

```c
#include <stdio.h>
#include <stdint.h>

union Demo {
    uint32_t as_int;
    uint8_t  as_bytes[4];
    float    as_float;
};

int main(void) {
    union Demo u;

    /* 写入一个成员... */
    u.as_int = 0x12345678;
    printf("as_int   = 0x%08X\n", u.as_int);    /* 0x12345678 */

    /* ...再用另一个成员读取，读到的是同一块内存的另一种解释 */
    printf("byte[0]  = 0x%02X\n", u.as_bytes[0]); /* 小端: 0x78 */
    printf("byte[1]  = 0x%02X\n", u.as_bytes[1]); /* 0x56 */

    /* 写入另一个成员会覆盖之前的值 */
    u.as_float = 3.14f;
    printf("as_int   = 0x%08X\n", u.as_int);    /* 不再是 0x12345678 */
    /* 输出: IEEE 754 浮点数 3.14 的二进制表示 */
}
```

**一句话总结**：`struct` 是"**并排存放**"（各占各的），`union` 是"**叠在一起**"（共享同一块）。

### 联合体的经典用途

| 用途 | 说明 |
|------|------|
| **标签联合体** | `enum` + `union`，同一对象在不同状态下持不同类型数据（本文重点） |
| **类型双关** | 用不同类型"解读"同一段内存（如上面的 `uint32` ↔ `uint8[4]` 互转） |
| **内存节约** | 多个变量不会同时使用，共享空间减少内存占用 |
| **协议解析** | 网络协议中同一段数据在不同消息类型下有不同的字段结构 |

---

## 为什么需要联合体？

### 问题场景

一个"菜单项"在任意时刻只能是 **5 种类型之一**：

| 类型 | 需要的字段 |
|------|-----------|
| SUBMENU | `items` 指针 + `count` |
| TOGGLE | `value_ptr` + `checked_value` + `on_change` |
| VALUE | `value_ptr` + `min` + `max` + `step` + `on_change` |
| ACTION | `action` 函数指针 |
| INFO | `detail_text` 指针 |

如果不使用联合体，结构体必须为所有类型的字段都分配空间：

```c
/* ❌ 不使用联合体：每个 menu_item 都要携带所有变体的字段 */
typedef struct menu_item {
    const char *text;
    menu_item_type_t type;

    /* SUBMENU */
    const struct menu_item **items;
    uint8_t                  count;

    /* TOGGLE */
    uint8_t *toggle_value_ptr;
    uint8_t  checked_value;
    void   (*toggle_on_change)(uint8_t);

    /* VALUE */
    uint8_t *value_value_ptr;
    uint8_t  min, max, step;
    void   (*value_on_change)(uint8_t);

    /* ACTION */
    void (*action)(void);

    /* INFO */
    const char *detail_text;
};
```

这在 STM32F407（RAM 仅 192KB）上是严重的浪费——如果有几十个菜单项定义在 Flash 中，也会造成不小的 ROM 浪费。

### 解决方案：联合体

```c
/* ✅ 使用联合体：同一块内存，按需解释 */
typedef struct menu_item {
    const char       *text;
    menu_item_type_t  type;        /* ← 标签：决定联合体中哪个成员当前有效 */
    union {
        struct { /* SUBMENU 的字段 */ } submenu;
        struct { /* TOGGLE 的字段 */ } toggle;
        struct { /* VALUE 的字段 */ } value;
        void (*action)(void);          /* ACTION */
        struct { /* INFO 的字段 */ } info;
    };
} menu_item_t;
```

**核心思想**：5 种变体共享同一块内存，同一时刻只有一种是"激活"的。

---

## 联合体的大小如何确定？

> **联合体大小 = 其最大成员的大小（含对齐填充）。**

以 Cortex-M4 (ARMCC, 32位, 指针 4 字节) 为例：

```
┌─────────────┬──────────────────────────────────────────────┬──────────┐
│ 联合体成员   │ 字段分解                                      │ 大小     │
├─────────────┼──────────────────────────────────────────────┼──────────┤
│ submenu     │ items(4B) + count(1B) + padding(3B)          │  8 字节  │
│ toggle      │ value_ptr(4B) + checked_value(1B) + pad(3B)  │ 12 字节  │
│             │                               + on_change(4B) │          │
│ value       │ value_ptr(4B) + min(1B) + max(1B) + step(1B) │ 12 字节  │
│             │               + pad(1B) + on_change(4B)      │          │
│ action      │ 函数指针(4B)                                   │  4 字节  │
│ info        │ detail_text 指针(4B)                           │  4 字节  │
├─────────────┴──────────────────────────────────────────────┼──────────┤
│ 联合体整体  │ ← 取最大值(toggle/value)                       │ 12 字节  │
└────────────┴──────────────────────────────────────────────┴──────────┘
```

**验证方法**：用 `sizeof(menu_item)` 即可在编译期确认实际大小。

```c
/* 可以在代码中加编译期断言验证 */
_Static_assert(sizeof(menu_item) <= 24, "menu_item too large");
```

---

## 会不会误读到其他成员的值？

**会！如果访问时没有先判断 `type`，确实是未定义行为。**

看一个具体的例子，`value.value_ptr` 和 `info.detail_text` 两个字段在联合体中的内存布局：

```
联合体内存区域 (12 字节):
┌────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┐
│ byte 0 │ byte 1 │ byte 2 │ byte 3 │ byte 4 │ byte 5 │ byte 6 │ byte 7 │ byte 8 │ byte 9 │ byte10 │ byte11 │
├────────┴────────┴────────┴────────┼────────┴────────┴────────┴────────┼────────┴────────┴────────┴────────┤
│      value.value_ptr (4B)         │      on_change 函数指针 (4B)       │     min(1B)+max(1B)+step(1B)+pad  │
├───────────────────────────────────┼────────────────────────────────────┼───────────────────────────────────┤
│      info.detail_text (4B)        │           未使用                    │               未使用              │
└───────────────────────────────────┴────────────────────────────────────┴───────────────────────────────────┘
```

如果代码这样写：

```c
menu_item item;  // 假设 item.type == MENU_TYPE_VALUE
// ...
const char *s = item.info.detail_text;  // ❌ 危险！
// 读到的是 item.value.value_ptr 的位模式，
// 将其解释为字符串指针 → 可能指向任意地址 → 崩溃！
```

### 解决方案：总是先判断 `type` 再访问

`type` 字段就是"标签（tag）"，它告诉你联合体中哪个成员当前是有效的：

```c
/* ✅ 正确用法：type 标签保护 */
void render_item(const menu_item_t *item) {
    switch (item->type) {
    case MENU_TYPE_SUBMENU:
        // 此时只能访问 item->submenu.xxx
        enter_menu(item->submenu.items, item->submenu.count);
        break;

    case MENU_TYPE_TOGGLE:
        // 此时只能访问 item->toggle.xxx
        *(item->toggle.value_ptr) ^= 1;
        break;

    case MENU_TYPE_VALUE:
        // 此时只能访问 item->value.xxx
        *(item->value.value_ptr) += item->value.step;
        break;

    case MENU_TYPE_ACTION:
        // 此时只能访问 item->action()
        if (item->action) item->action();
        break;

    case MENU_TYPE_INFO:
        // 此时只能访问 item->info.xxx
        show_detail(item->info.detail_text);
        break;
    }
}
```

**只要代码始终遵守"先判断 `type`，再访问对应的联合体成员"这一约定，就不会出错。**

---

## 代价：编译器不会帮你检查

这是 C 语言联合体的根本限制——**编译器不知道你访问的是否是"正确的"成员**。以下代码在 C 中完全合法，编译不会有任何警告：

```c
menu_item_t item;
item.type = MENU_TYPE_INFO;
item.info.detail_text = "版本 1.0";

// 编译器不会阻止这种操作：
uint8_t x = *(item.value.value_ptr);  // 编译通过！运行时 UB！
```

对比现代语言的**代数数据类型（ADT）**，它们会在编译期强制穷尽匹配，如 Rust 的 `enum`：

```rust
enum MenuItem {
    Submenu { items: &[MenuItem], count: u8 },
    Toggle  { value_ptr: &mut u8, checked_value: u8, on_change: fn(u8) },
    Value   { value_ptr: &mut u8, min: u8, max: u8, step: u8, on_change: fn(u8) },
    Action  { action: fn() },
    Info    { detail_text: &str },
}

fn render(item: &MenuItem) {
    match item {  // 编译器强制穷尽所有分支
        MenuItem::Value { value_ptr, .. } => { /* 安全访问 */ }
        MenuItem::Info { detail_text } => { /* 安全访问 */ }
        // ... 编译器确保不会误用
    }
}
```

在 C 语言中，这项安全检查完全落在程序员身上。防范措施包括：

| 方法 | 说明 |
|------|------|
| **严格约定** | 所有代码访问联合体前必须 switch 检查 `type` |
| **代码审查** | CR 时重点关注联合体成员访问是否受 `type` 保护 |
| **封装函数** | 不让外部直接访问联合体，通过访问器函数读写 |
| **静态分析** | 使用 Coverity / PC-lint 等工具检测联合体误用 |

---

## 本项目中的实际用法参考

查看 `menu_mgr.c` 和 `menu_items.c` 可以看到完整的使用模式：

- `menu_items.c`：定义菜单项时，根据 `type` 只初始化对应的联合体成员
- `menu_mgr.c`：渲染/处理菜单时，总是先 `switch (item->type)` 再访问联合体

---

## 总结

| 要点 | 说明 |
|------|------|
| **为什么用** | 节约内存，一个对象只需存一种变体的字段 |
| **大小怎么定** | 编译器取最大成员大小 + 对齐填充，`sizeof` 可验证 |
| **怎么防误用** | 用 `type` 枚举做标签，访问前先 `switch` 判断 |
| **代价** | C 编译器不会强制检查，安全全靠程序员纪律 |

标签联合体是嵌入式 C 开发中不可或缺的技巧，Linux 内核、FreeRTOS、各种 HAL 库中随处可见。
