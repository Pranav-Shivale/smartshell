/*
 * executor.h — Command execution engine for SmartShell.
 */

#ifndef SMARTSHELL_EXECUTOR_H
#define SMARTSHELL_EXECUTOR_H

#include "parser.h"

/*
 * Execute a command or pipeline.
 *
 * Parameters:
 *   cmd     - parsed command list (pipeline)
 *   cmdline - original command line string (or NULL to reconstruct)
 *
 * A pipeline is executed as one logical job representing multiple processes
 * registered in the job table.
 *
 * Returns:
 *   - The exit status of the last command in the pipeline on normal exit.
 *   - 128 + signal_number if the last command was killed by a signal.
 *   - -1 on fork/pipe/system call failure.
 */
int execute_pipeline(const command_t *cmd, const char *cmdline);

/*
 * Wrapper for command execution without explicit command line string.
 */
int execute_command(const command_t *cmd);

#endif /* SMARTSHELL_EXECUTOR_H */
