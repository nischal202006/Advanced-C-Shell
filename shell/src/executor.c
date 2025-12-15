// executor.c - handles running external commands, pipes, redirection, bg/fg

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

#include "executor.h"
#include "parser.h"
#include "builtins.h"
#include "signals.h"
#include "shell.h"
#include "jobs.h"

extern void log_add_entry(const char *cmd);
extern int g_interactive;

// open files for < and > redirects, dup2 them into stdin/stdout
static int setup_redir(atomic_cmd_t *cmd)
{
    if (cmd->input_file) {
        int fd = open(cmd->input_file, O_RDONLY);
        if (fd < 0) { printf("No such file or directory\n"); return -1; }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    if (cmd->output_file) {
        int fd;
        if (cmd->output_append)
            fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
        else
            fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        if (fd < 0) { printf("Unable to create file for writing\n"); return -1; }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    return 0;
}

// run a single command (no pipes)
static void exec_single(atomic_cmd_t *cmd, int bg, const char *cmd_str)
{
    // builtins run in current process (but we handle redirect by saving fds)
    if (is_builtin(cmd->args[0])) {
        int old_in = -1, old_out = -1;

        if (cmd->input_file) {
            int fd = open(cmd->input_file, O_RDONLY);
            if (fd < 0) { printf("No such file or directory\n"); return; }
            old_in = dup(STDIN_FILENO);
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        if (cmd->output_file) {
            int fd;
            if (cmd->output_append)
                fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
            else
                fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                printf("Unable to create file for writing\n");
                if (old_in >= 0) { dup2(old_in, STDIN_FILENO); close(old_in); }
                return;
            }
            old_out = dup(STDOUT_FILENO);
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        execute_builtin(cmd);

        if (old_in >= 0)  { dup2(old_in, STDIN_FILENO);  close(old_in); }
        if (old_out >= 0) { dup2(old_out, STDOUT_FILENO); close(old_out); }
        return;
    }

    // external command - fork
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return; }

    if (pid == 0) {
        // child
        setpgid(0, 0);
        reset_signals_for_child();

        if (bg) {
            int dn = open("/dev/null", O_RDONLY);
            if (dn >= 0) { dup2(dn, STDIN_FILENO); close(dn); }
        }

        if (setup_redir(cmd) != 0) _exit(1);

        execvp(cmd->args[0], cmd->args);
        printf("Command not found!\n");
        _exit(1);
    }

    // parent
    setpgid(pid, pid);

    if (bg) {
        int jn = add_job(pid, cmd_str ? cmd_str : cmd->args[0], JOB_RUNNING);
        printf("[%d] %d\n", jn, pid);
    } else {
        g_shell.fg_pid = pid;
        strncpy(g_shell.fg_cmd, cmd_str ? cmd_str : cmd->args[0], MAX_CMD_LEN - 1);
        g_shell.fg_cmd[MAX_CMD_LEN - 1] = '\0';

        if (g_interactive) tcsetpgrp(STDIN_FILENO, pid);

        int status;
        pid_t r;
        do { r = waitpid(pid, &status, WUNTRACED); } while (r == -1 && errno == EINTR);

        if (g_interactive) tcsetpgrp(STDIN_FILENO, getpgrp());

        if (r > 0 && WIFSTOPPED(status)) {
            int jn = add_job(pid, cmd_str ? cmd_str : cmd->args[0], JOB_STOPPED);
            printf("[%d] Stopped %s\n", jn, cmd_str ? cmd_str : cmd->args[0]);
        }

        g_shell.fg_pid = 0;
        g_shell.fg_cmd[0] = '\0';
    }
}

// run a pipeline: cmd1 | cmd2 | ... | cmdN
static void exec_pipeline(cmd_group_t *grp, int bg, const char *cmd_str)
{
    int n = grp->count;
    if (n == 1) { exec_single(&grp->commands[0], bg, cmd_str); return; }

    int pipes[MAX_PIPES][2];
    for (int i = 0; i < n - 1; i++) {
        if (pipe(pipes[i]) < 0) { perror("pipe"); return; }
    }

    pid_t pids[MAX_PIPES + 1];

    for (int i = 0; i < n; i++) {
        pids[i] = fork();
        if (pids[i] < 0) { perror("fork"); return; }

        if (pids[i] == 0) {
            // child i
            if (i == 0) setpgid(0, 0);
            else        setpgid(0, pids[0]);

            reset_signals_for_child();

            // wire up pipes
            if (i > 0)     dup2(pipes[i-1][0], STDIN_FILENO);
            if (i < n - 1) dup2(pipes[i][1], STDOUT_FILENO);

            // close all pipe fds
            for (int j = 0; j < n - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            // bg + first cmd: read from /dev/null if no explicit input redir
            if (bg && i == 0 && grp->commands[i].input_file == NULL) {
                int dn = open("/dev/null", O_RDONLY);
                if (dn >= 0) { dup2(dn, STDIN_FILENO); close(dn); }
            }

            if (setup_redir(&grp->commands[i]) != 0) _exit(1);
            execvp(grp->commands[i].args[0], grp->commands[i].args);
            printf("Command not found!\n");
            _exit(1);
        }

        // parent sets pgid
        if (i == 0) setpgid(pids[i], pids[i]);
        else        setpgid(pids[i], pids[0]);
    }

    // parent closes all pipes
    for (int i = 0; i < n - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    if (bg) {
        int jn = add_job(pids[0], cmd_str ? cmd_str : grp->commands[0].args[0], JOB_RUNNING);
        printf("[%d] %d\n", jn, pids[0]);
    } else {
        g_shell.fg_pid = pids[0];
        if (g_interactive) tcsetpgrp(STDIN_FILENO, pids[0]);

        for (int i = 0; i < n; i++) {
            int status;
            pid_t r;
            do { r = waitpid(pids[i], &status, WUNTRACED); } while (r == -1 && errno == EINTR);

            if (r > 0 && WIFSTOPPED(status) && i == n - 1) {
                int jn = add_job(pids[0], cmd_str ? cmd_str : grp->commands[0].args[0], JOB_STOPPED);
                printf("[%d] Stopped %s\n", jn, cmd_str ? cmd_str : grp->commands[0].args[0]);
            }
        }

        if (g_interactive) tcsetpgrp(STDIN_FILENO, getpgrp());
        g_shell.fg_pid = 0;
        g_shell.fg_cmd[0] = '\0';
    }
}

void execute_cmd_group(cmd_group_t *grp, int bg, const char *cmd_str)
{
    exec_pipeline(grp, bg, cmd_str);
}

// build a readable string from a cmd_group for display/logging
static void group_to_str(cmd_group_t *grp, char *buf, size_t sz)
{
    buf[0] = '\0';
    for (int c = 0; c < grp->count; c++) {
        if (c > 0) strncat(buf, " | ", sz - strlen(buf) - 1);
        for (int a = 0; a < grp->commands[c].argc; a++) {
            if (a > 0) strncat(buf, " ", sz - strlen(buf) - 1);
            strncat(buf, grp->commands[c].args[a], sz - strlen(buf) - 1);
        }
        if (grp->commands[c].input_file) {
            strncat(buf, " < ", sz - strlen(buf) - 1);
            strncat(buf, grp->commands[c].input_file, sz - strlen(buf) - 1);
        }
        if (grp->commands[c].output_file) {
            strncat(buf, grp->commands[c].output_append ? " >> " : " > ", sz - strlen(buf) - 1);
            strncat(buf, grp->commands[c].output_file, sz - strlen(buf) - 1);
        }
    }
}

void execute_command(parsed_cmd_t *parsed, const char *full_line)
{
    if (parsed->group_count == 0) return;

    for (int g = 0; g < parsed->group_count; g++) {
        int bg = 0;

        if (g < parsed->group_count - 1) {
            if (parsed->separators[g + 1] == SEP_AMPERSAND) bg = 1;
        } else {
            if (parsed->background) bg = 1;
        }

        char cs[MAX_CMD_LEN];
        group_to_str(&parsed->groups[g], cs, sizeof(cs));
        execute_cmd_group(&parsed->groups[g], bg, cs);
    }
}

void reap_background_jobs(void)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!g_shell.jobs[i].active) continue;

        int status;
        pid_t r = waitpid(g_shell.jobs[i].pid, &status, WNOHANG | WUNTRACED | WCONTINUED);

        if (r > 0) {
            if (WIFEXITED(status)) {
                printf("%s with pid %d exited normally\n",
                       g_shell.jobs[i].command, g_shell.jobs[i].pid);
                g_shell.jobs[i].active = 0;
            } else if (WIFSIGNALED(status)) {
                printf("%s with pid %d exited abnormally\n",
                       g_shell.jobs[i].command, g_shell.jobs[i].pid);
                g_shell.jobs[i].active = 0;
            } else if (WIFSTOPPED(status)) {
                g_shell.jobs[i].state = JOB_STOPPED;
            } else if (WIFCONTINUED(status)) {
                g_shell.jobs[i].state = JOB_RUNNING;
            }
        } else if (r == -1 && errno == ECHILD) {
            g_shell.jobs[i].active = 0;
        }
    }
}
