// jobs.c - manages the table of background/stopped processes

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "jobs.h"
#include "shell.h"

int add_job(pid_t pid, const char *command, job_state_t state)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!g_shell.jobs[i].active) {
            g_shell.jobs[i].pid = pid;
            strncpy(g_shell.jobs[i].command, command, MAX_CMD_LEN - 1);
            g_shell.jobs[i].command[MAX_CMD_LEN - 1] = '\0';
            g_shell.jobs[i].state = state;
            g_shell.jobs[i].active = 1;
            g_shell.jobs[i].job_number = g_shell.next_job_number++;
            return g_shell.jobs[i].job_number;
        }
    }
    fprintf(stderr, "Job table full!\n");
    return -1;
}

void remove_job(pid_t pid)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (g_shell.jobs[i].active && g_shell.jobs[i].pid == pid) {
            g_shell.jobs[i].active = 0;
            return;
        }
    }
}

job_t *find_job_by_number(int job_number)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (g_shell.jobs[i].active && g_shell.jobs[i].job_number == job_number)
            return &g_shell.jobs[i];
    }
    return NULL;
}

job_t *find_job_by_pid(pid_t pid)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (g_shell.jobs[i].active && g_shell.jobs[i].pid == pid)
            return &g_shell.jobs[i];
    }
    return NULL;
}

job_t *get_most_recent_job(void)
{
    job_t *best = NULL;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (g_shell.jobs[i].active) {
            if (!best || g_shell.jobs[i].job_number > best->job_number)
                best = &g_shell.jobs[i];
        }
    }
    return best;
}

void update_job_state(pid_t pid, job_state_t new_state)
{
    job_t *j = find_job_by_pid(pid);
    if (j) j->state = new_state;
}

static int cmp_by_name(const void *a, const void *b)
{
    const job_t *ja = *(const job_t *const *)a;
    const job_t *jb = *(const job_t *const *)b;
    return strcmp(ja->command, jb->command);
}

void print_activities(void)
{
    job_t *list[MAX_JOBS];
    int n = 0;

    // first check which ones are still alive
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!g_shell.jobs[i].active) continue;

        int status;
        pid_t r = waitpid(g_shell.jobs[i].pid, &status, WNOHANG | WUNTRACED | WCONTINUED);
        if (r > 0) {
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                g_shell.jobs[i].active = 0;
                continue;
            }
            if (WIFSTOPPED(status))
                g_shell.jobs[i].state = JOB_STOPPED;
            else if (WIFCONTINUED(status))
                g_shell.jobs[i].state = JOB_RUNNING;
        }
        if (g_shell.jobs[i].active)
            list[n++] = &g_shell.jobs[i];
    }

    // sort by command name
    qsort(list, (size_t)n, sizeof(job_t *), cmp_by_name);

    for (int i = 0; i < n; i++) {
        const char *st = (list[i]->state == JOB_RUNNING) ? "Running" : "Stopped";
        printf("[%d] : %s - %s\n", list[i]->pid, list[i]->command, st);
    }
}
