#!/usr/bin/env python3
"""Build hand-authored palette-indexed pixel sprites for the retro clock.

No ROM, emulator, downloaded game graphics, or runtime image decoding is used.
Run from GeekClock: .venv/bin/python firmware/tools/generate_mario_assets.py
"""
from pathlib import Path
import argparse

ROOT = Path(__file__).resolve().parents[1]
PALETTE = [
    (0, 0, 0),         # 0 transparent
    (181, 49, 32),     # 1 red cap / overalls
    (107, 109, 0),     # 2 olive-brown shirt / hair
    (230, 154, 37),    # 3 skin
    (28, 24, 28),      # 4 dark outline
    (252, 252, 252),   # 5 white
    (128, 208, 16),    # 6 bright green
    (0, 88, 0),        # 7 dark green
    (252, 216, 80),    # 8 gold
    (156, 74, 0),      # 9 goomba brown
    (252, 184, 104),   # A tan
    (184, 248, 24),    # B lime highlight
    (112, 116, 116),   # C gray shade
    (172, 196, 212),   # D cloud shadow
]
CHARS = {'.': 0, 'R': 1, 'B': 2, 'S': 3, 'X': 4, 'W': 5,
         'G': 6, 'D': 7, 'Y': 8, 'M': 9, 'T': 10, 'L': 11, 'C': 12, 'H': 13}


def grid(rows, width=16, height=16):
    assert len(rows) == height, (len(rows), height)
    assert all(len(row) <= width for row in rows), rows
    return width, height, [CHARS[c] for row in rows for c in row.ljust(width, '.')]


IDLE = [
    '.....RRRRR',
    '....RRRRRRRRR',
    '....BBSSBS',
    '...BSBSSSBSSS',
    '...BSBBSSSBSSSS',
    '...BBSSSSBBBB',
    '.....SSSSSSS',
    '....BBRBBB',
    '...BBBRBBRBBB',
    '..BBBBRRRRBBBB',
    '..SSBRYRYRBSS',
    '..SSSRRRRRRSSS',
    '..SSRRRRRRRRSS',
    '....RRR..RRR',
    '...BBB....BBB',
    '..BBBB....BBBB',
]
RUN1 = [
    '.....RRRRR',
    '....RRRRRRRRR',
    '....BBSSBS',
    '...BSBSSSBSSS',
    '...BSBBSSSBSSSS',
    '...BBSSSSBBBB',
    '.....SSSSSSS',
    '....BBRBBB',
    '...BBBRBBRBBSS',
    '..BBBBRRRRBSSS',
    '..SSBRYRYRRSS',
    '...SSRRRRRRR',
    '....RRRRRRRRB',
    '...BBRR..RBBB',
    '..BBB......BBB',
    '...BB',
]
RUN2 = [
    '.....RRRRR',
    '....RRRRRRRRR',
    '....BBSSBS',
    '...BSBSSSBSSS',
    '...BSBBSSSBSSSS',
    '...BBSSSSBBBB',
    '.....SSSSSSS',
    '....BBRBBB',
    '..SSBBRBBRBBB',
    '.SSSBBRRRRBBBB',
    '..SSRRRYRYRBSS',
    '....RRRRRRRSSS',
    '...BRRRRRRRR',
    '..BBBR..RRBB',
    '.BBB......BBB',
    '...........BB',
]
RUN3 = [
    '.....RRRRR',
    '....RRRRRRRRR',
    '....BBSSBS',
    '...BSBSSSBSSS',
    '...BSBBSSSBSSSS',
    '...BBSSSSBBBB',
    '.....SSSSSSS',
    '....BBRBBB',
    '...BBBRBBRBBB',
    '...BBBRRRRBBB',
    '...SSRYRYRSS',
    '...SSRRRRRSS',
    '.....RRRRRR',
    '.....BRRRB',
    '....BBB.BBB',
    '....BBB.BBB',
]
JUMP = [
    '.....RRRRR...SS',
    '....RRRRRRRRRSSS',
    '....BBSSBS..SSSS',
    '...BSBSSSBSSBBBB',
    '...BSBBSSSBSSBBB',
    '...BBSSSSBBBBBB',
    '.....SSSSSSSBB',
    '...BBBBRBBB.B',
    '..BBBBBRBBRRB',
    '.SSSBBRRRRRRR..B',
    '.SSSSRYRYRRRRBBB',
    '..SSRRRRRRRRRBBB',
    '....RRRRRRRRBBBB',
    '...BBBRRRRRB',
    '..BBBB',
    '..BBB',
]
GOOMBA1 = [
    '......MMMM',
    '.....MMMMMM',
    '....MMMMMMMM',
    '...MMMMMMMMMM',
    '..MMMMMMMMMMMM',
    '..MMXXMMMMXXMM',
    '.MMMWWXMMXWWMMM',
    '.MMMWWXMMXWWMMM',
    '.MMMMMMMMMMMMMM',
    '....TTTTTTTT',
    '...TTTTTTTTTT',
    '...TTTTTTTTTT',
    '..XXXTTTTTTXXX',
    '.XXXXXT TXXXXX'.replace(' ', 'T'),
    '.XXXXX....XXXXX',
    '..XXX......XXX',
]
GOOMBA2 = GOOMBA1[:12] + [
    '...XXTTTTTTXX',
    '..XXXXTTTTXXXX',
    '...XXXX..XXXX',
    '...XXXX..XXXX',
]
KOOPA1 = [
    '.......YYYY',
    '......YYYYYY',
    '......YXXYYY',
    '......YWXYYYY',
    '......YYYYYYYY',
    '.......YYYYYYY',
    '........YYYYY',
    '.......YYYY',
    '.....DDDDYYY',
    '....DGGGGDYY',
    '...DGGWWGGDY',
    '..DGGWGGWGGD',
    '..DGWGGGGWGD',
    '..DGWGGGGWGD',
    '..DGGWGGWGGD',
    '...DGGWWGGD',
    '...DDGGGGDD',
    '...YDDDDDDY',
    '...YYYYYYYY',
    '....YY..YY',
    '...YYY...YYY',
    '..YYYY....YYY',
    '..YYYY....YYY',
    '...YY',
]
KOOPA2 = KOOPA1[:19] + [
    '....YY..YY',
    '.....YYY..YYY',
    '......YYY..YYYY',
    '......YYY..YYYY',
    '.............YY',
]
PIRANHA = [
    '...GG....GG',
    '..GWWG..GWWG',
    '.GWWWWGGWWWWG',
    '.GWXXWWWWXXWG',
    '.GWWXXWWXXWWG',
    '..GWWXXXXWWG',
    '..GGWWXXWWGG',
    '...GGWWWWGG',
    '....GGWWGG',
    '.....GGGG',
    '......DD',
    '......DD',
    '..GG..DD..GG',
    '.GLLG.DD.GLLG',
    '.GLLLGDDGLLLG',
    '..GLLGDDGLLG',
    '...GG.DD.GG',
    '......DD',
    '......DD',
    '......DD',
    '......DD',
    '......DD',
    '......DD',
    '......DD',
]


def coin(frame):
    rows = []
    halfwidth = [3, 2, 1, 2][frame]
    for y in range(16):
        row = ['.'] * 8
        taper = 2 if y in (0, 15) else 1 if y in (1, 14) else 0
        radius = max(0, halfwidth - taper)
        for x in range(8):
            if abs(x - 3.5) <= radius + .5:
                edge = abs(x - 3.5) > max(0, radius - 1) + .5
                row[x] = 'M' if edge else 'Y'
                if not edge and x == 3: row[x] = 'W'
        rows.append(''.join(row))
    return grid(rows, 8, 16)


def rounded_shape(width, height, lobes, base, fill, shade):
    mask = set()
    for y in range(height):
        for x in range(width):
            if any((x-cx)**2*ry**2 + (y-cy)**2*rx**2 <= rx**2*ry**2 for cx,cy,rx,ry in lobes):
                if y <= base: mask.add((x, y))
    pixels = []
    for y in range(height):
        for x in range(width):
            if (x,y) not in mask: pixels.append(0); continue
            boundary = any((x+dx,y+dy) not in mask for dx,dy in [(0,-1),(0,1),(-1,0),(1,0)])
            color = 4 if boundary else shade if y > base-4 else fill
            pixels.append(color)
    return width, height, pixels


def emit(header, name, sprite):
    width,height,pixels = sprite
    assert len(pixels) == width*height
    assert all(0 <= n < len(PALETTE) for n in pixels)
    header.append(f'const uint8_t {name}_PIXELS[{len(pixels)}] PROGMEM = {{')
    for offset in range(0,len(pixels),width):
        header.append('    '+', '.join(f'{n:2}' for n in pixels[offset:offset+width])+',')
    header.append('};')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--preview',type=Path,default=None)
    args = parser.parse_args()
    sprites = {
        'MARIO_IDLE': grid(IDLE),
        'MARIO_JUMP': grid(JUMP),
        **{f'MARIO_RUN_{i}': grid(rows) for i,rows in enumerate([RUN1,RUN2,RUN3])},
        **{f'GOOMBA_{i}': grid(rows) for i,rows in enumerate([GOOMBA1,GOOMBA2])},
        **{f'KOOPA_{i}': grid(rows,16,24) for i,rows in enumerate([KOOPA1,KOOPA2])},
        **{f'COIN_{i}': coin(i) for i in range(4)},
        'CLOUD': rounded_shape(48,24,[(11,15,10,6),(20,9,10,9),(29,13,11,7),(38,16,9,5)],20,5,13),
        'BUSH': rounded_shape(32,16,[(7,11,7,5),(15,7,8,8),(24,11,7,5)],15,6,7),
        'PIRANHA': grid(PIRANHA,16,24),
    }
    header=['#pragma once','#include <Arduino.h>','#include <pgmspace.h>',
            '// Hand-authored pixel maps, generated by tools/generate_mario_assets.py.',
            '// Index 0 is transparent; all sprites are row-major palette indices.',
            'struct MarioSprite { const uint8_t* pixels; uint8_t w, h; };',
            f'const uint16_t MARIO_PALETTE_565[{len(PALETTE)}] PROGMEM = {{']
    header.append('    '+', '.join(f'0x{((r&248)<<8)|((g&252)<<3)|(b>>3):04X}' for r,g,b in PALETTE)+',')
    header.append('};')
    for name,sprite in sprites.items():emit(header,name,sprite)
    for name in ['MARIO_IDLE','MARIO_JUMP','CLOUD','BUSH','PIRANHA']:
        w,h,_=sprites[name];header.append(f'const MarioSprite {name} PROGMEM = {{ {name}_PIXELS, {w}, {h} }};')
    for name,count in [('MARIO_RUN',3),('GOOMBA',2),('KOOPA',2),('COIN',4)]:
        header.append(f'const MarioSprite {name}[{count}] PROGMEM = {{')
        for i in range(count):
            w,h,_=sprites[f'{name}_{i}'];header.append(f'    {{ {name}_{i}_PIXELS, {w}, {h} }},')
        header.append('};')
    destination=ROOT/'src/mario_assets.h';destination.write_text('\n'.join(header).rstrip()+'\n')
    if args.preview:
        from PIL import Image,ImageDraw
        args.preview.parent.mkdir(parents=True,exist_ok=True)
        canvas=Image.new('RGB',(720,480),(174,220,241));draw=ImageDraw.Draw(canvas)
        for i,(name,(w,h,pixels)) in enumerate(sprites.items()):
            im=Image.new('RGBA',(w,h));im.putdata([PALETTE[n]+(255 if n else 0,) for n in pixels])
            x=(i%4)*180+8;y=(i//4)*120+8
            canvas.paste(im.resize((w*3,h*3),Image.Resampling.NEAREST),(x,y),im.resize((w*3,h*3),Image.Resampling.NEAREST))
            draw.text((x,y+80),name,fill=(0,0,0))
        canvas.save(args.preview)
    print(f'Generated {destination}: {len(sprites)} sprites, {sum(len(s[2]) for s in sprites.values())} indexed pixel bytes')


if __name__=='__main__': main()
