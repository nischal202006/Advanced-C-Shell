#ifndef BUILTINS_H
#define BUILTINS_H

#include "parser.h"

int is_builtin(const char *cmd);
int execute_builtin(atomic_cmd_t *cmd);

void builtin_hop(atomic_cmd_t *cmd);
void builtin_reveal(atomic_cmd_t *cmd);
void builtin_log(atomic_cmd_t *cmd, const char *full_line);
void builtin_activities(void);
void builtin_ping(atomic_cmd_t *cmd);
void builtin_fg(atomic_cmd_t *cmd);
void builtin_bg(atomic_cmd_t *cmd);

#endif
