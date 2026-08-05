---
name: gen-font-cn
description: Generate 16x16 Chinese dot-matrix font glyph data and insert into font.c for OLED display projects. Use when the user asks to add missing Chinese characters to the font table, generate font glyphs for specific characters, or fix characters that don't display on the OLED screen due to missing font data. Triggered by requests like "生成字库", "添加字模", "补充字库", "字库中没有X字", "生成X字的字模".
---

# Chinese Font Glyph Generator

Generate 16x16 column-major dot-matrix glyph data for Chinese characters and insert into an STM32 OLED project's ont.c.

## Prerequisites

- reetype-py installed (pip install freetype-py)
- SimSun font at C:/Windows/Fonts/simsun.ttc

## Usage

Run the script with the path to ont.c and the characters to add:

`ash
python scripts/gen_glyph.py <path/to/font.c> <characters>
`

### Examples

`ash
# Add single character
python scripts/gen_glyph.py stm32f407/src/font.c 例

# Add multiple characters
python scripts/gen_glyph.py stm32f407/src/font.c 例版选项

# Characters already in the table are automatically skipped
python scripts/gen_glyph.py stm32f407/src/font.c 三系统
`

## Workflow

1. Identify which Chinese characters are missing from the font table
2. Run scripts/gen_glyph.py with the font.c path and the missing characters
3. The script renders each character using FreeType monochrome at 17ppem from SimSun
4. New entries are appended to chinese_font_table before the closing };
5. The script reports what was added and the new table size

## Format

Glyph data is 32 bytes per character in column-major order:
- Bytes 0-15: 16 columns x rows 0-7 (LSB = top)
- Bytes 16-31: 16 columns x rows 8-15

This matches the existing format used by the OLED display driver in this project.
