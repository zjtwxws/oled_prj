#!/usr/bin/env python3
# -*- coding: utf-8 -*-
'''
Generate 16x16 Chinese dot-matrix glyph data and insert into font.c.
Uses FreeType monochrome rendering from SimSun font at 17ppem.

Usage:
    python gen_glyph.py <font_c_path> <char1> [char2] [char3] ...

Example:
    python gen_glyph.py ../stm32f407/src/font.c 例版选项
'''

import sys, os, re

try:
    import freetype
except ImportError:
    print('ERROR: freetype-py not installed. Run: pip install freetype-py')
    sys.exit(1)

FONT_PATH = 'C:/Windows/Fonts/simsun.ttc'
PIXEL_SIZE = 17


def render_glyph(ch):
    face = freetype.Face(FONT_PATH)
    face.set_pixel_sizes(PIXEL_SIZE, PIXEL_SIZE)
    flags = (freetype.FT_LOAD_RENDER |
             freetype.FT_LOAD_TARGET_MONO |
             freetype.FT_LOAD_MONOCHROME)
    face.load_char(ch, flags)
    bitmap = face.glyph.bitmap
    buf = bitmap.buffer
    bw, bh = bitmap.width, bitmap.rows
    pitch = bitmap.pitch

    ox = (16 - bw) // 2
    oy = 16 - face.glyph.bitmap_top

    grid = [[0] * 16 for _ in range(16)]
    for row in range(bh):
        gy = oy + row
        if 0 <= gy < 16:
            for col in range(bw):
                gx = ox + col
                if 0 <= gx < 16:
                    bi = row * pitch + col // 8
                    biti = 7 - (col % 8)
                    if bi < len(buf) and ((buf[bi] >> biti) & 1):
                        grid[gy][gx] = 1

    glyph = []
    for col in range(16):
        for rp in range(2):
            val = 0
            for bit in range(8):
                if grid[rp * 8 + bit][col]:
                    val |= (1 << bit)
            glyph.append(val)
    return glyph


def glyph_to_c(glyph):
    return '{' + ', '.join('0x{:02x}'.format(v) for v in glyph) + '}'


def utf8_to_c_escape(ch):
    return '"' + ch + '"'


def read_existing(font_c_path):
    with open(font_c_path, 'r', encoding='utf-8') as f:
        content = f.read()
    # Match both \xHH\xHH\xHH escapes and real UTF-8 Chinese in {"X", ...} entries
    entries1 = re.findall(
        r'\\x([0-9a-f]{2})\\x([0-9a-f]{2})\\x([0-9a-f]{2})',
        content
    )
    # Also match real UTF-8: {"中", ...}
    cjk_range = r'\u4e00-\u9fff\u3000-\u303f\uff00-\uffef'
    entries2 = re.findall(
        r'\{' + re.escape('"') + r'([' + cjk_range + r'])' + re.escape('"'),
        content
    )
    existing = set()
    for h1, h2, h3 in entries1:
        try:
            existing.add(bytes([int(h1, 16), int(h2, 16), int(h3, 16)]).decode('utf-8'))
        except:
            pass
    for ch in entries2:
        existing.add(ch)
    return content, existing


def insert_glyphs(font_c_path, new_entries):
    with open(font_c_path, 'r', encoding='utf-8') as f:
        content = f.read()

    idx_table = content.find('CHINESE_FONT_TABLE_SIZE')
    if idx_table < 0:
        print('ERROR: CHINESE_FONT_TABLE_SIZE not found')
        return False

    close_brace = content.rfind('};', 0, idx_table)
    if close_brace < 0:
        print('ERROR: table closing }; not found')
        return False

    lines = []
    for ch, glyph in new_entries:
        esc = utf8_to_c_escape(ch)
        gstr = glyph_to_c(glyph)
        lines.append('    {' + esc + ', ' + gstr + '}, /* ' + ch + ' */')
    insert_text = '\n'.join(lines) + '\n'

    new_content = content[:close_brace] + insert_text + content[close_brace:]

    with open(font_c_path, 'w', encoding='utf-8') as f:
        f.write(new_content)
    return True


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    font_c_path = sys.argv[1]
    chars_text = ' '.join(sys.argv[2:])

    if not os.path.isfile(font_c_path):
        print('ERROR: font.c not found: ' + font_c_path)
        sys.exit(1)

    chars_needed = []
    seen = set()
    for ch in chars_text:
        if ord(ch) > 127 and ch not in seen:
            chars_needed.append(ch)
            seen.add(ch)

    if not chars_needed:
        print('No Chinese characters found.')
        sys.exit(0)

    content, existing = read_existing(font_c_path)
    print('Existing font table: ' + str(len(existing)) + ' unique Chinese chars.')

    new_chars = [ch for ch in chars_needed if ch not in existing]
    skip_count = len(chars_needed) - len(new_chars)

    if skip_count > 0:
        skipped = [ch for ch in chars_needed if ch in existing]
        print('Skip ' + str(skip_count) + ' already-existing: ' + ''.join(skipped))

    if not new_chars:
        print('All characters already exist.')
        sys.exit(0)

    print('Generating ' + str(len(new_chars)) + ' new: ' + ''.join(new_chars))

    new_entries = []
    for ch in new_chars:
        try:
            glyph = render_glyph(ch)
            new_entries.append((ch, glyph))
            nz = sum(1 for v in glyph if v != 0)
            print('  ' + ch + ' (U+' + format(ord(ch), '04X') + '): ' + str(nz) + ' bytes set')
        except Exception as e:
            print('  ERROR ' + ch + ': ' + str(e))

    if not new_entries:
        print('No glyphs generated.')
        sys.exit(1)

    ok = insert_glyphs(font_c_path, new_entries)
    if ok:
        content2, existing2 = read_existing(font_c_path)
        added = sum(1 for ch, _ in new_entries if ch in existing2)
        print('Done. Added ' + str(added) + '/' + str(len(new_entries)) +
              '. Table now has ' + str(len(existing2)) + ' chars.')
    else:
        print('ERROR: insertion failed.')


if __name__ == '__main__':
    main()
