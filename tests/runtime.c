#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned frequencies[4], volumes[4], audio_events;
static void audio(unsigned voice, unsigned frequency, unsigned volume) {
    assert(voice < 4);
    frequencies[voice] = frequency;
    volumes[voice] = volume;
    audio_events++;
}

static char *asset(const char *path, size_t limit) {
    const char *text = "return ds.sprite({'0.','.1'},{0xff0000,0x00ff00})";
    if (strcmp(path, "hero.lua") || strlen(text) >= limit)
        return NULL;
    char *copy = malloc(strlen(text) + 1);
    assert(copy);
    strcpy(copy, text);
    return copy;
}

int main(void) {
    runtime_set_asset_reader(asset);
    runtime_set_audio(audio);
    assert(runtime_start("local s=ds.load_asset('hero.lua'); function draw() "
                         "ds.clear(0); ds.draw_sprite(s,-1,0,2,true) end"));
    uint16_t media_pixels[256 * 192] = {0};
    RuntimeInput media_input = {.pixels = media_pixels};
    runtime_frame(media_input);
    assert(media_pixels[0] == 0x8000 && media_pixels[1] == 0x801f);
    assert(media_pixels[512] == 0x83e0 && media_pixels[514] == 0x8000);
    assert(!runtime_start("ds.sprite({'0','00'},{0xffffff})"));
    assert(!runtime_start("ds.sprite({'f'},{0xffffff})"));
    assert(!runtime_start("ds.sound('square',{{440,0,80}})"));
    assert(!runtime_start("ds.sound('noise',{{-1,1,80}})"));
    assert(!runtime_start("ds.play_sound(ds.sound('noise',{{440,1,80}}),1)"));
    runtime_stop();
    audio_events = 0;
    assert(runtime_start(
        "ds.play_sound(ds.sound('square',{{440,2,80},{0,1,0},{880,1,90}}),2)"));
    assert(audio_events == 0);
    runtime_frame(media_input);
    assert(frequencies[1] == 440 && volumes[1] == 80);
    assert(!runtime_start(
        "ds.play_sound(ds.sound('square',{{220,1,80}}),2); error('reject')"));
    assert(frequencies[1] == 440);
    runtime_frame(media_input);
    assert(frequencies[1] == 440);
    runtime_frame(media_input);
    assert(frequencies[1] == 0);
    runtime_frame(media_input);
    assert(frequencies[1] == 880);
    runtime_frame(media_input);
    assert(frequencies[1] == 0);
    assert(runtime_start(
        "ds.play_sound(ds.sound('noise',{{1000,1,60},{2000,1,40}}),4,true)"));
    runtime_frame(media_input);
    assert(frequencies[3] == 1000);
    runtime_frame(media_input);
    assert(frequencies[3] == 2000);
    runtime_frame(media_input);
    assert(frequencies[3] == 1000);
    runtime_stop();
    assert(frequencies[3] == 0);
    assert(runtime_start("function update() ds.load_asset('hero.lua') end"));
    runtime_frame(media_input);
    assert(!runtime_running() && strstr(runtime_error(), "startup"));
    assert(!runtime_start(
        "setmetatable(_G,{__index=function() error('lookup') end})"));
    assert(runtime_start(
        "function init() end; setmetatable(_G,{__index=function() "
        "error('update lookup') end})"));
    runtime_frame((RuntimeInput){0});
    assert(!runtime_running());
    assert(runtime_start(
        "function init() end; function update() end; "
        "setmetatable(_G,{__index=function() while true do end end})"));
    runtime_frame((RuntimeInput){0});
    assert(!runtime_running());
    assert(runtime_start("assert(string.rep('',2147483647)=='')"));
    assert(!runtime_start("string.rep('x',2147483647)"));
    runtime_stop();
    uint16_t pixels[256 * 192] = {0};
    RuntimeInput input = {.pixels = pixels};
    assert(runtime_start(
        "function draw() ds.clear(0); ds.rect(-2,-2,4,4,0xff0000) end"));
    runtime_frame(input);
    assert(pixels[0] == 0x801f);
    assert(pixels[257] == 0x801f);
    assert(pixels[258] == 0x8000);
    assert(!runtime_start("function invalid("));
    assert(runtime_running());
    assert(runtime_start("function update() while true do end end"));
    runtime_frame(input);
    assert(!runtime_running());
    assert(strstr(runtime_error(), "budget"));
    assert(!runtime_start("while true do end"));
    assert(runtime_start("function update() local t={} for i=1,100 do "
                         "t[i]=string.rep('x',100000) end end"));
    runtime_frame(input);
    assert(!runtime_running());
    assert(runtime_memory() == 0);
    assert(
        runtime_start("function draw() for i=1,1000 do ds.clear(0) end end"));
    runtime_frame(input);
    assert(!runtime_running());
    assert(strstr(runtime_error(), "Drawing budget"));
    assert(!runtime_start(
        "assert(io or os or package or debug or pcall or xpcall)"));
    assert(runtime_start("function draw() ds.rect(100000,0,1,1,0) end"));
    runtime_frame(input);
    assert(!runtime_running());
    assert(runtime_start("function draw() ds.clear(0x00ff00) end"));
    runtime_frame(input);
    assert(pixels[0] == 0x83e0);
    runtime_stop();
    assert(runtime_memory() == 0);
    puts("Runtime: rendering, errors, budgets and recovery passed");
    return 0;
}
