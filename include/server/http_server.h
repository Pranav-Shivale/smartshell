/*
 * http_server.h — HTTP server lifecycle and routing for SmartShell web server.
 *
 * Wraps CivetWeb to provide a configurable, secure localhost HTTP server
 * with static file serving and REST API routing.
 */

#ifndef SMARTSHELL_SERVER_HTTP_SERVER_H
#define SMARTSHELL_SERVER_HTTP_SERVER_H

#include <stdbool.h>

/* Default server configuration */
#define HTTP_DEFAULT_PORT       8080
#define HTTP_DEFAULT_BIND_ADDR  "127.0.0.1"
#define HTTP_SERVER_VERSION     "3.0.0"

/*
 * Server configuration structure.
 */
typedef struct {
    int         port;               /* Listening port */
    const char *bind_address;       /* Bind address (default: 127.0.0.1) */
    const char *document_root;      /* Static file root (for frontend) */
    const char *auth_token;         /* Required Bearer token */
    bool        allow_root;         /* Allow running as root (danger) */
    const char *shell_path;         /* Path to shell binary (default: bin/smartshell) */
    int         pty_grace_period_sec; /* Reconnect grace period in seconds */
} server_config_t;

/*
 * Initialize default server configuration.
 */
void server_config_init(server_config_t *config);

/*
 * Parse command-line arguments into server configuration.
 *
 * Returns 0 on success, -1 on error, 1 if --help was requested.
 */
int server_config_parse_args(server_config_t *config, int argc, char **argv);

/*
 * Start the HTTP server with the given configuration.
 *
 * This function blocks until the server is stopped (e.g., by signal).
 *
 * Returns 0 on clean shutdown, non-zero on error.
 */
int server_start(const server_config_t *config);

/*
 * Request the running server to stop gracefully.
 * Safe to call from a signal handler.
 */
void server_request_stop(void);

#endif /* SMARTSHELL_SERVER_HTTP_SERVER_H */

