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

int main(void)
{
    TEST_INIT();

    printf("Running Builtins Unit Tests:\n");
    RUN_TEST(test_is_builtin);
    RUN_TEST(test_builtin_help);
    RUN_TEST(test_builtin_pwd_and_cd);
    RUN_TEST(test_builtin_exit);

    TEST_REPORT();
}
