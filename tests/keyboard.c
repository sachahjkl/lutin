/* Compare the actual keyboard tile allocation before and during key presses. */
#include <nds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Keyboard *keyboard;
static unsigned char *expected;
static size_t size;

static void verify(const char *stage) {
    const unsigned char *actual =
        (const unsigned char *)bgGetGfxPtr(keyboard->background);
    if (memcmp(expected, actual, size)) {
        fprintf(stderr, "KEYBOARD TILES FAIL %s\n", stage);
        return;
    }
    fprintf(stderr, "KEYBOARD TILES PASS %s\n", stage);
}

Keyboard *__real_keyboardInit_call(const Keyboard *, int, BgType, BgSize, int,
                                   int, bool, bool);
Keyboard *__wrap_keyboardInit_call(const Keyboard *definition, int layer,
                                   BgType type, BgSize dimensions, int map,
                                   int tiles, bool main, bool load) {
    keyboard = __real_keyboardInit_call(definition, layer, type, dimensions,
                                        map, tiles, main, load);
    consoleDebugInit(DebugDevice_NOCASH);
    const unsigned char *compressed = (const unsigned char *)definition->tiles;
    size = (unsigned)compressed[1] | ((unsigned)compressed[2] << 8) |
           ((unsigned)compressed[3] << 16);
    expected = malloc(size);
    if (!expected)
        abort();
    decompress(compressed, expected, LZ77);
    verify("initialization");
    return keyboard;
}

s16 __real_keyboardUpdate(void);
s16 __wrap_keyboardUpdate(void) {
    s16 key = __real_keyboardUpdate();
    if (key >= 0)
        verify("pressed");
    return key;
}
