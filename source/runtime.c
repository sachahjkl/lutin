#include "runtime.h"
#include "lua_value.h"
#include "sandbox.h"
#include "text.h"
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef ARM9
#include <nds.h>
#endif

typedef struct {
    unsigned frequency, frames, volume;
} Note;

typedef struct {
    unsigned count;
    bool noise;
    Note notes[];
} Sound;

typedef struct {
    unsigned width, height;
#ifdef ARM9
    uint32_t texture;
#endif
    uint16_t pixels[];
} Sprite;
typedef struct {
    unsigned width, height, tile_size;
    unsigned char cells[];
} Tilemap;
enum { TILEMAP_MAX_SIZE = 64, TILEMAP_EMPTY = 255 };

typedef struct {
    Sound *sound;
    int reference;
    unsigned note, remaining;
    bool loop, dirty;
} Voice;

typedef struct {
    lua_State *state;
    size_t memory;
    size_t peak_memory;
    unsigned instructions;
    unsigned pixels;
    unsigned asset_bytes;
    unsigned storage_operations, storage_bytes;
    unsigned seed;
    int camera_x, camera_y;
    float eye_x, eye_y, eye_z, eye_yaw, eye_pitch;
    cJSON *temporary_json;
    Voice voices[RUNTIME_AUDIO_VOICES];
#ifdef ARM9
    int textures[128];
    unsigned texture_count, texture_bytes;
#endif
} Runtime;

static Runtime active;
static RuntimeInput input;
static char error[256];
static const unsigned char *font;
static char logs[16][256];
static unsigned log_count;
static RuntimeAssetReader asset_reader;
static RuntimeAudio audio_output;
static RuntimeSaveWriter save_writer;
static bool testing;
static unsigned test_buttons;
static uint16_t test_pixels[RUNTIME_SCREEN_WIDTH * RUNTIME_SCREEN_HEIGHT]
    __attribute__((aligned(32)));
static unsigned frame_count, last_frame_us, peak_frame_us, budget_failures;
static size_t peak_memory;
static RuntimePlatformMetrics platform_metrics;
void runtime_platform_metrics(RuntimePlatformMetrics metrics) {
    platform_metrics = metrics;
}
static Runtime *context(lua_State *state);
#ifdef ARM9
#include "graphics_nds.h"
#endif

static uint64_t microseconds(void) {
#ifdef ARM9
    return systemCounterGetTicks() * 1000000 / (BUS_CLOCK / 64);
#else
    return (uint64_t)clock() * 1000000 / CLOCKS_PER_SEC;
#endif
}

void runtime_set_asset_reader(RuntimeAssetReader reader) {
    asset_reader = reader;
}
void runtime_set_audio(RuntimeAudio output) { audio_output = output; }
void runtime_set_save_writer(RuntimeSaveWriter writer) { save_writer = writer; }

static void log_message(const char *message) {
    snprintf(logs[log_count++ % 16], sizeof(logs[0]), "%s", message);
}

unsigned runtime_logs(unsigned cursor, char *output, size_t capacity) {
    size_t used = 0;
    if (!capacity)
        return cursor;
    output[0] = 0;
    if (log_count > 16 && cursor < log_count - 16)
        cursor = log_count - 16;
    while (cursor < log_count) {
        int size = snprintf(output + used, capacity - used, "%u: %s\n", cursor,
                            logs[cursor % 16]);
        if (size < 0 || (size_t)size >= capacity - used)
            break;
        used += (size_t)size;
        cursor++;
    }
    return cursor;
}

void runtime_set_font(const unsigned char *value) { font = value; }

#ifndef ARM9
static void pixel(int x, int y, uint16_t value) {
    if (input.pixels && x >= 0 && y >= 0 && x < RUNTIME_SCREEN_WIDTH &&
        y < RUNTIME_SCREEN_HEIGHT)
        input.pixels[y * RUNTIME_SCREEN_WIDTH + x] = value;
}
#endif

void runtime_graphics_init(void) {
#ifdef ARM9
    glInit();
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND | GL_ANTIALIAS);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(0, 0, 255, 191);
    graphics.initialized = true;
#endif
}

bool runtime_capture(const char *path) {
    const uint16_t *capture = runtime_pixels();
    if (!capture)
        return false;
    FILE *file = fopen(path, "wb");
    if (!file)
        return false;
    bool ok = fprintf(file, "P6\n%d %d\n255\n", RUNTIME_SCREEN_WIDTH,
                      RUNTIME_SCREEN_HEIGHT) > 0;
    for (int y = 0; y < RUNTIME_SCREEN_HEIGHT && ok; y++) {
        unsigned char row[RUNTIME_SCREEN_WIDTH * 3];
        for (int x = 0; x < RUNTIME_SCREEN_WIDTH; x++) {
            unsigned value = capture[y * RUNTIME_SCREEN_WIDTH + x];
            for (int c = 0; c < 3; c++)
                row[x * 3 + c] = ((value >> (c * 5)) & 31) * 255 / 31;
        }
        ok = fwrite(row, 1, sizeof(row), file) == sizeof(row);
    }
    return fclose(file) == 0 && ok;
}
const uint16_t *runtime_pixels(void) {
#ifdef ARM9
    return graphics_capture(test_pixels) ? test_pixels : NULL;
#else
    return testing ? test_pixels : input.pixels;
#endif
}

static void *allocate(void *context, void *pointer, size_t old, size_t size) {
    Runtime *runtime = context;
    if (!pointer)
        old = 0;
    if (!size) {
        free(pointer);
        runtime->memory -= old;
        return NULL;
    }
    if (size > RUNTIME_MEMORY_BYTES - (runtime->memory - old))
        return NULL;
    void *result = realloc(pointer, size);
    if (result) {
        runtime->memory = runtime->memory - old + size;
        if (runtime->memory > runtime->peak_memory)
            runtime->peak_memory = runtime->memory;
    }
    return result;
}

static void budget(lua_State *state, lua_Debug *debug) {
    (void)debug;
    Runtime *runtime;
    lua_getallocf(state, (void **)&runtime);
    if (++runtime->instructions >= RUNTIME_INSTRUCTIONS / RUNTIME_HOOK_INTERVAL)
        luaL_error(state, "Instruction budget exceeded");
}

static uint16_t color(lua_State *state, int index) {
    unsigned value = (unsigned)luaL_checkinteger(state, index);
    return 0x8000 | ((value >> 19) & 31) | ((value >> 6) & 0x3e0) |
           ((value << 7) & 0x7c00);
}

static void charge_pixels(lua_State *state, unsigned count) {
    Runtime *runtime;
    lua_getallocf(state, (void **)&runtime);
    if (count > RUNTIME_PIXEL_BUDGET - runtime->pixels)
        luaL_error(state, "Drawing budget exceeded");
    runtime->pixels += count;
}

static int clear(lua_State *state) {
    uint16_t value = color(state, 1);
    charge_pixels(state, RUNTIME_SCREEN_WIDTH * RUNTIME_SCREEN_HEIGHT);
#ifdef ARM9
    graphics_begin();
    graphics.clear_color = value;
    graphics.drawing = true;
#else
    if (input.pixels)
        for (int i = 0; i < RUNTIME_SCREEN_WIDTH * RUNTIME_SCREEN_HEIGHT; i++)
            input.pixels[i] = value;
#endif
    return 0;
}

static int coordinate(lua_State *state, int index) {
    lua_Integer value = luaL_checkinteger(state, index);
    luaL_argcheck(state, value >= -4096 && value <= 4096, index,
                  "Coordinate out of range");
    return (int)value;
}

static int rectangle(lua_State *state) {
    int x = coordinate(state, 1), y = coordinate(state, 2);
    int width = coordinate(state, 3), height = coordinate(state, 4);
    uint16_t value = color(state, 5);
    Runtime *runtime;
    lua_getallocf(state, (void **)&runtime);
    x -= runtime->camera_x;
    y -= runtime->camera_y;
    int right = x + width, bottom = y + height;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (right > RUNTIME_SCREEN_WIDTH)
        right = RUNTIME_SCREEN_WIDTH;
    if (bottom > RUNTIME_SCREEN_HEIGHT)
        bottom = RUNTIME_SCREEN_HEIGHT;
    if (right > x && bottom > y)
        charge_pixels(state, (unsigned)((right - x) * (bottom - y)));
#ifdef ARM9
    graphics_quad(state, x, y, right - x, bottom - y, value, 0, 0, 0, 0, 0,
                  false);
#else
    if (input.pixels)
        for (int row = y; row < bottom; row++)
            for (int column = x; column < right; column++)
                input.pixels[row * RUNTIME_SCREEN_WIDTH + column] = value;
#endif
    return 0;
}

static int buttons(lua_State *state) {
    lua_pushinteger(state, input.held);
    lua_pushinteger(state, input.pressed);
    return 2;
}

static int draw_text(lua_State *state) {
    int x = coordinate(state, 1), y = coordinate(state, 2);
    Runtime *runtime;
    lua_getallocf(state, (void **)&runtime);
    x -= runtime->camera_x;
    y -= runtime->camera_y;
    size_t length;
    const unsigned char *text =
        (const unsigned char *)luaL_checklstring(state, 3, &length);
    uint16_t value = color(state, 4);
    if (length > 1024)
        return luaL_error(state, "Text exceeds 1024 bytes");
    charge_pixels(state, (unsigned)length * 64);
    const char *cursor = (const char *)text;
    int origin = x;
    while (*cursor) {
        unsigned char glyph = text_next(&cursor);
        if (glyph == '\n') {
            x = origin;
            y += TEXT_GLYPH_BYTES;
            continue;
        }
#ifdef ARM9
        unsigned index = glyph - TEXT_FIRST_GLYPH;
        graphics_quad(state, x, y, 8, 8, value, graphics.font_format,
                      (index % 16) * 8, (index / 16) * 8, 8, 8, false);
#else
        if (font)
            for (int row = 0; row < 8; row++)
                for (int column = 0; column < 8; column++)
                    if (font[(glyph - TEXT_FIRST_GLYPH) * TEXT_GLYPH_BYTES +
                             row] &
                        (1 << column))
                        pixel(x + column, y + row, value);
#endif
        x += TEXT_GLYPH_BYTES;
    }
    return 0;
}

static int line(lua_State *state) {
    int x = coordinate(state, 1), y = coordinate(state, 2);
    int end_x = coordinate(state, 3), end_y = coordinate(state, 4);
    uint16_t value = color(state, 5);
    Runtime *runtime;
    lua_getallocf(state, (void **)&runtime);
    x -= runtime->camera_x;
    y -= runtime->camera_y;
    end_x -= runtime->camera_x;
    end_y -= runtime->camera_y;
    int dx = abs(end_x - x), dy = -abs(end_y - y);
    charge_pixels(state, (unsigned)(dx > -dy ? dx : -dy) + 1);
#ifdef ARM9
    graphics_line(state, x, y, end_x, end_y, value);
#else
    int sx = x < end_x ? 1 : -1, sy = y < end_y ? 1 : -1;
    int error = dx + dy;
    while (true) {
        pixel(x, y, value);
        if (x == end_x && y == end_y)
            break;
        int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x += sx;
        }
        if (twice <= dx) {
            error += dx;
            y += sy;
        }
    }
#endif
    return 0;
}

static int touch(lua_State *state) {
    lua_pushinteger(state, input.touch_x);
    lua_pushinteger(state, input.touch_y);
    lua_pushboolean(state, input.touching);
    return 3;
}

static Runtime *context(lua_State *state) {
    Runtime *runtime;
    lua_getallocf(state, (void **)&runtime);
    return runtime;
}

static int load_asset(lua_State *state) {
    const char *path = luaL_checkstring(state, 1);
    Runtime *runtime = context(state);
    if (runtime == &active)
        return luaL_error(state, "Load assets at startup or in init");
    if (!asset_reader)
        return luaL_error(state, "Asset storage unavailable");
    if (runtime->asset_bytes >= RUNTIME_ASSET_READ_BYTES)
        return luaL_error(state, "Asset read budget exceeded");
    char *text = asset_reader(path, RUNTIME_ASSET_FILE_BYTES);
    if (!text)
        return luaL_error(state, "Cannot read asset: %s", path);
    size_t length = strlen(text);
    runtime->asset_bytes += (unsigned)length + 1;
    if (runtime->asset_bytes > RUNTIME_ASSET_READ_BYTES) {
        free(text);
        return luaL_error(state, "Asset read budget exceeded");
    }
    int status = luaL_loadbufferx(state, text, length, path, "t");
    free(text);
    if (status != LUA_OK)
        return lua_error(state);
    lua_call(state, 0, 1);
    return 1;
}

static int sprite(lua_State *state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    luaL_checktype(state, 2, LUA_TTABLE);
    unsigned height = (unsigned)lua_rawlen(state, 1);
    luaL_argcheck(state, height >= 1 && height <= RUNTIME_SPRITE_MAX_SIZE, 1,
                  "Sprite height must be 1..64");
    lua_rawgeti(state, 1, 1);
    size_t width;
    luaL_checklstring(state, -1, &width);
    luaL_argcheck(state, width >= 1 && width <= RUNTIME_SPRITE_MAX_SIZE, 1,
                  "Sprite width must be 1..64");
    lua_pop(state, 1);
    unsigned colors = (unsigned)lua_rawlen(state, 2);
    luaL_argcheck(state, colors >= 1 && colors <= RUNTIME_PALETTE_COLORS, 2,
                  "Palette must contain 1..16 colors");
    uint16_t palette[RUNTIME_PALETTE_COLORS];
    for (unsigned i = 0; i < colors; i++) {
        lua_rawgeti(state, 2, i + 1);
        palette[i] = color(state, -1);
        lua_pop(state, 1);
    }
    charge_pixels(state, (unsigned)width * height);
    Sprite *image = lua_newuserdatauv(
        state, sizeof(*image) + width * height * sizeof(uint16_t), 0);
    luaL_setmetatable(state, "lutin.sprite");
    image->width = (unsigned)width;
    image->height = height;
    for (unsigned y = 0; y < height; y++) {
        lua_rawgeti(state, 1, y + 1);
        size_t length;
        const char *row = luaL_checklstring(state, -1, &length);
        if (length != width)
            return luaL_error(state, "Sprite rows must have equal widths");
        for (unsigned x = 0; x < width; x++) {
            static const char symbols[] = "0123456789abcdef";
            const char *symbol = row[x] ? strchr(symbols, row[x]) : NULL;
            if (row[x] != '.' &&
                (!symbol || (unsigned)(symbol - symbols) >= colors))
                return luaL_error(state, "Invalid sprite palette index");
            image->pixels[y * width + x] =
                row[x] == '.' ? 0 : palette[symbol - symbols];
        }
        lua_pop(state, 1);
    }
#ifdef ARM9
    image->texture = graphics_texture(state, image->pixels, image->width,
                                      image->height, false);
#endif
    return 1;
}

static int draw_sprite(lua_State *state) {
    Sprite *image = luaL_checkudata(state, 1, "lutin.sprite");
    int x = coordinate(state, 2), y = coordinate(state, 3);
    x -= context(state)->camera_x;
    y -= context(state)->camera_y;
    lua_Integer scale = luaL_optinteger(state, 4, 1);
    luaL_argcheck(state, scale >= 1 && scale <= RUNTIME_SPRITE_MAX_SCALE, 4,
                  "Scale must be 1..8");
    bool flip = lua_toboolean(state, 5);
    charge_pixels(state,
                  image->width * image->height * (unsigned)(scale * scale));
#ifdef ARM9
    graphics_quad(state, x, y, image->width * scale, image->height * scale,
                  0xffff, image->texture, 0, 0, image->width, image->height,
                  flip);
#else
    for (unsigned row = 0; row < image->height; row++)
        for (unsigned column = 0; column < image->width; column++) {
            uint16_t value =
                image->pixels[row * image->width +
                              (flip ? image->width - column - 1 : column)];
            if (!value)
                continue;
            for (int dy = 0; dy < scale; dy++)
                for (int dx = 0; dx < scale; dx++)
                    pixel(x + (int)column * (int)scale + dx,
                          y + (int)row * (int)scale + dy, value);
        }
#endif
    return 0;
}

#include "runtime_mesh.h"

static int camera(lua_State *state) {
    int x = coordinate(state, 1), y = coordinate(state, 2);
    context(state)->camera_x = x;
    context(state)->camera_y = y;
    return 0;
}

static int measure_text(lua_State *state) {
    size_t length;
    const char *cursor = luaL_checklstring(state, 1, &length);
    luaL_argcheck(state, length <= 1024, 1, "Text exceeds 1024 bytes");
    unsigned width = 0, line_width = 0, height = TEXT_GLYPH_BYTES;
    while (*cursor) {
        if (text_next(&cursor) == '\n') {
            if (line_width > width)
                width = line_width;
            line_width = 0;
            height += TEXT_GLYPH_BYTES;
        } else
            line_width += TEXT_GLYPH_BYTES;
    }
    lua_pushinteger(state, line_width > width ? line_width : width);
    lua_pushinteger(state, height);
    return 2;
}

static int button(lua_State *state) {
    int x = coordinate(state, 1), y = coordinate(state, 2);
    int width = coordinate(state, 3), height = coordinate(state, 4);
    size_t length;
    luaL_checklstring(state, 5, &length);
    luaL_argcheck(
        state, width > 0 && height > 0 && length <= 32, 5,
        "Button requires positive size and a label of at most 32 bytes");
    bool inside = input.touching && input.touch_x >= x && input.touch_y >= y &&
                  input.touch_x < x + width && input.touch_y < y + height;
    Runtime *runtime = context(state);
    lua_pushcfunction(state, rectangle);
    lua_pushinteger(state, x + runtime->camera_x);
    lua_pushinteger(state, y + runtime->camera_y);
    lua_pushinteger(state, width);
    lua_pushinteger(state, height);
    lua_pushinteger(state, inside ? 0x406090 : 0x203040);
    lua_call(state, 5, 0);
    lua_pushcfunction(state, draw_text);
    lua_pushinteger(state, x + runtime->camera_x + 2);
    lua_pushinteger(state, y + runtime->camera_y + 2);
    lua_pushvalue(state, 5);
    lua_pushinteger(state, 0xffffff);
    lua_call(state, 4, 0);
    lua_pushboolean(state, inside && (input.pressed & BUTTON_TOUCH));
    return 1;
}

static int overlap(lua_State *state) {
    int values[8];
    for (unsigned i = 0; i < 8; i++)
        values[i] = coordinate(state, i + 1);
    lua_pushboolean(state, values[2] > 0 && values[3] > 0 && values[6] > 0 &&
                               values[7] > 0 &&
                               values[0] < values[4] + values[6] &&
                               values[4] < values[0] + values[2] &&
                               values[1] < values[5] + values[7] &&
                               values[5] < values[1] + values[3]);
    return 1;
}

static int module(lua_State *state) {
    const char *path = luaL_checkstring(state, 1);
    lua_settop(state, 1);
    lua_getfield(state, LUA_REGISTRYINDEX, "lutin.modules");
    lua_getfield(state, -1, path);
    if (!lua_isnil(state, -1))
        return 1;
    lua_pop(state, 1);
    lua_getfield(state, LUA_REGISTRYINDEX, "lutin.loading");
    lua_getfield(state, -1, path);
    if (lua_toboolean(state, -1))
        return luaL_error(state, "Module cycle: %s", path);
    lua_pop(state, 1);
    lua_pushboolean(state, true);
    lua_setfield(state, -2, path);
    lua_pushcfunction(state, load_asset);
    lua_pushvalue(state, 1);
    lua_call(state, 1, 1);
    if (lua_isnil(state, -1))
        return luaL_error(state, "Module must return a value");
    lua_pushvalue(state, -1);
    lua_setfield(state, 2, path);
    lua_pushnil(state);
    lua_setfield(state, 3, path);
    return 1;
}

static int tilemap(lua_State *state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    luaL_checktype(state, 2, LUA_TTABLE);
    int size = (int)luaL_checkinteger(state, 3);
    luaL_argcheck(state, size >= 1 && size <= RUNTIME_SPRITE_MAX_SIZE, 3,
                  "Tile size must be 1..64");
    unsigned height = lua_rawlen(state, 1), tiles = lua_rawlen(state, 2);
    luaL_argcheck(state, height && height <= TILEMAP_MAX_SIZE, 1,
                  "Map height must be 1..64");
    luaL_argcheck(state, tiles && tiles <= RUNTIME_PALETTE_COLORS, 2,
                  "Map requires 1..16 sprites");
    lua_rawgeti(state, 1, 1);
    size_t width;
    luaL_checklstring(state, -1, &width);
    luaL_argcheck(state, width && width <= TILEMAP_MAX_SIZE, 1,
                  "Map width must be 1..64");
    lua_pop(state, 1);
    for (unsigned i = 1; i <= tiles; i++) {
        lua_rawgeti(state, 2, i);
        Sprite *image = luaL_checkudata(state, -1, "lutin.sprite");
        if (image->width != (unsigned)size || image->height != (unsigned)size)
            return luaL_error(state, "Tile sprites must match tile size");
        lua_pop(state, 1);
    }
    Tilemap *map = lua_newuserdatauv(state, sizeof(*map) + width * height, 1);
    luaL_setmetatable(state, "lutin.tilemap");
    map->width = width;
    map->height = height;
    map->tile_size = size;
    lua_pushvalue(state, 2);
    lua_setiuservalue(state, -2, 1);
    for (unsigned y = 0; y < height; y++) {
        lua_rawgeti(state, 1, y + 1);
        size_t length;
        const char *row = luaL_checklstring(state, -1, &length);
        if (length != width)
            return luaL_error(state, "Map rows must have equal widths");
        for (unsigned x = 0; x < width; x++) {
            static const char symbols[] = "0123456789abcdef";
            const char *digit = row[x] ? strchr(symbols, row[x]) : NULL;
            if (row[x] != '.' &&
                (!digit || (unsigned)(digit - symbols) >= tiles))
                return luaL_error(state, "Invalid tile index");
            map->cells[y * width + x] =
                row[x] == '.' ? TILEMAP_EMPTY : (unsigned)(digit - symbols);
        }
        lua_pop(state, 1);
    }
    return 1;
}

static int draw_tilemap(lua_State *state) {
    Tilemap *map = luaL_checkudata(state, 1, "lutin.tilemap");
    charge_pixels(state, map->width * map->height);
    int x = coordinate(state, 2), y = coordinate(state, 3);
    lua_settop(state, 3);
    lua_getiuservalue(state, 1, 1);
    for (unsigned row = 0; row < map->height; row++)
        for (unsigned col = 0; col < map->width; col++) {
            unsigned cell = map->cells[row * map->width + col];
            int px = x + (int)(col * map->tile_size),
                py = y + (int)(row * map->tile_size);
            int sx = px - context(state)->camera_x,
                sy = py - context(state)->camera_y;
            if (cell == TILEMAP_EMPTY || sx >= RUNTIME_SCREEN_WIDTH ||
                sy >= RUNTIME_SCREEN_HEIGHT || sx + (int)map->tile_size <= 0 ||
                sy + (int)map->tile_size <= 0)
                continue;
            lua_pushcfunction(state, draw_sprite);
            lua_rawgeti(state, 4, cell + 1);
            lua_pushinteger(state, px);
            lua_pushinteger(state, py);
            lua_call(state, 3, 0);
        }
    return 0;
}

static int animation(lua_State *state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    lua_Integer rate = luaL_checkinteger(state, 2);
    size_t count = lua_rawlen(state, 1);
    luaL_argcheck(state, count && count <= RUNTIME_SOUND_MAX_NOTES, 1,
                  "Animation requires 1..128 sprites");
    luaL_argcheck(state, rate >= 1 && rate <= RUNTIME_FRAMES_PER_SECOND, 2,
                  "Frame duration must be 1..60 ticks");
    for (size_t i = 1; i <= count; i++) {
        lua_rawgeti(state, 1, i);
        luaL_checkudata(state, -1, "lutin.sprite");
        lua_pop(state, 1);
    }
    lua_newtable(state);
    lua_pushvalue(state, 1);
    lua_setfield(state, -2, "frames");
    lua_pushinteger(state, rate);
    lua_setfield(state, -2, "duration");
    return 1;
}

static int draw_animation(lua_State *state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    int x = coordinate(state, 2), y = coordinate(state, 3);
    lua_getfield(state, 1, "duration");
    lua_Integer rate = luaL_checkinteger(state, -1);
    lua_getfield(state, 1, "frames");
    luaL_checktype(state, -1, LUA_TTABLE);
    size_t count = lua_rawlen(state, -1);
    if (rate < 1 || count < 1 || count > RUNTIME_SOUND_MAX_NOTES)
        return luaL_error(state, "Invalid animation");
    lua_pushcfunction(state, draw_sprite);
    lua_rawgeti(state, -2, (frame_count / (unsigned)rate) % count + 1);
    lua_pushinteger(state, x);
    lua_pushinteger(state, y);
    lua_call(state, 3, 0);
    return 0;
}

static bool charge_storage(Runtime *runtime, size_t bytes) {
    if (++runtime->storage_operations > RUNTIME_STORAGE_OPERATIONS ||
        bytes > RUNTIME_STORAGE_BYTES - runtime->storage_bytes)
        return false;
    runtime->storage_bytes += (unsigned)bytes;
    return true;
}

static int save_data(lua_State *state) {
    Runtime *runtime = context(state);
    if (runtime != &active || testing || !save_writer)
        return luaL_error(state,
                          "Save data requires live execution and storage");
    ValueBudget value_budget = {0};
    cJSON *value = lua_value_json(state, 1, 0, &value_budget);
    if (!value)
        return luaL_error(state,
                          "Save data must be bounded JSON-compatible data");
    char *encoded = cJSON_PrintUnformatted(value);
    cJSON_Delete(value);
    bool budget_ok = charge_storage(runtime, encoded ? strlen(encoded) : 0);
    bool ok = budget_ok && encoded && strlen(encoded) <= RUNTIME_SAVE_BYTES &&
              save_writer("save-data.json", encoded);
    free(encoded);
    if (!budget_ok)
        return luaL_error(state, "Storage budget exceeded");
    if (!ok)
        return luaL_error(state, "Save data failed or exceeds 8192 bytes");
    return 0;
}

static int load_save(lua_State *state) {
    if (!charge_storage(context(state), RUNTIME_SAVE_BYTES))
        return luaL_error(state, "Storage budget exceeded");
    char *encoded = asset_reader
                        ? asset_reader("save-data.json", RUNTIME_SAVE_BYTES)
                        : NULL;
    if (!encoded) {
        lua_pushnil(state);
        return 1;
    }
    Runtime *runtime = context(state);
    runtime->temporary_json = cJSON_Parse(encoded);
    free(encoded);
    if (!runtime->temporary_json)
        return luaL_error(state, "Invalid save data");
    lua_value_push(state, runtime->temporary_json, 0, VALUE_MAX_DEPTH);
    cJSON_Delete(runtime->temporary_json);
    runtime->temporary_json = NULL;
    return 1;
}

static unsigned note_value(lua_State *state, int index, unsigned minimum,
                           unsigned maximum) {
    lua_rawgeti(state, -1, index);
    lua_Integer value = luaL_checkinteger(state, -1);
    if (value < 0 || (unsigned)value < minimum || (unsigned)value > maximum)
        luaL_error(state, "Sound note value out of range");
    lua_pop(state, 1);
    return (unsigned)value;
}

static int sound(lua_State *state) {
    const char *wave = luaL_checkstring(state, 1);
    luaL_argcheck(state, !strcmp(wave, "square") || !strcmp(wave, "noise"), 1,
                  "Use square or noise");
    luaL_checktype(state, 2, LUA_TTABLE);
    size_t count = lua_rawlen(state, 2);
    luaL_argcheck(state, count >= 1 && count <= RUNTIME_SOUND_MAX_NOTES, 2,
                  "Sound must contain 1..128 notes");
    charge_pixels(state, (unsigned)count);
    Sound *data =
        lua_newuserdatauv(state, sizeof(*data) + count * sizeof(Note), 0);
    luaL_setmetatable(state, "lutin.sound");
    data->count = (unsigned)count;
    data->noise = !strcmp(wave, "noise");
    for (unsigned i = 0; i < count; i++) {
        lua_rawgeti(state, 2, i + 1);
        luaL_checktype(state, -1, LUA_TTABLE);
        data->notes[i].frequency =
            note_value(state, 1, 0, RUNTIME_SOUND_MAX_FREQUENCY);
        if (data->notes[i].frequency &&
            data->notes[i].frequency < RUNTIME_SOUND_MIN_FREQUENCY)
            return luaL_error(state, "Frequency must be zero or 32..16000 Hz");
        data->notes[i].frames =
            note_value(state, 2, 1, RUNTIME_SOUND_MAX_FRAMES);
        data->notes[i].volume =
            note_value(state, 3, 0, RUNTIME_AUDIO_MAX_VOLUME);
        lua_pop(state, 1);
    }
    return 1;
}

static void stop_voice(lua_State *state, Voice *voice) {
    if (voice->sound)
        luaL_unref(state, LUA_REGISTRYINDEX, voice->reference);
    memset(voice, 0, sizeof(*voice));
    voice->dirty = true;
}

static int play_sound(lua_State *state) {
    Sound *data = luaL_checkudata(state, 1, "lutin.sound");
    lua_Integer number =
        luaL_optinteger(state, 2, data->noise ? RUNTIME_NOISE_VOICE + 1 : 1);
    luaL_argcheck(state, number >= 1 && number <= RUNTIME_AUDIO_VOICES, 2,
                  "Voice must be 1..4");
    luaL_argcheck(state, data->noise == (number == RUNTIME_NOISE_VOICE + 1), 2,
                  "Noise uses voice 4; square uses voices 1..3");
    Voice *voice = &context(state)->voices[number - 1];
    bool loop = lua_toboolean(state, 3);
    lua_pushvalue(state, 1);
    int reference = luaL_ref(state, LUA_REGISTRYINDEX);
    stop_voice(state, voice);
    voice->sound = data;
    voice->reference = reference;
    voice->loop = loop;
    return 0;
}

static int stop_sound(lua_State *state) {
    lua_Integer number = luaL_checkinteger(state, 1);
    luaL_argcheck(state, number >= 1 && number <= RUNTIME_AUDIO_VOICES, 1,
                  "Voice must be 1..4");
    stop_voice(state, &context(state)->voices[number - 1]);
    return 0;
}

static void audio_frame(void) {
    for (unsigned i = 0; i < RUNTIME_AUDIO_VOICES; i++) {
        Voice *voice = &active.voices[i];
        if (voice->remaining) {
            voice->remaining--;
            if (voice->remaining)
                continue;
            voice->note++;
            if (voice->note == voice->sound->count) {
                if (voice->loop)
                    voice->note = 0;
                else
                    stop_voice(active.state, voice);
            }
            voice->dirty = true;
        }
        if (!voice->dirty)
            continue;
        voice->dirty = false;
        unsigned frequency = 0, volume = 0;
        if (voice->sound) {
            Note *note = &voice->sound->notes[voice->note];
            frequency = note->frequency;
            volume = note->volume;
            voice->remaining = note->frames;
        }
        if (audio_output)
            audio_output(i, frequency, volume);
    }
}

static int invoke(lua_State *state) {
    const char *name = lua_touserdata(state, 1);
    bool delta = lua_toboolean(state, 2);
    lua_getglobal(state, name);
    if (lua_isnil(state, -1))
        return 0;
    if (delta)
        lua_pushnumber(state, 1.0 / RUNTIME_FRAMES_PER_SECOND);
    lua_call(state, delta ? 1 : 0, 0);
    return 0;
}

static bool call(Runtime *runtime, const char *name, bool delta) {
    lua_State *state = runtime->state;
    runtime->instructions = 0;
    runtime->pixels = 0;
    runtime->asset_bytes = 0;
    runtime->storage_operations = runtime->storage_bytes = 0;
    lua_pushcfunction(state, invoke);
    lua_pushlightuserdata(state, (void *)name);
    lua_pushboolean(state, delta);
    int status = lua_pcall(state, 2, 0, 0);
    cJSON_Delete(runtime->temporary_json);
    runtime->temporary_json = NULL;
    if (status == LUA_OK)
        return true;
    const char *message =
        lua_type(state, -1) == LUA_TSTRING ? lua_tostring(state, -1) : NULL;
    snprintf(error, sizeof(error), "%s",
             message ? message : "Non-text Lua error");
    log_message(error);
    lua_pop(state, 1);
    return false;
}

static int initialize(lua_State *state) {
#ifdef ARM9
    if (!graphics.font_format && font) {
        static uint16_t atlas[128 * 128];
        for (unsigned glyph = 0; glyph < TEXT_GLYPHS; glyph++)
            for (unsigned y = 0; y < 8; y++)
                for (unsigned x = 0; x < 8; x++)
                    atlas[((glyph / 16) * 8 + y) * 128 + (glyph % 16) * 8 + x] =
                        (font[glyph * 8 + y] & (1 << x)) ? 0xffff : 0;
        graphics.font_format = graphics_texture(state, atlas, 128, 128, true);
    }
#endif
    const luaL_Reg functions[] = {{"clear", clear},
                                  {"rect", rectangle},
                                  {"text", draw_text},
                                  {"line", line},
                                  {"buttons", buttons},
                                  {"touch", touch},
                                  {"load_asset", load_asset},
                                  {"module", module},
                                  {"camera", camera},
                                  {"mesh", mesh_create},
                                  {"draw_mesh", mesh_draw},
                                  {"camera3d", camera3d},
                                  {"measure_text", measure_text},
                                  {"button", button},
                                  {"overlap", overlap},
                                  {"tilemap", tilemap},
                                  {"draw_tilemap", draw_tilemap},
                                  {"animation", animation},
                                  {"draw_animation", draw_animation},
                                  {"save", save_data},
                                  {"load_save", load_save},
                                  {"sprite", sprite},
                                  {"draw_sprite", draw_sprite},
                                  {"sound", sound},
                                  {"play_sound", play_sound},
                                  {"stop_sound", stop_sound},
                                  {NULL, NULL}};
    luaL_newmetatable(state, "lutin.sprite");
    lua_pushliteral(state, "sprite");
    lua_setfield(state, -2, "__metatable");
    lua_pop(state, 1);
    luaL_newmetatable(state, "lutin.tilemap");
    lua_pushliteral(state, "tilemap");
    lua_setfield(state, -2, "__metatable");
    lua_pop(state, 1);
    luaL_newmetatable(state, "lutin.mesh");
    lua_pushliteral(state, "mesh");
    lua_setfield(state, -2, "__metatable");
    lua_pop(state, 1);
    lua_newtable(state);
    lua_setfield(state, LUA_REGISTRYINDEX, "lutin.modules");
    lua_newtable(state);
    lua_setfield(state, LUA_REGISTRYINDEX, "lutin.loading");
    luaL_newmetatable(state, "lutin.sound");
    lua_pushliteral(state, "sound");
    lua_setfield(state, -2, "__metatable");
    lua_pop(state, 1);
    luaL_requiref(state, "_G", luaopen_base, 1);
    lua_pop(state, 1);
    luaL_requiref(state, "math", luaopen_math, 1);
    lua_getfield(state, -1, "randomseed");
    lua_pushinteger(state, (lua_Integer)context(state)->seed);
    lua_call(state, 1, 0);
    lua_pop(state, 1);
    luaL_requiref(state, "string", luaopen_string, 1);
    sandbox_limit_string(state);
    const char *patterns[] = {"find", "match", "gmatch", "gsub"};
    for (unsigned i = 0; i < sizeof(patterns) / sizeof(*patterns); i++) {
        lua_pushnil(state);
        lua_setfield(state, -2, patterns[i]);
    }
    lua_pop(state, 1);
    luaL_requiref(state, "table", luaopen_table, 1);
    lua_pop(state, 1);
    const char *removed[] = {"dofile", "loadfile", "load",  "collectgarbage",
                             "print",  "pcall",    "xpcall"};
    for (unsigned i = 0; i < sizeof(removed) / sizeof(*removed); i++) {
        lua_pushnil(state);
        lua_setglobal(state, removed[i]);
    }
    luaL_newlib(state, functions);
    const struct {
        const char *name;
        unsigned value;
    } buttons[] = {{"A", BUTTON_A},         {"B", BUTTON_B},
                   {"X", BUTTON_X},         {"Y", BUTTON_Y},
                   {"LEFT", BUTTON_LEFT},   {"RIGHT", BUTTON_RIGHT},
                   {"UP", BUTTON_UP},       {"DOWN", BUTTON_DOWN},
                   {"L", BUTTON_L},         {"R", BUTTON_R},
                   {"START", BUTTON_START}, {"SELECT", BUTTON_SELECT},
                   {"TOUCH", BUTTON_TOUCH}};
    for (unsigned i = 0; i < sizeof(buttons) / sizeof(*buttons); i++) {
        lua_pushinteger(state, buttons[i].value);
        lua_setfield(state, -2, buttons[i].name);
    }
    lua_pushinteger(state, RUNTIME_SCREEN_WIDTH);
    lua_setfield(state, -2, "WIDTH");
    lua_pushinteger(state, RUNTIME_SCREEN_HEIGHT);
    lua_setfield(state, -2, "HEIGHT");
    lua_pushinteger(state, RUNTIME_FRAMES_PER_SECOND);
    lua_setfield(state, -2, "FPS");
    lua_pushinteger(state, RUNTIME_NOISE_VOICE + 1);
    lua_setfield(state, -2, "NOISE_VOICE");
    lua_setglobal(state, "ds");
    return 0;
}

static bool start(const char *code, unsigned seed, bool test) {
#ifdef ARM9
    uint16_t previous_clear_color = graphics.clear_color;
    graphics_begin();
#endif
    Runtime candidate = {.seed = seed};
    candidate.state = lua_newstate(allocate, &candidate);
    if (!candidate.state) {
        snprintf(error, sizeof(error), "Lua memory unavailable");
        return false;
    }
    lua_State *state = candidate.state;
    lua_sethook(state, budget, LUA_MASKCOUNT, RUNTIME_HOOK_INTERVAL);
    lua_pushcfunction(state, initialize);
    int status = lua_pcall(state, 0, 0, 0);
    if (status == LUA_OK)
        status = luaL_loadbufferx(state, code, strlen(code), "main.lua", "t");
    if (status == LUA_OK)
        status = lua_pcall(state, 0, 0, 0);
    if (status != LUA_OK) {
        cJSON_Delete(candidate.temporary_json);
        const char *message =
            lua_type(state, -1) == LUA_TSTRING ? lua_tostring(state, -1) : NULL;
        snprintf(error, sizeof(error), "%s",
                 message ? message : "Non-text Lua error");
        log_message(error);
        lua_close(state);
#ifdef ARM9
        graphics_release(&candidate);
        graphics.clear_color = previous_clear_color;
#endif
        return false;
    }
    if (!call(&candidate, "init", false)) {
        lua_close(state);
#ifdef ARM9
        graphics_release(&candidate);
        graphics.clear_color = previous_clear_color;
#endif
        return false;
    }
    runtime_stop();
    active = candidate;
    testing = test;
    test_buttons = 0;
    frame_count = last_frame_us = peak_frame_us = budget_failures = 0;
    peak_memory = active.peak_memory;
    lua_setallocf(state, allocate, &active);
    error[0] = 0;
    log_message("Program started");
#ifdef ARM9
    graphics_finish();
#endif
    return true;
}

bool runtime_start(const char *code) { return start(code, 1, false); }
bool runtime_start_test(const char *code, unsigned seed) {
#ifdef ARM9
    RuntimeInput previous_input = input;
    unsigned previous_frames = frame_count;
    input = (RuntimeInput){0};
    frame_count = 0;
    if (!start(code, seed, true)) {
        input = previous_input;
        frame_count = previous_frames;
        return false;
    }
    return true;
#else
    uint16_t *candidate_pixels =
        calloc(RUNTIME_SCREEN_WIDTH * RUNTIME_SCREEN_HEIGHT, sizeof(uint16_t));
    if (!candidate_pixels) {
        snprintf(error, sizeof(error), "Test framebuffer memory unavailable");
        return false;
    }
    RuntimeInput previous_input = input;
    unsigned previous_frames = frame_count;
    input = (RuntimeInput){.pixels = candidate_pixels};
    frame_count = 0;
    mesh_clear_depth = true;
    if (!start(code, seed, true)) {
        input = previous_input;
        frame_count = previous_frames;
        free(candidate_pixels);
        return false;
    }
    memcpy(test_pixels, candidate_pixels, sizeof(test_pixels));
    free(candidate_pixels);
    input.pixels = test_pixels;
    return true;
#endif
}

void runtime_stop(void) {
#ifdef ARM9
    graphics_release(&active);
#endif
    if (active.peak_memory > peak_memory)
        peak_memory = active.peak_memory;
    cJSON_Delete(active.temporary_json);
    if (active.state) {
        if (audio_output)
            for (unsigned i = 0; i < RUNTIME_AUDIO_VOICES; i++)
                audio_output(i, 0, 0);
        lua_close(active.state);
        log_message("Program stopped");
    }
    memset(&active, 0, sizeof(active));
    testing = false;
}

static void frame(RuntimeInput value) {
    input = value;
    if (!active.state)
        return;
    mesh_clear_depth = true;
#ifdef ARM9
    graphics_begin();
#endif
    uint64_t before = microseconds();
    if (active.state &&
        (!call(&active, "update", true) || !call(&active, "draw", false))) {
        if (strstr(error, "budget") || strstr(error, "memory"))
            budget_failures++;
        runtime_stop();
    }
    if (active.state)
        audio_frame();
#ifdef ARM9
    if (active.state)
        graphics_finish();
#endif
    frame_count++;
    last_frame_us = (unsigned)(microseconds() - before);
    if (last_frame_us > peak_frame_us)
        peak_frame_us = last_frame_us;
    if (active.peak_memory > peak_memory)
        peak_memory = active.peak_memory;
}

void runtime_frame(RuntimeInput value) {
    if (testing) {
#ifndef ARM9
        if (value.pixels)
            memcpy(value.pixels, test_pixels, sizeof(test_pixels));
#endif
        return;
    }
    frame(value);
}

bool runtime_step(unsigned frames, unsigned buttons, int x, int y) {
    if (!testing || !active.state || frames < 1 ||
        frames > RUNTIME_TEST_MAX_FRAMES || (buttons & ~BUTTON_ALL) || x < 0 ||
        x >= RUNTIME_SCREEN_WIDTH || y < 0 || y >= RUNTIME_SCREEN_HEIGHT)
        return false;
    for (unsigned i = 0; i < frames; i++) {
        frame((RuntimeInput){test_pixels, buttons, buttons & ~test_buttons, x,
                             y, (buttons & BUTTON_TOUCH) != 0});
        test_buttons = buttons;
        if (!active.state)
            return false;
#ifdef ARM9
        cothread_yield_irq(IRQ_VBLANK);
#endif
    }
    return true;
}

void runtime_finish_test(void) {
    testing = false;
    test_buttons = 0;
}

static int inspect_value(lua_State *state) {
    lua_getglobal(state, "inspect");
    if (lua_isnil(state, -1))
        return 1;
    lua_call(state, 0, 1);
    return 1;
}

cJSON *runtime_inspect(void) {
    if (active.peak_memory > peak_memory)
        peak_memory = active.peak_memory;
    cJSON *result = cJSON_CreateObject();
#ifdef ARM9
    cJSON_AddStringToObject(result, "renderer", "nds-gpu");
    cJSON_AddNumberToObject(result, "gpu_polygons", graphics.polygons);
    cJSON_AddNumberToObject(result, "gpu_vertices", graphics.vertices);
    cJSON_AddNumberToObject(result, "texture_bytes", active.texture_bytes);
    cJSON *platform = cJSON_AddObjectToObject(result, "platform");
    cJSON_AddNumberToObject(platform, "loop_us", platform_metrics.loop_us);
    cJSON_AddNumberToObject(platform, "work_us", platform_metrics.work_us);
    cJSON_AddNumberToObject(platform, "display_us",
                            platform_metrics.display_us);
    cJSON_AddNumberToObject(platform, "ui_us", platform_metrics.ui_us);
    cJSON_AddNumberToObject(platform, "agent_us", platform_metrics.agent_us);
    cJSON_AddNumberToObject(platform, "display_bytes",
                            platform_metrics.display_bytes);
    cJSON_AddNumberToObject(platform, "missed_vblanks",
                            platform_metrics.missed_vblanks);
#else
    cJSON_AddStringToObject(result, "renderer", "host-reference");
#endif
    cJSON_AddBoolToObject(result, "running", runtime_running());
    cJSON_AddBoolToObject(result, "testing", testing);
    cJSON_AddNumberToObject(result, "frames", frame_count);
    cJSON_AddNumberToObject(result, "memory", active.memory);
    cJSON_AddNumberToObject(result, "peak_memory", peak_memory);
    cJSON_AddNumberToObject(result, "last_frame_us", last_frame_us);
    cJSON_AddNumberToObject(result, "peak_frame_us", peak_frame_us);
    cJSON_AddNumberToObject(result, "budget_failures", budget_failures);
    cJSON_AddStringToObject(result, "error", error);
    if (active.state) {
        lua_State *state = active.state;
        active.instructions = active.pixels = 0;
        active.storage_operations = active.storage_bytes = 0;
        lua_pushcfunction(state, inspect_value);
        if (lua_pcall(state, 0, 1, 0) == LUA_OK) {
            ValueBudget value_budget = {0};
            cJSON *value = lua_value_json(state, -1, 0, &value_budget);
            if (value)
                cJSON_AddItemToObject(result, "state", value);
            else
                cJSON_AddStringToObject(
                    result, "inspection_error",
                    "State must be bounded JSON-compatible data");
        } else {
            cJSON_AddStringToObject(result, "inspection_error",
                                    lua_type(state, -1) == LUA_TSTRING
                                        ? lua_tostring(state, -1)
                                        : "Inspection failed");
        }
        lua_pop(state, 1);
        cJSON_Delete(active.temporary_json);
        active.temporary_json = NULL;
    }
    return result;
}

bool runtime_running(void) { return active.state != NULL; }
bool runtime_testing(void) { return testing; }
const char *runtime_error(void) { return error; }
size_t runtime_memory(void) { return active.memory; }
