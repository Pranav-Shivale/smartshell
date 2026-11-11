/*
 * http_server.c — HTTP server implementation for SmartShell web server.
 *
 * Uses CivetWeb as the embedded HTTP engine. Provides REST API routing,
 * static file serving, and security middleware (auth, Host/Origin validation).
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "server/http_server.h"
#include "server/auth.h"

#include <civetweb.h>
#include <cJSON.h>

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Global server state */
static struct mg_context *g_ctx = NULL;
static volatile sig_atomic_t g_stop_requested = 0;
static server_config_t g_config;

/* ---- Utility Helpers --------------------------------------------------- */

/*
 * Send a JSON response with the given HTTP status code.
 */
static void send_json_response(struct mg_connection *conn, int status_code,
                               const char *json_body)
{
    size_t body_len = strlen(json_body);
    mg_printf(conn,
              "HTTP/1.1 %d %s\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: %zu\r\n"
              "Cache-Control: no-store\r\n"
              "X-Content-Type-Options: nosniff\r\n"
              "\r\n"
              "%s",
              status_code,
              (status_code == 200) ? "OK" :
              (status_code == 201) ? "Created" :
              (status_code == 400) ? "Bad Request" :
              (status_code == 401) ? "Unauthorized" :
              (status_code == 403) ? "Forbidden" :
              (status_code == 404) ? "Not Found" :
              (status_code == 405) ? "Method Not Allowed" :
              (status_code == 413) ? "Payload Too Large" :
              (status_code == 500) ? "Internal Server Error" :
              "Unknown",
              body_len,
              json_body);
}

/*
 * Send a JSON error response.
 */
static void send_error(struct mg_connection *conn, int status_code,
                        const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        const char *fallback = "{\"error\":\"internal error\"}";
        send_json_response(conn, 500, fallback);
        return;
    }
    cJSON_AddStringToObject(root, "error", message);
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        const char *fallback = "{\"error\":\"internal error\"}";
        send_json_response(conn, 500, fallback);
        return;
    }

    send_json_response(conn, status_code, json_str);
    free(json_str);
}

/* ---- Security Middleware ----------------------------------------------- */

/*
 * Validate Host and Origin headers. Returns true if request passes checks.
 */
static bool validate_request_headers(struct mg_connection *conn,
                                     const struct mg_request_info *ri)
{
    /* Validate Host header */
    const char *host = mg_get_header(conn, "Host");
    if (!auth_validate_host(host, g_config.port)) {
        send_error(conn, 403, "invalid Host header");
        return false;
    }

    /* Validate Origin header (for browser-originated requests) */
    const char *origin = mg_get_header(conn, "Origin");
    if (!auth_validate_origin(origin, g_config.port)) {
        send_error(conn, 403, "invalid Origin header");
        return false;
    }

    (void)ri;
    return true;
}

/*
 * Authenticate request via Bearer token. Returns true if authenticated.
 */
static bool authenticate_request(struct mg_connection *conn)
{
    const char *auth_header = mg_get_header(conn, "Authorization");
    if (auth_header == NULL) {
        send_error(conn, 401, "authentication required");
        return false;
    }

    char provided_token[AUTH_TOKEN_HEX_LEN + 1];
    if (!auth_extract_bearer(auth_header, provided_token)) {
        send_error(conn, 401, "invalid authorization format");
        return false;
    }

    if (!auth_verify_token(provided_token, g_config.auth_token)) {
        send_error(conn, 401, "invalid token");
        return false;
    }

    /* Clear token from stack */
    explicit_bzero(provided_token, sizeof(provided_token));
    return true;
}

/* ---- API Endpoint Handlers --------------------------------------------- */

/*
 * GET /api/health — Public health check (no auth required).
 */
static int handle_health(struct mg_connection *conn, void *cbdata)
{
    const struct mg_request_info *ri = mg_get_request_info(conn);
    (void)cbdata;

    if (!validate_request_headers(conn, ri)) {
        return 200; /* Already sent response */
    }

    if (strcmp(ri->request_method, "GET") != 0) {
        send_error(conn, 405, "method not allowed");
        return 200;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        send_error(conn, 500, "internal error");
        return 200;
    }

    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "version", HTTP_SERVER_VERSION);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        send_error(conn, 500, "internal error");
        return 200;
    }

    send_json_response(conn, 200, json_str);
    free(json_str);
    return 200;
}

/*
 * GET /api/auth/verify — Verify authentication token.
 */
static int handle_auth_verify(struct mg_connection *conn, void *cbdata)
{
    const struct mg_request_info *ri = mg_get_request_info(conn);
    (void)cbdata;

    if (!validate_request_headers(conn, ri)) {
        return 200;
    }

    if (strcmp(ri->request_method, "GET") != 0) {
        send_error(conn, 405, "method not allowed");
        return 200;
    }

    if (!authenticate_request(conn)) {
        return 200; /* Error already sent */
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        send_error(conn, 500, "internal error");
        return 200;
    }

    cJSON_AddBoolToObject(root, "authenticated", cJSON_True);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        send_error(conn, 500, "internal error");
        return 200;
    }

    send_json_response(conn, 200, json_str);
    free(json_str);
    return 200;
}

/* ---- Server Lifecycle -------------------------------------------------- */

void server_config_init(server_config_t *config)
{
    if (config == NULL) {
        return;
    }
    config->port = HTTP_DEFAULT_PORT;
    config->bind_address = HTTP_DEFAULT_BIND_ADDR;
    config->document_root = NULL;
    config->auth_token = NULL;
    config->allow_root = false;
}

static void print_usage(const char *progname)
{
    fprintf(stderr,
            "Usage: %s [options]\n"
            "\n"
            "SmartShell Web Server v%s\n"
            "\n"
            "Options:\n"
            "  -p, --port PORT        Listen port (default: %d)\n"
            "  -d, --document-root DIR  Static file directory\n"
            "  --danger-allow-root    Allow running as root (dangerous)\n"
            "  -h, --help             Show this help message\n"
            "\n"
            "The server binds to 127.0.0.1 (localhost only) by default.\n"
            "An access token is generated on startup and saved to\n"
            "~/.smartshell/session.token\n",
            progname, HTTP_SERVER_VERSION, HTTP_DEFAULT_PORT);
}

int server_config_parse_args(server_config_t *config, int argc, char **argv)
{
    if (config == NULL) {
        return -1;
    }

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) &&
            i + 1 < argc) {
            char *endptr = NULL;
            long port = strtol(argv[++i], &endptr, 10);
            if (*endptr != '\0' || port <= 0 || port > 65535) {
                fprintf(stderr, "Error: invalid port '%s'\n", argv[i]);
                return -1;
            }
            config->port = (int)port;
        } else if ((strcmp(argv[i], "-d") == 0 ||
                     strcmp(argv[i], "--document-root") == 0) &&
                   i + 1 < argc) {
            config->document_root = argv[++i];
        } else if (strcmp(argv[i], "--danger-allow-root") == 0) {
            config->allow_root = true;
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 1;
        } else {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return -1;
        }
    }

    return 0;
}

static void server_signal_handler(int sig)
{
    (void)sig;
    g_stop_requested = 1;
}

void server_request_stop(void)
{
    g_stop_requested = 1;
}

int server_start(const server_config_t *config)
{
    if (config == NULL) {
        return -1;
    }

    /* Copy config for use by handlers */
    g_config = *config;

    /* Refuse to run as root unless explicitly allowed */
    if (auth_is_running_as_root() && !config->allow_root) {
        fprintf(stderr,
                "Error: refusing to run as root.\n"
                "Use --danger-allow-root to override (not recommended).\n");
        return -1;
    }

    /* Generate or display authentication token */
    char token[AUTH_TOKEN_HEX_LEN + 1];
    char token_path[4096];

    if (config->auth_token != NULL) {
        strncpy(token, config->auth_token, AUTH_TOKEN_HEX_LEN);
        token[AUTH_TOKEN_HEX_LEN] = '\0';
    } else {
        if (auth_generate_token(token) < 0) {
            fprintf(stderr, "Error: failed to generate access token\n");
            return -1;
        }
    }

    g_config.auth_token = token;

    /* Save token to file */
    if (auth_get_default_token_path(token_path, sizeof(token_path)) == 0) {
        if (auth_save_token(token, token_path) < 0) {
            fprintf(stderr, "Warning: failed to save token to %s\n", token_path);
        }
    }

    /* Configure CivetWeb */
    char port_str[32];
    snprintf(port_str, sizeof(port_str), "%s:%d",
             config->bind_address ? config->bind_address : HTTP_DEFAULT_BIND_ADDR,
             config->port);

    const char *civetweb_options[] = {
        "listening_ports",       port_str,
        "num_threads",           "4",
        "request_timeout_ms",    "30000",
        "enable_keep_alive",     "yes",
        "keep_alive_timeout_ms", "5000",
        NULL
    };

    /* Initialize CivetWeb library */
    mg_init_library(0);

    /* Start CivetWeb server */
    struct mg_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));

    g_ctx = mg_start(&callbacks, NULL, civetweb_options);
    if (g_ctx == NULL) {
        fprintf(stderr, "Error: failed to start web server on %s\n", port_str);
        mg_exit_library();
        return -1;
    }

    /* Register API endpoints */
    mg_set_request_handler(g_ctx, "/api/health", handle_health, NULL);
    mg_set_request_handler(g_ctx, "/api/auth/verify", handle_auth_verify, NULL);

    /* Install signal handlers for graceful shutdown */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = server_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Print startup banner */
    fprintf(stdout,
            "\n"
            "  SmartShell Web Server v%s\n"
            "  Listening on http://%s\n"
            "\n"
            "  Access Token: %s\n"
            "  Token saved to: %s\n"
            "\n"
            "  Press Ctrl+C to stop.\n"
            "\n",
            HTTP_SERVER_VERSION,
            port_str,
            token,
            token_path);
    fflush(stdout);

    /* Block until shutdown is requested */
    while (!g_stop_requested) {
        usleep(100000); /* 100ms */
    }

    fprintf(stdout, "\nShutting down server...\n");
    fflush(stdout);

    /* Stop CivetWeb server and clean up */
    mg_stop(g_ctx);
    g_ctx = NULL;
    mg_exit_library();

    /* Clear token from memory */
    explicit_bzero(token, sizeof(token));

    fprintf(stdout, "Server stopped.\n");
    fflush(stdout);

    return 0;
}

