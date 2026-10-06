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

/* Default time quantum for Round Robin scheduling */
#define SCHED_DEFAULT_RR_QUANTUM 4

/* Multilevel Feedback Queue (MLFQ) parameters */
#define SCHED_MLFQ_Q0_QUANTUM     4
#define SCHED_MLFQ_Q1_QUANTUM     8
#define SCHED_MLFQ_BOOST_INTERVAL 200

/*
 * Virtual process representation.
 * All timing metrics use simulated integer units.
 */
typedef struct {
    int pid;              /* Process identifier (positive integer) */
    int arrival_time;     /* Simulated arrival time (>= 0) */
    int burst_time;       /* Total CPU burst requirement (> 0) */
    int remaining_time;   /* Remaining CPU time needed to complete */
    int priority;         /* Static priority level (lower number = higher priority) */
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

/*
 * Run Round Robin (RR) preemptive scheduling.
 * Parameters:
 *   quantum - Positive integer time quantum (> 0)
 * Queue/Arrival policy:
 *   1. Arrived processes enter the FIFO ready queue when arrival_time <= current_time.
 *   2. Newly arrived processes during a slice are enqueued before requeueing a preempted job.
 *   3. Simultaneous arrivals enter queue ordered by earliest arrival, then lowest PID.
 * Returns 0 on success, -1 on invalid input (including quantum <= 0).
 */
int sched_schedule_rr(const sched_process_t *procs, int count, int quantum, sched_result_t *result);

/*
 * Run Priority non-preemptive scheduling.
 * Policy:
 *   1. Lower numeric value = higher priority.
 *   2. Non-preemptive (once scheduled, runs to burst completion).
 * Tie-breaking:
 *   1. Lower priority number (highest priority)
 *   2. Earliest arrival time
 *   3. Lowest PID
 * Returns 0 on success, -1 on invalid input.
 */
int sched_schedule_priority(const sched_process_t *procs, int count, sched_result_t *result);

/*
 * Run Multilevel Feedback Queue (MLFQ) 3-level scheduling simulation.
 * Architecture:
 *   - Q0: Highest priority, Round Robin, quantum = 4 ticks
 *   - Q1: Medium priority, Round Robin, quantum = 8 ticks
 *   - Q2: Lowest priority, FCFS (non-preemptive within Q2)
 * Policy:
 *   1. Initial placement: All new arrivals enter Q0.
 *   2. Higher-queue priority: Q0 > Q1 > Q2. Lower queues run only when higher queues empty.
 *   3. Higher-queue preemption: Arrivals into Q0 preempt running Q1 or Q2 jobs at the arrival tick.
 *   4. Demotion: Q0 processes exhausting 4 ticks demote to Q1. Q1 processes exhausting 8 ticks demote to Q2.
 *   5. Priority Boost: Every 200 simulated ticks, all jobs in Q1 and Q2 move to the tail of Q0 in deterministic FIFO order.
 *   6. Tie-breaking: Simultaneous arrivals order by arrival time asc, then lowest PID.
 * Returns 0 on success, -1 on invalid input.
 */
int sched_schedule_mlfq(const sched_process_t *procs, int count, sched_result_t *result);

/*
 * Run MLFQ with configurable quantum and boost parameters.
 * Returns 0 on success, -1 on invalid input (including quantums/boost <= 0).
 */
int sched_schedule_mlfq_custom(const sched_process_t *procs, int count,
                               int q0_quantum, int q1_quantum, int boost_interval,
                               sched_result_t *result);

/* Compute aggregate and per-process summary metrics */
int sched_compute_metrics(sched_result_t *result);

/* Helper to lookup a process by PID from the simulation results (returns NULL if not found) */
const sched_process_t *sched_result_get_proc(const sched_result_t *result, int pid);

/*
 * Render execution timeline into a formatted textual Gantt chart string.
 * Returns an allocated string that caller must free(), or NULL on error.
 */
char *sched_render_gantt(const sched_result_t *result);

/*
 * Render execution timeline into caller-provided buffer.
 * Returns the number of characters formatted (excluding terminating null byte),
 * or -1 on error.
 */
int sched_render_gantt_buf(const sched_result_t *result, char *buf, size_t size);

#endif /* SMARTSHELL_SCHEDULER_H */
