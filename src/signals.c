/*
 * signals.c — Signal handling, process groups, and terminal control implementation.
 */

#include "signals.h"
#include "job.h"
#include "utils.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Self-pipe file descriptors: [0] = read, [1] = write */
static int g_self_pipe[2] = { -1, -1 };

/* Interactive terminal state */
static bool           g_is_interactive = false;
static pid_t          g_shell_pgid = 0;
static int            g_shell_terminal = STDIN_FILENO;
static struct termios g_shell_tmodes;

/* Pre-allocated ring buffer for asynchronous child events */
typedef struct {
    pid_t pid;
    int   status;
} chld_event_t;

#define MAX_CHLD_EVENTS 512

static volatile chld_event_t g_chld_events[MAX_CHLD_EVENTS];
static volatile sig_atomic_t g_event_head = 0;
static volatile sig_atomic_t g_event_tail = 0;

/*
 * Async-signal-safe SIGCHLD handler.
 * Reaps any changed child processes and notifies the main loop via self-pipe.
 * Strictly uses only waitpid() and write() — no malloc, free, or stdio.
 */
static void sigchld_handler(int sig)
{
    (void)sig;
    int saved_errno = errno;
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        int next_head = (g_event_head + 1) % MAX_CHLD_EVENTS;
        if (next_head != g_event_tail) {
            g_chld_events[g_event_head].pid = pid;
            g_chld_events[g_event_head].status = status;
            g_event_head = next_head;
        }
        if (g_self_pipe[1] >= 0) {
            char c = 'c';
            ssize_t w = write(g_self_pipe[1], &c, 1);
            (void)w;
        }
    }

    errno = saved_errno;
}

void signals_init(void)
{
    /* 1. Detect interactivity */
    g_shell_terminal = STDIN_FILENO;
    g_is_interactive = (isatty(g_shell_terminal) == 1);

    /* 2. Create self-pipe */
    if (g_self_pipe[0] < 0) {
        if (pipe(g_self_pipe) < 0) {
            die("signals_init: pipe failed: %s\n", strerror(errno));
        }

        /* Non-blocking on both read and write ends */
        int flags = fcntl(g_self_pipe[0], F_GETFL, 0);
        fcntl(g_self_pipe[0], F_SETFL, flags | O_NONBLOCK);

        flags = fcntl(g_self_pipe[1], F_GETFL, 0);
        fcntl(g_self_pipe[1], F_SETFL, flags | O_NONBLOCK);

        /* Close-on-exec to prevent leaking into child processes */
        fcntl(g_self_pipe[0], F_SETFD, FD_CLOEXEC);
        fcntl(g_self_pipe[1], F_SETFD, FD_CLOEXEC);
    }

    /* 3. Install async-signal-safe SIGCHLD handler */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; /* Receive SIGCHLD on exit and on stop */

    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        die("signals_init: sigaction(SIGCHLD) failed: %s\n", strerror(errno));
    }
}

void signals_cleanup(void)
{
    if (g_self_pipe[0] >= 0) {
        close(g_self_pipe[0]);
        g_self_pipe[0] = -1;
    }
    if (g_self_pipe[1] >= 0) {
        close(g_self_pipe[1]);
        g_self_pipe[1] = -1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);
}

bool shell_is_interactive_mode(void)
{
    return g_is_interactive;
}

pid_t shell_get_pgid(void)
{
    return g_shell_pgid;
}

int shell_get_terminal_fd(void)
{
    return g_shell_terminal;
}

void shell_init_terminal(void)
{
    if (!g_is_interactive) {
        return;
    }

    /* Verify terminal ownership */
    pid_t pgrp = tcgetpgrp(g_shell_terminal);
    if (pgrp < 0) {
        g_is_interactive = false;
        return;
    }

    /* Loop until shell process group is in the foreground */
    while ((pgrp = tcgetpgrp(g_shell_terminal)) != (g_shell_pgid = getpgrp())) {
        if (pgrp < 0) {
            g_is_interactive = false;
            return;
        }
        kill(-g_shell_pgid, SIGTTIN);
    }

    /* Ignore interactive and job-control signals in the shell process */
    struct sigaction sa_ign;
    memset(&sa_ign, 0, sizeof(sa_ign));
    sa_ign.sa_handler = SIG_IGN;
    sigemptyset(&sa_ign.sa_mask);

    sigaction(SIGINT, &sa_ign, NULL);
    sigaction(SIGQUIT, &sa_ign, NULL);
    sigaction(SIGTSTP, &sa_ign, NULL);
    sigaction(SIGTTIN, &sa_ign, NULL);
    sigaction(SIGTTOU, &sa_ign, NULL);

    /* Put shell in its own process group */
    g_shell_pgid = getpid();
    if (setpgid(g_shell_pgid, g_shell_pgid) < 0) {
        /* EPERM is normal if already process group leader */
    }

    /* Take control of the terminal */
    tcsetpgrp(g_shell_terminal, g_shell_pgid);

    /* Save default terminal attributes */
    tcgetattr(g_shell_terminal, &g_shell_tmodes);
}

void shell_restore_terminal(void)
{
    if (!g_is_interactive) {
        return;
    }
    tcsetpgrp(g_shell_terminal, g_shell_pgid);
    tcsetattr(g_shell_terminal, TCSADRAIN, &g_shell_tmodes);
}

void shell_give_terminal_to(pid_t pgid)
{
    if (!g_is_interactive || pgid <= 0) {
        return;
    }
    tcsetpgrp(g_shell_terminal, pgid);
}

void shell_reclaim_terminal(void)
{
    if (!g_is_interactive) {
        return;
    }
    tcsetpgrp(g_shell_terminal, g_shell_pgid);
    tcsetattr(g_shell_terminal, TCSADRAIN, &g_shell_tmodes);
}

void signals_reset_child_defaults(void)
{
    struct sigaction sa_dfl;
    memset(&sa_dfl, 0, sizeof(sa_dfl));
    sa_dfl.sa_handler = SIG_DFL;
    sigemptyset(&sa_dfl.sa_mask);

    sigaction(SIGINT, &sa_dfl, NULL);
    sigaction(SIGQUIT, &sa_dfl, NULL);
    sigaction(SIGTSTP, &sa_dfl, NULL);
    sigaction(SIGTTIN, &sa_dfl, NULL);
    sigaction(SIGTTOU, &sa_dfl, NULL);
    sigaction(SIGCHLD, &sa_dfl, NULL);
}

void signals_block_sigchld(sigset_t *prev_mask)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, prev_mask);
}

void signals_unblock_sigchld(const sigset_t *prev_mask)
{
    sigprocmask(SIG_SETMASK, prev_mask, NULL);
}

int signals_get_self_pipe_read_fd(void)
{
    return g_self_pipe[0];
}

void signals_process_chld_events(void)
{
    /* 1. Drain the self-pipe */
    if (g_self_pipe[0] >= 0) {
        char buf[128];
        while (read(g_self_pipe[0], buf, sizeof(buf)) > 0) {
            /* Drain non-blocking pipe */
        }
    }

    /* 2. Block SIGCHLD while extracting events from the ring buffer */
    sigset_t mask, prev_mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &prev_mask);

    chld_event_t local_events[MAX_CHLD_EVENTS];
    int count = 0;

    while (g_event_tail != g_event_head && count < MAX_CHLD_EVENTS) {
        local_events[count++] = g_chld_events[g_event_tail];
        g_event_tail = (g_event_tail + 1) % MAX_CHLD_EVENTS;
    }

    sigprocmask(SIG_SETMASK, &prev_mask, NULL);

    /* 3. Apply state updates to the job table in main thread context */
    for (int i = 0; i < count; i++) {
        pid_t pid = local_events[i].pid;
        int status = local_events[i].status;
        proc_state_t new_state = PROC_RUNNING;

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            new_state = PROC_COMPLETED;
        } else if (WIFSTOPPED(status)) {
            new_state = PROC_STOPPED;
        } else if (WIFCONTINUED(status)) {
            new_state = PROC_RUNNING;
        }

        job_update_process_status(pid, status, new_state);
    }
}

int wait_for_job(job_t *job)
{
    if (job == NULL) {
        return 0;
    }

    /* Process any events that already arrived */
    signals_process_chld_events();

    while (job->state == JOB_RUNNING) {
        struct pollfd pfd;
        pfd.fd = g_self_pipe[0];
        pfd.events = POLLIN;
        pfd.revents = 0;

        int ret = poll(&pfd, 1, 100);
        if (ret < 0 && errno == EINTR) {
            /* Interrupted by signal; continue to process events */
        }

        signals_process_chld_events();
    }

    /* Return exit status of the last process in the pipeline */
    int last_status = 0;
    if (job->process_count > 0) {
        int raw_status = job->processes[job->process_count - 1].status;
        if (WIFEXITED(raw_status)) {
            last_status = WEXITSTATUS(raw_status);
        } else if (WIFSIGNALED(raw_status)) {
            last_status = 128 + WTERMSIG(raw_status);
        } else if (WIFSTOPPED(raw_status)) {
            last_status = 128 + WSTOPSIG(raw_status);
        }
    }

    return last_status;
}
