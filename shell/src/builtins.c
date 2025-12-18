// builtins.c - all the built-in commands for the shell
// hop, reveal, log, activities, ping, fg, bg, exit

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>

#include "builtins.h"
#include "shell.h"
#include "jobs.h"

extern int g_interactive;

static const char *builtin_list[] = {
    "hop", "reveal", "log", "activities", "ping",
    "fg", "bg", "exit", NULL
};

int is_builtin(const char *cmd)
{
    for (int i = 0; builtin_list[i]; i++)
        if (strcmp(cmd, builtin_list[i]) == 0) return 1;
    return 0;
}

// ---- hop (cd) ----

static void hop_to(const char *target)
{
    char path[MAX_PATH_SIZE];

    if (strcmp(target, "~") == 0 || strlen(target) == 0) {
        snprintf(path, sizeof(path), "%s", g_shell.home_dir);
    } else if (strcmp(target, "-") == 0) {
        if (!g_shell.has_prev_dir) return;
        snprintf(path, sizeof(path), "%s", g_shell.prev_dir);
    } else if (target[0] == '~' && (target[1] == '/' || target[1] == '\0')) {
        snprintf(path, sizeof(path), "%s%s", g_shell.home_dir, target + 1);
    } else {
        snprintf(path, sizeof(path), "%s", target);
    }

    char cur[MAX_PATH_SIZE];
    if (getcwd(cur, sizeof(cur)) == NULL) cur[0] = '\0';

    if (chdir(path) != 0) {
        printf("No such directory!\n");
        return;
    }

    // save old dir
    if (cur[0]) {
        strncpy(g_shell.prev_dir, cur, MAX_PATH_SIZE - 1);
        g_shell.prev_dir[MAX_PATH_SIZE - 1] = '\0';
        g_shell.has_prev_dir = 1;
    }
}

void builtin_hop(atomic_cmd_t *cmd)
{
    if (cmd->argc <= 1) {
        hop_to("~");
        return;
    }
    for (int i = 1; i < cmd->argc; i++)
        hop_to(cmd->args[i]);
}

// ---- reveal (ls) ----

static int str_cmp(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

void builtin_reveal(atomic_cmd_t *cmd)
{
    int show_hidden = 0, line_mode = 0;
    char *target = NULL;
    int tgt_count = 0;

    for (int i = 1; i < cmd->argc; i++) {
        if (cmd->args[i][0] == '-' && strlen(cmd->args[i]) > 1) {
            // parse flags
            for (int j = 1; cmd->args[i][j]; j++) {
                if (cmd->args[i][j] == 'a') show_hidden = 1;
                else if (cmd->args[i][j] == 'l') line_mode = 1;
            }
        } else {
            target = cmd->args[i];
            tgt_count++;
        }
    }

    if (tgt_count > 1) {
        printf("reveal: Invalid Syntax!\n");
        return;
    }

    // figure out which directory to list
    char dir_path[MAX_PATH_SIZE];
    if (!target || strcmp(target, ".") == 0) {
        if (getcwd(dir_path, sizeof(dir_path)) == NULL) { perror("getcwd"); return; }
    } else if (strcmp(target, "~") == 0) {
        snprintf(dir_path, sizeof(dir_path), "%s", g_shell.home_dir);
    } else if (strcmp(target, "..") == 0) {
        if (getcwd(dir_path, sizeof(dir_path)) == NULL) { perror("getcwd"); return; }
        char *sl = strrchr(dir_path, '/');
        if (sl && sl != dir_path) *sl = '\0';
    } else if (strcmp(target, "-") == 0) {
        if (!g_shell.has_prev_dir) { printf("No such directory!\n"); return; }
        snprintf(dir_path, sizeof(dir_path), "%s", g_shell.prev_dir);
    } else if (target[0] == '~' && (target[1] == '/' || target[1] == '\0')) {
        snprintf(dir_path, sizeof(dir_path), "%s%s", g_shell.home_dir, target + 1);
    } else if (target[0] == '/') {
        snprintf(dir_path, sizeof(dir_path), "%s", target);
    } else {
        char cwd[MAX_PATH_SIZE];
        if (getcwd(cwd, sizeof(cwd)) == NULL) { perror("getcwd"); return; }
        strncpy(dir_path, cwd, MAX_PATH_SIZE - 1);
        dir_path[MAX_PATH_SIZE - 1] = '\0';
        strncat(dir_path, "/", MAX_PATH_SIZE - strlen(dir_path) - 1);
        strncat(dir_path, target, MAX_PATH_SIZE - strlen(dir_path) - 1);
    }

    DIR *dir = opendir(dir_path);
    if (!dir) { printf("No such directory!\n"); return; }

    char *entries[4096];
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && count < 4096) {
        if (!show_hidden && ent->d_name[0] == '.') continue;
        entries[count++] = strdup(ent->d_name);
    }
    closedir(dir);

    qsort(entries, (size_t)count, sizeof(char *), str_cmp);

    if (line_mode) {
        for (int i = 0; i < count; i++)
            printf("%s\n", entries[i]);
    } else {
        for (int i = 0; i < count; i++) {
            if (i > 0) printf(" ");
            printf("%s", entries[i]);
        }
        if (count > 0) printf("\n");
    }

    for (int i = 0; i < count; i++) free(entries[i]);
}

// ---- log (command history) ----

static void log_load(void)
{
    FILE *f = fopen(g_shell.log_file_path, "r");
    if (!f) return;

    g_shell.log_count = 0;
    g_shell.log_start = 0;

    char line[MAX_CMD_LEN];
    while (fgets(line, sizeof(line), f) && g_shell.log_count < MAX_LOG_ENTRIES) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        strncpy(g_shell.log_entries[g_shell.log_count], line, MAX_CMD_LEN - 1);
        g_shell.log_entries[g_shell.log_count][MAX_CMD_LEN - 1] = '\0';
        g_shell.log_count++;
    }
    fclose(f);
}

static void log_save(void)
{
    FILE *f = fopen(g_shell.log_file_path, "w");
    if (!f) return;
    for (int i = 0; i < g_shell.log_count; i++) {
        int idx = (g_shell.log_start + i) % MAX_LOG_ENTRIES;
        fprintf(f, "%s\n", g_shell.log_entries[idx]);
    }
    fclose(f);
}

void log_add_entry(const char *cmd)
{
    // don't add duplicates of the last entry
    if (g_shell.log_count > 0) {
        int last = (g_shell.log_start + g_shell.log_count - 1) % MAX_LOG_ENTRIES;
        if (strcmp(g_shell.log_entries[last], cmd) == 0) return;
    }

    if (g_shell.log_count < MAX_LOG_ENTRIES) {
        int idx = (g_shell.log_start + g_shell.log_count) % MAX_LOG_ENTRIES;
        strncpy(g_shell.log_entries[idx], cmd, MAX_CMD_LEN - 1);
        g_shell.log_entries[idx][MAX_CMD_LEN - 1] = '\0';
        g_shell.log_count++;
    } else {
        // overwrite oldest
        strncpy(g_shell.log_entries[g_shell.log_start], cmd, MAX_CMD_LEN - 1);
        g_shell.log_entries[g_shell.log_start][MAX_CMD_LEN - 1] = '\0';
        g_shell.log_start = (g_shell.log_start + 1) % MAX_LOG_ENTRIES;
    }
    log_save();
}

void log_init(void)
{
    strncpy(g_shell.log_file_path, g_shell.home_dir, MAX_PATH_SIZE - 1);
    g_shell.log_file_path[MAX_PATH_SIZE - 1] = '\0';
    strncat(g_shell.log_file_path, "/", MAX_PATH_SIZE - strlen(g_shell.log_file_path) - 1);
    strncat(g_shell.log_file_path, LOG_FILE, MAX_PATH_SIZE - strlen(g_shell.log_file_path) - 1);
    g_shell.log_count = 0;
    g_shell.log_start = 0;
    log_load();
}

void builtin_log(atomic_cmd_t *cmd, const char *full_line)
{
    (void)full_line;

    if (cmd->argc == 1) {
        // print history oldest to newest
        for (int i = 0; i < g_shell.log_count; i++) {
            int idx = (g_shell.log_start + i) % MAX_LOG_ENTRIES;
            printf("%s\n", g_shell.log_entries[idx]);
        }
    } else if (cmd->argc == 2 && strcmp(cmd->args[1], "purge") == 0) {
        g_shell.log_count = 0;
        g_shell.log_start = 0;
        log_save();
    } else if (cmd->argc == 3 && strcmp(cmd->args[1], "execute") == 0) {
        char *end;
        long idx = strtol(cmd->args[2], &end, 10);
        if (*end != '\0' || idx < 1 || idx > g_shell.log_count) {
            printf("log: Invalid Syntax!\n");
            return;
        }
        // index is 1-based, newest first
        int actual = (g_shell.log_start + g_shell.log_count - (int)idx) % MAX_LOG_ENTRIES;
        if (actual < 0) actual += MAX_LOG_ENTRIES;

        char rerun[MAX_CMD_LEN];
        strncpy(rerun, g_shell.log_entries[actual], MAX_CMD_LEN - 1);
        rerun[MAX_CMD_LEN - 1] = '\0';
        printf("%s\n", rerun);

        extern void execute_line(const char *line, int from_log);
        execute_line(rerun, 1);
    } else {
        printf("log: Invalid Syntax!\n");
    }
}

// ---- activities ----

void builtin_activities(void) { print_activities(); }

// ---- ping ----

void builtin_ping(atomic_cmd_t *cmd)
{
    if (cmd->argc != 3) { printf("Invalid syntax!\n"); return; }

    char *e1, *e2;
    long pid_val = strtol(cmd->args[1], &e1, 10);
    long sig_val = strtol(cmd->args[2], &e2, 10);

    if (*e1 != '\0' || *e2 != '\0') { printf("Invalid syntax!\n"); return; }

    int sig = (int)(sig_val % 32);
    if (kill((pid_t)pid_val, sig) != 0) {
        printf("No such process found\n");
        return;
    }
    printf("Sent signal %d to process with pid %ld\n", sig, pid_val);

    // update our job table if relevant
    if (sig == SIGSTOP || sig == SIGTSTP)
        update_job_state((pid_t)pid_val, JOB_STOPPED);
    else if (sig == SIGCONT)
        update_job_state((pid_t)pid_val, JOB_RUNNING);
    else if (sig == SIGKILL || sig == SIGTERM)
        remove_job((pid_t)pid_val);
}

// ---- fg ----

void builtin_fg(atomic_cmd_t *cmd)
{
    job_t *j = NULL;

    if (cmd->argc == 1) {
        j = get_most_recent_job();
    } else if (cmd->argc == 2) {
        char *end;
        int num = (int)strtol(cmd->args[1], &end, 10);
        if (*end != '\0') { printf("No such job\n"); return; }
        j = find_job_by_number(num);
    } else {
        printf("No such job\n"); return;
    }

    if (!j) { printf("No such job\n"); return; }

    printf("%s\n", j->command);

    g_shell.fg_pid = j->pid;
    strncpy(g_shell.fg_cmd, j->command, MAX_CMD_LEN - 1);
    g_shell.fg_cmd[MAX_CMD_LEN - 1] = '\0';
    g_shell.fg_job_number = j->job_number;

    if (j->state == JOB_STOPPED)
        kill(-j->pid, SIGCONT);
    j->state = JOB_RUNNING;

    if (g_interactive) tcsetpgrp(STDIN_FILENO, j->pid);

    int status;
    pid_t r;
    do { r = waitpid(j->pid, &status, WUNTRACED); } while (r == -1 && errno == EINTR);

    if (g_interactive) tcsetpgrp(STDIN_FILENO, getpgrp());

    if (r > 0 && WIFSTOPPED(status)) {
        j->state = JOB_STOPPED;
        printf("[%d] Stopped %s\n", j->job_number, j->command);
    } else {
        remove_job(j->pid);
    }

    g_shell.fg_pid = 0;
    g_shell.fg_cmd[0] = '\0';
}

// ---- bg ----

void builtin_bg(atomic_cmd_t *cmd)
{
    job_t *j = NULL;

    if (cmd->argc == 1) {
        j = get_most_recent_job();
    } else if (cmd->argc == 2) {
        char *end;
        int num = (int)strtol(cmd->args[1], &end, 10);
        if (*end != '\0') { printf("No such job\n"); return; }
        j = find_job_by_number(num);
    } else {
        printf("No such job\n"); return;
    }

    if (!j) { printf("No such job\n"); return; }

    if (j->state == JOB_RUNNING) {
        printf("Job already running\n");
        return;
    }

    j->state = JOB_RUNNING;
    kill(-j->pid, SIGCONT);
    printf("[%d] %s &\n", j->job_number, j->command);
}

// ---- dispatcher ----

int execute_builtin(atomic_cmd_t *cmd)
{
    const char *name = cmd->args[0];
    if (strcmp(name, "hop") == 0)             builtin_hop(cmd);
    else if (strcmp(name, "reveal") == 0)     builtin_reveal(cmd);
    else if (strcmp(name, "log") == 0)        builtin_log(cmd, NULL);
    else if (strcmp(name, "activities") == 0) builtin_activities();
    else if (strcmp(name, "ping") == 0)       builtin_ping(cmd);
    else if (strcmp(name, "fg") == 0)         builtin_fg(cmd);
    else if (strcmp(name, "bg") == 0)         builtin_bg(cmd);
    else if (strcmp(name, "exit") == 0)       exit(0);
    else return -1;
    return 0;
}
