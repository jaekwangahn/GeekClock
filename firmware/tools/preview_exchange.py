#!/usr/bin/env python3
"""Render the actual exchange screen on a checked host framebuffer.

Run: .venv/bin/python firmware/tools/preview_exchange.py --output /tmp/geekmagic-exchange-preview
Requires clang++ and Pillow. No network or device access is performed.
"""
from pathlib import Path
import argparse
import re
import shutil
import subprocess
import tempfile
from PIL import Image, ImageDraw
from preview_pet import ARDUINO_STUB, PGMSPACE_STUB, HOST_PRELUDE

FIRMWARE = Path(__file__).resolve().parents[1]
VIEW = r'''
#include <cmath>
using std::isfinite;
struct ExchangeView {
    bool valid = false, loading = false, stale = false;
    double krwPerJpy = 0;
    char rateDate[11] = {};
    char fetchedAt[32] = {};
    char error[72] = {};
};
static ExchangeView gExchangeView;
'''
MAIN = r'''
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
    const char* names[] = {"normal", "first-loading", "refresh-cached", "cached-error", "offline-empty", "wide-value", "night"};
    for (int scenario = 0; scenario < 7; ++scenario) {
        gExchangeView = ExchangeView{};
        gExchangeView.valid = true;
        gExchangeView.krwPerJpy = 8.3572;
        strcpy(gExchangeView.rateDate, "2026-11-13");
        strcpy(gExchangeView.fetchedAt, "2026-11-15 21:30:45 KST");
        strcpy(gCfg.theme, scenario == 6 ? "night" : "pink");
        if (scenario == 1) { gExchangeView.valid = false; gExchangeView.loading = true; gExchangeView.rateDate[0]=gExchangeView.fetchedAt[0]=0; }
        if (scenario == 2) gExchangeView.loading = true;
        if (scenario == 3) { gExchangeView.stale = true; strcpy(gExchangeView.error,"network failure"); }
        if (scenario == 4) { gExchangeView.valid = false; gExchangeView.stale = true; gExchangeView.rateDate[0]=gExchangeView.fetchedAt[0]=0; strcpy(gExchangeView.error,"offline"); }
        if (scenario == 5) gExchangeView.krwPerJpy = 1000;
        renderExchangeScreen();
        savePpm((out / (std::string(names[scenario])+".ppm")).string());
    }
    for (int theme=0;theme<2;++theme)
    for (int validity=0;validity<2;++validity)
    for (int loading=0;loading<2;++loading)
    for (int stale=0;stale<2;++stale)
    for (double rate : {0.,0.01,8.3572,99.99,1000.,1e100,std::nan("")}) {
        gExchangeView.valid=validity;gExchangeView.loading=loading;gExchangeView.stale=stale;
        gExchangeView.krwPerJpy=rate;
        strcpy(gCfg.theme,theme?"night":"pink");
        renderExchangeScreen();
    }
    puts("PASS: 7 exchange previews + 112 edge/state/theme combinations under ASan/UBSan.");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('/tmp/geekmagic-exchange-preview'))
    args = parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    source = (FIRMWARE/'src/main.cpp').read_text()
    helpers = source[source.index('struct ThemePalette {'):source.index('static void blitRleStreamStrip(')]
    renderer = (FIRMWARE/'src/exchange_renderer.h').read_text()
    assets = (FIRMWARE/'src/exchange_assets.h').read_text()
    required=set()
    for literal in re.findall(r'"([^"\n]*)"',renderer):
        required.update(ord(c) for c in literal if ord(c)>=128)
    for font in ['FONT_FX_10','FONT_FX_13']:
        glyphs=re.search(rf'{font}_GLYPHS\[\d+\].*?=\s*\{{(.*?)\}};',assets,re.S)
        codes={int(v) for v in re.findall(r'\{\s*(\d+),',glyphs.group(1))}
        assert not required-codes, (font,required-codes)
    with tempfile.TemporaryDirectory(prefix='geekmagic-exchange-render-') as folder:
        temp=Path(folder)
        (temp/'Arduino.h').write_text(ARDUINO_STUB)
        (temp/'pgmspace.h').write_text(PGMSPACE_STUB)
        includes='\n'.join(f'#include "{FIRMWARE / "src" / name}"' for name in ['chiikawa_assets.h','exchange_assets.h'])
        code=HOST_PRELUDE+includes+VIEW+helpers+f'\n#include "{FIRMWARE / "src" / "exchange_renderer.h"}"\n'+MAIN
        (temp/'preview.cpp').write_text(code)
        subprocess.run([shutil.which('clang++'),'--std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(temp),str(temp/'preview.cpp'),'-o',str(temp/'preview')],check=True)
        subprocess.run([str(temp/'preview'),str(args.output.resolve())],check=True)
    names=['normal','first-loading','refresh-cached','cached-error','offline-empty','wide-value','night']
    collage=Image.new('RGB',(240*4,260*2),'#fff4f8');draw=ImageDraw.Draw(collage)
    for i,name in enumerate(names):
        with Image.open(args.output/f'{name}.ppm') as image:
            image.save(args.output/f'{name}.png');x,y=(i%4)*240,(i//4)*260
            collage.paste(image,(x,y));draw.text((x+8,y+244),name,fill='#513743')
        (args.output/f'{name}.ppm').unlink()
    collage.save(args.output/'exchange-scenes.png')
    print(f'Glyph coverage passed; exact renderer previews: {args.output}')


if __name__=='__main__':main()
