/*
 * test_scheduler.c — Unit tests for Phase 8A user-space CPU scheduling simulation.
 */

#include "scheduler.h"
#include "test_framework.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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
/* A. FCFS Tests                                                             */
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

    /* Timeline check: [0, 5] PID 1 */
    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(5, res.timeline[0].end_time);

    /* Process metrics */
    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_NOT_NULL(p1);
    ASSERT_EQ_INT(0, p1->start_time);
    ASSERT_EQ_INT(5, p1->completion_time);
    ASSERT_EQ_INT(5, p1->turnaround_time);
    ASSERT_EQ_INT(0, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    /* Aggregate metrics */
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

    /* Timeline check: [0, 4] IDLE, [4, 7] PID 1 */
    ASSERT_EQ_INT(SCHED_PID_IDLE, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(4, res.timeline[0].end_time);

    ASSERT_EQ_INT(1, res.timeline[1].pid);
    ASSERT_EQ_INT(4, res.timeline[1].start_time);
    ASSERT_EQ_INT(7, res.timeline[1].end_time);

    /* Process metrics */
    const sched_process_t *p1 = sched_result_get_proc(&res, 1);
    ASSERT_NOT_NULL(p1);
    ASSERT_EQ_INT(4, p1->start_time);
    ASSERT_EQ_INT(7, p1->completion_time);
    ASSERT_EQ_INT(3, p1->turnaround_time);
    ASSERT_EQ_INT(0, p1->waiting_time);
    ASSERT_EQ_INT(0, p1->response_time);

    /* Aggregate metrics */
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
    /* Given P2, P1, P3 all arriving at 0 in arbitrary input order */
    sched_process_t procs[3];
    sched_process_init(&procs[0], 2, 0, 4, 0);
    sched_process_init(&procs[1], 1, 0, 3, 0);
    sched_process_init(&procs[2], 3, 0, 2, 0);

    sched_result_t res;
    sched_result_init(&res);

    int rc = sched_schedule_fcfs(procs, 3, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(3, res.timeline_count);

    /* Order must be P1 (pid 1), P2 (pid 2), P3 (pid 3) */
    ASSERT_EQ_INT(1, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(3, res.timeline[0].end_time);

    ASSERT_EQ_INT(2, res.timeline[1].pid);
    ASSERT_EQ_INT(3, res.timeline[1].start_time);
    ASSERT_EQ_INT(7, res.timeline[1].end_time);

    ASSERT_EQ_INT(3, res.timeline[2].pid);
    ASSERT_EQ_INT(7, res.timeline[2].start_time);
    ASSERT_EQ_INT(9, res.timeline[2].end_time);

    /* Check per-process metrics */
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

    /* Summary: elapsed=9, busy=9, idle=0, util=100% */
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

    /* [0, 1] IDLE */
    ASSERT_EQ_INT(SCHED_PID_IDLE, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(1, res.timeline[0].end_time);

    /* [1, 5] P1 */
    ASSERT_EQ_INT(1, res.timeline[1].pid);
    ASSERT_EQ_INT(1, res.timeline[1].start_time);
    ASSERT_EQ_INT(5, res.timeline[1].end_time);

    /* [5, 8] IDLE */
    ASSERT_EQ_INT(SCHED_PID_IDLE, res.timeline[2].pid);
    ASSERT_EQ_INT(5, res.timeline[2].start_time);
    ASSERT_EQ_INT(8, res.timeline[2].end_time);

    /* [8, 11] P2 */
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
/* B. SJF Tests                                                              */
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

    /* Shortest burst executes first: P4 (1), P2 (2), P3 (5), P1 (7) */
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

    /*
     * At t=0: idle [0, 1]
     * At t=1: P2 and P3 arrived. Same burst 4, same arrival 1 -> P2 has lower PID (2 < 3)
     * P2 runs [1, 5]
     * At t=5: P3 (arr=1) and P1 (arr=2) ready. Same burst 4 -> P3 arrived earlier (1 < 2)
     * P3 runs [5, 9]
     * At t=9: P1 runs [9, 13]
     */
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
    /*
     * SJF is non-preemptive.
     * P1 arrives at 0 with burst 8.
     * P2 arrives at 1 with burst 2.
     * P3 arrives at 2 with burst 3.
     * P1 must execute to completion (0 to 8).
     * Then at 8, both P2 and P3 are ready; P2 (burst 2) runs (8 to 10).
     * Then P3 runs (10 to 13).
     */
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

    /* [0, 3] IDLE */
    ASSERT_EQ_INT(SCHED_PID_IDLE, res.timeline[0].pid);
    ASSERT_EQ_INT(0, res.timeline[0].start_time);
    ASSERT_EQ_INT(3, res.timeline[0].end_time);

    /* [3, 5] P1 */
    ASSERT_EQ_INT(1, res.timeline[1].pid);
    ASSERT_EQ_INT(3, res.timeline[1].start_time);
    ASSERT_EQ_INT(5, res.timeline[1].end_time);

    /* [5, 10] IDLE */
    ASSERT_EQ_INT(SCHED_PID_IDLE, res.timeline[2].pid);
    ASSERT_EQ_INT(5, res.timeline[2].start_time);
    ASSERT_EQ_INT(10, res.timeline[2].end_time);

    /* [10, 14] P2 */
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
/* C. Edge Cases & Validation                                                */
/* ------------------------------------------------------------------------- */

static void test_edge_case_zero_processes(void) {
    sched_result_t res;
    sched_result_init(&res);

    /* FCFS zero processes */
    int rc = sched_schedule_fcfs(NULL, 0, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(0, res.process_count);
    ASSERT_EQ_INT(0, res.timeline_count);
    ASSERT_EQ_INT(0, res.total_elapsed_time);
    ASSERT_EQ_INT(0, res.total_busy_time);
    ASSERT_EQ_INT(0, res.total_idle_time);
    ASSERT_DOUBLE_NEAR(0.0, res.cpu_utilization, EPSILON);
    ASSERT_DOUBLE_NEAR(0.0, res.avg_turnaround_time, EPSILON);
    ASSERT_DOUBLE_NEAR(0.0, res.avg_waiting_time, EPSILON);
    ASSERT_DOUBLE_NEAR(0.0, res.avg_response_time, EPSILON);
    sched_result_free(&res);

    /* SJF zero processes */
    rc = sched_schedule_sjf(NULL, 0, &res);
    ASSERT_EQ_INT(0, rc);
    ASSERT_EQ_INT(0, res.process_count);
    ASSERT_EQ_INT(0, res.timeline_count);
    ASSERT_EQ_INT(0, res.total_elapsed_time);
    ASSERT_EQ_INT(0, res.total_busy_time);
    ASSERT_EQ_INT(0, res.total_idle_time);
    ASSERT_DOUBLE_NEAR(0.0, res.cpu_utilization, EPSILON);
    ASSERT_DOUBLE_NEAR(0.0, res.avg_turnaround_time, EPSILON);
    ASSERT_DOUBLE_NEAR(0.0, res.avg_waiting_time, EPSILON);
    ASSERT_DOUBLE_NEAR(0.0, res.avg_response_time, EPSILON);
    sched_result_free(&res);
}

static void test_edge_case_invalid_inputs(void) {
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 5, 0);
    sched_process_init(&procs[1], 2, 0, 3, 0);

    sched_result_t res;
    sched_result_init(&res);

    /* NULL result pointer */
    ASSERT_EQ_INT(-1, sched_schedule_fcfs(procs, 2, NULL));
    ASSERT_EQ_INT(-1, sched_schedule_sjf(procs, 2, NULL));

    /* Negative count */
    ASSERT_EQ_INT(-1, sched_schedule_fcfs(procs, -1, &res));
    ASSERT_EQ_INT(-1, sched_schedule_sjf(procs, -1, &res));

    /* NULL procs with count > 0 */
    ASSERT_EQ_INT(-1, sched_schedule_fcfs(NULL, 2, &res));
    ASSERT_EQ_INT(-1, sched_schedule_sjf(NULL, 2, &res));

    /* Burst time <= 0 */
    procs[0].burst_time = 0;
    ASSERT_EQ_INT(-1, sched_schedule_fcfs(procs, 2, &res));
    ASSERT_EQ_INT(-1, sched_schedule_sjf(procs, 2, &res));
    procs[0].burst_time = 5;

    /* Negative arrival time */
    procs[1].arrival_time = -3;
    ASSERT_EQ_INT(-1, sched_schedule_fcfs(procs, 2, &res));
    ASSERT_EQ_INT(-1, sched_schedule_sjf(procs, 2, &res));
    procs[1].arrival_time = 0;

    /* Idle PID passed as process PID */
    procs[0].pid = SCHED_PID_IDLE;
    ASSERT_EQ_INT(-1, sched_schedule_fcfs(procs, 2, &res));
    ASSERT_EQ_INT(-1, sched_schedule_sjf(procs, 2, &res));
    procs[0].pid = 1;

    /* Duplicate PID */
    procs[1].pid = 1;
    ASSERT_EQ_INT(-1, sched_schedule_fcfs(procs, 2, &res));
    ASSERT_EQ_INT(-1, sched_schedule_sjf(procs, 2, &res));
    procs[1].pid = 2;

    sched_result_free(&res);
}

/* ------------------------------------------------------------------------- */
/* D. Determinism & Lifecycle                                                */
/* ------------------------------------------------------------------------- */

static void test_determinism(void) {
    sched_process_t procs[3];
    sched_process_init(&procs[0], 1, 0, 6, 0);
    sched_process_init(&procs[1], 2, 2, 4, 0);
    sched_process_init(&procs[2], 3, 4, 3, 0);

    sched_result_t res1, res2;
    sched_result_init(&res1);
    sched_result_init(&res2);

    /* Run twice on fresh structures */
    ASSERT_EQ_INT(0, sched_schedule_fcfs(procs, 3, &res1));
    ASSERT_EQ_INT(0, sched_schedule_fcfs(procs, 3, &res2));

    ASSERT_EQ_INT(res1.timeline_count, res2.timeline_count);
    for (int i = 0; i < res1.timeline_count; i++) {
        ASSERT_EQ_INT(res1.timeline[i].pid, res2.timeline[i].pid);
        ASSERT_EQ_INT(res1.timeline[i].start_time, res2.timeline[i].start_time);
        ASSERT_EQ_INT(res1.timeline[i].end_time, res2.timeline[i].end_time);
    }
    ASSERT_DOUBLE_NEAR(res1.avg_turnaround_time, res2.avg_turnaround_time, EPSILON);
    ASSERT_DOUBLE_NEAR(res1.avg_waiting_time, res2.avg_waiting_time, EPSILON);

    sched_result_free(&res1);
    sched_result_free(&res2);
}

static void test_memory_lifecycle_reuse(void) {
    sched_process_t procs[2];
    sched_process_init(&procs[0], 10, 0, 3, 0);
    sched_process_init(&procs[1], 20, 1, 2, 0);

    sched_result_t res;
    sched_result_init(&res);

    for (int iter = 0; iter < 50; iter++) {
        ASSERT_EQ_INT(0, sched_schedule_fcfs(procs, 2, &res));
        ASSERT_EQ_INT(2, res.timeline_count);
        ASSERT_EQ_INT(0, sched_schedule_sjf(procs, 2, &res));
        ASSERT_EQ_INT(2, res.timeline_count);
    }

    sched_result_free(&res);
}

/* ------------------------------------------------------------------------- */
/* Main Test Runner                                                          */
/* ------------------------------------------------------------------------- */

int main(void) {
    TEST_INIT();

    printf("Running Phase 8A Scheduler Foundation Unit Tests:\n");

    RUN_TEST(test_fcfs_single_process_zero_arrival);
    RUN_TEST(test_fcfs_single_process_nonzero_arrival);
    RUN_TEST(test_fcfs_multiple_arriving_at_zero_tiebreak);
    RUN_TEST(test_fcfs_staggered_arrivals);
    RUN_TEST(test_fcfs_idle_gaps_between_processes);

    RUN_TEST(test_sjf_different_burst_lengths);
    RUN_TEST(test_sjf_equal_bursts_and_tie_breaking);
    RUN_TEST(test_sjf_non_preemptive_staggered_arrivals);
    RUN_TEST(test_sjf_idle_before_and_between_jobs);

    RUN_TEST(test_edge_case_zero_processes);
    RUN_TEST(test_edge_case_invalid_inputs);

    RUN_TEST(test_determinism);
    RUN_TEST(test_memory_lifecycle_reuse);

    TEST_REPORT();
}
