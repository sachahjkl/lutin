#ifndef LUTIN_RUNTIME_MESH_H
#define LUTIN_RUNTIME_MESH_H

enum { MESH_MAX_VERTICES = 256, MESH_MAX_FACES = 256 };
typedef struct {
    float x, y, z;
} Vertex;
typedef struct {
    unsigned short indices[3], color;
} Face;
typedef struct {
    unsigned vertices, faces;
    Vertex positions[MESH_MAX_VERTICES];
    Face triangles[MESH_MAX_FACES];
} Mesh;

/* Fixed rendering scratch: no per-frame allocations or pointers into Lua. */
static struct {
    Vertex vertices[MESH_MAX_VERTICES];
    uint32_t depth[RUNTIME_SCREEN_WIDTH * RUNTIME_SCREEN_HEIGHT];
} mesh_scratch;
static bool mesh_clear_depth = true;

static float mesh_number(lua_State *state, int index) {
    float value = luaL_checknumber(state, index);
    luaL_argcheck(state, isfinite(value) && fabsf(value) <= 1024, index,
                  "3D values must be finite and within -1024..1024");
    return value;
}

static int mesh_create(lua_State *state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    luaL_checktype(state, 2, LUA_TTABLE);
    size_t vertices = lua_rawlen(state, 1), faces = lua_rawlen(state, 2);
    luaL_argcheck(state, vertices >= 3 && vertices <= MESH_MAX_VERTICES, 1,
                  "Mesh requires 3..256 vertices");
    luaL_argcheck(state, faces >= 1 && faces <= MESH_MAX_FACES, 2,
                  "Mesh requires 1..256 triangles");
    Mesh *mesh = lua_newuserdatauv(state, sizeof(Mesh), 0);
    luaL_setmetatable(state, "lutin.mesh");
    mesh->vertices = vertices;
    mesh->faces = faces;
    for (unsigned i = 0; i < vertices; i++) {
        lua_rawgeti(state, 1, i + 1);
        luaL_checktype(state, -1, LUA_TTABLE);
        float values[3];
        for (unsigned j = 0; j < 3; j++) {
            lua_rawgeti(state, -1, j + 1);
            values[j] = mesh_number(state, -1);
            lua_pop(state, 1);
        }
        mesh->positions[i] = (Vertex){values[0], values[1], values[2]};
        lua_pop(state, 1);
    }
    for (unsigned i = 0; i < faces; i++) {
        lua_rawgeti(state, 2, i + 1);
        luaL_checktype(state, -1, LUA_TTABLE);
        for (unsigned j = 0; j < 3; j++) {
            lua_rawgeti(state, -1, j + 1);
            lua_Integer index = luaL_checkinteger(state, -1);
            if (index < 1 || (lua_Unsigned)index > vertices)
                return luaL_error(state, "Invalid mesh vertex index");
            mesh->triangles[i].indices[j] = (unsigned)index - 1;
            lua_pop(state, 1);
        }
        lua_rawgeti(state, -1, 4);
        mesh->triangles[i].color = color(state, -1);
        lua_pop(state, 2);
    }
    return 1;
}

static int camera3d(lua_State *state) {
    float values[5];
    for (unsigned i = 0; i < 5; i++)
        values[i] = mesh_number(state, i + 1);
    Runtime *runtime = context(state);
    runtime->eye_x = values[0];
    runtime->eye_y = values[1];
    runtime->eye_z = values[2];
    runtime->eye_yaw = values[3];
    runtime->eye_pitch = values[4];
    return 0;
}

static int64_t mesh_edge(int64_t ax, int64_t ay, int64_t bx, int64_t by,
                         int64_t x, int64_t y) {
    return (x - ax) * (by - ay) - (y - ay) * (bx - ax);
}

static void mesh_triangle(lua_State *state, Vertex a, Vertex b, Vertex c,
                          uint16_t value) {
    const float focal = 160.0f;
    Vertex projected[3] = {a, b, c};
    for (unsigned i = 0; i < 3; i++) {
        projected[i].x =
            RUNTIME_SCREEN_WIDTH / 2 + focal * projected[i].x / projected[i].z;
        projected[i].y =
            RUNTIME_SCREEN_HEIGHT / 2 - focal * projected[i].y / projected[i].z;
        projected[i].z = 1.0f / projected[i].z;
    }
    a = projected[0];
    b = projected[1];
    c = projected[2];
    int64_t px[3], py[3];
    for (unsigned i = 0; i < 3; i++) {
        px[i] = (int64_t)llroundf(projected[i].x * 16);
        py[i] = (int64_t)llroundf(projected[i].y * 16);
    }
    int64_t area = mesh_edge(px[0], py[0], px[1], py[1], px[2], py[2]);
    if (!area)
        return;
    int left = (int)fmaxf(
        0, fminf(RUNTIME_SCREEN_WIDTH, floorf(fminf(a.x, fminf(b.x, c.x)))));
    int right = (int)fmaxf(
        0, fminf(RUNTIME_SCREEN_WIDTH, ceilf(fmaxf(a.x, fmaxf(b.x, c.x)))));
    int top = (int)fmaxf(
        0, fminf(RUNTIME_SCREEN_HEIGHT, floorf(fminf(a.y, fminf(b.y, c.y)))));
    int bottom = (int)fmaxf(
        0, fminf(RUNTIME_SCREEN_HEIGHT, ceilf(fmaxf(a.y, fmaxf(b.y, c.y)))));
    charge_pixels(state, (unsigned)((right - left) * (bottom - top)));
    int64_t edge[3], step_x[3], step_y[3];
    int sign = area < 0 ? -1 : 1;
    for (unsigned i = 0; i < 3; i++) {
        unsigned j = (i + 1) % 3, k = (i + 2) % 3;
        edge[i] = sign * mesh_edge(px[j], py[j], px[k], py[k], left * 16 + 8,
                                   top * 16 + 8);
        step_x[i] = sign * (py[k] - py[j]) * 16;
        step_y[i] = sign * (px[j] - px[k]) * 16;
    }
    float inverse_area = 1.0f / (float)(sign * area);
    float row_depth = 0, depth_x = 0, depth_y = 0;
    for (unsigned i = 0; i < 3; i++) {
        row_depth += (float)edge[i] * projected[i].z * inverse_area;
        depth_x += (float)step_x[i] * projected[i].z * inverse_area;
        depth_y += (float)step_y[i] * projected[i].z * inverse_area;
    }
    /* Bound conversion for subpixel-degenerate triangles outside the viewport.
     */
    float depth_bound = fabsf(row_depth) + fabsf(depth_x) * (right - left) +
                        fabsf(depth_y) * (bottom - top);
    if (!isfinite(depth_bound) || depth_bound > 1.0e9f)
        return;
    const float depth_scale = 268435456.0f;
    int64_t fixed_row = llroundf(row_depth * depth_scale);
    int64_t fixed_x = llroundf(depth_x * depth_scale);
    int64_t fixed_y = llroundf(depth_y * depth_scale);
    /* The inner loop uses integer additions, comparisons, and stores only. */
    for (int y = top; y < bottom; y++) {
        int64_t u = edge[0], v = edge[1], w = edge[2];
        int64_t depth = fixed_row;
        for (int x = left; x < right; x++) {
            unsigned offset = y * RUNTIME_SCREEN_WIDTH + x;
            if (u >= 0 && v >= 0 && w >= 0 && depth > 0 &&
                depth <= UINT32_MAX &&
                (uint32_t)depth > mesh_scratch.depth[offset]) {
                mesh_scratch.depth[offset] = (uint32_t)depth;
                pixel(x, y, value);
            }
            u += step_x[0];
            v += step_x[1];
            w += step_x[2];
            depth += fixed_x;
        }
        for (unsigned i = 0; i < 3; i++)
            edge[i] += step_y[i];
        fixed_row += fixed_y;
    }
}

static int mesh_draw(lua_State *state) {
    Mesh *mesh = luaL_checkudata(state, 1, "lutin.mesh");
    charge_pixels(state, mesh->vertices + mesh->faces * 3);
    float x = mesh_number(state, 2), y = mesh_number(state, 3),
          z = mesh_number(state, 4);
    float yaw = mesh_number(state, 5);
    Runtime *runtime = context(state);
    float sine = sinf(yaw), cosine = cosf(yaw);
    float view_sine = sinf(-runtime->eye_yaw),
          view_cosine = cosf(-runtime->eye_yaw);
    float pitch_sine = sinf(-runtime->eye_pitch),
          pitch_cosine = cosf(-runtime->eye_pitch);
    if (mesh_clear_depth) {
        memset(mesh_scratch.depth, 0, sizeof(mesh_scratch.depth));
        mesh_clear_depth = false;
    }
    for (unsigned i = 0; i < mesh->vertices; i++) {
        Vertex p = mesh->positions[i];
        float px = p.x * cosine + p.z * sine + x - runtime->eye_x;
        float py = p.y + y - runtime->eye_y;
        float pz = -p.x * sine + p.z * cosine + z - runtime->eye_z;
        float vx = px * view_cosine + pz * view_sine;
        float vz = -px * view_sine + pz * view_cosine;
        mesh_scratch.vertices[i] =
            (Vertex){vx, py * pitch_cosine - vz * pitch_sine,
                     py * pitch_sine + vz * pitch_cosine};
    }
    const float near_plane = 0.25f;
    for (unsigned i = 0; i < mesh->faces; i++) {
        Face *face = &mesh->triangles[i];
        Vertex clipped[4];
        unsigned count = 0;
        for (unsigned j = 0; j < 3; j++) {
            Vertex a = mesh_scratch.vertices[face->indices[j]],
                   b = mesh_scratch.vertices[face->indices[(j + 1) % 3]];
            if (a.z >= near_plane)
                clipped[count++] = a;
            if ((a.z >= near_plane) != (b.z >= near_plane)) {
                float t = (near_plane - a.z) / (b.z - a.z);
                clipped[count++] = (Vertex){a.x + t * (b.x - a.x),
                                            a.y + t * (b.y - a.y), near_plane};
            }
        }
        for (unsigned j = 1; j + 1 < count; j++)
            mesh_triangle(state, clipped[0], clipped[j], clipped[j + 1],
                          face->color);
    }
    return 0;
}
#endif
