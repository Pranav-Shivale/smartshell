/*
 * main.c — SmartShell entry point.
 *
 * Initializes the shell and runs the REPL.
 * Future phases will add signal handler installation, job table
 * initialization, and other startup tasks here.
 */

#include "shell.h"

int main(void)
{
    return shell_run();
}
