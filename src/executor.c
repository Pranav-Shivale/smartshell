/*
 * executor.c — External command and pipeline execution engine for SmartShell.
 */

#include "executor.h"
#include "builtins.h"
#include "job.h"
#include "redirect.h"
#include "signals.h"
#include "utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int execute_pipeline(const command_t *cmd, const char *cmdline)
{
    if (cmd == NULL) {
        return 0;
    }

    /* Count number of commands in the pipeline */
    int num_cmds = 0;
    for (const command_t *c = cmd; c != NULL; c = c->pipe_next) {
        num_cmds++;
    }

    if (num_cmds == 0) {
        return 0;
    }

    int num_pipes = num_cmds - 1;
    int *pipefds = NULL;

    /* Create all required pipes */
    if (num_pipes > 0) {
        pipefds = xmalloc(num_pipes * 2 * sizeof(int));
        for (int i = 0; i < num_pipes; i++) {
            if (pipe(&pipefds[2 * i]) < 0) {
                fprintf(stderr, "smartshell: pipe failed: %s\n", strerror(errno));
                for (int j = 0; j < i; j++) {
                    close(pipefds[2 * j]);
                    close(pipefds[2 * j + 1]);
                }
                free(pipefds);
                return -1;
            }
        }
    }

    /* Array of child process IDs representing this pipeline job */
    pid_t *pids = xmalloc(num_cmds * sizeof(pid_t));
    const command_t *curr = cmd;

    /* Block SIGCHLD across forks and job registration to prevent race conditions */
    sigset_t prev_mask;
    signals_block_sigchld(&prev_mask);

    for (int i = 0; i < num_cmds; i++, curr = curr->pipe_next) {
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "smartshell: fork failed: %s\n", strerror(errno));

            /* Close all pipes in parent */
            if (pipefds != NULL) {
                for (int j = 0; j < 2 * num_pipes; j++) {
                    close(pipefds[j]);
                }
                free(pipefds);
            }

            signals_unblock_sigchld(&prev_mask);

            /* Wait for any children already forked */
            for (int j = 0; j < i; j++) {
                int status;
                waitpid(pids[j], &status, 0);
            }
            free(pids);
            return -1;
        }

        if (pid == 0) {
            /* Child process i */

            /* 1. Unblock signals in child */
            signals_unblock_sigchld(&prev_mask);

            /* 2. Process group assignment: leader is pids[0] (or 0 for leader child) */
            pid_t leader_pgid = (i == 0) ? 0 : pids[0];
            setpgid(0, leader_pgid);

            /* 3. Reset signal handlers to default if interactive */
            if (shell_is_interactive_mode()) {
                signals_reset_child_defaults();
            }

            /* 4. Pipe input from previous command */
            if (i > 0) {
                if (dup2(pipefds[2 * (i - 1)], STDIN_FILENO) < 0) {
                    fprintf(stderr, "smartshell: dup2 pipe in failed: %s\n", strerror(errno));
                    _exit(1);
                }
            }

            /* 5. Pipe output to next command */
            if (i < num_cmds - 1) {
                if (dup2(pipefds[2 * i + 1], STDOUT_FILENO) < 0) {
                    fprintf(stderr, "smartshell: dup2 pipe out failed: %s\n", strerror(errno));
                    _exit(1);
                }
            }

            /* 6. Close all pipe file descriptors in child */
            if (pipefds != NULL) {
                for (int j = 0; j < 2 * num_pipes; j++) {
                    close(pipefds[j]);
                }
            }

            /* 7. Set up file redirections (<, >, >>, 2>) */
            if (setup_redirections(curr) < 0) {
                _exit(1);
            }

            /* 8. Handle empty command (e.g. only redirection like '> file') */
            if (curr->argv == NULL || curr->argv[0] == NULL) {
                _exit(0);
            }

            /* 9. Built-in command within a pipeline */
            if (is_builtin(curr->argv[0])) {
                bool should_exit = false;
                int exit_code = 0;
                int ret = execute_builtin(curr, &should_exit, &exit_code);
                fflush(NULL);
                _exit(ret);
            }

            /* 10. External program execution */
            execvp(curr->argv[0], curr->argv);

            int err = errno;
            if (err == ENOENT) {
                fprintf(stderr, "smartshell: command not found: %s\n", curr->argv[0]);
                _exit(127);
            } else if (err == EACCES) {
                fprintf(stderr, "smartshell: permission denied: %s\n", curr->argv[0]);
                _exit(126);
            } else {
                fprintf(stderr, "smartshell: %s: %s\n", curr->argv[0], strerror(err));
                _exit(126);
            }
        }

        /* Parent process: record PID and set child's process group */
        pids[i] = pid;
        setpgid(pid, pids[0]);
    }

    /* Parent process: close all pipe file descriptors */
    if (pipefds != NULL) {
        for (int j = 0; j < 2 * num_pipes; j++) {
            close(pipefds[j]);
        }
        free(pipefds);
    }

    /* Register the pipeline as one logical job in the job table */
    bool is_foreground = !cmd->is_background;
    job_t *job = job_create(pids, cmd, num_cmds, cmdline, is_foreground);

    /* Transfer terminal ownership to the child process group before waiting */
    if (is_foreground && shell_is_interactive_mode()) {
        shell_give_terminal_to(pids[0]);
    }

    /* Unblock SIGCHLD now that job is registered and terminal assigned */
    signals_unblock_sigchld(&prev_mask);

    int last_status = 0;
    if (is_foreground) {
        /* Wait for foreground job to finish or stop */
        last_status = wait_for_job(job);

        /* Reclaim terminal ownership for the shell */
        if (shell_is_interactive_mode()) {
            shell_reclaim_terminal();
        }

        /* Clean up completed jobs; retain stopped jobs in the table */
        if (job != NULL) {
            if (job_is_completed(job)) {
                job_remove(job->jid);
            } else if (job_is_stopped(job)) {
                printf("\n[%d]+  Stopped  %s\n", job->jid, job->command_line);
                job->is_foreground = false;
                job->notified = true;
            }
        }
    } else {
        /* Background job: print start notification and do not wait */
        if (job != NULL) {
            printf("[%d] %d\n", job->jid, (int)pids[0]);
            fflush(stdout);
        }
    }

    free(pids);
    return last_status;
}

int execute_command(const command_t *cmd)
{
    return execute_pipeline(cmd, NULL);
}
