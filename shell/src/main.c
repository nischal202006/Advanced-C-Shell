// main.c - entry point for the shell

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <termios.h>

#include "shell.h"
#include "prompt.h"
#include "parser.h"
#include "builtins.h"
#include "executor.h"
#include "signals.h"
#include "jobs.h"

shell_ctx_t g_shell;
int g_interactive = 0; // set to 1 if stdin is a terminal

extern void log_add_entry(const char *cmd);
extern void log_init(void);

static int is_log_cmd(parsed_cmd_t *p)
{
    if (p->group_count == 0) return 0;
    if (p->groups[0].count == 0) return 0;
    if (p->groups[0].commands[0].argc == 0) return 0;
    return (strcmp(p->groups[0].commands[0].args[0], "log") == 0);
}

void execute_line(const char *line, int from_log)
{
    parsed_cmd_t parsed;
    if (parse_input(line, &parsed) != 0) return;
    if (parsed.group_count == 0) return;

    if (!from_log && !is_log_cmd(&parsed))
        log_add_entry(line);

    execute_command(&parsed, line);
    free_parsed_cmd(&parsed);
}

static void strip_newline(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' ||
                       s[len-1] == '\t' || s[len-1] == ' '))
        s[--len] = '\0';
}

int main(void)
{
    char input[MAX_INPUT_SIZE];
    memset(&g_shell, 0, sizeof(g_shell));

    if (getcwd(g_shell.home_dir, MAX_PATH_SIZE) == NULL) {
        perror("getcwd");
        return 1;
    }

    g_shell.has_prev_dir = 0;
    g_shell.next_job_number = 1;
    g_shell.fg_pid = 0;

    log_init();

    // check if we're running interactively (stdin is a terminal)
    g_interactive = isatty(STDIN_FILENO);

    if (g_interactive) {
        // ignore SIGTTOU so tcsetpgrp doesn't stop us
        signal(SIGTTOU, SIG_IGN);

        setup_signal_handlers();

        // take terminal control
        // loop until we're in the foreground process group
        while (tcgetpgrp(STDIN_FILENO) != (getpgrp()))
            kill(-getpgrp(), SIGTTIN);

        setpgid(0, 0);
        tcsetpgrp(STDIN_FILENO, getpgrp());
    } else {
        setup_signal_handlers();
    }

    // main loop
    while (1) {
        reap_background_jobs();
        display_prompt();

        if (fgets(input, sizeof(input), stdin) == NULL) {
            // ctrl-c interrupted fgets -- not real EOF, just redraw prompt
            if (!feof(stdin)) {
                clearerr(stdin);
                continue;
            }
            // actual ctrl-d (EOF)
            printf("logout\n");
            for (int i = 0; i < MAX_JOBS; i++)
                if (g_shell.jobs[i].active) kill(g_shell.jobs[i].pid, SIGKILL);
            break;
        }

        strip_newline(input);
        if (strlen(input) == 0) continue;

        execute_line(input, 0);
    }

    return 0;
}
