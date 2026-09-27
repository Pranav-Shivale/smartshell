/*
 * executor.h — Command execution engine for SmartShell.
 */

#ifndef SMARTSHELL_EXECUTOR_H
#define SMARTSHELL_EXECUTOR_H

#include "parser.h"

/*
 * Execute a command or pipeline.
 *
 * If cmd->pipe_next is NULL, executes a single command (with redirections).
 * If cmd->pipe_next is not NULL, executes the entire pipeline.
 *
 * Architecture note: A pipeline is executed as one logical job representing
 * multiple processes. In Phase 3, all processes run in the foreground, and
 * the parent waits for all of them to terminate.
 *
 * Returns:
 *   - The exit status of the last command in the pipeline on normal exit.
 *   - 128 + signal_number if the last command was killed by a signal.
 *   - -1 on fork/pipe/system call failure.
 */
int execute_pipeline(const command_t *cmd);

/*
 * Wrapper for command execution (calls execute_pipeline).
 */
int execute_command(const command_t *cmd);

#endif /* SMARTSHELL_EXECUTOR_H */
