/*
 * parser.h — Command-line parser for SmartShell.
 *
 * Tokenizes raw input into a command structure with support for:
 *   - Whitespace-delimited tokens
 *   - Single-quoted strings (no escape processing)
 *   - Double-quoted strings (with \" and \\ escapes)
 *   - Backslash escaping outside quotes
 *
 * Fields for redirection, pipes, and background are defined here
 * for future phases but are not parsed in Phase 2.
 */

#ifndef SMARTSHELL_PARSER_H
#define SMARTSHELL_PARSER_H

#include <stdbool.h>

typedef struct command {
    char **argv;              /* NULL-terminated argument list             */
    int    argc;              /* number of arguments (excluding NULL)      */
    char  *input_file;        /* stdin redirect filename  (Phase 3)       */
    char  *output_file;       /* stdout redirect filename (Phase 3)       */
    char  *error_file;        /* stderr redirect filename (Phase 3)       */
    bool   append_output;     /* true for >> , false for > (Phase 3)      */
    bool   is_background;     /* trailing & (Phase 4)                     */
    struct command *pipe_next; /* next command in pipeline (Phase 3)      */
} command_t;

/*
 * Parse a command line string into a command_t structure.
 *
 * Returns:
 *   - A newly allocated command_t on success (caller must free with
 *     free_command()).
 *   - NULL if the input is empty/whitespace-only (*errmsg is set to NULL).
 *   - NULL on parse error (*errmsg points to a static error description).
 */
command_t *parse_command(const char *line, const char **errmsg);

/*
 * Free a command_t and all associated memory.
 * Safe to call with NULL.
 */
void free_command(command_t *cmd);

#endif /* SMARTSHELL_PARSER_H */
