// parser.c - tokenizer + recursive descent parser for the shell grammar

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "parser.h"
#include "shell.h"

// token types
typedef enum {
    TOK_WORD,
    TOK_PIPE,        // |
    TOK_SEMICOLON,   // ;
    TOK_AMPERSAND,   // &
    TOK_REDIR_IN,    // <
    TOK_REDIR_OUT,   // >
    TOK_REDIR_APP,   // >>
    TOK_END
} tok_type_t;

typedef struct {
    tok_type_t type;
    char       val[512]; // individual tokens wont be this long
} token_t;

// break input string into tokens
static int tokenize(const char *input, token_t *toks, int max)
{
    int cnt = 0;
    int i = 0;
    int len = (int)strlen(input);

    while (i < len && cnt < max - 1) {
        // skip whitespace
        while (i < len && isspace((unsigned char)input[i]))
            i++;
        if (i >= len) break;

        char c = input[i];

        if (c == '|') {
            toks[cnt].type = TOK_PIPE;
            strcpy(toks[cnt].val, "|");
            cnt++; i++;
        } else if (c == ';') {
            toks[cnt].type = TOK_SEMICOLON;
            strcpy(toks[cnt].val, ";");
            cnt++; i++;
        } else if (c == '&') {
            toks[cnt].type = TOK_AMPERSAND;
            strcpy(toks[cnt].val, "&");
            cnt++; i++;
        } else if (c == '<') {
            toks[cnt].type = TOK_REDIR_IN;
            strcpy(toks[cnt].val, "<");
            cnt++; i++;
        } else if (c == '>') {
            if (i + 1 < len && input[i+1] == '>') {
                toks[cnt].type = TOK_REDIR_APP;
                strcpy(toks[cnt].val, ">>");
                cnt++; i += 2;
            } else {
                toks[cnt].type = TOK_REDIR_OUT;
                strcpy(toks[cnt].val, ">");
                cnt++; i++;
            }
        } else {
            // word: grab everything until special char or whitespace
            int start = i;
            while (i < len && !isspace((unsigned char)input[i])
                   && input[i] != '|' && input[i] != '&'
                   && input[i] != '>' && input[i] != '<'
                   && input[i] != ';')
                i++;

            int wlen = i - start;
            if (wlen >= 512) wlen = 511;
            strncpy(toks[cnt].val, input + start, (size_t)wlen);
            toks[cnt].val[wlen] = '\0';
            toks[cnt].type = TOK_WORD;
            cnt++;
        }
    }

    toks[cnt].type = TOK_END;
    toks[cnt].val[0] = '\0';
    return cnt + 1;
}

// simple parser state
typedef struct {
    token_t *toks;
    int pos;
} pstate_t;

static token_t *peek(pstate_t *ps) { return &ps->toks[ps->pos]; }

static token_t *advance(pstate_t *ps)
{
    token_t *t = &ps->toks[ps->pos];
    if (t->type != TOK_END) ps->pos++;
    return t;
}

// parse one atomic command (handles args + redirects)
static int parse_atomic(pstate_t *ps, atomic_cmd_t *cmd)
{
    memset(cmd, 0, sizeof(*cmd));

    // first token has to be a word (command name)
    if (peek(ps)->type != TOK_WORD)
        return -1;

    while (1) {
        token_t *t = peek(ps);

        if (t->type == TOK_WORD) {
            if (cmd->argc >= MAX_ARGS - 1) break;
            cmd->args[cmd->argc++] = strdup(t->val);
            advance(ps);
        } else if (t->type == TOK_REDIR_IN) {
            advance(ps);
            if (peek(ps)->type != TOK_WORD) return -1;
            if (cmd->input_file) free(cmd->input_file);
            cmd->input_file = strdup(peek(ps)->val);
            advance(ps);
        } else if (t->type == TOK_REDIR_OUT) {
            advance(ps);
            if (peek(ps)->type != TOK_WORD) return -1;
            if (cmd->output_file) free(cmd->output_file);
            cmd->output_file = strdup(peek(ps)->val);
            cmd->output_append = 0;
            advance(ps);
        } else if (t->type == TOK_REDIR_APP) {
            advance(ps);
            if (peek(ps)->type != TOK_WORD) return -1;
            if (cmd->output_file) free(cmd->output_file);
            cmd->output_file = strdup(peek(ps)->val);
            cmd->output_append = 1;
            advance(ps);
        } else {
            break; // done with this atomic
        }
    }
    cmd->args[cmd->argc] = NULL;
    return (cmd->argc == 0) ? -1 : 0;
}

// parse a pipeline (atomic | atomic | ...)
static int parse_cmd_group(pstate_t *ps, cmd_group_t *grp)
{
    memset(grp, 0, sizeof(*grp));
    grp->count = 0;

    if (parse_atomic(ps, &grp->commands[grp->count]) != 0)
        return -1;
    grp->count++;

    while (peek(ps)->type == TOK_PIPE) {
        advance(ps); // eat the |
        if (grp->count >= MAX_PIPES + 1) return -1;
        if (parse_atomic(ps, &grp->commands[grp->count]) != 0)
            return -1;
        grp->count++;
    }
    return 0;
}

int parse_input(const char *input, parsed_cmd_t *parsed)
{
    token_t toks[256];
    memset(parsed, 0, sizeof(*parsed));

    // skip blank lines
    const char *p = input;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '\0') {
        parsed->group_count = 0;
        return 0;
    }

    tokenize(input, toks, 256);

    pstate_t ps;
    ps.toks = toks;
    ps.pos = 0;

    // first group
    parsed->separators[0] = SEP_NONE;
    if (parse_cmd_group(&ps, &parsed->groups[0]) != 0) {
        printf("Invalid Syntax!\n");
        free_parsed_cmd(parsed);
        return -1;
    }
    parsed->group_count = 1;

    // rest of the groups separated by ; or &
    while (peek(&ps)->type == TOK_SEMICOLON || peek(&ps)->type == TOK_AMPERSAND) {
        token_t *sep = advance(&ps);

        // trailing & with nothing after?
        if (peek(&ps)->type == TOK_END) {
            if (sep->type == TOK_AMPERSAND)
                parsed->background = 1;
            break;
        }

        if (parsed->group_count >= MAX_GROUPS) {
            printf("Invalid Syntax!\n");
            free_parsed_cmd(parsed);
            return -1;
        }

        parsed->separators[parsed->group_count] =
            (sep->type == TOK_SEMICOLON) ? SEP_SEMICOLON : SEP_AMPERSAND;

        if (parse_cmd_group(&ps, &parsed->groups[parsed->group_count]) != 0) {
            printf("Invalid Syntax!\n");
            free_parsed_cmd(parsed);
            return -1;
        }
        parsed->group_count++;
    }

    // make sure we consumed everything
    if (peek(&ps)->type != TOK_END) {
        printf("Invalid Syntax!\n");
        free_parsed_cmd(parsed);
        return -1;
    }

    return 0;
}

void free_parsed_cmd(parsed_cmd_t *parsed)
{
    for (int g = 0; g < parsed->group_count; g++) {
        cmd_group_t *grp = &parsed->groups[g];
        for (int c = 0; c < grp->count; c++) {
            atomic_cmd_t *cmd = &grp->commands[c];
            for (int a = 0; a < cmd->argc; a++) {
                free(cmd->args[a]);
                cmd->args[a] = NULL;
            }
            free(cmd->input_file);
            cmd->input_file = NULL;
            free(cmd->output_file);
            cmd->output_file = NULL;
        }
    }
    parsed->group_count = 0;
}
