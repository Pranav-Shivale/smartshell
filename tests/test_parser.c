/*
 * test_parser.c — Unit tests for SmartShell command-line parser.
 */

#include "parser.h"
#include "test_framework.h"

#include <stdlib.h>

static void test_empty_input(void)
{
    const char *err = NULL;
    command_t *cmd = parse_command("", &err);
    ASSERT_NULL(cmd);
    ASSERT_NULL(err);

    cmd = parse_command("     \t  \r  \n   ", &err);
    ASSERT_NULL(cmd);
    ASSERT_NULL(err);
}

static void test_simple_command(void)
{
    const char *err = NULL;
    command_t *cmd = parse_command("ls -la /tmp", &err);
    ASSERT_NOT_NULL(cmd);
    ASSERT_NULL(err);
    ASSERT_EQ_INT(3, cmd->argc);
    ASSERT_EQ_STR("ls", cmd->argv[0]);
    ASSERT_EQ_STR("-la", cmd->argv[1]);
    ASSERT_EQ_STR("/tmp", cmd->argv[2]);
    ASSERT_NULL(cmd->argv[3]);
    free_command(cmd);
}

static void test_multiple_spaces(void)
{
    const char *err = NULL;
    command_t *cmd = parse_command("   echo    foo    bar   ", &err);
    ASSERT_NOT_NULL(cmd);
    ASSERT_NULL(err);
    ASSERT_EQ_INT(3, cmd->argc);
    ASSERT_EQ_STR("echo", cmd->argv[0]);
    ASSERT_EQ_STR("foo", cmd->argv[1]);
    ASSERT_EQ_STR("bar", cmd->argv[2]);
    ASSERT_NULL(cmd->argv[3]);
    free_command(cmd);
}

static void test_single_quotes(void)
{
    const char *err = NULL;
    command_t *cmd = parse_command("echo 'hello world'", &err);
    ASSERT_NOT_NULL(cmd);
    ASSERT_NULL(err);
    ASSERT_EQ_INT(2, cmd->argc);
    ASSERT_EQ_STR("echo", cmd->argv[0]);
    ASSERT_EQ_STR("hello world", cmd->argv[1]);
    ASSERT_NULL(cmd->argv[2]);
    free_command(cmd);

    /* Single quotes preserve literal backslashes */
    cmd = parse_command("echo 'foo\\nbar'", &err);
    ASSERT_NOT_NULL(cmd);
    ASSERT_NULL(err);
    ASSERT_EQ_INT(2, cmd->argc);
    ASSERT_EQ_STR("echo", cmd->argv[0]);
    ASSERT_EQ_STR("foo\\nbar", cmd->argv[1]);
    free_command(cmd);
}

static void test_double_quotes(void)
{
    const char *err = NULL;
    command_t *cmd = parse_command("echo \"hello world\"", &err);
    ASSERT_NOT_NULL(cmd);
    ASSERT_NULL(err);
    ASSERT_EQ_INT(2, cmd->argc);
    ASSERT_EQ_STR("echo", cmd->argv[0]);
    ASSERT_EQ_STR("hello world", cmd->argv[1]);
    ASSERT_NULL(cmd->argv[2]);
    free_command(cmd);

    /* Double quotes handle escaped quotes and backslashes */
    cmd = parse_command("echo \"hello \\\"world\\\" \\\\ test\"", &err);
    ASSERT_NOT_NULL(cmd);
    ASSERT_NULL(err);
    ASSERT_EQ_INT(2, cmd->argc);
    ASSERT_EQ_STR("echo", cmd->argv[0]);
    ASSERT_EQ_STR("hello \"world\" \\ test", cmd->argv[1]);
    free_command(cmd);
}

static void test_backslash_outside_quotes(void)
{
    const char *err = NULL;
    command_t *cmd = parse_command("echo hello\\ world\\! test\\ \\ test", &err);
    ASSERT_NOT_NULL(cmd);
    ASSERT_NULL(err);
    ASSERT_EQ_INT(3, cmd->argc);
    ASSERT_EQ_STR("echo", cmd->argv[0]);
    ASSERT_EQ_STR("hello world!", cmd->argv[1]);
    ASSERT_EQ_STR("test  test", cmd->argv[2]);
    ASSERT_NULL(cmd->argv[3]);
    free_command(cmd);
}

static void test_mixed_quotes(void)
{
    const char *err = NULL;
    command_t *cmd = parse_command("echo foo'bar'\"baz\"", &err);
    ASSERT_NOT_NULL(cmd);
    ASSERT_NULL(err);
    ASSERT_EQ_INT(2, cmd->argc);
    ASSERT_EQ_STR("echo", cmd->argv[0]);
    ASSERT_EQ_STR("foobarbaz", cmd->argv[1]);
    ASSERT_NULL(cmd->argv[2]);
    free_command(cmd);
}

static void test_empty_quotes(void)
{
    const char *err = NULL;
    command_t *cmd = parse_command("echo \"\" ''", &err);
    ASSERT_NOT_NULL(cmd);
    ASSERT_NULL(err);
    ASSERT_EQ_INT(3, cmd->argc);
    ASSERT_EQ_STR("echo", cmd->argv[0]);
    ASSERT_EQ_STR("", cmd->argv[1]);
    ASSERT_EQ_STR("", cmd->argv[2]);
    ASSERT_NULL(cmd->argv[3]);
    free_command(cmd);
}

static void test_unclosed_single_quote(void)
{
    const char *err = NULL;
    command_t *cmd = parse_command("echo 'unclosed", &err);
    ASSERT_NULL(cmd);
    ASSERT_NOT_NULL(err);
    ASSERT_EQ_STR("unclosed single quote", err);
}

static void test_unclosed_double_quote(void)
{
    const char *err = NULL;
    command_t *cmd = parse_command("echo \"unclosed", &err);
    ASSERT_NULL(cmd);
    ASSERT_NOT_NULL(err);
    ASSERT_EQ_STR("unclosed double quote", err);
}

static void test_trailing_backslash(void)
{
    const char *err = NULL;
    command_t *cmd = parse_command("echo trailing\\", &err);
    ASSERT_NULL(cmd);
    ASSERT_NOT_NULL(err);
    ASSERT_EQ_STR("trailing backslash", err);
}

int main(void)
{
    TEST_INIT();

    printf("Running Parser Unit Tests:\n");
    RUN_TEST(test_empty_input);
    RUN_TEST(test_simple_command);
    RUN_TEST(test_multiple_spaces);
    RUN_TEST(test_single_quotes);
    RUN_TEST(test_double_quotes);
    RUN_TEST(test_backslash_outside_quotes);
    RUN_TEST(test_mixed_quotes);
    RUN_TEST(test_empty_quotes);
    RUN_TEST(test_unclosed_single_quote);
    RUN_TEST(test_unclosed_double_quote);
    RUN_TEST(test_trailing_backslash);

    TEST_REPORT();
}
