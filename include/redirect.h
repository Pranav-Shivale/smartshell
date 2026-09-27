/*
 * redirect.h — File descriptor redirection for SmartShell.
 */

#ifndef SMARTSHELL_REDIRECT_H
#define SMARTSHELL_REDIRECT_H

#include "parser.h"

/*
 * Set up input (<), output (> or >>), and error (2>) redirections for a command.
 *
 * Redirections:
 *   - input_file  : opened read-only and duplicated to STDIN_FILENO.
 *   - output_file : opened write-only (create + truncate or append) and
 *                   duplicated to STDOUT_FILENO.
 *   - error_file  : opened write-only (create + truncate) and
 *                   duplicated to STDERR_FILENO.
 *
 * In all cases, the opened file descriptor is closed after dup2().
 *
 * Returns:
 *    0 on success.
 *   -1 on error (an error message is printed to stderr).
 */
int setup_redirections(const command_t *cmd);

#endif /* SMARTSHELL_REDIRECT_H */
