/*
 * builtins.h — Built-in command interface for SmartShell.
 *
 * Phase 2 built-ins: cd, pwd, exit, quit, help.
 */

#ifndef SMARTSHELL_BUILTINS_H
#define SMARTSHELL_BUILTINS_H

#include <stdbool.h>

#include "parser.h"

/*
 * Check whether a command name corresponds to a built-in command.
 * Returns true if name matches a known built-in.
 */
bool is_builtin(const char *name);

/*
 * Execute a built-in command.
 *
 * Parameters:
 *   cmd         — parsed command (argv[0] must be a built-in name)
 *   should_exit — set to true if the shell should terminate (exit/quit)
 *   exit_code   — set to the requested shell exit code when *should_exit
 *                 is true (only meaningful if *should_exit == true)
 *
 * Returns the built-in's own exit status (0 = success, non-zero = error).
 */
int execute_builtin(const command_t *cmd, bool *should_exit, int *exit_code);

#endif /* SMARTSHELL_BUILTINS_H */
