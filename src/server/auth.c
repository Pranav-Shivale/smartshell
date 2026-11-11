/*
 * auth.c — Authentication and security implementation for SmartShell web server.
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "server/auth.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int auth_generate_token(char *hex_out)
{
    if (hex_out == NULL) {
        return -1;
    }

    unsigned char raw[AUTH_TOKEN_BYTES];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    size_t total = 0;
    while (total < sizeof(raw)) {
        ssize_t n = read(fd, raw + total, sizeof(raw) - total);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return -1;
        }
        if (n == 0) {
            close(fd);
            return -1;
        }
        total += (size_t)n;
    }
    close(fd);

    for (size_t i = 0; i < AUTH_TOKEN_BYTES; i++) {
        snprintf(hex_out + i * 2, 3, "%02x", raw[i]);
    }
    hex_out[AUTH_TOKEN_HEX_LEN] = '\0';

    /* Clear raw bytes from stack */
    explicit_bzero(raw, sizeof(raw));
    return 0;
}

int auth_save_token(const char *token, const char *filepath)
{
    if (token == NULL || filepath == NULL) {
        return -1;
    }

    /* Create parent directory if needed */
    char dir_path[4096];
    strncpy(dir_path, filepath, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';

    char *last_slash = strrchr(dir_path, '/');
    if (last_slash != NULL && last_slash != dir_path) {
        *last_slash = '\0';
        /* mkdir -p equivalent for single level */
        if (mkdir(dir_path, 0700) < 0 && errno != EEXIST) {
            return -1;
        }
    }

    /* Write token with restrictive permissions */
    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return -1;
    }

    size_t len = strlen(token);
    ssize_t written = write(fd, token, len);
    if (written < 0 || (size_t)written != len) {
        close(fd);
        return -1;
    }

    /* Write trailing newline */
    if (write(fd, "\n", 1) != 1) {
        close(fd);
        return -1;
    }

    close(fd);

    /* Ensure permissions are correct even if umask was permissive */
    if (chmod(filepath, 0600) < 0) {
        return -1;
    }

    return 0;
}

int auth_load_token(const char *filepath, char *hex_out)
{
    if (filepath == NULL || hex_out == NULL) {
        return -1;
    }

    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    char buf[AUTH_TOKEN_HEX_LEN + 2]; /* token + newline + null */
    ssize_t n = read(fd, buf, sizeof(buf));
    close(fd);

    if (n < AUTH_TOKEN_HEX_LEN) {
        return -1;
    }

    /* Strip trailing whitespace */
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) {
        n--;
    }

    if (n != AUTH_TOKEN_HEX_LEN) {
        return -1;
    }

    memcpy(hex_out, buf, AUTH_TOKEN_HEX_LEN);
    hex_out[AUTH_TOKEN_HEX_LEN] = '\0';

    return 0;
}

bool auth_verify_token(const char *provided, const char *expected)
{
    if (provided == NULL || expected == NULL) {
        return false;
    }

    size_t plen = strlen(provided);
    size_t elen = strlen(expected);

    if (plen != elen || plen != AUTH_TOKEN_HEX_LEN) {
        return false;
    }

    /*
     * Constant-time comparison: XOR all bytes and accumulate differences.
     * The loop always examines every byte regardless of mismatches.
     */
    volatile unsigned char result = 0;
    for (size_t i = 0; i < AUTH_TOKEN_HEX_LEN; i++) {
        result |= (unsigned char)((unsigned char)provided[i] ^ (unsigned char)expected[i]);
    }
    return result == 0;
}

bool auth_validate_host(const char *host_header, int expected_port)
{
    if (host_header == NULL) {
        return false;
    }

    char expected_with_port[64];
    char expected_localhost[64];

    snprintf(expected_with_port, sizeof(expected_with_port), "127.0.0.1:%d", expected_port);
    snprintf(expected_localhost, sizeof(expected_localhost), "localhost:%d", expected_port);

    if (strcmp(host_header, expected_with_port) == 0) {
        return true;
    }
    if (strcmp(host_header, expected_localhost) == 0) {
        return true;
    }

    /* Also allow without port for default port 80 */
    if (expected_port == 80) {
        if (strcmp(host_header, "127.0.0.1") == 0 ||
            strcmp(host_header, "localhost") == 0) {
            return true;
        }
    }

    return false;
}

bool auth_validate_origin(const char *origin_header, int expected_port)
{
    /* No Origin header means non-browser request — allow */
    if (origin_header == NULL || origin_header[0] == '\0') {
        return true;
    }

    char expected_origin[128];
    char expected_localhost[128];

    snprintf(expected_origin, sizeof(expected_origin),
             "http://127.0.0.1:%d", expected_port);
    snprintf(expected_localhost, sizeof(expected_localhost),
             "http://localhost:%d", expected_port);

    if (strcmp(origin_header, expected_origin) == 0) {
        return true;
    }
    if (strcmp(origin_header, expected_localhost) == 0) {
        return true;
    }

    return false;
}

bool auth_extract_bearer(const char *auth_header, char *token_out)
{
    if (auth_header == NULL || token_out == NULL) {
        return false;
    }

    /* Must start with "Bearer " (case-sensitive per RFC 6750) */
    const char *prefix = "Bearer ";
    size_t prefix_len = strlen(prefix);

    if (strncmp(auth_header, prefix, prefix_len) != 0) {
        return false;
    }

    const char *token_start = auth_header + prefix_len;

    /* Skip leading whitespace after "Bearer " */
    while (*token_start == ' ') {
        token_start++;
    }

    size_t token_len = strlen(token_start);

    /* Trim trailing whitespace */
    while (token_len > 0 && token_start[token_len - 1] == ' ') {
        token_len--;
    }

    if (token_len == 0 || token_len > AUTH_TOKEN_HEX_LEN) {
        return false;
    }

    memcpy(token_out, token_start, token_len);
    token_out[token_len] = '\0';
    return true;
}

bool auth_is_running_as_root(void)
{
    return geteuid() == 0;
}

int auth_get_default_token_path(char *path_out, size_t size)
{
    if (path_out == NULL || size == 0) {
        return -1;
    }

    const char *custom = getenv("SMARTSHELL_TOKEN_FILE");
    if (custom != NULL && custom[0] != '\0') {
        int n = snprintf(path_out, size, "%s", custom);
        if (n < 0 || (size_t)n >= size) {
            return -1;
        }
        return 0;
    }

    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        return -1;
    }

    int n = snprintf(path_out, size, "%s/.smartshell/session.token", home);
    if (n < 0 || (size_t)n >= size) {
        return -1;
    }

    return 0;
}

