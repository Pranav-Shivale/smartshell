/*
 * signals.h — Signal handling, process groups, and terminal control for SmartShell.
 */

#ifndef SMARTSHELL_SIGNALS_H
#define SMARTSHELL_SIGNALS_H

#include "job.h"

#include <stdbool.h>
#include <signal.h>
#include <sys/types.h>
#include <termios.h>

/*
 * Initialize the signal handling and self-pipe subsystem.
 */
void signals_init(void);

/*
 * Clean up signal handlers and self-pipe resources.
 */
void signals_cleanup(void);

/*
 * Check if the shell is running in an interactive terminal session.
 */
bool shell_is_interactive_mode(void);

/*
 * Initialize terminal control for interactive shell:
 * - ensures shell is in the foreground
 * - sets shell process group
 * - saves terminal attributes
 * - configures signal ignores for shell process
 */
void shell_init_terminal(void);

/*
 * Restore terminal attributes and foreground process group on shell exit.
 */
void shell_restore_terminal(void);

/*
 * Transfer terminal ownership to the given process group.
 * Safe no-op in non-interactive mode.
 */
void shell_give_terminal_to(pid_t pgid);

/*
 * Reclaim terminal ownership for the shell process group.
 * Safe no-op in non-interactive mode.
 */
void shell_reclaim_terminal(void);

/*
 * Return the shell's process group ID.
 */
pid_t shell_get_pgid(void);

/*
 * Return the terminal file descriptor (STDIN_FILENO).
 */
int shell_get_terminal_fd(void);

/*
 * Reset signals to default (SIG_DFL) in child processes before exec.
 */
void signals_reset_child_defaults(void);

/*
 * Block SIGCHLD (e.g. before fork) to prevent race conditions.
 */
void signals_block_sigchld(sigset_t *prev_mask);

/*
 * Restore previous signal mask (e.g. after job creation or in child).
 */
void signals_unblock_sigchld(const sigset_t *prev_mask);

/*
 * Read file descriptor of the self-pipe for polling in wait loops.
 */
int signals_get_self_pipe_read_fd(void);

/*
 * Drain self-pipe and process all pending child process events.
 * Updates process and job states in the job table.
 */
void signals_process_chld_events(void);

/*
 * Wait for a foreground job until it completes or stops.
 * Returns the exit status of the job.
 */
int wait_for_job(job_t *job);

#endif /* SMARTSHELL_SIGNALS_H */
