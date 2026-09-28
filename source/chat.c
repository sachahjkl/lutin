#include "chat.h"
#include "agent.h"
#include <string.h>

static void line(ChatPage *page, const char *text, size_t length,
                 unsigned color) {
    size_t row = page->total++;
    if (row < page->start || row - page->start >= CHAT_ROWS)
        return;
    row -= page->start;
    memcpy(page->lines[row], text, length);
    page->lines[row][length] = 0;
    page->colors[row] = color;
}

static void message(const char *role, const char *text, void *context) {
    ChatPage *page = context;
    unsigned color =
        !strcmp(role, "YOU") ? 6 : (!strncmp(role, "TOOL", 4) ? 3 : 2);
    if (!strcmp(role, "TOOL / error"))
        color = 1;
    line(page, role, strlen(role), color);
    if (!page->tools_expanded && !strncmp(role, "TOOL", 4)) {
        line(page, "[START > Tool details]", 22, 7);
        line(page, "", 0, 7);
        return;
    }
    char buffer[CHAT_COLUMNS];
    size_t length = 0;
    while (*text) {
        unsigned char value = (unsigned char)*text++;
        if (value == '\n') {
            line(page, buffer, length, 7);
            length = 0;
        } else {
            buffer[length++] = value >= 32 && value < 127 ? (char)value : ' ';
            if (length == CHAT_COLUMNS) {
                line(page, buffer, length, 7);
                length = 0;
            }
        }
    }
    if (length)
        line(page, buffer, length, 7);
    line(page, "", 0, 7);
}

void chat_page(ChatPage *page, unsigned scroll, bool tools_expanded) {
    memset(page, 0, sizeof(*page));
    page->tools_expanded = tools_expanded;
    page->start = (size_t)-1;
    agent_conversation(message, page);
    size_t maximum = page->total > CHAT_ROWS ? page->total - CHAT_ROWS : 0;
    page->start = scroll > maximum ? 0 : maximum - scroll;
    page->total = 0;
    agent_conversation(message, page);
}
