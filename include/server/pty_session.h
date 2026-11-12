/*
 * pty_session.h — PTY session manager for SmartShell browser terminals.
 *
 * Provides isolated pseudo-terminal (PTY) master/slave pairs, each running an
 * instance of bin/smartshell with independent process groups, job control,
 * signal routing, and terminal window sizing.
 */

#ifndef SMARTSHELL_SERVER_PTY_SESSION_H
#define SMARTSHELL_SERVER_PTY_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>

/* Forward declaration for CivetWeb connection handle */
struct mg_connection;

/* PTY Session Manager Limits & Constants */
#define PTY_MAX_SESSIONS            16
#define PTY_SESSION_ID_HEX_LEN      32    /* 16 random bytes = 32 hex chars */
#define PTY_TICKET_HEX_LEN          64    /* 32 random bytes = 64 hex chars */
#define PTY_TICKET_EXPIRY_SEC       30    /* Single-use ticket expiry in seconds */
#define PTY_DEFAULT_GRACE_SEC       30    /* Disconnect reconnect grace period */
#define PTY_OUTPUT_BUF_SIZE         65536 /* 64KB bounded scrollback buffer */
#define PTY_READ_CHUNK_SIZE         4096  /* Read chunk size for multiplexer */
#define WS_MAX_PAYLOAD_SIZE         65536 /* 64KB max incoming WebSocket frame */

/* Session lifecycle status */
typedef enum {
    PTY_STATUS_CREATED = 0,    /* Created via REST; waiting for WebSocket connect */
    PTY_STATUS_ACTIVE,         /* WebSocket connected and actively streaming */
    PTY_STATUS_DISCONNECTED,   /* WebSocket dropped; in reconnect grace period */
    PTY_STATUS_TERMINATED      /* Child exited or session explicitly terminated */
} pty_session_status_t;

/* PTY Session representation */
typedef struct pty_session {
    char session_id[PTY_SESSION_ID_HEX_LEN + 1];
    char ws_ticket[PTY_TICKET_HEX_LEN + 1];
    time_t ticket_expires_at;
    bool ticket_used;

    pid_t child_pid;
    int master_fd;
    pty_session_status_t status;
    int exit_code;

    time_t created_at;
    time_t disconnected_at;

    /* Active WebSocket connection handle (NULL when disconnected) */
    struct mg_connection *conn;

    /* Terminal window dimensions */
    unsigned short cols;
    unsigned short rows;

    /* Buffered output for scrollback / reconnect */
    char output_buf[PTY_OUTPUT_BUF_SIZE];
    size_t output_len;

    /* Per-session synchronization */
    pthread_mutex_t lock;
    bool in_use;
} pty_session_t;

/*
 * Initialize the PTY session manager and start background reader thread.
 *
 * shell_executable: Path to bin/smartshell executable.
 * grace_period_sec: Reconnect grace period in seconds.
 *
 * Returns 0 on success, -1 on failure.
 */
int pty_manager_init(const char *shell_executable, int grace_period_sec);

/*
 * Gracefully shut down the PTY session manager.
 * Terminates all child shell processes, closes all master FDs, and joins the reader thread.
 */
void pty_manager_shutdown(void);

/*
 * Create a new isolated PTY session running SmartShell.
 *
 * cols, rows: Initial terminal size (defaults to 80x24 if 0).
 * Returns pointer to created session, or NULL on failure or max session limit.
 */
pty_session_t *pty_session_create(unsigned short cols, unsigned short rows);

/*
 * Look up a session by its unique session_id.
 * Returns pointer to session or NULL if not found.
 */
pty_session_t *pty_session_find(const char *session_id);

/*
 * Authenticate a WebSocket connection attempt.
 *
 * Validates session_id and ws_ticket using constant-time comparison,
 * verifies expiration, and marks the ticket as used (single-use).
 *
 * Returns true if authenticated, false otherwise.
 */
bool pty_session_authenticate_ticket(const char *session_id, const char *ticket);

/*
 * Generate a new single-use WebSocket ticket for reconnecting a disconnected session.
 *
 * Returns 0 on success, -1 on failure (e.g. session not found or terminated).
 */
int pty_session_issue_reconnect_ticket(const char *session_id, char *ticket_out, size_t size);

/*
 * Attach a newly established WebSocket connection to the session.
 * Flushes any buffered scrollback output to the connection.
 */
void pty_session_attach_conn(pty_session_t *session, struct mg_connection *conn);

/*
 * Detach a WebSocket connection when closed.
 * Transitions session to PTY_STATUS_DISCONNECTED and starts grace timer.
 */
void pty_session_detach_conn(pty_session_t *session);

/*
 * Write input data (keystrokes, commands, Ctrl+C, Ctrl+Z) to the PTY master FD.
 *
 * Returns number of bytes written, or -1 on error.
 */
int pty_session_write_input(pty_session_t *session, const char *data, size_t len);

/*
 * Resize the terminal window via TIOCSWINSZ.
 * Automatically sends SIGWINCH to the foreground process group.
 *
 * Returns 0 on success, -1 on error.
 */
int pty_session_resize(pty_session_t *session, unsigned short cols, unsigned short rows);

/*
 * Terminate a session explicitly.
 *
 * Returns 0 on success, -1 on error.
 */
int pty_session_terminate(pty_session_t *session);

/*
 * Delete and free a session by session ID.
 *
 * Returns 0 on success, -1 if not found.
 */
int pty_session_delete(const char *session_id);

/*
 * Copy snapshots of active/disconnected sessions for REST listing.
 *
 * Returns number of sessions copied.
 */
int pty_session_list(pty_session_t *out_list, int max_count);

/*
 * Convert session status enum to string representation.
 */
const char *pty_session_status_str(pty_session_status_t status);

#endif /* SMARTSHELL_SERVER_PTY_SESSION_H */

