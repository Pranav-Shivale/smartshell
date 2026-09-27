/*
 * executor.c — External command and pipeline execution engine for SmartShell.
 */

#include "executor.h"
#include "builtins.h"
#include "job.h"
#include "redirect.h"
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

            /* 1. Pipe input from previous command */
            if (i > 0) {
                if (dup2(pipefds[2 * (i - 1)], STDIN_FILENO) < 0) {
                    fprintf(stderr, "smartshell: dup2 pipe in failed: %s\n", strerror(errno));
                    _exit(1);
                }
            }

            /* 2. Pipe output to next command */
            if (i < num_cmds - 1) {
                if (dup2(pipefds[2 * i + 1], STDOUT_FILENO) < 0) {
                    fprintf(stderr, "smartshell: dup2 pipe out failed: %s\n", strerror(errno));
                    _exit(1);
                }
            }

            /* 3. Close all pipe file descriptors in child */
            if (pipefds != NULL) {
                for (int j = 0; j < 2 * num_pipes; j++) {
                    close(pipefds[j]);
                }
            }

            /* 4. Set up file redirections (<, >, >>, 2>) */
            if (setup_redirections(curr) < 0) {
                _exit(1);
            }

            /* 5. Handle empty command (e.g. only redirection like '> file') */
            if (curr->argv == NULL || curr->argv[0] == NULL) {
                _exit(0);
            }

            /* 6. Built-in command within a pipeline */
            if (is_builtin(curr->argv[0])) {
                bool should_exit = false;
                int exit_code = 0;
                int ret = execute_builtin(curr, &should_exit, &exit_code);
                fflush(NULL);
                _exit(ret);
            }

            /* 7. External program execution */
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

        pids[i] = pid;
    }

    /* Parent process: close all pipe file descriptors */
    if (pipefds != NULL) {
        for (int j = 0; j < 2 * num_pipes; j++) {
            close(pipefds[j]);
        }
        free(pipefds);
    }

    /* Register the pipeline as one logical job in the job table */
    job_t *job = job_create(pids, cmd, num_cmds, cmdline, true);
    int jid = (job != NULL) ? job->jid : -1;

    /* Parent process: wait for all child processes to complete */
    int last_status = 0;
    for (int i = 0; i < num_cmds; i++) {
        int status = 0;
        pid_t wpid;
        do {
            wpid = waitpid(pids[i], &status, 0);
        } while (wpid == -1 && errno == EINTR);

        if (wpid == -1) {
            fprintf(stderr, "smartshell: waitpid failed: %s\n", strerror(errno));
            last_status = -1;
        } else {
            job_update_process_status(pids[i], status, PROC_COMPLETED);
            if (i == num_cmds - 1) {
                if (WIFEXITED(status)) {
                    last_status = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    last_status = 128 + WTERMSIG(status);
                }
            }
        }
    }

    /* Foreground job has completed; clean up from table */
    if (jid > 0) {
        job_remove(jid);
    }

    free(pids);
    return last_status;
}

int execute_command(const command_t *cmd)
{
    return execute_pipeline(cmd, NULL);
}
