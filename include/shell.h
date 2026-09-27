/*
 * shell.h — SmartShell REPL (read-eval-print loop) interface.
 *
 * The REPL reads lines from stdin, processes them, and prints results
 * to stdout. In Phase 1, processing is limited to echoing the input.
 * Later phases will add parsing, built-in dispatch, and command execution.
 */

#ifndef SMARTSHELL_SHELL_H
#define SMARTSHELL_SHELL_H

/*
 * Run the SmartShell REPL.
 *
 * Displays a prompt, reads one line at a time using fgets(), processes it,
 * and repeats until EOF (Ctrl+D) or a fatal error.
 *
 * Returns 0 on clean exit, non-zero on error.
 */
int shell_run(void);

#endif /* SMARTSHELL_SHELL_H */
