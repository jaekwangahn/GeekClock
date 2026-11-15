#include "../src/mario_scene.h"
#include <cassert>
#include <cstring>
#include <iostream>
int main() {
    char out[6];
    mario_scene::formatTime(0,5,true,false,out);assert(strcmp(out,"0:05")==0);
    mario_scene::formatTime(0,5,true,true,out);assert(strcmp(out,"12:05")==0);
    mario_scene::formatTime(12,59,true,true,out);assert(strcmp(out,"12:59")==0);
    mario_scene::formatTime(23,59,true,true,out);assert(strcmp(out,"11:59")==0);
    mario_scene::formatTime(23,59,true,false,out);assert(strcmp(out,"23:59")==0);
    mario_scene::formatTime(24,0,true,false,out);assert(strcmp(out,"--:--")==0);
    mario_scene::formatTime(0,60,true,false,out);assert(strcmp(out,"--:--")==0);
    mario_scene::formatTime(0,0,false,false,out);assert(strcmp(out,"--:--")==0);
    for(int hour=0;hour<24;++hour) for(int min=0;min<60;++min) for(bool twelve:{false,true}) {
        mario_scene::formatTime(hour,min,true,twelve,out);
        const int units=int(mario_scene::timeUnits(out));
        assert(units>0 && units<=17);const int cellW=232/units<15?232/units:15;
        assert(units*cellW<=232);assert(strlen(out)>=4 && strlen(out)<=5);
    }
    assert(mario_scene::jumpHeight(0)==0);assert(mario_scene::jumpHeight(450)==64);
    assert(mario_scene::jumpHeight(900)==0);assert(mario_scene::jumpHeight(UINT32_MAX)==0);
    for(uint32_t t=0;t<=900;++t) assert(mario_scene::jumpHeight(t)>=0 && mario_scene::jumpHeight(t)<=64);
    for(uint32_t t=0;t<300000;t+=37) assert(mario_scene::walkX(t)>=0 && mario_scene::walkX(t)<=176);
    assert(mario_scene::isNight(23)&&mario_scene::isNight(0)&&!mario_scene::isNight(12));
    assert(mario_scene::isDusk(18)&&mario_scene::isDusk(5)&&!mario_scene::isDusk(12));
    assert(mario_scene::glyphWidth('1')==1&&mario_scene::glyphWidth('2')==3);
    puts("PASS: all 2880 clock formats, layout bounds, jumps, movement and day/night helpers.");
}
