/*
 * shell.c — SmartShell REPL implementation.
 *
 * Phase 1: Minimal REPL that reads lines and echoes them back.
 * Exits cleanly on EOF (Ctrl+D) or read error.
 */

#include "shell.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Maximum length of a single input line (including null terminator). */
#define SMARTSHELL_MAX_INPUT 4096

/* Shell prompt string. */
#define SMARTSHELL_PROMPT "smartshell> "

/*
 * Display the shell prompt and flush stdout.
 * Returns true on success, false if writing to stdout fails.
 */
static bool display_prompt(void)
{
    if (fputs(SMARTSHELL_PROMPT, stdout) == EOF) {
        return false;
    }
    if (fflush(stdout) == EOF) {
        return false;
    }
    return true;
}

/*
 * Read one line of input from stdin into buf (max bufsize bytes).
 * Strips the trailing newline if present.
 *
 * Returns:
 *   len > 0  — a non-empty line was read (len = strlen after stripping)
 *   0        — an empty line was read (user just pressed Enter)
 *  -1        — EOF or read error
 */
static int read_line(char *buf, size_t bufsize)
{
    if (fgets(buf, (int)bufsize, stdin) == NULL) {
        return -1;
    }

    size_t len = strlen(buf);

    /* Strip trailing newline */
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
        len--;
    }

    return (int)len;
}

int shell_run(void)
{
    char input[SMARTSHELL_MAX_INPUT];

    for (;;) {
        if (!display_prompt()) {
            break;
        }

        int len = read_line(input, sizeof(input));
        if (len < 0) {
            /* EOF (Ctrl+D) or read error — exit cleanly */
            fputs("\n", stdout);
            break;
        }
        if (len == 0) {
            /* Empty line — skip */
            continue;
        }

        /*
         * Phase 1 behavior: echo the input back.
         * This will be replaced by: parse → built-in check → execute
         * in subsequent phases.
         */
        fprintf(stdout, "%s\n", input);
    }

    return 0;
}
