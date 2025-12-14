#ifndef PARSER_H
#define PARSER_H

#include "shell.h"

/*
 * Grammar we're parsing:
 *   shell_cmd  -> cmd_group ((&|;) cmd_group)* &?
 *   cmd_group  -> atomic (| atomic)*
 *   atomic     -> name (name | input | output)*
 *   input      -> < name
 *   output     -> > name | >> name
 *   name       -> [^|&><;\s]+
 */

// represents a single command like "cat -n file.txt > out.txt"
typedef struct {
    char   *args[MAX_ARGS]; // null terminated
    int     argc;
    char   *input_file;     // from < redirect (last one wins)
    char   *output_file;    // from > or >> redirect
    int     output_append;  // 1 if >> was used
} atomic_cmd_t;

// a pipeline: cmd1 | cmd2 | cmd3
typedef struct {
    atomic_cmd_t  commands[MAX_PIPES + 1];
    int           count;
} cmd_group_t;

typedef enum {
    SEP_NONE,
    SEP_SEMICOLON,  // ;
    SEP_AMPERSAND   // &
} separator_t;

// full parsed input line
typedef struct {
    cmd_group_t   groups[MAX_GROUPS];
    separator_t   separators[MAX_GROUPS];
    int           group_count;
    int           background; // trailing &
} parsed_cmd_t;

// returns 0 on success, -1 on syntax error
int parse_input(const char *input, parsed_cmd_t *parsed);
void free_parsed_cmd(parsed_cmd_t *parsed);

#endif
