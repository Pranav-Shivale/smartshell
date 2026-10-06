/*
 * job.h — Job and process management foundation for SmartShell.
 */

#ifndef SMARTSHELL_JOB_H
#define SMARTSHELL_JOB_H

#include "parser.h"

#include <stdbool.h>
#include <sys/types.h>

#define MAX_JOBS 64

/* Process execution state within a job */
typedef enum {
    PROC_RUNNING,
    PROC_STOPPED,
    PROC_COMPLETED
} proc_state_t;

/* A single process within a job / pipeline */
typedef struct {
    pid_t        pid;            /* Process PID returned by fork()   */
    char       **argv;           /* Argument vector copy             */
    int          argc;           /* Argument count                   */
    int          status;         /* Exit/wait status code            */
    proc_state_t state;          /* Current process state            */
} process_t;

/* Aggregate job execution state */
typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_COMPLETED
} job_state_t;

/*
 * Logical Job structure representing a command or multi-process pipeline.
 *
 * In SmartShell:
 *   one Job = one command line = one pipeline = one process group.
 */
typedef struct job {
    int          jid;            /* Job ID: 1, 2, ...                */
    pid_t        pgid;           /* Process group ID (leader PID)    */
    char        *command_line;   /* Command line representation      */
    process_t   *processes;      /* Array of processes in pipeline   */
    int          process_count;  /* Number of processes in pipeline  */
    job_state_t  state;          /* Aggregate job state              */
    bool         is_foreground;  /* Foreground vs background         */
    bool         notified;       /* Notification flag for user       */
} job_t;

/*
 * Initialize the global job table.
 */
void job_table_init(void);

/*
 * Clean up all jobs in the table and release all allocated resources.
 */
void job_table_cleanup(void);

/*
 * Create a new job in the job table.
 *
 * Parameters:
 *   pids          - array of child PIDs of length count
 *   cmd_list      - head of the parsed command_t pipeline
 *   count         - number of processes in the pipeline
 *   cmdline       - command line string (or NULL to reconstruct from cmd_list)
 *   is_foreground - true if job is running in foreground
 *
 * Returns pointer to created job_t, or NULL if table is full or allocation failed.
 */
job_t *job_create(const pid_t *pids, const command_t *cmd_list, int count,
                  const char *cmdline, bool is_foreground);

/*
 * Find a job by its Job ID (JID).
 * Returns job_t pointer, or NULL if not found.
 */
job_t *job_find_by_jid(int jid);

/*
 * Find a job containing a process with the given PID.
 * Returns job_t pointer, or NULL if not found.
 */
job_t *job_find_by_pid(pid_t pid);

/*
 * Find a job by its Process Group ID (PGID).
 * Returns job_t pointer, or NULL if not found.
 */
job_t *job_find_by_pgid(pid_t pgid);

/*
 * Update the status and state of a specific process within a job.
 * Automatically recalculates the job's aggregate state.
 *
 * Returns true if the process was found and updated, false otherwise.
 */
bool job_update_process_status(pid_t pid, int status, proc_state_t new_state);

/*
 * Recalculate and update the aggregate state of a job based on its processes.
 */
void job_update_state(job_t *job);

/*
 * Remove a job from the table by JID and free all its allocated resources.
 * Returns true if found and removed, false otherwise.
 */
bool job_remove(int jid);

/*
 * State inspection helpers.
 */
bool job_is_completed(const job_t *job);
bool job_is_stopped(const job_t *job);
bool job_is_running(const job_t *job);

/*
 * Return the number of active jobs currently in the table.
 */
int job_table_active_count(void);

/*
 * Print all active jobs in the table (minimum internal infrastructure).
 */
void job_table_print(void);

/*
 * Helper to convert job state to human-readable string ("Running", "Stopped", "Done").
 */
const char *job_state_to_string(job_state_t state);

/*
 * Check all non-foreground jobs in the table:
 * - print notifications for newly completed or stopped background jobs
 * - safely remove and free completed background jobs
 */
void job_notify_and_cleanup(void);

/*
 * Get the current default job (for fg/bg without arguments).
 * Priority: highest JID stopped job; if none, highest JID running job.
 * Returns job_t pointer, or NULL if no active jobs exist.
 */
job_t *job_get_current(void);

#endif /* SMARTSHELL_JOB_H */
