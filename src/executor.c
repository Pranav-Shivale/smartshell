/*
 * executor.c — External command execution engine for SmartShell.
 */

#include "executor.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int execute_command(const command_t *cmd)
{
    if (cmd == NULL || cmd->argv == NULL || cmd->argv[0] == NULL) {
        return 0;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "smartshell: fork failed: %s\n", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        /* Child process: execute the requested program */
        execvp(cmd->argv[0], cmd->argv);

        /* execvp only returns on failure */
        int err = errno;
        if (err == ENOENT) {
            fprintf(stderr, "smartshell: command not found: %s\n", cmd->argv[0]);
            _exit(127);
        } else if (err == EACCES) {
            fprintf(stderr, "smartshell: permission denied: %s\n", cmd->argv[0]);
            _exit(126);
        } else {
            fprintf(stderr, "smartshell: %s: %s\n", cmd->argv[0], strerror(err));
            _exit(126);
        }
    }

    /* Parent process: wait for foreground child to complete */
    int status = 0;
    pid_t wpid;
    do {
        wpid = waitpid(pid, &status, 0);
    } while (wpid == -1 && errno == EINTR);

    if (wpid == -1) {
        fprintf(stderr, "smartshell: waitpid failed: %s\n", strerror(errno));
        return -1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return status;
}
