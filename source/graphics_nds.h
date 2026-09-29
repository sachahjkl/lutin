#ifndef LUTIN_GRAPHICS_NDS_H
#define LUTIN_GRAPHICS_NDS_H

enum {
    GRAPHICS_WORDS = 32768,
    GRAPHICS_TEXTURES = 128,
    GRAPHICS_TEXTURE_BYTES = 96 * 1024,
    GRAPHICS_PHASE_PRIMITIVES = 512
};
static struct {
    uint32_t words[GRAPHICS_WORDS + 1] __attribute__((aligned(32)));
    unsigned used, polygons, vertices, reserved_vertices, background, overlay;
    unsigned mode;
    uint32_t bound_texture, bound_color;
    int32_t model[16];
    bool drawing, mesh, initialized, presented;
    uint16_t clear_color;
    uint32_t font_format;
} graphics;

static void graphics_command(lua_State *state, unsigned command, unsigned count,
                             const uint32_t *values) {
    if (count + 1 > GRAPHICS_WORDS - graphics.used)
        luaL_error(state, "GPU command budget exceeded");
    graphics.words[++graphics.used] = command;
    for (unsigned i = 0; i < count; i++)
        graphics.words[++graphics.used] = values[i];
}

static void graphics_word(lua_State *state, unsigned command, uint32_t value) {
    graphics_command(state, command, 1, &value);
}

static void graphics_budget(lua_State *state, unsigned polygons,
                            unsigned vertices) {
    /* Six clipping planes can add six vertices to each convex polygon. */
    unsigned reserved = vertices + 6 * polygons;
    if (polygons > 2048 - graphics.polygons ||
        reserved > 6144 - graphics.reserved_vertices)
        luaL_error(state, "GPU geometry budget exceeded");
    graphics.polygons += polygons;
    graphics.vertices += vertices;
    graphics.reserved_vertices += reserved;
    graphics.drawing = true;
}

static void graphics_begin(void) {
    graphics.used = graphics.polygons = graphics.vertices = 0;
    graphics.reserved_vertices = 0;
    graphics.background = graphics.overlay = 0;
    graphics.drawing = graphics.mesh = false;
    graphics.mode = 0;
    graphics.bound_texture = graphics.bound_color = UINT32_MAX;
}

static uint32_t graphics_texture(lua_State *state, const uint16_t *pixels,
                                 unsigned width, unsigned height, bool shared) {
    Runtime *runtime = context(state);
    unsigned w = 8, h = 8;
    while (w < width)
        w *= 2;
    while (h < height)
        h *= 2;
    size_t bytes = w * h * sizeof(uint16_t);
    if (!shared && (runtime->texture_count == GRAPHICS_TEXTURES ||
                    bytes > GRAPHICS_TEXTURE_BYTES - runtime->texture_bytes))
        luaL_error(state, "GPU texture budget exceeded");
    uint16_t *padded = calloc(w * h, sizeof(uint16_t));
    if (!padded)
        luaL_error(state, "Texture staging memory unavailable");
    for (unsigned y = 0; y < height; y++)
        memcpy(padded + y * w, pixels + y * width, width * sizeof(uint16_t));
    int texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(0, texture);
    cothread_yield_irq(IRQ_VBLANK);
    int ok = glTexImageNtr2D(GL_RGBA, w, h, TEXGEN_TEXCOORD, padded, NULL);
    free(padded);
    if (!ok) {
        if (texture)
            glDeleteTextures(1, &texture);
        luaL_error(state, "VRAM texture allocation failed");
    }
    unsigned sw = 0, sh = 0;
    for (unsigned size = 8; size < w; size *= 2)
        sw++;
    for (unsigned size = 8; size < h; size *= 2)
        sh++;
    uint32_t format =
        (((uintptr_t)glGetTexturePointer(texture) >> 3) & 0xffff) | (sw << 20) |
        (sh << 23) | (GL_RGBA << 26);
    if (!shared) {
        runtime->textures[runtime->texture_count++] = texture;
        runtime->texture_bytes += bytes;
    }
    return format;
}

static void graphics_release(Runtime *runtime) {
    for (unsigned i = 0; i < runtime->texture_count; i++)
        glDeleteTextures(1, &runtime->textures[i]);
    runtime->texture_count = runtime->texture_bytes = 0;
}

static void graphics_matrix(lua_State *state, unsigned mode,
                            const int32_t matrix[16]) {
    graphics_word(state, REG2ID(MATRIX_CONTROL), mode);
    graphics_command(state, REG2ID(MATRIX_LOAD4x4), 16,
                     (const uint32_t *)matrix);
}

static void graphics_material(lua_State *state, uint32_t texture,
                              uint16_t color) {
    if (graphics.bound_texture == UINT32_MAX)
        graphics_word(state, FIFO_POLY_FORMAT, POLY_ALPHA(31) | POLY_CULL_NONE);
    if (graphics.bound_texture != texture) {
        graphics_word(state, FIFO_TEX_FORMAT, texture);
        graphics.bound_texture = texture;
    }
    if (graphics.bound_color != (color & 0x7fff)) {
        graphics_word(state, FIFO_COLOR, color & 0x7fff);
        graphics.bound_color = color & 0x7fff;
    }
}

static void graphics_mesh_matrix(lua_State *state, const int32_t model[16]) {
    if (graphics.mode != 2) {
        const int32_t projection[16] = {5120, 0, 0,    0,    0, 6827, 0,     0,
                                        0,    0, 3289, 4096, 0, 0,    -1642, 0};
        graphics_matrix(state, GL_PROJECTION, projection);
    }
    if (graphics.mode != 2 ||
        memcmp(graphics.model, model, sizeof(graphics.model))) {
        graphics_matrix(state, GL_MODELVIEW, model);
        memcpy(graphics.model, model, sizeof(graphics.model));
    }
    graphics.mode = 2;
    graphics.mesh = true;
}

static void graphics_2d(lua_State *state, uint32_t texture, uint16_t color) {
    unsigned *phase = graphics.mesh ? &graphics.overlay : &graphics.background;
    if (++*phase > GRAPHICS_PHASE_PRIMITIVES)
        luaL_error(state, "GPU 2D phase budget exceeded");
    const int32_t projection[16] = {
        131072, 0, 0, 0, 0, -174763, 0, 0, 0, 0, 4096, 0, -4096, 4096, 0, 4096};
    const int32_t identity[16] = {4096, 0, 0,    0, 0, 4096, 0, 0,
                                  0,    0, 4096, 0, 0, 0,    0, 4096};
    unsigned mode = graphics.mesh ? 3 : 1;
    if (graphics.mode != mode) {
        graphics_matrix(state, GL_PROJECTION, projection);
        graphics_matrix(state, GL_MODELVIEW, identity);
        graphics.mode = mode;
    }
    graphics_material(state, texture, color);
}

static void graphics_vertex(lua_State *state, int x, int y, int z) {
    uint32_t values[2] = {(uint16_t)x | ((uint32_t)(uint16_t)y << 16),
                          (uint16_t)z};
    graphics_command(state, FIFO_VERTEX16, 2, values);
}

static void graphics_quad(lua_State *state, int x, int y, int w, int h,
                          uint16_t color, uint32_t texture, int u, int v,
                          int tw, int th, bool flip) {
    if (w <= 0 || h <= 0 || x >= 256 || y >= 192 || x + w <= 0 || y + h <= 0)
        return;
    graphics_budget(state, 1, 4);
    graphics_2d(state, texture, color);
    int z = graphics.mesh ? -3500 - (int)graphics.overlay
                          : 4090 - (int)graphics.background;
    int xs[4] = {x, x, x + w, x + w}, ys[4] = {y, y + h, y + h, y};
    int us[4] = {u, u, u + tw, u + tw}, vs[4] = {v, v + th, v + th, v};
    if (flip)
        for (unsigned i = 0; i < 4; i++)
            us[i] = u + tw - (us[i] - u);
    graphics_word(state, FIFO_BEGIN, GL_QUADS);
    for (unsigned i = 0; i < 4; i++) {
        if (texture)
            graphics_word(state, FIFO_TEX_COORD,
                          (uint16_t)(us[i] * 16) |
                              ((uint32_t)(uint16_t)(vs[i] * 16) << 16));
        graphics_vertex(state, xs[i], ys[i], z);
    }
    graphics_command(state, FIFO_END, 0, NULL);
}

static void graphics_line(lua_State *state, int x, int y, int end_x, int end_y,
                          uint16_t color) {
    graphics_budget(state, 1, 3);
    graphics_2d(state, 0, color);
    int z = graphics.mesh ? -3500 - (int)graphics.overlay
                          : 4090 - (int)graphics.background;
    graphics_word(state, FIFO_BEGIN, GL_TRIANGLES);
    graphics_vertex(state, x, y, z);
    graphics_vertex(state, end_x + 1, end_y + 1, z);
    graphics_vertex(state, end_x + 1, end_y + 1, z);
    graphics_command(state, FIFO_END, 0, NULL);
}

static void graphics_finish(void) {
    if (!graphics.drawing)
        return;
    glClearColor(graphics.clear_color & 31, (graphics.clear_color >> 5) & 31,
                 (graphics.clear_color >> 10) & 31, 31);
    graphics.words[0] = graphics.used;
    if (graphics.used)
        glCallList(graphics.words);
    glFlush(GL_TRANS_MANUALSORT);
    graphics.presented = true;
}

static bool graphics_capture(uint16_t *pixels) {
    if (!graphics.presented)
        return false;
    cothread_yield_irq(IRQ_VBLANK);
    REG_DISPCAPCNT = DCAP_ENABLE | DCAP_BANK(3) | DCAP_SIZE(DCAP_SIZE_256x192) |
                     DCAP_SRC_A(DCAP_SRC_A_3DONLY);
    cothread_yield_irq(IRQ_VBLANK);
    DC_InvalidateRange(pixels, 256 * 192 * 2);
    dmaCopy(VRAM_D, pixels, 256 * 192 * 2);
    DC_InvalidateRange(pixels, 256 * 192 * 2);
    return true;
}
#endif
