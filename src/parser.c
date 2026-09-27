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

/* Token types produced by the lexer. */
typedef enum {
    TOK_WORD,          /* regular argument or filename */
    TOK_PIPE,          /* | */
    TOK_REDIR_IN,      /* < */
    TOK_REDIR_OUT,     /* > */
    TOK_REDIR_APPEND,  /* >> */
    TOK_REDIR_ERR,     /* 2> */
    TOK_EOF            /* end of input line */
} token_type_t;

typedef struct {
    token_type_t type;
    char        *text; /* non-NULL only for TOK_WORD */
} token_t;

typedef struct {
    token_t *tokens;
    size_t   count;
    size_t   cap;
} token_array_t;

static void token_array_init(token_array_t *ta)
{
    ta->cap = 16;
    ta->count = 0;
    ta->tokens = xmalloc(ta->cap * sizeof(token_t));
}

static void token_array_append(token_array_t *ta, token_type_t type, char *text)
{
    if (ta->count >= ta->cap) {
        ta->cap *= 2;
        ta->tokens = xrealloc(ta->tokens, ta->cap * sizeof(token_t));
    }
    ta->tokens[ta->count].type = type;
    ta->tokens[ta->count].text = text;
    ta->count++;
}

static void token_array_free(token_array_t *ta)
{
    for (size_t i = 0; i < ta->count; i++) {
        free(ta->tokens[i].text);
    }
    free(ta->tokens);
    ta->tokens = NULL;
    ta->count = 0;
    ta->cap = 0;
}

/*
 * Lexer: tokenizes an input line into an array of token_t.
 * Returns true on success, false on syntax error (*errmsg set).
 */
static bool tokenize(const char *line, token_array_t *ta, const char **errmsg)
{
    const char *p = line;

    while (*p != '\0') {
        /* 1. Skip whitespace */
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        /* 2. Check for 2> */
        if (*p == '2' && *(p + 1) == '>') {
            token_array_append(ta, TOK_REDIR_ERR, NULL);
            p += 2;
            continue;
        }

        /* 3. Check for >> */
        if (*p == '>' && *(p + 1) == '>') {
            token_array_append(ta, TOK_REDIR_APPEND, NULL);
            p += 2;
            continue;
        }

        /* 4. Check for > */
        if (*p == '>') {
            token_array_append(ta, TOK_REDIR_OUT, NULL);
            p++;
            continue;
        }

        /* 5. Check for < */
        if (*p == '<') {
            token_array_append(ta, TOK_REDIR_IN, NULL);
            p++;
            continue;
        }

        /* 6. Check for | */
        if (*p == '|') {
            token_array_append(ta, TOK_PIPE, NULL);
            p++;
            continue;
        }

        /* 7. Read a word (argument or filename) */
        token_buf_t buf;
        token_buf_init(&buf);
        bool in_word = false;

        while (*p != '\0') {
            if (isspace((unsigned char)*p)) {
                break;
            }
            if (*p == '|' || *p == '<' || *p == '>') {
                break;
            }
            if (*p == '2' && *(p + 1) == '>') {
                break;
            }

            if (*p == '\\') {
                in_word = true;
                p++;
                if (*p == '\0') {
                    if (errmsg != NULL) {
                        *errmsg = "trailing backslash";
                    }
                    token_buf_free(&buf);
                    return false;
                }
                token_buf_append(&buf, *p);
                p++;
            } else if (*p == '\'') {
                in_word = true;
                p++;
                while (*p != '\0' && *p != '\'') {
                    token_buf_append(&buf, *p);
                    p++;
                }
                if (*p == '\0') {
                    if (errmsg != NULL) {
                        *errmsg = "unclosed single quote";
                    }
                    token_buf_free(&buf);
                    return false;
                }
                p++; /* skip closing quote */
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
                            token_buf_free(&buf);
                            return false;
                        }
                        if (*p == '"' || *p == '\\' || *p == '$' || *p == '`') {
                            token_buf_append(&buf, *p);
                        } else {
                            token_buf_append(&buf, '\\');
                            token_buf_append(&buf, *p);
                        }
                        p++;
                    } else {
                        token_buf_append(&buf, *p);
                        p++;
                    }
                }
                if (*p == '\0') {
                    if (errmsg != NULL) {
                        *errmsg = "unclosed double quote";
                    }
                    token_buf_free(&buf);
                    return false;
                }
                p++; /* skip closing quote */
            } else {
                in_word = true;
                token_buf_append(&buf, *p);
                p++;
            }
        }

        if (in_word) {
            token_array_append(ta, TOK_WORD, buf.buf);
        } else {
            token_buf_free(&buf);
        }
    }

    token_array_append(ta, TOK_EOF, NULL);
    return true;
}

command_t *parse_command(const char *line, const char **errmsg)
{
    if (errmsg != NULL) {
        *errmsg = NULL;
    }

    if (line == NULL) {
        return NULL;
    }

    token_array_t ta;
    token_array_init(&ta);

    if (!tokenize(line, &ta, errmsg)) {
        token_array_free(&ta);
        return NULL;
    }

    if (ta.tokens[0].type == TOK_EOF) {
        /* Empty or whitespace-only input */
        token_array_free(&ta);
        return NULL;
    }

    if (ta.tokens[0].type == TOK_PIPE) {
        if (errmsg != NULL) {
            *errmsg = "unexpected token '|'";
        }
        token_array_free(&ta);
        return NULL;
    }

    command_t *head = NULL;
    command_t *curr = NULL;
    size_t i = 0;

    while (i < ta.count && ta.tokens[i].type != TOK_EOF) {
        command_t *cmd = xcalloc(1, sizeof(command_t));
        size_t argv_cap = 8;
        cmd->argv = xmalloc(argv_cap * sizeof(char *));
        cmd->argv[0] = NULL;
        cmd->argc = 0;

        bool stage_done = false;

        while (i < ta.count && !stage_done) {
            token_t *tok = &ta.tokens[i];

            switch (tok->type) {
                case TOK_WORD:
                    if (cmd->argc + 2 > (int)argv_cap) {
                        argv_cap *= 2;
                        cmd->argv = xrealloc(cmd->argv, argv_cap * sizeof(char *));
                    }
                    cmd->argv[cmd->argc++] = xstrdup(tok->text);
                    cmd->argv[cmd->argc] = NULL;
                    i++;
                    break;

                case TOK_REDIR_IN:
                case TOK_REDIR_OUT:
                case TOK_REDIR_APPEND:
                case TOK_REDIR_ERR: {
                    token_type_t rtype = tok->type;
                    i++;
                    if (i >= ta.count || ta.tokens[i].type != TOK_WORD) {
                        if (errmsg != NULL) {
                            if (i < ta.count && ta.tokens[i].type == TOK_PIPE) {
                                *errmsg = "unexpected token '|'";
                            } else {
                                *errmsg = "expected filename after redirection";
                            }
                        }
                        free_command(cmd);
                        free_command(head);
                        token_array_free(&ta);
                        return NULL;
                    }

                    char *filename = xstrdup(ta.tokens[i].text);
                    i++;

                    if (rtype == TOK_REDIR_IN) {
                        free(cmd->input_file);
                        cmd->input_file = filename;
                    } else if (rtype == TOK_REDIR_OUT) {
                        free(cmd->output_file);
                        cmd->output_file = filename;
                        cmd->append_output = false;
                    } else if (rtype == TOK_REDIR_APPEND) {
                        free(cmd->output_file);
                        cmd->output_file = filename;
                        cmd->append_output = true;
                    } else if (rtype == TOK_REDIR_ERR) {
                        free(cmd->error_file);
                        cmd->error_file = filename;
                    }
                    break;
                }

                case TOK_PIPE:
                    i++; /* consume pipe */
                    stage_done = true;
                    break;

                case TOK_EOF:
                    stage_done = true;
                    break;
            }
        }

        /* Check if stage was completely empty */
        if (cmd->argc == 0 && cmd->input_file == NULL &&
            cmd->output_file == NULL && cmd->error_file == NULL) {
            if (errmsg != NULL) {
                *errmsg = "unexpected token '|'";
            }
            free_command(cmd);
            free_command(head);
            token_array_free(&ta);
            return NULL;
        }

        if (head == NULL) {
            head = cmd;
        } else {
            curr->pipe_next = cmd;
        }
        curr = cmd;

        /* If we stopped because of a pipe, check if the next token is EOF */
        if (i < ta.count && ta.tokens[i - 1].type == TOK_PIPE && ta.tokens[i].type == TOK_EOF) {
            if (errmsg != NULL) {
                *errmsg = "unexpected token '|'";
            }
            free_command(head);
            token_array_free(&ta);
            return NULL;
        }
    }

    token_array_free(&ta);
    return head;
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
