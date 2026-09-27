/*
 * builtins.c — Built-in commands implementation for SmartShell.
 */

#include "builtins.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool is_builtin(const char *name)
{
    if (name == NULL) {
        return false;
    }
    return (strcmp(name, "cd") == 0 ||
            strcmp(name, "pwd") == 0 ||
            strcmp(name, "exit") == 0 ||
            strcmp(name, "quit") == 0 ||
            strcmp(name, "help") == 0);
}

static int builtin_cd(const command_t *cmd)
{
    const char *target = NULL;

    if (cmd->argc == 1 || strcmp(cmd->argv[1], "~") == 0) {
        target = getenv("HOME");
        if (target == NULL || *target == '\0') {
            fprintf(stderr, "smartshell: cd: HOME not set\n");
            return 1;
        }
    } else if (cmd->argc == 2) {
        target = cmd->argv[1];
    } else {
        fprintf(stderr, "smartshell: cd: too many arguments\n");
        return 1;
    }

    if (chdir(target) != 0) {
        fprintf(stderr, "smartshell: cd: %s: %s\n", target, strerror(errno));
        return 1;
    }

    return 0;
}

static int builtin_pwd(void)
{
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
        return 0;
    }

    fprintf(stderr, "smartshell: pwd: %s\n", strerror(errno));
    return 1;
}

static int builtin_help(void)
{
    printf("SmartShell — Built-in Commands:\n");
    printf("  cd [dir]       Change the working directory (defaults to HOME)\n");
    printf("  pwd            Print the current working directory\n");
    printf("  help           Display this help message\n");
    printf("  exit [code]    Exit the shell with optional status (0-255)\n");
    printf("  quit [code]    Alias for exit\n\n");
    printf("External commands are located via PATH and executed in the foreground.\n");
    return 0;
}

static int builtin_exit(const command_t *cmd, bool *should_exit, int *exit_code)
{
    int code = 0;

    if (cmd->argc > 1) {
        char *endptr = NULL;
        errno = 0;
        long val = strtol(cmd->argv[1], &endptr, 10);

        if (errno != 0 || endptr == cmd->argv[1] || *endptr != '\0') {
            fprintf(stderr, "smartshell: %s: %s: numeric argument required\n",
                    cmd->argv[0], cmd->argv[1]);
            if (should_exit != NULL) {
                *should_exit = true;
            }
            if (exit_code != NULL) {
                *exit_code = 2;
            }
            return 2;
        }

        if (cmd->argc > 2) {
            fprintf(stderr, "smartshell: %s: too many arguments\n", cmd->argv[0]);
            if (should_exit != NULL) {
                *should_exit = false;
            }
            return 1;
        }

        code = (int)(val & 0xFF);
    }

    if (should_exit != NULL) {
        *should_exit = true;
    }
    if (exit_code != NULL) {
        *exit_code = code;
    }

    return 0;
}

int execute_builtin(const command_t *cmd, bool *should_exit, int *exit_code)
{
    if (should_exit != NULL) {
        *should_exit = false;
    }
    if (exit_code != NULL) {
        *exit_code = 0;
    }

    if (cmd == NULL || cmd->argv == NULL || cmd->argv[0] == NULL) {
        return 0;
    }

    const char *name = cmd->argv[0];

    if (strcmp(name, "cd") == 0) {
        return builtin_cd(cmd);
    }
    if (strcmp(name, "pwd") == 0) {
        return builtin_pwd();
    }
    if (strcmp(name, "help") == 0) {
        return builtin_help();
    }
    if (strcmp(name, "exit") == 0 || strcmp(name, "quit") == 0) {
        return builtin_exit(cmd, should_exit, exit_code);
    }

    return 0;
}
