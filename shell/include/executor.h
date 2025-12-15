#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"

void execute_command(parsed_cmd_t *parsed, const char *full_line);
void execute_cmd_group(cmd_group_t *group, int background, const char *cmd_str);
void reap_background_jobs(void);

#endif
