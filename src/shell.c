/*
 * shell.c — SmartShell REPL implementation.
 */

#include "shell.h"
#include "builtins.h"
#include "executor.h"
#include "job.h"
#include "parser.h"
#include "redirect.h"
#include "signals.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
    int last_status = 0;

    /* Initialize subsystems */
    job_table_init();
    signals_init();
    shell_init_terminal();

    for (;;) {
        /* Process any asynchronous child events before displaying prompt */
        signals_process_chld_events();

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

        const char *errmsg = NULL;
        command_t *cmd = parse_command(input, &errmsg);

        if (cmd == NULL) {
            if (errmsg != NULL) {
                fprintf(stderr, "smartshell: syntax error: %s\n", errmsg);
            }
            continue;
        }

        /* Check if single command and built-in */
        if (cmd->pipe_next == NULL && cmd->argv != NULL &&
            cmd->argv[0] != NULL && is_builtin(cmd->argv[0])) {
            int saved_stdin = -1;
            int saved_stdout = -1;
            int saved_stderr = -1;
            bool redir_error = false;

            if (cmd->input_file != NULL) {
                saved_stdin = dup(STDIN_FILENO);
            }
            if (cmd->output_file != NULL) {
                saved_stdout = dup(STDOUT_FILENO);
            }
            if (cmd->error_file != NULL) {
                saved_stderr = dup(STDERR_FILENO);
            }

            if (setup_redirections(cmd) < 0) {
                redir_error = true;
                last_status = 1;
            }

            if (!redir_error) {
                bool should_exit = false;
                int exit_code = 0;
                last_status = execute_builtin(cmd, &should_exit, &exit_code);
                if (should_exit) {
                    if (saved_stdin != -1) {
                        dup2(saved_stdin, STDIN_FILENO);
                        close(saved_stdin);
                    }
                    if (saved_stdout != -1) {
                        dup2(saved_stdout, STDOUT_FILENO);
                        close(saved_stdout);
                    }
                    if (saved_stderr != -1) {
                        dup2(saved_stderr, STDERR_FILENO);
                        close(saved_stderr);
                    }
                    free_command(cmd);
                    shell_restore_terminal();
                    signals_cleanup();
                    job_table_cleanup();
                    return exit_code;
                }
            }

            /* Restore saved file descriptors */
            if (saved_stdin != -1) {
                dup2(saved_stdin, STDIN_FILENO);
                close(saved_stdin);
            }
            if (saved_stdout != -1) {
                dup2(saved_stdout, STDOUT_FILENO);
                close(saved_stdout);
            }
            if (saved_stderr != -1) {
                dup2(saved_stderr, STDERR_FILENO);
                close(saved_stderr);
            }
        } else {
            last_status = execute_pipeline(cmd, input);
        }

        free_command(cmd);
    }

    shell_restore_terminal();
    signals_cleanup();
    job_table_cleanup();
    return last_status;
}
