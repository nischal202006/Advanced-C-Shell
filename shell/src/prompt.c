// prompt.c - handles displaying the shell prompt

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <pwd.h>

#include "prompt.h"
#include "shell.h"

void display_prompt(void)
{
    char cwd[MAX_PATH_SIZE];
    char disp[MAX_PATH_SIZE];
    char hostname[256];

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd");
        snprintf(cwd, sizeof(cwd), "?");
    }

    // grab username from passwd
    struct passwd *pw = getpwuid(getuid());
    const char *user = pw ? pw->pw_name : "user";

    if (gethostname(hostname, sizeof(hostname)) != 0)
        snprintf(hostname, sizeof(hostname), "localhost");

    // strip domain part if any
    char *dot = strchr(hostname, '.');
    if (dot) *dot = '\0';

    // check if cwd starts with home dir, replace with ~
    size_t hlen = strlen(g_shell.home_dir);
    if (strncmp(cwd, g_shell.home_dir, hlen) == 0) {
        if (cwd[hlen] == '\0') {
            snprintf(disp, sizeof(disp), "~");
        } else if (cwd[hlen] == '/') {
            snprintf(disp, sizeof(disp), "~%s", cwd + hlen);
        } else {
            // edge case: /home/userX vs /home/user
            snprintf(disp, sizeof(disp), "%s", cwd);
        }
    } else {
        snprintf(disp, sizeof(disp), "%s", cwd);
    }

    printf("<%s@%s:%s> ", user, hostname, disp);
    fflush(stdout);
}
