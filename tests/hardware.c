#include "runtime.h"
#include <assert.h>
#include <nds.h>
#include <stdio.h>
#include <string.h>

static const uint16_t *step(const char *code) {
    assert(runtime_start_test(code, 17));
    assert(runtime_step(1, 0, 0, 0));
    const uint16_t *pixels = runtime_pixels();
    assert(pixels);
    return pixels;
}

void __real_runtime_set_font(const unsigned char *);
void __wrap_runtime_set_font(const unsigned char *font) {
    __real_runtime_set_font(font);
    consoleDebugInit(DebugDevice_NOCASH);
    irqEnable(IRQ_VBLANK);
    const uint16_t *pixels =
        step("local s=ds.sprite({'0.','.0'},{0xff0000}) "
             "function draw() ds.clear(0x0000ff); ds.rect(4,4,12,12,0x00ff00); "
             "ds.draw_sprite(s,6,6,2) end");
    assert(pixels[0] == (0x8000 | (31 << 10)));
    assert(pixels[7 * 256 + 7] == 0x801f);
    assert(pixels[7 * 256 + 9] == (0x8000 | (31 << 5)));
    assert(pixels[9 * 256 + 9] == 0x801f);
    fprintf(stderr, "GPU PASS sprite transparency and scaling\n");
    pixels = step("function draw() ds.clear(0); ds.line(2,2,20,2,0xff0000); "
                  "ds.line(2,4,2,20,0x00ff00) end");
    assert(pixels[2 * 256 + 10] == 0x801f);
    assert(pixels[10 * 256 + 2] == (0x8000 | (31 << 5)));
    fprintf(stderr, "GPU PASS horizontal and vertical lines\n");
    pixels =
        step("local m=ds.mesh({{-1,-1,0},{1,-1,0},{0,1,0}},{{1,2,3,0x00ff00}}) "
             "function draw() ds.clear(0); ds.rect(0,0,256,192,0x0000ff); "
             "ds.draw_mesh(m,0,0,5,0); ds.rect(120,88,16,16,0xff0000) end");
    assert(pixels[96 * 256 + 128] == 0x801f);
    assert(pixels[106 * 256 + 120] == (0x8000 | (31 << 5)));
    assert(pixels[0] == (0x8000 | (31 << 10)));
    fprintf(stderr, "GPU PASS background, mesh and HUD layering\n");
    pixels = step(
        "local v={{-1,-1,0},{1,-1,0},{0,1,0}} local "
        "a=ds.mesh(v,{{1,2,3,0xff0000}}) local b=ds.mesh(v,{{1,2,3,0x00ff00}}) "
        "function draw() ds.clear(0); ds.draw_mesh(a,0,0,3,0); "
        "ds.draw_mesh(b,0,0,5,0) end");
    assert(pixels[96 * 256 + 128] == 0x801f);
    fprintf(stderr, "GPU PASS depth ordering\n");
    pixels = step("local m=ds.mesh({{-1,-1,0.1},{1,-1,1},{0,1,1}},"
                  "{{1,2,3,0xff0000}}) function draw() ds.clear(0); "
                  "ds.draw_mesh(m,0,0,0,0) end");
    assert(pixels[96 * 256 + 128] == 0x801f);
    fprintf(stderr, "GPU PASS near-plane boundary triangle\n");
    pixels =
        step("local m=ds.mesh({{-1,-1,0},{1,-1,0},{0,1,0}},{{1,2,3,0xff0000}}) "
             "function draw() ds.clear(0); ds.draw_mesh(m,0,0,200,0); "
             "ds.draw_mesh(m,0,0,-2,0) end");
    for (unsigned i = 0; i < 256 * 192; i++)
        assert(pixels[i] == 0x8000);
    pixels = step(
        "ds.clear(0xff0000) function draw() ds.rect(8,8,4,4,0xffffff) end");
    assert(pixels[0] == 0x801f);
    assert(!runtime_start_test("ds.clear(0); error('rejected')", 1));
    assert(runtime_pixels()[0] == 0x801f);
    assert(runtime_step(1, 0, 0, 0));
    assert(runtime_pixels()[0] == 0x801f);
    assert(!runtime_start_test(
        "function init() ds.clear(0); error('rejected') end", 1));
    assert(runtime_step(1, 0, 0, 0));
    assert(runtime_pixels()[0] == 0x801f);
    fprintf(stderr, "GPU PASS clipping and startup rollback\n");
    assert(runtime_step(4, KEY_RIGHT | KEY_TOUCH, 23, 45));
    assert(
        runtime_start_test("assert(ds.buttons()==0) local x,y,down=ds.touch(); "
                           "assert(x==0 and y==0 and not down) "
                           "local red=ds.sprite({'0'},{0xff0000}) "
                           "local green=ds.sprite({'0'},{0x00ff00}) "
                           "local a=ds.animation({red,green},1) "
                           "ds.clear(0); ds.draw_animation(a,0,0)",
                           17));
    assert(runtime_pixels()[0] == 0x801f);
    fprintf(stderr, "GPU PASS seeded startup input and animation reset\n");
    pixels = step("function draw() ds.clear(0); ds.text(0,0,'é',0xffffff) end");
    unsigned visible = 0;
    for (unsigned y = 0; y < 8; y++)
        for (unsigned x = 0; x < 8; x++)
            visible += pixels[y * 256 + x] == 0xffff;
    assert(visible > 5);
    assert(runtime_start_test(
        "function draw() for i=1,513 do ds.rect(0,0,1,1,0xffffff) end end", 1));
    assert(!runtime_step(1, 0, 0, 0));
    assert(strstr(runtime_error(), "GPU 2D phase budget"));
    assert(runtime_start_test(
        "local faces={} for i=1,256 do faces[i]={1,2,3,0xffffff} end "
        "local m=ds.mesh({{-16,-16,0},{16,-16,0},{0,16,0}},faces) "
        "function draw() ds.clear(0); for i=1,3 do ds.draw_mesh(m,0,0,1,0) end "
        "end",
        1));
    assert(!runtime_step(1, 0, 0, 0));
    assert(strstr(runtime_error(), "GPU geometry budget"));
    fprintf(stderr, "GPU PASS clipping expansion budget\n");
    for (unsigned i = 0; i < 3; i++) {
        assert(!runtime_start_test(
            "local rows={} for i=1,64 do rows[i]=string.rep('0',64) end local "
            "s={} for i=1,13 do s[i]=ds.sprite(rows,{0xffffff}) end",
            1));
        assert(strstr(runtime_error(), "texture budget"));
    }
    pixels = step("ds.clear(0x00ff00)");
    assert(pixels[0] == (0x8000 | (31 << 5)));
    fprintf(stderr,
            "GPU PASS font, budgets and texture cleanup\nGPU ALL PASS\n");
    runtime_stop();
}
