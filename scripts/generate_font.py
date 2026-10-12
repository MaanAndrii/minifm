"""Generate an LVGL v9 bitmap font with Latin and Cyrillic glyphs (requires Pillow)."""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

size = int(sys.argv[3]) if len(sys.argv) > 3 else 18
name = sys.argv[4] if len(sys.argv) > 4 else "app_font_18"
font = ImageFont.truetype(sys.argv[1], size)
glyphs = [(0, 0, 0, 0, 0, 0)]
bitmap = []
numeric = size >= 80
codes = list(range(45, 59)) if numeric else list(range(0x20, 0x7F)) + list(range(0x400, 0x500)) + [0xB7,0x2013,0x2014]
for code in codes:
    char = chr(code)
    left, top, right, bottom = font.getbbox(char, anchor="ls")
    width, height = right-left, bottom-top
    image = Image.new("L", (max(1, width), max(1, height)))
    ImageDraw.Draw(image).text((-left, -top), char, font=font, fill=255, anchor="ls")
    offset = len(bitmap)
    pixels = [min(15, (value+8)//17) for value in image.getdata()] if width and height else []
    for i in range(0, len(pixels), 2):
        bitmap.append((pixels[i] << 4) | (pixels[i+1] if i+1 < len(pixels) else 0))
    glyphs.append((offset, round(font.getlength(char)*16), width, height, left, -bottom))

output = ['/* Generated from Noto Sans; see vendor/LICENSE.NotoSans. */', '#include "lvgl.h"',
          'static const uint8_t bitmap[] = {']
for i in range(0, len(bitmap), 24):
    output.append(','.join(f'0x{x:02x}' for x in bitmap[i:i+24]) + ',')
output.append('};\nstatic const lv_font_fmt_txt_glyph_dsc_t glyphs[] = {')
for index, advance, width, height, x, y in glyphs:
    output.append(f'{{.bitmap_index={index},.adv_w={advance},.box_w={width},.box_h={height},.ofs_x={x},.ofs_y={y}}},')
maps = ['{.range_start=45,.range_length=14,.glyph_id_start=1,.type=LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},'] if numeric else [
    '{.range_start=0x20,.range_length=95,.glyph_id_start=1,.type=LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},',
    '{.range_start=0x400,.range_length=256,.glyph_id_start=96,.type=LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},',
    '{.range_start=0xB7,.range_length=1,.glyph_id_start=352,.type=LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},',
    '{.range_start=0x2013,.range_length=2,.glyph_id_start=353,.type=LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},']
output += ['};', 'static const lv_font_fmt_txt_cmap_t cmaps[] = {', *maps,
    '};', 'static lv_font_fmt_txt_dsc_t descriptor = {',
    f'.glyph_bitmap=bitmap,.glyph_dsc=glyphs,.cmaps=cmaps,.cmap_num={len(maps)},.bpp=4,.bitmap_format=0,', '};',
    f'const lv_font_t {name} = {{',
    '.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,',
    f'.line_height={round(size*1.45)},.base_line={round(size/3)},.dsc=&descriptor,', '};']
Path(sys.argv[2]).write_text('\n'.join(output)+'\n', encoding='utf-8')
print(f'Generated {len(glyphs)-1} glyphs; {len(bitmap)} bitmap bytes')
