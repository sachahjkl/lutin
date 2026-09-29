#include "image.h"
#include "runtime.h"
#include "text.h"
#include "tools.h"
#include "workspace.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool fail_restore;
int __real_rename(const char *, const char *);
int __wrap_rename(const char *source, const char *destination) {
    if (fail_restore && strstr(source, ".write.tmp") &&
        strstr(destination, "/main.lua")) {
        errno = EIO;
        return -1;
    }
    struct stat info;
    if (stat(destination, &info) == 0) {
        errno = EEXIST;
        return -1;
    }
    return __real_rename(source, destination);
}

static char output[16384];
static unsigned storage_reads, storage_writes;
static char *count_read(const char *path, size_t limit) {
    (void)path;
    (void)limit;
    storage_reads++;
    char *data = malloc(3);
    memcpy(data, "{}", 3);
    return data;
}
static bool count_write(const char *path, const char *data) {
    (void)path;
    (void)data;
    storage_writes++;
    return true;
}
static void execute(const char *code) {
    if (!tools_execute(code, output, sizeof(output))) {
        fprintf(stderr, "%s\n%s\n", code, output);
        abort();
    }
}

static char *fixture(const char *path) {
    FILE *file = fopen(path, "rb");
    assert(file);
    char *data = malloc(RUNTIME_ASSET_FILE_BYTES + 1);
    assert(data);
    size_t length = fread(data, 1, RUNTIME_ASSET_FILE_BYTES, file);
    assert(!ferror(file) && fgetc(file) == EOF);
    data[length] = 0;
    assert(fclose(file) == 0);
    return data;
}

int main(int argc, char **argv) {
    assert(argc == 3);
    workspace_init(true, "work");
    runtime_set_asset_reader(workspace_read);
    runtime_set_save_writer(workspace_write);
    execute("local value=1 for i=1,12 do value={child=value} end return value");
    const char *program =
        "local x,n,pressed=0,0,0 local random=math.random(1,100000) "
        "assert(ds.buttons()==0) local tx,ty,down=ds.touch(); assert(tx==0 and "
        "ty==0 and not down) "
        "function update() local held,edge=ds.buttons(); if held & ds.RIGHT ~= "
        "0 then x=x+1 end "
        "if edge & ds.A ~= 0 then pressed=pressed+1 end n=n+1 end "
        "function draw() ds.clear(0x102030) ds.rect(x,10,8,8,0xff0000) end "
        "function inspect() local tx,ty,t=ds.touch(); return "
        "{x=x,n=n,pressed=pressed,random=random,touch={x=tx,y=ty,down=t}} end";
    assert(workspace_write("main.lua", program));
    execute("tools.start_test('main.lua',17); local "
            "s=tools.step_frames(60,tools.RIGHT); assert(s.state.x==60 and "
            "s.frames==60); return s");
    uint16_t screen[RUNTIME_SCREEN_WIDTH * RUNTIME_SCREEN_HEIGHT] = {0};
    runtime_frame((RuntimeInput){.pixels = screen, .held = BUTTON_LEFT});
    execute("assert(tools.inspect_runtime().frames==60); local "
            "s=tools.step_frames(10,tools.A | tools.TOUCH,23,45); "
            "assert(s.state.pressed==1 and s.state.touch.down and "
            "s.state.touch.x==23); return s");
    execute("local r=tools.inspect_runtime().state.random; "
            "tools.start_test('main.lua',17); "
            "assert(tools.inspect_runtime().state.random==r); return "
            "tools.step_frames(1,0)");
    assert(!tools_execute("tools.step_frames(100,0); tools.step_frames(21,0)",
                          output, sizeof(output)));
    assert(strstr(output, "120 frames"));
    tools_set_image_support(false);
    assert(!tools_execute("return tools.capture_screen()", output,
                          sizeof(output)));
    tools_set_image_support(true);
    execute("return tools.capture_screen()");
    assert(strstr(output, ".png"));
    char *url = image_data_url(output);
    assert(url && !strncmp(url, "data:image/png;base64,iVBOR", 26));
    free(url);

    assert(workspace_write("save-data.json", "{\"score\":42}"));
    assert(workspace_checkpoint("working", false));
    assert(workspace_write("main.lua", "broken"));
    assert(workspace_write("extra.lua", "new file"));
    fail_restore = true;
    assert(!workspace_checkpoint("working", true));
    fail_restore = false;
    char *restored = workspace_read("main.lua", RUNTIME_ASSET_FILE_BYTES);
    assert(restored && !strcmp(restored, program));
    free(restored);
    assert(workspace_missing("extra.lua"));
    restored = workspace_read("save-data.json", RUNTIME_SAVE_BYTES);
    assert(restored && strstr(restored, "42"));
    free(restored);
    assert(!workspace_checkpoint("../escape", true));
    assert(!tools_execute("tools.write_file('save-data.json','x','missing')",
                          output, sizeof(output)));

    assert(rename("work/projects/1/main.lua",
                  "work/projects/1/.main.lua.bak") == 0);
    assert(workspace_checkpoint("backup", false));
    assert(workspace_remove("main.lua"));
    assert(workspace_checkpoint("backup", true));
    restored = workspace_read("main.lua", RUNTIME_ASSET_FILE_BYTES);
    assert(restored && !strcmp(restored, program));
    free(restored);
    for (unsigned i = 0; i < 80; i++) {
        char name[32];
        snprintf(name, sizeof(name), "extra-%u.lua", i);
        assert(workspace_write(name, "extra"));
    }
    assert(!workspace_checkpoint("too-many", false));
    assert(workspace_write("orphan.lua", "extra"));
    assert(rename("work/projects/1/orphan.lua",
                  "work/projects/1/.orphan.lua.bak") == 0);
    char *large = malloc(RUNTIME_ASSET_FILE_BYTES + 2);
    memset(large, 'x', RUNTIME_ASSET_FILE_BYTES + 1);
    large[RUNTIME_ASSET_FILE_BYTES + 1] = 0;
    assert(workspace_write("large.txt", large));
    free(large);
    assert(workspace_checkpoint("working", true));
    assert(workspace_missing("orphan.lua") && workspace_missing("large.txt"));
    for (unsigned i = 0; i < 80; i++) {
        char name[32];
        snprintf(name, sizeof(name), "extra-%u.lua", i);
        assert(workspace_missing(name));
    }
    assert(workspace_select(2));
    assert(workspace_write("restore-pending.json", "{}"));
    assert(workspace_select(1));
    assert(!workspace_select(2));
    assert(!strcmp(workspace_root(), "work/projects/1"));

    assert(workspace_write("part.lua", "return {value=17}"));
    assert(runtime_start_test(
        "local a=ds.module('part.lua'); local b=ds.module('part.lua'); "
        "assert(a==b) "
        "function inspect() return {value=a.value,save=ds.load_save()} end",
        1));
    execute("local s=tools.inspect_runtime(); assert(s.state.value==17 and "
            "s.state.save.score==42); return s");
    assert(workspace_write("cycle.lua", "return ds.module('cycle.lua')"));
    assert(!runtime_start("local a=ds.module('cycle.lua')"));
    assert(strstr(runtime_error(), "cycle"));

    const char *boundaries[] = {
        "function inspect() local t=7 for i=1,8 do t={t} end return t end",
        "function inspect() local t={} for i=1,255 do t[i]=i end return t end"};
    for (unsigned i = 0; i < 2; i++) {
        assert(runtime_start_test(boundaries[i], 1));
        execute("local s=tools.inspect_runtime(); assert(not "
                "s.inspection_error); return s");
        cJSON *arguments = cJSON_CreateObject();
        assert(
            tools_call("inspect_runtime", arguments, output, sizeof(output)));
        assert(!strstr(output, "inspection_error"));
        cJSON_Delete(arguments);
    }
    assert(runtime_start_test(
        "ds.clear(0xff0000); function init() ds.rect(0,0,1,1,0x00ff00) end",
        1));
    assert(runtime_pixels()[0] == (0x8000 | 0x3e0));
    assert(runtime_pixels()[1] == 0x801f);
    assert(!runtime_start_test("ds.clear(0); error('reject replacement')", 1));
    assert(runtime_pixels()[1] == 0x801f);

    runtime_set_save_writer(count_write);
    assert(runtime_start(
        "function update() for i=1,100 do ds.save({x=1}) end end"));
    runtime_frame((RuntimeInput){.pixels = screen});
    assert(!runtime_running() && strstr(runtime_error(), "Storage budget"));
    assert(storage_writes == RUNTIME_STORAGE_OPERATIONS);
    storage_writes = 0;
    assert(runtime_start(
        "local data={a=string.rep('a',4000),b=string.rep('b',4000)} function "
        "update() for i=1,100 do ds.save(data) end end"));
    runtime_frame((RuntimeInput){.pixels = screen});
    assert(!runtime_running() && strstr(runtime_error(), "Storage budget"));
    assert(storage_writes == 2);
    runtime_set_asset_reader(count_read);
    assert(runtime_start(
        "function update() for i=1,100 do ds.load_save() end end"));
    runtime_frame((RuntimeInput){.pixels = screen});
    assert(!runtime_running() && strstr(runtime_error(), "Storage budget"));
    assert(storage_reads == 2);
    runtime_set_asset_reader(workspace_read);
    runtime_set_save_writer(workspace_write);

    assert(runtime_start_test("function inspect() while true do end end", 1));
    execute("local s=tools.inspect_runtime(); assert(s.inspection_error and "
            "s.running); return s");
    assert(runtime_start_test(
        "function inspect() local t={} t.self=t return t end", 1));
    execute("local s=tools.inspect_runtime(); assert(s.inspection_error); "
            "return s");
    assert(runtime_start_test("function update() while true do end end", 1));
    assert(!runtime_step(1, 0, 0, 0));
    cJSON *inspection = runtime_inspect();
    assert(cJSON_GetObjectItemCaseSensitive(inspection, "budget_failures")
               ->valueint == 1);
    cJSON_Delete(inspection);

    assert(runtime_start_test(
        "local s=ds.sprite({'0'},{0xff0000}) local m=ds.tilemap({'0.'},{s},1) "
        "local a=ds.animation({s},1) function draw() ds.clear(0) "
        "ds.camera(10,0); ds.draw_tilemap(m,10,0); "
        "ds.draw_animation(a,11,0); assert(ds.overlap(0,0,2,2,1,1,2,2)); "
        "local w,h=ds.measure_text('éà\\nœ'); assert(w==16 and h==16) end",
        1));
    assert(runtime_step(1, 0, 0, 0));
    assert(runtime_pixels()[0] == 0x801f && runtime_pixels()[1] == 0x801f);
    assert(runtime_start_test(
        "local clicks=0 function draw() if ds.button(0,0,40,20,'OK') then "
        "clicks=clicks+1 end end "
        "function inspect() return {clicks=clicks} end",
        1));
    assert(runtime_step(3, BUTTON_TOUCH, 5, 5));
    execute("assert(tools.inspect_runtime().state.clicks==1); return 'button "
            "passed'");

    assert(runtime_start_test(
        "local m=ds.mesh({{-1,-1,0},{1,-1,0},{0,1,0}},{{1,2,3,0x00ff00}}) "
        "function draw() ds.clear(0); ds.draw_mesh(m,0,0,5,0) end",
        1));
    assert(runtime_step(1, 0, 0, 0));
    assert(runtime_pixels()[96 * 256 + 128] == (0x8000 | 0x3e0));
    assert(runtime_start_test(
        "local vertices={{-1,-1,0},{1,-1,0},{0,1,0}} "
        "local front=ds.mesh(vertices,{{1,2,3,0xff0000}}) "
        "local back=ds.mesh(vertices,{{1,2,3,0x00ff00}}) "
        "function draw() ds.clear(0); ds.draw_mesh(front,0,0,3,0); "
        "ds.draw_mesh(back,0,0,5,0) end",
        1));
    assert(runtime_step(1, 0, 0, 0));
    assert(runtime_pixels()[96 * 256 + 128] == 0x801f);
    assert(runtime_start_test(
        "local m=ds.mesh({{-0.2,-0.2,-1},{0.2,-0.2,1},{0,0.2,1}},"
        "{{1,2,3,0xff0000}}) "
        "function draw() ds.clear(0); ds.draw_mesh(m,0,0,0,0) end",
        1));
    assert(runtime_step(1, 0, 0, 0));
    unsigned colored = 0;
    for (unsigned i = 0; i < RUNTIME_SCREEN_WIDTH * RUNTIME_SCREEN_HEIGHT; i++)
        colored += runtime_pixels()[i] == 0x801f;
    assert(colored > 0);
    runtime_stop();
    unsigned char ascii[96 * TEXT_GLYPH_BYTES] = {0};
    unsigned char glyphs[TEXT_GLYPHS * TEXT_GLYPH_BYTES];
    text_extend_font(glyphs, ascii);
    runtime_set_font(glyphs);
    assert(runtime_start_test(
        "function draw() ds.clear(0); ds.text(0,0,'é',0xffffff) end", 1));
    assert(runtime_step(1, 0, 0, 0));
    for (unsigned y = 0; y < TEXT_GLYPH_BYTES; y++)
        for (unsigned x = 0; x < TEXT_GLYPH_BYTES; x++)
            assert(runtime_pixels()[y * RUNTIME_SCREEN_WIDTH + x] ==
                   ((text_latin[233 - 192][y] & (1 << x)) ? 0xffff : 0x8000));
    runtime_stop();
    const char *text = "é e\xcc\x81 ç œ";
    assert(text_next(&text) == 233);
    assert(text_next(&text) == ' ');
    assert(text_next(&text) == 233);
    assert(text_next(&text) == ' ');
    assert(text_next(&text) == 231);
    assert(text_next(&text) == ' ');
    assert(text_next(&text) == 129);
    char *demo = fixture(argv[1]);
    assert(workspace_write("main.lua", demo));
    free(demo);
    char *script = fixture(argv[2]);
    execute(script);
    free(script);
    execute("assert(tools.inspect_runtime().budget_failures==0); "
            "tools.finish_test(); return 'Platformer passed'");
    runtime_stop();
    puts("Creation: deterministic input, inspection, budgets, PNG, checkpoint "
         "recovery, modules, saves, tiles, animation, widgets, 3D and accents "
         "passed");
    return 0;
}
