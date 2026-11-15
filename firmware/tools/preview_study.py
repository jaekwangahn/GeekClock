#!/usr/bin/env python3
"""Render the actual Pro study timer and furnished pet room on a host framebuffer.

Run: .venv/bin/python firmware/tools/preview_study.py --output /tmp/geekmagic-study-preview
The same C++ renderers and assets run with checked TFT/PROGMEM shims.
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
GLOBALS = r'''
static PetGame gPetGame;
static RoomDecor gRoom;
static PomodoroTimer gPomodoro;
static uint16_t gStudyRewardCoins = 0;
static uint32_t gStudyRewardAtMs = 0;
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
static void resetScene() {
    gPetGame=PetGame{};gPetGame.active=true;gPetGame.tick(0);
    gPetGame.x=88;gPetGame.y=108;
    gRoom=RoomDecor{};gRoom.data.ownedMask=255;
    gPomodoro=PomodoroTimer{};
    gStudyRewardCoins=0;gStudyRewardAtMs=0;
    fakeNowMs=2000;strcpy(gCfg.theme,"pink");
}
int main(int argc,char**argv) {
    assert(argc==2);const std::filesystem::path out(argv[1]);
    const char* names[]={"room-default","room-soda","room-night","room-berry-rug","room-star-rug","room-cloud-bed","room-moon-bed","room-plant","room-star-lamp","room-cozy","pomo-ready","pomo-focus","pomo-paused","pomo-break","pomo-long-break","pomo-reward"};
    for(int scene=0;scene<16;++scene) {
        resetScene();
        if(scene==1)gRoom.data.wallpaper=1;
        if(scene==2)gRoom.data.wallpaper=2;
        if(scene==3)gRoom.data.rug=3;
        if(scene==4)gRoom.data.rug=4;
        if(scene==5)gRoom.data.bed=5;
        if(scene==6)gRoom.data.bed=6;
        if(scene==7)gRoom.data.decor=7;
        if(scene==8)gRoom.data.decor=8;
        if(scene==9){gRoom.data.wallpaper=2;gRoom.data.rug=4;gRoom.data.bed=6;gRoom.data.decor=8;gRoom.data.coins=240;}
        if(scene==11){gPomodoro.data.running=true;gPomodoro.data.remainingMs=1125000;gPomodoro.data.cycle=2;gPomodoro.data.completedFocus=6;gPomodoro.data.totalFocusMinutes=150;}
        if(scene==12){gPomodoro.data.remainingMs=330000;}
        if(scene==13){gPomodoro.data.phase=1;gPomodoro.data.remainingMs=300000;gPomodoro.data.running=true;}
        if(scene==14){gPomodoro.data.phase=2;gPomodoro.data.remainingMs=900000;}
        if(scene==15){gPomodoro.data.phase=1;gPomodoro.data.remainingMs=300000;gStudyRewardCoins=10;gStudyRewardAtMs=1500;gRoom.data.coins=30;gPomodoro.data.completedFocus=1;gPomodoro.data.totalFocusMinutes=25;gPomodoro.data.cycle=1;}
        if(scene<10)renderPetScreen();else renderPomodoroScreen();
        savePpm((out/(std::string(names[scene])+".ppm")).string());
    }
    unsigned cases=0;
    for(int wall:{0,1,2})for(int rug:{0,3,4})for(int bed:{0,5,6})for(int decor:{0,7,8}) {
        resetScene();gRoom.data.wallpaper=wall;gRoom.data.rug=rug;gRoom.data.bed=bed;gRoom.data.decor=decor;
        renderPetScreen();++cases;
    }
    for(int corner=0;corner<4;++corner)for(int pose=0;pose<4;++pose)for(int frame=0;frame<4;++frame) {
        resetScene();gRoom.data.wallpaper=2;gRoom.data.rug=4;gRoom.data.bed=6;gRoom.data.decor=8;gRoom.data.coins=99999;
        gPetGame.x=corner&1?PetGame::MAX_X:PetGame::MIN_X;gPetGame.y=corner&2?PetGame::MAX_Y:PetGame::MIN_Y;
        if(pose==1)gPetGame.feed(1000);if(pose==2)gPetGame.play(1000);if(pose==3)gPetGame.sleep(1000);
        fakeNowMs=1200+frame*180;renderPetScreen();++cases;
    }
    for(int phase=0;phase<3;++phase)for(bool running:{false,true})for(uint32_t remaining:{uint32_t(1),uint32_t(1000),uint32_t(60000)})for(uint32_t coins:{uint32_t(0),uint32_t(20),uint32_t(99999)}) {
        resetScene();gPomodoro.data.phase=phase;gPomodoro.data.running=running;gPomodoro.data.remainingMs=remaining;
        gPomodoro.data.completedFocus=UINT32_MAX;gPomodoro.data.totalFocusMinutes=UINT32_MAX;gRoom.data.coins=coins;
        renderPomodoroScreen();++cases;
    }
    resetScene();gPetGame.data.xp=9899;gRoom.data.coins=99999;gRoom.data.wallpaper=2;gRoom.data.rug=4;gRoom.data.bed=6;gRoom.data.decor=8;
    assert(225-measureText4Bit(FONT_STUDY_10,"L99 C99999") > 14+measureText4Bit(FONT_STUDY_13,"치이카와의 방"));
    renderPetScreen();savePpm((out/"room-max-hud.ppm").string());++cases;
    gPomodoro.data.focusMinutes=90;gPomodoro.data.remainingMs=5400000;gPomodoro.data.completedFocus=UINT32_MAX;gPomodoro.data.totalFocusMinutes=UINT32_MAX;
    assert(measureText4Bit(FONT_TIME_40,"90:00")<210);
    renderPomodoroScreen();savePpm((out/"pomo-90-min.ppm").string());++cases;
    for(int phase=0;phase<3;++phase)for(bool running:{false,true}){
        resetScene();gPomodoro.data.focusMinutes=90;gPomodoro.data.shortBreakMinutes=30;gPomodoro.data.longBreakMinutes=60;
        gPomodoro.data.phase=phase;gPomodoro.data.running=running;gPomodoro.data.remainingMs=gPomodoro.phaseDurationMs();
        renderPomodoroScreen();++cases;
    }
    for(int i=0;i<24;++i){resetScene();fakeNowMs=1500+i*100;gPomodoro.data.running=true;gPomodoro.data.remainingMs=1125000-i*100;renderPomodoroScreen();savePpm((out/("frame-"+std::to_string(i)+".ppm")).string());}
    printf("PASS: actual room/study renderers, %u furniture/state/bounds cases under ASan/UBSan.\n", cases);
}
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=Path('/tmp/geekmagic-study-preview'))
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    source=(FIRMWARE/'src/main.cpp').read_text()
    helpers=source[source.index('struct ThemePalette {'):source.index('static void blitRleStreamStrip(')]
    study=(FIRMWARE/'src/study_renderer.h').read_text()
    pet=(FIRMWARE/'src/pet_renderer.h').read_text()
    assets=(FIRMWARE/'src/study_assets.h').read_text()
    # Every Korean label newly drawn with the study fonts must be available.
    required=set()
    for text in [study,'치이카와의 방']:
        for literal in re.findall(r'"([^"\n]*)"',text):required.update(ord(c)for c in literal if ord(c)>=128)
    required.update(ord(c)for c in '치이카와의 방')
    for font in ['FONT_STUDY_10','FONT_STUDY_13']:
        glyphs=re.search(rf'{font}_GLYPHS\[\d+\].*?=\s*\{{(.*?)\}};',assets,re.S)
        codes={int(x)for x in re.findall(r'\{\s*(\d+),',glyphs.group(1))}
        assert not required-codes,(font,required-codes)
    with tempfile.TemporaryDirectory(prefix='geekmagic-study-')as folder:
        temp=Path(folder);(temp/'Arduino.h').write_text(ARDUINO_STUB);(temp/'pgmspace.h').write_text(PGMSPACE_STUB)
        includes='\n'.join(f'#include "{FIRMWARE/"src"/name}"'for name in ['chiikawa_assets.h','pet_assets.h','study_assets.h','pet_game.h','pomodoro.h','room_decor.h'])
        renderers='\n'.join(f'#include "{FIRMWARE/"src"/name}"'for name in ['room_renderer.h','pet_renderer.h','study_renderer.h'])
        (temp/'preview.cpp').write_text(HOST_PRELUDE+includes+GLOBALS+helpers+renderers+MAIN)
        subprocess.run([shutil.which('clang++'),'--std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(temp),str(temp/'preview.cpp'),'-o',str(temp/'preview')],check=True)
        subprocess.run([str(temp/'preview'),str(args.output.resolve())],check=True)
    names=['room-default','room-soda','room-night','room-berry-rug','room-star-rug','room-cloud-bed','room-moon-bed','room-plant','room-star-lamp','room-cozy','pomo-ready','pomo-focus','pomo-paused','pomo-break','pomo-long-break','pomo-reward']
    collage=Image.new('RGB',(960,1040),'#fff4f8');draw=ImageDraw.Draw(collage)
    for i,name in enumerate(names):
        file=args.output/f'{name}.ppm'
        with Image.open(file)as image:
            image.save(args.output/f'{name}.png');x,y=(i%4)*240,(i//4)*260;collage.paste(image,(x,y));draw.text((x+7,y+244),name,fill='#593b46')
        file.unlink()
    collage.save(args.output/'study-scenes.png')
    for name in ['room-max-hud','pomo-90-min']:
        file=args.output/f'{name}.ppm'
        with Image.open(file)as image:image.save(args.output/f'{name}.png')
        file.unlink()
    frames=[]
    for i in range(24):
        file=args.output/f'frame-{i}.ppm'
        with Image.open(file)as image:frames.append(image.copy())
        file.unlink()
    frames[0].save(args.output/'pomodoro.gif',save_all=True,append_images=frames[1:],duration=100,loop=0,optimize=False,disposal=2)
    print(f'Glyph coverage and exact previews: {args.output}')

if __name__=='__main__':main()
