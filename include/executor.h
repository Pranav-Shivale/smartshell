/*
 * executor.h — External command execution engine for SmartShell.
 *
 * Executes external commands using fork() + execvp() + waitpid().
 * Phase 2 supports foreground execution of single commands only.
 */

#ifndef SMARTSHELL_EXECUTOR_H
#define SMARTSHELL_EXECUTOR_H

#include "parser.h"

/*
 * Execute an external command in a foreground child process.
 *
 * Forks a child, calls execvp(), and waits for the child to finish.
 *
 * Returns:
 *   - The child's exit status (0-255) on normal exit.
 *   - 128 + signal_number if the child was killed by a signal.
 *   - -1 if fork() or waitpid() fails (error printed to stderr).
 */
int execute_command(const command_t *cmd);

#endif /* SMARTSHELL_EXECUTOR_H */
