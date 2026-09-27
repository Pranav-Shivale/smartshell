/*
 * parser.c — Command-line parser implementation for SmartShell.
 */

#include "parser.h"
#include "utils.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Dynamic string buffer for building individual tokens. */
typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} token_buf_t;

static void token_buf_init(token_buf_t *tb)
{
    tb->cap = 32;
    tb->len = 0;
    tb->buf = xmalloc(tb->cap);
    tb->buf[0] = '\0';
}

static void token_buf_append(token_buf_t *tb, char c)
{
    if (tb->len + 1 >= tb->cap) {
        tb->cap *= 2;
        tb->buf = xrealloc(tb->buf, tb->cap);
    }
    tb->buf[tb->len++] = c;
    tb->buf[tb->len] = '\0';
}

static void token_buf_free(token_buf_t *tb)
{
    free(tb->buf);
    tb->buf = NULL;
    tb->len = 0;
    tb->cap = 0;
}

/* Dynamic argument list for accumulating argv elements. */
typedef struct {
    char **items;
    size_t count;
    size_t cap;
} arg_list_t;

static void arg_list_init(arg_list_t *al)
{
    al->cap = 8;
    al->count = 0;
    al->items = xmalloc(al->cap * sizeof(char *));
    al->items[0] = NULL;
}

static void arg_list_append(arg_list_t *al, char *arg)
{
    if (al->count + 2 > al->cap) {
        al->cap *= 2;
        al->items = xrealloc(al->items, al->cap * sizeof(char *));
    }
    al->items[al->count++] = arg;
    al->items[al->count] = NULL;
}

static void arg_list_free(arg_list_t *al)
{
    for (size_t i = 0; i < al->count; i++) {
        free(al->items[i]);
    }
    free(al->items);
    al->items = NULL;
    al->count = 0;
    al->cap = 0;
}

command_t *parse_command(const char *line, const char **errmsg)
{
    if (errmsg != NULL) {
        *errmsg = NULL;
    }

    if (line == NULL) {
        return NULL;
    }

    token_buf_t tok;
    token_buf_init(&tok);

    arg_list_t args;
    arg_list_init(&args);

    bool in_word = false;
    const char *p = line;

    while (*p != '\0') {
        if (isspace((unsigned char)*p)) {
            if (in_word) {
                arg_list_append(&args, tok.buf);
                token_buf_init(&tok);
                in_word = false;
            }
            p++;
        } else if (*p == '\\') {
            in_word = true;
            p++;
            if (*p == '\0') {
                if (errmsg != NULL) {
                    *errmsg = "trailing backslash";
                }
                token_buf_free(&tok);
                arg_list_free(&args);
                return NULL;
            }
            token_buf_append(&tok, *p);
            p++;
        } else if (*p == '\'') {
            in_word = true;
            p++;
            while (*p != '\0' && *p != '\'') {
                token_buf_append(&tok, *p);
                p++;
            }
            if (*p == '\0') {
                if (errmsg != NULL) {
                    *errmsg = "unclosed single quote";
                }
                token_buf_free(&tok);
                arg_list_free(&args);
                return NULL;
            }
            p++; /* skip closing single quote */
        } else if (*p == '"') {
            in_word = true;
            p++;
            while (*p != '\0' && *p != '"') {
                if (*p == '\\') {
                    p++;
                    if (*p == '\0') {
                        if (errmsg != NULL) {
                            *errmsg = "unclosed double quote";
                        }
                        token_buf_free(&tok);
                        arg_list_free(&args);
                        return NULL;
                    }
                    if (*p == '"' || *p == '\\' || *p == '$' || *p == '`') {
                        token_buf_append(&tok, *p);
                    } else {
                        token_buf_append(&tok, '\\');
                        token_buf_append(&tok, *p);
                    }
                    p++;
                } else {
                    token_buf_append(&tok, *p);
                    p++;
                }
            }
            if (*p == '\0') {
                if (errmsg != NULL) {
                    *errmsg = "unclosed double quote";
                }
                token_buf_free(&tok);
                arg_list_free(&args);
                return NULL;
            }
            p++; /* skip closing double quote */
        } else {
            in_word = true;
            token_buf_append(&tok, *p);
            p++;
        }
    }

    if (in_word) {
        arg_list_append(&args, tok.buf);
    } else {
        token_buf_free(&tok);
    }

    if (args.count == 0) {
        arg_list_free(&args);
        return NULL;
    }

    command_t *cmd = xcalloc(1, sizeof(command_t));
    cmd->argv = args.items;
    cmd->argc = (int)args.count;
    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->error_file = NULL;
    cmd->append_output = false;
    cmd->is_background = false;
    cmd->pipe_next = NULL;

    return cmd;
}

void free_command(command_t *cmd)
{
    if (cmd == NULL) {
        return;
    }

    if (cmd->argv != NULL) {
        for (int i = 0; i < cmd->argc; i++) {
            free(cmd->argv[i]);
        }
        free(cmd->argv);
    }

    free(cmd->input_file);
    free(cmd->output_file);
    free(cmd->error_file);

    if (cmd->pipe_next != NULL) {
        free_command(cmd->pipe_next);
    }

    free(cmd);
}
