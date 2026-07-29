#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 STM32 OLED 项目所需的中文字模数组。

用法:
    python gen_font.py

输出: 直接覆盖 ../src/font.c 中的 chinese_font_table 区域。
      若不想依赖系统字体, 可先用 fonttools 子集化字体, 再运行本脚本。

依赖:
    pip install pillow
"""

import os
import re
import subprocess
from PIL import Image, ImageDraw, ImageFont

# 项目需要显示的常用汉字集合 (去重)
TEXT = """
欢迎进入系统
OLED三级联动控制系统
天气温度湿度冷热晴雨雪风
北南东西中高低
模式静态滚动左右上下翻页淡入淡出
按键事件已连接断开异常错误等待切换
设置成功失败屏幕预览内容空
秒分时日月年星期今天
当前状态显示文字
"""

# 额外补充一些常见字, 提升通用性
EXTRA = """的一是在不了有和人这中大为上个国我以要他时来用们生到作地于出就分对成会可主发年动同工也能下过子说产种面而方后多定行学法所民得经十三之进着等部度家电力里如水化高自二理起小物现实加量都两体制机当使点从业本去把性好应开它合还因由其些然前外天政四日那社义事平形相全表间样与关各重新线内数正心反你明看原又么利比或但质气第向道命此变条只没结解问意建月公无系军很情者最立代想已通并提直题党程展五果料象员革位入常文总次品式活设及管特件长求老头基资边流路级少图山统接知较将组见计别她手角期根论运农指几九区强放决西被干做必战先回则任取完举色达摸儿营"""


def collect_chars(text):
    """收集所有唯一汉字, 保持首次出现顺序。"""
    seen = set()
    result = []
    for ch in text:
        if '\u4e00' <= ch <= '\u9fff' and ch not in seen:
            seen.add(ch)
            result.append(ch)
    return result


def find_font():
    """尝试找一个可用的中文字体。"""
    candidates = [
        "C:/Windows/Fonts/simsun.ttc",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ]
    for f in candidates:
        if os.path.exists(f):
            return f
    # 尝试 fc-list
    try:
        out = subprocess.check_output(["fc-list", ":lang=zh", "family"], text=True)
        first = out.strip().split("\n")[0].split(",")[0]
        if first:
            return first
    except Exception:
        pass
    raise RuntimeError("未找到中文字体, 请手动指定 FONT_PATH 环境变量")


def render_glyph(font_path, ch, size=16):
    """渲染单个汉字为 16x16 位图, 返回 32 字节 (按列, 每列 2 字节)。"""
    img = Image.new("1", (size, size), 1)  # 1=白底
    draw = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype(font_path, size)
    except Exception as e:
        raise RuntimeError(f"无法加载字体 {font_path}: {e}")

    # 计算居中位置
    bbox = draw.textbbox((0, 0), ch, font=font)
    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]
    x = (size - w) // 2 - bbox[0]
    y = (size - h) // 2 - bbox[1]
    draw.text((x, y), ch, font=font, fill=0)  # 0=黑字

    pixels = img.load()
    bytes_data = []
    for col in range(size):
        upper = 0
        lower = 0
        for row in range(8):
            if pixels[col, row] == 0:
                upper |= (1 << row)
        for row in range(8, 16):
            if pixels[col, row] == 0:
                lower |= (1 << (row - 8))
        bytes_data.append(upper)
        bytes_data.append(lower)
    return bytes_data


def escape_utf8(ch):
    """将汉字转为 C 字符串转义序列 (UTF-8)。"""
    return "".join("\\x%02x" % b for b in ch.encode("utf-8"))


def generate_table(chars, font_path):
    lines = []
    lines.append("/* 中文字模表 (UTF-8 编码, 16x16 点阵, 每字 32 字节) */")
    lines.append("typedef struct {")
    lines.append("    const char *utf8;      /* UTF-8 编码的单个汉字 */")
    lines.append("    const uint8_t glyph[32]; /* 16x16 点阵, 按列扫描 */")
    lines.append("} chinese_char_t;")
    lines.append("")
    lines.append("static const chinese_char_t chinese_font_table[] = {")
    for ch in chars:
        glyph = render_glyph(font_path, ch)
        utf8_esc = escape_utf8(ch)
        hex_str = ", ".join("0x%02x" % b for b in glyph)
        lines.append(f'    {{"{utf8_esc}", {{{hex_str}}}}}, /* {ch} */')
    lines.append("};")
    lines.append("")
    lines.append(f"#define CHINESE_FONT_TABLE_SIZE  (sizeof(chinese_font_table) / sizeof(chinese_font_table[0]))")
    lines.append("")
    return "\n".join(lines)


def patch_font_c(font_c_path, table_text):
    with open(font_c_path, "r", encoding="utf-8") as f:
        content = f.read()

    marker_start = "/* === AUTO-GENERATED CHINESE FONT TABLE START === */"
    marker_end = "/* === AUTO-GENERATED CHINESE FONT TABLE END === */"

    new_block = marker_start + "\n" + table_text + marker_end

    if marker_start in content and marker_end in content:
        pattern = re.compile(re.escape(marker_start) + ".*?" + re.escape(marker_end), re.DOTALL)
        content = pattern.sub(new_block, content)
    else:
        # 插入到文件末尾的 /* --- 公开接口 --- */ 之前
        insert_marker = "/* --- 公开接口 --- */"
        if insert_marker in content:
            content = content.replace(insert_marker, new_block + "\n" + insert_marker)
        else:
            content += "\n" + new_block + "\n"

    with open(font_c_path, "w", encoding="utf-8") as f:
        f.write(content)


def main():
    chars = collect_chars(TEXT + EXTRA)
    font_path = os.environ.get("FONT_PATH", find_font())
    print(f"字体: {font_path}")
    print(f"生成 {len(chars)} 个汉字字模")

    table_text = generate_table(chars, font_path)

    script_dir = os.path.dirname(os.path.abspath(__file__))
    font_c_path = os.path.join(script_dir, "..", "src", "font.c")
    patch_font_c(font_c_path, table_text)
    print(f"已更新: {font_c_path}")


if __name__ == "__main__":
    main()
