#!/usr/bin/env python3
"""Convert the supplied title-screen reference to an indexed, row-RLE asset.

Run from GeekClock: .venv/bin/python firmware/tools/generate_mario_title_assets.py
The supplied PNG is retained in firmware/assets. Dynamic text and the original
stationary character are cleared before nearest-neighbour 240x240 conversion.
"""
from pathlib import Path
import argparse
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/mario_title_reference.png"


def rgb565(color):
    r, g, b = color
    return ((r & 248) << 8) | ((g & 252) << 3) | (b >> 3)


def font(source):
    # Most visible letters and the 0/1/2 digits come directly from the supplied
    # 8-bit title HUD. Remaining clock/date characters use matching 7x7 pixels.
    patterns = {
        '0': ['0111110','1100011','1100111','1101011','1110011','1100011','0111110'],
        '1': ['0011000','0111000','0011000','0011000','0011000','0011000','0111110'],
        '2': ['0111110','1100011','0000011','0001110','0110000','1100000','1111111'],
        '3': ['0111110','1100011','0000011','0011110','0000011','1100011','0111110'],
        '4': ['0001110','0011110','0110110','1100110','1111111','0000110','0000110'],
        '5': ['1111111','1100000','1111110','0000011','0000011','1100011','0111110'],
        '6': ['0011110','0110000','1100000','1111110','1100011','1100011','0111110'],
        '7': ['1111111','1100011','0000110','0001100','0011000','0011000','0011000'],
        '8': ['0111110','1100011','1100011','0111110','1100011','1100011','0111110'],
        '9': ['0111110','1100011','1100011','0111111','0000011','0000110','0111100'],
        'A': ['0011100','0110110','1100011','1100011','1111111','1100011','1100011'],
        'B': ['1111110','1100011','1100011','1111110','1100011','1100011','1111110'],
        'C': ['0111110','1100011','1100000','1100000','1100000','1100011','0111110'],
        'D': ['1111100','1100110','1100011','1100011','1100011','1100110','1111100'],
        'E': ['1111111','1100000','1100000','1111110','1100000','1100000','1111111'],
        'F': ['1111111','1100000','1100000','1111110','1100000','1100000','1100000'],
        'G': ['0111110','1100011','1100000','1101111','1100011','1100011','0111110'],
        'H': ['1100011','1100011','1100011','1111111','1100011','1100011','1100011'],
        'I': ['0111111','0001100','0001100','0001100','0001100','0001100','0111111'],
        'J': ['0011111','0000110','0000110','0000110','1100110','1100110','0111100'],
        'K': ['1100011','1100110','1101100','1111000','1101100','1100110','1100011'],
        'L': ['1100000','1100000','1100000','1100000','1100000','1100000','1111111'],
        'M': ['1100011','1110111','1111111','1111111','1101011','1100011','1100011'],
        'N': ['1100011','1110011','1111011','1101111','1100111','1100011','1100011'],
        'O': ['0111110','1100011','1100011','1100011','1100011','1100011','0111110'],
        'P': ['1111110','1100011','1100011','1111110','1100000','1100000','1100000'],
        'Q': ['0111110','1100011','1100011','1100011','1101011','1100110','0111101'],
        'R': ['1111110','1100011','1100011','1111110','1101100','1100110','1100011'],
        'S': ['0111110','1100011','1100000','0111110','0000011','1100011','0111110'],
        'T': ['1111111','0011000','0011000','0011000','0011000','0011000','0011000'],
        'U': ['1100011','1100011','1100011','1100011','1100011','1100011','0111110'],
        'V': ['1100011','1100011','1100011','1100011','1100011','0110110','0011100'],
        'W': ['1100011','1100011','1101011','1111111','1111111','1110111','1100011'],
        'X': ['1100011','1100011','0110110','0011100','0110110','1100011','1100011'],
        'Y': ['1100011','1100011','0110110','0011100','0011100','0011100','0011100'],
        'Z': ['1111111','0000011','0000110','0011100','0110000','1100000','1111111'],
        ':': ['0000000','0011000','0011000','0000000','0011000','0011000','0000000'],
        '.': ['0000000','0000000','0000000','0000000','0000000','0011000','0011000'],
        '-': ['0000000','0000000','0000000','0111110','0000000','0000000','0000000'],
        '/': ['0000011','0000110','0001100','0011000','0110000','1100000','1000000'],
        ' ': ['0000000'] * 7,
    }
    rows = {c: [int(p, 2) << 1 for p in pattern] + [0] for c, pattern in patterns.items()}
    # Extract actual white pixels, excluding the reference's black drop shadow.
    locations = {c: (24 + i * 8, 8) for i, c in enumerate('MARIO')}
    locations.update({c: (144 + i * 8, 8) for i, c in enumerate('WORLD')})
    locations.update({c: (200 + i * 8, 8) for i, c in enumerate('TIME')})
    locations.update({c: (48 + i * 16, 144) for i, c in enumerate('INSERT COIN') if c != ' '})
    locations.update({'P': (112, 176), '0': (24, 16), '1': (152, 16), '2': (136, 176)})
    for c, (x, y) in locations.items():
        bits = [sum((1 << (7-col)) for col in range(8)
                    if source.getpixel((x+col, y+line)) == (255,255,255)) for line in range(8)]
        if any(bits): rows[c] = bits
    return [rows.get(chr(c), rows['-']) for c in range(32,128)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--preview-dir', type=Path, default=Path('/tmp/geekmagic-title-assets'))
    args = parser.parse_args()
    original = Image.open(SOURCE).convert('RGB')
    assert original.size == (256,224)
    image = original.copy()
    sky = original.getpixel((0,0))
    image.paste(sky, (40,140,216,156))
    image.paste(sky, (92,173,188,185))
    # This rectangle is entirely inside the original left green hill.
    image.paste(original.getpixel((30,190)), (39,183,58,200))
    image = image.resize((240,240), Image.Resampling.NEAREST)
    colors = sorted(set(image.get_flattened_data()))
    palette = sorted(set(map(rgb565, colors)))
    assert len(palette) <= 256
    lookup = {c:i for i,c in enumerate(palette)}
    pixels = [lookup[rgb565(c)] for c in image.get_flattened_data()]
    data, offsets = bytearray(), [0]
    for y in range(240):
        row = pixels[y*240:(y+1)*240]
        x = 0
        while x < 240:
            count = 1
            while x+count < 240 and row[x+count] == row[x]: count += 1
            data.extend([count,row[x]]); x += count
        offsets.append(len(data))
    decoded = []
    for y in range(240):
        row=[]
        for at in range(offsets[y],offsets[y+1],2): row.extend([data[at+1]]*data[at])
        assert len(row)==240
        decoded.extend(row)
    assert decoded == pixels
    # Verify the logo region is an untouched nearest-neighbour rendering.
    retained = original.resize((240,240),Image.Resampling.NEAREST)
    for y in range(26,128):
        for x in range(240): assert image.getpixel((x,y)) == retained.getpixel((x,y))
    fonts = font(original)
    out=['#pragma once', '#include <Arduino.h>', '',
         '// Generated from the supplied reference; regenerate with tools/generate_mario_title_assets.py.',
         'constexpr uint16_t MARIO_TITLE_WIDTH = 240;', 'constexpr uint16_t MARIO_TITLE_HEIGHT = 240;',
         f'constexpr uint16_t MARIO_TITLE_SKY_565 = 0x{rgb565(sky):04x};',
         f'const uint16_t MARIO_TITLE_PALETTE_565[{len(palette)}] PROGMEM = {{' + ', '.join(f'0x{v:04x}' for v in palette) + '};',
         'const uint32_t MARIO_TITLE_ROW_OFFSETS[241] PROGMEM = {']
    out.extend('    '+', '.join(str(v) for v in offsets[i:i+16])+',' for i in range(0,len(offsets),16))
    out += ['};', f'const uint8_t MARIO_TITLE_BACKGROUND_RLE[{len(data)}] PROGMEM = {{']
    out.extend('    '+', '.join(f'0x{v:02x}' for v in data[i:i+20])+',' for i in range(0,len(data),20))
    out += ['};', 'const uint8_t MARIO_TITLE_FONT[96][8] PROGMEM = {']
    out.extend('    {'+', '.join(f'0x{v:02x}' for v in row)+'}, // '+str(i+32) for i,row in enumerate(fonts))
    out += ['};', 'static inline uint8_t titleGlyphRow(char value, uint8_t row) {',
            '    if (row >= 8) return 0;', '    uint8_t c = uint8_t(value);',
            "    if (c >= 'a' && c <= 'z') c -= 32;", "    if (c < 32 || c > 127) c = '-';",
            '    return pgm_read_byte(&MARIO_TITLE_FONT[c - 32][row]);', '}', '']
    (ROOT/'src/mario_title_assets.h').write_text('\n'.join(out))
    args.preview_dir.mkdir(parents=True,exist_ok=True)
    image.save(args.preview_dir/'background.png')
    print(f'PASS: 240 rows RLE roundtrip; {len(palette)} colors; {len(data)} RLE bytes; logo pixels unchanged.')
    print(f'Background preview: {args.preview_dir / "background.png"}')


if __name__ == '__main__': main()
