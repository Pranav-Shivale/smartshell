/*
 * test_proc_monitor.c — Unit tests for Phase 7 /proc process monitoring subsystem.
 */

#include "proc_monitor.h"
#include "test_framework.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_parse_pid_stat_normal(void)
{
    const char *fixture =
        "1234 (test_proc) S 1000 1234 1000 34817 1234 4194304 309 221 0 0 15 25 0 0 20 0 4 0 100000 10485760 512 18446744073709551615";

    proc_info_t info;
    memset(&info, 0, sizeof(info));

    bool ok = proc_parse_pid_stat(fixture, &info);
    ASSERT_TRUE(ok);
    ASSERT_EQ_INT(1234, info.pid);
    ASSERT_EQ_STR("test_proc", info.name);
    ASSERT_EQ_INT('S', info.state_char);
    ASSERT_EQ_STR("Sleeping", info.state_str);
    ASSERT_EQ_INT(1000, info.ppid);
    ASSERT_EQ_INT(1234, info.pgid);
    ASSERT_EQ_INT(1000, info.sid);
    ASSERT_EQ_INT(15, (int)info.utime_ticks);
    ASSERT_EQ_INT(25, (int)info.stime_ticks);
    ASSERT_EQ_INT(4, (int)info.num_threads);
    ASSERT_EQ_INT(10485760, (int)info.vsize_bytes);
    ASSERT_TRUE(info.rss_bytes > 0);
}

static void test_parse_pid_stat_spaces_and_parens_in_name(void)
{
    /* Process name containing both spaces and nested parentheses */
    const char *fixture =
        "5678 (worker (bg) thread) R 1 5678 5678 0 0 0 0 0 0 0 100 200 0 0 20 0 2 0 0 20971520 1024";

    proc_info_t info;
    memset(&info, 0, sizeof(info));

    bool ok = proc_parse_pid_stat(fixture, &info);
    ASSERT_TRUE(ok);
    ASSERT_EQ_INT(5678, info.pid);
    ASSERT_EQ_STR("worker (bg) thread", info.name);
    ASSERT_EQ_INT('R', info.state_char);
    ASSERT_EQ_STR("Running", info.state_str);
    ASSERT_EQ_INT(1, info.ppid);
    ASSERT_EQ_INT(5678, info.pgid);
    ASSERT_EQ_INT(100, (int)info.utime_ticks);
    ASSERT_EQ_INT(200, (int)info.stime_ticks);
}

static void test_parse_pid_stat_malformed(void)
{
    proc_info_t info;

    /* NULL input */
    ASSERT_FALSE(proc_parse_pid_stat(NULL, &info));

    /* Empty input */
    ASSERT_FALSE(proc_parse_pid_stat("", &info));

    /* Missing parentheses */
    ASSERT_FALSE(proc_parse_pid_stat("1234 test_proc S 1000 1234 1000", &info));

    /* Incomplete fields */
    ASSERT_FALSE(proc_parse_pid_stat("1234 (test_proc)", &info));

    /* Negative PID */
    ASSERT_FALSE(proc_parse_pid_stat("-1234 (test_proc) S 1 1 1 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0", &info));
}

static void test_parse_pid_status(void)
{
    const char *fixture =
        "Name:\ttest_proc\n"
        "State:\tS (sleeping)\n"
        "Uid:\t1005\t1005\t1005\t1005\n"
        "Threads:\t8\n"
        "VmSize:\t   16384 kB\n"
        "VmRSS:\t    4096 kB\n";

    proc_info_t info;
    memset(&info, 0, sizeof(info));

    bool ok = proc_parse_pid_status(fixture, &info);
    ASSERT_TRUE(ok);
    ASSERT_EQ_INT(1005, (int)info.uid);
    ASSERT_EQ_INT(8, (int)info.num_threads);
    ASSERT_EQ_INT(16384 * 1024, (int)info.vsize_bytes);
    ASSERT_EQ_INT(4096 * 1024, (int)info.rss_bytes);
}

static void test_parse_proc_stat(void)
{
    const char *fixture =
        "cpu  1000 200 300 5000 50 10 20 5 0 0\n"
        "cpu0 500 100 150 2500 25 5 10 2 0 0\n"
        "intr 1234567\n";

    sys_cpu_t cpu;
    memset(&cpu, 0, sizeof(cpu));

    bool ok = proc_parse_proc_stat(fixture, &cpu);
    ASSERT_TRUE(ok);
    ASSERT_EQ_INT(1000, (int)cpu.user);
    ASSERT_EQ_INT(200, (int)cpu.nice);
    ASSERT_EQ_INT(300, (int)cpu.system);
    ASSERT_EQ_INT(5000, (int)cpu.idle);
    ASSERT_EQ_INT(50, (int)cpu.iowait);
    ASSERT_EQ_INT(10, (int)cpu.irq);
    ASSERT_EQ_INT(20, (int)cpu.softirq);
    ASSERT_EQ_INT(5, (int)cpu.steal);
    ASSERT_EQ_INT(5050, (int)cpu.idle_all);
    ASSERT_EQ_INT(6585, (int)cpu.total);
}

static void test_parse_proc_stat_malformed(void)
{
    sys_cpu_t cpu;

    ASSERT_FALSE(proc_parse_proc_stat(NULL, &cpu));
    ASSERT_FALSE(proc_parse_proc_stat("", &cpu));
    ASSERT_FALSE(proc_parse_proc_stat("intr 1234567\nctxt 89012\n", &cpu));
}

static void test_parse_meminfo(void)
{
    const char *fixture =
        "MemTotal:       16384000 kB\n"
        "MemFree:         4096000 kB\n"
        "MemAvailable:   10240000 kB\n"
        "Buffers:          512000 kB\n"
        "Cached:          2048000 kB\n"
        "SwapTotal:       2048000 kB\n"
        "SwapFree:        1024000 kB\n";

    sys_mem_t mem;
    memset(&mem, 0, sizeof(mem));

    bool ok = proc_parse_meminfo(fixture, &mem);
    ASSERT_TRUE(ok);
    ASSERT_EQ_INT(16384000, (int)mem.mem_total_kb);
    ASSERT_EQ_INT(4096000, (int)mem.mem_free_kb);
    ASSERT_EQ_INT(10240000, (int)mem.mem_available_kb);
    ASSERT_EQ_INT(512000, (int)mem.buffers_kb);
    ASSERT_EQ_INT(2048000, (int)mem.cached_kb);
    ASSERT_EQ_INT(2048000, (int)mem.swap_total_kb);
    ASSERT_EQ_INT(1024000, (int)mem.swap_free_kb);
}

static void test_cpu_utilization_calculation(void)
{
    sys_cpu_t s1, s2;

    /* 1. Normal delta: total 200, idle delta 50 -> 75% */
    s1.total = 1000;
    s1.idle_all = 800;
    s2.total = 1200;
    s2.idle_all = 850;

    double util = proc_calc_cpu_utilization(&s1, &s2);
    ASSERT_TRUE(fabs(util - 75.0) < 0.001);

    /* 2. Zero delta -> 0% */
    s2.total = 1000;
    s2.idle_all = 800;
    util = proc_calc_cpu_utilization(&s1, &s2);
    ASSERT_TRUE(fabs(util - 0.0) < 0.001);

    /* 3. Counter anomaly (s2 < s1) -> 0% */
    s2.total = 900;
    s2.idle_all = 700;
    util = proc_calc_cpu_utilization(&s1, &s2);
    ASSERT_TRUE(fabs(util - 0.0) < 0.001);

    /* 4. 100% busy: idle_delta = 0 */
    s1.total = 1000;
    s1.idle_all = 800;
    s2.total = 1100;
    s2.idle_all = 800;
    util = proc_calc_cpu_utilization(&s1, &s2);
    ASSERT_TRUE(fabs(util - 100.0) < 0.001);

    /* 5. 100% idle: idle_delta == total_delta */
    s1.total = 1000;
    s1.idle_all = 800;
    s2.total = 1100;
    s2.idle_all = 900;
    util = proc_calc_cpu_utilization(&s1, &s2);
    ASSERT_TRUE(fabs(util - 0.0) < 0.001);
}

static void test_format_and_state_helpers(void)
{
    char buf[64];

    proc_format_bytes(512, buf, sizeof(buf));
    ASSERT_EQ_STR("512 B", buf);

    proc_format_bytes(2048, buf, sizeof(buf));
    ASSERT_EQ_STR("2.0 KB", buf);

    proc_format_bytes(10 * 1024 * 1024, buf, sizeof(buf));
    ASSERT_EQ_STR("10.0 MB", buf);

    proc_format_bytes(2ULL * 1024 * 1024 * 1024, buf, sizeof(buf));
    ASSERT_EQ_STR("2.00 GB", buf);

    ASSERT_EQ_STR("Running", proc_state_to_string('R'));
    ASSERT_EQ_STR("Sleeping", proc_state_to_string('S'));
    ASSERT_EQ_STR("Zombie", proc_state_to_string('Z'));
    ASSERT_EQ_STR("Stopped", proc_state_to_string('T'));
    ASSERT_EQ_STR("Unknown", proc_state_to_string('?'));
}

static void test_live_proc_self(void)
{
    proc_info_t info;
    pid_t self_pid = getpid();

    bool ok = proc_read_pid_info(self_pid, &info);
    ASSERT_TRUE(ok);
    ASSERT_EQ_INT((int)self_pid, (int)info.pid);
    ASSERT_TRUE(info.ppid > 0);
    ASSERT_TRUE(strlen(info.name) > 0);
    ASSERT_TRUE(info.vsize_bytes > 0);

    sys_cpu_t cpu;
    ok = proc_read_sys_cpu(&cpu);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(cpu.total > 0);

    sys_mem_t mem;
    ok = proc_read_sys_mem(&mem);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(mem.mem_total_kb > 0);
    ASSERT_TRUE(mem.mem_available_kb > 0);
}

static void test_invalid_and_disappearing_pid(void)
{
    proc_info_t info;

    /* Negative PID */
    ASSERT_FALSE(proc_read_pid_info(-1, &info));
    ASSERT_FALSE(proc_sample_pid_info(-1, 10, &info));

    /* Zero PID */
    ASSERT_FALSE(proc_read_pid_info(0, &info));

    /* Nonexistent PID */
    ASSERT_FALSE(proc_read_pid_info(9999999, &info));
    ASSERT_FALSE(proc_sample_pid_info(9999999, 10, &info));
}

int main(void)
{
    TEST_INIT();

    printf("Running Phase 7 /proc Monitor Unit Tests:\n");
    RUN_TEST(test_parse_pid_stat_normal);
    RUN_TEST(test_parse_pid_stat_spaces_and_parens_in_name);
    RUN_TEST(test_parse_pid_stat_malformed);
    RUN_TEST(test_parse_pid_status);
    RUN_TEST(test_parse_proc_stat);
    RUN_TEST(test_parse_proc_stat_malformed);
    RUN_TEST(test_parse_meminfo);
    RUN_TEST(test_cpu_utilization_calculation);
    RUN_TEST(test_format_and_state_helpers);
    RUN_TEST(test_live_proc_self);
    RUN_TEST(test_invalid_and_disappearing_pid);

    TEST_REPORT();
}
