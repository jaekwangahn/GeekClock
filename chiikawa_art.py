"""
Chiikawa Screen Renderer for GeekMagic 240x240 IPS Clock.
Uses 2x supersampling (480x480 -> 240x240 LANCZOS) for crisp anti-aliased graphics
and supports both static Baseline JPEG (subsampling=0) and animated GIF output.
"""

from __future__ import annotations
import io
import math
import os
from datetime import datetime, date
from typing import Any, Dict, List, Optional, Tuple
from PIL import Image, ImageDraw, ImageFont, ImageOps, ImageSequence

SCREEN_SIZE = 240
SCALE = 2
CANVAS_SIZE = SCREEN_SIZE * SCALE  # 480x480 supersampled canvas

# Color Themes (Pastel palettes tailored for ST7789 IPS LCD vibrancy)
THEMES: Dict[str, Dict[str, Any]] = {
    "pink": {
        "name": "치이카와 피치핑크",
        "bg_top": (255, 232, 240),
        "bg_bottom": (255, 248, 250),
        "card_bg": (255, 255, 255, 230),
        "card_border": (255, 188, 210),
        "accent": (255, 105, 150),
        "accent_soft": (255, 212, 226),
        "text_main": (68, 46, 56),
        "text_sub": (140, 105, 118),
        "badge_bg": (255, 115, 158),
        "badge_text": (255, 255, 255),
    },
    "sky": {
        "name": "하치와레 소다블루",
        "bg_top": (218, 240, 255),
        "bg_bottom": (246, 252, 255),
        "card_bg": (255, 255, 255, 230),
        "card_border": (168, 216, 250),
        "accent": (72, 156, 228),
        "accent_soft": (205, 234, 255),
        "text_main": (42, 62, 84),
        "text_sub": (102, 128, 154),
        "badge_bg": (78, 162, 232),
        "badge_text": (255, 255, 255),
    },
    "butter": {
        "name": "우사기 커스터드옐로우",
        "bg_top": (255, 244, 200),
        "bg_bottom": (255, 252, 238),
        "card_bg": (255, 255, 255, 232),
        "card_border": (250, 214, 138),
        "accent": (235, 148, 45),
        "accent_soft": (255, 234, 180),
        "text_main": (78, 56, 34),
        "text_sub": (146, 116, 82),
        "badge_bg": (242, 156, 52),
        "badge_text": (255, 255, 255),
    },
    "lavender": {
        "name": "모모가 라벤더드림",
        "bg_top": (238, 226, 255),
        "bg_bottom": (250, 246, 255),
        "card_bg": (255, 255, 255, 230),
        "card_border": (206, 184, 248),
        "accent": (152, 108, 224),
        "accent_soft": (228, 214, 255),
        "text_main": (58, 44, 82),
        "text_sub": (122, 104, 152),
        "badge_bg": (156, 112, 228),
        "badge_text": (255, 255, 255),
    },
    "night": {
        "name": "별빛 파자마 나이트",
        "bg_top": (24, 28, 56),
        "bg_bottom": (46, 52, 92),
        "card_bg": (38, 44, 78, 225),
        "card_border": (96, 110, 172),
        "accent": (255, 198, 92),
        "accent_soft": (62, 72, 122),
        "text_main": (248, 246, 255),
        "text_sub": (182, 190, 226),
        "badge_bg": (255, 136, 172),
        "badge_text": (255, 255, 255),
    },
}

WEEKDAYS_KO = ["월", "화", "수", "목", "금", "토", "일"]


def load_font(size: int, weight: str = "bold", rounded_digits: bool = False) -> ImageFont.FreeTypeFont:
    """Loads macOS Korean / Rounded fonts with graceful fallbacks."""
    if rounded_digits:
        for path in [
            "/System/Library/Fonts/SFNSRounded.ttf",
            "/System/Library/Fonts/SFCompactRounded.ttf",
        ]:
            if os.path.exists(path):
                try:
                    return ImageFont.truetype(path, size)
                except Exception:
                    pass

    gothic_path = "/System/Library/Fonts/AppleSDGothicNeo.ttc"
    if os.path.exists(gothic_path):
        idx_map = {
            "regular": 0,
            "medium": 2,
            "semibold": 4,
            "bold": 6,
            "extrabold": 14,
            "heavy": 16,
        }
        idx = idx_map.get(weight, 6)
        try:
            return ImageFont.truetype(gothic_path, size, index=idx)
        except Exception:
            try:
                return ImageFont.truetype(gothic_path, size, index=0)
            except Exception:
                pass

    for fallback in [
        "/System/Library/Fonts/Supplemental/AppleGothic.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    ]:
        if os.path.exists(fallback):
            try:
                return ImageFont.truetype(fallback, size)
            except Exception:
                pass

    return ImageFont.load_default()


def _lerp_color(c1: Tuple[int, int, int], c2: Tuple[int, int, int], t: float) -> Tuple[int, int, int]:
    return (
        int(c1[0] + (c2[0] - c1[0]) * t),
        int(c1[1] + (c2[1] - c1[1]) * t),
        int(c1[2] + (c2[2] - c1[2]) * t),
    )


def draw_background(
    canvas: Image.Image,
    theme: Dict[str, Any],
    is_night: bool = False,
    frame_idx: int = 0,
) -> None:
    """Draws a soft pastel gradient background with subtle decorative polka dots or stars."""
    draw = ImageDraw.Draw(canvas)
    w, h = canvas.size
    c1, c2 = theme["bg_top"], theme["bg_bottom"]
    for y in range(h):
        c = _lerp_color(c1, c2, y / max(1, h - 1))
        draw.line([(0, y), (w, y)], fill=c)

    # Subtle decorative pattern
    if is_night:
        stars = [
            (48, 42, 4),
            (130, 28, 3),
            (340, 36, 5),
            (432, 68, 4),
            (64, 240, 4),
            (426, 220, 5),
            (38, 360, 3),
            (442, 340, 4),
        ]
        for i, (sx, sy, sr) in enumerate(stars):
            twinkle = ((frame_idx + i) % 2 == 0)
            r = sr + (2 if twinkle else 0)
            star_col = (255, 242, 165) if twinkle else (190, 200, 240)
            draw.line([(sx - r * 2, sy), (sx + r * 2, sy)], fill=star_col, width=2)
            draw.line([(sx, sy - r * 2), (sx, sy + r * 2)], fill=star_col, width=2)
            draw.ellipse([sx - r, sy - r, sx + r, sy + r], fill=(255, 252, 220))
    else:
        # Soft pastel polka dots / sparkles in background
        dot_col = _lerp_color(theme["bg_top"], theme["card_border"], 0.35)
        for gx in range(36, w, 72):
            for gy in range(36, h, 72):
                offset = 18 if (gy // 72) % 2 else 0
                px = gx + offset
                if 0 < px < w:
                    draw.ellipse([px - 4, gy - 4, px + 4, gy + 4], fill=dot_col)


def draw_weather_icon(
    draw: ImageDraw.ImageDraw,
    cx: int,
    cy: int,
    weather_code: str = "sunny",
    scale: float = 1.0,
    frame_idx: int = 0,
) -> None:
    """Draws a cute vector weather icon at (cx, cy)."""
    s = scale
    if weather_code in ("clear", "sunny"):
        # Cute Sun
        r = int(18 * s)
        ray_len = int(27 * s)
        ray_in = int(21 * s)
        angle_offset = (frame_idx * 15) * math.pi / 180.0
        for i in range(8):
            ang = i * (math.pi / 4) + angle_offset
            x1 = cx + int(math.cos(ang) * ray_in)
            y1 = cy + int(math.sin(ang) * ray_in)
            x2 = cx + int(math.cos(ang) * ray_len)
            y2 = cy + int(math.sin(ang) * ray_len)
            draw.line([(x1, y1), (x2, y2)], fill=(255, 168, 38), width=max(2, int(4 * s)))
        draw.ellipse(
            [cx - r, cy - r, cx + r, cy + r],
            fill=(255, 208, 62),
            outline=(245, 158, 28),
            width=max(2, int(3 * s)),
        )
    elif weather_code in ("partly_cloudy", "cloudy"):
        # Sun peeking behind fluffy cloud
        if weather_code == "partly_cloudy":
            sr = int(14 * s)
            sx, sy = cx - int(10 * s), cy - int(8 * s)
            draw.ellipse(
                [sx - sr, sy - sr, sx + sr, sy + sr],
                fill=(255, 206, 68),
                outline=(242, 162, 36),
                width=max(2, int(3 * s)),
            )
        # Cloud puffs
        c_fill = (255, 255, 255) if weather_code == "partly_cloudy" else (232, 238, 246)
        c_out = (146, 172, 202)
        draw.ellipse([cx - int(22 * s), cy - int(4 * s), cx + int(22 * s), cy + int(16 * s)], fill=c_fill, outline=c_out, width=3)
        draw.ellipse([cx - int(16 * s), cy - int(16 * s), cx + int(6 * s), cy + int(8 * s)], fill=c_fill, outline=c_out, width=3)
        draw.ellipse([cx - int(2 * s), cy - int(12 * s), cx + int(18 * s), cy + int(10 * s)], fill=c_fill, outline=c_out, width=3)
        draw.rectangle([cx - int(18 * s), cy - int(4 * s), cx + int(18 * s), cy + int(13 * s)], fill=c_fill)
        draw.line([(cx - int(18 * s), cy + int(15 * s)), (cx + int(18 * s), cy + int(15 * s))], fill=c_out, width=3)
    elif weather_code in ("rain", "Rain", "drizzle", "thunderstorm"):
        # Cloud + raindrops
        c_fill = (216, 228, 244)
        c_out = (116, 146, 184)
        cy_c = cy - int(6 * s)
        draw.ellipse([cx - int(20 * s), cy_c - int(12 * s), cx + int(4 * s), cy_c + int(8 * s)], fill=c_fill, outline=c_out, width=3)
        draw.ellipse([cx - int(4 * s), cy_c - int(10 * s), cx + int(20 * s), cy_c + int(8 * s)], fill=c_fill, outline=c_out, width=3)
        draw.rectangle([cx - int(16 * s), cy_c - int(2 * s), cx + int(16 * s), cy_c + int(7 * s)], fill=c_fill)
        # Raindrops
        drop_shift = (frame_idx % 2) * int(4 * s)
        for dx in (-11, 0, 11):
            rx = cx + int(dx * s)
            ry = cy + int(10 * s) + drop_shift
            draw.line([(rx, ry), (rx - int(3 * s), ry + int(9 * s))], fill=(76, 156, 238), width=max(2, int(3.5 * s)))
    elif weather_code in ("snow",):
        # Snowflake / cloud
        c_fill = (235, 244, 255)
        c_out = (135, 168, 208)
        cy_c = cy - int(6 * s)
        draw.ellipse([cx - int(18 * s), cy_c - int(10 * s), cx + int(18 * s), cy_c + int(8 * s)], fill=c_fill, outline=c_out, width=3)
        for dx in (-10, 0, 10):
            sx = cx + int(dx * s)
            sy = cy + int(14 * s)
            draw.ellipse([sx - 3, sy - 3, sx + 3, sy + 3], fill=(110, 180, 250))


def _draw_blush_cheeks(draw: ImageDraw.ImageDraw, lx: int, rx: int, y: int, rw: int = 20, rh: int = 12, color=(255, 162, 184)) -> None:
    """Draws cute Chiikawa-style oval blush cheeks with tiny diagonal hatch lines."""
    for cx in (lx, rx):
        draw.ellipse([cx - rw, y - rh, cx + rw, y + rh], fill=color)
        # Signature Nagano-style cheek hatch lines
        line_col = (238, 102, 136)
        for offset in (-7, 0, 7):
            draw.line(
                [(cx + offset - 3, y - 5), (cx + offset + 3, y + 5)],
                fill=line_col,
                width=2,
            )


def _draw_character_face_details(
    draw: ImageDraw.ImageDraw,
    cx: int,
    cy: int,
    mood: str = "happy",
    frame_idx: int = 0,
    scale: float = 1.0,
) -> None:
    """Draws expressive Chiikawa eyes, eyebrows, blush, and mouth."""
    outline = (54, 42, 46)
    eye_dx = int(34 * scale)
    eye_y = cy - int(6 * scale)
    blush_dx = int(54 * scale)
    blush_y = cy + int(12 * scale)

    _draw_blush_cheeks(
        draw,
        cx - blush_dx,
        cx + blush_dx,
        blush_y,
        rw=int(20 * scale),
        rh=int(12 * scale),
    )

    blink = (frame_idx == 3) and mood not in ("wink", "sleep")

    if mood == "sleep":
        # Closed peaceful sleeping eyes (curved U lines)
        for ex in (cx - eye_dx, cx + eye_dx):
            r = int(12 * scale)
            draw.arc([ex - r, eye_y - r // 2, ex + r, eye_y + r], start=15, end=165, fill=outline, width=int(4 * scale))
    elif mood == "wink" or blink:
        # Happy > < or ^ ^ eyes
        for i, ex in enumerate((cx - eye_dx, cx + eye_dx)):
            r = int(11 * scale)
            if mood == "wink" and i == 1:
                draw.line([(ex - r, eye_y - int(6 * scale)), (ex + r // 2, eye_y)], fill=outline, width=int(4 * scale))
                draw.line([(ex + r // 2, eye_y), (ex - r, eye_y + int(6 * scale))], fill=outline, width=int(4 * scale))
            elif blink:
                draw.line([(ex - r, eye_y), (ex + r, eye_y)], fill=outline, width=int(4 * scale))
            else:
                er = int(11 * scale)
                draw.ellipse([ex - er, eye_y - er, ex + er, eye_y + er], fill=outline)
                # Dual white eye highlights (big top, small bottom — signature Chiikawa look!)
                hr1 = max(2, int(4.5 * scale))
                hr2 = max(1, int(2.5 * scale))
                draw.ellipse([ex - er // 2 - hr1 + 2, eye_y - er // 2 - hr1 + 1, ex - er // 2 + hr1 + 2, eye_y - er // 2 + hr1 + 1], fill=(255, 255, 255))
                draw.ellipse([ex + er // 3 - hr2, eye_y + er // 3 - hr2, ex + er // 3 + hr2, eye_y + er // 3 + hr2], fill=(255, 255, 255))
    else:
        # Signature round dark eyes with big top-left + tiny bottom-right white shine
        for ex in (cx - eye_dx, cx + eye_dx):
            er = int(11.5 * scale)
            draw.ellipse([ex - er, eye_y - er, ex + er, eye_y + er], fill=outline)
            hr1 = max(2, int(4.8 * scale))
            hr2 = max(2, int(2.5 * scale))
            draw.ellipse(
                [ex - int(3 * scale) - hr1, eye_y - int(4 * scale) - hr1, ex - int(3 * scale) + hr1, eye_y - int(4 * scale) + hr1],
                fill=(255, 255, 255),
            )
            draw.ellipse(
                [ex + int(4 * scale) - hr2, eye_y + int(4 * scale) - hr2, ex + int(4 * scale) + hr2, eye_y + int(4 * scale) + hr2],
                fill=(255, 255, 255),
            )
            if mood == "cry":
                # Cute watery tear drop at bottom of eye
                draw.ellipse(
                    [ex - int(6 * scale), eye_y + er - int(3 * scale), ex + int(6 * scale), eye_y + er + int(9 * scale)],
                    fill=(130, 210, 255),
                    outline=outline,
                    width=2,
                )

    # Small eyebrows
    brow_y = eye_y - int(19 * scale)
    for sign, ex in ((-1, cx - eye_dx), (1, cx + eye_dx)):
        if mood == "cry":
            draw.line(
                [(ex - int(9 * scale), brow_y + sign * int(4 * scale)), (ex + int(9 * scale), brow_y - sign * int(4 * scale))],
                fill=outline,
                width=max(2, int(3 * scale)),
            )
        else:
            draw.arc(
                [ex - int(10 * scale), brow_y - int(4 * scale), ex + int(10 * scale), brow_y + int(6 * scale)],
                start=200,
                end=340,
                fill=outline,
                width=max(2, int(3 * scale)),
            )

    # Cute W-shaped cat/hamster mouth ("ω") + pink tongue if happy/cheer
    my = cy + int(8 * scale)
    mw = int(11 * scale)
    mh = int(9 * scale)
    if mood in ("happy", "cheer", "eat"):
        # Open happy mouth under the ω
        draw.chord(
            [cx - mw, my - int(2 * scale), cx + mw, my + int(18 * scale)],
            start=0,
            end=180,
            fill=(242, 118, 142),
            outline=outline,
            width=max(2, int(3.2 * scale)),
        )
    # Draw the two little curves of 'ω'
    draw.arc([cx - mw, my - mh // 2, cx, my + mh], start=10, end=175, fill=outline, width=max(2, int(3.5 * scale)))
    draw.arc([cx, my - mh // 2, cx + mw, my + mh], start=5, end=170, fill=outline, width=max(2, int(3.5 * scale)))


def draw_single_character(
    draw: ImageDraw.ImageDraw,
    character: str,
    cx: int,
    cy: int,
    scale: float = 1.0,
    mood: str = "happy",
    frame_idx: int = 0,
) -> None:
    """
    Draws Chiikawa, Hachiware, or Usagi centered around (cx, cy).
    """
    outline = (54, 42, 46)
    lw = max(3, int(5 * scale))

    # Lively bounce animation across frames
    bounce_y = int(math.sin(frame_idx * math.pi / 2) * 14 * scale) if frame_idx else 0
    sway_x = int(math.cos(frame_idx * math.pi / 2) * 6 * scale) if frame_idx else 0
    cx = cx + sway_x
    cy = cy + bounce_y

    if character == "usagi":
        body_col = (255, 241, 198)
        ear_in = (255, 198, 206)
    else:
        body_col = (255, 255, 255)
        ear_in = (255, 198, 212) if character == "chiikawa" else (255, 192, 208)

    # 1. Ears (drawn behind head)
    if character == "chiikawa":
        # Round bear-like ears
        for sign in (-1, 1):
            ex = cx + sign * int(64 * scale)
            ey = cy - int(66 * scale)
            er = int(26 * scale)
            draw.ellipse([ex - er, ey - er, ex + er, ey + er], fill=body_col, outline=outline, width=lw)
            ir = int(14 * scale)
            draw.ellipse([ex - ir, ey - ir + int(3 * scale), ex + ir, ey + ir + int(3 * scale)], fill=ear_in)
    elif character == "hachiware":
        # Pointed cat-like ears
        for sign in (-1, 1):
            base_x = cx + sign * int(54 * scale)
            base_y = cy - int(52 * scale)
            tip_x = cx + sign * int(74 * scale)
            tip_y = cy - int(98 * scale)
            p1 = (base_x - sign * int(24 * scale), base_y)
            p2 = (tip_x, tip_y)
            p3 = (base_x + sign * int(18 * scale), base_y + int(18 * scale))
            draw.polygon([p1, p2, p3], fill=(94, 168, 228), outline=outline)
            draw.line([p1, p2, p3], fill=outline, width=lw, joint="curve")
            # Pink inner ear triangle
            ip1 = (base_x - sign * int(10 * scale), base_y - int(2 * scale))
            ip2 = (cx + sign * int(66 * scale), cy - int(82 * scale))
            ip3 = (base_x + sign * int(8 * scale), base_y + int(8 * scale))
            draw.polygon([ip1, ip2, ip3], fill=ear_in)
    elif character == "usagi":
        # Tall bunny ears
        for sign in (-1, 1):
            ex = cx + sign * int(42 * scale)
            ey_top = cy - int(132 * scale)
            ey_bot = cy - int(42 * scale)
            ew = int(21 * scale)
            draw.rounded_rectangle(
                [ex - ew, ey_top, ex + ew, ey_bot],
                radius=ew,
                fill=body_col,
                outline=outline,
                width=lw,
            )
            iw = int(10 * scale)
            draw.rounded_rectangle(
                [ex - iw, ey_top + int(14 * scale), ex + iw, ey_bot - int(10 * scale)],
                radius=iw,
                fill=ear_in,
            )

    # 2. Cute stubby body & feet below head
    bw = int(68 * scale)
    bh_top = cy + int(36 * scale)
    bh_bot = cy + int(106 * scale)
    draw.rounded_rectangle(
        [cx - bw, bh_top, cx + bw, bh_bot],
        radius=int(32 * scale),
        fill=body_col,
        outline=outline,
        width=lw,
    )
    # Little legs (do a cute alternating step-dance when frame_idx > 0)
    for sign in (-1, 1):
        lx = cx + sign * int(28 * scale)
        step_dy = int(math.sin((frame_idx + (0 if sign < 0 else 2)) * math.pi / 2) * 6 * scale) if frame_idx else 0
        draw.rounded_rectangle(
            [lx - int(16 * scale), bh_bot - int(14 * scale) + step_dy, lx + int(16 * scale), bh_bot + int(12 * scale) + step_dy],
            radius=int(12 * scale),
            fill=body_col,
            outline=outline,
            width=lw,
        )

    # 3. Chubby round head (slightly wider than tall, mochi-shaped)
    hw = int(92 * scale)
    hh = int(76 * scale)
    draw.ellipse(
        [cx - hw, cy - hh, cx + hw, cy + hh],
        fill=body_col,
        outline=outline,
        width=lw,
    )
    # Seamless mochi neck merge: erase the middle of the chin line so head flows into tummy
    neck_w = int(46 * scale)
    draw.rectangle(
        [cx - neck_w, cy + hh - lw * 2, cx + neck_w, cy + hh + lw * 2],
        fill=body_col,
    )

    # 4. Hachiware's signature blue parted hair pattern ("八" shape)
    if character == "hachiware":
        blue_col = (94, 168, 228)
        # Left blue patch on forehead
        left_patch = [
            (cx - int(4 * scale), cy - int(73 * scale)),
            (cx - int(48 * scale), cy - int(64 * scale)),
            (cx - int(80 * scale), cy - int(32 * scale)),
            (cx - int(84 * scale), cy - int(12 * scale)),
            (cx - int(42 * scale), cy - int(26 * scale)),
            (cx - int(8 * scale), cy - int(52 * scale)),
        ]
        right_patch = [
            (cx + int(4 * scale), cy - int(73 * scale)),
            (cx + int(48 * scale), cy - int(64 * scale)),
            (cx + int(80 * scale), cy - int(32 * scale)),
            (cx + int(84 * scale), cy - int(12 * scale)),
            (cx + int(42 * scale), cy - int(26 * scale)),
            (cx + int(8 * scale), cy - int(52 * scale)),
        ]
        draw.polygon(left_patch, fill=blue_col)
        draw.polygon(right_patch, fill=blue_col)
        # Redraw head outline on top so blue stays crisp inside head border
        draw.arc([cx - hw, cy - hh, cx + hw, cy + hh], start=180, end=360, fill=outline, width=lw)

    # Optional prop: Sasumata (토벌봉) when mood == "cheer" (drawn behind right paw)
    if mood == "cheer":
        prop_col = (255, 148, 178) if character == "chiikawa" else (112, 188, 250)
        px = cx + int(102 * scale)
        py_top = cy - int(12 * scale)
        py_bot = cy + int(98 * scale)
        draw.line([(px, py_top), (px, py_bot)], fill=outline, width=max(5, int(11 * scale)))
        draw.line([(px, py_top), (px, py_bot)], fill=prop_col, width=max(3, int(6 * scale)))
        # Forked U prong at top of sasumata
        fr = int(18 * scale)
        draw.arc([px - fr, py_top - fr * 2 + 4, px + fr, py_top + 6], start=0, end=180, fill=outline, width=max(5, int(10 * scale)))
        draw.arc([px - fr, py_top - fr * 2 + 4, px + fr, py_top + 6], start=0, end=180, fill=prop_col, width=max(3, int(6 * scale)))

    # 5. Cute little hands/paws (waving happily across frames)
    arm_y = cy + int(50 * scale)
    wave_dy = int(math.sin(frame_idx * math.pi / 2) * 18 * scale) if frame_idx else (-int(10 * scale) if mood == "cheer" else 0)
    # Left paw
    draw.ellipse(
        [cx - int(84 * scale), arm_y - int(14 * scale) + wave_dy, cx - int(52 * scale), arm_y + int(14 * scale) + wave_dy],
        fill=body_col,
        outline=outline,
        width=lw,
    )
    # Right paw
    draw.ellipse(
        [cx + int(52 * scale), arm_y - int(14 * scale) - wave_dy, cx + int(84 * scale), arm_y + int(14 * scale) - wave_dy],
        fill=body_col,
        outline=outline,
        width=lw,
    )

    # 6. Face details
    _draw_character_face_details(draw, cx, cy, mood=mood, frame_idx=frame_idx, scale=scale)


def calculate_dday(target_date_str: str) -> Optional[str]:
    """Calculates D-Day badge text (e.g. 'D-14', 'D-DAY', 'D+5') from 'YYYY-MM-DD'."""
    if not target_date_str or not target_date_str.strip():
        return None
    try:
        target = datetime.strptime(target_date_str.strip(), "%Y-%m-%d").date()
        today = date.today()
        diff = (target - today).days
        if diff == 0:
            return "D-DAY"
        elif diff > 0:
            return f"D-{diff}"
        else:
            return f"D+{abs(diff)}"
    except Exception:
        return None


def _compose_single_frame(
    config: Dict[str, Any],
    weather: Dict[str, Any],
    now: datetime,
    frame_idx: int = 0,
    custom_frame: Optional[Image.Image] = None,
) -> Image.Image:
    """Renders a single 240x240 frame using 480x480 supersampling."""
    theme_key = config.get("theme", "pink")
    theme = THEMES.get(theme_key, THEMES["pink"])
    is_night = theme_key == "night"

    canvas = Image.new("RGBA", (CANVAS_SIZE, CANVAS_SIZE), (255, 255, 255, 255))
    draw_background(canvas, theme, is_night=is_night, frame_idx=frame_idx)

    # Create overlay layer for semi-transparent cards
    overlay = Image.new("RGBA", (CANVAS_SIZE, CANVAS_SIZE), (0, 0, 0, 0))
    odraw = ImageDraw.Draw(overlay)

    layout = config.get("layout", "character_focus")  # 'character_focus' or 'full_custom_bg'

    # If user uploaded a custom image/GIF and chose 'full_custom_bg', fill background with it
    if custom_frame is not None and layout == "full_custom_bg":
        bg_img = ImageOps.fit(custom_frame.convert("RGBA"), (CANVAS_SIZE, CANVAS_SIZE), Image.Resampling.LANCZOS)
        canvas.alpha_composite(bg_img)
        # Add soft top & bottom frosted glass bars so time & temp are always readable
        odraw.rounded_rectangle(
            [16, 14, CANVAS_SIZE - 16, 156],
            radius=28,
            fill=(255, 255, 255, 205) if not is_night else (24, 28, 52, 210),
            outline=theme["card_border"],
            width=3,
        )
        if config.get("custom_text") or config.get("dday_date"):
            odraw.rounded_rectangle(
                [20, CANVAS_SIZE - 68, CANVAS_SIZE - 20, CANVAS_SIZE - 14],
                radius=22,
                fill=(255, 255, 255, 215) if not is_night else (24, 28, 52, 215),
                outline=theme["card_border"],
                width=3,
            )
        canvas.alpha_composite(overlay)
    else:
        # Top Dashboard Card (Time + Date + Weather/Temp)
        odraw.rounded_rectangle(
            [18, 16, CANVAS_SIZE - 18, 182],
            radius=32,
            fill=theme["card_bg"],
            outline=theme["card_border"],
            width=4,
        )
        canvas.alpha_composite(overlay)

    draw = ImageDraw.Draw(canvas)

    # --- 1. Draw Time & Date on Left Side of Top Card ---
    use_12h = bool(config.get("use_12h", False))
    if use_12h:
        hour_val = now.hour % 12 or 12
        time_str = f"{hour_val:02d}:{now.minute:02d}"
        ampm_str = "AM" if now.hour < 12 else "PM"
    else:
        time_str = f"{now.hour:02d}:{now.minute:02d}"
        ampm_str = ""

    font_time = load_font(82, weight="heavy", rounded_digits=True)
    font_ampm = load_font(22, weight="bold", rounded_digits=True)
    font_date = load_font(25, weight="bold")
    font_temp = load_font(44, weight="heavy", rounded_digits=True)
    font_city = load_font(22, weight="bold")
    font_msg = load_font(25, weight="bold")
    font_badge = load_font(22, weight="extrabold")

    # Date & Weekday string (e.g. "11월 6일 (금)")
    weekday_ko = WEEKDAYS_KO[now.weekday()]
    date_str = f"{now.month}월 {now.day}일 ({weekday_ko})"
    draw.text((40, 30), date_str, font=font_date, fill=theme["text_sub"])

    if ampm_str:
        draw.text((210, 33), ampm_str, font=font_ampm, fill=theme["accent"])

    # Large Time HH:MM
    draw.text((36, 62), time_str, font=font_time, fill=theme["text_main"])

    # Vertical divider inside top card
    div_x = 294
    draw.line([(div_x, 34), (div_x, 164)], fill=theme["card_border"], width=3)

    # --- 2. Draw Weather & Temperature on Right Side of Top Card ---
    temp_val = weather.get("temp", 18)
    weather_code = weather.get("code", "sunny")
    city_label = config.get("city_label") or weather.get("city", "서울")
    temp_str = f"{temp_val}°"

    # Weather icon at top-right
    draw_weather_icon(draw, cx=344, cy=66, weather_code=weather_code, scale=1.05, frame_idx=frame_idx)

    # City label next to weather icon
    draw.text((384, 52), city_label[:4], font=font_city, fill=theme["text_sub"])

    # Big Temperature below icon
    temp_bbox = draw.textbbox((0, 0), temp_str, font=font_temp)
    tw = temp_bbox[2] - temp_bbox[0]
    tx = 386 - tw // 2
    draw.text((tx, 96), temp_str, font=font_temp, fill=theme["accent"])

    # Min/Max or Humidity sub-label
    sub_weather = weather.get("summary", "")
    if sub_weather:
        font_wsub = load_font(19, weight="semibold")
        sb = draw.textbbox((0, 0), sub_weather, font=font_wsub)
        sw = sb[2] - sb[0]
        draw.text((386 - sw // 2, 148), sub_weather, font=font_wsub, fill=theme["text_sub"])

    # --- 3. Draw Character Area (Middle / Bottom) ---
    character = config.get("character", "chiikawa")
    mood = config.get("mood", "happy")

    if custom_frame is not None and layout != "full_custom_bg":
        # Sticker / framed custom image in character area
        target_h = 224
        target_w = 340
        char_img = ImageOps.contain(custom_frame.convert("RGBA"), (target_w, target_h), Image.Resampling.LANCZOS)
        cw, ch = char_img.size
        paste_x = (CANVAS_SIZE - cw) // 2
        paste_y = 194 + (target_h - ch) // 2
        canvas.alpha_composite(char_img, (paste_x, paste_y))
    elif layout != "full_custom_bg":
        # Floating animated sparkles & hearts around the character area
        sp_pts = [(52, 224), (428, 220), (42, 356), (438, 352), (240, 200)]
        for s_i, (sx, sy) in enumerate(sp_pts):
            bob = int(math.sin((frame_idx + s_i) * math.pi / 2) * 10)
            sy_b = sy + bob
            if (frame_idx + s_i) % 2 == 0:
                r = 9
                draw.line([(sx - r, sy_b), (sx + r, sy_b)], fill=theme["accent"], width=4)
                draw.line([(sx, sy_b - r), (sx, sy_b + r)], fill=theme["accent"], width=4)
            else:
                draw.ellipse([sx - 5, sy_b - 5, sx + 5, sy_b + 5], fill=theme["card_border"])

        if character == "trio":
            # Draw Hachiware (left), Chiikawa (center), Usagi (right) with lively wave phase
            draw_single_character(draw, "hachiware", cx=112, cy=312, scale=0.70, mood=mood, frame_idx=frame_idx)
            draw_single_character(draw, "usagi", cx=368, cy=312, scale=0.70, mood=mood, frame_idx=frame_idx + 2)
            draw_single_character(draw, "chiikawa", cx=240, cy=318, scale=0.80, mood=mood, frame_idx=frame_idx + 1)
        else:
            draw_single_character(draw, character, cx=240, cy=306, scale=0.92, mood=mood, frame_idx=frame_idx)

    # --- 4. Draw Bottom Speech Bubble / Custom Text & D-Day Badge ---
    custom_text = (config.get("custom_text") or "").strip()
    dday_str = calculate_dday(config.get("dday_date", ""))
    dday_title = (config.get("dday_title") or "").strip()

    if custom_text or dday_str:
        bar_top = 418
        bar_bot = 468
        draw.rounded_rectangle(
            [22, bar_top, CANVAS_SIZE - 22, bar_bot],
            radius=24,
            fill=(255, 255, 255) if not is_night else (42, 48, 86),
            outline=theme["card_border"],
            width=3,
        )

        text_left = 38
        if dday_str:
            badge_label = f"{dday_title} {dday_str}".strip() if dday_title else dday_str
            bb = draw.textbbox((0, 0), badge_label, font=font_badge)
            bw = (bb[2] - bb[0]) + 28
            draw.rounded_rectangle(
                [30, bar_top + 7, 30 + bw, bar_bot - 7],
                radius=18,
                fill=theme["badge_bg"],
            )
            draw.text((44, bar_top + 11), badge_label, font=font_badge, fill=theme["badge_text"])
            text_left = 30 + bw + 14

        if custom_text:
            draw.text((text_left, bar_top + 10), custom_text, font=font_msg, fill=theme["text_main"])

    # Downsample 480x480 -> 240x240 with high-quality LANCZOS filter
    final_img = canvas.convert("RGB").resize((SCREEN_SIZE, SCREEN_SIZE), Image.Resampling.LANCZOS)
    return final_img


def render_clock_jpeg(
    config: Dict[str, Any],
    weather: Dict[str, Any],
    now: Optional[datetime] = None,
    custom_image_bytes: Optional[bytes] = None,
) -> bytes:
    """
    Renders a 240x240 Baseline JPEG (subsampling=0, progressive=False)
    guaranteed to be compatible with the GeekMagic ST7789 hardware JPEG decoder.
    """
    if now is None:
        now = datetime.now()

    custom_frame = None
    if custom_image_bytes:
        try:
            custom_frame = Image.open(io.BytesIO(custom_image_bytes))
        except Exception:
            custom_frame = None

    img = _compose_single_frame(config, weather, now, frame_idx=0, custom_frame=custom_frame)
    buf = io.BytesIO()
    img.save(
        buf,
        format="JPEG",
        quality=94,
        subsampling=0,
        progressive=False,
        optimize=False,
    )
    return buf.getvalue()


def render_smalltv_80x80_gif(
    config: Dict[str, Any],
    custom_image_bytes: Optional[bytes] = None,
) -> bytes:
    """
    Renders an 80x80 animated GIF (<700KB) specifically for the GeekMagic SmallTV
    native weather-clock screen's 80x80 animation slot (/upload + /set?gif=10).
    Uses a pure #000000 black background by default so the character blends seamlessly
    into the SmallTV's native black clock/temperature screen.
    """
    target_size = 80
    ss = 240  # 3x supersampling (240x240 -> 80x80) for ultra-clean lines
    character = config.get("character", "chiikawa")
    mood = config.get("mood", "happy")
    bg_mode = config.get("gif_bg_mode", "black")  # 'black' (seamless on SmallTV) or 'theme'
    theme_key = config.get("theme", "pink")
    theme = THEMES.get(theme_key, THEMES["pink"])

    custom_text = (config.get("custom_text") or "").strip()
    dday_str = calculate_dday(config.get("dday_date", ""))
    dday_title = (config.get("dday_title") or "").strip()
    if dday_str:
        badge_str = f"{dday_title} {dday_str}".strip()
    else:
        badge_str = custom_text[:12]

    frames: List[Image.Image] = []
    durations: List[int] = []

    if custom_image_bytes:
        try:
            src = Image.open(io.BytesIO(custom_image_bytes))
            raw_frames = []
            for f in ImageSequence.Iterator(src):
                raw_frames.append((f.copy(), f.info.get("duration", 120)))
            step = max(1, len(raw_frames) // 16)
            sampled = raw_frames[::step][:16]
            for f_img, dur in sampled:
                bg_col = (0, 0, 0, 255) if bg_mode == "black" else (*theme["bg_top"], 255)
                canvas = Image.new("RGBA", (target_size, target_size), bg_col)
                contained = ImageOps.contain(f_img.convert("RGBA"), (target_size, target_size), Image.Resampling.LANCZOS)
                cw, ch = contained.size
                canvas.alpha_composite(contained, ((target_size - cw) // 2, (target_size - ch) // 2))
                frames.append(canvas.convert("RGB").quantize(colors=128, method=Image.Quantize.MEDIANCUT))
                durations.append(max(90, int(dur * step)))
        except Exception:
            pass

    if not frames:
        for idx in range(6):
            bg_col = (0, 0, 0, 255) if bg_mode == "black" else (*theme["bg_top"], 255)
            canvas = Image.new("RGBA", (ss, ss), bg_col)
            draw = ImageDraw.Draw(canvas)

            # Floating sparkles / stars around character
            sparkle_cols = [(255, 168, 202), (130, 208, 255), (255, 224, 130)]
            sp_positions = [(28, 30), (212, 34), (24, 158), (216, 154)]
            for s_i, (sx, sy) in enumerate(sp_positions):
                if (idx + s_i) % 2 == 0:
                    sc = sparkle_cols[(idx + s_i) % len(sparkle_cols)]
                    r = 7
                    draw.line([(sx - r, sy), (sx + r, sy)], fill=sc, width=3)
                    draw.line([(sx, sy - r), (sx, sy + r)], fill=sc, width=3)

            if character == "trio":
                # Draw all 3 characters (Hachiware left, Chiikawa center, Usagi right) together!
                base_y = 122 if badge_str else 138
                draw_single_character(draw, "hachiware", cx=56, cy=base_y, scale=0.46, mood=mood, frame_idx=idx)
                draw_single_character(draw, "usagi", cx=184, cy=base_y + 4, scale=0.45, mood=mood, frame_idx=idx + 1)
                draw_single_character(draw, "chiikawa", cx=120, cy=base_y + 8, scale=0.52, mood=mood, frame_idx=idx)
            else:
                cy_char = 118 if badge_str else 132
                scale_char = 0.88 if badge_str else 0.96
                if character == "usagi":
                    cy_char += 18
                    scale_char *= 0.88

                draw_single_character(
                    draw,
                    character,
                    cx=ss // 2,
                    cy=cy_char,
                    scale=scale_char,
                    mood=mood,
                    frame_idx=idx,
                )

            # Optional mini caption / D-Day pill at bottom of 80x80
            if badge_str:
                for fsz in (28, 25, 22, 19):
                    font_mini = load_font(fsz, weight="heavy")
                    tb = draw.textbbox((0, 0), badge_str, font=font_mini)
                    tw = tb[2] - tb[0]
                    if tw <= ss - 28:
                        break
                pill_w = min(ss - 8, tw + 24)
                pill_x1 = (ss - pill_w) // 2
                pill_x2 = pill_x1 + pill_w
                draw.rounded_rectangle(
                    [pill_x1, ss - 44, pill_x2, ss - 6],
                    radius=18,
                    fill=theme["badge_bg"],
                    outline=(255, 255, 255),
                    width=3,
                )
                th = tb[3] - tb[1]
                draw.text(((ss - tw) // 2, ss - 26 - th // 2 - 2), badge_str, font=font_mini, fill=(255, 255, 255))

            small = canvas.convert("RGB").resize((target_size, target_size), Image.Resampling.LANCZOS)
            frames.append(small.quantize(colors=128, method=Image.Quantize.MEDIANCUT))
            durations.append(260 if character != "trio" else 380)

    buf = io.BytesIO()
    frames[0].save(
        buf,
        format="GIF",
        save_all=True,
        append_images=frames[1:],
        duration=durations,
        loop=0,
        optimize=True,
    )
    return buf.getvalue()


def render_clock_gif(
    config: Dict[str, Any],
    weather: Dict[str, Any],
    now: Optional[datetime] = None,
    custom_image_bytes: Optional[bytes] = None,
) -> bytes:
    """
    Renders an animated 240x240 GIF (either animating the built-in Chiikawa characters
    or compositing the live time/temperature overlay onto each frame of a user's GIF).
    Kept compact (<150KB) so GeekMagic LittleFS storage handles it smoothly.
    """
    if now is None:
        now = datetime.now()

    frames: List[Image.Image] = []
    durations: List[int] = []

    if custom_image_bytes:
        try:
            src = Image.open(io.BytesIO(custom_image_bytes))
            raw_frames = []
            for f in ImageSequence.Iterator(src):
                raw_frames.append((f.copy(), f.info.get("duration", 140)))
            # Cap at max 12 frames so file size fits easily in GeekMagic flash memory
            step = max(1, len(raw_frames) // 12)
            sampled = raw_frames[::step][:12]
            for idx, (f_img, dur) in enumerate(sampled):
                composed = _compose_single_frame(config, weather, now, frame_idx=idx, custom_frame=f_img)
                frames.append(composed.quantize(colors=128, method=Image.Quantize.MEDIANCUT))
                durations.append(max(100, int(dur * step)))
        except Exception:
            pass

    if not frames:
        # Built-in 4-frame lively animation (bounce, wave paws, step-dance, blink, sparkle)
        # 420ms frame delay + 64-color palette gives smooth animation while leaving ESP8266 CPU headroom for HTTP
        for idx in range(4):
            composed = _compose_single_frame(config, weather, now, frame_idx=idx, custom_frame=None)
            frames.append(composed.quantize(colors=64, method=Image.Quantize.MEDIANCUT))
            durations.append(420)

    buf = io.BytesIO()
    frames[0].save(
        buf,
        format="GIF",
        save_all=True,
        append_images=frames[1:],
        duration=durations,
        loop=0,
        optimize=True,
    )
    return buf.getvalue()
