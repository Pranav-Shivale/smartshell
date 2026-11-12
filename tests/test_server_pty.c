/*
 * test_server_pty.c — Unit tests for SmartShell PTY session management.
 */

#include "test_framework.h"
#include "server/pty_session.h"
#include "server/auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

static void test_pty_session_create_and_find(void)
{
    ASSERT_EQ_INT(0, pty_manager_init("bin/smartshell", 30));

    pty_session_t *s = pty_session_create(80, 24);
    ASSERT_TRUE(s != NULL);
    ASSERT_TRUE(s->child_pid > 0);
    ASSERT_TRUE(s->master_fd >= 0);
    ASSERT_EQ_INT(PTY_SESSION_ID_HEX_LEN, (int)strlen(s->session_id));
    ASSERT_EQ_INT(PTY_TICKET_HEX_LEN, (int)strlen(s->ws_ticket));
    ASSERT_EQ_INT(80, (int)s->cols);
    ASSERT_EQ_INT(24, (int)s->rows);
    ASSERT_EQ_INT(PTY_STATUS_CREATED, (int)s->status);

    /* Test lookup */
    pty_session_t *found = pty_session_find(s->session_id);
    ASSERT_TRUE(found == s);

    /* Nonexistent session lookup */
    ASSERT_TRUE(pty_session_find("nonexistent_session_id_12345") == NULL);
    ASSERT_TRUE(pty_session_find(NULL) == NULL);

    /* Delete session */
    char saved_id[PTY_SESSION_ID_HEX_LEN + 1];
    strcpy(saved_id, s->session_id);
    ASSERT_EQ_INT(0, pty_session_delete(saved_id));
    ASSERT_TRUE(pty_session_find(saved_id) == NULL);

    pty_manager_shutdown();
}

static void test_pty_ticket_authentication(void)
{
    ASSERT_EQ_INT(0, pty_manager_init("bin/smartshell", 30));

    pty_session_t *s = pty_session_create(80, 24);
    ASSERT_TRUE(s != NULL);

    char ticket_copy[PTY_TICKET_HEX_LEN + 1];
    strcpy(ticket_copy, s->ws_ticket);

    /* Authentication with invalid session or ticket */
    ASSERT_FALSE(pty_session_authenticate_ticket("bad_session_id", ticket_copy));
    ASSERT_FALSE(pty_session_authenticate_ticket(s->session_id, "bad_ticket_12345"));
    ASSERT_FALSE(pty_session_authenticate_ticket(NULL, ticket_copy));
    ASSERT_FALSE(pty_session_authenticate_ticket(s->session_id, NULL));

    /* Valid authentication */
    ASSERT_TRUE(pty_session_authenticate_ticket(s->session_id, ticket_copy));

    /* Single-use ticket check: replay should fail */
    ASSERT_FALSE(pty_session_authenticate_ticket(s->session_id, ticket_copy));

    /* Issue new reconnect ticket */
    char new_ticket[PTY_TICKET_HEX_LEN + 1];
    ASSERT_EQ_INT(0, pty_session_issue_reconnect_ticket(s->session_id, new_ticket, sizeof(new_ticket)));
    ASSERT_EQ_INT(PTY_TICKET_HEX_LEN, (int)strlen(new_ticket));

    /* Authenticate with the newly issued reconnect ticket */
    ASSERT_TRUE(pty_session_authenticate_ticket(s->session_id, new_ticket));
    ASSERT_FALSE(pty_session_authenticate_ticket(s->session_id, new_ticket));

    /* Clean up */
    ASSERT_EQ_INT(0, pty_session_delete(s->session_id));
    pty_manager_shutdown();
}

static void test_pty_session_resize(void)
{
    ASSERT_EQ_INT(0, pty_manager_init("bin/smartshell", 30));

    pty_session_t *s = pty_session_create(80, 24);
    ASSERT_TRUE(s != NULL);

    /* Valid resize */
    ASSERT_EQ_INT(0, pty_session_resize(s, 120, 40));
    ASSERT_EQ_INT(120, (int)s->cols);
    ASSERT_EQ_INT(40, (int)s->rows);

    /* Invalid resize dimensions */
    ASSERT_EQ_INT(-1, pty_session_resize(s, 0, 40));
    ASSERT_EQ_INT(-1, pty_session_resize(s, 120, 0));
    ASSERT_EQ_INT(-1, pty_session_resize(NULL, 80, 24));

    ASSERT_EQ_INT(0, pty_session_delete(s->session_id));
    pty_manager_shutdown();
}

static void test_pty_session_input_and_output(void)
{
    ASSERT_EQ_INT(0, pty_manager_init("bin/smartshell", 30));

    pty_session_t *s = pty_session_create(80, 24);
    ASSERT_TRUE(s != NULL);

    /* Write command to PTY */
    const char *cmd = "echo pty_test_token_xyz123\n";
    int written = pty_session_write_input(s, cmd, strlen(cmd));
    ASSERT_EQ_INT((int)strlen(cmd), written);

    /* Wait briefly for reader thread to process output */
    usleep(150000); /* 150ms */

    /* Verify output buffer contains the echoed output */
    ASSERT_TRUE(s->output_len > 0);
    ASSERT_TRUE(strstr(s->output_buf, "pty_test_token_xyz123") != NULL);

    ASSERT_EQ_INT(0, pty_session_delete(s->session_id));
    pty_manager_shutdown();
}

static void test_pty_session_status_and_list(void)
{
    ASSERT_EQ_INT(0, pty_manager_init("bin/smartshell", 30));

    pty_session_t *s1 = pty_session_create(80, 24);
    pty_session_t *s2 = pty_session_create(100, 30);
    ASSERT_TRUE(s1 != NULL);
    ASSERT_TRUE(s2 != NULL);

    pty_session_t list[PTY_MAX_SESSIONS];
    int count = pty_session_list(list, PTY_MAX_SESSIONS);
    ASSERT_EQ_INT(2, count);

    /* Check status strings */
    ASSERT_EQ_STR("created", pty_session_status_str(PTY_STATUS_CREATED));
    ASSERT_EQ_STR("active", pty_session_status_str(PTY_STATUS_ACTIVE));
    ASSERT_EQ_STR("disconnected", pty_session_status_str(PTY_STATUS_DISCONNECTED));
    ASSERT_EQ_STR("terminated", pty_session_status_str(PTY_STATUS_TERMINATED));

    ASSERT_EQ_INT(0, pty_session_delete(s1->session_id));
    ASSERT_EQ_INT(0, pty_session_delete(s2->session_id));
    pty_manager_shutdown();
}

static void test_pty_session_max_limit(void)
{
    ASSERT_EQ_INT(0, pty_manager_init("bin/smartshell", 30));

    pty_session_t *sessions[PTY_MAX_SESSIONS];
    for (int i = 0; i < PTY_MAX_SESSIONS; i++) {
        sessions[i] = pty_session_create(80, 24);
        ASSERT_TRUE(sessions[i] != NULL);
    }

    /* 17th session should be rejected */
    pty_session_t *overflow = pty_session_create(80, 24);
    ASSERT_TRUE(overflow == NULL);

    /* Delete one session */
    char del_id[PTY_SESSION_ID_HEX_LEN + 1];
    strcpy(del_id, sessions[0]->session_id);
    ASSERT_EQ_INT(0, pty_session_delete(del_id));

    /* Now creating one more should succeed */
    pty_session_t *replacement = pty_session_create(80, 24);
    ASSERT_TRUE(replacement != NULL);

    /* Clean up all sessions */
    pty_session_delete(replacement->session_id);
    for (int i = 1; i < PTY_MAX_SESSIONS; i++) {
        pty_session_delete(sessions[i]->session_id);
    }

    pty_manager_shutdown();
}

static void test_pty_ticket_expiry(void)
{
    ASSERT_EQ_INT(0, pty_manager_init("bin/smartshell", 30));

    pty_session_t *s = pty_session_create(80, 24);
    ASSERT_TRUE(s != NULL);

    char ticket_copy[PTY_TICKET_HEX_LEN + 1];
    strcpy(ticket_copy, s->ws_ticket);

    /* Artificially expire the ticket */
    s->ticket_expires_at = time(NULL) - 10;

    /* Authentication with expired ticket should fail */
    ASSERT_FALSE(pty_session_authenticate_ticket(s->session_id, ticket_copy));

    ASSERT_EQ_INT(0, pty_session_delete(s->session_id));
    pty_manager_shutdown();
}

static void test_pty_child_sighup_and_sigkill_escalation(void)
{
    ASSERT_EQ_INT(0, pty_manager_init("bin/smartshell", 30));

    pty_session_t *s = pty_session_create(80, 24);
    ASSERT_TRUE(s != NULL);
    pid_t child_pid = s->child_pid;
    ASSERT_TRUE(child_pid > 0);

    /* Command shell to ignore SIGHUP and sleep */
    const char *cmd = "trap '' HUP; sleep 60\n";
    pty_session_write_input(s, cmd, strlen(cmd));
    usleep(150000); /* 150ms to ensure trap and sleep execute */

    /* Terminate session: coordinator must escalate to SIGKILL and reap child */
    ASSERT_EQ_INT(0, pty_session_terminate(s));
    ASSERT_EQ_INT(PTY_STATUS_TERMINATED, (int)s->status);
    ASSERT_EQ_INT(-1, s->child_pid);

    /* Verify child process no longer exists (no zombie, fully reaped) */
    int res = kill(child_pid, 0);
    ASSERT_TRUE(res == -1 && errno == ESRCH);

    ASSERT_EQ_INT(0, pty_session_delete(s->session_id));
    pty_manager_shutdown();
}

int main(void)
{
    TEST_INIT();
    printf("Running PTY Session Management Unit Tests:\n");

    RUN_TEST(test_pty_session_create_and_find);
    RUN_TEST(test_pty_ticket_authentication);
    RUN_TEST(test_pty_ticket_expiry);
    RUN_TEST(test_pty_session_resize);
    RUN_TEST(test_pty_session_input_and_output);
    RUN_TEST(test_pty_child_sighup_and_sigkill_escalation);
    RUN_TEST(test_pty_session_status_and_list);
    RUN_TEST(test_pty_session_max_limit);

    TEST_REPORT();
}
