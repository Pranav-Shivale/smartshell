/*
 * auth.h — Authentication and security module for SmartShell web server.
 *
 * Provides cryptographically secure token generation, constant-time
 * verification, Host/Origin header validation, and root-execution refusal.
 */

#ifndef SMARTSHELL_SERVER_AUTH_H
#define SMARTSHELL_SERVER_AUTH_H

#include <stdbool.h>
#include <stddef.h>

/* Token length in bytes (32 bytes = 64 hex characters) */
#define AUTH_TOKEN_BYTES     32
#define AUTH_TOKEN_HEX_LEN   (AUTH_TOKEN_BYTES * 2)

/* Maximum allowed request body size (1 MB) */
#define AUTH_MAX_REQUEST_BODY (1024 * 1024)

/*
 * Generate a cryptographically secure random access token.
 * Reads from /dev/urandom.
 *
 * Parameters:
 *   hex_out  — buffer of at least AUTH_TOKEN_HEX_LEN+1 bytes
 *
 * Returns 0 on success, -1 on failure.
 */
int auth_generate_token(char *hex_out);

/*
 * Save the token to a file with restrictive permissions (0600).
 *
 * Parameters:
 *   token    — hex-encoded token string
 *   filepath — path to save the token file
 *
 * Returns 0 on success, -1 on failure.
 */
int auth_save_token(const char *token, const char *filepath);

/*
 * Load a previously saved token from file.
 *
 * Parameters:
 *   filepath — path to the token file
 *   hex_out  — buffer of at least AUTH_TOKEN_HEX_LEN+1 bytes
 *
 * Returns 0 on success, -1 on failure.
 */
int auth_load_token(const char *filepath, char *hex_out);

/*
 * Constant-time comparison of two token strings.
 * Prevents timing side-channel attacks.
 *
 * Returns true if tokens are identical, false otherwise.
 */
bool auth_verify_token(const char *provided, const char *expected);

/*
 * Validate the Host header against allowed localhost values.
 *
 * Parameters:
 *   host_header    — the Host header value from the request
 *   expected_port  — the port the server is listening on
 *
 * Returns true if valid (127.0.0.1:port or localhost:port), false otherwise.
 */
bool auth_validate_host(const char *host_header, int expected_port);

/*
 * Validate the Origin header for browser-originated requests.
 *
 * Parameters:
 *   origin_header  — the Origin header value (may be NULL for non-browser requests)
 *   expected_port  — the port the server is listening on
 *
 * Returns true if valid or absent, false if present and invalid.
 */
bool auth_validate_origin(const char *origin_header, int expected_port);

/*
 * Extract the Bearer token from an Authorization header value.
 *
 * Parameters:
 *   auth_header — the full Authorization header value
 *   token_out   — buffer of at least AUTH_TOKEN_HEX_LEN+1 bytes
 *
 * Returns true if a Bearer token was extracted, false otherwise.
 */
bool auth_extract_bearer(const char *auth_header, char *token_out);

/*
 * Check if the current process is running as root (euid == 0).
 *
 * Returns true if running as root.
 */
bool auth_is_running_as_root(void);

/*
 * Get the default token file path (~/.smartshell/session.token).
 *
 * Parameters:
 *   path_out — buffer to store the path
 *   size     — size of path_out buffer
 *
 * Returns 0 on success, -1 on failure.
 */
int auth_get_default_token_path(char *path_out, size_t size);

#endif /* SMARTSHELL_SERVER_AUTH_H */

