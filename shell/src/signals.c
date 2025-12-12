// signals.c - sets up signal handlers for the shell
// shell itself should never die from ctrl-c or ctrl-z

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#include "signals.h"
#include "shell.h"
#include "jobs.h"

// ctrl-c: forward to fg child, don't kill shell
static void handle_sigint(int sig)
{
    (void)sig;
    if (g_shell.fg_pid > 0) {
        kill(-g_shell.fg_pid, SIGINT);
    } else {
        write(STDOUT_FILENO, "\n", 1);
    }
}

// ctrl-z: forward to fg child
static void handle_sigtstp(int sig)
{
    (void)sig;
    if (g_shell.fg_pid > 0)
        kill(-g_shell.fg_pid, SIGTSTP);
}

// reap zombie bg children
static void handle_sigchld(int sig)
{
    (void)sig;
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        if (pid == g_shell.fg_pid)
            continue; // fg process handled separately

        job_t *j = find_job_by_pid(pid);
        if (!j) continue;

        if (WIFEXITED(status) || WIFSIGNALED(status))
            j->state = JOB_DONE;
        else if (WIFSTOPPED(status))
            j->state = JOB_STOPPED;
        else if (WIFCONTINUED(status))
            j->state = JOB_RUNNING;
    }
}

void setup_signal_handlers(void)
{
    struct sigaction sa;

    // SIGINT - no SA_RESTART so fgets gets interrupted and prompt redraws
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    // SIGTSTP - same, no restart
    sa.sa_handler = handle_sigtstp;
    sa.sa_flags = 0;
    sigaction(SIGTSTP, &sa, NULL);

    // SIGCHLD - SA_NOCLDSTOP so we don't get notified on stop
    sa.sa_handler = handle_sigchld;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
}

void reset_signals_for_child(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);
}
