#ifndef JOBS_H
#define JOBS_H

#include "shell.h"

int add_job(pid_t pid, const char *command, job_state_t state);
void remove_job(pid_t pid);
job_t *find_job_by_number(int job_number);
job_t *find_job_by_pid(pid_t pid);
job_t *get_most_recent_job(void);
void update_job_state(pid_t pid, job_state_t new_state);
void print_activities(void);

#endif
