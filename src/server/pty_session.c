/*
 * pty_session.c — Implementation of isolated PTY session management.
 *
 * Each browser session runs its own pseudo-terminal master/slave pair and
 * an independent instance of bin/smartshell with proper job-control isolation.
 */

#define _GNU_SOURCE
#include "server/pty_session.h"
#include "server/auth.h"
#include "civetweb.h"
#include "cJSON.h"

#include <pty.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poll.h>
#include <pthread.h>
#include <dirent.h>
#include <stdatomic.h>
#include <sys/syscall.h>

/* Global PTY Manager State */
static pty_session_t    g_sessions[PTY_MAX_SESSIONS];
static pthread_mutex_t  g_manager_lock = PTHREAD_MUTEX_INITIALIZER;
static char             g_shell_path[1024] = "bin/smartshell";
static int              g_grace_period_sec = PTY_DEFAULT_GRACE_SEC;
static bool             g_initialized = false;
static volatile bool    g_reader_running = false;
static pthread_t        g_reader_thread;
static int              g_wakeup_pipe[2] = {-1, -1};

/* Forward declarations */
static void *pty_reader_thread_func(void *arg);
static void pty_session_do_terminate_locked(pty_session_t *s);
static void pty_session_reap_child_locked(pty_session_t *s, bool send_signals);
static int pty_ws_send_output(struct mg_connection *conn, const char *data, size_t len);

const char *pty_session_status_str(pty_session_status_t status)
{
    switch (status) {
        case PTY_STATUS_CREATED:      return "created";
        case PTY_STATUS_ACTIVE:       return "active";
        case PTY_STATUS_DISCONNECTED: return "disconnected";
        case PTY_STATUS_TERMINATED:   return "terminated";
        default:                      return "unknown";
    }
}

static void wakeup_reader_thread(void)
{
    if (g_wakeup_pipe[1] >= 0) {
        char b = 1;
        ssize_t w = write(g_wakeup_pipe[1], &b, 1);
        (void)w;
    }
}

int pty_manager_init(const char *shell_executable, int grace_period_sec)
{
    pthread_mutex_lock(&g_manager_lock);

    if (g_initialized) {
        pthread_mutex_unlock(&g_manager_lock);
        return 0;
    }

    if (shell_executable != NULL && shell_executable[0] != '\0') {
        strncpy(g_shell_path, shell_executable, sizeof(g_shell_path) - 1);
        g_shell_path[sizeof(g_shell_path) - 1] = '\0';
    } else {
        strncpy(g_shell_path, "bin/smartshell", sizeof(g_shell_path) - 1);
    }

    g_grace_period_sec = (grace_period_sec >= 0) ? grace_period_sec : PTY_DEFAULT_GRACE_SEC;

    /* Initialize session slots */
    for (int i = 0; i < PTY_MAX_SESSIONS; i++) {
        memset(&g_sessions[i], 0, sizeof(pty_session_t));
        g_sessions[i].master_fd = -1;
        g_sessions[i].child_pid = -1;
        g_sessions[i].status = PTY_STATUS_TERMINATED;
        pthread_mutex_init(&g_sessions[i].lock, NULL);
    }

    /* Create non-blocking self-pipe for waking up multiplexer */
    if (pipe(g_wakeup_pipe) < 0) {
        pthread_mutex_unlock(&g_manager_lock);
        return -1;
    }

    int flags;
    flags = fcntl(g_wakeup_pipe[0], F_GETFL, 0);
    if (flags >= 0) fcntl(g_wakeup_pipe[0], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(g_wakeup_pipe[1], F_GETFL, 0);
    if (flags >= 0) fcntl(g_wakeup_pipe[1], F_SETFL, flags | O_NONBLOCK);

    /* Launch background multiplexer reader thread */
    g_reader_running = true;
    if (pthread_create(&g_reader_thread, NULL, pty_reader_thread_func, NULL) != 0) {
        g_reader_running = false;
        close(g_wakeup_pipe[0]);
        close(g_wakeup_pipe[1]);
        g_wakeup_pipe[0] = -1;
        g_wakeup_pipe[1] = -1;
        pthread_mutex_unlock(&g_manager_lock);
        return -1;
    }

    g_initialized = true;
    pthread_mutex_unlock(&g_manager_lock);
    return 0;
}

void pty_manager_shutdown(void)
{
    pthread_mutex_lock(&g_manager_lock);
    if (!g_initialized) {
        pthread_mutex_unlock(&g_manager_lock);
        return;
    }

    g_reader_running = false;
    wakeup_reader_thread();
    pthread_mutex_unlock(&g_manager_lock);

    /* Wait for reader thread to finish */
    pthread_join(g_reader_thread, NULL);

    pthread_mutex_lock(&g_manager_lock);

    /* Terminate and reap all active sessions */
    for (int i = 0; i < PTY_MAX_SESSIONS; i++) {
        pty_session_t *s = &g_sessions[i];
        if (s->in_use) {
            pthread_mutex_lock(&s->lock);
            pty_session_do_terminate_locked(s);
            s->in_use = false;
            pthread_mutex_unlock(&s->lock);
            pthread_mutex_destroy(&s->lock);
        }
    }

    if (g_wakeup_pipe[0] >= 0) {
        close(g_wakeup_pipe[0]);
        close(g_wakeup_pipe[1]);
        g_wakeup_pipe[0] = -1;
        g_wakeup_pipe[1] = -1;
    }

    g_initialized = false;
    pthread_mutex_unlock(&g_manager_lock);
}

typedef enum {
    WS_STATE_FREE = 0,
    WS_STATE_UNCONFIRMED,   /* ws_connect_handler accepted; awaiting ws_ready_handler */
    WS_STATE_PENDING_AUTH,  /* ws_ready_handler fired; awaiting first auth message */
    WS_STATE_AUTHENTICATED, /* Auth succeeded; bound to a PTY session */
    WS_STATE_CLOSING,       /* close_requested set; worker thread will exit */
    WS_STATE_RELEASING      /* ws_close_handler + connection_close fired; awaiting refcount 0 */
} ws_state_t;

typedef struct {
    ws_state_t state;
    uint32_t generation;
    atomic_int ref_count;
    atomic_bool close_requested;

    uint64_t lease_id;
    struct mg_connection *conn;

    /* Timeout deadlines (CLOCK_MONOTONIC) */
    struct timespec handshake_deadline;  /* Valid in UNCONFIRMED */
    struct timespec auth_deadline;       /* Valid in PENDING_AUTH */

    char session_id[PTY_SESSION_ID_HEX_LEN + 1];
} ws_registry_entry_t;

typedef struct {
    pthread_mutex_t lock;
    int live_ws_count;
    int pending_auth_count;
    uint32_t next_generation;
    ws_registry_entry_t entries[MAX_LIVE_WS];
} ws_registry_t;

static ws_registry_t g_ws_registry = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .live_ws_count = 0,
    .pending_auth_count = 0,
    .next_generation = 0
};

static inline bool timespec_ge(struct timespec a, struct timespec b)
{
    if (a.tv_sec > b.tv_sec) return true;
    if (a.tv_sec == b.tv_sec && a.tv_nsec >= b.tv_nsec) return true;
    return false;
}

static void pty_admission_release_ref(uint32_t slot, uint32_t generation)
{
    if (slot >= MAX_LIVE_WS) return;

    ws_registry_entry_t *entry = &g_ws_registry.entries[slot];
    int prev = atomic_fetch_sub(&entry->ref_count, 1);

    if (prev == 1) {
        /* Last reference dropped. Reclaim slot. */
        pthread_mutex_lock(&g_ws_registry.lock);
        if (entry->generation == generation &&
            entry->state == WS_STATE_RELEASING) {
            entry->state = WS_STATE_FREE;
            entry->lease_id = 0;
            entry->conn = NULL;
            entry->session_id[0] = '\0';
            atomic_store(&entry->close_requested, false);
        }
        pthread_mutex_unlock(&g_ws_registry.lock);
    }
}

int pty_admission_reserve(struct mg_connection *conn, ws_conn_ctx_t *ctx)
{
    if (conn == NULL || ctx == NULL) return -1;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    pthread_mutex_lock(&g_ws_registry.lock);

    if (g_ws_registry.live_ws_count >= MAX_LIVE_WS ||
        g_ws_registry.pending_auth_count >= MAX_PENDING_WS) {
        pthread_mutex_unlock(&g_ws_registry.lock);
        return -1;
    }

    int slot = -1;
    for (int i = 0; i < MAX_LIVE_WS; i++) {
        if (g_ws_registry.entries[i].state == WS_STATE_FREE &&
            atomic_load(&g_ws_registry.entries[i].ref_count) == 0) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        pthread_mutex_unlock(&g_ws_registry.lock);
        return -1;
    }

    ws_registry_entry_t *entry = &g_ws_registry.entries[slot];

    if (++g_ws_registry.next_generation == 0)
        g_ws_registry.next_generation = 1;

    entry->generation = g_ws_registry.next_generation;
    entry->lease_id = ((uint64_t)entry->generation << 32) | (uint32_t)slot;
    entry->conn = conn;
    entry->state = WS_STATE_UNCONFIRMED;
    entry->session_id[0] = '\0';
    entry->handshake_deadline.tv_sec = now.tv_sec + 5;
    entry->handshake_deadline.tv_nsec = now.tv_nsec;
    atomic_store(&entry->ref_count, 1);         /* Connection lifetime reference */
    atomic_store(&entry->close_requested, false);

    g_ws_registry.live_ws_count++;
    g_ws_registry.pending_auth_count++;

    ctx->lease_id = entry->lease_id;

    pthread_mutex_unlock(&g_ws_registry.lock);
    return 0;
}

int pty_admission_confirm(uint64_t lease_id)
{
    uint32_t slot = (uint32_t)(lease_id & 0xFFFFFFFF);
    uint32_t gen  = (uint32_t)(lease_id >> 32);
    if (slot >= MAX_LIVE_WS) return -1;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    pthread_mutex_lock(&g_ws_registry.lock);
    ws_registry_entry_t *entry = &g_ws_registry.entries[slot];

    if (entry->generation != gen || entry->state != WS_STATE_UNCONFIRMED) {
        pthread_mutex_unlock(&g_ws_registry.lock);
        return -1;
    }

    entry->state = WS_STATE_PENDING_AUTH;
    entry->auth_deadline.tv_sec = now.tv_sec + WS_AUTH_TIMEOUT_SEC;
    entry->auth_deadline.tv_nsec = now.tv_nsec;

    pthread_mutex_unlock(&g_ws_registry.lock);
    return 0;
}

int pty_admission_authenticate(uint64_t lease_id, const char *session_id)
{
    uint32_t slot = (uint32_t)(lease_id & 0xFFFFFFFF);
    uint32_t gen  = (uint32_t)(lease_id >> 32);
    if (slot >= MAX_LIVE_WS || session_id == NULL) return -1;

    pthread_mutex_lock(&g_ws_registry.lock);
    ws_registry_entry_t *entry = &g_ws_registry.entries[slot];

    if (entry->generation != gen || entry->state != WS_STATE_PENDING_AUTH) {
        pthread_mutex_unlock(&g_ws_registry.lock);
        return -1;
    }

    entry->state = WS_STATE_AUTHENTICATED;
    strncpy(entry->session_id, session_id, sizeof(entry->session_id) - 1);
    entry->session_id[sizeof(entry->session_id) - 1] = '\0';
    g_ws_registry.pending_auth_count--;

    pthread_mutex_unlock(&g_ws_registry.lock);
    return 0;
}

void pty_admission_request_close(uint64_t lease_id)
{
    uint32_t slot = (uint32_t)(lease_id & 0xFFFFFFFF);
    uint32_t gen  = (uint32_t)(lease_id >> 32);
    if (slot >= MAX_LIVE_WS) return;

    pthread_mutex_lock(&g_ws_registry.lock);
    ws_registry_entry_t *entry = &g_ws_registry.entries[slot];

    if (entry->generation != gen ||
        entry->state == WS_STATE_FREE ||
        entry->state == WS_STATE_RELEASING) {
        pthread_mutex_unlock(&g_ws_registry.lock);
        return;
    }

    atomic_store(&entry->close_requested, true);

    if (entry->state == WS_STATE_UNCONFIRMED ||
        entry->state == WS_STATE_PENDING_AUTH) {
        g_ws_registry.pending_auth_count--;
        entry->state = WS_STATE_CLOSING;
    } else if (entry->state == WS_STATE_AUTHENTICATED) {
        entry->state = WS_STATE_CLOSING;
    }

    pthread_mutex_unlock(&g_ws_registry.lock);
}

bool pty_admission_is_close_requested(uint64_t lease_id)
{
    if (lease_id == 0) return false;
    uint32_t slot = (uint32_t)(lease_id & 0xFFFFFFFF);
    uint32_t gen  = (uint32_t)(lease_id >> 32);
    if (slot >= MAX_LIVE_WS) return false;

    ws_registry_entry_t *entry = &g_ws_registry.entries[slot];
    if (entry->generation != gen) return false;
    return atomic_load(&entry->close_requested);
}

uint64_t pty_admission_find_lease_by_conn(const struct mg_connection *conn)
{
    if (conn == NULL) return 0;
    uint64_t lease_id = 0;
    pthread_mutex_lock(&g_ws_registry.lock);
    for (int i = 0; i < MAX_LIVE_WS; i++) {
        if (g_ws_registry.entries[i].conn == conn &&
            g_ws_registry.entries[i].state != WS_STATE_FREE &&
            g_ws_registry.entries[i].state != WS_STATE_RELEASING) {
            lease_id = g_ws_registry.entries[i].lease_id;
            break;
        }
    }
    pthread_mutex_unlock(&g_ws_registry.lock);
    return lease_id;
}

void pty_admission_release_lease(uint64_t lease_id)
{
    if (lease_id == 0) return;
    uint32_t slot = (uint32_t)(lease_id & 0xFFFFFFFF);
    uint32_t gen  = (uint32_t)(lease_id >> 32);

    if (slot < MAX_LIVE_WS) {
        pthread_mutex_lock(&g_ws_registry.lock);
        ws_registry_entry_t *entry = &g_ws_registry.entries[slot];

        if (entry->generation == gen &&
            entry->state != WS_STATE_FREE &&
            entry->state != WS_STATE_RELEASING) {

            if (entry->state == WS_STATE_UNCONFIRMED ||
                entry->state == WS_STATE_PENDING_AUTH) {
                g_ws_registry.pending_auth_count--;
            }

            g_ws_registry.live_ws_count--;
            entry->conn = NULL;
            entry->state = WS_STATE_RELEASING;
        }

        pthread_mutex_unlock(&g_ws_registry.lock);

        pty_admission_release_ref(slot, gen);
    }
}

void pty_admission_on_connection_close(const struct mg_connection *conn)
{
    if (conn == NULL) return;

    ws_conn_ctx_t *ctx = (ws_conn_ctx_t *)mg_get_user_connection_data(conn);
    if (ctx == NULL) return;

    if (ctx->lease_id != 0) {
        pty_admission_release_lease(ctx->lease_id);
    }

    free(ctx);
}

void pty_admission_check_timeouts(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    uint64_t expired[MAX_LIVE_WS];
    int expired_count = 0;

    pthread_mutex_lock(&g_ws_registry.lock);
    for (int i = 0; i < MAX_LIVE_WS; i++) {
        ws_registry_entry_t *entry = &g_ws_registry.entries[i];

        if (entry->state == WS_STATE_UNCONFIRMED) {
            if (timespec_ge(now, entry->handshake_deadline)) {
                expired[expired_count++] = entry->lease_id;
            }
        } else if (entry->state == WS_STATE_PENDING_AUTH) {
            if (timespec_ge(now, entry->auth_deadline)) {
                expired[expired_count++] = entry->lease_id;
            }
        }
    }
    pthread_mutex_unlock(&g_ws_registry.lock);

    for (int i = 0; i < expired_count; i++) {
        pty_admission_request_close(expired[i]);
    }
}

int pty_admission_get_counts(int *live_count, int *pending_count)
{
    pthread_mutex_lock(&g_ws_registry.lock);
    if (live_count) *live_count = g_ws_registry.live_ws_count;
    if (pending_count) *pending_count = g_ws_registry.pending_auth_count;
    pthread_mutex_unlock(&g_ws_registry.lock);
    return 0;
}

void pty_session_add_pending_auth(struct mg_connection *conn)
{
    (void)conn;
}

void pty_session_remove_pending_auth(const struct mg_connection *conn)
{
    (void)conn;
}

void pty_session_check_auth_timeouts(void)
{
    pty_admission_check_timeouts();
}

static void pty_session_append_output(pty_session_t *s, const char *data, size_t len)
{
    if (len == 0 || data == NULL) return;

    /* Cap to buffer capacity */
    if (len >= sizeof(s->output_buf)) {
        data += (len - (sizeof(s->output_buf) - 1));
        len = sizeof(s->output_buf) - 1;
    }

    if (s->output_len + len >= sizeof(s->output_buf)) {
        size_t overflow = (s->output_len + len) - (sizeof(s->output_buf) - 1);
        size_t retain = s->output_len - overflow;
        memmove(s->output_buf, s->output_buf + overflow, retain);
        s->output_len = retain;
    }

    memcpy(s->output_buf + s->output_len, data, len);
    s->output_len += len;
    s->output_buf[s->output_len] = '\0';
}

static int pty_ws_send_output(struct mg_connection *conn, const char *data, size_t len)
{
    if (conn == NULL || data == NULL || len == 0) return 0;

    /* Build JSON message: {"type": "output", "data": "..."} */
    cJSON *msg = cJSON_CreateObject();
    if (msg == NULL) return -1;

    cJSON_AddStringToObject(msg, "type", "output");

    /*
     * Check if data contains embedded nulls.
     * If so, replace with spaces for safe cJSON null-terminated string handling.
     */
    bool has_null = false;
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '\0') {
            has_null = true;
            break;
        }
    }

    if (!has_null) {
        cJSON_AddStringToObject(msg, "data", data);
    } else {
        char *sanitized = malloc(len + 1);
        if (sanitized) {
            for (size_t i = 0; i < len; i++) {
                sanitized[i] = (data[i] == '\0') ? ' ' : data[i];
            }
            sanitized[len] = '\0';
            cJSON_AddStringToObject(msg, "data", sanitized);
            free(sanitized);
        }
    }

    char *json_str = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);

    int ret = -1;
    if (json_str != NULL) {
        ret = mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT, json_str, strlen(json_str));
        free(json_str);
    }
    return ret;
}

static void pty_ws_send_exit(struct mg_connection *conn, int exit_code)
{
    if (conn == NULL) return;

    cJSON *msg = cJSON_CreateObject();
    if (msg == NULL) return;

    cJSON_AddStringToObject(msg, "type", "exit");
    cJSON_AddNumberToObject(msg, "code", exit_code);

    char *json_str = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);

    if (json_str != NULL) {
        mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT, json_str, strlen(json_str));
        free(json_str);
    }
}

#if defined(__linux__) && defined(__NR_pidfd_open) && defined(__NR_pidfd_send_signal)
typedef enum {
    PIDFD_RESULT_SENT,         /* Signal delivered to the correct process */
    PIDFD_RESULT_ALREADY_DEAD, /* Process already exited (ESRCH) */
    PIDFD_RESULT_SENT_UNSAFE,  /* Signal sent via kill(); PID reuse possible */
    PIDFD_RESULT_FAILED        /* Signal could not be sent */
} pidfd_result_t;

static pidfd_result_t safe_kill_process(pid_t pid, int sig)
{
    int pfd = (int)syscall(__NR_pidfd_open, pid, 0);
    if (pfd >= 0) {
        int ret = (int)syscall(__NR_pidfd_send_signal, pfd, sig, NULL, 0);
        int saved_errno = errno;
        close(pfd);
        if (ret == 0) return PIDFD_RESULT_SENT;
        if (saved_errno == ESRCH) return PIDFD_RESULT_ALREADY_DEAD;
        return PIDFD_RESULT_FAILED;
    }

    switch (errno) {
    case ESRCH:
        return PIDFD_RESULT_ALREADY_DEAD;

    case ENOSYS:
        /* Kernel does not support pidfd_open (pre-5.3 compatibility fallback) */
        break;

    case EMFILE:
    case ENFILE:
        fprintf(stderr,
                "WARNING: pidfd_open(%d) failed: fd limit (%s); "
                "skipping signal %d to avoid PID reuse risk\n",
                (int)pid, strerror(errno), sig);
        return PIDFD_RESULT_FAILED;

    case EPERM:
        fprintf(stderr,
                "WARNING: pidfd_open(%d) failed: EPERM; skipping signal %d\n",
                (int)pid, sig);
        return PIDFD_RESULT_FAILED;

    default:
        fprintf(stderr,
                "WARNING: pidfd_open(%d) failed: %s; skipping signal %d\n",
                (int)pid, strerror(errno), sig);
        return PIDFD_RESULT_FAILED;
    }

    if (kill(pid, sig) == 0) return PIDFD_RESULT_SENT_UNSAFE;
    if (errno == ESRCH) return PIDFD_RESULT_ALREADY_DEAD;
    return PIDFD_RESULT_FAILED;
}
#else
static int safe_kill_process(pid_t pid, int sig)
{
    return kill(pid, sig);
}
#endif

/*
 * Kill any surviving descendant processes that belong to the PTY session.
 * Reads /proc to find processes whose session ID (SID) matches sid.
 * Uses bounded 3-pass sweep with pidfd verification where available.
 */
static void pty_kill_session_descendants(pid_t sid)
{
    if (sid <= 1) return;

    for (int pass = 0; pass < 3; pass++) {
        DIR *dir = opendir("/proc");
        if (dir == NULL) return;

        int found_count = 0;
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_name[0] < '0' || ent->d_name[0] > '9') continue;
            pid_t p = (pid_t)atoi(ent->d_name);
            if (p > 1 && p != getpid()) {
                if (getsid(p) == sid) {
                    safe_kill_process(p, SIGKILL);
                    found_count++;
                }
            }
        }
        closedir(dir);
        if (found_count == 0) {
            break;
        }
        usleep(5000); /* 5ms backoff before next pass */
    }
}

/*
 * Coordinated single-owner child process termination and reaping.
 *
 * Sends SIGHUP to the child process group and child PID, waits up to 50ms,
 * and escalates to SIGKILL if the child ignores SIGHUP or remains alive.
 * Reaps with non-blocking waitpid(WNOHANG) and preserves child_pid for
 * quarantine if child cannot be reaped immediately.
 * Must be called with s->lock held.
 */
static void pty_session_reap_child_locked(pty_session_t *s, bool send_signals)
{
    if (s->child_pid <= 0) {
        return;
    }

    pid_t pid = s->child_pid;
    int status = 0;

    if (!send_signals) {
        pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            if (WIFEXITED(status)) {
                s->exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                s->exit_code = 128 + WTERMSIG(status);
            }
            s->child_pid = -1;
            pty_kill_session_descendants(pid);
            return;
        }
    }

    /* Send SIGHUP to the child's process group and the child itself */
    kill(-pid, SIGHUP);
    kill(pid, SIGHUP);

    /* Poll waitpid with WNOHANG for up to 50ms (5 x 10ms) */
    bool reaped = false;
    for (int i = 0; i < 5; i++) {
        usleep(10000); /* 10ms */
        pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            reaped = true;
            break;
        }
    }

    if (!reaped) {
        /* Child ignored SIGHUP or hasn't exited: escalate to SIGKILL */
        kill(-pid, SIGKILL);
        kill(pid, SIGKILL);

        /* Poll waitpid with WNOHANG for up to 50ms */
        for (int i = 0; i < 5; i++) {
            usleep(10000); /* 10ms */
            pid_t waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) {
                reaped = true;
                break;
            }
        }

        if (!reaped) {
            /* Final non-blocking poll */
            pid_t waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) {
                reaped = true;
            }
        }
    }

    if (reaped) {
        if (WIFEXITED(status)) {
            s->exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            s->exit_code = 128 + WTERMSIG(status);
        }
        s->child_pid = -1;
        pty_kill_session_descendants(pid);
    } else {
        /* Non-blocking timeout: inspect /proc/<pid>/stat for diagnostics */
        char state_char = '?';
        char stat_path[64];
        snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", (int)pid);
        FILE *fp = fopen(stat_path, "r");
        if (fp != NULL) {
            int p;
            char comm[256];
            if (fscanf(fp, "%d %s %c", &p, comm, &state_char) != 3) {
                state_char = '?';
            }
            fclose(fp);
        }
        /* Child remains unreaped: preserve s->child_pid for background polling */
        s->exit_code = 137; /* 128 + SIGKILL */
        fprintf(stderr,
                "WARNING: PTY session %s child PID %d failed to exit after SIGKILL (state: %c); slot quarantined.\n",
                s->session_id, (int)pid, state_char);
    }
}

static void pty_session_do_terminate_locked(pty_session_t *s)
{
    if (s->status == PTY_STATUS_TERMINATED && s->master_fd < 0 && s->child_pid <= 0) {
        return;
    }

    /* Terminate and reap child process (SIGHUP -> SIGKILL escalation) */
    pty_session_reap_child_locked(s, true);

    /* Close master FD */
    if (s->master_fd >= 0) {
        close(s->master_fd);
        s->master_fd = -1;
    }

    /* Send exit notification to WebSocket client if active, and request closure */
    if (s->conn != NULL) {
        uint64_t lease = pty_admission_find_lease_by_conn(s->conn);
        if (lease != 0) {
            pty_admission_request_close(lease);
        }
        pty_ws_send_exit(s->conn, s->exit_code);
        s->conn = NULL;
    }

    s->status = PTY_STATUS_TERMINATED;
}

pty_session_t *pty_session_create(unsigned short cols, unsigned short rows)
{
    pthread_mutex_lock(&g_manager_lock);

    /* Find free slot */
    pty_session_t *slot = NULL;
    for (int i = 0; i < PTY_MAX_SESSIONS; i++) {
        if (!g_sessions[i].in_use && g_sessions[i].child_pid <= 0) {
            slot = &g_sessions[i];
            break;
        }
    }

    if (slot == NULL) {
        pthread_mutex_unlock(&g_manager_lock);
        return NULL; /* Max sessions reached */
    }

    /* Generate session ID (32 hex characters) */
    char full_hex[AUTH_TOKEN_HEX_LEN + 1];
    if (auth_generate_token(full_hex) < 0) {
        pthread_mutex_unlock(&g_manager_lock);
        return NULL;
    }
    memcpy(slot->session_id, full_hex, PTY_SESSION_ID_HEX_LEN);
    slot->session_id[PTY_SESSION_ID_HEX_LEN] = '\0';

    /* Generate initial single-use WebSocket ticket (64 hex characters) */
    if (auth_generate_token(slot->ws_ticket) < 0) {
        pthread_mutex_unlock(&g_manager_lock);
        return NULL;
    }
    slot->ticket_expires_at = time(NULL) + PTY_TICKET_EXPIRY_SEC;
    slot->ticket_used = false;

    slot->cols = (cols > 0) ? cols : 80;
    slot->rows = (rows > 0) ? rows : 24;
    slot->created_at = time(NULL);
    slot->disconnected_at = 0;
    slot->conn = NULL;
    slot->output_len = 0;
    slot->output_buf[0] = '\0';
    slot->status = PTY_STATUS_CREATED;
    slot->exit_code = 0;

    /* Set up terminal window size */
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = slot->cols;
    ws.ws_row = slot->rows;

    int master_fd = -1;
    int slave_fd = -1;

    if (openpty(&master_fd, &slave_fd, NULL, NULL, &ws) < 0) {
        pthread_mutex_unlock(&g_manager_lock);
        return NULL;
    }

    /* Fork child process */
    pid_t pid = fork();
    if (pid < 0) {
        close(master_fd);
        close(slave_fd);
        pthread_mutex_unlock(&g_manager_lock);
        return NULL;
    }

    if (pid == 0) {
        /*
         * CHILD PROCESS:
         * 1. Close master FD.
         * 2. Call setsid() to become session leader.
         * 3. Attach slave as controlling terminal (TIOCSCTTY).
         * 4. Redirect stdin, stdout, stderr to slave.
         * 5. Reset signal handlers and mask to defaults.
         * 6. Close inherited server FDs.
         * 7. Exec bin/smartshell.
         */
        close(master_fd);

        if (setsid() < 0) {
            _exit(1);
        }

        if (ioctl(slave_fd, TIOCSCTTY, 0) < 0) {
            _exit(1);
        }

        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);
        if (slave_fd > STDERR_FILENO) {
            close(slave_fd);
        }

        /* Reset signal mask and signal handlers */
        sigset_t empty;
        sigemptyset(&empty);
        sigprocmask(SIG_SETMASK, &empty, NULL);

        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);

        /* Close inherited file descriptors from 3 to 1024 */
        int max_fd = (int)sysconf(_SC_OPEN_MAX);
        if (max_fd < 0 || max_fd > 1024) max_fd = 1024;
        for (int fd = 3; fd < max_fd; fd++) {
            close(fd);
        }

        /* Set standard environment variables */
        setenv("TERM", "xterm-256color", 1);
        setenv("SMARTSHELL_VERSION", "3.0.0", 1);

        char *argv[] = {(char *)"smartshell", NULL};
        execv(g_shell_path, argv);

        /* If execv fails, exit */
        _exit(127);
    }

    /* PARENT PROCESS */
    close(slave_fd);

    /* Set master FD to non-blocking */
    int flags = fcntl(master_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);
    }

    slot->master_fd = master_fd;
    slot->child_pid = pid;
    slot->in_use = true;

    wakeup_reader_thread();
    pthread_mutex_unlock(&g_manager_lock);

    return slot;
}

pty_session_t *pty_session_find(const char *session_id)
{
    if (session_id == NULL) return NULL;

    pthread_mutex_lock(&g_manager_lock);
    for (int i = 0; i < PTY_MAX_SESSIONS; i++) {
        if (g_sessions[i].in_use &&
            strcmp(g_sessions[i].session_id, session_id) == 0) {
            pthread_mutex_unlock(&g_manager_lock);
            return &g_sessions[i];
        }
    }
    pthread_mutex_unlock(&g_manager_lock);
    return NULL;
}

bool pty_session_authenticate_ticket(const char *session_id, const char *ticket)
{
    if (session_id == NULL || ticket == NULL) {
        return false;
    }

    pty_session_t *s = pty_session_find(session_id);
    if (s == NULL) {
        return false;
    }

    pthread_mutex_lock(&s->lock);

    if (s->status == PTY_STATUS_TERMINATED) {
        pthread_mutex_unlock(&s->lock);
        return false;
    }

    if (s->ticket_used) {
        pthread_mutex_unlock(&s->lock);
        return false;
    }

    time_t now = time(NULL);
    if (now > s->ticket_expires_at) {
        pthread_mutex_unlock(&s->lock);
        return false;
    }

    /* Constant-time comparison */
    size_t tlen = strlen(ticket);
    size_t elen = strlen(s->ws_ticket);
    if (tlen != elen || tlen != PTY_TICKET_HEX_LEN) {
        pthread_mutex_unlock(&s->lock);
        return false;
    }

    volatile unsigned char diff = 0;
    for (size_t i = 0; i < PTY_TICKET_HEX_LEN; i++) {
        diff |= (unsigned char)((unsigned char)ticket[i] ^ (unsigned char)s->ws_ticket[i]);
    }

    if (diff != 0) {
        pthread_mutex_unlock(&s->lock);
        return false;
    }

    /* Single-use: burn ticket immediately upon successful authentication */
    s->ticket_used = true;
    explicit_bzero(s->ws_ticket, sizeof(s->ws_ticket));
    pthread_mutex_unlock(&s->lock);
    return true;
}

int pty_session_issue_reconnect_ticket(const char *session_id, char *ticket_out, size_t size)
{
    if (session_id == NULL || ticket_out == NULL || size < PTY_TICKET_HEX_LEN + 1) {
        return -1;
    }

    pty_session_t *s = pty_session_find(session_id);
    if (s == NULL) return -1;

    pthread_mutex_lock(&s->lock);

    if (s->status == PTY_STATUS_TERMINATED) {
        pthread_mutex_unlock(&s->lock);
        return -1;
    }

    /* Check if disconnected grace period has elapsed */
    time_t now = time(NULL);
    if (s->status == PTY_STATUS_DISCONNECTED && s->disconnected_at > 0) {
        if ((now - s->disconnected_at) >= g_grace_period_sec) {
            pty_session_do_terminate_locked(s);
            pthread_mutex_unlock(&s->lock);
            return -1;
        }
    }

    /* Generate fresh single-use ticket */
    if (auth_generate_token(s->ws_ticket) < 0) {
        pthread_mutex_unlock(&s->lock);
        return -1;
    }
    s->ticket_expires_at = time(NULL) + PTY_TICKET_EXPIRY_SEC;
    s->ticket_used = false;

    strncpy(ticket_out, s->ws_ticket, size - 1);
    ticket_out[size - 1] = '\0';

    pthread_mutex_unlock(&s->lock);
    return 0;
}

void pty_session_attach_conn(pty_session_t *session, struct mg_connection *conn)
{
    if (session == NULL || conn == NULL) return;

    pthread_mutex_lock(&session->lock);

    /* Transfer of ownership: cleanly invalidate and close previous connection */
    if (session->conn != NULL && session->conn != conn) {
        struct mg_connection *old_conn = session->conn;

        /* Invalidate old connection user data so it cannot send further commands */
        ws_conn_ctx_t *old_cd = (ws_conn_ctx_t *)mg_get_user_connection_data(old_conn);
        if (old_cd != NULL) {
            old_cd->authenticated = false;
            old_cd->session_id[0] = '\0';
        }

        uint64_t old_lease = pty_admission_find_lease_by_conn(old_conn);
        if (old_lease != 0) {
            pty_admission_request_close(old_lease);
        }

        /* Send error to old connection */
        cJSON *err_msg = cJSON_CreateObject();
        if (err_msg != NULL) {
            cJSON_AddStringToObject(err_msg, "type", "error");
            cJSON_AddStringToObject(err_msg, "message", "session transferred to new connection");
            char *err_str = cJSON_PrintUnformatted(err_msg);
            cJSON_Delete(err_msg);
            if (err_str != NULL) {
                mg_websocket_write(old_conn, MG_WEBSOCKET_OPCODE_TEXT, err_str, strlen(err_str));
                free(err_str);
            }
        }
    }

    session->conn = conn;
    session->status = PTY_STATUS_ACTIVE;
    session->disconnected_at = 0;

    /* Flush scrollback output to newly attached connection */
    if (session->output_len > 0) {
        pty_ws_send_output(conn, session->output_buf, session->output_len);
    }
    pthread_mutex_unlock(&session->lock);
}

void pty_session_detach_conn(pty_session_t *session, struct mg_connection *conn)
{
    if (session == NULL) return;

    pthread_mutex_lock(&session->lock);
    if (conn == NULL || session->conn == conn) {
        session->conn = NULL;
        if (session->status == PTY_STATUS_ACTIVE) {
            session->status = PTY_STATUS_DISCONNECTED;
            session->disconnected_at = time(NULL);
        }
    }
    pthread_mutex_unlock(&session->lock);
}

int pty_session_write_input(pty_session_t *session, const char *data, size_t len)
{
    if (session == NULL || data == NULL || len == 0) return -1;

    pthread_mutex_lock(&session->lock);
    if (session->master_fd < 0 || session->status == PTY_STATUS_TERMINATED) {
        pthread_mutex_unlock(&session->lock);
        return -1;
    }

    ssize_t written = write(session->master_fd, data, len);
    int res = (written >= 0) ? (int)written : -1;
    pthread_mutex_unlock(&session->lock);
    return res;
}

int pty_session_resize(pty_session_t *session, unsigned short cols, unsigned short rows)
{
    if (session == NULL || cols == 0 || rows == 0) return -1;

    pthread_mutex_lock(&session->lock);
    if (session->master_fd < 0 || session->status == PTY_STATUS_TERMINATED) {
        pthread_mutex_unlock(&session->lock);
        return -1;
    }

    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = cols;
    ws.ws_row = rows;

    int ret = ioctl(session->master_fd, TIOCSWINSZ, &ws);
    if (ret == 0) {
        session->cols = cols;
        session->rows = rows;
    }
    pthread_mutex_unlock(&session->lock);
    return ret;
}

int pty_session_terminate(pty_session_t *session)
{
    if (session == NULL) return -1;

    pthread_mutex_lock(&session->lock);
    pty_session_do_terminate_locked(session);
    pthread_mutex_unlock(&session->lock);

    wakeup_reader_thread();
    return 0;
}

int pty_session_delete(const char *session_id)
{
    if (session_id == NULL) return -1;

    pthread_mutex_lock(&g_manager_lock);
    pty_session_t *target = NULL;
    for (int i = 0; i < PTY_MAX_SESSIONS; i++) {
        if (g_sessions[i].in_use &&
            strcmp(g_sessions[i].session_id, session_id) == 0) {
            target = &g_sessions[i];
            break;
        }
    }

    if (target == NULL) {
        pthread_mutex_unlock(&g_manager_lock);
        return -1;
    }

    pthread_mutex_lock(&target->lock);
    pty_session_do_terminate_locked(target);
    if (target->child_pid <= 0) {
        target->in_use = false;
    }
    pthread_mutex_unlock(&target->lock);

    wakeup_reader_thread();
    pthread_mutex_unlock(&g_manager_lock);
    return 0;
}

int pty_session_list(pty_session_t *out_list, int max_count)
{
    if (out_list == NULL || max_count <= 0) return 0;

    int count = 0;
    pthread_mutex_lock(&g_manager_lock);
    for (int i = 0; i < PTY_MAX_SESSIONS && count < max_count; i++) {
        if (g_sessions[i].in_use) {
            pthread_mutex_lock(&g_sessions[i].lock);
            out_list[count++] = g_sessions[i];
            pthread_mutex_unlock(&g_sessions[i].lock);
        }
    }
    pthread_mutex_unlock(&g_manager_lock);
    return count;
}

/*
 * Background multiplexer reader thread.
 * Efficiently polls all active PTY master FDs and the wakeup self-pipe.
 * Checks disconnect grace periods on each timeout interval.
 */
static void *pty_reader_thread_func(void *arg)
{
    (void)arg;
    struct pollfd pfds[PTY_MAX_SESSIONS + 1];
    pty_session_t *session_ptrs[PTY_MAX_SESSIONS + 1];

    while (g_reader_running) {
        int nfds = 0;

        /* pfds[0] is the wakeup pipe */
        pfds[0].fd = g_wakeup_pipe[0];
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;
        session_ptrs[0] = NULL;
        nfds++;

        time_t now = time(NULL);

        pthread_mutex_lock(&g_manager_lock);
        for (int i = 0; i < PTY_MAX_SESSIONS; i++) {
            pty_session_t *s = &g_sessions[i];
            if (!s->in_use) continue;

            pthread_mutex_lock(&s->lock);

            /* Retry reaping any quarantined child processes */
            if (s->status == PTY_STATUS_TERMINATED && s->child_pid > 0) {
                int q_status = 0;
                pid_t waited = waitpid(s->child_pid, &q_status, WNOHANG);
                if (waited == s->child_pid) {
                    pid_t old_pid = s->child_pid;
                    s->child_pid = -1;
                    pty_kill_session_descendants(old_pid);
                    s->in_use = false;
                }
            }

            /* Check disconnect grace period expiry */
            if (s->status == PTY_STATUS_DISCONNECTED && s->disconnected_at > 0) {
                if ((now - s->disconnected_at) >= g_grace_period_sec) {
                    /* Grace period elapsed without reconnect: terminate session */
                    pty_session_do_terminate_locked(s);
                    pthread_mutex_unlock(&s->lock);
                    continue;
                }
            }

            /* Monitor master FD if open */
            if (s->master_fd >= 0 && s->status != PTY_STATUS_TERMINATED) {
                pfds[nfds].fd = s->master_fd;
                pfds[nfds].events = POLLIN;
                pfds[nfds].revents = 0;
                session_ptrs[nfds] = s;
                nfds++;
            }
            pthread_mutex_unlock(&s->lock);
        }
        pthread_mutex_unlock(&g_manager_lock);

        /* Poll with 500ms timeout for periodic grace period and auth timeout checks */
        int ret = poll(pfds, nfds, 500);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (!g_reader_running) {
            break;
        }

        /* Check for unauthenticated WebSocket connection timeouts */
        pty_admission_check_timeouts();

        /* Drain wakeup pipe if signaled */
        if (pfds[0].revents & POLLIN) {
            char drain[64];
            while (read(g_wakeup_pipe[0], drain, sizeof(drain)) > 0) {}
        }

        /* Process PTY events */
        for (int i = 1; i < nfds; i++) {
            pty_session_t *s = session_ptrs[i];
            if (s == NULL) continue;

            if (pfds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                pthread_mutex_lock(&s->lock);
                if (s->master_fd >= 0) {
                    char chunk[PTY_READ_CHUNK_SIZE];
                    ssize_t n = read(s->master_fd, chunk, sizeof(chunk) - 1);
                    if (n > 0) {
                        chunk[n] = '\0';
                        pty_session_append_output(s, chunk, (size_t)n);

                        if (s->conn != NULL && s->status == PTY_STATUS_ACTIVE) {
                            int wres = pty_ws_send_output(s->conn, chunk, (size_t)n);
                            if (wres <= 0) {
                                /* Write failed: client disconnected or socket broken */
                                s->conn = NULL;
                                s->status = PTY_STATUS_DISCONNECTED;
                                s->disconnected_at = time(NULL);
                            }
                        }
                    } else if (n <= 0) {
                        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                            /* Non-blocking read had no data */
                        } else {
                            /* Child closed terminal or exited (EIO on Linux) */
                            pty_session_do_terminate_locked(s);
                        }
                    }
                }
                pthread_mutex_unlock(&s->lock);
            }
        }
    }

    return NULL;
}

