#!/usr/bin/env python3
"""Render the uploaded NES title clock into a checked host framebuffer and GIF.

Run: .venv/bin/python firmware/tools/preview_mario.py --output /tmp/geekmagic-mario-preview
Requires clang++ and Pillow. No device, ROM, or network access is performed.
"""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile
from PIL import Image, ImageDraw
from preview_pet import ARDUINO_STUB, PGMSPACE_STUB, HOST_PRELUDE

FIRMWARE=Path(__file__).resolve().parents[1]
VIEW=r'''
struct MarioClockView {
    int hour=0,minute=0,second=0;
    int year=0,month=0,day=0;
    bool timeValid=false,use12h=false,showSec=false;
    uint32_t nowMs=0,jumpStartedMs=0;
    bool jumpActive=false;
};
static MarioClockView gMarioView;
'''
MAIN=r'''
static void savePpm(const std::string& output) {
    std::ofstream f(output,std::ios::binary);f << "P6\n240 240\n255\n";
    for(uint16_t value:framebuffer) {
        const char rgb[]={char(((value>>11)&31)*255/31),char(((value>>5)&63)*255/63),char((value&31)*255/31)};
        f.write(rgb,sizeof(rgb));
    }
}
int main(int argc,char**argv) {
    assert(argc==2);const std::filesystem::path out(argv[1]);
    const char* names[]={"morning-seconds","noon-jump","afternoon","evening","late-night","midnight-12h","time-sync","hours-minutes"};
    const int hours[]={7,12,17,19,23,0,0,22};
    const int minutes[]={0,0,0,45,59,0,0,58};
    for(int i=0;i<8;++i) {
        gMarioView=MarioClockView{};gMarioView.hour=hours[i];gMarioView.minute=minutes[i];
        gMarioView.second=i==4?1:20;gMarioView.timeValid=i!=6;gMarioView.use12h=i==5;
        gMarioView.year=2026;gMarioView.month=11;gMarioView.day=15;gMarioView.showSec=i!=7;
        gMarioView.nowMs=14000+i*350;gMarioView.jumpActive=i==1;gMarioView.jumpStartedMs=gMarioView.nowMs-450;
        renderMarioScreen();savePpm((out/(std::string(names[i])+".ppm")).string());
    }
    for(int i=0;i<48;++i) {
        gMarioView=MarioClockView{};gMarioView.hour=i<24?7:23;gMarioView.minute=i<24?0:59;
        gMarioView.second=20+i/10;gMarioView.timeValid=true;gMarioView.nowMs=6000+(i%24)*100;
        gMarioView.year=2026;gMarioView.month=11;gMarioView.day=15;gMarioView.showSec=true;
        gMarioView.jumpStartedMs=6500;gMarioView.jumpActive=(i%24)>=5&&(i%24)<14;
        renderMarioScreen();savePpm((out/("frame-"+std::to_string(i)+".ppm")).string());
    }
    for(int h=0;h<24;++h) for(int m:{0,1,11,22,30,45,58,59})
    for(bool twelve:{false,true}) for(bool jumping:{false,true}) for(bool seconds:{false,true}) {
        gMarioView.hour=h;gMarioView.minute=m;gMarioView.second=m;gMarioView.use12h=twelve;
        gMarioView.timeValid=true;gMarioView.nowMs=123450+h*777+m*17;gMarioView.showSec=seconds;
        gMarioView.year=2026;gMarioView.month=11;gMarioView.day=15;
        gMarioView.jumpStartedMs=gMarioView.nowMs-450;gMarioView.jumpActive=jumping;
        renderMarioScreen();
    }
    for(uint32_t at:{uint32_t(0),uint32_t(1),UINT32_MAX,uint32_t(UINT32_MAX-450),uint32_t(450)}) {
        gMarioView.nowMs=at;gMarioView.jumpStartedMs=UINT32_MAX-449;gMarioView.jumpActive=true;renderMarioScreen();
    }
    puts("PASS: exact title renderer, 1536 clock/layout/pose/seconds scenes + timer wrap under ASan/UBSan.");
}
'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=Path('/tmp/geekmagic-mario-preview'))
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    source=(FIRMWARE/'src/main.cpp').read_text()
    helpers=source[source.index('struct ThemePalette {'):source.index('static void blitRleStreamStrip(')]
    with tempfile.TemporaryDirectory(prefix='geekmagic-mario-render-') as folder:
        temp=Path(folder);(temp/'Arduino.h').write_text(ARDUINO_STUB);(temp/'pgmspace.h').write_text(PGMSPACE_STUB)
        includes='\n'.join(f'#include "{FIRMWARE/"src"/name}"' for name in ['chiikawa_assets.h','mario_assets.h','mario_title_assets.h'])
        code=HOST_PRELUDE+includes+VIEW+helpers+f'\n#include "{FIRMWARE/"src"/"mario_renderer.h"}"\n'+MAIN
        (temp/'preview.cpp').write_text(code)
        subprocess.run([shutil.which('clang++'),'--std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(temp),str(temp/'preview.cpp'),'-o',str(temp/'preview')],check=True)
        subprocess.run([str(temp/'preview'),str(args.output.resolve())],check=True)
    names=['morning-seconds','noon-jump','afternoon','evening','late-night','midnight-12h','time-sync','hours-minutes']
    collage=Image.new('RGB',(960,520),'#fff5e5');draw=ImageDraw.Draw(collage)
    for i,name in enumerate(names):
        with Image.open(args.output/f'{name}.ppm') as image:
            image.save(args.output/f'{name}.png');x,y=(i%4)*240,(i//4)*260
            collage.paste(image,(x,y));draw.text((x+8,y+244),name,fill='#493220')
        (args.output/f'{name}.ppm').unlink()
    collage.save(args.output/'mario-scenes.png')
    frames=[]
    for i in range(48):
        file=args.output/f'frame-{i}.ppm'
        with Image.open(file) as image:frames.append(image.copy().convert('RGB'))
        file.unlink()
    frames[0].save(args.output/'mario-clock.gif',save_all=True,append_images=frames[1:],duration=100,loop=0,optimize=False,disposal=2)
    print(f'Exact renderer previews and 48-frame GIF: {args.output}')

if __name__=='__main__':main()
