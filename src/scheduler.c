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

int sched_render_gantt_buf(const sched_result_t *result, char *buf, size_t size) {
    if (!result) {
        return -1;
    }
    if (result->timeline_count <= 0) {
        if (buf && size > 0) {
            buf[0] = '\0';
        }
        return 0;
    }

    int n = result->timeline_count;
    int *widths = malloc((size_t)n * sizeof(int));
    int *bars = malloc((size_t)(n + 1) * sizeof(int));
    if (!widths || !bars) {
        free(widths);
        free(bars);
        return -1;
    }

    bars[0] = 0;
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
        bars[i + 1] = bars[i] + 1 + w;
    }

    int total_bar_cols = bars[n] + 1;
    int line1_len = total_bar_cols + 1; /* bar string + \n */

    int last_end_digits = sched_digits(result->timeline[n - 1].end_time);
    int line2_buf_len = bars[n] + last_end_digits + 2;

    int total_len = line1_len + line2_buf_len;

    if (buf && size > 0) {
        char *l1 = malloc((size_t)line1_len + 2);
        char *l2 = malloc((size_t)line2_buf_len + 2);
        if (!l1 || !l2) {
            free(widths);
            free(bars);
            free(l1);
            free(l2);
            return -1;
        }

        memset(l1, ' ', (size_t)total_bar_cols);
        for (int i = 0; i <= n; i++) {
            l1[bars[i]] = '|';
        }
        for (int i = 0; i < n; i++) {
            char label[32];
            if (result->timeline[i].pid == SCHED_PID_IDLE) {
                snprintf(label, sizeof(label), "IDLE");
            } else {
                snprintf(label, sizeof(label), "P%d", result->timeline[i].pid);
            }
            int label_len = (int)strlen(label);
            int pad = (widths[i] - label_len) / 2;
            int start_col = bars[i] + 1 + pad;
            memcpy(&l1[start_col], label, (size_t)label_len);
        }
        l1[total_bar_cols] = '\n';
        l1[total_bar_cols + 1] = '\0';

        memset(l2, ' ', (size_t)(bars[n] + last_end_digits + 1));
        for (int i = 0; i < n; i++) {
            char num[32];
            int nlen = snprintf(num, sizeof(num), "%d", result->timeline[i].start_time);
            memcpy(&l2[bars[i]], num, (size_t)nlen);
        }
        char end_num[32];
        int end_nlen = snprintf(end_num, sizeof(end_num), "%d", result->timeline[n - 1].end_time);
        memcpy(&l2[bars[n]], end_num, (size_t)end_nlen);

        int l2_end = bars[n] + end_nlen;
        while (l2_end > 0 && l2[l2_end - 1] == ' ') {
            l2_end--;
        }
        l2[l2_end] = '\n';
        l2[l2_end + 1] = '\0';

        int written = snprintf(buf, size, "%s%s", l1, l2);
        free(l1);
        free(l2);
        free(widths);
        free(bars);
        return written;
    }

    free(widths);
    free(bars);
    return total_len;
}

char *sched_render_gantt(const sched_result_t *result) {
    if (!result) return NULL;
    if (result->timeline_count == 0) {
        char *empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }

    int len = sched_render_gantt_buf(result, NULL, 0);
    if (len < 0) return NULL;

    char *buf = malloc((size_t)len + 16);
    if (!buf) return NULL;

    if (sched_render_gantt_buf(result, buf, (size_t)len + 16) < 0) {
        free(buf);
        return NULL;
    }
    return buf;
}
