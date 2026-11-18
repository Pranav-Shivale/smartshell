/*
 * main_server.c — Entry point for SmartShell web server (smartshell-server).
 *
 * This is a standalone executable separate from the SmartShell CLI.
 * It provides a localhost HTTP/WebSocket server for browser-based access
 * to SmartShell functionality.
 */

#include "server/http_server.h"
#include "server/auth.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    server_config_t config;
    server_config_init(&config);

    int parse_result = server_config_parse_args(&config, argc, argv);
    if (parse_result != 0) {
        /* parse_result == 1 means --help was shown; -1 means error */
        return (parse_result == 1) ? 0 : EXIT_FAILURE;
    }

    int result = server_start(&config);
    return (result == 0) ? 0 : EXIT_FAILURE;
}

