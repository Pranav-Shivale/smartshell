/*
 * scheduler.h — User-space CPU scheduling simulation subsystem for SmartShell.
 *
 * NOTE: This is an educational simulation module. SmartShell does NOT control,
 * replace, or modify Linux's real kernel scheduler. Virtual time is represented
 * with integer values and advanced mathematically without real process execution
 * or wall-clock sleeping.
 */

#ifndef SMARTSHELL_SCHEDULER_H
#define SMARTSHELL_SCHEDULER_H

#include <stdbool.h>
#include <stddef.h>

/* Sentinel value for uninitialized or unset simulated time */
#define SCHED_TIME_UNSET (-1)

/* Special PID denoting CPU idle periods in the timeline */
#define SCHED_PID_IDLE   (-1)

/*
 * Virtual process representation.
 * All timing metrics use simulated integer units.
 */
typedef struct {
    int pid;              /* Process identifier (positive integer) */
    int arrival_time;     /* Simulated arrival time (>= 0) */
    int burst_time;       /* Total CPU burst requirement (> 0) */
    int remaining_time;   /* Remaining CPU time needed to complete */
    int priority;         /* Static priority level */
    int start_time;       /* First time process received CPU (or SCHED_TIME_UNSET) */
    int completion_time;  /* Time process finished execution (or SCHED_TIME_UNSET) */
    int waiting_time;     /* Total time spent waiting in ready queue */
    int turnaround_time;  /* Total time from arrival to completion */
    int response_time;    /* Time from arrival to first execution */
} sched_process_t;

/*
 * Timeline interval entry.
 * Represents execution on the simulated CPU during [start_time, end_time].
 * If pid == SCHED_PID_IDLE, CPU was idle during this interval.
 */
typedef struct {
    int pid;              /* Executing process PID or SCHED_PID_IDLE */
    int start_time;       /* Simulated start tick of this interval */
    int end_time;         /* Simulated end tick of this interval */
} sched_timeline_entry_t;

/*
 * Scheduling simulation result.
 * Contains detailed process states, the structured execution timeline,
 * and system-wide aggregate performance metrics.
 */
typedef struct {
    sched_process_t        *processes;           /* Array of processed virtual jobs */
    int                     process_count;       /* Number of processes in result */
    sched_timeline_entry_t *timeline;            /* Structured execution timeline */
    int                     timeline_count;      /* Number of timeline interval slices */
    int                     timeline_cap;        /* Capacity of allocated timeline */
    int                     total_elapsed_time;  /* Total simulated span from 0 to finish */
    int                     total_busy_time;     /* Total simulated time CPU executed processes */
    int                     total_idle_time;     /* Total simulated time CPU was idle */
    double                  cpu_utilization;     /* (busy_time / elapsed_time) * 100.0 */
    double                  avg_waiting_time;    /* Mean waiting time across processes */
    double                  avg_turnaround_time; /* Mean turnaround time across processes */
    double                  avg_response_time;   /* Mean response time across processes */
} sched_result_t;

/* Lifecycle and memory management */
void sched_result_init(sched_result_t *result);
void sched_result_free(sched_result_t *result);

/* Initialize a virtual process structure with default unset metrics */
void sched_process_init(sched_process_t *proc, int pid, int arrival_time, int burst_time, int priority);

/* Append an execution or idle interval to the timeline (merging contiguous matching slices) */
int sched_timeline_append(sched_result_t *result, int pid, int start_time, int end_time);

/*
 * Run First-Come, First-Served (FCFS) non-preemptive scheduling.
 * Tie-breaking:
 *   1. Earliest arrival time
 *   2. Lowest PID
 * Returns 0 on success, -1 on invalid input.
 */
int sched_schedule_fcfs(const sched_process_t *procs, int count, sched_result_t *result);

/*
 * Run Shortest Job First (SJF) non-preemptive scheduling.
 * Tie-breaking:
 *   1. Shortest burst time
 *   2. Earliest arrival time
 *   3. Lowest PID
 * Returns 0 on success, -1 on invalid input.
 */
int sched_schedule_sjf(const sched_process_t *procs, int count, sched_result_t *result);

/* Compute aggregate and per-process summary metrics */
int sched_compute_metrics(sched_result_t *result);

/* Helper to lookup a process by PID from the simulation results (returns NULL if not found) */
const sched_process_t *sched_result_get_proc(const sched_result_t *result, int pid);

#endif /* SMARTSHELL_SCHEDULER_H */
