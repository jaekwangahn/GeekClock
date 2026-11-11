#!/usr/bin/env python3
"""
Generates C++ PROGMEM header (src/chiikawa_assets.h) for the standalone ESP8266 GeekMagic firmware.
Reuses chiikawa_art.py so the character sprites, weather icons, rounded clock digits,
and Korean AppleSDGothicNeo UI glyphs match the Python Studio renderer pixel-for-pixel.
"""

from __future__ import annotations
import os
import sys
from typing import Dict, List, Tuple
from PIL import Image, ImageDraw

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, ROOT_DIR)

import chiikawa_art as art  # noqa: E402

OUT_HEADER = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "src", "chiikawa_assets.h"))


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def encode_rle6(indices: List[int]) -> bytes:
    """
    Compact RLE for 6-bit palette indices (0..63):
    - Top 2 bits = 0, 1, 2 -> run length = 1, 2, 3 of (byte & 0x3F) in 1 byte.
    - Top 2 bits = 3 (0xC0 | idx) -> followed by 1 byte (run - 4), encoding runs of 4..259 in 2 bytes.
    """
    out = bytearray()
    n = len(indices)
    i = 0
    while i < n:
        val = indices[i] & 0x3F
        run = 1
        while i + run < n and (indices[i + run] & 0x3F) == val and run < 259:
            run += 1
        i += run
        while run > 0:
            if run <= 3:
                out.append(((run - 1) << 6) | val)
                run = 0
            else:
                chunk = min(run, 259)
                out.append(0xC0 | val)
                out.append(chunk - 4)
                run -= chunk
    return bytes(out)


def render_character_frames_rgba(character: str, mood: str = "happy") -> Tuple[int, int, List[Image.Image]]:
    """
    Renders 4 animation frames at 2x supersampling and downsamples with LANCZOS to 240x240 space.
    Single characters fit cleanly in 116x108; Trio fits in 212x104.
    """
    if character == "trio":
        w, h = 212, 104
    else:
        w, h = 116, 108

    ss_w, ss_h = w * 2, h * 2
    frames: List[Image.Image] = []

    for idx in range(4):
        canvas = Image.new("RGBA", (ss_w, ss_h), (0, 0, 0, 0))
        draw = ImageDraw.Draw(canvas)
        if character == "trio":
            art.draw_single_character(draw, "hachiware", cx=50, cy=114, scale=0.68, mood=mood, frame_idx=idx)
            art.draw_single_character(draw, "usagi", cx=ss_w - 50, cy=116, scale=0.66, mood=mood, frame_idx=idx + 2)
            art.draw_single_character(draw, "chiikawa", cx=ss_w // 2, cy=120, scale=0.78, mood=mood, frame_idx=idx + 1)
        else:
            cy = 120 if character == "usagi" else 110
            sc = 0.80 if character == "usagi" else 0.88
            art.draw_single_character(draw, character, cx=ss_w // 2, cy=cy, scale=sc, mood=mood, frame_idx=idx)

        small = canvas.resize((w, h), Image.Resampling.LANCZOS)
        frames.append(small)

    return w, h, frames


def render_weather_icons_rgba() -> Tuple[int, int, Dict[str, Image.Image]]:
    w, h = 34, 30
    ss_w, ss_h = w * 2, h * 2
    codes = ["sunny", "partly_cloudy", "cloudy", "rain", "snow"]
    icons: Dict[str, Image.Image] = {}
    for code in codes:
        canvas = Image.new("RGBA", (ss_w, ss_h), (0, 0, 0, 0))
        draw = ImageDraw.Draw(canvas)
        art.draw_weather_icon(draw, cx=ss_w // 2, cy=ss_h // 2 + 2, weather_code=code, scale=1.0, frame_idx=0)
        icons[code] = canvas.resize((w, h), Image.Resampling.LANCZOS)
    return w, h, icons


def build_shared_palette_and_compress() -> Tuple[List[int], Dict[str, Tuple[int, int, List[bytes]]], Dict[str, bytes]]:
    """
    Builds a single shared 64-entry palette across all characters & weather icons:
    - Index 0: Fully transparent
    - Indices 1..48: Top 48 opaque RGB565 colors from quantized sprites
    - Indices 49..63: 15 semi-transparent alpha levels (16..240) of Chiikawa outline (54, 42, 46)
      so character edges blend smoothly over ANY theme background!
    """
    char_names = ["chiikawa", "hachiware", "usagi", "trio"]
    char_rgba: Dict[str, Tuple[int, int, List[Image.Image]]] = {}
    for cname in char_names:
        char_rgba[cname] = render_character_frames_rgba(cname, mood="happy")

    ww, wh, weather_rgba = render_weather_icons_rgba()

    # Collect all opaque pixels (alpha >= 200) onto a strip image to quantize cleanly to 48 colors
    opaque_samples: List[Tuple[int, int, int]] = []
    for _, (_, _, flist) in char_rgba.items():
        for im in flist:
            for r, g, b, a in im.getdata():
                if a >= 180:
                    # Un-premultiply if slightly translucent near outline
                    opaque_samples.append((r, g, b))
    for _, im in weather_rgba.items():
        for r, g, b, a in im.getdata():
            if a >= 140:
                opaque_samples.append((r, g, b))

    sample_w = 256
    sample_h = max(1, (len(opaque_samples) + sample_w - 1) // sample_w)
    strip = Image.new("RGB", (sample_w, sample_h), (255, 255, 255))
    strip.putdata(opaque_samples + [(255, 255, 255)] * (sample_w * sample_h - len(opaque_samples)))

    quant = strip.quantize(colors=46, method=Image.Quantize.MEDIANCUT)
    raw_pal = quant.getpalette()[: 46 * 3]
    palette_rgb: List[Tuple[int, int, int]] = [(0, 0, 0)]  # idx 0 = transparent

    # Force exact pure white & exact outline dark brown to always exist cleanly
    forced = [(255, 255, 255), (54, 42, 46)]
    for fc in forced:
        palette_rgb.append(fc)
    for i in range(0, len(raw_pal), 3):
        c = (raw_pal[i], raw_pal[i + 1], raw_pal[i + 2])
        if len(palette_rgb) < 49:
            palette_rgb.append(c)
    while len(palette_rgb) < 49:
        palette_rgb.append((255, 255, 255))

    def nearest_opaque_idx(r: int, g: int, b: int) -> int:
        best_i = 1
        best_d = 10**9
        for idx in range(1, 49):
            pr, pg, pb = palette_rgb[idx]
            d = (r - pr) * (r - pr) + (g - pg) * (g - pg) + (b - pb) * (b - pb)
            if d < best_d:
                best_d = d
                best_i = idx
                if d == 0:
                    break
        return best_i

    def image_to_rle(im: Image.Image, is_weather: bool = False) -> bytes:
        idxs: List[int] = []
        for r, g, b, a in im.getdata():
            if a < 18:
                idxs.append(0)
            elif a >= 228:
                idxs.append(nearest_opaque_idx(r, g, b))
            else:
                if not is_weather and r < 140 and g < 130 and b < 135:
                    # Semi-transparent dark outline -> map alpha (18..227) to 49..63
                    step = min(14, max(0, (a - 16) * 15 // 224))
                    idxs.append(49 + step)
                elif a >= 115:
                    idxs.append(nearest_opaque_idx(r, g, b))
                else:
                    # Light translucent edge on weather icon -> map soft alpha outline or skip
                    if not is_weather:
                        step = min(14, max(0, (a * 8) // 255))
                        idxs.append(49 + step)
                    else:
                        idxs.append(0)
        return encode_rle6(idxs)

    compressed_chars: Dict[str, Tuple[int, int, List[bytes]]] = {}
    for cname, (cw, ch, flist) in char_rgba.items():
        compressed_chars[cname] = (cw, ch, [image_to_rle(f, is_weather=False) for f in flist])

    compressed_weather: Dict[str, bytes] = {}
    for code, im in weather_rgba.items():
        compressed_weather[code] = image_to_rle(im, is_weather=True)

    palette_565 = [rgb888_to_rgb565(r, g, b) for (r, g, b) in palette_rgb]
    return palette_565, compressed_chars, (ww, wh, compressed_weather)


def generate_4bit_font(
    charset: str,
    font_size: int,
    weight: str = "bold",
    rounded: bool = False,
) -> Tuple[int, List[Tuple[int, int, int, int, int, int, int]], bytes]:
    """
    Renders anti-aliased 4-bit grayscale alpha glyphs (2x supersampled then LANCZOS downsampled!).
    Returns:
      line_height,
      glyph_entries: list of (codepoint, width, height, xoffset, yoffset, xadvance, data_offset),
      packed_bitmap_bytes (2 pixels per byte, high nibble first).
    """
    ss = 2
    font_ss = art.load_font(font_size * ss, weight=weight, rounded_digits=rounded)
    dummy = Image.new("L", (8, 8), 0)
    ddraw = ImageDraw.Draw(dummy)

    unique_chars = sorted(set(charset), key=lambda ch: ord(ch))
    glyph_entries: List[Tuple[int, int, int, int, int, int, int]] = []
    bitmap_pool = bytearray()

    for ch in unique_chars:
        cp = ord(ch)
        if ch == " ":
            adv = max(4, font_size // 3)
            glyph_entries.append((cp, 0, 0, 0, 0, adv, len(bitmap_pool)))
            continue

        bbox = ddraw.textbbox((32, 32), ch, font=font_ss)
        bw_ss = max(2, bbox[2] - bbox[0] + 4)
        bh_ss = max(2, bbox[3] - bbox[1] + 4)

        pad_x = 32 - bbox[0] + 2
        pad_y = 32 - bbox[1] + 2
        im_ss = Image.new("L", (bw_ss, bh_ss), 0)
        idraw = ImageDraw.Draw(im_ss)
        idraw.text((pad_x, pad_y), ch, font=font_ss, fill=255)

        tw = max(1, (bw_ss + 1) // ss)
        th = max(1, (bh_ss + 1) // ss)
        im = im_ss.resize((tw, th), Image.Resampling.LANCZOS)

        # Trim empty border pixels
        nz = im.getbbox()
        if not nz:
            adv = max(4, font_size // 3)
            glyph_entries.append((cp, 0, 0, 0, 0, adv, len(bitmap_pool)))
            continue

        im_crop = im.crop(nz)
        gw, gh = im_crop.size
        xoff = (bbox[0] - 32 - 2) // ss + nz[0]
        yoff = (bbox[1] - 32 - 2) // ss + nz[1]

        adv_ss = ddraw.textlength(ch, font=font_ss)
        xadv = max(gw + max(0, xoff), int(round(adv_ss / ss)))

        offset = len(bitmap_pool)
        pixels = list(im_crop.getdata())
        # Pack 2 4-bit alpha nibbles per byte
        for i in range(0, len(pixels), 2):
            hi = (pixels[i] + 8) >> 4
            if hi > 15:
                hi = 15
            lo = 0
            if i + 1 < len(pixels):
                lo = (pixels[i + 1] + 8) >> 4
                if lo > 15:
                    lo = 15
            bitmap_pool.append((hi << 4) | lo)

        glyph_entries.append((cp, gw, gh, xoff, yoff, xadv, offset))

    return font_size + 4, glyph_entries, bytes(bitmap_pool)


def format_c_bytes(data: bytes, indent: str = "    ") -> str:
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        lines.append(indent + ", ".join(f"0x{b:02X}" for b in chunk))
    return ",\n".join(lines)


def main() -> None:
    palette_565, chars, (ww, wh, weather) = build_shared_palette_and_compress()

    # 1. Big Rounded Clock Font (40px)
    _, time_glyphs, time_bmp = generate_4bit_font("0123456789: ", font_size=40, weight="heavy", rounded=True)

    # 2. Medium Rounded Temperature / Sub-clock Font (21px)
    _, temp_glyphs, temp_bmp = generate_4bit_font(
        "0123456789+-./:%°AMP ", font_size=21, weight="heavy", rounded=True
    )

    # 3. Korean + ASCII UI Font (13px AppleSDGothicNeo Bold)
    ascii_chars = "".join(chr(c) for c in range(0x20, 0x7F))
    ko_chars = (
        "°♡♥★☆♪·…✓"
        "년월일시분초오전후요일화수목금토"
        "서울부산인천대구대전광주울산수원성남고양용인창원청주전주천안제주도쿄오사카뉴욕런던파리"
        "맑음구름조금흐림비눈소나기번개안개최고저습도현재날씨기온체감"
        "연결중설정접속시간동기화완료대기밝기모드테마캐릭터저장자동켜짐꺼펌웨어업데이트정상오류"
        "오늘도화이팅행복해웃자힘내사랑해치이카와하레우사기삼총사모모가"
        "토벌성공야하응앗맛있는거먹자푹쉬자좋은하루보내요기념생휴가여행시험월급날주말평일"
        "재광님짱최강귀여운시계별빛파자마나이트소다블루피치핑크커스터드옐로우라벤더드림"
    )
    _, ui_glyphs, ui_bmp = generate_4bit_font(ascii_chars + ko_chars, font_size=13, weight="bold", rounded=False)

    # 4. Small Badge/Sub Font (11px AppleSDGothicNeo Bold)
    _, sub_glyphs, sub_bmp = generate_4bit_font(
        ascii_chars + "°♡★월일화수목금토최고저습도오전후서울부산인천대구대전광주수원성남제주도쿄동기화완료연결중",
        font_size=10,
        weight="semibold",
        rounded=False,
    )

    total_sprite_bytes = sum(len(b) for _, (_, _, fl) in chars.items() for b in fl) + sum(
        len(b) for b in weather.values()
    )
    total_font_bytes = len(time_bmp) + len(temp_bmp) + len(ui_bmp) + len(sub_bmp)

    out = []
    out.append("#pragma once")
    out.append("#include <Arduino.h>")
    out.append("#include <pgmspace.h>")
    out.append("")
    out.append("// Auto-generated by firmware/tools/generate_assets.py — DO NOT EDIT MANUALLY")
    out.append(f"// Sprites RLE total: {total_sprite_bytes} bytes | Fonts 4-bit alpha total: {total_font_bytes} bytes")
    out.append("")
    out.append("struct Glyph4Bit {")
    out.append("    uint16_t codepoint;")
    out.append("    uint8_t width;")
    out.append("    uint8_t height;")
    out.append("    int8_t xOffset;")
    out.append("    int8_t yOffset;")
    out.append("    uint8_t xAdvance;")
    out.append("    uint16_t bitmapOffset;")
    out.append("};")
    out.append("")
    out.append("struct Font4Bit {")
    out.append("    uint8_t lineHeight;")
    out.append("    uint16_t glyphCount;")
    out.append("    const Glyph4Bit* glyphs;")
    out.append("    const uint8_t* bitmap;")
    out.append("};")
    out.append("")
    out.append("struct SpriteFrameRLE {")
    out.append("    uint16_t len;")
    out.append("    const uint8_t* data;")
    out.append("};")
    out.append("")

    # Palette (49 entries: 0=transparent, 1..48=RGB565)
    out.append("const uint16_t SPRITE_PALETTE_565[49] PROGMEM = {")
    out.append("    " + ", ".join(f"0x{c:04X}" for c in palette_565))
    out.append("};")
    out.append("")

    # Characters
    for cname in ["chiikawa", "hachiware", "usagi", "trio"]:
        cw, ch, flist = chars[cname]
        for idx, bdata in enumerate(flist):
            out.append(f"const uint8_t SPR_{cname.upper()}_F{idx}[{len(bdata)}] PROGMEM = {{")
            out.append(format_c_bytes(bdata))
            out.append("};")
        out.append(f"const SpriteFrameRLE CHAR_FRAMES_{cname.upper()}[4] PROGMEM = {{")
        for idx, bdata in enumerate(flist):
            out.append(f"    {{ {len(bdata)}, SPR_{cname.upper()}_F{idx} }},")
        out.append("};")
        out.append(f"constexpr uint16_t CHAR_{cname.upper()}_W = {cw};")
        out.append(f"constexpr uint16_t CHAR_{cname.upper()}_H = {ch};")
        out.append("")

    # Weather icons
    out.append(f"constexpr uint16_t WEATHER_ICON_W = {ww};")
    out.append(f"constexpr uint16_t WEATHER_ICON_H = {wh};")
    w_order = ["sunny", "partly_cloudy", "cloudy", "rain", "snow"]
    for code in w_order:
        bdata = weather[code]
        out.append(f"const uint8_t SPR_WEATHER_{code.upper()}[{len(bdata)}] PROGMEM = {{")
        out.append(format_c_bytes(bdata))
        out.append("};")
    out.append("const SpriteFrameRLE WEATHER_SPRITES[5] PROGMEM = {")
    for code in w_order:
        bdata = weather[code]
        out.append(f"    {{ {len(bdata)}, SPR_WEATHER_{code.upper()} }},")
    out.append("};")
    out.append("")

    # Fonts helper
    def emit_font(name: str, line_h: int, glyphs: list, bmp: bytes) -> None:
        out.append(f"const uint8_t {name}_BITMAP[{max(1, len(bmp))}] PROGMEM = {{")
        out.append(format_c_bytes(bmp if bmp else b"\x00"))
        out.append("};")
        out.append(f"const Glyph4Bit {name}_GLYPHS[{len(glyphs)}] PROGMEM = {{")
        for cp, gw, gh, xo, yo, xa, off in glyphs:
            out.append(f"    {{ {cp}, {gw}, {gh}, {xo}, {yo}, {xa}, {off} }},")
        out.append("};")
        out.append(f"const Font4Bit {name} PROGMEM = {{ {line_h}, {len(glyphs)}, {name}_GLYPHS, {name}_BITMAP }};")
        out.append("")

    emit_font("FONT_TIME_40", 42, time_glyphs, time_bmp)
    emit_font("FONT_TEMP_21", 23, temp_glyphs, temp_bmp)
    emit_font("FONT_UI_13", 16, ui_glyphs, ui_bmp)
    emit_font("FONT_SUB_10", 13, sub_glyphs, sub_bmp)

    os.makedirs(os.path.dirname(OUT_HEADER), exist_ok=True)
    with open(OUT_HEADER, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")

    print(f"Generated {OUT_HEADER}")
    print(f"  Character + Weather RLE Sprites: {total_sprite_bytes:,} bytes")
    print(f"  4-Bit Anti-Aliased Fonts:        {total_font_bytes:,} bytes")
    print(f"  Total PROGMEM Asset Payload:     {total_sprite_bytes + total_font_bytes:,} bytes")


if __name__ == "__main__":
    main()
