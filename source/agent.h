#ifndef AI_AGENT_H
#define AI_AGENT_H
#include <stdbool.h>
typedef void (*AgentConversationText)(const char *role, const char *text,
                                      void *context);
void agent_conversation(AgentConversationText visit, void *context);
bool agent_open(unsigned session);
bool agent_sessions(unsigned after, unsigned *numbers, unsigned capacity,
                    unsigned *count);
bool agent_create(unsigned *number);
bool agent_reset(void);
bool agent_delete(void);
bool agent_submit(const char *text, bool steer);
void agent_tick(void);
void agent_stop(void);
bool agent_close(void);
bool agent_resume(void);
unsigned agent_model(void);
bool agent_select_model(unsigned index);
bool agent_busy(void);
const char *agent_status(void);
const char *agent_text(void);
unsigned agent_queue_size(void);
bool agent_remove_queued(unsigned index);
const char *agent_queued(unsigned index);
bool agent_edit_queued(unsigned index, const char *text);
bool agent_prioritize_queued(unsigned index);
#endif
