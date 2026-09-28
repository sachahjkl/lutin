#ifndef AI_CHAT_H
#define AI_CHAT_H
#include <stdbool.h>
#include <stddef.h>
#define CHAT_ROWS 16
#define CHAT_COLUMNS 32
typedef struct {
    char lines[CHAT_ROWS][CHAT_COLUMNS + 1];
    unsigned colors[CHAT_ROWS];
    size_t total, start;
    bool tools_expanded;
} ChatPage;
void chat_page(ChatPage *page, unsigned scroll, bool tools_expanded);
#endif
