/*
 * test_redirect.c — Unit tests for SmartShell redirection and pipeline parsing/setup.
 */

#include "parser.h"
#include "redirect.h"
#include "test_framework.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_parse_redirections(void)
{
    const char *err = NULL;

    /* Input redirection */
    command_t *cmd = parse_command("cat < input.txt", &err);
    ASSERT_NOT_NULL(cmd);
    ASSERT_NULL(err);
    ASSERT_EQ_INT(1, cmd->argc);
    ASSERT_EQ_STR("cat", cmd->argv[0]);
    ASSERT_EQ_STR("input.txt", cmd->input_file);
    ASSERT_NULL(cmd->output_file);
    ASSERT_NULL(cmd->error_file);
    ASSERT_FALSE(cmd->append_output);
    free_command(cmd);

    /* Output redirection (truncate) */
    cmd = parse_command("echo hello > out.txt", &err);
    ASSERT_NOT_NULL(cmd);
    ASSERT_NULL(err);
    ASSERT_EQ_INT(2, cmd->argc);
    ASSERT_EQ_STR("echo", cmd->argv[0]);
    ASSERT_EQ_STR("hello", cmd->argv[1]);
    ASSERT_EQ_STR("out.txt", cmd->output_file);
    ASSERT_FALSE(cmd->append_output);
    ASSERT_NULL(cmd->input_file);
    ASSERT_NULL(cmd->error_file);
    free_command(cmd);

    /* Output redirection (append) */
    cmd = parse_command("echo world >> append.txt", &err);
    ASSERT_NOT_NULL(cmd);
    ASSERT_NULL(err);
    ASSERT_EQ_INT(2, cmd->argc);
    ASSERT_EQ_STR("append.txt", cmd->output_file);
    ASSERT_TRUE(cmd->append_output);
    free_command(cmd);

    /* Error redirection */
    cmd = parse_command("mycmd 2> err.log", &err);
    ASSERT_NOT_NULL(cmd);
    ASSERT_NULL(err);
    ASSERT_EQ_INT(1, cmd->argc);
    ASSERT_EQ_STR("mycmd", cmd->argv[0]);
    ASSERT_EQ_STR("err.log", cmd->error_file);
    free_command(cmd);

    /* All redirections combined */
    cmd = parse_command("tool < in.dat > out.dat 2> err.dat", &err);
    ASSERT_NOT_NULL(cmd);
    ASSERT_NULL(err);
    ASSERT_EQ_INT(1, cmd->argc);
    ASSERT_EQ_STR("in.dat", cmd->input_file);
    ASSERT_EQ_STR("out.dat", cmd->output_file);
    ASSERT_FALSE(cmd->append_output);
    ASSERT_EQ_STR("err.dat", cmd->error_file);
    free_command(cmd);

    /* Unspaced redirections */
    cmd = parse_command("cat<in.txt>out.txt 2>err.txt", &err);
    ASSERT_NOT_NULL(cmd);
    ASSERT_NULL(err);
    ASSERT_EQ_INT(1, cmd->argc);
    ASSERT_EQ_STR("cat", cmd->argv[0]);
    ASSERT_EQ_STR("in.txt", cmd->input_file);
    ASSERT_EQ_STR("out.txt", cmd->output_file);
    ASSERT_EQ_STR("err.txt", cmd->error_file);
    free_command(cmd);
}

static void test_parse_pipelines(void)
{
    const char *err = NULL;

    /* 2-stage pipeline */
    command_t *cmd = parse_command("ls -la | grep txt", &err);
    ASSERT_NOT_NULL(cmd);
    ASSERT_NULL(err);
    ASSERT_EQ_INT(2, cmd->argc);
    ASSERT_EQ_STR("ls", cmd->argv[0]);
    ASSERT_EQ_STR("-la", cmd->argv[1]);
    ASSERT_NOT_NULL(cmd->pipe_next);

    command_t *stage2 = cmd->pipe_next;
    ASSERT_EQ_INT(2, stage2->argc);
    ASSERT_EQ_STR("grep", stage2->argv[0]);
    ASSERT_EQ_STR("txt", stage2->argv[1]);
    ASSERT_NULL(stage2->pipe_next);
    free_command(cmd);

    /* 3-stage pipeline */
    cmd = parse_command("cat file | grep text | wc -l", &err);
    ASSERT_NOT_NULL(cmd);
    ASSERT_NULL(err);
    ASSERT_EQ_INT(2, cmd->argc);
    ASSERT_EQ_STR("cat", cmd->argv[0]);
    ASSERT_NOT_NULL(cmd->pipe_next);

    stage2 = cmd->pipe_next;
    ASSERT_EQ_INT(2, stage2->argc);
    ASSERT_EQ_STR("grep", stage2->argv[0]);
    ASSERT_NOT_NULL(stage2->pipe_next);

    command_t *stage3 = stage2->pipe_next;
    ASSERT_EQ_INT(2, stage3->argc);
    ASSERT_EQ_STR("wc", stage3->argv[0]);
    ASSERT_EQ_STR("-l", stage3->argv[1]);
    ASSERT_NULL(stage3->pipe_next);
    free_command(cmd);

    /* Pipeline with redirections */
    cmd = parse_command("cat < in.txt | grep text > out.txt", &err);
    ASSERT_NOT_NULL(cmd);
    ASSERT_NULL(err);
    ASSERT_EQ_STR("in.txt", cmd->input_file);
    ASSERT_NULL(cmd->output_file);
    ASSERT_NOT_NULL(cmd->pipe_next);

    stage2 = cmd->pipe_next;
    ASSERT_NULL(stage2->input_file);
    ASSERT_EQ_STR("out.txt", stage2->output_file);
    ASSERT_FALSE(stage2->append_output);
    free_command(cmd);
}

static void test_pipeline_and_redir_syntax_errors(void)
{
    const char *err = NULL;

    /* Leading pipe */
    command_t *cmd = parse_command("| ls", &err);
    ASSERT_NULL(cmd);
    ASSERT_NOT_NULL(err);

    /* Trailing pipe */
    cmd = parse_command("ls |", &err);
    ASSERT_NULL(cmd);
    ASSERT_NOT_NULL(err);

    /* Double pipe */
    cmd = parse_command("ls | | grep foo", &err);
    ASSERT_NULL(cmd);
    ASSERT_NOT_NULL(err);

    /* Missing redirect target */
    cmd = parse_command("echo hello >", &err);
    ASSERT_NULL(cmd);
    ASSERT_NOT_NULL(err);

    /* Redirect immediately followed by pipe */
    cmd = parse_command("echo hello > | cat", &err);
    ASSERT_NULL(cmd);
    ASSERT_NOT_NULL(err);
}

static void test_setup_redirections_execution(void)
{
    const char *tmp_in = "/tmp/smartshell_test_in.txt";
    const char *tmp_out = "/tmp/smartshell_test_out.txt";

    /* Write test data to tmp_in */
    FILE *f = fopen(tmp_in, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "redirection test data\n");
    fclose(f);

    /* Save original stdout */
    int saved_stdout = dup(STDOUT_FILENO);
    ASSERT_TRUE(saved_stdout >= 0);

    /* Test output redirection setup */
    command_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.output_file = (char *)tmp_out;
    cmd.append_output = false;

    int ret = setup_redirections(&cmd);
    ASSERT_EQ_INT(0, ret);

    /* Write to stdout (now redirected to tmp_out) */
    printf("written via redirected stdout\n");
    fflush(stdout);

    /* Restore stdout */
    ASSERT_TRUE(dup2(saved_stdout, STDOUT_FILENO) >= 0);
    close(saved_stdout);

    /* Verify tmp_out content */
    f = fopen(tmp_out, "r");
    ASSERT_NOT_NULL(f);
    char buf[128];
    ASSERT_NOT_NULL(fgets(buf, sizeof(buf), f));
    ASSERT_EQ_STR("written via redirected stdout\n", buf);
    fclose(f);

    /* Clean up */
    unlink(tmp_in);
    unlink(tmp_out);
}

int main(void)
{
    TEST_INIT();

    printf("Running Redirection & Pipeline Unit Tests:\n");
    RUN_TEST(test_parse_redirections);
    RUN_TEST(test_parse_pipelines);
    RUN_TEST(test_pipeline_and_redir_syntax_errors);
    RUN_TEST(test_setup_redirections_execution);

    TEST_REPORT();
}
