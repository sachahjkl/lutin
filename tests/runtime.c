#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
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
