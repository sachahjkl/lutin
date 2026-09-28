#include "runtime.h"
#include "sandbox.h"
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    uint16_t pixels[];
} Sprite;

typedef struct {
    Sound *sound;
    int reference;
    unsigned note, remaining;
    bool loop, dirty;
} Voice;

typedef struct {
    lua_State *state;
    size_t memory;
    unsigned instructions;
    unsigned pixels;
    unsigned asset_bytes;
    Voice voices[RUNTIME_AUDIO_VOICES];
} Runtime;

static Runtime active;
static RuntimeInput input;
static char error[256];
static const unsigned char *font;
static char logs[16][256];
static unsigned log_count;
static RuntimeAssetReader asset_reader;
static RuntimeAudio audio_output;

void runtime_set_asset_reader(RuntimeAssetReader reader) {
    asset_reader = reader;
}
void runtime_set_audio(RuntimeAudio output) { audio_output = output; }

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

static void pixel(int x, int y, uint16_t value) {
    if (input.pixels && x >= 0 && y >= 0 && x < RUNTIME_SCREEN_WIDTH &&
        y < RUNTIME_SCREEN_HEIGHT)
        input.pixels[y * RUNTIME_SCREEN_WIDTH + x] = value;
}

bool runtime_capture(const char *path) {
    if (!input.pixels)
        return false;
    FILE *file = fopen(path, "wb");
    if (!file)
        return false;
    bool ok = fprintf(file, "P6\n%d %d\n255\n", RUNTIME_SCREEN_WIDTH,
                      RUNTIME_SCREEN_HEIGHT) > 0;
    for (int y = 0; y < RUNTIME_SCREEN_HEIGHT && ok; y++) {
        unsigned char row[RUNTIME_SCREEN_WIDTH * 3];
        for (int x = 0; x < RUNTIME_SCREEN_WIDTH; x++) {
            unsigned value = input.pixels[y * RUNTIME_SCREEN_WIDTH + x];
            for (int c = 0; c < 3; c++)
                row[x * 3 + c] = ((value >> (c * 5)) & 31) * 255 / 31;
        }
        ok = fwrite(row, 1, sizeof(row), file) == sizeof(row);
    }
    return fclose(file) == 0 && ok;
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
    if (size > 2 * 1024 * 1024 - (runtime->memory - old))
        return NULL;
    void *result = realloc(pointer, size);
    if (result)
        runtime->memory = runtime->memory - old + size;
    return result;
}

static void budget(lua_State *state, lua_Debug *debug) {
    (void)debug;
    Runtime *runtime;
    lua_getallocf(state, (void **)&runtime);
    if (++runtime->instructions >= 100)
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
    if (count > 262144 - runtime->pixels)
        luaL_error(state, "Drawing budget exceeded");
    runtime->pixels += count;
}

static int clear(lua_State *state) {
    uint16_t value = color(state, 1);
    charge_pixels(state, RUNTIME_SCREEN_WIDTH * RUNTIME_SCREEN_HEIGHT);
    if (input.pixels)
        for (int i = 0; i < RUNTIME_SCREEN_WIDTH * RUNTIME_SCREEN_HEIGHT; i++)
            input.pixels[i] = value;
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
    if (input.pixels)
        for (int row = y; row < bottom; row++)
            for (int column = x; column < right; column++)
                input.pixels[row * RUNTIME_SCREEN_WIDTH + column] = value;
    return 0;
}

static int buttons(lua_State *state) {
    lua_pushinteger(state, input.held);
    lua_pushinteger(state, input.pressed);
    return 2;
}

static int draw_text(lua_State *state) {
    int x = coordinate(state, 1), y = coordinate(state, 2);
    size_t length;
    const unsigned char *text =
        (const unsigned char *)luaL_checklstring(state, 3, &length);
    uint16_t value = color(state, 4);
    if (length > 1024)
        return luaL_error(state, "Text exceeds 1024 bytes");
    charge_pixels(state, (unsigned)length * 64);
    if (font)
        for (size_t i = 0; i < length; i++) {
            if (text[i] < 32 || text[i] > 127)
                continue;
            for (int row = 0; row < 8; row++)
                for (int column = 0; column < 8; column++)
                    if (font[(text[i] - 32) * 8 + row] & (1 << column))
                        pixel(x + (int)i * 8 + column, y + row, value);
        }
    return 0;
}

static int line(lua_State *state) {
    int x = coordinate(state, 1), y = coordinate(state, 2);
    int end_x = coordinate(state, 3), end_y = coordinate(state, 4);
    uint16_t value = color(state, 5);
    int dx = abs(end_x - x), dy = -abs(end_y - y);
    charge_pixels(state, (unsigned)(dx > -dy ? dx : -dy) + 1);
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
    return 1;
}

static int draw_sprite(lua_State *state) {
    Sprite *image = luaL_checkudata(state, 1, "lutin.sprite");
    int x = coordinate(state, 2), y = coordinate(state, 3);
    lua_Integer scale = luaL_optinteger(state, 4, 1);
    luaL_argcheck(state, scale >= 1 && scale <= RUNTIME_SPRITE_MAX_SCALE, 4,
                  "Scale must be 1..8");
    bool flip = lua_toboolean(state, 5);
    charge_pixels(state,
                  image->width * image->height * (unsigned)(scale * scale));
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
    return 0;
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
    lua_pushcfunction(state, invoke);
    lua_pushlightuserdata(state, (void *)name);
    lua_pushboolean(state, delta);
    if (lua_pcall(state, 2, 0, 0) == LUA_OK)
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
    const luaL_Reg functions[] = {{"clear", clear},
                                  {"rect", rectangle},
                                  {"text", draw_text},
                                  {"line", line},
                                  {"buttons", buttons},
                                  {"touch", touch},
                                  {"load_asset", load_asset},
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
    luaL_newmetatable(state, "lutin.sound");
    lua_pushliteral(state, "sound");
    lua_setfield(state, -2, "__metatable");
    lua_pop(state, 1);
    luaL_requiref(state, "_G", luaopen_base, 1);
    lua_pop(state, 1);
    luaL_requiref(state, "math", luaopen_math, 1);
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

bool runtime_start(const char *code) {
    Runtime candidate = {0};
    candidate.state = lua_newstate(allocate, &candidate);
    if (!candidate.state) {
        snprintf(error, sizeof(error), "Lua memory unavailable");
        return false;
    }
    lua_State *state = candidate.state;
    lua_sethook(state, budget, LUA_MASKCOUNT, 1000);
    lua_pushcfunction(state, initialize);
    int status = lua_pcall(state, 0, 0, 0);
    if (status == LUA_OK)
        status = luaL_loadbufferx(state, code, strlen(code), "main.lua", "t");
    if (status == LUA_OK)
        status = lua_pcall(state, 0, 0, 0);
    if (status != LUA_OK) {
        const char *message =
            lua_type(state, -1) == LUA_TSTRING ? lua_tostring(state, -1) : NULL;
        snprintf(error, sizeof(error), "%s",
                 message ? message : "Non-text Lua error");
        log_message(error);
        lua_close(state);
        return false;
    }
    if (!call(&candidate, "init", false)) {
        lua_close(state);
        return false;
    }
    runtime_stop();
    active = candidate;
    lua_setallocf(state, allocate, &active);
    error[0] = 0;
    log_message("Program started");
    return true;
}

void runtime_stop(void) {
    if (active.state) {
        if (audio_output)
            for (unsigned i = 0; i < RUNTIME_AUDIO_VOICES; i++)
                audio_output(i, 0, 0);
        lua_close(active.state);
        log_message("Program stopped");
    }
    memset(&active, 0, sizeof(active));
}

void runtime_frame(RuntimeInput value) {
    input = value;
    if (active.state &&
        (!call(&active, "update", true) || !call(&active, "draw", false)))
        runtime_stop();
    if (active.state)
        audio_frame();
}

bool runtime_running(void) { return active.state != NULL; }
const char *runtime_error(void) { return error; }
size_t runtime_memory(void) { return active.memory; }
