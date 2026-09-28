#include "chat.h"
#include "agent.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned messages;
void agent_conversation(AgentConversationText visit, void *context) {
    for (unsigned i = 0; i < messages; i++) {
        char text[96];
        snprintf(text, sizeof(text), "message %u\n\033[2J%s", i,
                 "1234567890123456789012345678901234567890");
        visit(i % 2 ? "TOOL / result" : "YOU", text, context);
    }
}

int main(void) {
    ChatPage page;
    chat_page(&page, 0, true);
    assert(page.total == 0);
    messages = 1;
    chat_page(&page, 0, true);
    assert(!strcmp(page.lines[0], "YOU") && page.colors[0] == 6);
    assert(!strchr(page.lines[2], '\033'));
    messages = 1000;
    chat_page(&page, 0, true);
    assert(page.start + CHAT_ROWS == page.total);
    size_t total = page.total;
    chat_page(&page, 8, true);
    assert(page.start + CHAT_ROWS + 8 == total);
    chat_page(&page, 999999, true);
    assert(page.start == 0 && !strcmp(page.lines[1], "message 0"));
    for (unsigned row = 0; row < CHAT_ROWS; row++)
        assert(strlen(page.lines[row]) <= CHAT_COLUMNS);
    chat_page(&page, 0, false);
    assert(page.total < total);
    puts("Chat: history paging, tail following, colors and control "
         "sanitization passed");
    return 0;
}
