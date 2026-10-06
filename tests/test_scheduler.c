/*
 * test_scheduler.c — Unit tests for Phase 8A & 8B user-space CPU scheduling simulation.
 */

#include "scheduler.h"
#include "test_framework.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_DOUBLE_NEAR(exp, act, eps) do { \
    double _diff = fabs((double)(exp) - (double)(act)); \
    if (_diff > (eps)) { \
        fprintf(stderr, "    ASSERT_DOUBLE_NEAR failed at %s:%d: expected %f, got %f (diff %f > %f)\n", \
                __FILE__, __LINE__, (double)(exp), (double)(act), _diff, (double)(eps)); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define EPSILON 0.0001

/* ------------------------------------------------------------------------- */
/* A. FCFS Tests (Phase 8A)                                                  */
/* ------------------------------------------------------------------------- */

static void test_fcfs_single_process_zero_arrival(void) {
    sched_process_t procs[1];
    sched_process_init(&procs[0], 1, 0, 5, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_fcfs(procs, 1, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(1, res.process_count);
    ASSERT_EQ_INT(1, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(5, res.timeline[0].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_NOT_NULL(p1);
    ASSERT_EQ_INT(0, p1->start_time);
    ASSERT_EQ_INT(5, p1->completion_time);
    ASSERT_EQ_INT(0, p1->remaining_time);
    ASSERT_EQ_INT(5, p1->turnaround_time);
    ASSERT_EQ_INT(0, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    ASSERT_EQ_INT(5, res.total_elapsed_time);
    ASSERT_EQ_INT(5, res.total_busy_time);
    ASSERT_EQ_INT(0, res.total_idle_time);
    ASSERT_DOUBLE_NEAR(100.0, res.cpu_utilization, EPSILON);
    ASSERT_DOUBLE_NEAR(5.0, res.avg_turnaround_time, EPSILON);
    ASSERT_DOUBLE_NEAR(0.0, res.avg_waiting_time, EPSILON);
    ASSERT_DOUBLE_NEAR(0.0, res.avg_response_time, EPSILON);

    sched_result_free(&res);
}

static void test_fcfs_single_process_nonzero_arrival(void) {
    sched_process_t procs[1];
    sched_process_init(&procs[0], 1, 4, 3, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_fcfs(procs, 1, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(1, res.process_count);
    ASSERT_EQ_INT(2, res.timeline_count);

    ASSERT_EQ_INT(SCHED_PID_IDLE, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(4, res.timeline[0].end_time);

    ASSERT_EQ_INT(1, res.timeline[1].pid);
    ASSERT_EQ_INT(4, res.timeline[1].start_time);
    ASSERT_EQ_INT(7, res.timeline[1].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_NOT_NULL(p1);
    ASSERT_EQ_INT(4, p1->start_time);
    ASSERT_EQ_INT(7, p1->completion_time);
    ASSERT_EQ_INT(0, p1->remaining_time);
    ASSERT_EQ_INT(3, p1->turnaround_time);
    ASSERT_EQ_INT(0, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    ASSERT_EQ_INT(7, res.total_elapsed_time);
    ASSERT_EQ_INT(3, res.total_busy_time);
    ASSERT_EQ_INT(4, res.total_idle_time);
    ASSERT_DOUBLE_NEAR((3.0 / 7.0) * 100.0, res.cpu_utilization, EPSILON);
    ASSERT_DOUBLE_NEAR(3.0, res.avg_turnaround_time, EPSILON);
    ASSERT_DOUBLE_NEAR(0.0, res.avg_waiting_time, EPSILON);
    ASSERT_DOUBLE_NEAR(0.0, res.avg_response_time, EPSILON);

    sched_result_free(&res);
}

static void test_fcfs_multiple_arriving_at_zero_tiebreak(void) {
    sched_process_t procs[3];
    sched_process_init(&procs[0], 2, 0, 4, 0);
    sched_process_init(&procs[1], 1, 0, 3, 0);
    sched_process_init(&procs[2], 3, 0, 2, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_fcfs(procs, 3, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(3, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(3, res.timeline[0].end_time);

    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(3, res.timeline[1].start_time);
    ASSERT_EQ_INT(7, res.timeline[1].end_time);

    ASSERT_EQ_INT(3, res.timeline[2].pid);
    ASSERT_EQ_INT(7, res.timeline[2].start_time);
    ASSERT_EQ_INT(9, res.timeline[2].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_NOT_NULL(p1);
    ASSERT_EQ_INT(0, p1->start_time);
    ASSERT_EQ_INT(3, p1->completion_time);
    ASSERT_EQ_INT(3, p1->turnaround_time);
    ASSERT_EQ_INT(0, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_NOT_NULL(p2);
    ASSERT_EQ_INT(3, p2->start_time);
    ASSERT_EQ_INT(7, p2->completion_time);
    ASSERT_EQ_INT(7, p2->turnaround_time);
    ASSERT_EQ_INT(3, p2->waiting_time);
    ASSERT_EQ_INT(3, p2->response_time);

    const sched_process_t *p3 = sched_result_get_proc(&res, 3);
    ASSERT_NOT_NULL(p3);
    ASSERT_EQ_INT(7, p3->start_time);
    ASSERT_EQ_INT(9, p3->completion_time);
    ASSERT_EQ_INT(9, p3->turnaround_time);
    ASSERT_EQ_INT(7, p3->waiting_time);
    ASSERT_EQ_INT(7, p3->response_time);

    ASSERT_EQ_INT(9, res.total_elapsed_time);
    ASSERT_EQ_INT(9, res.total_busy_time);
    ASSERT_EQ_INT(0, res.total_idle_time);
    ASSERT_DOUBLE_NEAR(100.0, res.cpu_utilization, EPSILON);
    ASSERT_DOUBLE_NEAR(19.0 / 3.0, res.avg_turnaround_time, EPSILON);
    ASSERT_DOUBLE_NEAR(10.0 / 3.0, res.avg_waiting_time, EPSILON);
    ASSERT_DOUBLE_NEAR(10.0 / 3.0, res.avg_response_time, EPSILON);

    sched_result_free(&res);
}

static void test_fcfs_staggered_arrivals(void) {
    sched_process_t procs[3];
    sched_process_init(&procs[0], 1, 0, 6, 0);
    sched_process_init(&procs[1], 2, 2, 4, 0);
    sched_process_init(&procs[2], 3, 4, 3, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_fcfs(procs, 3, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(3, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(6, res.timeline[0].end_time);

    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(6, res.timeline[1].start_time);
    ASSERT_EQ_INT(10, res.timeline[1].end_time);

    ASSERT_EQ_INT(3, res.timeline[2].pid);
    ASSERT_EQ_INT(10, res.timeline[2].start_time);
    ASSERT_EQ_INT(13, res.timeline[2].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(0, p1->start_time);
    ASSERT_EQ_INT(6, p1->completion_time);
    ASSERT_EQ_INT(6, p1->turnaround_time);
    ASSERT_EQ_INT(0, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(6, p2->start_time);
    ASSERT_EQ_INT(10, p2->completion_time);
    ASSERT_EQ_INT(8, p2->turnaround_time);
    ASSERT_EQ_INT(4, p2->waiting_time);
    ASSERT_EQ_INT(4, p2->response_time);

    const sched_process_t *p3 = sched_result_get_proc(&res, 3);
    ASSERT_EQ_INT(10, p3->start_time);
    ASSERT_EQ_INT(13, p3->completion_time);
    ASSERT_EQ_INT(9, p3->turnaround_time);
    ASSERT_EQ_INT(6, p3->waiting_time);
    ASSERT_EQ_INT(6, p3->response_time);

    ASSERT_EQ_INT(13, res.total_elapsed_time);
    ASSERT_EQ_INT(13, res.total_busy_time);
    ASSERT_EQ_INT(0, res.total_idle_time);
    ASSERT_DOUBLE_NEAR(100.0, res.cpu_utilization, EPSILON);
    ASSERT_DOUBLE_NEAR(23.0 / 3.0, res.avg_turnaround_time, EPSILON);
    ASSERT_DOUBLE_NEAR(10.0 / 3.0, res.avg_waiting_time, EPSILON);
    ASSERT_DOUBLE_NEAR(10.0 / 3.0, res.avg_response_time, EPSILON);

    sched_result_free(&res);
}

static void test_fcfs_idle_gaps_between_processes(void) {
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 1, 4, 0);
    sched_process_init(&procs[1], 2, 8, 3, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_fcfs(procs, 2, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(4, res.timeline_count);

    ASSERT_EQ_INT(SCHED_PID_IDLE, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(1, res.timeline[0].end_time);

    ASSERT_EQ_INT(1, res.timeline[1].pid);
    ASSERT_EQ_INT(1, res.timeline[1].start_time);
    ASSERT_EQ_INT(5, res.timeline[1].end_time);

    ASSERT_EQ_INT(SCHED_PID_IDLE, res.timeline[2].pid);
    ASSERT_EQ_INT(5, res.timeline[2].start_time);
    ASSERT_EQ_INT(8, res.timeline[2].end_time);

    ASSERT_EQ_INT(2, res.timeline[3].pid);
    ASSERT_EQ_INT(8, res.timeline[3].start_time);
    ASSERT_EQ_INT(11, res.timeline[3].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(1, p1->start_time);
    ASSERT_EQ_INT(5, p1->completion_time);
    ASSERT_EQ_INT(4, p1->turnaround_time);
    ASSERT_EQ_INT(0, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(8, p2->start_time);
    ASSERT_EQ_INT(11, p2->completion_time);
    ASSERT_EQ_INT(3, p2->turnaround_time);
    ASSERT_EQ_INT(0, p2->waiting_time);
    ASSERT_EQ_INT(0, p2->response_time);

    ASSERT_EQ_INT(11, res.total_elapsed_time);
    ASSERT_EQ_INT(7, res.total_busy_time);
    ASSERT_EQ_INT(4, res.total_idle_time);
    ASSERT_DOUBLE_NEAR((7.0 / 11.0) * 100.0, res.cpu_utilization, EPSILON);
    ASSERT_DOUBLE_NEAR(3.5, res.avg_turnaround_time, EPSILON);
    ASSERT_DOUBLE_NEAR(0.0, res.avg_waiting_time, EPSILON);
    ASSERT_DOUBLE_NEAR(0.0, res.avg_response_time, EPSILON);

    sched_result_free(&res);
}

/* ------------------------------------------------------------------------- */
/* B. SJF Tests (Phase 8A)                                                   */
/* ------------------------------------------------------------------------- */

static void test_sjf_different_burst_lengths(void) {
    sched_process_t procs[4];
    sched_process_init(&procs[0], 1, 0, 7, 0);
    sched_process_init(&procs[1], 2, 0, 2, 0);
    sched_process_init(&procs[2], 3, 0, 5, 0);
    sched_process_init(&procs[3], 4, 0, 1, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_sjf(procs, 4, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(4, res.timeline_count);

    ASSERT_EQ_INT(4, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(1, res.timeline[0].end_time);

    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(1, res.timeline[1].start_time);
    ASSERT_EQ_INT(3, res.timeline[1].end_time);

    ASSERT_EQ_INT(3, res.timeline[2].pid);
    ASSERT_EQ_INT(3, res.timeline[2].start_time);
    ASSERT_EQ_INT(8, res.timeline[2].end_time);

    ASSERT_EQ_INT(1, res.timeline[3].pid);
    ASSERT_EQ_INT(8, res.timeline[3].start_time);
    ASSERT_EQ_INT(15, res.timeline[3].end_time);

    const sched_process_t *p4 = sched_result_get_proc(&res, 4);
    ASSERT_EQ_INT(0, p4->start_time);
    ASSERT_EQ_INT(1, p4->completion_time);
    ASSERT_EQ_INT(1, p4->turnaround_time);
    ASSERT_EQ_INT(0, p4->waiting_time);
    ASSERT_EQ_INT(0, p4->response_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(1, p2->start_time);
    ASSERT_EQ_INT(3, p2->completion_time);
    ASSERT_EQ_INT(3, p2->turnaround_time);
    ASSERT_EQ_INT(1, p2->waiting_time);
    ASSERT_EQ_INT(1, p2->response_time);

    const sched_process_t *p3 = sched_result_get_proc(&res, 3);
    ASSERT_EQ_INT(3, p3->start_time);
    ASSERT_EQ_INT(8, p3->completion_time);
    ASSERT_EQ_INT(8, p3->turnaround_time);
    ASSERT_EQ_INT(3, p3->waiting_time);
    ASSERT_EQ_INT(3, p3->response_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(8, p1->start_time);
    ASSERT_EQ_INT(15, p1->completion_time);
    ASSERT_EQ_INT(15, p1->turnaround_time);
    ASSERT_EQ_INT(8, p1->waiting_time);
    ASSERT_EQ_INT(8, p1->response_time);

    ASSERT_EQ_INT(15, res.total_elapsed_time);
    ASSERT_EQ_INT(15, res.total_busy_time);
    ASSERT_EQ_INT(0, res.total_idle_time);
    ASSERT_DOUBLE_NEAR(100.0, res.cpu_utilization, EPSILON);
    ASSERT_DOUBLE_NEAR(27.0 / 4.0, res.avg_turnaround_time, EPSILON);
    ASSERT_DOUBLE_NEAR(12.0 / 4.0, res.avg_waiting_time, EPSILON);
    ASSERT_DOUBLE_NEAR(12.0 / 4.0, res.avg_response_time, EPSILON);

    sched_result_free(&res);
}

static void test_sjf_equal_bursts_and_tie_breaking(void) {
    sched_process_t procs[3];
    sched_process_init(&procs[0], 1, 2, 4, 0);
    sched_process_init(&procs[1], 2, 1, 4, 0);
    sched_process_init(&procs[2], 3, 1, 4, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_sjf(procs, 3, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(4, res.timeline_count);

    ASSERT_EQ_INT(SCHED_PID_IDLE, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(1, res.timeline[0].end_time);

    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(1, res.timeline[1].start_time);
    ASSERT_EQ_INT(5, res.timeline[1].end_time);

    ASSERT_EQ_INT(3, res.timeline[2].pid);
    ASSERT_EQ_INT(5, res.timeline[2].start_time);
    ASSERT_EQ_INT(9, res.timeline[2].end_time);

    ASSERT_EQ_INT(1, res.timeline[3].pid);
    ASSERT_EQ_INT(9, res.timeline[3].start_time);
    ASSERT_EQ_INT(13, res.timeline[3].end_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(1, p2->start_time);
    ASSERT_EQ_INT(5, p2->completion_time);
    ASSERT_EQ_INT(4, p2->turnaround_time);
    ASSERT_EQ_INT(0, p2->waiting_time);
    ASSERT_EQ_INT(0, p2->response_time);

    const sched_process_t *p3 = sched_result_get_proc(&res, 3);
    ASSERT_EQ_INT(5, p3->start_time);
    ASSERT_EQ_INT(9, p3->completion_time);
    ASSERT_EQ_INT(8, p3->turnaround_time);
    ASSERT_EQ_INT(4, p3->waiting_time);
    ASSERT_EQ_INT(4, p3->response_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(9, p1->start_time);
    ASSERT_EQ_INT(13, p1->completion_time);
    ASSERT_EQ_INT(11, p1->turnaround_time);
    ASSERT_EQ_INT(7, p1->waiting_time);
    ASSERT_EQ_INT(7, p1->response_time);

    ASSERT_EQ_INT(13, res.total_elapsed_time);
    ASSERT_EQ_INT(12, res.total_busy_time);
    ASSERT_EQ_INT(1, res.total_idle_time);
    ASSERT_DOUBLE_NEAR((12.0 / 13.0) * 100.0, res.cpu_utilization, EPSILON);
    ASSERT_DOUBLE_NEAR(23.0 / 3.0, res.avg_turnaround_time, EPSILON);
    ASSERT_DOUBLE_NEAR(11.0 / 3.0, res.avg_waiting_time, EPSILON);
    ASSERT_DOUBLE_NEAR(11.0 / 3.0, res.avg_response_time, EPSILON);

    sched_result_free(&res);
}

static void test_sjf_non_preemptive_staggered_arrivals(void) {
    sched_process_t procs[3];
    sched_process_init(&procs[0], 1, 0, 8, 0);
    sched_process_init(&procs[1], 2, 1, 2, 0);
    sched_process_init(&procs[2], 3, 2, 3, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_sjf(procs, 3, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(3, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(8, res.timeline[0].end_time);

    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(8, res.timeline[1].start_time);
    ASSERT_EQ_INT(10, res.timeline[1].end_time);

    ASSERT_EQ_INT(3, res.timeline[2].pid);
    ASSERT_EQ_INT(10, res.timeline[2].start_time);
    ASSERT_EQ_INT(13, res.timeline[2].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(0, p1->start_time);
    ASSERT_EQ_INT(8, p1->completion_time);
    ASSERT_EQ_INT(8, p1->turnaround_time);
    ASSERT_EQ_INT(0, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(8, p2->start_time);
    ASSERT_EQ_INT(10, p2->completion_time);
    ASSERT_EQ_INT(9, p2->turnaround_time);
    ASSERT_EQ_INT(7, p2->waiting_time);
    ASSERT_EQ_INT(7, p2->response_time);

    const sched_process_t *p3 = sched_result_get_proc(&res, 3);
    ASSERT_EQ_INT(10, p3->start_time);
    ASSERT_EQ_INT(13, p3->completion_time);
    ASSERT_EQ_INT(11, p3->turnaround_time);
    ASSERT_EQ_INT(8, p3->waiting_time);
    ASSERT_EQ_INT(8, p3->response_time);

    ASSERT_EQ_INT(13, res.total_elapsed_time);
    ASSERT_EQ_INT(13, res.total_busy_time);
    ASSERT_EQ_INT(0, res.total_idle_time);
    ASSERT_DOUBLE_NEAR(100.0, res.cpu_utilization, EPSILON);
    ASSERT_DOUBLE_NEAR(28.0 / 3.0, res.avg_turnaround_time, EPSILON);
    ASSERT_DOUBLE_NEAR(15.0 / 3.0, res.avg_waiting_time, EPSILON);
    ASSERT_DOUBLE_NEAR(15.0 / 3.0, res.avg_response_time, EPSILON);

    sched_result_free(&res);
}

static void test_sjf_idle_before_and_between_jobs(void) {
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 3, 2, 0);
    sched_process_init(&procs[1], 2, 10, 4, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_sjf(procs, 2, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(4, res.timeline_count);

    ASSERT_EQ_INT(SCHED_PID_IDLE, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(3, res.timeline[0].end_time);

    ASSERT_EQ_INT(1, res.timeline[1].pid);
    ASSERT_EQ_INT(3, res.timeline[1].start_time);
    ASSERT_EQ_INT(5, res.timeline[1].end_time);

    ASSERT_EQ_INT(SCHED_PID_IDLE, res.timeline[2].pid);
    ASSERT_EQ_INT(5, res.timeline[2].start_time);
    ASSERT_EQ_INT(10, res.timeline[2].end_time);

    ASSERT_EQ_INT(2, res.timeline[3].pid);
    ASSERT_EQ_INT(10, res.timeline[3].start_time);
    ASSERT_EQ_INT(14, res.timeline[3].end_time);

    ASSERT_EQ_INT(14, res.total_elapsed_time);
    ASSERT_EQ_INT(6, res.total_busy_time);
    ASSERT_EQ_INT(8, res.total_idle_time);
    ASSERT_DOUBLE_NEAR((6.0 / 14.0) * 100.0, res.cpu_utilization, EPSILON);
    ASSERT_DOUBLE_NEAR(3.0, res.avg_turnaround_time, EPSILON);
    ASSERT_DOUBLE_NEAR(0.0, res.avg_waiting_time, EPSILON);
    ASSERT_DOUBLE_NEAR(0.0, res.avg_response_time, EPSILON);

    sched_result_free(&res);
}

/* ------------------------------------------------------------------------- */
/* C. Round Robin Tests (Phase 8B)                                           */
/* ------------------------------------------------------------------------- */

static void test_rr_single_process_quantum_smaller_than_burst(void) {
    sched_process_t procs[1];
    sched_process_init(&procs[0], 1, 0, 7, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_rr(procs, 1, 3, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(1, res.process_count);
    ASSERT_EQ_INT(1, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(7, res.timeline[0].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_NOT_NULL(p1);
    ASSERT_EQ_INT(0, p1->start_time);
    ASSERT_EQ_INT(7, p1->completion_time);
    ASSERT_EQ_INT(0, p1->remaining_time);
    ASSERT_EQ_INT(7, p1->turnaround_time);
    ASSERT_EQ_INT(0, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    ASSERT_EQ_INT(7, res.total_elapsed_time);
    ASSERT_EQ_INT(7, res.total_busy_time);
    ASSERT_EQ_INT(0, res.total_idle_time);
    ASSERT_DOUBLE_NEAR(100.0, res.cpu_utilization, EPSILON);

    sched_result_free(&res);
}

static void test_rr_single_process_quantum_larger_than_burst(void) {
    sched_process_t procs[1];
    sched_process_init(&procs[0], 1, 0, 3, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_rr(procs, 1, 10, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(1, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(3, res.timeline[0].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(0, p1->start_time);
    ASSERT_EQ_INT(3, p1->completion_time);
    ASSERT_EQ_INT(0, p1->remaining_time);
    ASSERT_EQ_INT(3, p1->turnaround_time);
    ASSERT_EQ_INT(0, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    sched_result_free(&res);
}

static void test_rr_multiple_processes_arriving_at_zero(void) {
    /*
     * P1: burst 5, P2: burst 3. Quantum 2.
     * Slices: P1 [0, 2], P2 [2, 4], P1 [4, 6], P2 [6, 7], P1 [7, 8].
     */
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 5, 0);
    sched_process_init(&procs[1], 2, 0, 3, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_rr(procs, 2, 2, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(5, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(2, res.timeline[0].end_time);

    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(2, res.timeline[1].start_time);
    ASSERT_EQ_INT(4, res.timeline[1].end_time);

    ASSERT_EQ_INT(1, res.timeline[2].pid);
    ASSERT_EQ_INT(4, res.timeline[2].start_time);
    ASSERT_EQ_INT(6, res.timeline[2].end_time);

    ASSERT_EQ_INT(2, res.timeline[3].pid);
    ASSERT_EQ_INT(6, res.timeline[3].start_time);
    ASSERT_EQ_INT(7, res.timeline[3].end_time);

    ASSERT_EQ_INT(1, res.timeline[4].pid);
    ASSERT_EQ_INT(7, res.timeline[4].start_time);
    ASSERT_EQ_INT(8, res.timeline[4].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(0, p1->start_time);
    ASSERT_EQ_INT(8, p1->completion_time);
    ASSERT_EQ_INT(0, p1->remaining_time);
    ASSERT_EQ_INT(8, p1->turnaround_time);
    ASSERT_EQ_INT(3, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(2, p2->start_time);
    ASSERT_EQ_INT(7, p2->completion_time);
    ASSERT_EQ_INT(0, p2->remaining_time);
    ASSERT_EQ_INT(7, p2->turnaround_time);
    ASSERT_EQ_INT(4, p2->waiting_time);
    ASSERT_EQ_INT(2, p2->response_time);

    ASSERT_EQ_INT(8, res.total_elapsed_time);
    ASSERT_EQ_INT(8, res.total_busy_time);
    ASSERT_EQ_INT(0, res.total_idle_time);
    ASSERT_DOUBLE_NEAR(100.0, res.cpu_utilization, EPSILON);
    ASSERT_DOUBLE_NEAR(7.5, res.avg_turnaround_time, EPSILON);
    ASSERT_DOUBLE_NEAR(3.5, res.avg_waiting_time, EPSILON);
    ASSERT_DOUBLE_NEAR(1.0, res.avg_response_time, EPSILON);

    sched_result_free(&res);
}

static void test_rr_staggered_and_new_arrival_during_quantum(void) {
    /*
     * P1: arr 0, burst 4
     * P2: arr 1, burst 5
     * P3: arr 3, burst 2
     * Quantum = 2
     *
     * [0, 2] P1 (P2 arrives at 1, enqueued before P1 requeued -> queue: [P2, P1])
     * [2, 4] P2 (P3 arrives at 3, enqueued before P2 requeued -> queue: [P1, P3, P2])
     * [4, 6] P1 (P1 finishes at 6 -> queue: [P3, P2])
     * [6, 8] P3 (P3 finishes at 8 -> queue: [P2])
     * [8, 11] P2 (runs 8 to 10 then 10 to 11, contiguous merge -> [8, 11])
     */
    sched_process_t procs[3];
    sched_process_init(&procs[0], 1, 0, 4, 0);
    sched_process_init(&procs[1], 2, 1, 5, 0);
    sched_process_init(&procs[2], 3, 3, 2, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_rr(procs, 3, 2, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(5, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(2, res.timeline[0].end_time);

    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(2, res.timeline[1].start_time);
    ASSERT_EQ_INT(4, res.timeline[1].end_time);

    ASSERT_EQ_INT(1, res.timeline[2].pid);
    ASSERT_EQ_INT(4, res.timeline[2].start_time);
    ASSERT_EQ_INT(6, res.timeline[2].end_time);

    ASSERT_EQ_INT(3, res.timeline[3].pid);
    ASSERT_EQ_INT(6, res.timeline[3].start_time);
    ASSERT_EQ_INT(8, res.timeline[3].end_time);

    ASSERT_EQ_INT(2, res.timeline[4].pid);
    ASSERT_EQ_INT(8, res.timeline[4].start_time);
    ASSERT_EQ_INT(11, res.timeline[4].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(0, p1->start_time);
    ASSERT_EQ_INT(6, p1->completion_time);
    ASSERT_EQ_INT(6, p1->turnaround_time);
    ASSERT_EQ_INT(2, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(2, p2->start_time);
    ASSERT_EQ_INT(11, p2->completion_time);
    ASSERT_EQ_INT(10, p2->turnaround_time);
    ASSERT_EQ_INT(5, p2->waiting_time);
    ASSERT_EQ_INT(1, p2->response_time);

    const sched_process_t *p3 = sched_result_get_proc(&res, 3);
    ASSERT_EQ_INT(6, p3->start_time);
    ASSERT_EQ_INT(8, p3->completion_time);
    ASSERT_EQ_INT(5, p3->turnaround_time);
    ASSERT_EQ_INT(3, p3->waiting_time);
    ASSERT_EQ_INT(3, p3->response_time);

    ASSERT_EQ_INT(11, res.total_elapsed_time);
    ASSERT_EQ_INT(11, res.total_busy_time);
    ASSERT_EQ_INT(0, res.total_idle_time);
    ASSERT_DOUBLE_NEAR(100.0, res.cpu_utilization, EPSILON);
    ASSERT_DOUBLE_NEAR(7.0, res.avg_turnaround_time, EPSILON);
    ASSERT_DOUBLE_NEAR(10.0 / 3.0, res.avg_waiting_time, EPSILON);
    ASSERT_DOUBLE_NEAR(4.0 / 3.0, res.avg_response_time, EPSILON);

    sched_result_free(&res);
}

static void test_rr_process_finishing_at_quantum_boundary(void) {
    /*
     * P1: arr 0, burst 4
     * P2: arr 2, burst 2
     * Quantum = 2
     *
     * At t=0: P1 runs [0, 2].
     * At t=2: P2 arrives exactly at quantum boundary!
     * P2 enqueued before P1 requeued -> queue: [P2, P1].
     * P2 runs [2, 4] and finishes exactly at quantum boundary!
     * P1 runs [4, 6] and finishes.
     */
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 4, 0);
    sched_process_init(&procs[1], 2, 2, 2, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_rr(procs, 2, 2, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(3, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(2, res.timeline[0].end_time);

    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(2, res.timeline[1].start_time);
    ASSERT_EQ_INT(4, res.timeline[1].end_time);

    ASSERT_EQ_INT(1, res.timeline[2].pid);
    ASSERT_EQ_INT(4, res.timeline[2].start_time);
    ASSERT_EQ_INT(6, res.timeline[2].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(0, p1->start_time);
    ASSERT_EQ_INT(6, p1->completion_time);
    ASSERT_EQ_INT(6, p1->turnaround_time);
    ASSERT_EQ_INT(2, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(2, p2->start_time);
    ASSERT_EQ_INT(4, p2->completion_time);
    ASSERT_EQ_INT(2, p2->turnaround_time);
    ASSERT_EQ_INT(0, p2->waiting_time);
    ASSERT_EQ_INT(0, p2->response_time);

    sched_result_free(&res);
}

static void test_rr_burst_exactly_equal_quantum(void) {
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 4, 0);
    sched_process_init(&procs[1], 2, 0, 4, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_rr(procs, 2, 4, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(2, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(4, res.timeline[0].end_time);

    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(4, res.timeline[1].start_time);
    ASSERT_EQ_INT(8, res.timeline[1].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(0, p1->start_time);
    ASSERT_EQ_INT(4, p1->completion_time);
    ASSERT_EQ_INT(0, p1->remaining_time);
    ASSERT_EQ_INT(4, p1->turnaround_time);
    ASSERT_EQ_INT(0, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(4, p2->start_time);
    ASSERT_EQ_INT(8, p2->completion_time);
    ASSERT_EQ_INT(0, p2->remaining_time);
    ASSERT_EQ_INT(8, p2->turnaround_time);
    ASSERT_EQ_INT(4, p2->waiting_time);
    ASSERT_EQ_INT(4, p2->response_time);

    ASSERT_EQ_INT(8, res.total_elapsed_time);
    ASSERT_EQ_INT(8, res.total_busy_time);
    ASSERT_EQ_INT(0, res.total_idle_time);
    ASSERT_DOUBLE_NEAR(100.0, res.cpu_utilization, EPSILON);
    ASSERT_DOUBLE_NEAR(6.0, res.avg_turnaround_time, EPSILON);
    ASSERT_DOUBLE_NEAR(2.0, res.avg_waiting_time, EPSILON);
    ASSERT_DOUBLE_NEAR(2.0, res.avg_response_time, EPSILON);

    sched_result_free(&res);
}

static void test_rr_arrival_exactly_at_quantum_boundary(void) {
    /*
     * P1: arr 0, burst 6
     * P2: arr 2, burst 3
     * Quantum = 2
     *
     * At t=0: P1 runs [0, 2].
     * At t=2: P2 arrives at quantum boundary while P1 has remaining 4.
     * Ordering rule: newly arrived P2 is enqueued BEFORE P1 is requeued.
     * Queue: [P2, P1].
     * At t=2: P2 runs [2, 4]. Remaining P2 = 1. P2 requeued. Queue: [P1, P2].
     * At t=4: P1 runs [4, 6]. Remaining P1 = 2. P1 requeued. Queue: [P2, P1].
     * At t=6: P2 runs [6, 7]. Finishes! Queue: [P1].
     * At t=7: P1 runs [7, 9]. Finishes!
     *
     * Slices: [0, 2] P1, [2, 4] P2, [4, 6] P1, [6, 7] P2, [7, 9] P1
     */
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 6, 0);
    sched_process_init(&procs[1], 2, 2, 3, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_rr(procs, 2, 2, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(5, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(2, res.timeline[0].end_time);

    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(2, res.timeline[1].start_time);
    ASSERT_EQ_INT(4, res.timeline[1].end_time);

    ASSERT_EQ_INT(1, res.timeline[2].pid);
    ASSERT_EQ_INT(4, res.timeline[2].start_time);
    ASSERT_EQ_INT(6, res.timeline[2].end_time);

    ASSERT_EQ_INT(2, res.timeline[3].pid);
    ASSERT_EQ_INT(6, res.timeline[3].start_time);
    ASSERT_EQ_INT(7, res.timeline[3].end_time);

    ASSERT_EQ_INT(1, res.timeline[4].pid);
    ASSERT_EQ_INT(7, res.timeline[4].start_time);
    ASSERT_EQ_INT(9, res.timeline[4].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(0, p1->start_time);
    ASSERT_EQ_INT(9, p1->completion_time);
    ASSERT_EQ_INT(0, p1->remaining_time);
    ASSERT_EQ_INT(9, p1->turnaround_time);
    ASSERT_EQ_INT(3, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(2, p2->start_time);
    ASSERT_EQ_INT(7, p2->completion_time);
    ASSERT_EQ_INT(0, p2->remaining_time);
    ASSERT_EQ_INT(5, p2->turnaround_time);
    ASSERT_EQ_INT(2, p2->waiting_time);
    ASSERT_EQ_INT(0, p2->response_time);

    ASSERT_EQ_INT(9, res.total_elapsed_time);
    ASSERT_EQ_INT(9, res.total_busy_time);
    ASSERT_EQ_INT(0, res.total_idle_time);
    ASSERT_DOUBLE_NEAR(100.0, res.cpu_utilization, EPSILON);

    sched_result_free(&res);
}

static void test_rr_idle_periods(void) {
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 2, 3, 0);
    sched_process_init(&procs[1], 2, 9, 2, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_rr(procs, 2, 4, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(4, res.timeline_count);

    /* [0, 2] IDLE */
    ASSERT_EQ_INT(SCHED_PID_IDLE, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(2, res.timeline[0].end_time);

    /* [2, 5] P1 */
    ASSERT_EQ_INT(1, res.timeline[1].pid);
    ASSERT_EQ_INT(2, res.timeline[1].start_time);
    ASSERT_EQ_INT(5, res.timeline[1].end_time);

    /* [5, 9] IDLE */
    ASSERT_EQ_INT(SCHED_PID_IDLE, res.timeline[2].pid);
    ASSERT_EQ_INT(5, res.timeline[2].start_time);
    ASSERT_EQ_INT(9, res.timeline[2].end_time);

    /* [9, 11] P2 */
    ASSERT_EQ_INT(2, res.timeline[3].pid);
    ASSERT_EQ_INT(9, res.timeline[3].start_time);
    ASSERT_EQ_INT(11, res.timeline[3].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(2, p1->start_time);
    ASSERT_EQ_INT(5, p1->completion_time);
    ASSERT_EQ_INT(3, p1->turnaround_time);
    ASSERT_EQ_INT(0, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(9, p2->start_time);
    ASSERT_EQ_INT(11, p2->completion_time);
    ASSERT_EQ_INT(2, p2->turnaround_time);
    ASSERT_EQ_INT(0, p2->waiting_time);
    ASSERT_EQ_INT(0, p2->response_time);

    ASSERT_EQ_INT(11, res.total_elapsed_time);
    ASSERT_EQ_INT(5, res.total_busy_time);
    ASSERT_EQ_INT(6, res.total_idle_time);
    ASSERT_DOUBLE_NEAR((5.0 / 11.0) * 100.0, res.cpu_utilization, EPSILON);

    sched_result_free(&res);
}

static void test_rr_invalid_quantum(void) {
    sched_process_t procs[1];
    sched_process_init(&procs[0], 1, 0, 5, 0);

    sched_result_t res;
    sched_result_init(&res);

    ASSERT_EQ_INT(-1, sched_schedule_rr(procs, 1, 0, &res));
    ASSERT_EQ_INT(-1, sched_schedule_rr(procs, 1, -3, &res));

    sched_result_free(&res);
}

static void test_rr_determinism(void) {
    sched_process_t procs[3];
    sched_process_init(&procs[0], 1, 0, 4, 0);
    sched_process_init(&procs[1], 2, 1, 5, 0);
    sched_process_init(&procs[2], 3, 3, 2, 0);

    sched_result_t res1, res2;
    sched_result_init(&res1);
    sched_result_init(&res2);

    for (int iter = 0; iter < 10; iter++) {
        ASSERT_EQ_INT(0, sched_schedule_rr(procs, 3, 2, &res1));
        ASSERT_EQ_INT(0, sched_schedule_rr(procs, 3, 2, &res2));

        ASSERT_EQ_INT(res1.timeline_count, res2.timeline_count);
        for (int i = 0; i < res1.timeline_count; i++) {
            ASSERT_EQ_INT(res1.timeline[i].pid, res2.timeline[i].pid);
            ASSERT_EQ_INT(res1.timeline[i].start_time, res2.timeline[i].start_time);
            ASSERT_EQ_INT(res1.timeline[i].end_time, res2.timeline[i].end_time);
        }
        ASSERT_DOUBLE_NEAR(res1.avg_waiting_time, res2.avg_waiting_time, EPSILON);
    }

    sched_result_free(&res1);
    sched_result_free(&res2);
}

/* ------------------------------------------------------------------------- */
/* D. Priority Tests (Phase 8B)                                              */
/* ------------------------------------------------------------------------- */

static void test_priority_single_process(void) {
    sched_process_t procs[1];
    sched_process_init(&procs[0], 1, 0, 5, 2);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_priority(procs, 1, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(1, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(5, res.timeline[0].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(0, p1->start_time);
    ASSERT_EQ_INT(5, p1->completion_time);
    ASSERT_EQ_INT(0, p1->remaining_time);
    ASSERT_EQ_INT(5, p1->turnaround_time);
    ASSERT_EQ_INT(0, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    sched_result_free(&res);
}

static void test_priority_different_priorities(void) {
    /*
     * Lower number = higher priority.
     * P1: prio 3, burst 4
     * P2: prio 1, burst 3
     * P3: prio 2, burst 2
     * Order: P2, P3, P1.
     */
    sched_process_t procs[3];
    sched_process_init(&procs[0], 1, 0, 4, 3);
    sched_process_init(&procs[1], 2, 0, 3, 1);
    sched_process_init(&procs[2], 3, 0, 2, 2);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_priority(procs, 3, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(3, res.timeline_count);

    ASSERT_EQ_INT(2, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(3, res.timeline[0].end_time);

    ASSERT_EQ_INT(3, res.timeline[1].pid);
    ASSERT_EQ_INT(3, res.timeline[1].start_time);
    ASSERT_EQ_INT(5, res.timeline[1].end_time);

    ASSERT_EQ_INT(1, res.timeline[2].pid);
    ASSERT_EQ_INT(5, res.timeline[2].start_time);
    ASSERT_EQ_INT(9, res.timeline[2].end_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(0, p2->start_time);
    ASSERT_EQ_INT(3, p2->completion_time);
    ASSERT_EQ_INT(3, p2->turnaround_time);
    ASSERT_EQ_INT(0, p2->waiting_time);
    ASSERT_EQ_INT(0, p2->response_time);

    const sched_process_t *p3 = sched_result_get_proc(&res, 3);
    ASSERT_EQ_INT(3, p3->start_time);
    ASSERT_EQ_INT(5, p3->completion_time);
    ASSERT_EQ_INT(5, p3->turnaround_time);
    ASSERT_EQ_INT(3, p3->waiting_time);
    ASSERT_EQ_INT(3, p3->response_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(5, p1->start_time);
    ASSERT_EQ_INT(9, p1->completion_time);
    ASSERT_EQ_INT(9, p1->turnaround_time);
    ASSERT_EQ_INT(5, p1->waiting_time);
    ASSERT_EQ_INT(5, p1->response_time);

    ASSERT_EQ_INT(9, res.total_elapsed_time);
    ASSERT_EQ_INT(9, res.total_busy_time);
    ASSERT_EQ_INT(0, res.total_idle_time);
    ASSERT_DOUBLE_NEAR(100.0, res.cpu_utilization, EPSILON);
    ASSERT_DOUBLE_NEAR(17.0 / 3.0, res.avg_turnaround_time, EPSILON);
    ASSERT_DOUBLE_NEAR(8.0 / 3.0, res.avg_waiting_time, EPSILON);
    ASSERT_DOUBLE_NEAR(8.0 / 3.0, res.avg_response_time, EPSILON);

    sched_result_free(&res);
}

static void test_priority_same_priority_tie_breaking(void) {
    /*
     * P1: arr 2, burst 3, prio 1, pid 1
     * P2: arr 1, burst 3, prio 1, pid 2
     * P3: arr 1, burst 3, prio 1, pid 3
     *
     * t=0 to 1: IDLE [0, 1]
     * t=1: P2 and P3 ready (prio 1, arr 1) -> tie-break by lower PID -> P2 runs [1, 4]
     * t=4: P3 (arr 1) and P1 (arr 2) ready (both prio 1) -> tie-break by earlier arrival -> P3 runs [4, 7]
     * t=7: P1 runs [7, 10]
     */
    sched_process_t procs[3];
    sched_process_init(&procs[0], 1, 2, 3, 1);
    sched_process_init(&procs[1], 2, 1, 3, 1);
    sched_process_init(&procs[2], 3, 1, 3, 1);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_priority(procs, 3, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(4, res.timeline_count);

    ASSERT_EQ_INT(SCHED_PID_IDLE, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(1, res.timeline[0].end_time);

    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(1, res.timeline[1].start_time);
    ASSERT_EQ_INT(4, res.timeline[1].end_time);

    ASSERT_EQ_INT(3, res.timeline[2].pid);
    ASSERT_EQ_INT(4, res.timeline[2].start_time);
    ASSERT_EQ_INT(7, res.timeline[2].end_time);

    ASSERT_EQ_INT(1, res.timeline[3].pid);
    ASSERT_EQ_INT(7, res.timeline[3].start_time);
    ASSERT_EQ_INT(10, res.timeline[3].end_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(1, p2->start_time);
    ASSERT_EQ_INT(4, p2->completion_time);
    ASSERT_EQ_INT(3, p2->turnaround_time);
    ASSERT_EQ_INT(0, p2->waiting_time);
    ASSERT_EQ_INT(0, p2->response_time);

    const sched_process_t *p3 = sched_result_get_proc(&res, 3);
    ASSERT_EQ_INT(4, p3->start_time);
    ASSERT_EQ_INT(7, p3->completion_time);
    ASSERT_EQ_INT(6, p3->turnaround_time);
    ASSERT_EQ_INT(3, p3->waiting_time);
    ASSERT_EQ_INT(3, p3->response_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(7, p1->start_time);
    ASSERT_EQ_INT(10, p1->completion_time);
    ASSERT_EQ_INT(8, p1->turnaround_time);
    ASSERT_EQ_INT(5, p1->waiting_time);
    ASSERT_EQ_INT(5, p1->response_time);

    ASSERT_EQ_INT(10, res.total_elapsed_time);
    ASSERT_EQ_INT(9, res.total_busy_time);
    ASSERT_EQ_INT(1, res.total_idle_time);
    ASSERT_DOUBLE_NEAR(90.0, res.cpu_utilization, EPSILON);

    sched_result_free(&res);
}

static void test_priority_non_preemptive_behavior(void) {
    /*
     * P1: arr 0, burst 8, prio 5 (low priority)
     * P2: arr 2, burst 2, prio 1 (high priority)
     * Non-preemptive: P1 runs [0, 8] without interruption.
     */
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 8, 5);
    sched_process_init(&procs[1], 2, 2, 2, 1);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_priority(procs, 2, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(2, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(8, res.timeline[0].end_time);

    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(8, res.timeline[1].start_time);
    ASSERT_EQ_INT(10, res.timeline[1].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(0, p1->start_time);
    ASSERT_EQ_INT(8, p1->completion_time);
    ASSERT_EQ_INT(8, p1->turnaround_time);
    ASSERT_EQ_INT(0, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(8, p2->start_time);
    ASSERT_EQ_INT(10, p2->completion_time);
    ASSERT_EQ_INT(8, p2->turnaround_time);
    ASSERT_EQ_INT(6, p2->waiting_time);
    ASSERT_EQ_INT(6, p2->response_time);

    sched_result_free(&res);
}

static void test_priority_staggered_arrivals(void) {
    /*
     * P1: arr 0, burst 5, prio 3
     * P2: arr 1, burst 4, prio 1 (highest priority)
     * P3: arr 2, burst 3, prio 2
     *
     * At t=0: P1 starts running [0, 5].
     * At t=5: P2 and P3 are ready. P2 has higher priority (1 < 2).
     * P2 runs [5, 9].
     * At t=9: P3 runs [9, 12].
     */
    sched_process_t procs[3];
    sched_process_init(&procs[0], 1, 0, 5, 3);
    sched_process_init(&procs[1], 2, 1, 4, 1);
    sched_process_init(&procs[2], 3, 2, 3, 2);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_priority(procs, 3, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(3, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(5, res.timeline[0].end_time);

    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(5, res.timeline[1].start_time);
    ASSERT_EQ_INT(9, res.timeline[1].end_time);

    ASSERT_EQ_INT(3, res.timeline[2].pid);
    ASSERT_EQ_INT(9, res.timeline[2].start_time);
    ASSERT_EQ_INT(12, res.timeline[2].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(0, p1->start_time);
    ASSERT_EQ_INT(5, p1->completion_time);
    ASSERT_EQ_INT(5, p1->turnaround_time);
    ASSERT_EQ_INT(0, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(5, p2->start_time);
    ASSERT_EQ_INT(9, p2->completion_time);
    ASSERT_EQ_INT(8, p2->turnaround_time);
    ASSERT_EQ_INT(4, p2->waiting_time);
    ASSERT_EQ_INT(4, p2->response_time);

    const sched_process_t *p3 = sched_result_get_proc(&res, 3);
    ASSERT_EQ_INT(9, p3->start_time);
    ASSERT_EQ_INT(12, p3->completion_time);
    ASSERT_EQ_INT(10, p3->turnaround_time);
    ASSERT_EQ_INT(7, p3->waiting_time);
    ASSERT_EQ_INT(7, p3->response_time);

    ASSERT_EQ_INT(12, res.total_elapsed_time);
    ASSERT_EQ_INT(12, res.total_busy_time);
    ASSERT_EQ_INT(0, res.total_idle_time);
    ASSERT_DOUBLE_NEAR(100.0, res.cpu_utilization, EPSILON);
    ASSERT_DOUBLE_NEAR(23.0 / 3.0, res.avg_turnaround_time, EPSILON);
    ASSERT_DOUBLE_NEAR(11.0 / 3.0, res.avg_waiting_time, EPSILON);
    ASSERT_DOUBLE_NEAR(11.0 / 3.0, res.avg_response_time, EPSILON);

    sched_result_free(&res);
}

static void test_priority_idle_periods(void) {
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 3, 2, 2);
    sched_process_init(&procs[1], 2, 8, 3, 1);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_priority(procs, 2, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(4, res.timeline_count);

    ASSERT_EQ_INT(SCHED_PID_IDLE, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(3, res.timeline[0].end_time);

    ASSERT_EQ_INT(1, res.timeline[1].pid);
    ASSERT_EQ_INT(3, res.timeline[1].start_time);
    ASSERT_EQ_INT(5, res.timeline[1].end_time);

    ASSERT_EQ_INT(SCHED_PID_IDLE, res.timeline[2].pid);
    ASSERT_EQ_INT(5, res.timeline[2].start_time);
    ASSERT_EQ_INT(8, res.timeline[2].end_time);

    ASSERT_EQ_INT(2, res.timeline[3].pid);
    ASSERT_EQ_INT(8, res.timeline[3].start_time);
    ASSERT_EQ_INT(11, res.timeline[3].end_time);

    ASSERT_EQ_INT(11, res.total_elapsed_time);
    ASSERT_EQ_INT(5, res.total_busy_time);
    ASSERT_EQ_INT(6, res.total_idle_time);

    sched_result_free(&res);
}

static void test_priority_determinism(void) {
    sched_process_t procs[3];
    sched_process_init(&procs[0], 1, 0, 4, 3);
    sched_process_init(&procs[1], 2, 0, 3, 1);
    sched_process_init(&procs[2], 3, 0, 2, 2);

    sched_result_t res1, res2;
    sched_result_init(&res1);
    sched_result_init(&res2);

    for (int iter = 0; iter < 10; iter++) {
        ASSERT_EQ_INT(0, sched_schedule_priority(procs, 3, &res1));
        ASSERT_EQ_INT(0, sched_schedule_priority(procs, 3, &res2));

        ASSERT_EQ_INT(res1.timeline_count, res2.timeline_count);
        for (int i = 0; i < res1.timeline_count; i++) {
            ASSERT_EQ_INT(res1.timeline[i].pid, res2.timeline[i].pid);
            ASSERT_EQ_INT(res1.timeline[i].start_time, res2.timeline[i].start_time);
            ASSERT_EQ_INT(res1.timeline[i].end_time, res2.timeline[i].end_time);
        }
    }

    sched_result_free(&res1);
    sched_result_free(&res2);
}

/* ------------------------------------------------------------------------- */
/* E. Gantt Chart Presentation Tests (Phase 8B)                              */
/* ------------------------------------------------------------------------- */

static void test_gantt_simple_fcfs(void) {
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 4, 0);
    sched_process_init(&procs[1], 2, 0, 3, 0);

    sched_result_t res;
    sched_result_init(&res);
    ASSERT_EQ_INT(0, sched_schedule_fcfs(procs, 2, &res));

    char *gantt = sched_render_gantt(&res);
    ASSERT_NOT_NULL(gantt);

    /* Verify line 1 contains bars and process names */
    ASSERT_NOT_NULL(strstr(gantt, "P1"));
    ASSERT_NOT_NULL(strstr(gantt, "P2"));
    ASSERT_NOT_NULL(strstr(gantt, "|"));

    /* Verify line 2 contains time boundary labels: 0, 4, 7 */
    ASSERT_NOT_NULL(strstr(gantt, "0"));
    ASSERT_NOT_NULL(strstr(gantt, "4"));
    ASSERT_NOT_NULL(strstr(gantt, "7"));

    free(gantt);
    sched_result_free(&res);
}

static void test_gantt_idle_intervals(void) {
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 2, 3, 0);
    sched_process_init(&procs[1], 2, 8, 2, 0);

    sched_result_t res;
    sched_result_init(&res);
    ASSERT_EQ_INT(0, sched_schedule_fcfs(procs, 2, &res));

    char *gantt = sched_render_gantt(&res);
    ASSERT_NOT_NULL(gantt);

    /* Must contain IDLE representation */
    ASSERT_NOT_NULL(strstr(gantt, "IDLE"));
    ASSERT_NOT_NULL(strstr(gantt, "P1"));
    ASSERT_NOT_NULL(strstr(gantt, "P2"));

    /* Boundaries: 0, 2, 5, 8, 10 */
    ASSERT_NOT_NULL(strstr(gantt, "0"));
    ASSERT_NOT_NULL(strstr(gantt, "2"));
    ASSERT_NOT_NULL(strstr(gantt, "5"));
    ASSERT_NOT_NULL(strstr(gantt, "8"));
    ASSERT_NOT_NULL(strstr(gantt, "10"));

    free(gantt);
    sched_result_free(&res);
}

static void test_gantt_rr_repeated_slices(void) {
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 5, 0);
    sched_process_init(&procs[1], 2, 0, 3, 0);

    sched_result_t res;
    sched_result_init(&res);
    ASSERT_EQ_INT(0, sched_schedule_rr(procs, 2, 2, &res));

    char *gantt = sched_render_gantt(&res);
    ASSERT_NOT_NULL(gantt);

    /* Slices: P1, P2, P1, P2, P1 */
    char *first_p1 = strstr(gantt, "P1");
    ASSERT_NOT_NULL(first_p1);
    char *first_p2 = strstr(first_p1 + 2, "P2");
    ASSERT_NOT_NULL(first_p2);
    char *second_p1 = strstr(first_p2 + 2, "P1");
    ASSERT_NOT_NULL(second_p1);
    char *second_p2 = strstr(second_p1 + 2, "P2");
    ASSERT_NOT_NULL(second_p2);
    char *third_p1 = strstr(second_p2 + 2, "P1");
    ASSERT_NOT_NULL(third_p1);

    /* Boundaries: 0, 2, 4, 6, 7, 8 */
    ASSERT_NOT_NULL(strstr(gantt, "0"));
    ASSERT_NOT_NULL(strstr(gantt, "2"));
    ASSERT_NOT_NULL(strstr(gantt, "4"));
    ASSERT_NOT_NULL(strstr(gantt, "6"));
    ASSERT_NOT_NULL(strstr(gantt, "7"));
    ASSERT_NOT_NULL(strstr(gantt, "8"));

    free(gantt);
    sched_result_free(&res);
}

static void test_gantt_buffer_and_empty(void) {
    sched_result_t res;
    sched_result_init(&res);

    /* Empty result */
    char *empty_str = sched_render_gantt(&res);
    ASSERT_NOT_NULL(empty_str);
    ASSERT_EQ_INT(0, (int)strlen(empty_str));
    free(empty_str);

    char buf[128];
    int rc = sched_render_gantt_buf(&res, buf, sizeof(buf));
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_STR("", buf);

    /* NULL result pointer */
    ASSERT_NULL(sched_render_gantt(NULL));
    ASSERT_EQ_INT(-1, sched_render_gantt_buf(NULL, buf, sizeof(buf)));

    sched_result_free(&res);
}

/* ------------------------------------------------------------------------- */
/* F. Edge Cases & Lifecycle (Phase 8A & 8B)                                 */
/* ------------------------------------------------------------------------- */

static void test_edge_case_zero_processes(void) {
    sched_result_t res;
    sched_result_init(&res);

    ASSERT_EQ_INT(0, sched_schedule_fcfs(NULL, 0, &res));
    ASSERT_EQ_INT(0, res.process_count);
    sched_result_free(&res);

    ASSERT_EQ_INT(0, sched_schedule_sjf(NULL, 0, &res));
    ASSERT_EQ_INT(0, res.process_count);
    sched_result_free(&res);

    ASSERT_EQ_INT(0, sched_schedule_rr(NULL, 0, 4, &res));
    ASSERT_EQ_INT(0, res.process_count);
    sched_result_free(&res);

    ASSERT_EQ_INT(0, sched_schedule_priority(NULL, 0, &res));
    ASSERT_EQ_INT(0, res.process_count);
    sched_result_free(&res);
}

static void test_edge_case_invalid_inputs(void) {
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 5, 0);
    sched_process_init(&procs[1], 2, 0, 3, 0);

    sched_result_t res;
    sched_result_init(&res);

    ASSERT_EQ_INT(-1, sched_schedule_fcfs(procs, 2, NULL));
    ASSERT_EQ_INT(-1, sched_schedule_sjf(procs, 2, NULL));
    ASSERT_EQ_INT(-1, sched_schedule_rr(procs, 2, 4, NULL));
    ASSERT_EQ_INT(-1, sched_schedule_priority(procs, 2, NULL));

    ASSERT_EQ_INT(-1, sched_schedule_fcfs(procs, -1, &res));
    ASSERT_EQ_INT(-1, sched_schedule_sjf(procs, -1, &res));
    ASSERT_EQ_INT(-1, sched_schedule_rr(procs, -1, 4, &res));
    ASSERT_EQ_INT(-1, sched_schedule_priority(procs, -1, &res));

    ASSERT_EQ_INT(-1, sched_schedule_fcfs(NULL, 2, &res));
    ASSERT_EQ_INT(-1, sched_schedule_sjf(NULL, 2, &res));
    ASSERT_EQ_INT(-1, sched_schedule_rr(NULL, 2, 4, &res));
    ASSERT_EQ_INT(-1, sched_schedule_priority(NULL, 2, &res));

    procs[0].burst_time = 0;
    ASSERT_EQ_INT(-1, sched_schedule_fcfs(procs, 2, &res));
    ASSERT_EQ_INT(-1, sched_schedule_sjf(procs, 2, &res));
    ASSERT_EQ_INT(-1, sched_schedule_rr(procs, 2, 4, &res));
    ASSERT_EQ_INT(-1, sched_schedule_priority(procs, 2, &res));
    procs[0].burst_time = 5;

    procs[1].arrival_time = -3;
    ASSERT_EQ_INT(-1, sched_schedule_fcfs(procs, 2, &res));
    ASSERT_EQ_INT(-1, sched_schedule_sjf(procs, 2, &res));
    ASSERT_EQ_INT(-1, sched_schedule_rr(procs, 2, 4, &res));
    ASSERT_EQ_INT(-1, sched_schedule_priority(procs, 2, &res));
    procs[1].arrival_time = 0;

    procs[0].pid = SCHED_PID_IDLE;
    ASSERT_EQ_INT(-1, sched_schedule_fcfs(procs, 2, &res));
    ASSERT_EQ_INT(-1, sched_schedule_sjf(procs, 2, &res));
    ASSERT_EQ_INT(-1, sched_schedule_rr(procs, 2, 4, &res));
    ASSERT_EQ_INT(-1, sched_schedule_priority(procs, 2, &res));
    procs[0].pid = 1;

    procs[1].pid = 1;
    ASSERT_EQ_INT(-1, sched_schedule_fcfs(procs, 2, &res));
    ASSERT_EQ_INT(-1, sched_schedule_sjf(procs, 2, &res));
    ASSERT_EQ_INT(-1, sched_schedule_rr(procs, 2, 4, &res));
    ASSERT_EQ_INT(-1, sched_schedule_priority(procs, 2, &res));
    procs[1].pid = 2;

    sched_result_free(&res);
}

/* ------------------------------------------------------------------------- */
/* E. Multilevel Feedback Queue (MLFQ) Tests (Phase 8C)                      */
/* ------------------------------------------------------------------------- */

static void test_mlfq_single_process_completes_in_q0(void) {
    sched_process_t procs[1];
    sched_process_init(&procs[0], 1, 0, 3, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_mlfq(procs, 1, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(1, res.process_count);
    ASSERT_EQ_INT(1, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(3, res.timeline[0].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_NOT_NULL(p1);
    ASSERT_EQ_INT(0, p1->start_time);
    ASSERT_EQ_INT(3, p1->completion_time);
    ASSERT_EQ_INT(0, p1->remaining_time);
    ASSERT_EQ_INT(3, p1->turnaround_time);
    ASSERT_EQ_INT(0, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    ASSERT_EQ_INT(3, res.total_elapsed_time);
    ASSERT_EQ_INT(3, res.total_busy_time);
    ASSERT_EQ_INT(0, res.total_idle_time);
    ASSERT_DOUBLE_NEAR(100.0, res.cpu_utilization, EPSILON);

    sched_result_free(&res);
}

static void test_mlfq_single_process_q0_demotion_to_q1(void) {
    /*
     * P1 (burst 8, arr 0) runs 0..4 in Q0, demoted to Q1 at 4.
     * Starts in Q1 at 4.
     * P2 (burst 2, arr 5) arrives at 5 in Q0 -> preempts P1 in Q1!
     * P2 runs 5..7 in Q0 to completion.
     * P1 resumes in Q1 at 7 and finishes remaining 3 ticks at 10.
     */
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 8, 0);
    sched_process_init(&procs[1], 2, 5, 2, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_mlfq(procs, 2, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(2, res.process_count);
    ASSERT_EQ_INT(3, res.timeline_count);

    /* P1 runs 0..5 (0..4 in Q0, 4..5 in Q1 merged) */
    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(5, res.timeline[0].end_time);

    /* P2 preempts in Q0: 5..7 */
    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(5, res.timeline[1].start_time);
    ASSERT_EQ_INT(7, res.timeline[1].end_time);

    /* P1 resumes in Q1: 7..10 */
    ASSERT_EQ_INT(1, res.timeline[2].pid);
    ASSERT_EQ_INT(7, res.timeline[2].start_time);
    ASSERT_EQ_INT(10, res.timeline[2].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_NOT_NULL(p1);
    ASSERT_EQ_INT(0, p1->start_time);
    ASSERT_EQ_INT(10, p1->completion_time);
    ASSERT_EQ_INT(0, p1->remaining_time);
    ASSERT_EQ_INT(10, p1->turnaround_time);
    ASSERT_EQ_INT(2, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_NOT_NULL(p2);
    ASSERT_EQ_INT(5, p2->start_time);
    ASSERT_EQ_INT(7, p2->completion_time);
    ASSERT_EQ_INT(0, p2->remaining_time);
    ASSERT_EQ_INT(2, p2->turnaround_time);
    ASSERT_EQ_INT(0, p2->waiting_time);
    ASSERT_EQ_INT(0, p2->response_time);

    sched_result_free(&res);
}

static void test_mlfq_single_process_q1_demotion_to_q2(void) {
    /*
     * P1 (burst 18, arr 0) runs:
     * 0..4 in Q0 (demotes to Q1)
     * 4..12 in Q1 (uses 8 ticks, demotes to Q2 at 12)
     * 12..13 in Q2 (needs 6 ticks).
     * P2 (burst 2, arr 13) arrives in Q0 -> preempts P1 in Q2!
     * P2 runs 13..15 in Q0 to completion.
     * P1 resumes in Q2 at 15..20 (5 ticks) to completion.
     */
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 18, 0);
    sched_process_init(&procs[1], 2, 13, 2, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_mlfq(procs, 2, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(2, res.process_count);
    ASSERT_EQ_INT(3, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(13, res.timeline[0].end_time);

    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(13, res.timeline[1].start_time);
    ASSERT_EQ_INT(15, res.timeline[1].end_time);

    ASSERT_EQ_INT(1, res.timeline[2].pid);
    ASSERT_EQ_INT(15, res.timeline[2].start_time);
    ASSERT_EQ_INT(20, res.timeline[2].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(0, p1->start_time);
    ASSERT_EQ_INT(20, p1->completion_time);
    ASSERT_EQ_INT(20, p1->turnaround_time);
    ASSERT_EQ_INT(2, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(13, p2->start_time);
    ASSERT_EQ_INT(15, p2->completion_time);
    ASSERT_EQ_INT(2, p2->turnaround_time);
    ASSERT_EQ_INT(0, p2->waiting_time);
    ASSERT_EQ_INT(0, p2->response_time);

    sched_result_free(&res);
}

static void test_mlfq_q2_fcfs_behavior_and_no_further_demotion(void) {
    /*
     * P1 (burst 14, arr 0), P2 (burst 14, arr 0)
     * 0..4: P1 (Q0)
     * 4..8: P2 (Q0)
     * 8..16: P1 (Q1, 8 ticks) -> demotes to Q2 at 16 (rem 2)
     * 16..24: P2 (Q1, 8 ticks) -> demotes to Q2 at 24 (rem 2)
     * 24..26: P1 (Q2 FCFS, 2 ticks) -> completes at 26
     * 26..28: P2 (Q2 FCFS, 2 ticks) -> completes at 28
     */
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 14, 0);
    sched_process_init(&procs[1], 2, 0, 14, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_mlfq(procs, 2, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(2, res.process_count);
    ASSERT_EQ_INT(6, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(4, res.timeline[0].end_time);

    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(4, res.timeline[1].start_time);
    ASSERT_EQ_INT(8, res.timeline[1].end_time);

    ASSERT_EQ_INT(1, res.timeline[2].pid);
    ASSERT_EQ_INT(8, res.timeline[2].start_time);
    ASSERT_EQ_INT(16, res.timeline[2].end_time);

    ASSERT_EQ_INT(2, res.timeline[3].pid);
    ASSERT_EQ_INT(16, res.timeline[3].start_time);
    ASSERT_EQ_INT(24, res.timeline[3].end_time);

    ASSERT_EQ_INT(1, res.timeline[4].pid);
    ASSERT_EQ_INT(24, res.timeline[4].start_time);
    ASSERT_EQ_INT(26, res.timeline[4].end_time);

    ASSERT_EQ_INT(2, res.timeline[5].pid);
    ASSERT_EQ_INT(26, res.timeline[5].start_time);
    ASSERT_EQ_INT(28, res.timeline[5].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(26, p1->completion_time);
    ASSERT_EQ_INT(26, p1->turnaround_time);
    ASSERT_EQ_INT(12, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(28, p2->completion_time);
    ASSERT_EQ_INT(28, p2->turnaround_time);
    ASSERT_EQ_INT(14, p2->waiting_time);
    ASSERT_EQ_INT(4, p2->response_time);

    sched_result_free(&res);
}

static void test_mlfq_multiple_processes_arriving_at_zero(void) {
    sched_process_t procs[3];
    sched_process_init(&procs[0], 1, 0, 3, 0);
    sched_process_init(&procs[1], 2, 0, 6, 0);
    sched_process_init(&procs[2], 3, 0, 2, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_mlfq(procs, 3, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(3, res.process_count);
    ASSERT_EQ_INT(4, res.timeline_count);

    /* 0..3: P1 completes */
    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(3, res.timeline[0].end_time);

    /* 3..7: P2 in Q0 (demotes to Q1) */
    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(3, res.timeline[1].start_time);
    ASSERT_EQ_INT(7, res.timeline[1].end_time);

    /* 7..9: P3 completes */
    ASSERT_EQ_INT(3, res.timeline[2].pid);
    ASSERT_EQ_INT(7, res.timeline[2].start_time);
    ASSERT_EQ_INT(9, res.timeline[2].end_time);

    /* 9..11: P2 in Q1 completes */
    ASSERT_EQ_INT(2, res.timeline[3].pid);
    ASSERT_EQ_INT(9, res.timeline[3].start_time);
    ASSERT_EQ_INT(11, res.timeline[3].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(3, p1->completion_time);
    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(11, p2->completion_time);
    const sched_process_t *p3 = sched_result_get_proc(&res, 3);
    ASSERT_EQ_INT(9, p3->completion_time);

    sched_result_free(&res);
}

static void test_mlfq_staggered_arrivals(void) {
    sched_process_t procs[3];
    sched_process_init(&procs[0], 1, 0, 16, 0);
    sched_process_init(&procs[1], 2, 2, 2, 0);
    sched_process_init(&procs[2], 3, 6, 2, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_mlfq(procs, 3, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(3, res.process_count);
    ASSERT_EQ_INT(4, res.timeline_count);

    /* 0..4: P1 (Q0) */
    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(4, res.timeline[0].end_time);

    /* 4..6: P2 (Q0) completes */
    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(4, res.timeline[1].start_time);
    ASSERT_EQ_INT(6, res.timeline[1].end_time);

    /* 6..8: P3 (Q0) completes */
    ASSERT_EQ_INT(3, res.timeline[2].pid);
    ASSERT_EQ_INT(6, res.timeline[2].start_time);
    ASSERT_EQ_INT(8, res.timeline[2].end_time);

    /* 8..20: P1 (8..16 in Q1 + 16..20 in Q2) completes */
    ASSERT_EQ_INT(1, res.timeline[3].pid);
    ASSERT_EQ_INT(8, res.timeline[3].start_time);
    ASSERT_EQ_INT(20, res.timeline[3].end_time);

    sched_result_free(&res);
}

static void test_mlfq_higher_priority_preemption_q2(void) {
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 20, 0);
    sched_process_init(&procs[1], 2, 15, 3, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_mlfq(procs, 2, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(2, res.process_count);
    ASSERT_EQ_INT(3, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(15, res.timeline[0].end_time);

    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(15, res.timeline[1].start_time);
    ASSERT_EQ_INT(18, res.timeline[1].end_time);

    ASSERT_EQ_INT(1, res.timeline[2].pid);
    ASSERT_EQ_INT(18, res.timeline[2].start_time);
    ASSERT_EQ_INT(23, res.timeline[2].end_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(18, p2->completion_time);
    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(23, p1->completion_time);

    sched_result_free(&res);
}

static void test_mlfq_higher_priority_preemption_q1(void) {
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 10, 0);
    sched_process_init(&procs[1], 2, 6, 2, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_mlfq(procs, 2, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(2, res.process_count);
    ASSERT_EQ_INT(3, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(6, res.timeline[0].end_time);

    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(6, res.timeline[1].start_time);
    ASSERT_EQ_INT(8, res.timeline[1].end_time);

    ASSERT_EQ_INT(1, res.timeline[2].pid);
    ASSERT_EQ_INT(8, res.timeline[2].start_time);
    ASSERT_EQ_INT(12, res.timeline[2].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(12, p1->completion_time);
    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(8, p2->completion_time);

    sched_result_free(&res);
}

static void test_mlfq_exact_quantum_boundary_arrival(void) {
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 8, 0);
    sched_process_init(&procs[1], 2, 4, 3, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_mlfq(procs, 2, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(2, res.process_count);
    ASSERT_EQ_INT(3, res.timeline_count);

    /* 0..4: P1 (Q0) */
    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(4, res.timeline[0].end_time);

    /* 4..7: P2 (Q0) runs before P1 in Q1! */
    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(4, res.timeline[1].start_time);
    ASSERT_EQ_INT(7, res.timeline[1].end_time);

    /* 7..11: P1 (Q1) */
    ASSERT_EQ_INT(1, res.timeline[2].pid);
    ASSERT_EQ_INT(7, res.timeline[2].start_time);
    ASSERT_EQ_INT(11, res.timeline[2].end_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(7, p2->completion_time);
    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(11, p1->completion_time);

    sched_result_free(&res);
}

static void test_mlfq_priority_boost_at_200_ticks(void) {
    sched_process_t procs[1];
    sched_process_init(&procs[0], 1, 0, 250, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_mlfq(procs, 1, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(1, res.process_count);
    ASSERT_EQ_INT(1, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(250, res.timeline[0].end_time);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(0, p1->start_time);
    ASSERT_EQ_INT(250, p1->completion_time);
    ASSERT_EQ_INT(0, p1->remaining_time);
    ASSERT_EQ_INT(250, p1->turnaround_time);
    ASSERT_EQ_INT(0, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    ASSERT_EQ_INT(250, res.total_elapsed_time);
    ASSERT_EQ_INT(250, res.total_busy_time);
    ASSERT_EQ_INT(0, res.total_idle_time);

    sched_result_free(&res);
}

static void test_mlfq_multiple_boosts_over_400_ticks(void) {
    sched_process_t procs[1];
    sched_process_init(&procs[0], 1, 0, 450, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_mlfq(procs, 1, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(1, res.process_count);
    ASSERT_EQ_INT(1, res.timeline_count);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(450, p1->completion_time);
    ASSERT_EQ_INT(450, res.total_elapsed_time);

    sched_result_free(&res);
}

static void test_mlfq_boost_ordering_and_allotment_reset(void) {
    /*
     * Custom MLFQ: q0=4, q1=8, boost=24.
     * P1 (burst 30, arr 0), P2 (burst 30, arr 0).
     * 0..4: P1 (Q0)
     * 4..8: P2 (Q0)
     * 8..16: P1 (Q1) -> demotes to Q2 at 16
     * 16..24: P2 (Q1) -> demotes to Q2 at 24
     * At 24: Boost occurs! Q2 order is [P1, P2]. Both move to Q0 with allotment=4.
     * 24..28: P1 in Q0 (4 ticks)
     * 28..32: P2 in Q0 (4 ticks)
     */
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 30, 0);
    sched_process_init(&procs[1], 2, 0, 30, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_mlfq_custom(procs, 2, 4, 8, 24, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(2, res.process_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(4, res.timeline[0].end_time);

    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(4, res.timeline[1].start_time);
    ASSERT_EQ_INT(8, res.timeline[1].end_time);

    ASSERT_EQ_INT(1, res.timeline[2].pid);
    ASSERT_EQ_INT(8, res.timeline[2].start_time);
    ASSERT_EQ_INT(16, res.timeline[2].end_time);

    ASSERT_EQ_INT(2, res.timeline[3].pid);
    ASSERT_EQ_INT(16, res.timeline[3].start_time);
    ASSERT_EQ_INT(24, res.timeline[3].end_time);

    /* Post-boost execution in Q0: P1 then P2 */
    ASSERT_EQ_INT(1, res.timeline[4].pid);
    ASSERT_EQ_INT(24, res.timeline[4].start_time);
    ASSERT_EQ_INT(28, res.timeline[4].end_time);

    ASSERT_EQ_INT(2, res.timeline[5].pid);
    ASSERT_EQ_INT(28, res.timeline[5].start_time);
    ASSERT_EQ_INT(32, res.timeline[5].end_time);

    sched_result_free(&res);
}

static void test_mlfq_starvation_prevention_boost(void) {
    /*
     * P1: long job (arr 0, burst 250).
     * P2: short job (arr 195, burst 3).
     * P3: short job (arr 198, burst 3).
     *
     * P1 runs 0..4 (Q0), 4..12 (Q1), 12..195 (Q2).
     * At 195, P2 arrives in Q0 -> preempts P1 in Q2!
     * 195..198: P2 runs in Q0 to completion at 198.
     * At 198, P3 arrives in Q0.
     * 198..200: P3 runs 2 ticks in Q0.
     * At 200, boost triggers!
     * P3 was at head of Q0 (1 tick left, resets to 4).
     * P1 (in Q2) is boosted to tail of Q0 (gets 4 ticks).
     * 200..201: P3 finishes its remaining 1 tick!
     * 201..205: P1 runs in Q0! (prevented starvation).
     */
    sched_process_t procs[3];
    sched_process_init(&procs[0], 1, 0, 250, 0);
    sched_process_init(&procs[1], 2, 195, 3, 0);
    sched_process_init(&procs[2], 3, 198, 3, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_mlfq(procs, 3, &res);
    ASSERT_EQ_INT(0, rc);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(198, p2->completion_time);
    const sched_process_t *p3 = sched_result_get_proc(&res, 3);
    ASSERT_EQ_INT(201, p3->completion_time);
    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(256, p1->completion_time);

    sched_result_free(&res);
}

static void test_mlfq_idle_periods(void) {
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 3, 0);
    sched_process_init(&procs[1], 2, 8, 4, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_mlfq(procs, 2, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(2, res.process_count);
    ASSERT_EQ_INT(3, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(3, res.timeline[0].end_time);

    ASSERT_EQ_INT(SCHED_PID_IDLE, res.timeline[1].pid);
    ASSERT_EQ_INT(3, res.timeline[1].start_time);
    ASSERT_EQ_INT(8, res.timeline[1].end_time);

    ASSERT_EQ_INT(2, res.timeline[2].pid);
    ASSERT_EQ_INT(8, res.timeline[2].start_time);
    ASSERT_EQ_INT(12, res.timeline[2].end_time);

    ASSERT_EQ_INT(12, res.total_elapsed_time);
    ASSERT_EQ_INT(7, res.total_busy_time);
    ASSERT_EQ_INT(5, res.total_idle_time);
    ASSERT_DOUBLE_NEAR((7.0 / 12.0) * 100.0, res.cpu_utilization, EPSILON);

    sched_result_free(&res);
}

static void test_mlfq_repeated_pid_slices(void) {
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 8, 0);
    sched_process_init(&procs[1], 2, 0, 8, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_mlfq(procs, 2, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(4, res.timeline_count);

    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(4, res.timeline[0].end_time);

    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(4, res.timeline[1].start_time);
    ASSERT_EQ_INT(8, res.timeline[1].end_time);

    ASSERT_EQ_INT(1, res.timeline[2].pid);
    ASSERT_EQ_INT(8, res.timeline[2].start_time);
    ASSERT_EQ_INT(12, res.timeline[2].end_time);

    ASSERT_EQ_INT(2, res.timeline[3].pid);
    ASSERT_EQ_INT(12, res.timeline[3].start_time);
    ASSERT_EQ_INT(16, res.timeline[3].end_time);

    sched_result_free(&res);
}

static void test_mlfq_metrics_calculation(void) {
    sched_process_t procs[3];
    sched_process_init(&procs[0], 1, 0, 6, 0);
    sched_process_init(&procs[1], 2, 1, 4, 0);
    sched_process_init(&procs[2], 3, 2, 2, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_mlfq(procs, 3, &res);
    ASSERT_EQ_INT(0, rc);

    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_EQ_INT(0, p1->start_time);
    ASSERT_EQ_INT(12, p1->completion_time);
    ASSERT_EQ_INT(12, p1->turnaround_time);
    ASSERT_EQ_INT(6, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    const sched_process_t *p2 = sched_result_get_proc(&res, 2);
    ASSERT_EQ_INT(4, p2->start_time);
    ASSERT_EQ_INT(8, p2->completion_time);
    ASSERT_EQ_INT(7, p2->turnaround_time);
    ASSERT_EQ_INT(3, p2->waiting_time);
    ASSERT_EQ_INT(3, p2->response_time);

    const sched_process_t *p3 = sched_result_get_proc(&res, 3);
    ASSERT_EQ_INT(8, p3->start_time);
    ASSERT_EQ_INT(10, p3->completion_time);
    ASSERT_EQ_INT(8, p3->turnaround_time);
    ASSERT_EQ_INT(6, p3->waiting_time);
    ASSERT_EQ_INT(6, p3->response_time);

    ASSERT_EQ_INT(12, res.total_elapsed_time);
    ASSERT_EQ_INT(12, res.total_busy_time);
    ASSERT_EQ_INT(0, res.total_idle_time);
    ASSERT_DOUBLE_NEAR(100.0, res.cpu_utilization, EPSILON);
    ASSERT_DOUBLE_NEAR(9.0, res.avg_turnaround_time, EPSILON);
    ASSERT_DOUBLE_NEAR(5.0, res.avg_waiting_time, EPSILON);
    ASSERT_DOUBLE_NEAR(3.0, res.avg_response_time, EPSILON);

    sched_result_free(&res);
}

static void test_mlfq_determinism(void) {
    sched_process_t procs[4];
    sched_process_init(&procs[0], 1, 0, 10, 0);
    sched_process_init(&procs[1], 2, 2, 6, 0);
    sched_process_init(&procs[2], 3, 5, 4, 0);
    sched_process_init(&procs[3], 4, 8, 2, 0);

    sched_result_t res1, res2;
    sched_result_init(&res1);
    sched_result_init(&res2);

    ASSERT_EQ_INT(0, sched_schedule_mlfq(procs, 4, &res1));
    ASSERT_EQ_INT(0, sched_schedule_mlfq(procs, 4, &res2));

    ASSERT_EQ_INT(res1.timeline_count, res2.timeline_count);
    for (int i = 0; i < res1.timeline_count; i++) {
        ASSERT_EQ_INT(res1.timeline[i].pid, res2.timeline[i].pid);
        ASSERT_EQ_INT(res1.timeline[i].start_time, res2.timeline[i].start_time);
        ASSERT_EQ_INT(res1.timeline[i].end_time, res2.timeline[i].end_time);
    }

    ASSERT_DOUBLE_NEAR(res1.avg_turnaround_time, res2.avg_turnaround_time, EPSILON);
    ASSERT_DOUBLE_NEAR(res1.avg_waiting_time, res2.avg_waiting_time, EPSILON);
    ASSERT_DOUBLE_NEAR(res1.avg_response_time, res2.avg_response_time, EPSILON);

    sched_result_free(&res1);
    sched_result_free(&res2);
}

static void test_mlfq_edge_cases(void) {
    sched_result_t res;
    sched_result_init(&res);

    /* Zero processes */
    ASSERT_EQ_INT(0, sched_schedule_mlfq(NULL, 0, &res));
    ASSERT_EQ_INT(0, res.process_count);
    ASSERT_EQ_INT(0, res.timeline_count);
    ASSERT_EQ_INT(0, res.total_elapsed_time);

    /* NULL result */
    ASSERT_EQ_INT(-1, sched_schedule_mlfq(NULL, 0, NULL));

    /* NULL procs with count > 0 */
    ASSERT_EQ_INT(-1, sched_schedule_mlfq(NULL, 1, &res));

    /* Negative count */
    ASSERT_EQ_INT(-1, sched_schedule_mlfq(NULL, -1, &res));

    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 5, 0);
    sched_process_init(&procs[1], 2, 0, 5, 0);

    /* Invalid burst */
    procs[0].burst_time = 0;
    ASSERT_EQ_INT(-1, sched_schedule_mlfq(procs, 2, &res));
    procs[0].burst_time = -3;
    ASSERT_EQ_INT(-1, sched_schedule_mlfq(procs, 2, &res));
    procs[0].burst_time = 5;

    /* Invalid arrival */
    procs[0].arrival_time = -1;
    ASSERT_EQ_INT(-1, sched_schedule_mlfq(procs, 2, &res));
    procs[0].arrival_time = 0;

    /* Duplicate PIDs */
    procs[1].pid = 1;
    ASSERT_EQ_INT(-1, sched_schedule_mlfq(procs, 2, &res));
    procs[1].pid = 2;

    /* Custom MLFQ invalid parameters */
    ASSERT_EQ_INT(-1, sched_schedule_mlfq_custom(procs, 2, 0, 8, 200, &res));
    ASSERT_EQ_INT(-1, sched_schedule_mlfq_custom(procs, 2, 4, 0, 200, &res));
    ASSERT_EQ_INT(-1, sched_schedule_mlfq_custom(procs, 2, 4, 8, 0, &res));
    ASSERT_EQ_INT(-1, sched_schedule_mlfq_custom(procs, 2, -1, 8, 200, &res));

    sched_result_free(&res);
}

static void test_gantt_mlfq(void) {
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 5, 0);
    sched_process_init(&procs[1], 2, 2, 3, 0);

    sched_result_t res;
    sched_result_init(&res);

    ASSERT_EQ_INT(0, sched_schedule_mlfq(procs, 2, &res));

    char *chart = sched_render_gantt(&res);
    ASSERT_NOT_NULL(chart);
    ASSERT_TRUE(strstr(chart, "P1") != NULL);
    ASSERT_TRUE(strstr(chart, "P2") != NULL);

    free(chart);
    sched_result_free(&res);
}

static void test_determinism_fcfs(void) {
    sched_process_t procs[3];
    sched_process_init(&procs[0], 1, 0, 6, 0);
    sched_process_init(&procs[1], 2, 2, 4, 0);
    sched_process_init(&procs[2], 3, 4, 3, 0);

    sched_result_t res1, res2;
    sched_result_init(&res1);
    sched_result_init(&res2);

    ASSERT_EQ_INT(0, sched_schedule_fcfs(procs, 3, &res1));
    ASSERT_EQ_INT(0, sched_schedule_fcfs(procs, 3, &res2));

    ASSERT_EQ_INT(res1.timeline_count, res2.timeline_count);
    for (int i = 0; i < res1.timeline_count; i++) {
        ASSERT_EQ_INT(res1.timeline[i].pid, res2.timeline[i].pid);
        ASSERT_EQ_INT(res1.timeline[i].start_time, res2.timeline[i].start_time);
        ASSERT_EQ_INT(res1.timeline[i].end_time, res2.timeline[i].end_time);
    }

    sched_result_free(&res1);
    sched_result_free(&res2);
}

static void test_memory_lifecycle_reuse(void) {
    sched_process_t procs[2];
    sched_process_init(&procs[0], 10, 0, 3, 1);
    sched_process_init(&procs[1], 20, 1, 2, 2);

    sched_result_t res;
    sched_result_init(&res);

    for (int iter = 0; iter < 30; iter++) {
        ASSERT_EQ_INT(0, sched_schedule_fcfs(procs, 2, &res));
        ASSERT_EQ_INT(2, res.timeline_count);

        ASSERT_EQ_INT(0, sched_schedule_sjf(procs, 2, &res));
        ASSERT_EQ_INT(2, res.timeline_count);

        ASSERT_EQ_INT(0, sched_schedule_rr(procs, 2, 2, &res));
        ASSERT_EQ_INT(3, res.timeline_count);

        ASSERT_EQ_INT(0, sched_schedule_priority(procs, 2, &res));
        ASSERT_EQ_INT(2, res.timeline_count);

        ASSERT_EQ_INT(0, sched_schedule_mlfq(procs, 2, &res));
        ASSERT_TRUE(res.timeline_count >= 1);
    }

    sched_result_free(&res);
}

/* ------------------------------------------------------------------------- */
/* Main Test Runner                                                          */
/* ------------------------------------------------------------------------- */

int main(void) {
    TEST_INIT();

    printf("Running Phase 8A, 8B & 8C Scheduler Unit Tests:\n");

    /* FCFS Tests */
    RUN_TEST(test_fcfs_single_process_zero_arrival);
    RUN_TEST(test_fcfs_single_process_nonzero_arrival);
    RUN_TEST(test_fcfs_multiple_arriving_at_zero_tiebreak);
    RUN_TEST(test_fcfs_staggered_arrivals);
    RUN_TEST(test_fcfs_idle_gaps_between_processes);

    /* SJF Tests */
    RUN_TEST(test_sjf_different_burst_lengths);
    RUN_TEST(test_sjf_equal_bursts_and_tie_breaking);
    RUN_TEST(test_sjf_non_preemptive_staggered_arrivals);
    RUN_TEST(test_sjf_idle_before_and_between_jobs);

    /* Round Robin Tests */
    RUN_TEST(test_rr_single_process_quantum_smaller_than_burst);
    RUN_TEST(test_rr_single_process_quantum_larger_than_burst);
    RUN_TEST(test_rr_multiple_processes_arriving_at_zero);
    RUN_TEST(test_rr_staggered_and_new_arrival_during_quantum);
    RUN_TEST(test_rr_process_finishing_at_quantum_boundary);
    RUN_TEST(test_rr_burst_exactly_equal_quantum);
    RUN_TEST(test_rr_arrival_exactly_at_quantum_boundary);
    RUN_TEST(test_rr_idle_periods);
    RUN_TEST(test_rr_invalid_quantum);
    RUN_TEST(test_rr_determinism);

    /* Priority Tests */
    RUN_TEST(test_priority_single_process);
    RUN_TEST(test_priority_different_priorities);
    RUN_TEST(test_priority_same_priority_tie_breaking);
    RUN_TEST(test_priority_non_preemptive_behavior);
    RUN_TEST(test_priority_staggered_arrivals);
    RUN_TEST(test_priority_idle_periods);
    RUN_TEST(test_priority_determinism);

    /* Multilevel Feedback Queue (MLFQ) Tests */
    RUN_TEST(test_mlfq_single_process_completes_in_q0);
    RUN_TEST(test_mlfq_single_process_q0_demotion_to_q1);
    RUN_TEST(test_mlfq_single_process_q1_demotion_to_q2);
    RUN_TEST(test_mlfq_q2_fcfs_behavior_and_no_further_demotion);
    RUN_TEST(test_mlfq_multiple_processes_arriving_at_zero);
    RUN_TEST(test_mlfq_staggered_arrivals);
    RUN_TEST(test_mlfq_higher_priority_preemption_q2);
    RUN_TEST(test_mlfq_higher_priority_preemption_q1);
    RUN_TEST(test_mlfq_exact_quantum_boundary_arrival);
    RUN_TEST(test_mlfq_priority_boost_at_200_ticks);
    RUN_TEST(test_mlfq_multiple_boosts_over_400_ticks);
    RUN_TEST(test_mlfq_boost_ordering_and_allotment_reset);
    RUN_TEST(test_mlfq_starvation_prevention_boost);
    RUN_TEST(test_mlfq_idle_periods);
    RUN_TEST(test_mlfq_repeated_pid_slices);
    RUN_TEST(test_mlfq_metrics_calculation);
    RUN_TEST(test_mlfq_determinism);
    RUN_TEST(test_mlfq_edge_cases);
    RUN_TEST(test_gantt_mlfq);

    /* Gantt Chart Tests */
    RUN_TEST(test_gantt_simple_fcfs);
    RUN_TEST(test_gantt_idle_intervals);
    RUN_TEST(test_gantt_rr_repeated_slices);
    RUN_TEST(test_gantt_buffer_and_empty);

    /* Edge Cases & Lifecycle */
    RUN_TEST(test_edge_case_zero_processes);
    RUN_TEST(test_edge_case_invalid_inputs);
    RUN_TEST(test_determinism_fcfs);
    RUN_TEST(test_memory_lifecycle_reuse);

    TEST_REPORT();
}
