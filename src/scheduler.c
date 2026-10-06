/*
 * scheduler.c — User-space CPU scheduling simulation implementation.
 *
 * Implements deterministic FCFS and non-preemptive SJF algorithms along
 * with execution timeline tracking and metric computation.
 */

#include "scheduler.h"

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
