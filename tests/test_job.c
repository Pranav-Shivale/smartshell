/*
 * test_job.c — Unit tests for SmartShell job and process management foundation.
 */

#include "job.h"
#include "parser.h"
#include "test_framework.h"

#include <stdlib.h>

static void test_job_table_init_and_empty(void)
{
    job_table_init();
    ASSERT_EQ_INT(0, job_table_active_count());
    ASSERT_NULL(job_find_by_jid(1));
    ASSERT_NULL(job_find_by_pid(1234));
    ASSERT_NULL(job_find_by_pgid(1234));
    job_table_cleanup();
}

static void test_job_create_single(void)
{
    job_table_init();

    command_t *cmd = parse_command("ls -la /tmp", NULL);
    ASSERT_NOT_NULL(cmd);

    pid_t pids[1] = { 1001 };
    job_t *job = job_create(pids, cmd, 1, "ls -la /tmp", true);

    ASSERT_NOT_NULL(job);
    ASSERT_EQ_INT(1, job->jid);
    ASSERT_EQ_INT(1001, job->pgid);
    ASSERT_EQ_INT(1, job->process_count);
    ASSERT_TRUE(job->is_foreground);
    ASSERT_TRUE(job_is_running(job));
    ASSERT_EQ_STR("ls -la /tmp", job->command_line);

    ASSERT_EQ_INT(1001, job->processes[0].pid);
    ASSERT_EQ_INT(PROC_RUNNING, job->processes[0].state);
    ASSERT_EQ_INT(3, job->processes[0].argc);
    ASSERT_EQ_STR("ls", job->processes[0].argv[0]);
    ASSERT_EQ_STR("-la", job->processes[0].argv[1]);
    ASSERT_EQ_STR("/tmp", job->processes[0].argv[2]);
    ASSERT_NULL(job->processes[0].argv[3]);

    ASSERT_EQ_INT(1, job_table_active_count());

    free_command(cmd);
    job_table_cleanup();
}

static void test_job_create_pipeline(void)
{
    job_table_init();

    command_t *cmd = parse_command("cat file.txt | grep text | wc -l", NULL);
    ASSERT_NOT_NULL(cmd);

    pid_t pids[3] = { 2001, 2002, 2003 };
    job_t *job = job_create(pids, cmd, 3, "cat file.txt | grep text | wc -l", true);

    ASSERT_NOT_NULL(job);
    ASSERT_EQ_INT(1, job->jid);
    ASSERT_EQ_INT(2001, job->pgid); /* Leader PID */
    ASSERT_EQ_INT(3, job->process_count);
    ASSERT_TRUE(job_is_running(job));

    /* Process 1 */
    ASSERT_EQ_INT(2001, job->processes[0].pid);
    ASSERT_EQ_INT(2, job->processes[0].argc);
    ASSERT_EQ_STR("cat", job->processes[0].argv[0]);
    ASSERT_EQ_STR("file.txt", job->processes[0].argv[1]);

    /* Process 2 */
    ASSERT_EQ_INT(2002, job->processes[1].pid);
    ASSERT_EQ_INT(2, job->processes[1].argc);
    ASSERT_EQ_STR("grep", job->processes[1].argv[0]);
    ASSERT_EQ_STR("text", job->processes[1].argv[1]);

    /* Process 3 */
    ASSERT_EQ_INT(2003, job->processes[2].pid);
    ASSERT_EQ_INT(2, job->processes[2].argc);
    ASSERT_EQ_STR("wc", job->processes[2].argv[0]);
    ASSERT_EQ_STR("-l", job->processes[2].argv[1]);

    free_command(cmd);
    job_table_cleanup();
}

static void test_job_lookups(void)
{
    job_table_init();

    command_t *cmd1 = parse_command("echo hello", NULL);
    pid_t pids1[1] = { 3001 };
    job_t *job1 = job_create(pids1, cmd1, 1, "echo hello", true);

    command_t *cmd2 = parse_command("cat | grep x", NULL);
    pid_t pids2[2] = { 3002, 3003 };
    job_t *job2 = job_create(pids2, cmd2, 2, "cat | grep x", true);

    ASSERT_NOT_NULL(job1);
    ASSERT_NOT_NULL(job2);

    /* Lookup by JID */
    ASSERT_EQ_INT(job1->jid, job_find_by_jid(1)->jid);
    ASSERT_EQ_INT(job2->jid, job_find_by_jid(2)->jid);
    ASSERT_NULL(job_find_by_jid(999));
    ASSERT_NULL(job_find_by_jid(-1));

    /* Lookup by PID */
    ASSERT_EQ_INT(1, job_find_by_pid(3001)->jid);
    ASSERT_EQ_INT(2, job_find_by_pid(3002)->jid);
    ASSERT_EQ_INT(2, job_find_by_pid(3003)->jid);
    ASSERT_NULL(job_find_by_pid(9999));

    /* Lookup by PGID */
    ASSERT_EQ_INT(1, job_find_by_pgid(3001)->jid);
    ASSERT_EQ_INT(2, job_find_by_pgid(3002)->jid);
    ASSERT_NULL(job_find_by_pgid(3003)); /* 3003 is not leader */
    ASSERT_NULL(job_find_by_pgid(9999));

    free_command(cmd1);
    free_command(cmd2);
    job_table_cleanup();
}

static void test_job_state_transitions(void)
{
    job_table_init();

    command_t *cmd = parse_command("p1 | p2 | p3", NULL);
    pid_t pids[3] = { 4001, 4002, 4003 };
    job_t *job = job_create(pids, cmd, 3, "p1 | p2 | p3", true);

    ASSERT_NOT_NULL(job);
    ASSERT_TRUE(job_is_running(job));

    /* Process 1 completes */
    ASSERT_TRUE(job_update_process_status(4001, 0, PROC_COMPLETED));
    ASSERT_TRUE(job_is_running(job)); /* 4002 and 4003 still running */

    /* Process 2 stops */
    ASSERT_TRUE(job_update_process_status(4002, 0, PROC_STOPPED));
    ASSERT_TRUE(job_is_running(job)); /* 4003 still running */

    /* Process 3 stops -> all non-completed processes are stopped */
    ASSERT_TRUE(job_update_process_status(4003, 0, PROC_STOPPED));
    ASSERT_TRUE(job_is_stopped(job));

    /* Process 2 completes while 3 is still stopped */
    ASSERT_TRUE(job_update_process_status(4002, 0, PROC_COMPLETED));
    ASSERT_TRUE(job_is_stopped(job));

    /* Process 3 completes -> all processes completed */
    ASSERT_TRUE(job_update_process_status(4003, 0, PROC_COMPLETED));
    ASSERT_TRUE(job_is_completed(job));

    /* Updating nonexistent process returns false */
    ASSERT_FALSE(job_update_process_status(9999, 0, PROC_COMPLETED));

    free_command(cmd);
    job_table_cleanup();
}

static void test_job_remove(void)
{
    job_table_init();

    command_t *cmd = parse_command("sleep 10", NULL);
    pid_t pids[1] = { 5001 };
    job_t *job = job_create(pids, cmd, 1, "sleep 10", true);
    ASSERT_NOT_NULL(job);
    ASSERT_EQ_INT(1, job_table_active_count());

    ASSERT_TRUE(job_remove(job->jid));
    ASSERT_EQ_INT(0, job_table_active_count());
    ASSERT_NULL(job_find_by_jid(1));
    ASSERT_NULL(job_find_by_pid(5001));

    /* Removing non-existent JID */
    ASSERT_FALSE(job_remove(999));

    free_command(cmd);
    job_table_cleanup();
}

static void test_job_table_bounded_capacity(void)
{
    job_table_init();

    command_t *cmd = parse_command("noop", NULL);

    /* Fill table up to MAX_JOBS */
    for (int i = 0; i < MAX_JOBS; i++) {
        pid_t p = (pid_t)(6000 + i);
        job_t *j = job_create(&p, cmd, 1, "noop", true);
        ASSERT_NOT_NULL(j);
        ASSERT_EQ_INT(i + 1, j->jid);
    }

    ASSERT_EQ_INT(MAX_JOBS, job_table_active_count());

    /* Adding one more must fail */
    pid_t overflow_pid = 7000;
    job_t *overflow_job = job_create(&overflow_pid, cmd, 1, "noop", true);
    ASSERT_NULL(overflow_job);
    ASSERT_EQ_INT(MAX_JOBS, job_table_active_count());

    /* Removing job 5 should free up a slot */
    ASSERT_TRUE(job_remove(5));
    ASSERT_EQ_INT(MAX_JOBS - 1, job_table_active_count());

    /* Next created job should reuse JID 5 */
    job_t *reused_job = job_create(&overflow_pid, cmd, 1, "noop", true);
    ASSERT_NOT_NULL(reused_job);
    ASSERT_EQ_INT(5, reused_job->jid);
    ASSERT_EQ_INT(MAX_JOBS, job_table_active_count());

    free_command(cmd);
    job_table_cleanup();
}

static void test_job_table_cleanup(void)
{
    job_table_init();

    command_t *cmd = parse_command("cmd", NULL);
    for (int i = 0; i < 5; i++) {
        pid_t p = (pid_t)(8000 + i);
        job_create(&p, cmd, 1, "cmd", true);
    }

    ASSERT_EQ_INT(5, job_table_active_count());
    job_table_cleanup();
    ASSERT_EQ_INT(0, job_table_active_count());

    free_command(cmd);
}

int main(void)
{
    TEST_INIT();

    printf("Running Job Management Unit Tests:\n");
    RUN_TEST(test_job_table_init_and_empty);
    RUN_TEST(test_job_create_single);
    RUN_TEST(test_job_create_pipeline);
    RUN_TEST(test_job_lookups);
    RUN_TEST(test_job_state_transitions);
    RUN_TEST(test_job_remove);
    RUN_TEST(test_job_table_bounded_capacity);
    RUN_TEST(test_job_table_cleanup);

    TEST_REPORT();
}
