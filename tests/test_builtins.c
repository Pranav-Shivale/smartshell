/*
 * test_builtins.c — Unit tests for SmartShell built-in commands.
 */

#include "builtins.h"
#include "parser.h"
#include "test_framework.h"

#include <stdlib.h>
#include <unistd.h>

static void test_is_builtin(void)
{
    ASSERT_TRUE(is_builtin("cd"));
    ASSERT_TRUE(is_builtin("pwd"));
    ASSERT_TRUE(is_builtin("exit"));
    ASSERT_TRUE(is_builtin("quit"));
    ASSERT_TRUE(is_builtin("help"));
    ASSERT_TRUE(is_builtin("schedule"));

    ASSERT_FALSE(is_builtin("ls"));
    ASSERT_FALSE(is_builtin("echo"));
    ASSERT_FALSE(is_builtin("cat"));
    ASSERT_FALSE(is_builtin(""));
    ASSERT_FALSE(is_builtin(NULL));
}

static void test_builtin_help(void)
{
    command_t *cmd = parse_command("help", NULL);
    ASSERT_NOT_NULL(cmd);

    bool should_exit = false;
    int exit_code = 0;
    int ret = execute_builtin(cmd, &should_exit, &exit_code);

    ASSERT_EQ_INT(0, ret);
    ASSERT_FALSE(should_exit);
    ASSERT_EQ_INT(0, exit_code);

    free_command(cmd);
}

static void test_builtin_pwd_and_cd(void)
{
    char original_cwd[4096];
    ASSERT_NOT_NULL(getcwd(original_cwd, sizeof(original_cwd)));

    /* cd /tmp */
    command_t *cmd = parse_command("cd /tmp", NULL);
    ASSERT_NOT_NULL(cmd);

    bool should_exit = false;
    int exit_code = 0;
    int ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(0, ret);
    ASSERT_FALSE(should_exit);
    free_command(cmd);

    char current_cwd[4096];
    ASSERT_NOT_NULL(getcwd(current_cwd, sizeof(current_cwd)));
    ASSERT_EQ_STR("/tmp", current_cwd);

    /* cd to nonexistent dir */
    cmd = parse_command("cd /nonexistent_directory_smartshell_test", NULL);
    ASSERT_NOT_NULL(cmd);
    ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(1, ret);
    ASSERT_FALSE(should_exit);
    free_command(cmd);

    /* Restore original cwd */
    ASSERT_EQ_INT(0, chdir(original_cwd));
}

static void test_builtin_exit(void)
{
    /* Plain exit */
    command_t *cmd = parse_command("exit", NULL);
    ASSERT_NOT_NULL(cmd);
    bool should_exit = false;
    int exit_code = -1;
    int ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(0, ret);
    ASSERT_TRUE(should_exit);
    ASSERT_EQ_INT(0, exit_code);
    free_command(cmd);

    /* Exit with status code */
    cmd = parse_command("exit 42", NULL);
    ASSERT_NOT_NULL(cmd);
    should_exit = false;
    exit_code = -1;
    ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(0, ret);
    ASSERT_TRUE(should_exit);
    ASSERT_EQ_INT(42, exit_code);
    free_command(cmd);

    /* Quit alias */
    cmd = parse_command("quit 7", NULL);
    ASSERT_NOT_NULL(cmd);
    should_exit = false;
    exit_code = -1;
    ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(0, ret);
    ASSERT_TRUE(should_exit);
    ASSERT_EQ_INT(7, exit_code);
    free_command(cmd);

    /* Exit with non-numeric argument */
    cmd = parse_command("exit abc", NULL);
    ASSERT_NOT_NULL(cmd);
    should_exit = false;
    exit_code = -1;
    ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(2, ret);
    ASSERT_TRUE(should_exit);
    ASSERT_EQ_INT(2, exit_code);
    free_command(cmd);

    /* Exit with too many arguments */
    cmd = parse_command("exit 1 2", NULL);
    ASSERT_NOT_NULL(cmd);
    should_exit = true;
    exit_code = -1;
    ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(1, ret);
    ASSERT_FALSE(should_exit);
    free_command(cmd);
}

static char *capture_builtin_output(const command_t *cmd, int *ret_code)
{
    int pipefd[2];
    if (pipe(pipefd) != 0) return NULL;

    int saved_stdout = dup(STDOUT_FILENO);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);

    bool should_exit = false;
    int exit_code = 0;
    int rc = execute_builtin(cmd, &should_exit, &exit_code);
    if (ret_code) *ret_code = rc;

    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    char *buf = calloc(8192, 1);
    if (!buf) {
        close(pipefd[0]);
        return NULL;
    }
    ssize_t n = read(pipefd[0], buf, 8191);
    close(pipefd[0]);
    if (n >= 0) {
        buf[n] = '\0';
    }
    return buf;
}

static void test_builtin_schedule_help_and_no_args(void)
{
    command_t *cmd = parse_command("schedule", NULL);
    ASSERT_NOT_NULL(cmd);
    bool should_exit = false;
    int exit_code = -1;
    int ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(0, ret);
    ASSERT_FALSE(should_exit);
    free_command(cmd);

    cmd = parse_command("schedule help", NULL);
    ASSERT_NOT_NULL(cmd);
    ret = execute_builtin(cmd, &should_exit, &exit_code);
    ASSERT_EQ_INT(0, ret);
    ASSERT_FALSE(should_exit);
    free_command(cmd);
}

static void test_builtin_schedule_algorithms(void)
{
    const char *algs[] = {
        "schedule fcfs",
        "schedule sjf",
        "schedule rr",
        "schedule rr 2",
        "schedule priority",
        "schedule mlfq"
    };

    for (size_t i = 0; i < sizeof(algs) / sizeof(algs[0]); i++) {
        command_t *cmd = parse_command(algs[i], NULL);
        ASSERT_NOT_NULL(cmd);
        bool should_exit = false;
        int exit_code = -1;
        int ret = execute_builtin(cmd, &should_exit, &exit_code);
        ASSERT_EQ_INT(0, ret);
        ASSERT_FALSE(should_exit);
        free_command(cmd);
    }
}

static void test_builtin_schedule_errors(void)
{
    const char *err_cmds[] = {
        "schedule unknown_alg",
        "schedule rr 0",
        "schedule rr -1",
        "schedule rr abc",
        "schedule rr 4 extra",
        "schedule fcfs extra",
        "schedule sjf extra",
        "schedule priority extra",
        "schedule mlfq extra",
        "schedule help extra"
    };

    for (size_t i = 0; i < sizeof(err_cmds) / sizeof(err_cmds[0]); i++) {
        command_t *cmd = parse_command(err_cmds[i], NULL);
        ASSERT_NOT_NULL(cmd);
        bool should_exit = false;
        int exit_code = -1;
        int ret = execute_builtin(cmd, &should_exit, &exit_code);
        ASSERT_EQ_INT(1, ret);
        ASSERT_FALSE(should_exit);
        free_command(cmd);
    }
}

static void test_builtin_schedule_output_capture(void)
{
    command_t *cmd = parse_command("schedule rr 4", NULL);
    ASSERT_NOT_NULL(cmd);
    int rc = -1;
    char *out = capture_builtin_output(cmd, &rc);
    ASSERT_NOT_NULL(out);
    ASSERT_EQ_INT(0, rc);

    ASSERT_TRUE(strstr(out, "SmartShell CPU Scheduling Simulation") != NULL);
    ASSERT_TRUE(strstr(out, "Algorithm:  Round Robin (RR)") != NULL);
    ASSERT_TRUE(strstr(out, "RR Quantum: 4") != NULL);
    ASSERT_TRUE(strstr(out, "Workload:") != NULL);
    ASSERT_TRUE(strstr(out, "Gantt Chart:") != NULL);
    ASSERT_TRUE(strstr(out, "Process Metrics:") != NULL);
    ASSERT_TRUE(strstr(out, "Summary:") != NULL);
    ASSERT_TRUE(strstr(out, "CPU Utilization:") != NULL);
    ASSERT_TRUE(strstr(out, "Average Waiting Time:") != NULL);

    free(out);
    free_command(cmd);

    cmd = parse_command("schedule mlfq", NULL);
    ASSERT_NOT_NULL(cmd);
    out = capture_builtin_output(cmd, &rc);
    ASSERT_NOT_NULL(out);
    ASSERT_EQ_INT(0, rc);

    ASSERT_TRUE(strstr(out, "Algorithm:  Multilevel Feedback Queue (MLFQ)") != NULL);
    ASSERT_TRUE(strstr(out, "Gantt Chart:") != NULL);
    ASSERT_TRUE(strstr(out, "Summary:") != NULL);

    free(out);
    free_command(cmd);
}

static void test_builtin_schedule_determinism(void)
{
    command_t *cmd1 = parse_command("schedule fcfs", NULL);
    command_t *cmd2 = parse_command("schedule fcfs", NULL);
    ASSERT_NOT_NULL(cmd1);
    ASSERT_NOT_NULL(cmd2);

    int rc1 = -1, rc2 = -1;
    char *out1 = capture_builtin_output(cmd1, &rc1);
    char *out2 = capture_builtin_output(cmd2, &rc2);

    ASSERT_NOT_NULL(out1);
    ASSERT_NOT_NULL(out2);
    ASSERT_EQ_INT(0, rc1);
    ASSERT_EQ_INT(0, rc2);
    ASSERT_EQ_STR(out1, out2);

    free(out1);
    free(out2);
    free_command(cmd1);
    free_command(cmd2);
}

int main(void)
{
    TEST_INIT();

    printf("Running Builtins Unit Tests:\n");
    RUN_TEST(test_is_builtin);
    RUN_TEST(test_builtin_help);
    RUN_TEST(test_builtin_pwd_and_cd);
    RUN_TEST(test_builtin_exit);
    RUN_TEST(test_builtin_schedule_help_and_no_args);
    RUN_TEST(test_builtin_schedule_algorithms);
    RUN_TEST(test_builtin_schedule_errors);
    RUN_TEST(test_builtin_schedule_output_capture);
    RUN_TEST(test_builtin_schedule_determinism);

    TEST_REPORT();
}
