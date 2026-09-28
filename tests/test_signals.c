/*
 * test_signals.c — Unit tests for SmartShell signals, process groups, and job control.
 */

#include "job.h"
#include "parser.h"
#include "signals.h"
#include "test_framework.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void test_signals_init_and_pipe(void)
{
    signals_init();

    int read_fd = signals_get_self_pipe_read_fd();
    ASSERT_TRUE(read_fd >= 0);

    /* Verify non-blocking status */
    int flags = fcntl(read_fd, F_GETFL, 0);
    ASSERT_TRUE(flags >= 0);
    ASSERT_TRUE((flags & O_NONBLOCK) != 0);

    signals_cleanup();
}

static void test_signals_block_unblock(void)
{
    sigset_t prev_mask, cur_mask;

    signals_block_sigchld(&prev_mask);
    sigprocmask(0, NULL, &cur_mask);
    ASSERT_TRUE(sigismember(&cur_mask, SIGCHLD) == 1);

    signals_unblock_sigchld(&prev_mask);
    sigprocmask(0, NULL, &cur_mask);
    ASSERT_TRUE(sigismember(&cur_mask, SIGCHLD) == 0);
}

static void test_child_process_group_creation(void)
{
    pid_t pid = fork();
    ASSERT_TRUE(pid >= 0);

    if (pid == 0) {
        /* Child */
        setpgid(0, 0);
        pid_t my_pgrp = getpgrp();
        if (my_pgrp == getpid()) {
            _exit(0);
        } else {
            _exit(1);
        }
    }

    /* Parent */
    setpgid(pid, pid);

    int status = 0;
    waitpid(pid, &status, 0);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ_INT(0, WEXITSTATUS(status));
}

static void test_sigchld_event_processing_exit(void)
{
    job_table_init();
    signals_init();

    sigset_t prev_mask;
    signals_block_sigchld(&prev_mask);

    pid_t pid = fork();
    ASSERT_TRUE(pid >= 0);

    if (pid == 0) {
        signals_unblock_sigchld(&prev_mask);
        setpgid(0, 0);
        _exit(42);
    }

    setpgid(pid, pid);

    command_t *cmd = parse_command("test_exit", NULL);
    job_t *job = job_create(&pid, cmd, 1, "test_exit", true);
    ASSERT_NOT_NULL(job);

    signals_unblock_sigchld(&prev_mask);

    int exit_status = wait_for_job(job);
    ASSERT_EQ_INT(42, exit_status);
    ASSERT_TRUE(job_is_completed(job));

    free_command(cmd);
    job_table_cleanup();
    signals_cleanup();
}

static void test_sigchld_event_processing_stop(void)
{
    job_table_init();
    signals_init();

    sigset_t prev_mask;
    signals_block_sigchld(&prev_mask);

    pid_t pid = fork();
    ASSERT_TRUE(pid >= 0);

    if (pid == 0) {
        signals_unblock_sigchld(&prev_mask);
        setpgid(0, 0);
        /* Stop self with SIGSTOP */
        raise(SIGSTOP);
        _exit(0);
    }

    setpgid(pid, pid);

    command_t *cmd = parse_command("test_stop", NULL);
    job_t *job = job_create(&pid, cmd, 1, "test_stop", true);
    ASSERT_NOT_NULL(job);

    signals_unblock_sigchld(&prev_mask);

    /* wait_for_job should return when job is stopped */
    wait_for_job(job);
    ASSERT_TRUE(job_is_stopped(job));

    /* Send SIGCONT to let child finish and resume job */
    job_update_process_status(pid, 0, PROC_RUNNING);
    kill(pid, SIGCONT);

    wait_for_job(job);
    ASSERT_TRUE(job_is_completed(job));

    free_command(cmd);
    job_table_cleanup();
    signals_cleanup();
}

static void test_pipeline_multi_process_wait(void)
{
    job_table_init();
    signals_init();

    sigset_t prev_mask;
    signals_block_sigchld(&prev_mask);

    pid_t pids[2];

    /* First process: exits with 0 immediately */
    pids[0] = fork();
    ASSERT_TRUE(pids[0] >= 0);
    if (pids[0] == 0) {
        signals_unblock_sigchld(&prev_mask);
        setpgid(0, 0);
        _exit(0);
    }
    setpgid(pids[0], pids[0]);

    /* Second process: sleeps 50ms and exits with 7 */
    pids[1] = fork();
    ASSERT_TRUE(pids[1] >= 0);
    if (pids[1] == 0) {
        signals_unblock_sigchld(&prev_mask);
        setpgid(0, pids[0]);
        struct timespec ts = { 0, 50000000L };
        nanosleep(&ts, NULL);
        _exit(7);
    }
    setpgid(pids[1], pids[0]);

    command_t *cmd = parse_command("proc1 | proc2", NULL);
    job_t *job = job_create(pids, cmd, 2, "proc1 | proc2", true);
    ASSERT_NOT_NULL(job);

    signals_unblock_sigchld(&prev_mask);

    int exit_status = wait_for_job(job);

    ASSERT_EQ_INT(7, exit_status);
    ASSERT_TRUE(job_is_completed(job));
    ASSERT_EQ_INT(PROC_COMPLETED, job->processes[0].state);
    ASSERT_EQ_INT(PROC_COMPLETED, job->processes[1].state);

    free_command(cmd);
    job_table_cleanup();
    signals_cleanup();
}

int main(void)
{
    TEST_INIT();

    printf("Running Signals and Job Control Unit Tests:\n");
    RUN_TEST(test_signals_init_and_pipe);
    RUN_TEST(test_signals_block_unblock);
    RUN_TEST(test_child_process_group_creation);
    RUN_TEST(test_sigchld_event_processing_exit);
    RUN_TEST(test_sigchld_event_processing_stop);
    RUN_TEST(test_pipeline_multi_process_wait);

    TEST_REPORT();
}
