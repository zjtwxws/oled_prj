# AGENTS.md — oled_prj 项目约定

## 编码约定

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

## 菜单系统

菜单定义文件: `stm32f407/src/menu_items.c`
菜单引擎: `stm32f407/src/menu_mgr.c`

菜单项类型: SUBMENU / TOGGLE / VALUE / ACTION / INFO
最大深度: `MENU_MAX_DEPTH = 8`
