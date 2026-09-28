/*
 * job.c — Job and process management foundation implementation for SmartShell.
 */

#include "job.h"
#include "utils.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bounded job table structure */
typedef struct {
    job_t *jobs[MAX_JOBS];
    int    count;
} job_table_t;

static job_table_t g_job_table;

void job_table_init(void)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        g_job_table.jobs[i] = NULL;
    }
    g_job_table.count = 0;
}

static void job_free_internal(job_t *job)
{
    if (job == NULL) {
        return;
    }

    free(job->command_line);

    if (job->processes != NULL) {
        for (int i = 0; i < job->process_count; i++) {
            if (job->processes[i].argv != NULL) {
                for (int j = 0; j < job->processes[i].argc; j++) {
                    free(job->processes[i].argv[j]);
                }
                free(job->processes[i].argv);
            }
        }
        free(job->processes);
    }

    free(job);
}

void job_table_cleanup(void)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (g_job_table.jobs[i] != NULL) {
            job_t *job = g_job_table.jobs[i];
            if (job->state == JOB_RUNNING || job->state == JOB_STOPPED) {
                if (job->pgid > 0) {
                    kill(-job->pgid, SIGHUP);
                    kill(-job->pgid, SIGCONT);
                }
            }
            job_free_internal(job);
            g_job_table.jobs[i] = NULL;
        }
    }
    g_job_table.count = 0;
}

/*
 * Find the lowest unused positive integer for the next Job ID.
 * Returns -1 if the table is completely full.
 */
static int get_next_jid(void)
{
    for (int candidate = 1; candidate <= MAX_JOBS; candidate++) {
        bool in_use = false;
        for (int i = 0; i < MAX_JOBS; i++) {
            if (g_job_table.jobs[i] != NULL && g_job_table.jobs[i]->jid == candidate) {
                in_use = true;
                break;
            }
        }
        if (!in_use) {
            return candidate;
        }
    }
    return -1;
}

/*
 * Reconstruct a readable command line string from a parsed pipeline.
 */
static char *reconstruct_command_line(const command_t *cmd_list)
{
    if (cmd_list == NULL) {
        return xstrdup("");
    }

    size_t total_len = 0;
    for (const command_t *c = cmd_list; c != NULL; c = c->pipe_next) {
        for (int i = 0; i < c->argc; i++) {
            total_len += strlen(c->argv[i]) + 1;
        }
        if (c->pipe_next != NULL) {
            total_len += 3; /* " | " */
        }
    }

    char *buf = xmalloc(total_len + 1);
    buf[0] = '\0';

    for (const command_t *c = cmd_list; c != NULL; c = c->pipe_next) {
        for (int i = 0; i < c->argc; i++) {
            strcat(buf, c->argv[i]);
            if (i < c->argc - 1) {
                strcat(buf, " ");
            }
        }
        if (c->pipe_next != NULL) {
            strcat(buf, " | ");
        }
    }

    return buf;
}

job_t *job_create(const pid_t *pids, const command_t *cmd_list, int count,
                  const char *cmdline, bool is_foreground)
{
    if (pids == NULL || count <= 0) {
        return NULL;
    }

    if (g_job_table.count >= MAX_JOBS) {
        fprintf(stderr, "smartshell: job table full (maximum %d jobs)\n", MAX_JOBS);
        return NULL;
    }

    int jid = get_next_jid();
    if (jid < 0) {
        fprintf(stderr, "smartshell: no available job IDs\n");
        return NULL;
    }

    int slot = -1;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (g_job_table.jobs[i] == NULL) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return NULL;
    }

    job_t *job = xcalloc(1, sizeof(job_t));
    job->jid = jid;
    job->pgid = pids[0];
    job->process_count = count;
    job->is_foreground = is_foreground;
    job->state = JOB_RUNNING;
    job->notified = false;

    if (cmdline != NULL && *cmdline != '\0') {
        job->command_line = xstrdup(cmdline);
    } else {
        job->command_line = reconstruct_command_line(cmd_list);
    }

    job->processes = xcalloc(count, sizeof(process_t));
    const command_t *curr = cmd_list;

    for (int i = 0; i < count; i++) {
        job->processes[i].pid = pids[i];
        job->processes[i].status = 0;
        job->processes[i].state = PROC_RUNNING;

        if (curr != NULL && curr->argv != NULL && curr->argc > 0) {
            job->processes[i].argc = curr->argc;
            job->processes[i].argv = xmalloc((curr->argc + 1) * sizeof(char *));
            for (int j = 0; j < curr->argc; j++) {
                job->processes[i].argv[j] = xstrdup(curr->argv[j]);
            }
            job->processes[i].argv[curr->argc] = NULL;
        } else {
            job->processes[i].argc = 0;
            job->processes[i].argv = NULL;
        }

        if (curr != NULL) {
            curr = curr->pipe_next;
        }
    }

    g_job_table.jobs[slot] = job;
    g_job_table.count++;

    return job;
}

job_t *job_find_by_jid(int jid)
{
    if (jid <= 0) {
        return NULL;
    }

    for (int i = 0; i < MAX_JOBS; i++) {
        if (g_job_table.jobs[i] != NULL && g_job_table.jobs[i]->jid == jid) {
            return g_job_table.jobs[i];
        }
    }

    return NULL;
}

job_t *job_find_by_pid(pid_t pid)
{
    if (pid <= 0) {
        return NULL;
    }

    for (int i = 0; i < MAX_JOBS; i++) {
        job_t *job = g_job_table.jobs[i];
        if (job != NULL) {
            for (int j = 0; j < job->process_count; j++) {
                if (job->processes[j].pid == pid) {
                    return job;
                }
            }
        }
    }

    return NULL;
}

job_t *job_find_by_pgid(pid_t pgid)
{
    if (pgid <= 0) {
        return NULL;
    }

    for (int i = 0; i < MAX_JOBS; i++) {
        if (g_job_table.jobs[i] != NULL && g_job_table.jobs[i]->pgid == pgid) {
            return g_job_table.jobs[i];
        }
    }

    return NULL;
}

void job_update_state(job_t *job)
{
    if (job == NULL) {
        return;
    }

    int completed = 0;
    int stopped = 0;
    int running = 0;

    for (int i = 0; i < job->process_count; i++) {
        switch (job->processes[i].state) {
            case PROC_COMPLETED:
                completed++;
                break;
            case PROC_STOPPED:
                stopped++;
                break;
            case PROC_RUNNING:
                running++;
                break;
        }
    }

    if (completed == job->process_count) {
        job->state = JOB_COMPLETED;
    } else if (running > 0) {
        job->state = JOB_RUNNING;
    } else if (stopped + completed == job->process_count && stopped > 0) {
        job->state = JOB_STOPPED;
    }
}

bool job_update_process_status(pid_t pid, int status, proc_state_t new_state)
{
    if (pid <= 0) {
        return false;
    }

    job_t *job = job_find_by_pid(pid);
    if (job == NULL) {
        return false;
    }

    for (int i = 0; i < job->process_count; i++) {
        if (job->processes[i].pid == pid) {
            job->processes[i].status = status;
            job->processes[i].state = new_state;
            job_update_state(job);
            return true;
        }
    }

    return false;
}

bool job_remove(int jid)
{
    if (jid <= 0) {
        return false;
    }

    for (int i = 0; i < MAX_JOBS; i++) {
        if (g_job_table.jobs[i] != NULL && g_job_table.jobs[i]->jid == jid) {
            job_free_internal(g_job_table.jobs[i]);
            g_job_table.jobs[i] = NULL;
            g_job_table.count--;
            return true;
        }
    }

    return false;
}

bool job_is_completed(const job_t *job)
{
    return (job != NULL && job->state == JOB_COMPLETED);
}

bool job_is_stopped(const job_t *job)
{
    return (job != NULL && job->state == JOB_STOPPED);
}

bool job_is_running(const job_t *job)
{
    return (job != NULL && job->state == JOB_RUNNING);
}

int job_table_active_count(void)
{
    return g_job_table.count;
}

const char *job_state_to_string(job_state_t state)
{
    switch (state) {
        case JOB_RUNNING:   return "Running";
        case JOB_STOPPED:   return "Stopped";
        case JOB_COMPLETED: return "Done";
        default:            return "Unknown";
    }
}

void job_table_print(void)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        const job_t *job = g_job_table.jobs[i];
        if (job != NULL) {
            printf("[%d]  %-10s %s\n",
                   job->jid,
                   job_state_to_string(job->state),
                   job->command_line);
        }
    }
}
