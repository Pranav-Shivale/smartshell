/*
 * test_server_auth.c — Unit tests for SmartShell web server authentication and security.
 */

#include "test_framework.h"
#include "server/auth.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void test_token_generation_and_entropy(void)
{
    char token1[AUTH_TOKEN_HEX_LEN + 1];
    char token2[AUTH_TOKEN_HEX_LEN + 1];

    ASSERT_EQ_INT(0, auth_generate_token(token1));
    ASSERT_EQ_INT(0, auth_generate_token(token2));

    /* Check length */
    ASSERT_EQ_INT(AUTH_TOKEN_HEX_LEN, (int)strlen(token1));
    ASSERT_EQ_INT(AUTH_TOKEN_HEX_LEN, (int)strlen(token2));

    /* Check all characters are valid lower-case hex digits */
    for (int i = 0; i < AUTH_TOKEN_HEX_LEN; i++) {
        ASSERT_TRUE(isxdigit((unsigned char)token1[i]));
        ASSERT_TRUE(isxdigit((unsigned char)token2[i]));
    }

    /* Check two independently generated tokens are not identical */
    ASSERT_TRUE(strcmp(token1, token2) != 0);

    /* Test null pointer handling */
    ASSERT_EQ_INT(-1, auth_generate_token(NULL));
}

static void test_token_save_load_and_permissions(void)
{
    char token[AUTH_TOKEN_HEX_LEN + 1];
    ASSERT_EQ_INT(0, auth_generate_token(token));

    const char *test_path = "/tmp/smartshell_test_token_dir/test.token";
    unlink(test_path);
    rmdir("/tmp/smartshell_test_token_dir");

    /* Save token */
    ASSERT_EQ_INT(0, auth_save_token(token, test_path));

    /* Verify file permissions are 0600 */
    struct stat st;
    ASSERT_EQ_INT(0, stat(test_path, &st));
    mode_t file_perm = st.st_mode & 0777;
    ASSERT_EQ_INT(0600, (int)file_perm);

    /* Load token and verify contents match */
    char loaded_token[AUTH_TOKEN_HEX_LEN + 1];
    ASSERT_EQ_INT(0, auth_load_token(test_path, loaded_token));
    ASSERT_EQ_STR(token, loaded_token);

    /* Cleanup */
    unlink(test_path);
    rmdir("/tmp/smartshell_test_token_dir");

    /* Test error paths */
    ASSERT_EQ_INT(-1, auth_save_token(NULL, test_path));
    ASSERT_EQ_INT(-1, auth_save_token(token, NULL));
    ASSERT_EQ_INT(-1, auth_load_token(NULL, loaded_token));
    ASSERT_EQ_INT(-1, auth_load_token("/nonexistent/file/path", loaded_token));
}

static void test_constant_time_token_verification(void)
{
    char token1[AUTH_TOKEN_HEX_LEN + 1];
    char token2[AUTH_TOKEN_HEX_LEN + 1];
    auth_generate_token(token1);
    auth_generate_token(token2);

    /* Identical token matches */
    ASSERT_TRUE(auth_verify_token(token1, token1));

    /* Different tokens do not match */
    ASSERT_FALSE(auth_verify_token(token1, token2));

    /* Single byte difference fails */
    char mutated[AUTH_TOKEN_HEX_LEN + 1];
    strcpy(mutated, token1);
    mutated[0] = (mutated[0] == 'a') ? 'b' : 'a';
    ASSERT_FALSE(auth_verify_token(token1, mutated));

    /* Substring / truncated tokens fail */
    char truncated[AUTH_TOKEN_HEX_LEN + 1];
    strncpy(truncated, token1, 32);
    truncated[32] = '\0';
    ASSERT_FALSE(auth_verify_token(token1, truncated));
    ASSERT_FALSE(auth_verify_token(truncated, token1));

    /* NULL handling */
    ASSERT_FALSE(auth_verify_token(NULL, token1));
    ASSERT_FALSE(auth_verify_token(token1, NULL));
    ASSERT_FALSE(auth_verify_token(NULL, NULL));
}

static void test_host_header_validation(void)
{
    int port = 8080;

    /* Valid hosts */
    ASSERT_TRUE(auth_validate_host("127.0.0.1:8080", port));
    ASSERT_TRUE(auth_validate_host("localhost:8080", port));

    /* Invalid ports */
    ASSERT_FALSE(auth_validate_host("127.0.0.1:9090", port));
    ASSERT_FALSE(auth_validate_host("localhost:3000", port));

    /* External / malicious hostnames */
    ASSERT_FALSE(auth_validate_host("evil.com:8080", port));
    ASSERT_FALSE(auth_validate_host("attacker.localhost:8080", port));
    ASSERT_FALSE(auth_validate_host("192.168.1.1:8080", port));
    ASSERT_FALSE(auth_validate_host("example.org", port));

    /* Missing port when port != 80 */
    ASSERT_FALSE(auth_validate_host("127.0.0.1", port));
    ASSERT_FALSE(auth_validate_host("localhost", port));

    /* NULL / empty */
    ASSERT_FALSE(auth_validate_host(NULL, port));
    ASSERT_FALSE(auth_validate_host("", port));

    /* Port 80 allows without port */
    ASSERT_TRUE(auth_validate_host("127.0.0.1", 80));
    ASSERT_TRUE(auth_validate_host("localhost", 80));
}

static void test_origin_header_validation(void)
{
    int port = 8080;

    /* Absent origin (e.g., curl, non-browser clients) is permitted */
    ASSERT_TRUE(auth_validate_origin(NULL, port));
    ASSERT_TRUE(auth_validate_origin("", port));

    /* Valid browser origins */
    ASSERT_TRUE(auth_validate_origin("http://127.0.0.1:8080", port));
    ASSERT_TRUE(auth_validate_origin("http://localhost:8080", port));

    /* Invalid / malicious origins */
    ASSERT_FALSE(auth_validate_origin("http://evil.com", port));
    ASSERT_FALSE(auth_validate_origin("http://evil.com:8080", port));
    ASSERT_FALSE(auth_validate_origin("https://attacker.org", port));
    ASSERT_FALSE(auth_validate_origin("http://localhost:9090", port));
    ASSERT_FALSE(auth_validate_origin("http://127.0.0.1:9090", port));
    ASSERT_FALSE(auth_validate_origin("null", port));
}

static void test_bearer_token_extraction(void)
{
    char token_out[AUTH_TOKEN_HEX_LEN + 1];

    /* Valid Bearer format */
    const char *valid = "Bearer 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    ASSERT_TRUE(auth_extract_bearer(valid, token_out));
    ASSERT_EQ_STR("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", token_out);

    /* Extra spaces */
    const char *extra_spaces = "Bearer   0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef  ";
    ASSERT_TRUE(auth_extract_bearer(extra_spaces, token_out));
    ASSERT_EQ_STR("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", token_out);

    /* Invalid prefix / missing Bearer */
    ASSERT_FALSE(auth_extract_bearer("Basic abcdef", token_out));
    ASSERT_FALSE(auth_extract_bearer("Token 12345", token_out));
    ASSERT_FALSE(auth_extract_bearer("bearer 12345", token_out)); /* Case-sensitive per RFC */
    ASSERT_FALSE(auth_extract_bearer("Bearer", token_out));
    ASSERT_FALSE(auth_extract_bearer("Bearer ", token_out));
    ASSERT_FALSE(auth_extract_bearer("", token_out));
    ASSERT_FALSE(auth_extract_bearer(NULL, token_out));
    ASSERT_FALSE(auth_extract_bearer(valid, NULL));
}

static void test_root_detection_and_default_path(void)
{
    /* In normal non-root test environment, should return false */
    bool is_root = auth_is_running_as_root();
    if (geteuid() == 0) {
        ASSERT_TRUE(is_root);
    } else {
        ASSERT_FALSE(is_root);
    }

    char path[1024];
    ASSERT_EQ_INT(0, auth_get_default_token_path(path, sizeof(path)));
    ASSERT_TRUE(strstr(path, "/.smartshell/session.token") != NULL);

    /* Buffer too small */
    char small_buf[5];
    ASSERT_EQ_INT(-1, auth_get_default_token_path(small_buf, sizeof(small_buf)));
}

int main(void)
{
    TEST_INIT();
    printf("Running Server Authentication and Security Unit Tests:\n");

    RUN_TEST(test_token_generation_and_entropy);
    RUN_TEST(test_token_save_load_and_permissions);
    RUN_TEST(test_constant_time_token_verification);
    RUN_TEST(test_host_header_validation);
    RUN_TEST(test_origin_header_validation);
    RUN_TEST(test_bearer_token_extraction);
    RUN_TEST(test_root_detection_and_default_path);

    TEST_REPORT();
}

