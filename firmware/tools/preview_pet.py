#!/usr/bin/env python3
"""Host-render the actual Pro pet renderer with checked framebuffer writes.

Run: .venv/bin/python firmware/tools/preview_pet.py --output /tmp/geekmagic-pet-preview
Requires clang++ and Pillow. Uses the actual generated assets and compositing
helpers from main.cpp, with host PROGMEM and TFT shims; no device is contacted.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from PIL import Image, ImageDraw


FIRMWARE = Path(__file__).resolve().parents[1]

ARDUINO_STUB = r"""#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
using std::max;
using std::min;
template<typename T> constexpr T constrain(T v, T lo, T hi) { return v < lo ? lo : v > hi ? hi : v; }
uint32_t millis();
void yield();
"""

PGMSPACE_STUB = r"""#pragma once
#define PROGMEM
#define pgm_read_byte(addr) (*(addr))
#define pgm_read_word(addr) (*(addr))
#define pgm_read_dword(addr) (*(addr))
"""

HOST_PRELUDE = r"""
#include <Arduino.h>
#include <cassert>
#include <string>
#include <fstream>
#include <filesystem>
static constexpr int SCREEN_W = 240, SCREEN_H = 240, STRIP_H = 20;
static uint16_t stripBuf[SCREEN_W * STRIP_H];
static uint16_t framebuffer[SCREEN_W * SCREEN_H];
static uint32_t fakeNowMs;
uint32_t millis() { return fakeNowMs; }
void yield() {}
static struct { char theme[16] = "pink"; } gCfg;
struct MockTft {
    void draw16bitRGBBitmap(int x, int y, uint16_t* src, int w, int h) {
        assert(x >= 0 && y >= 0 && w > 0 && h > 0 && x+w <= SCREEN_W && y+h <= SCREEN_H);
        for (int yy = 0; yy < h; ++yy)
            for (int xx = 0; xx < w; ++xx)
                framebuffer[(y+yy)*SCREEN_W+x+xx] = src[yy*w+xx];
    }
} tft;
"""

HOST_MAIN = r"""
static void savePpm(const std::string& output) {
    std::ofstream f(output, std::ios::binary);
    f << "P6\n240 240\n255\n";
    for (uint16_t value : framebuffer) {
        const char rgb[] = {char(((value>>11)&31)*255/31), char(((value>>5)&63)*255/63), char((value&31)*255/31)};
        f.write(rgb, sizeof(rgb));
    }
}
int main(int argc, char** argv) {
    assert(argc == 2);
    const std::filesystem::path out(argv[1]);
    for (int scenario = 0; scenario < 7; ++scenario) {
        gPetGame = PetGame{};
        gPetGame.active = true;
        gPetGame.tick(0);
        gPetGame.x = scenario == 0 ? PetGame::MIN_X : scenario == 1 ? PetGame::MAX_X : 69;
        gPetGame.y = scenario == 0 ? PetGame::MIN_Y : scenario == 1 ? PetGame::MAX_Y : 103;
        strcpy(gCfg.theme, scenario == 6 ? "night" : "pink");
        fakeNowMs = 1000;
        switch (scenario) {
            case 2: assert(gPetGame.feed(1000)); break;
            case 3: assert(gPetGame.play(1000)); break;
            case 4: assert(gPetGame.sleep(1000)); break;
            case 5: gPetGame.data.fullness = 10; break;
            case 6: assert(gPetGame.sleep(1000)); break;
        }
        renderPetScreen();
        static const char* names[] = {"wander-top-left", "wander-bottom-right", "eating", "happy", "sleeping", "hungry", "night"};
        savePpm((out / (std::string(names[scenario])+".ppm")).string());
    }
    // Exercise every animation frame at all four movement bounds with both
    // directions, expressions, and palettes, while ASan/UBSan check indexing.
    for (int theme = 0; theme < 2; ++theme)
    for (int pose = 0; pose < 5; ++pose)
    for (int frame = 0; frame < 4; ++frame)
    for (int corner = 0; corner < 4; ++corner)
    for (int facing = 0; facing < 2; ++facing) {
        gPetGame = PetGame{};
        gPetGame.active = true;
        gPetGame.tick(0);
        if (facing) {
            gPetGame.x = PetGame::MAX_X;
            gPetGame.tick(100);
            assert(gPetGame.facingLeft());
        }
        gPetGame.x = (corner&1) ? PetGame::MAX_X : PetGame::MIN_X;
        gPetGame.y = (corner&2) ? PetGame::MAX_Y : PetGame::MIN_Y;
        strcpy(gCfg.theme, theme ? "night" : "pink");
        if (pose == 1) assert(gPetGame.feed(1000));
        if (pose == 2) assert(gPetGame.play(1000));
        if (pose == 3) assert(gPetGame.sleep(1000));
        if (pose == 4) gPetGame.data.happiness = 10;
        fakeNowMs = (pose == 3 ? 600 : 180) * frame;
        renderPetScreen();
    }
    puts("PASS: 7 preview scenes + 320 bounded animation scenes, checked framebuffer writes.");
}
"""


def check_glyph_coverage(renderer: str, assets: str) -> None:
    required = set()
    for literal in re.findall(r'"([^"\n]*)"', renderer):
        required.update(ord(ch) for ch in literal if ord(ch) >= 128)
    for font in ("FONT_PET_10", "FONT_PET_13"):
        match = re.search(rf"{font}_GLYPHS\[\d+\].*?=\s*\{{(.*?)\}};", assets, re.S)
        if not match:
            raise ValueError(f"Missing {font}")
        codes = {int(value) for value in re.findall(r"\{\s*(\d+),", match.group(1))}
        missing = required - codes
        if missing:
            raise ValueError(f"{font} missing renderer glyphs: {''.join(chr(cp) for cp in sorted(missing))}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("/tmp/geekmagic-pet-preview"))
    args = parser.parse_args()
    compiler = shutil.which("clang++")
    if not compiler:
        raise RuntimeError("clang++ is required for actual renderer previews")
    args.output.mkdir(parents=True, exist_ok=True)
    source = (FIRMWARE / "src" / "main.cpp").read_text(encoding="utf-8")
    helpers = source[source.index("struct ThemePalette {"):source.index("static void blitRleStreamStrip(")]
    renderer = (FIRMWARE / "src" / "pet_renderer.h").read_text(encoding="utf-8")
    check_glyph_coverage(renderer, (FIRMWARE / "src" / "pet_assets.h").read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory(prefix="geekmagic-pet-render-") as folder:
        temp = Path(folder)
        (temp / "Arduino.h").write_text(ARDUINO_STUB)
        (temp / "pgmspace.h").write_text(PGMSPACE_STUB)
        includes = "\n".join(f'#include "{FIRMWARE / "src" / name}"' for name in ["chiikawa_assets.h", "pet_assets.h", "pet_game.h"])
        render_include = f'\n#include "{FIRMWARE / "src" / "pet_renderer.h"}"\n'
        code = HOST_PRELUDE + includes + "\nstatic PetGame gPetGame;\n" + helpers + render_include + HOST_MAIN
        (temp / "preview.cpp").write_text(code)
        subprocess.run([compiler, "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(temp), str(temp / "preview.cpp"), "-o", str(temp / "preview")], check=True)
        subprocess.run([str(temp / "preview"), str(args.output.resolve())], check=True)

    names = ["wander-top-left", "wander-bottom-right", "eating", "happy", "sleeping", "hungry", "night"]
    collage = Image.new("RGB", (240 * 4, 260 * 2), "#fff4f8")
    draw = ImageDraw.Draw(collage)
    for i, name in enumerate(names):
        with Image.open(args.output / f"{name}.ppm") as img:
            img.save(args.output / f"{name}.png")
            x, y = (i % 4) * 240, (i // 4) * 260
            collage.paste(img, (x, y))
            draw.text((x + 8, y + 244), name, fill="#513743")
        (args.output / f"{name}.ppm").unlink()
    collage.save(args.output / "pet-scenes.png")
    print(f"Glyph coverage passed; actual renderer previews: {args.output}")


if __name__ == "__main__":
    main()
