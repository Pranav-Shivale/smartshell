/*
 * scheduler.c — User-space CPU scheduling simulation implementation.
 *
 * Implements deterministic FCFS, non-preemptive SJF, Round Robin (RR),
 * non-preemptive Priority scheduling, execution timeline tracking,
 * Gantt chart textual presentation, and metric computation.
 */

#include "scheduler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void sched_result_init(sched_result_t *result) {
    if (!result) return;
    memset(result, 0, sizeof(*result));
}

void sched_result_free(sched_result_t *result) {
    if (!result) return;
    free(result->processes);
    free(result->timeline);
    sched_result_init(result);
}

void sched_process_init(sched_process_t *proc, int pid, int arrival_time, int burst_time, int priority) {
    if (!proc) return;
    proc->pid = pid;
    proc->arrival_time = arrival_time;
    proc->burst_time = burst_time;
    proc->remaining_time = burst_time;
    proc->priority = priority;
    proc->start_time = SCHED_TIME_UNSET;
    proc->completion_time = SCHED_TIME_UNSET;
    proc->waiting_time = SCHED_TIME_UNSET;
    proc->turnaround_time = SCHED_TIME_UNSET;
    proc->response_time = SCHED_TIME_UNSET;
}

int sched_timeline_append(sched_result_t *result, int pid, int start_time, int end_time) {
    if (!result || start_time >= end_time) {
        return -1;
    }

    /* Merge with contiguous interval of the same PID */
    if (result->timeline_count > 0) {
        sched_timeline_entry_t *last = &result->timeline[result->timeline_count - 1];
        if (last->pid == pid && last->end_time == start_time) {
            last->end_time = end_time;
            return 0;
        }
    }

    if (result->timeline_count >= result->timeline_cap) {
        int new_cap = (result->timeline_cap == 0) ? 16 : (result->timeline_cap * 2);
        sched_timeline_entry_t *new_arr = realloc(result->timeline,
                                                  (size_t)new_cap * sizeof(sched_timeline_entry_t));
        if (!new_arr) {
            return -1;
        }
        result->timeline = new_arr;
        result->timeline_cap = new_cap;
    }

    result->timeline[result->timeline_count].pid = pid;
    result->timeline[result->timeline_count].start_time = start_time;
    result->timeline[result->timeline_count].end_time = end_time;
    result->timeline_count++;
    return 0;
}

static int sched_validate_inputs(const sched_process_t *procs, int count, sched_result_t *result) {
    if (!result || count < 0) {
        return -1;
    }
    if (count == 0) {
        return 0;
    }
    if (!procs) {
        return -1;
    }

    for (int i = 0; i < count; i++) {
        if (procs[i].pid < 0 || procs[i].pid == SCHED_PID_IDLE) {
            return -1;
        }
        if (procs[i].burst_time <= 0 || procs[i].arrival_time < 0) {
            return -1;
        }
        /* Disallow duplicate PIDs */
        for (int j = i + 1; j < count; j++) {
            if (procs[i].pid == procs[j].pid) {
                return -1;
            }
        }
    }

    return 0;
}

int sched_compute_metrics(sched_result_t *result) {
    if (!result) {
        return -1;
    }

    if (result->process_count == 0) {
        result->total_elapsed_time = 0;
        result->total_busy_time = 0;
        result->total_idle_time = 0;
        result->cpu_utilization = 0.0;
        result->avg_waiting_time = 0.0;
        result->avg_turnaround_time = 0.0;
        result->avg_response_time = 0.0;
        return 0;
    }

    int total_busy = 0;
    double sum_wt = 0.0;
    double sum_tat = 0.0;
    double sum_rt = 0.0;
    int max_completion = 0;

    for (int i = 0; i < result->process_count; i++) {
        sched_process_t *p = &result->processes[i];

        if (p->completion_time != SCHED_TIME_UNSET && p->arrival_time != SCHED_TIME_UNSET) {
            p->turnaround_time = p->completion_time - p->arrival_time;
            p->waiting_time = p->turnaround_time - p->burst_time;
        }
        if (p->start_time != SCHED_TIME_UNSET && p->arrival_time != SCHED_TIME_UNSET) {
            p->response_time = p->start_time - p->arrival_time;
        }

        total_busy += p->burst_time;
        sum_wt += (double)p->waiting_time;
        sum_tat += (double)p->turnaround_time;
        sum_rt += (double)p->response_time;

        if (p->completion_time > max_completion) {
            max_completion = p->completion_time;
        }
    }

    int elapsed = max_completion;
    if (result->timeline_count > 0) {
        int last_end = result->timeline[result->timeline_count - 1].end_time;
        if (last_end > elapsed) {
            elapsed = last_end;
        }
    }

    result->total_elapsed_time = elapsed;
    result->total_busy_time = total_busy;
    result->total_idle_time = elapsed - total_busy;
    if (result->total_idle_time < 0) {
        result->total_idle_time = 0;
    }

    if (elapsed > 0) {
        result->cpu_utilization = ((double)total_busy / (double)elapsed) * 100.0;
    } else {
        result->cpu_utilization = 0.0;
    }

    result->avg_waiting_time = sum_wt / (double)result->process_count;
    result->avg_turnaround_time = sum_tat / (double)result->process_count;
    result->avg_response_time = sum_rt / (double)result->process_count;

    return 0;
}

int sched_schedule_fcfs(const sched_process_t *procs, int count, sched_result_t *result) {
    if (sched_validate_inputs(procs, count, result) != 0) {
        return -1;
    }

    sched_result_free(result);

    if (count == 0) {
        return sched_compute_metrics(result);
    }

    result->processes = calloc((size_t)count, sizeof(sched_process_t));
    if (!result->processes) {
        return -1;
    }
    result->process_count = count;

    for (int i = 0; i < count; i++) {
        result->processes[i] = procs[i];
        result->processes[i].remaining_time = procs[i].burst_time;
        result->processes[i].start_time = SCHED_TIME_UNSET;
        result->processes[i].completion_time = SCHED_TIME_UNSET;
        result->processes[i].waiting_time = SCHED_TIME_UNSET;
        result->processes[i].turnaround_time = SCHED_TIME_UNSET;
        result->processes[i].response_time = SCHED_TIME_UNSET;
    }

    bool *completed = calloc((size_t)count, sizeof(bool));
    if (!completed) {
        sched_result_free(result);
        return -1;
    }

    int completed_count = 0;
    int current_time = 0;

    while (completed_count < count) {
        /* Find arrived processes */
        int best_idx = -1;
        for (int i = 0; i < count; i++) {
            if (completed[i] || result->processes[i].arrival_time > current_time) {
                continue;
            }

            if (best_idx == -1) {
                best_idx = i;
            } else {
                const sched_process_t *cur = &result->processes[i];
                const sched_process_t *best = &result->processes[best_idx];

                if (cur->arrival_time < best->arrival_time) {
                    best_idx = i;
                } else if (cur->arrival_time == best->arrival_time) {
                    if (cur->pid < best->pid) {
                        best_idx = i;
                    }
                }
            }
        }

        /* If no process has arrived yet, advance virtual time to next arrival */
        if (best_idx == -1) {
            int next_arrival = -1;
            for (int i = 0; i < count; i++) {
                if (!completed[i]) {
                    if (next_arrival == -1 || result->processes[i].arrival_time < next_arrival) {
                        next_arrival = result->processes[i].arrival_time;
                    }
                }
            }

            if (next_arrival > current_time) {
                if (sched_timeline_append(result, SCHED_PID_IDLE, current_time, next_arrival) != 0) {
                    free(completed);
                    sched_result_free(result);
                    return -1;
                }
                current_time = next_arrival;
            }
            continue;
        }

        /* Schedule the chosen process non-preemptively */
        sched_process_t *p = &result->processes[best_idx];
        p->start_time = current_time;
        p->completion_time = current_time + p->burst_time;
        p->remaining_time = 0;
        p->turnaround_time = p->completion_time - p->arrival_time;
        p->waiting_time = p->turnaround_time - p->burst_time;
        p->response_time = p->start_time - p->arrival_time;

        if (sched_timeline_append(result, p->pid, p->start_time, p->completion_time) != 0) {
            free(completed);
            sched_result_free(result);
            return -1;
        }

        current_time = p->completion_time;
        completed[best_idx] = true;
        completed_count++;
    }

    free(completed);
    return sched_compute_metrics(result);
}

int sched_schedule_sjf(const sched_process_t *procs, int count, sched_result_t *result) {
    if (sched_validate_inputs(procs, count, result) != 0) {
        return -1;
    }

    sched_result_free(result);

    if (count == 0) {
        return sched_compute_metrics(result);
    }

    result->processes = calloc((size_t)count, sizeof(sched_process_t));
    if (!result->processes) {
        return -1;
    }
    result->process_count = count;

    for (int i = 0; i < count; i++) {
        result->processes[i] = procs[i];
        result->processes[i].remaining_time = procs[i].burst_time;
        result->processes[i].start_time = SCHED_TIME_UNSET;
        result->processes[i].completion_time = SCHED_TIME_UNSET;
        result->processes[i].waiting_time = SCHED_TIME_UNSET;
        result->processes[i].turnaround_time = SCHED_TIME_UNSET;
        result->processes[i].response_time = SCHED_TIME_UNSET;
    }

    bool *completed = calloc((size_t)count, sizeof(bool));
    if (!completed) {
        sched_result_free(result);
        return -1;
    }

    int completed_count = 0;
    int current_time = 0;

    while (completed_count < count) {
        /* Find arrived processes */
        int best_idx = -1;
        for (int i = 0; i < count; i++) {
            if (completed[i] || result->processes[i].arrival_time > current_time) {
                continue;
            }

            if (best_idx == -1) {
                best_idx = i;
            } else {
                const sched_process_t *cur = &result->processes[i];
                const sched_process_t *best = &result->processes[best_idx];

                if (cur->burst_time < best->burst_time) {
                    best_idx = i;
                } else if (cur->burst_time == best->burst_time) {
                    if (cur->arrival_time < best->arrival_time) {
                        best_idx = i;
                    } else if (cur->arrival_time == best->arrival_time) {
                        if (cur->pid < best->pid) {
                            best_idx = i;
                        }
                    }
                }
            }
        }

        /* If no process has arrived yet, advance virtual time to next arrival */
        if (best_idx == -1) {
            int next_arrival = -1;
            for (int i = 0; i < count; i++) {
                if (!completed[i]) {
                    if (next_arrival == -1 || result->processes[i].arrival_time < next_arrival) {
                        next_arrival = result->processes[i].arrival_time;
                    }
                }
            }

            if (next_arrival > current_time) {
                if (sched_timeline_append(result, SCHED_PID_IDLE, current_time, next_arrival) != 0) {
                    free(completed);
                    sched_result_free(result);
                    return -1;
                }
                current_time = next_arrival;
            }
            continue;
        }

        /* Schedule the chosen process non-preemptively */
        sched_process_t *p = &result->processes[best_idx];
        p->start_time = current_time;
        p->completion_time = current_time + p->burst_time;
        p->remaining_time = 0;
        p->turnaround_time = p->completion_time - p->arrival_time;
        p->waiting_time = p->turnaround_time - p->burst_time;
        p->response_time = p->start_time - p->arrival_time;

        if (sched_timeline_append(result, p->pid, p->start_time, p->completion_time) != 0) {
            free(completed);
            sched_result_free(result);
            return -1;
        }

        current_time = p->completion_time;
        completed[best_idx] = true;
        completed_count++;
    }

    free(completed);
    return sched_compute_metrics(result);
}

int sched_schedule_priority(const sched_process_t *procs, int count, sched_result_t *result) {
    if (sched_validate_inputs(procs, count, result) != 0) {
        return -1;
    }

    sched_result_free(result);

    if (count == 0) {
        return sched_compute_metrics(result);
    }

    result->processes = calloc((size_t)count, sizeof(sched_process_t));
    if (!result->processes) {
        return -1;
    }
    result->process_count = count;

    for (int i = 0; i < count; i++) {
        result->processes[i] = procs[i];
        result->processes[i].remaining_time = procs[i].burst_time;
        result->processes[i].start_time = SCHED_TIME_UNSET;
        result->processes[i].completion_time = SCHED_TIME_UNSET;
        result->processes[i].waiting_time = SCHED_TIME_UNSET;
        result->processes[i].turnaround_time = SCHED_TIME_UNSET;
        result->processes[i].response_time = SCHED_TIME_UNSET;
    }

    bool *completed = calloc((size_t)count, sizeof(bool));
    if (!completed) {
        sched_result_free(result);
        return -1;
    }

    int completed_count = 0;
    int current_time = 0;

    while (completed_count < count) {
        /* Find arrived processes */
        int best_idx = -1;
        for (int i = 0; i < count; i++) {
            if (completed[i] || result->processes[i].arrival_time > current_time) {
                continue;
            }

            if (best_idx == -1) {
                best_idx = i;
            } else {
                const sched_process_t *cur = &result->processes[i];
                const sched_process_t *best = &result->processes[best_idx];

                /* Lower priority number = higher priority */
                if (cur->priority < best->priority) {
                    best_idx = i;
                } else if (cur->priority == best->priority) {
                    if (cur->arrival_time < best->arrival_time) {
                        best_idx = i;
                    } else if (cur->arrival_time == best->arrival_time) {
                        if (cur->pid < best->pid) {
                            best_idx = i;
                        }
                    }
                }
            }
        }

        /* If no process has arrived yet, advance virtual time to next arrival */
        if (best_idx == -1) {
            int next_arrival = -1;
            for (int i = 0; i < count; i++) {
                if (!completed[i]) {
                    if (next_arrival == -1 || result->processes[i].arrival_time < next_arrival) {
                        next_arrival = result->processes[i].arrival_time;
                    }
                }
            }

            if (next_arrival > current_time) {
                if (sched_timeline_append(result, SCHED_PID_IDLE, current_time, next_arrival) != 0) {
                    free(completed);
                    sched_result_free(result);
                    return -1;
                }
                current_time = next_arrival;
            }
            continue;
        }

        /* Schedule the chosen process non-preemptively */
        sched_process_t *p = &result->processes[best_idx];
        p->start_time = current_time;
        p->completion_time = current_time + p->burst_time;
        p->remaining_time = 0;
        p->turnaround_time = p->completion_time - p->arrival_time;
        p->waiting_time = p->turnaround_time - p->burst_time;
        p->response_time = p->start_time - p->arrival_time;

        if (sched_timeline_append(result, p->pid, p->start_time, p->completion_time) != 0) {
            free(completed);
            sched_result_free(result);
            return -1;
        }

        current_time = p->completion_time;
        completed[best_idx] = true;
        completed_count++;
    }

    free(completed);
    return sched_compute_metrics(result);
}

/*
 * Helper for Round Robin: collect un-enqueued processes arriving <= limit_time,
 * sort them deterministically by (arrival_time, pid), and push to queue.
 */
static void sched_rr_enqueue_arrivals(const sched_result_t *result, int count, int limit_time,
                                      bool *enqueued, int *queue, int *q_tail, int *q_size, int q_cap) {
    int num_new = 0;
    int *new_arr = malloc((size_t)count * sizeof(int));
    if (!new_arr) return;

    for (int i = 0; i < count; i++) {
        if (!enqueued[i] && result->processes[i].arrival_time <= limit_time) {
            new_arr[num_new++] = i;
        }
    }

    /* Sort deterministic arrivals: arrival_time asc, then pid asc */
    for (int i = 0; i < num_new - 1; i++) {
        for (int j = i + 1; j < num_new; j++) {
            int a = new_arr[i];
            int b = new_arr[j];
            bool swap = false;
            if (result->processes[a].arrival_time > result->processes[b].arrival_time) {
                swap = true;
            } else if (result->processes[a].arrival_time == result->processes[b].arrival_time) {
                if (result->processes[a].pid > result->processes[b].pid) {
                    swap = true;
                }
            }
            if (swap) {
                new_arr[i] = b;
                new_arr[j] = a;
            }
        }
    }

    for (int i = 0; i < num_new; i++) {
        int idx = new_arr[i];
        queue[*q_tail] = idx;
        *q_tail = (*q_tail + 1) % q_cap;
        (*q_size)++;
        enqueued[idx] = true;
    }

    free(new_arr);
}

int sched_schedule_rr(const sched_process_t *procs, int count, int quantum, sched_result_t *result) {
    if (quantum <= 0 || sched_validate_inputs(procs, count, result) != 0) {
        return -1;
    }

    sched_result_free(result);

    if (count == 0) {
        return sched_compute_metrics(result);
    }

    result->processes = calloc((size_t)count, sizeof(sched_process_t));
    if (!result->processes) {
        return -1;
    }
    result->process_count = count;

    for (int i = 0; i < count; i++) {
        result->processes[i] = procs[i];
        result->processes[i].remaining_time = procs[i].burst_time;
        result->processes[i].start_time = SCHED_TIME_UNSET;
        result->processes[i].completion_time = SCHED_TIME_UNSET;
        result->processes[i].waiting_time = SCHED_TIME_UNSET;
        result->processes[i].turnaround_time = SCHED_TIME_UNSET;
        result->processes[i].response_time = SCHED_TIME_UNSET;
    }

    int q_cap = count + 4;
    int *queue = malloc((size_t)q_cap * sizeof(int));
    bool *enqueued = calloc((size_t)count, sizeof(bool));
    if (!queue || !enqueued) {
        free(queue);
        free(enqueued);
        sched_result_free(result);
        return -1;
    }

    int q_head = 0;
    int q_tail = 0;
    int q_size = 0;
    int completed_count = 0;
    int current_time = 0;

    /* Enqueue all initial arrivals at t = 0 */
    sched_rr_enqueue_arrivals(result, count, current_time, enqueued, queue, &q_tail, &q_size, q_cap);

    while (completed_count < count) {
        if (q_size == 0) {
            /* Ready queue empty: find next arrival time */
            int next_arrival = -1;
            for (int i = 0; i < count; i++) {
                if (!enqueued[i]) {
                    if (next_arrival == -1 || result->processes[i].arrival_time < next_arrival) {
                        next_arrival = result->processes[i].arrival_time;
                    }
                }
            }

            if (next_arrival > current_time) {
                if (sched_timeline_append(result, SCHED_PID_IDLE, current_time, next_arrival) != 0) {
                    free(queue);
                    free(enqueued);
                    sched_result_free(result);
                    return -1;
                }
                current_time = next_arrival;
            }

            sched_rr_enqueue_arrivals(result, count, current_time, enqueued, queue, &q_tail, &q_size, q_cap);
            continue;
        }

        /* Dequeue next process */
        int cur_idx = queue[q_head];
        q_head = (q_head + 1) % q_cap;
        q_size--;

        sched_process_t *p = &result->processes[cur_idx];
        if (p->start_time == SCHED_TIME_UNSET) {
            p->start_time = current_time;
            p->response_time = p->start_time - p->arrival_time;
        }

        int run_time = (p->remaining_time < quantum) ? p->remaining_time : quantum;
        int end_slice_time = current_time + run_time;

        if (sched_timeline_append(result, p->pid, current_time, end_slice_time) != 0) {
            free(queue);
            free(enqueued);
            sched_result_free(result);
            return -1;
        }

        /*
         * Enqueue any process arriving in (current_time, end_slice_time]
         * BEFORE requeueing p!
         */
        sched_rr_enqueue_arrivals(result, count, end_slice_time, enqueued, queue, &q_tail, &q_size, q_cap);

        current_time = end_slice_time;
        p->remaining_time -= run_time;

        if (p->remaining_time == 0) {
            p->completion_time = current_time;
            p->turnaround_time = p->completion_time - p->arrival_time;
            p->waiting_time = p->turnaround_time - p->burst_time;
            completed_count++;
        } else {
            /* Quantum expired; requeue at back of ready queue */
            queue[q_tail] = cur_idx;
            q_tail = (q_tail + 1) % q_cap;
            q_size++;
        }
    }

    free(queue);
    free(enqueued);
    return sched_compute_metrics(result);
}

/*
 * FIFO Queue helper for MLFQ simulation.
 */
typedef struct {
    int *data;
    int  head;
    int  tail;
    int  size;
    int  cap;
} sched_queue_t;

static int sched_queue_init(sched_queue_t *q, int cap) {
    if (!q || cap <= 0) return -1;
    q->data = malloc((size_t)cap * sizeof(int));
    if (!q->data) return -1;
    q->head = 0;
    q->tail = 0;
    q->size = 0;
    q->cap = cap;
    return 0;
}

static void sched_queue_free(sched_queue_t *q) {
    if (!q) return;
    free(q->data);
    q->data = NULL;
    q->size = 0;
}

static inline bool sched_queue_empty(const sched_queue_t *q) {
    return q->size == 0;
}

static void sched_queue_push_back(sched_queue_t *q, int val) {
    q->data[q->tail] = val;
    q->tail = (q->tail + 1) % q->cap;
    q->size++;
}

static void sched_queue_push_front(sched_queue_t *q, int val) {
    q->head = (q->head - 1 + q->cap) % q->cap;
    q->data[q->head] = val;
    q->size++;
}

static int sched_queue_pop_front(sched_queue_t *q) {
    int val = q->data[q->head];
    q->head = (q->head + 1) % q->cap;
    q->size--;
    return val;
}

/*
 * MLFQ helper: collect newly arrived processes with arrival_time <= limit_time,
 * sort them deterministically by (arrival_time asc, pid asc), and push to Q0.
 */
static void sched_mlfq_enqueue_arrivals(const sched_result_t *result, int count, int limit_time,
                                        bool *arrived, sched_queue_t *q0,
                                        int *allotment, int q0_quantum) {
    int num_new = 0;
    int *new_arr = malloc((size_t)count * sizeof(int));
    if (!new_arr) return;

    for (int i = 0; i < count; i++) {
        if (!arrived[i] && result->processes[i].arrival_time <= limit_time) {
            new_arr[num_new++] = i;
        }
    }

    /* Sort deterministic arrivals: arrival_time asc, then pid asc */
    for (int i = 0; i < num_new - 1; i++) {
        for (int j = i + 1; j < num_new; j++) {
            int a = new_arr[i];
            int b = new_arr[j];
            bool swap = false;
            if (result->processes[a].arrival_time > result->processes[b].arrival_time) {
                swap = true;
            } else if (result->processes[a].arrival_time == result->processes[b].arrival_time) {
                if (result->processes[a].pid > result->processes[b].pid) {
                    swap = true;
                }
            }
            if (swap) {
                new_arr[i] = b;
                new_arr[j] = a;
            }
        }
    }

    for (int i = 0; i < num_new; i++) {
        int idx = new_arr[i];
        sched_queue_push_back(q0, idx);
        allotment[idx] = q0_quantum;
        arrived[idx] = true;
    }

    free(new_arr);
}

/*
 * MLFQ helper: find the earliest arrival time among un-arrived processes.
 * Returns -1 if all processes have already arrived.
 */
static int sched_mlfq_next_arrival(const sched_result_t *result, int count, const bool *arrived) {
    int next_arr = -1;
    for (int i = 0; i < count; i++) {
        if (!arrived[i]) {
            if (next_arr == -1 || result->processes[i].arrival_time < next_arr) {
                next_arr = result->processes[i].arrival_time;
            }
        }
    }
    return next_arr;
}

int sched_schedule_mlfq_custom(const sched_process_t *procs, int count,
                               int q0_quantum, int q1_quantum, int boost_interval,
                               sched_result_t *result) {
    if (q0_quantum <= 0 || q1_quantum <= 0 || boost_interval <= 0 ||
        sched_validate_inputs(procs, count, result) != 0) {
        return -1;
    }

    sched_result_free(result);

    if (count == 0) {
        return sched_compute_metrics(result);
    }

    result->processes = calloc((size_t)count, sizeof(sched_process_t));
    if (!result->processes) {
        return -1;
    }
    result->process_count = count;

    for (int i = 0; i < count; i++) {
        result->processes[i] = procs[i];
        result->processes[i].remaining_time = procs[i].burst_time;
        result->processes[i].start_time = SCHED_TIME_UNSET;
        result->processes[i].completion_time = SCHED_TIME_UNSET;
        result->processes[i].waiting_time = SCHED_TIME_UNSET;
        result->processes[i].turnaround_time = SCHED_TIME_UNSET;
        result->processes[i].response_time = SCHED_TIME_UNSET;
    }

    int q_cap = count + 4;
    sched_queue_t q0, q1, q2;
    if (sched_queue_init(&q0, q_cap) != 0 ||
        sched_queue_init(&q1, q_cap) != 0 ||
        sched_queue_init(&q2, q_cap) != 0) {
        sched_queue_free(&q0);
        sched_queue_free(&q1);
        sched_queue_free(&q2);
        sched_result_free(result);
        return -1;
    }

    bool *arrived = calloc((size_t)count, sizeof(bool));
    int *allotment = calloc((size_t)count, sizeof(int));
    if (!arrived || !allotment) {
        free(arrived);
        free(allotment);
        sched_queue_free(&q0);
        sched_queue_free(&q1);
        sched_queue_free(&q2);
        sched_result_free(result);
        return -1;
    }

    int completed_count = 0;
    int current_time = 0;
    int next_boost_time = boost_interval;

    /* Enqueue initial arrivals at t = 0 */
    sched_mlfq_enqueue_arrivals(result, count, current_time, arrived, &q0, allotment, q0_quantum);

    while (completed_count < count) {
        if (sched_queue_empty(&q0) && sched_queue_empty(&q1) && sched_queue_empty(&q2)) {
            /* All ready queues are empty: advance to earliest upcoming arrival */
            int next_arr = sched_mlfq_next_arrival(result, count, arrived);
            if (next_arr > current_time) {
                if (sched_timeline_append(result, SCHED_PID_IDLE, current_time, next_arr) != 0) {
                    free(arrived);
                    free(allotment);
                    sched_queue_free(&q0);
                    sched_queue_free(&q1);
                    sched_queue_free(&q2);
                    sched_result_free(result);
                    return -1;
                }
                current_time = next_arr;
            }

            /* Advance next_boost_time if idle span crossed boost boundaries */
            while (next_boost_time <= current_time) {
                next_boost_time += boost_interval;
            }

            sched_mlfq_enqueue_arrivals(result, count, current_time, arrived, &q0, allotment, q0_quantum);
            continue;
        }

        /* Select job from highest priority non-empty queue: Q0 > Q1 > Q2 */
        int cur_idx;
        int cur_queue;
        if (!sched_queue_empty(&q0)) {
            cur_idx = sched_queue_pop_front(&q0);
            cur_queue = 0;
        } else if (!sched_queue_empty(&q1)) {
            cur_idx = sched_queue_pop_front(&q1);
            cur_queue = 1;
        } else {
            cur_idx = sched_queue_pop_front(&q2);
            cur_queue = 2;
        }

        sched_process_t *p = &result->processes[cur_idx];
        if (p->start_time == SCHED_TIME_UNSET) {
            p->start_time = current_time;
            p->response_time = p->start_time - p->arrival_time;
        }

        /* Determine nominal execution duration for this slice */
        int desired_run;
        if (cur_queue == 0 || cur_queue == 1) {
            desired_run = (p->remaining_time < allotment[cur_idx]) ? p->remaining_time : allotment[cur_idx];
        } else {
            /* Q2 is FCFS non-preemptive within Q2 */
            desired_run = p->remaining_time;
        }

        int actual_run = desired_run;

        /* Boost boundary constraint: do not silently run past boost boundary */
        if (current_time + actual_run > next_boost_time) {
            actual_run = next_boost_time - current_time;
        }

        /* Higher-queue preemption constraint: if in Q1 or Q2, preempt at next arrival (which enters Q0) */
        if (cur_queue == 1 || cur_queue == 2) {
            int next_arr = sched_mlfq_next_arrival(result, count, arrived);
            if (next_arr != -1 && next_arr > current_time && next_arr < current_time + actual_run) {
                actual_run = next_arr - current_time;
            }
        }

        int end_slice_time = current_time + actual_run;
        if (sched_timeline_append(result, p->pid, current_time, end_slice_time) != 0) {
            free(arrived);
            free(allotment);
            sched_queue_free(&q0);
            sched_queue_free(&q1);
            sched_queue_free(&q2);
            sched_result_free(result);
            return -1;
        }

        current_time = end_slice_time;
        p->remaining_time -= actual_run;
        if (cur_queue == 0 || cur_queue == 1) {
            allotment[cur_idx] -= actual_run;
        }

        if (p->remaining_time == 0) {
            p->completion_time = current_time;
            p->turnaround_time = p->completion_time - p->arrival_time;
            p->waiting_time = p->turnaround_time - p->burst_time;
            completed_count++;
        } else {
            /* Process did not finish: determine requeue or demotion */
            if (cur_queue == 0) {
                if (allotment[cur_idx] == 0) {
                    /* Exhausted Q0 quantum: demote to tail of Q1 */
                    allotment[cur_idx] = q1_quantum;
                    sched_queue_push_back(&q1, cur_idx);
                } else {
                    /* Preempted before quantum expiration (boost boundary): keep at head of Q0 */
                    sched_queue_push_front(&q0, cur_idx);
                }
            } else if (cur_queue == 1) {
                if (allotment[cur_idx] == 0) {
                    /* Exhausted Q1 quantum: demote to tail of Q2 */
                    sched_queue_push_back(&q2, cur_idx);
                } else {
                    /* Preempted before quantum expiration: retain remaining allotment at head of Q1 */
                    sched_queue_push_front(&q1, cur_idx);
                }
            } else {
                /* Q2: preempted by boost or higher-queue arrival, remains at head of Q2 */
                sched_queue_push_front(&q2, cur_idx);
            }
        }

        /* Enqueue any new arrivals up to current_time into Q0 (before lower queues run) */
        sched_mlfq_enqueue_arrivals(result, count, current_time, arrived, &q0, allotment, q0_quantum);

        /* Process Priority Boost if current_time reached boost boundary */
        if (current_time == next_boost_time) {
            /* Move all processes in Q1 to Q0 in FIFO order */
            while (!sched_queue_empty(&q1)) {
                int idx = sched_queue_pop_front(&q1);
                sched_queue_push_back(&q0, idx);
                allotment[idx] = q0_quantum;
            }
            /* Move all processes in Q2 to Q0 in FIFO order */
            while (!sched_queue_empty(&q2)) {
                int idx = sched_queue_pop_front(&q2);
                sched_queue_push_back(&q0, idx);
                allotment[idx] = q0_quantum;
            }
            /* Reset quantum for any processes already in Q0 */
            for (int i = 0; i < count; i++) {
                if (arrived[i] && result->processes[i].remaining_time > 0) {
                    allotment[i] = q0_quantum;
                }
            }

            next_boost_time += boost_interval;
        }
    }

    free(arrived);
    free(allotment);
    sched_queue_free(&q0);
    sched_queue_free(&q1);
    sched_queue_free(&q2);

    return sched_compute_metrics(result);
}

int sched_schedule_mlfq(const sched_process_t *procs, int count, sched_result_t *result) {
    return sched_schedule_mlfq_custom(procs, count,
                                      SCHED_MLFQ_Q0_QUANTUM,
                                      SCHED_MLFQ_Q1_QUANTUM,
                                      SCHED_MLFQ_BOOST_INTERVAL,
                                      result);
}

const sched_process_t *sched_result_get_proc(const sched_result_t *result, int pid) {
    if (!result || !result->processes) {
        return NULL;
    }
    for (int i = 0; i < result->process_count; i++) {
        if (result->processes[i].pid == pid) {
            return &result->processes[i];
        }
    }
    return NULL;
}

static int sched_digits(int n) {
    if (n < 0) n = -n;
    if (n == 0) return 1;
    int d = 0;
    while (n > 0) {
        d++;
        n /= 10;
    }
    return d;
}

int sched_render_gantt_wrapped_buf(const sched_result_t *result, char *buf, size_t size, int max_cols) {
    if (!result) {
        return -1;
    }
    if (result->timeline_count <= 0) {
        if (buf && size > 0) {
            buf[0] = '\0';
        }
        return 0;
    }

    if (max_cols < 20) {
        max_cols = 20;
    }

    int n = result->timeline_count;
    int *widths = malloc((size_t)n * sizeof(int));
    if (!widths) {
        return -1;
    }

    for (int i = 0; i < n; i++) {
        char label[32];
        if (result->timeline[i].pid == SCHED_PID_IDLE) {
            snprintf(label, sizeof(label), "IDLE");
        } else {
            snprintf(label, sizeof(label), "P%d", result->timeline[i].pid);
        }
        int label_len = (int)strlen(label);
        int d_start = sched_digits(result->timeline[i].start_time);
        int d_end = sched_digits(result->timeline[i].end_time);

        int w = 4;
        if (label_len + 2 > w) w = label_len + 2;
        if (d_start + 1 > w) w = d_start + 1;
        if (d_end + 1 > w) w = d_end + 1;

        widths[i] = w;
    }

    /* Partition timeline into rows of <= max_cols */
    int *row_starts = malloc(((size_t)n + 1) * sizeof(int));
    int *row_ends = malloc(((size_t)n + 1) * sizeof(int));
    if (!row_starts || !row_ends) {
        free(widths);
        free(row_starts);
        free(row_ends);
        return -1;
    }

    int num_rows = 0;
    int cur_start = 0;
    int cur_cols = 1; /* Opening '|' */

    for (int i = 0; i < n; i++) {
        int slice_cols = widths[i] + 1; /* content + '|' */
        if (cur_cols + slice_cols > max_cols && i > cur_start) {
            row_starts[num_rows] = cur_start;
            row_ends[num_rows] = i - 1;
            num_rows++;
            cur_start = i;
            cur_cols = 1 + slice_cols;
        } else {
            cur_cols += slice_cols;
        }
    }
    row_starts[num_rows] = cur_start;
    row_ends[num_rows] = n - 1;
    num_rows++;

    int total_chars = 0;
    size_t write_pos = 0;

    for (int r = 0; r < num_rows; r++) {
        int s = row_starts[r];
        int e = row_ends[r];
        int m = e - s + 1; /* Slices in this row */

        int *bars = malloc(((size_t)m + 1) * sizeof(int));
        if (!bars) {
            free(widths);
            free(row_starts);
            free(row_ends);
            return -1;
        }

        bars[0] = 0;
        for (int j = 0; j < m; j++) {
            bars[j + 1] = bars[j] + 1 + widths[s + j];
        }

        int total_bar_cols = bars[m] + 1;
        int line1_len = total_bar_cols + 1;

        int last_end_digits = sched_digits(result->timeline[e].end_time);
        int line2_buf_len = bars[m] + last_end_digits + 2;

        char *l1 = malloc((size_t)line1_len + 2);
        char *l2 = malloc((size_t)line2_buf_len + 2);
        if (!l1 || !l2) {
            free(bars);
            free(l1);
            free(l2);
            free(widths);
            free(row_starts);
            free(row_ends);
            return -1;
        }

        /* Build Line 1 */
        memset(l1, ' ', (size_t)total_bar_cols);
        for (int j = 0; j <= m; j++) {
            l1[bars[j]] = '|';
        }
        for (int j = 0; j < m; j++) {
            int idx = s + j;
            char label[32];
            if (result->timeline[idx].pid == SCHED_PID_IDLE) {
                snprintf(label, sizeof(label), "IDLE");
            } else {
                snprintf(label, sizeof(label), "P%d", result->timeline[idx].pid);
            }
            int label_len = (int)strlen(label);
            int pad = (widths[idx] - label_len) / 2;
            int start_col = bars[j] + 1 + pad;
            memcpy(&l1[start_col], label, (size_t)label_len);
        }
        l1[total_bar_cols] = '\n';
        l1[total_bar_cols + 1] = '\0';

        /* Build Line 2 */
        memset(l2, ' ', (size_t)(bars[m] + last_end_digits + 1));
        for (int j = 0; j < m; j++) {
            int idx = s + j;
            char num[32];
            int nlen = snprintf(num, sizeof(num), "%d", result->timeline[idx].start_time);
            memcpy(&l2[bars[j]], num, (size_t)nlen);
        }
        char end_num[32];
        int end_nlen = snprintf(end_num, sizeof(end_num), "%d", result->timeline[e].end_time);
        memcpy(&l2[bars[m]], end_num, (size_t)end_nlen);

        int l2_end = bars[m] + end_nlen;
        while (l2_end > 0 && l2[l2_end - 1] == ' ') {
            l2_end--;
        }
        l2[l2_end] = '\n';
        l2[l2_end + 1] = '\0';

        int l1_actual = (int)strlen(l1);
        int l2_actual = (int)strlen(l2);
        int extra_nl = (r < num_rows - 1) ? 1 : 0;

        total_chars += l1_actual + l2_actual + extra_nl;

        if (buf && size > 0) {
            if (write_pos < size - 1) {
                size_t space = size - 1 - write_pos;
                size_t c1 = ((size_t)l1_actual < space) ? (size_t)l1_actual : space;
                memcpy(buf + write_pos, l1, c1);
                write_pos += c1;
            }
            if (write_pos < size - 1) {
                size_t space = size - 1 - write_pos;
                size_t c2 = ((size_t)l2_actual < space) ? (size_t)l2_actual : space;
                memcpy(buf + write_pos, l2, c2);
                write_pos += c2;
            }
            if (extra_nl && write_pos < size - 1) {
                buf[write_pos++] = '\n';
            }
            buf[write_pos] = '\0';
        }

        free(bars);
        free(l1);
        free(l2);
    }

    free(widths);
    free(row_starts);
    free(row_ends);

    return total_chars;
}

int sched_render_gantt_buf(const sched_result_t *result, char *buf, size_t size) {
    return sched_render_gantt_wrapped_buf(result, buf, size, SCHED_GANTT_DEFAULT_MAX_COLS);
}

char *sched_render_gantt_wrapped(const sched_result_t *result, int max_cols) {
    if (!result) return NULL;
    if (result->timeline_count == 0) {
        char *empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }

    int len = sched_render_gantt_wrapped_buf(result, NULL, 0, max_cols);
    if (len < 0) return NULL;

    char *buf = malloc((size_t)len + 16);
    if (!buf) return NULL;

    if (sched_render_gantt_wrapped_buf(result, buf, (size_t)len + 16, max_cols) < 0) {
        free(buf);
        return NULL;
    }
    return buf;
}

char *sched_render_gantt(const sched_result_t *result) {
    return sched_render_gantt_wrapped(result, SCHED_GANTT_DEFAULT_MAX_COLS);
}

void sched_compare_result_init(sched_compare_result_t *cmp) {
    if (!cmp) return;
    memset(cmp, 0, sizeof(*cmp));
    sched_result_init(&cmp->fcfs);
    sched_result_init(&cmp->sjf);
    sched_result_init(&cmp->rr);
    sched_result_init(&cmp->priority);
    sched_result_init(&cmp->mlfq);
    cmp->rr_quantum = SCHED_DEFAULT_RR_QUANTUM;
}

void sched_compare_result_free(sched_compare_result_t *cmp) {
    if (!cmp) return;
    sched_result_free(&cmp->fcfs);
    sched_result_free(&cmp->sjf);
    sched_result_free(&cmp->rr);
    sched_result_free(&cmp->priority);
    sched_result_free(&cmp->mlfq);
    sched_compare_result_init(cmp);
}

int sched_compare_all(const sched_process_t *procs, int count, int rr_quantum,
                      sched_compare_result_t *cmp) {
    if (!cmp || !procs || count <= 0 || rr_quantum <= 0) {
        return -1;
    }

    sched_compare_result_init(cmp);
    cmp->rr_quantum = rr_quantum;
    cmp->process_count = count;

    int total_burst = 0;
    for (int i = 0; i < count; i++) {
        if (procs[i].burst_time <= 0 || procs[i].arrival_time < 0 || procs[i].pid < 0) {
            return -1;
        }
        total_burst += procs[i].burst_time;
    }
    cmp->total_burst_time = total_burst;

    /* Execute each algorithm on independent copies */
    if (sched_schedule_fcfs(procs, count, &cmp->fcfs) != 0) {
        sched_compare_result_free(cmp);
        return -1;
    }
    if (sched_schedule_sjf(procs, count, &cmp->sjf) != 0) {
        sched_compare_result_free(cmp);
        return -1;
    }
    if (sched_schedule_rr(procs, count, rr_quantum, &cmp->rr) != 0) {
        sched_compare_result_free(cmp);
        return -1;
    }
    if (sched_schedule_priority(procs, count, &cmp->priority) != 0) {
        sched_compare_result_free(cmp);
        return -1;
    }
    if (sched_schedule_mlfq(procs, count, &cmp->mlfq) != 0) {
        sched_compare_result_free(cmp);
        return -1;
    }

    return 0;
}

int sched_render_comparison_buf(const sched_compare_result_t *cmp, char *buf, size_t size) {
    if (!cmp) {
        return -1;
    }

    char rr_name[64];
    snprintf(rr_name, sizeof(rr_name), "Round Robin (RR, Q=%d)", cmp->rr_quantum);

    char tmp[4096];
    int offset = snprintf(tmp, sizeof(tmp),
        "SmartShell CPU Scheduling Algorithm Comparison\n"
        "Workload: %d processes (Total Workload Burst: %d ticks)\n\n"
        "-------------------------------------------------------------------------------------------------------\n"
        "Algorithm                         Avg Waiting  Avg Turnaround  Avg Response  CPU Util  Elapsed Busy Idle\n"
        "-------------------------------------------------------------------------------------------------------\n"
        "%-32s %12.2f %15.2f %13.2f %8.2f%% %8d %4d %4d\n"
        "%-32s %12.2f %15.2f %13.2f %8.2f%% %8d %4d %4d\n"
        "%-32s %12.2f %15.2f %13.2f %8.2f%% %8d %4d %4d\n"
        "%-32s %12.2f %15.2f %13.2f %8.2f%% %8d %4d %4d\n"
        "%-32s %12.2f %15.2f %13.2f %8.2f%% %8d %4d %4d\n"
        "-------------------------------------------------------------------------------------------------------\n\n"
        "Simulation Parameters & Policy:\n"
        "  Round Robin Quantum:     %d ticks\n"
        "  MLFQ Queue Architecture: Q0 (RR, Q=%d) -> Q1 (RR, Q=%d) -> Q2 (FCFS)\n"
        "  MLFQ Priority Boost:     Starvation prevention boost every %d ticks\n"
        "  Total Workload Busy:     %d ticks\n",
        cmp->process_count, cmp->total_burst_time,
        "First-Come, First-Served (FCFS)",
        cmp->fcfs.avg_waiting_time, cmp->fcfs.avg_turnaround_time, cmp->fcfs.avg_response_time,
        cmp->fcfs.cpu_utilization, cmp->fcfs.total_elapsed_time, cmp->fcfs.total_busy_time, cmp->fcfs.total_idle_time,
        "Shortest Job First (SJF)",
        cmp->sjf.avg_waiting_time, cmp->sjf.avg_turnaround_time, cmp->sjf.avg_response_time,
        cmp->sjf.cpu_utilization, cmp->sjf.total_elapsed_time, cmp->sjf.total_busy_time, cmp->sjf.total_idle_time,
        rr_name,
        cmp->rr.avg_waiting_time, cmp->rr.avg_turnaround_time, cmp->rr.avg_response_time,
        cmp->rr.cpu_utilization, cmp->rr.total_elapsed_time, cmp->rr.total_busy_time, cmp->rr.total_idle_time,
        "Non-Preemptive Priority",
        cmp->priority.avg_waiting_time, cmp->priority.avg_turnaround_time, cmp->priority.avg_response_time,
        cmp->priority.cpu_utilization, cmp->priority.total_elapsed_time, cmp->priority.total_busy_time, cmp->priority.total_idle_time,
        "Multilevel Feedback Queue (MLFQ)",
        cmp->mlfq.avg_waiting_time, cmp->mlfq.avg_turnaround_time, cmp->mlfq.avg_response_time,
        cmp->mlfq.cpu_utilization, cmp->mlfq.total_elapsed_time, cmp->mlfq.total_busy_time, cmp->mlfq.total_idle_time,
        cmp->rr_quantum,
        SCHED_MLFQ_Q0_QUANTUM, SCHED_MLFQ_Q1_QUANTUM,
        SCHED_MLFQ_BOOST_INTERVAL,
        cmp->total_burst_time);

    if (offset < 0) {
        return -1;
    }

    if (buf && size > 0) {
        snprintf(buf, size, "%s", tmp);
    }

    return offset;
}

char *sched_render_comparison(const sched_compare_result_t *cmp) {
    if (!cmp) return NULL;
    int len = sched_render_comparison_buf(cmp, NULL, 0);
    if (len < 0) return NULL;

    char *buf = malloc((size_t)len + 16);
    if (!buf) return NULL;

    if (sched_render_comparison_buf(cmp, buf, (size_t)len + 16) < 0) {
        free(buf);
        return NULL;
    }
    return buf;
}
