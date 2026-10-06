/*
 * test_job_control.c — Unit tests for Phase 6 Job Control (fg, bg, kill, specs, cleanup).
 */

#include "builtins.h"
#include "job.h"
#include "parser.h"
#include "signals.h"
#include "test_framework.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void test_job_get_current(void)
{
    job_table_init();

    /* 1. Empty table has no current job */
    ASSERT_NULL(job_get_current());

    /* 2. Single running job */
    command_t *cmd1 = parse_command("sleep 10 &", NULL);
    pid_t pids1[1] = { 1001 };
    job_t *job1 = job_create(pids1, cmd1, 1, "sleep 10 &", false);
    ASSERT_NOT_NULL(job1);
    ASSERT_EQ_INT(job1->jid, job_get_current()->jid);

    /* 3. Second running job becomes current (higher JID) */
    command_t *cmd2 = parse_command("sleep 20 &", NULL);
    pid_t pids2[1] = { 1002 };
    job_t *job2 = job_create(pids2, cmd2, 1, "sleep 20 &", false);
    ASSERT_NOT_NULL(job2);
    ASSERT_EQ_INT(job2->jid, job_get_current()->jid);

    /* 4. If job1 is stopped, stopped job takes priority */
    job_update_process_status(1001, 0, PROC_STOPPED);
    ASSERT_EQ_INT(job1->jid, job_get_current()->jid);

    /* 5. If job2 also stopped, highest JID stopped job takes priority */
    job_update_process_status(1002, 0, PROC_STOPPED);
    ASSERT_EQ_INT(job2->jid, job_get_current()->jid);

    free_command(cmd1);
    free_command(cmd2);
    job_table_cleanup();
}

static void test_fg_errors(void)
{
    job_table_init();

    /* 1. fg with no jobs in table */
    command_t *cmd = parse_command("fg", NULL);
    ASSERT_NOT_NULL(cmd);
    bool should_exit = false;
    int exit_code = 0;
    int ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(1, ret);
    ASSERT_FALSE(should_exit);
    free_command(cmd);

    /* 2. fg with nonexistent JID */
    cmd = parse_command("fg %99", NULL);
    ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(1, ret);
    free_command(cmd);

    /* 3. fg with invalid spec */
    cmd = parse_command("fg %invalid", NULL);
    ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(1, ret);
    free_command(cmd);

    cmd = parse_command("fg %", NULL);
    ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(1, ret);
    free_command(cmd);

    /* 4. fg with non-% spec */
    cmd = parse_command("fg 1", NULL);
    ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(1, ret);
    free_command(cmd);

    /* 5. fg with too many arguments */
    cmd = parse_command("fg %1 %2", NULL);
    ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(1, ret);
    free_command(cmd);

    job_table_cleanup();
}

static void test_bg_errors(void)
{
    job_table_init();

    /* 1. bg with no jobs */
    command_t *cmd = parse_command("bg", NULL);
    bool should_exit = false;
    int exit_code = 0;
    int ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(1, ret);
    free_command(cmd);

    /* 2. bg with nonexistent JID */
    cmd = parse_command("bg %99", NULL);
    ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(1, ret);
    free_command(cmd);

    /* 3. bg on a job that is already running (not stopped) */
    command_t *run_cmd = parse_command("sleep 30 &", NULL);
    pid_t pids[1] = { 2001 };
    job_t *job = job_create(pids, run_cmd, 1, "sleep 30 &", false);
    ASSERT_NOT_NULL(job);

    cmd = parse_command("bg %1", NULL);
    ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(1, ret);
    free_command(cmd);

    free_command(run_cmd);
    job_table_cleanup();
}

static void test_kill_builtin(void)
{
    job_table_init();

    /* 1. kill with no arguments */
    command_t *cmd = parse_command("kill", NULL);
    bool should_exit = false;
    int exit_code = 0;
    int ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(1, ret);
    free_command(cmd);

    /* 2. kill with invalid signal specification */
    cmd = parse_command("kill -INVALIDSIG %1", NULL);
    ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(1, ret);
    free_command(cmd);

    /* 3. kill with nonexistent JID */
    cmd = parse_command("kill %99", NULL);
    ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(1, ret);
    free_command(cmd);

    cmd = parse_command("kill -TERM %99", NULL);
    ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(1, ret);
    free_command(cmd);

    /* 4. kill cannot kill own shell process */
    char kill_self_str[64];
    snprintf(kill_self_str, sizeof(kill_self_str), "kill %d", (int)getpid());
    cmd = parse_command(kill_self_str, NULL);
    ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(1, ret);
    free_command(cmd);

    /* 5. kill with non-numeric, non-% target */
    cmd = parse_command("kill abc", NULL);
    ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(1, ret);
    free_command(cmd);

    /* 6. kill -9 with invalid job spec */
    cmd = parse_command("kill -9 %abc", NULL);
    ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(1, ret);
    free_command(cmd);

    job_table_cleanup();
}

static void test_job_completion_cleanup(void)
{
    job_table_init();

    command_t *cmd = parse_command("sleep 5 &", NULL);
    pid_t pids[1] = { 3001 };
    job_t *job = job_create(pids, cmd, 1, "sleep 5 &", false);
    ASSERT_NOT_NULL(job);
    ASSERT_EQ_INT(1, job->jid);
    ASSERT_EQ_INT(1, job_table_active_count());

    /* While running, cleanup does not remove it */
    job_notify_and_cleanup();
    ASSERT_EQ_INT(1, job_table_active_count());

    /* Mark completed */
    job_update_process_status(3001, 0, PROC_COMPLETED);
    ASSERT_TRUE(job_is_completed(job));

    /* notify_and_cleanup should print notification and remove it */
    job_notify_and_cleanup();
    ASSERT_EQ_INT(0, job_table_active_count());
    ASSERT_NULL(job_find_by_jid(1));

    /* Slot should be reusable for JID 1 */
    job_t *reused = job_create(pids, cmd, 1, "sleep 5 &", false);
    ASSERT_NOT_NULL(reused);
    ASSERT_EQ_INT(1, reused->jid);

    free_command(cmd);
    job_table_cleanup();
}

static void test_job_table_print_omits_completed(void)
{
    job_table_init();

    command_t *cmd1 = parse_command("running_cmd &", NULL);
    pid_t pids1[1] = { 4001 };
    job_t *job1 = job_create(pids1, cmd1, 1, "running_cmd &", false);

    command_t *cmd2 = parse_command("done_cmd &", NULL);
    pid_t pids2[1] = { 4002 };
    job_t *job2 = job_create(pids2, cmd2, 1, "done_cmd &", false);
    job_update_process_status(4002, 0, PROC_COMPLETED);

    /* Verify job1 is running and job2 is completed */
    ASSERT_TRUE(job_is_running(job1));
    ASSERT_TRUE(job_is_completed(job2));

    /* job_table_print should only list job1, not job2 */
    /* We verify job_table_print executes cleanly */
    job_table_print();

    free_command(cmd1);
    free_command(cmd2);
    job_table_cleanup();
}

int main(void)
{
    TEST_INIT();

    printf("Running Phase 6 Job Control Unit Tests:\n");
    RUN_TEST(test_job_get_current);
    RUN_TEST(test_fg_errors);
    RUN_TEST(test_bg_errors);
    RUN_TEST(test_kill_builtin);
    RUN_TEST(test_job_completion_cleanup);
    RUN_TEST(test_job_table_print_omits_completed);

    TEST_REPORT();
}
