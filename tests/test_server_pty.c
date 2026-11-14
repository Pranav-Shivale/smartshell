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
#include <dirent.h>

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

static void test_pty_shutdown_multiple_sessions_and_descendants(void)
{
    ASSERT_EQ_INT(0, pty_manager_init("bin/smartshell", 30));

    pty_session_t *s1 = pty_session_create(80, 24);
    pty_session_t *s2 = pty_session_create(80, 24);
    pty_session_t *s3 = pty_session_create(80, 24);
    ASSERT_TRUE(s1 != NULL);
    ASSERT_TRUE(s2 != NULL);
    ASSERT_TRUE(s3 != NULL);

    pid_t pid1 = s1->child_pid;
    pid_t pid2 = s2->child_pid;
    pid_t pid3 = s3->child_pid;

    /* Session 2: ignores SIGHUP */
    const char *cmd2 = "trap '' HUP; sleep 60\n";
    pty_session_write_input(s2, cmd2, strlen(cmd2));

    /* Session 3: spawns background processes */
    const char *cmd3 = "sleep 60 & sleep 60 &\n";
    pty_session_write_input(s3, cmd3, strlen(cmd3));

    usleep(200000); /* 200ms to allow processes to spawn */

    /* Shutdown manager with multiple active sessions and diverse children */
    pty_manager_shutdown();

    /* Verify all 3 child shells are dead and reaped */
    ASSERT_TRUE(kill(pid1, 0) == -1 && errno == ESRCH);
    ASSERT_TRUE(kill(pid2, 0) == -1 && errno == ESRCH);
    ASSERT_TRUE(kill(pid3, 0) == -1 && errno == ESRCH);

    /* Verify no descendant process remains with session ID matching any session */
    DIR *dir = opendir("/proc");
    if (dir != NULL) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_name[0] < '0' || ent->d_name[0] > '9') continue;
            pid_t p = (pid_t)atoi(ent->d_name);
            if (p > 1 && p != getpid()) {
                pid_t sid = getsid(p);
                ASSERT_TRUE(sid != pid1 && sid != pid2 && sid != pid3);
            }
        }
        closedir(dir);
    }
}

static void test_pty_admission_capacity_and_limits(void)
{
    int live = -1, pending = -1;
    pty_admission_get_counts(&live, &pending);
    ASSERT_EQ_INT(0, live);
    ASSERT_EQ_INT(0, pending);

    ws_conn_ctx_t ctxs[MAX_LIVE_WS + 2];
    memset(ctxs, 0, sizeof(ctxs));

    /* Reserve up to MAX_PENDING_WS (8) */
    for (int i = 0; i < MAX_PENDING_WS; i++) {
        ASSERT_EQ_INT(0, pty_admission_reserve((struct mg_connection *)(uintptr_t)(i + 1), &ctxs[i]));
        ASSERT_TRUE(ctxs[i].lease_id != 0);
    }
    pty_admission_get_counts(&live, &pending);
    ASSERT_EQ_INT(MAX_PENDING_WS, live);
    ASSERT_EQ_INT(MAX_PENDING_WS, pending);

    /* 9th unconfirmed connection must be rejected due to pending auth capacity limit */
    ws_conn_ctx_t overflow_ctx;
    memset(&overflow_ctx, 0, sizeof(overflow_ctx));
    ASSERT_EQ_INT(-1, pty_admission_reserve((struct mg_connection *)(uintptr_t)999, &overflow_ctx));
    ASSERT_EQ_INT(0, (int)overflow_ctx.lease_id);

    /* Confirm and authenticate all 8 pending connections */
    for (int i = 0; i < MAX_PENDING_WS; i++) {
        ASSERT_EQ_INT(0, pty_admission_confirm(ctxs[i].lease_id));
        ASSERT_EQ_INT(0, pty_admission_authenticate(ctxs[i].lease_id, "sess_123"));
    }
    pty_admission_get_counts(&live, &pending);
    ASSERT_EQ_INT(MAX_PENDING_WS, live);
    ASSERT_EQ_INT(0, pending);

    /* Reserve next 8 connections */
    for (int i = 8; i < 16; i++) {
        ASSERT_EQ_INT(0, pty_admission_reserve((struct mg_connection *)(uintptr_t)(i + 1), &ctxs[i]));
        ASSERT_TRUE(ctxs[i].lease_id != 0);
    }
    /* Confirm and authenticate these 8 connections as well */
    for (int i = 8; i < 16; i++) {
        ASSERT_EQ_INT(0, pty_admission_confirm(ctxs[i].lease_id));
        ASSERT_EQ_INT(0, pty_admission_authenticate(ctxs[i].lease_id, "sess_456"));
    }
    pty_admission_get_counts(&live, &pending);
    ASSERT_EQ_INT(16, live);
    ASSERT_EQ_INT(0, pending);

    /* Reserve remaining 2 connections up to MAX_LIVE_WS (18) */
    for (int i = 16; i < MAX_LIVE_WS; i++) {
        ASSERT_EQ_INT(0, pty_admission_reserve((struct mg_connection *)(uintptr_t)(i + 1), &ctxs[i]));
        ASSERT_TRUE(ctxs[i].lease_id != 0);
    }
    pty_admission_get_counts(&live, &pending);
    ASSERT_EQ_INT(MAX_LIVE_WS, live);
    ASSERT_EQ_INT(2, pending);

    /* 19th connection must be rejected due to total live WebSocket limit */
    ASSERT_EQ_INT(-1, pty_admission_reserve((struct mg_connection *)(uintptr_t)1000, &overflow_ctx));

    /* Release all active leases */
    for (int i = 0; i < MAX_LIVE_WS; i++) {
        pty_admission_release_lease(ctxs[i].lease_id);
    }
    pty_admission_get_counts(&live, &pending);
    ASSERT_EQ_INT(0, live);
    ASSERT_EQ_INT(0, pending);
}

static void test_pty_admission_lifecycle_and_reclamation(void)
{
    ws_conn_ctx_t ctx1;
    memset(&ctx1, 0, sizeof(ctx1));
    ASSERT_EQ_INT(0, pty_admission_reserve((struct mg_connection *)(uintptr_t)42, &ctx1));
    uint64_t lease1 = ctx1.lease_id;
    ASSERT_TRUE(lease1 != 0);

    ASSERT_EQ_INT(0, pty_admission_confirm(lease1));
    ASSERT_EQ_INT(0, pty_admission_authenticate(lease1, "sess_abc"));

    ASSERT_FALSE(pty_admission_is_close_requested(lease1));
    pty_admission_request_close(lease1);
    ASSERT_TRUE(pty_admission_is_close_requested(lease1));

    pty_admission_release_lease(lease1);

    int live = -1, pending = -1;
    pty_admission_get_counts(&live, &pending);
    ASSERT_EQ_INT(0, live);
    ASSERT_EQ_INT(0, pending);

    /* Next allocation should reuse slot but assign an incremented generation */
    ws_conn_ctx_t ctx2;
    memset(&ctx2, 0, sizeof(ctx2));
    ASSERT_EQ_INT(0, pty_admission_reserve((struct mg_connection *)(uintptr_t)43, &ctx2));
    uint64_t lease2 = ctx2.lease_id;
    ASSERT_TRUE(lease2 != 0);
    ASSERT_TRUE(lease2 != lease1);

    /* Slot index in lower 32 bits should match, but upper generation must differ */
    uint32_t slot1 = (uint32_t)(lease1 & 0xFFFFFFFF);
    uint32_t slot2 = (uint32_t)(lease2 & 0xFFFFFFFF);
    uint32_t gen1  = (uint32_t)(lease1 >> 32);
    uint32_t gen2  = (uint32_t)(lease2 >> 32);
    ASSERT_EQ_INT((int)slot1, (int)slot2);
    ASSERT_TRUE(gen2 > gen1);

    /* Stale lease operations on old generation must fail or be no-ops */
    ASSERT_EQ_INT(-1, pty_admission_confirm(lease1));
    ASSERT_EQ_INT(-1, pty_admission_authenticate(lease1, "sess_stale"));
    pty_admission_release_lease(lease1); /* Stale release: ignored */

    pty_admission_get_counts(&live, &pending);
    ASSERT_EQ_INT(1, live); /* Still 1 live from ctx2 */

    /* Clean up ctx2 */
    pty_admission_release_lease(lease2);
    pty_admission_get_counts(&live, &pending);
    ASSERT_EQ_INT(0, live);
    ASSERT_EQ_INT(0, pending);
}

static void test_pty_admission_handshake_abort_cleanup(void)
{
    ws_conn_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ASSERT_EQ_INT(0, pty_admission_reserve((struct mg_connection *)(uintptr_t)777, &ctx));

    int live = -1, pending = -1;
    pty_admission_get_counts(&live, &pending);
    ASSERT_EQ_INT(1, live);
    ASSERT_EQ_INT(1, pending);

    /* Early abort during handshake before ready: connection_close callback runs */
    pty_admission_release_lease(ctx.lease_id);

    pty_admission_get_counts(&live, &pending);
    ASSERT_EQ_INT(0, live);
    ASSERT_EQ_INT(0, pending);
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
    RUN_TEST(test_pty_shutdown_multiple_sessions_and_descendants);
    RUN_TEST(test_pty_session_status_and_list);
    RUN_TEST(test_pty_session_max_limit);
    RUN_TEST(test_pty_admission_capacity_and_limits);
    RUN_TEST(test_pty_admission_lifecycle_and_reclamation);
    RUN_TEST(test_pty_admission_handshake_abort_cleanup);

    TEST_REPORT();
}
