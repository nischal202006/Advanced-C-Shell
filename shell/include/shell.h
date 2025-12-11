#ifndef SHELL_H
#define SHELL_H

#include <sys/types.h>

// some global constants
#define MAX_INPUT_SIZE    4096
#define MAX_PATH_SIZE     4096
#define MAX_ARGS          64
#define MAX_PIPES         16
#define MAX_GROUPS        32
#define MAX_LOG_ENTRIES   15
#define MAX_JOBS          64
#define LOG_FILE          ".shell_log"
#define MAX_CMD_LEN       4096

// states for background jobs
typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE
} job_state_t;

// tracks a single bg/stopped job
typedef struct {
    int            job_number;
    pid_t          pid;
    char           command[MAX_CMD_LEN];
    job_state_t    state;
    int            active; // 1 = in use, 0 = free slot
} job_t;

// main shell context -- global state carried across the REPL
typedef struct {
    char    home_dir[MAX_PATH_SIZE];
    char    prev_dir[MAX_PATH_SIZE];
    int     has_prev_dir; // false until first valid hop

    // job control stuff
    job_t   jobs[MAX_JOBS];
    int     next_job_number;

    // currently running foreground process
    pid_t   fg_pid;
    char    fg_cmd[MAX_CMD_LEN];
    int     fg_job_number;

    // command history (circular buffer)
    char    log_entries[MAX_LOG_ENTRIES][MAX_CMD_LEN];
    int     log_count;
    int     log_start;
    char    log_file_path[MAX_PATH_SIZE];
} shell_ctx_t;

extern shell_ctx_t g_shell;

#endif
