/*
 * redirect.c — File descriptor redirection implementation for SmartShell.
 */

#include "redirect.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int setup_redirections(const command_t *cmd)
{
    if (cmd == NULL) {
        return 0;
    }

    /* Input redirection (<) */
    if (cmd->input_file != NULL) {
        int fd = open(cmd->input_file, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "smartshell: %s: %s\n", cmd->input_file, strerror(errno));
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) < 0) {
            fprintf(stderr, "smartshell: dup2 stdin failed: %s\n", strerror(errno));
            close(fd);
            return -1;
        }
        close(fd);
    }

    /* Output redirection (> or >>) */
    if (cmd->output_file != NULL) {
        int flags = O_WRONLY | O_CREAT;
        if (cmd->append_output) {
            flags |= O_APPEND;
        } else {
            flags |= O_TRUNC;
        }

        int fd = open(cmd->output_file, flags, 0666);
        if (fd < 0) {
            fprintf(stderr, "smartshell: %s: %s\n", cmd->output_file, strerror(errno));
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            fprintf(stderr, "smartshell: dup2 stdout failed: %s\n", strerror(errno));
            close(fd);
            return -1;
        }
        close(fd);
    }

    /* Error redirection (2>) */
    if (cmd->error_file != NULL) {
        int flags = O_WRONLY | O_CREAT | O_TRUNC;
        int fd = open(cmd->error_file, flags, 0666);
        if (fd < 0) {
            fprintf(stderr, "smartshell: %s: %s\n", cmd->error_file, strerror(errno));
            return -1;
        }
        if (dup2(fd, STDERR_FILENO) < 0) {
            fprintf(stderr, "smartshell: dup2 stderr failed: %s\n", strerror(errno));
            close(fd);
            return -1;
        }
        close(fd);
    }

    return 0;
}
