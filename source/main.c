#include "agent.h"
#include "backend.h"
#include "chat.h"
#include "config.h"
#include "network.h"
#include "platform.h"
#include "platform_audio.h"
#include "platform_input.h"
#include "runtime.h"
#include "workspace.h"
#include <fat.h>
#include <nds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char demo[] =
    "x,y,vx,vy=120,80,80,60\n"
    "function update(dt)\n"
    " local held=ds.buttons()\n"
    " if held & ds.LEFT ~= 0 then x=x-100*dt end\n"
    " if held & ds.RIGHT ~= 0 then x=x+100*dt end\n"
    " x=x+vx*dt;y=y+vy*dt\n"
    " if x<0 then x=0;vx=math.abs(vx) end\n"
    " if x>244 then x=244;vx=-math.abs(vx) end\n"
    " if y<0 then y=0;vy=math.abs(vy) end\n"
    " if y>180 then y=180;vy=-math.abs(vy) end\n"
    "end\n"
    "function draw()\n"
    " ds.clear(0x102030)\n"
    " ds.rect(math.floor(x),math.floor(y),12,12,0x60d0ff)\n"
    " ds.rect(0,190,256,2,0xffffff)\n"
    "end\n";

typedef enum {
    COMPOSE,
    PREVIEW,
    PLAY,
    QUEUE,
    SOURCE,
    MODELS,
    SESSIONS,
    PROJECTS
} View;
typedef enum {
    MENU_KEYBOARD,
    MENU_PREVIEW,
    MENU_PLAY,
    MENU_QUEUE,
    MENU_SOURCE,
    MENU_RUN,
    MENU_STOP_PROGRAM,
    MENU_STOP_AGENT,
    MENU_RESUME,
    MENU_SESSION,
    MENU_PROJECT,
    MENU_SAVE,
    MENU_MODEL,
    MENU_TOOLS,
    MENU_QUIT,
    MENU_RELOAD_CATALOG,
    MENU_UPDATE_CATALOG
} MenuAction;
static View view;
static bool modal, steer;
static bool tools_expanded;
static unsigned model_index;
enum { MODEL_PAGE_SIZE = 12 };
static bool catalog_updating;
static unsigned session_items[12], session_count, session_index, session_after;
static unsigned project_items[WORKSPACE_PROJECT_PAGE_SIZE], project_count,
    project_index, project_after;
static unsigned menu_index, project = 1, session_number = 1;
static unsigned chat_scroll, source_scroll, queue_index;
static int editing_queue = -1;
static char prompt[1024], code[32769], message[160];
static uint16_t frame_pixels[256 * 192];
static PrintConsole top_console, bottom_console;
static uint16_t top_map[32 * 32], bottom_map[32 * 32];
static int program_background;
static const char *menu_items[] = {"Keyboard",
                                   "Preview",
                                   "Play controls",
                                   "Queue",
                                   "Source",
                                   "Run program",
                                   "Stop program",
                                   "Stop agent",
                                   "Resume queue",
                                   "Sessions",
                                   "Projects",
                                   "Save source",
                                   "Model",
                                   "Tool details",
                                   "Quit",
                                   "Reload model catalog",
                                   "Update model catalog"};
#define MENU_COUNT (sizeof(menu_items) / sizeof(*menu_items))

static bool load(void) {
    if (workspace_load_entry(code, sizeof(code), demo))
        return true;
    snprintf(message, sizeof(message), "%s", workspace_error());
    return false;
}

static void refresh_sessions(void) {
    agent_sessions(session_after, session_items, 12, &session_count);
    session_index = 0;
}

static void session_changed(void) {
    editing_queue = -1;
    chat_scroll = 0;
    message[0] = 0;
    session_after = 0;
    refresh_sessions();
}

static void refresh_projects(void) {
    if (!workspace_projects(project_after, project_items,
                            WORKSPACE_PROJECT_PAGE_SIZE, &project_count))
        snprintf(message, sizeof(message), "%s", workspace_error());
    project_index = 0;
}

static void open_project(unsigned selected) {
    if (agent_busy() || runtime_running()) {
        snprintf(message, sizeof(message), "Stop agent and program first");
        return;
    }
    if (!agent_close())
        return;
    if (!workspace_select(selected)) {
        snprintf(message, sizeof(message), "%s", workspace_error());
        return;
    }
    unsigned numbers[1], count;
    if (!agent_sessions(0, numbers, 1, &count) ||
        !(count ? agent_open(numbers[0]) : agent_create(&numbers[0]))) {
        workspace_select(project);
        snprintf(message, sizeof(message), "Cannot open project session");
        return;
    }
    project = selected;
    session_number = numbers[0];
    session_changed();
    load();
}

static void page_text(const char *text, unsigned rows) {
    unsigned column = 0;
    while (*text && rows) {
        unsigned char value = (unsigned char)*text++;
        if (value == '\n') {
            consolePrintChar('\n');
            column = 0;
            rows--;
        } else {
            consolePrintChar(value >= 32 && value < 127 ? value : ' ');
            if (++column == 32) {
                column = 0;
                rows--;
            }
        }
    }
}

static void layout(void) {
    keyboardHide();
    bgHide(program_background);
    bgShow(bottom_console.bgId);
    if (!modal && (view == PREVIEW || view == PLAY)) {
        bgHide(bottom_console.bgId);
        bgShow(program_background);
    } else if (!modal && view == COMPOSE)
        keyboardShow();
}

static bool action(MenuAction index) {
    if (index <= MENU_SOURCE) {
        view = (View)index;
        if (view == SOURCE)
            load();
        if (view == QUEUE)
            editing_queue = -1;
    } else if (index == MENU_RUN) {
        if (load() && runtime_start(code)) {
            message[0] = 0;
            view = PREVIEW;
        } else if (runtime_error()[0])
            snprintf(message, sizeof(message), "%s", runtime_error());
    } else if (index == MENU_STOP_PROGRAM) {
        runtime_stop();
    } else if (index == MENU_STOP_AGENT) {
        agent_stop();
        snprintf(message, sizeof(message), "Agent stopped; queue paused");
    } else if (index == MENU_RESUME) {
        agent_resume();
    } else if (index == MENU_SESSION) {
        view = SESSIONS;
        session_after = 0;
        refresh_sessions();
    } else if (index == MENU_PROJECT) {
        view = PROJECTS;
        project_after = 0;
        refresh_projects();
    } else if (index == MENU_SAVE) {
        snprintf(message, sizeof(message), "%s",
                 workspace_write("main.lua", code) ? "Saved main.lua"
                                                   : workspace_error());
    } else if (index == MENU_MODEL) {
        view = MODELS;
        model_index = agent_model() < BACKEND_COUNT ? agent_model() : 0;
    } else if (index == MENU_TOOLS) {
        tools_expanded = !tools_expanded;
        chat_scroll = 0;
    } else if (index == MENU_RELOAD_CATALOG || index == MENU_UPDATE_CATALOG) {
        if (agent_busy())
            snprintf(message, sizeof(message),
                     "Stop agent before catalog changes");
        else if (agent_close()) {
            if (index == MENU_RELOAD_CATALOG) {
                if (catalog_load("/lutin")) {
                    config_load("/lutin/config.json");
                    model_index = 0;
                    snprintf(message, sizeof(message), "%s",
                             config_error()[0] ? config_error()
                                               : "Model catalog reloaded");
                } else
                    snprintf(message, sizeof(message), "%s", catalog_error());
            } else {
                catalog_updating = network_update_catalog();
                snprintf(message, sizeof(message), "%s", network_status());
            }
        }
    } else if (index == MENU_QUIT) {
        if (agent_close())
            return false;
        snprintf(message, sizeof(message), "Save failed; quit cancelled");
    }
    modal = false;
    layout();
    return true;
}

static void render_top(bool storage) {
    consoleSelect(&top_console);
    consoleClear();
    consoleSetColor(&top_console, CONSOLE_CYAN);
    printf("Lutin / CHAT  WiFi:%s\n", network_wifi());
    consoleSetColor(&top_console, CONSOLE_LIGHT_GRAY);
    printf("%s SD:%s P%u S%u  %s\n", isDSiMode() ? "DSi" : "DS",
           storage ? "OK" : "--", project, session_number,
           runtime_running() ? "RUN" : "STOP");
    consoleSetColor(&top_console, CONSOLE_YELLOW);
    page_text(agent_status(), 2);
    ChatPage page;
    chat_page(&page, chat_scroll, tools_expanded);
    if (!page.total) {
        consoleSetCursor(&top_console, 0, 4);
        consoleSetColor(&top_console, CONSOLE_GREEN);
        consolePrintString("Ready to build.\n\n");
        consoleSetColor(&top_console, CONSOLE_LIGHT_GRAY);
        consolePrintString(
            "Type a request below.\nENTER sends it to the agent.\n\n");
        consolePrintString(
            "L Queue mode / R Steer mode\nSTART opens the menu.\n");
    } else {
        for (unsigned row = 0; row < CHAT_ROWS; row++) {
            consoleSetCursor(&top_console, 0, (int)row + 4);
            consoleSetColor(&top_console, (ConsoleColor)page.colors[row]);
            /* Do not interpret control codes from model or file content. */
            for (const char *p = page.lines[row]; *p; p++)
                consolePrintChar(*p);
        }
    }
    consoleSetColor(&top_console, CONSOLE_LIGHT_GRAY);
    consoleSetCursor(&top_console, 0, 20);
    consolePrintString("--------------------------------");
    consoleSetCursor(&top_console, 0, 21);
    page_text(runtime_error()[0] ? runtime_error() : message, 1);
    consoleSetCursor(&top_console, 0, 22);
    consoleSetColor(&top_console, CONSOLE_MAGENTA);
    consolePrintString(agent_model() < BACKEND_COUNT
                           ? backends[agent_model()].label
                           : "Select a catalog model");
    consoleSetCursor(&top_console, 0, 23);
    consoleSetColor(&top_console, CONSOLE_CYAN);
    printf("START Menu  Queue:%u %s", agent_queue_size(),
           chat_scroll ? "HISTORY" : "LIVE");
}

static void render_bottom(void) {
    if (!modal && (view == PREVIEW || view == PLAY))
        return;
    consoleSelect(&bottom_console);
    consoleClear();
    consoleSetColor(&bottom_console, CONSOLE_CYAN);
    if (modal) {
        consolePrintString("MENU  /  A Select  B Back\n\n");
        for (unsigned i = 0; i < MENU_COUNT; i++) {
            consoleSetColor(&bottom_console, i == menu_index
                                                 ? CONSOLE_GREEN
                                                 : CONSOLE_LIGHT_GRAY);
            printf("%s %s\n", i == menu_index ? ">" : " ", menu_items[i]);
        }
        consoleSetColor(&bottom_console, CONSOLE_LIGHT_GRAY);
        consolePrintString(
            "\nTouch an item to select.\nDraft is kept when switching.\n");
    } else if (view == COMPOSE) {
        printf("%s | ENTER Send\n", editing_queue >= 0 ? "EDIT QUEUE"
                                    : steer            ? "STEER"
                                                       : "QUEUE");
        consoleSetColor(&bottom_console, CONSOLE_LIGHT_GRAY);
        consolePrintString("L Queue  R Steer  START Menu\n");
        size_t length = strlen(prompt);
        page_text(prompt + (length > 128 ? length - 128 : 0), 4);
    } else if (view == QUEUE) {
        consolePrintString("QUEUE  A Resume  X Edit\n");
        consoleSetColor(&bottom_console, CONSOLE_LIGHT_GRAY);
        consolePrintString(
            "UP/DOWN Select  Y First\nLEFT Delete  START Menu\n\n");
        unsigned count = agent_queue_size();
        if (queue_index >= count)
            queue_index = count ? count - 1 : 0;
        printf("Item %u / %u\n\n", count ? queue_index + 1 : 0, count);
        page_text(count ? agent_queued(queue_index) : "Queue is empty.", 16);
    } else if (view == SESSIONS) {
        consolePrintString("SESSIONS / A Open  X New\n");
        consoleSetColor(&bottom_console, CONSOLE_LIGHT_GRAY);
        consolePrintString("UP/DOWN Select  LEFT/RIGHT Page\nY Reset current  "
                           "B Delete current\n\n");
        for (unsigned i = 0; i < session_count; i++) {
            consoleSetColor(&bottom_console, i == session_index
                                                 ? CONSOLE_GREEN
                                                 : CONSOLE_LIGHT_GRAY);
            printf("%s Session %u%s\n", i == session_index ? ">" : " ",
                   session_items[i],
                   session_items[i] == session_number ? " *" : "");
        }
        if (!session_count)
            consolePrintString("No sessions. X creates one.\n");
        consoleSetColor(&bottom_console, CONSOLE_LIGHT_GRAY);
        consolePrintString(
            "\n* Current session\nReset/delete keep project files.\n");
    } else if (view == PROJECTS) {
        consolePrintString("PROJECTS / A Open  X New\n");
        consoleSetColor(&bottom_console, CONSOLE_LIGHT_GRAY);
        consolePrintString(
            "UP/DOWN Select  RIGHT Next\nLEFT First page  START Menu\n\n");
        for (unsigned i = 0; i < project_count; i++)
            printf("%s Project %u%s\n", i == project_index ? ">" : " ",
                   project_items[i], project_items[i] == project ? " *" : "");
        if (!project_count)
            consolePrintString("No projects on this page.\n");
    } else if (view == MODELS) {
        consolePrintString("MODEL / A Select\n");
        consoleSetColor(&bottom_console, CONSOLE_LIGHT_GRAY);
        consolePrintString("UP/DOWN  START Menu\n\n");
        unsigned first = model_index / MODEL_PAGE_SIZE * MODEL_PAGE_SIZE;
        for (unsigned i = first;
             i < BACKEND_COUNT && i < first + MODEL_PAGE_SIZE; i++) {
            consoleSetColor(&bottom_console, i == model_index
                                                 ? CONSOLE_GREEN
                                                 : CONSOLE_LIGHT_GRAY);
            printf("%s %s\n", i == model_index ? ">" : " ", backends[i].label);
        }
        consoleSetColor(&bottom_console, CONSOLE_LIGHT_GRAY);
        consolePrintString("\nSelection pauses the agent.\nQueue is "
                           "kept.\nKeys: /lutin/keys/<provider>\n");
    } else {
        consolePrintString("main.lua  UP/DOWN Scroll\n");
        consoleSetColor(&bottom_console, CONSOLE_LIGHT_GRAY);
        consolePrintString("START Menu\n\n");
        size_t length = strlen(code);
        if (source_scroll > length)
            source_scroll = (unsigned)length;
        page_text(code + source_scroll, 20);
    }
}

int main(void) {
    defaultExceptionHandler();
    setvbuf(stdout, NULL, _IONBF, 0);
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankB(VRAM_B_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    lcdMainOnBottom();
    program_background = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 4, 0);
    uint16_t *pixels = bgGetGfxPtr(program_background);
    for (unsigned i = 0; i < 256 * 192; i++)
        frame_pixels[i] = RGB15(2, 4, 6) | BIT(15);
    consoleInit(&top_console, 0, BgType_Text4bpp, BgSize_T_256x256, 31, 0,
                false, true);
    runtime_set_font(top_console.font.gfx);
    consoleInit(&bottom_console, 0, BgType_Text4bpp, BgSize_T_256x256, 31, 0,
                true, true);
    Keyboard *keyboard = keyboardInit(NULL, 1, BgType_Text4bpp,
                                      BgSize_T_256x512, 20, 1, true, true);
    keyboard->scrollSpeed = 0;
    uint16_t *top_vram = top_console.fontBgMap;
    uint16_t *bottom_vram = bottom_console.fontBgMap;
    top_console.fontBgMap = top_map;
    bottom_console.fontBgMap = bottom_map;
    bool storage = fatInitDefault();
    platform_input_init();
    workspace_init(storage, "/lutin");
    catalog_load("/lutin");
    runtime_set_asset_reader(workspace_read);
    soundEnable();
    runtime_set_audio(platform_audio);
    config_load("/lutin/config.json");
    tools_expanded = config_get()->tool_details;
    view = config_get()->start_in_sessions ? SESSIONS : COMPOSE;
    refresh_sessions();
    if (session_count) {
        session_number = session_items[0];
        agent_open(session_number);
    } else
        agent_create(&session_number);
    load();
    if (config_error()[0])
        snprintf(message, sizeof(message), "%s", config_error());
    if (catalog_error()[0])
        snprintf(message, sizeof(message), "%s", catalog_error());
    layout();
    unsigned frames = 0;
    bool running = true;
    while (running) {
        platform_wait_frame();
        DC_FlushRange(frame_pixels, sizeof(frame_pixels));
        dmaCopy(frame_pixels, pixels, sizeof(frame_pixels));
        DC_FlushRange(top_map, sizeof(top_map));
        DC_FlushRange(bottom_map, sizeof(bottom_map));
        dmaCopy(top_map, top_vram, sizeof(top_map));
        dmaCopy(bottom_map, bottom_vram, sizeof(bottom_map));
        InputSample sample = platform_input_take();
        unsigned pressed = sample.pressed, held = sample.held;
        touchPosition touch = {.px = sample.touch_x, .py = sample.touch_y};
        bool menu_input = modal || (pressed & KEY_START);
        if (catalog_updating) {
            if (pressed & KEY_B)
                network_stop();
        } else if (pressed & KEY_START) {
            modal = !modal;
            layout();
        } else if (modal) {
            if (pressed & KEY_UP)
                menu_index = (menu_index + MENU_COUNT - 1) % MENU_COUNT;
            if (pressed & KEY_DOWN)
                menu_index = (menu_index + 1) % MENU_COUNT;
            if ((pressed & KEY_TOUCH) && touch.py >= 16 &&
                touch.py < 16 + MENU_COUNT * 8) {
                menu_index = (touch.py - 16) / 8;
                running = action(menu_index);
            } else if (pressed & KEY_A)
                running = action(menu_index);
            else if (pressed & KEY_B) {
                modal = false;
                layout();
            }
        } else if (view != PLAY) {
            if (pressed & KEY_L) {
                steer = false;
                view = COMPOSE;
                layout();
            }
            if (pressed & KEY_R) {
                steer = true;
                view = COMPOSE;
                layout();
            }
            if (view == COMPOSE) {
                if (pressed & KEY_B)
                    editing_queue = -1;
                int key = keyboardUpdate();
                size_t length = strlen(prompt);
                if (key == '\b' && length)
                    prompt[length - 1] = 0;
                else if (key >= 32 && key < 127 &&
                         length + 1 < sizeof(prompt)) {
                    prompt[length] = (char)key;
                    prompt[length + 1] = 0;
                } else if (key == '\n' || key == '\r') {
                    bool ok =
                        editing_queue >= 0
                            ? agent_edit_queued((unsigned)editing_queue, prompt)
                            : agent_submit(prompt, steer);
                    if (ok) {
                        prompt[0] = 0;
                        editing_queue = -1;
                        chat_scroll = 0;
                    }
                }
            }
            if (view == COMPOSE || view == PREVIEW) {
                if ((pressed & KEY_UP) && chat_scroll < 262144)
                    chat_scroll += 8;
                if (pressed & KEY_DOWN)
                    chat_scroll = chat_scroll > 8 ? chat_scroll - 8 : 0;
                if (pressed & KEY_A)
                    chat_scroll = 0;
            } else if (view == QUEUE) {
                if ((pressed & KEY_DOWN) &&
                    queue_index + 1 < agent_queue_size())
                    queue_index++;
                if ((pressed & KEY_UP) && queue_index)
                    queue_index--;
                if (pressed & KEY_A)
                    agent_resume();
                if (pressed & KEY_LEFT)
                    agent_remove_queued(queue_index);
                if (pressed & KEY_Y)
                    agent_prioritize_queued(queue_index);
                if ((pressed & KEY_X) && queue_index < agent_queue_size() &&
                    agent_close()) {
                    editing_queue = (int)queue_index;
                    snprintf(prompt, sizeof(prompt), "%s",
                             agent_queued(queue_index));
                    view = COMPOSE;
                    layout();
                }
            } else if (view == SESSIONS) {
                if ((pressed & KEY_DOWN) && session_index + 1 < session_count)
                    session_index++;
                if ((pressed & KEY_UP) && session_index)
                    session_index--;
                if ((pressed & KEY_RIGHT) && session_count) {
                    session_after = session_items[session_count - 1];
                    refresh_sessions();
                }
                if (pressed & KEY_LEFT) {
                    session_after = 0;
                    refresh_sessions();
                }
                if ((pressed & KEY_A) && session_count &&
                    agent_open(session_items[session_index])) {
                    session_number = session_items[session_index];
                    session_changed();
                }
                if ((pressed & KEY_X) && agent_create(&session_number))
                    session_changed();
                if ((pressed & KEY_Y) && agent_reset())
                    session_changed();
                if ((pressed & KEY_B) && agent_delete()) {
                    session_number = 0;
                    session_changed();
                    if (session_count && agent_open(session_items[0]))
                        session_number = session_items[0];
                }
            } else if (view == PROJECTS) {
                if ((pressed & KEY_DOWN) && project_index + 1 < project_count)
                    project_index++;
                if ((pressed & KEY_UP) && project_index)
                    project_index--;
                if ((pressed & KEY_RIGHT) && project_count) {
                    project_after = project_items[project_count - 1];
                    refresh_projects();
                }
                if (pressed & KEY_LEFT) {
                    project_after = 0;
                    refresh_projects();
                }
                if ((pressed & KEY_A) && project_count)
                    open_project(project_items[project_index]);
                if (pressed & KEY_X) {
                    unsigned created;
                    if (agent_busy() || runtime_running())
                        snprintf(message, sizeof(message),
                                 "Stop agent and program first");
                    else if (workspace_create(&created)) {
                        open_project(created);
                        project_after = created - 1;
                        refresh_projects();
                    } else
                        snprintf(message, sizeof(message), "%s",
                                 workspace_error());
                }
            } else if (view == MODELS && BACKEND_COUNT) {
                if (pressed & KEY_UP)
                    model_index =
                        (model_index + BACKEND_COUNT - 1) % BACKEND_COUNT;
                if (pressed & KEY_DOWN)
                    model_index = (model_index + 1) % BACKEND_COUNT;
                if ((pressed & KEY_TOUCH) && touch.py >= 24 &&
                    touch.py < 24 + MODEL_PAGE_SIZE * 8) {
                    unsigned selected =
                        model_index / MODEL_PAGE_SIZE * MODEL_PAGE_SIZE +
                        (touch.py - 24) / 8;
                    if (selected < BACKEND_COUNT) {
                        model_index = selected;
                        pressed |= KEY_A;
                    }
                }
                if ((pressed & KEY_A) && agent_select_model(model_index)) {
                    view = COMPOSE;
                    layout();
                }
            } else if (view == SOURCE) {
                if ((pressed & KEY_DOWN) && source_scroll < strlen(code))
                    source_scroll += 128;
                if (pressed & KEY_UP)
                    source_scroll =
                        source_scroll > 128 ? source_scroll - 128 : 0;
            }
        }
        bool play = view == PLAY && !menu_input && !modal && !catalog_updating;
        InputSample game = platform_input_game(sample, play);
        if (held & KEY_TOUCH)
            touchRead(&touch);
        runtime_frame((RuntimeInput){frame_pixels, game.held, game.pressed,
                                     touch.px, touch.py,
                                     (game.held & KEY_TOUCH) != 0});
        if (catalog_updating) {
            network_tick();
            snprintf(message, sizeof(message), "%s", network_status());
            if (!network_busy()) {
                catalog_updating = false;
                if (network_catalog_updated()) {
                    config_load("/lutin/config.json");
                    model_index = 0;
                    if (config_error()[0])
                        snprintf(message, sizeof(message), "%s",
                                 config_error());
                } else if (pressed & KEY_B)
                    snprintf(message, sizeof(message),
                             "Catalog update cancelled");
            }
        } else if (editing_queue < 0 && !pressed)
            agent_tick();
        if (frames++ % 6 == 0 || pressed) {
            render_top(storage);
            render_bottom();
        }
    }
    runtime_stop();
    agent_stop();
    return 0;
}
