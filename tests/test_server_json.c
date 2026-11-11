/*
 * test_server_json.c — Unit tests for SmartShell web server JSON serialization and parsing.
 */

#include "test_framework.h"
#include "server/json_api.h"
#include "proc_monitor.h"
#include "scheduler.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_sysinfo_serialization(void)
{
    proc_sysinfo_t info;
    memset(&info, 0, sizeof(info));
    strncpy(info.os_name, "Linux", sizeof(info.os_name) - 1);
    strncpy(info.kernel_release, "6.1.0-test", sizeof(info.kernel_release) - 1);
    strncpy(info.kernel_version, "#1 SMP PREEMPT", sizeof(info.kernel_version) - 1);
    strncpy(info.machine_arch, "x86_64", sizeof(info.machine_arch) - 1);
    info.cpus_online = 8;
    info.cpus_configured = 8;
    info.proc_accessible = true;
    info.mem.mem_total_kb = 16000000;
    info.mem.mem_available_kb = 12000000;
    info.mem.mem_free_kb = 8000000;
    info.mem.swap_total_kb = 2000000;
    info.mem.swap_free_kb = 2000000;
    info.shell_pid = 1234;
    info.shell_ppid = 1000;
    info.shell_pgid = 1234;
    info.shell_sid = 1000;

    char *json_str = json_serialize_sysinfo(&info);
    ASSERT_TRUE(json_str != NULL);

    cJSON *root = cJSON_Parse(json_str);
    ASSERT_TRUE(root != NULL);

    cJSON *os = cJSON_GetObjectItem(root, "os");
    ASSERT_TRUE(os != NULL && cJSON_IsString(os));
    ASSERT_EQ_STR("Linux 6.1.0-test", os->valuestring);

    cJSON *arch = cJSON_GetObjectItem(root, "architecture");
    ASSERT_TRUE(arch != NULL && cJSON_IsString(arch));
    ASSERT_EQ_STR("x86_64", arch->valuestring);

    cJSON *cpus = cJSON_GetObjectItem(root, "cpus_online");
    ASSERT_TRUE(cpus != NULL && cJSON_IsNumber(cpus));
    ASSERT_EQ_INT(8, (int)cpus->valuedouble);

    cJSON *mem = cJSON_GetObjectItem(root, "memory");
    ASSERT_TRUE(mem != NULL && cJSON_IsObject(mem));
    cJSON *total = cJSON_GetObjectItem(mem, "total_kb");
    ASSERT_TRUE(total != NULL && (int)total->valuedouble == 16000000);

    cJSON *shell = cJSON_GetObjectItem(root, "shell");
    ASSERT_TRUE(shell != NULL && cJSON_IsObject(shell));
    cJSON *pid = cJSON_GetObjectItem(shell, "pid");
    ASSERT_TRUE(pid != NULL && (int)pid->valuedouble == 1234);

    cJSON_Delete(root);
    free(json_str);

    /* Test NULL handling */
    ASSERT_TRUE(json_serialize_sysinfo(NULL) == NULL);
}

static void test_metrics_serialization(void)
{
    sys_mem_t mem;
    memset(&mem, 0, sizeof(mem));
    mem.mem_total_kb = 8000000;
    mem.mem_available_kb = 5000000;
    mem.mem_free_kb = 2000000;
    mem.buffers_kb = 500000;
    mem.cached_kb = 2500000;
    mem.swap_total_kb = 1000000;
    mem.swap_free_kb = 1000000;

    char *json_str = json_serialize_metrics(35.5, &mem);
    ASSERT_TRUE(json_str != NULL);

    cJSON *root = cJSON_Parse(json_str);
    ASSERT_TRUE(root != NULL);

    cJSON *cpu = cJSON_GetObjectItem(root, "cpu_utilization");
    ASSERT_TRUE(cpu != NULL && cJSON_IsNumber(cpu));
    ASSERT_TRUE(cpu->valuedouble >= 35.4 && cpu->valuedouble <= 35.6);

    cJSON *m = cJSON_GetObjectItem(root, "memory");
    ASSERT_TRUE(m != NULL && cJSON_IsObject(m));
    cJSON *avail = cJSON_GetObjectItem(m, "available_kb");
    ASSERT_TRUE(avail != NULL && (int)avail->valuedouble == 5000000);

    cJSON_Delete(root);
    free(json_str);

    /* Test with NULL mem */
    char *json_no_mem = json_serialize_metrics(10.0, NULL);
    ASSERT_TRUE(json_no_mem != NULL);
    cJSON *r2 = cJSON_Parse(json_no_mem);
    ASSERT_TRUE(r2 != NULL);
    ASSERT_TRUE(cJSON_GetObjectItem(r2, "memory") == NULL);
    cJSON_Delete(r2);
    free(json_no_mem);
}

static void test_processes_serialization(void)
{
    proc_snapshot_t snap;
    proc_snap_entry_t entries[3];
    memset(&snap, 0, sizeof(snap));
    memset(entries, 0, sizeof(entries));

    entries[0].pid = 1;
    entries[0].ppid = 0;
    entries[0].pgid = 1;
    strncpy(entries[0].name, "systemd", sizeof(entries[0].name) - 1);
    entries[0].state_char = 'S';
    entries[0].state_str = "Sleeping";
    entries[0].cpu_percent = 0.5;
    entries[0].rss_bytes = 1024 * 1024 * 10;
    entries[0].vsize_bytes = 1024 * 1024 * 50;
    entries[0].num_threads = 1;

    entries[1].pid = 200;
    entries[1].ppid = 1;
    entries[1].pgid = 200;
    strncpy(entries[1].name, "smartshell", sizeof(entries[1].name) - 1);
    entries[1].state_char = 'R';
    entries[1].state_str = "Running";
    entries[1].cpu_percent = 25.0;
    entries[1].rss_bytes = 1024 * 1024 * 5;
    entries[1].vsize_bytes = 1024 * 1024 * 20;
    entries[1].num_threads = 2;

    entries[2].pid = 300;
    entries[2].ppid = 200;
    entries[2].pgid = 200;
    strncpy(entries[2].name, "worker", sizeof(entries[2].name) - 1);
    entries[2].state_char = 'S';
    entries[2].state_str = "Sleeping";
    entries[2].cpu_percent = 10.0;
    entries[2].rss_bytes = 1024 * 1024 * 8;
    entries[2].vsize_bytes = 1024 * 1024 * 25;
    entries[2].num_threads = 1;

    snap.entries = entries;
    snap.count = 3;
    snap.capacity = 3;

    /* Serialize with limit = 2 */
    char *json_str = json_serialize_processes(&snap, PROC_SORT_CPU, 2);
    ASSERT_TRUE(json_str != NULL);

    cJSON *root = cJSON_Parse(json_str);
    ASSERT_TRUE(root != NULL);

    cJSON *sort = cJSON_GetObjectItem(root, "sort");
    ASSERT_TRUE(sort != NULL && strcmp(sort->valuestring, "cpu") == 0);

    cJSON *total = cJSON_GetObjectItem(root, "total");
    ASSERT_TRUE(total != NULL && (int)total->valuedouble == 3);

    cJSON *count = cJSON_GetObjectItem(root, "count");
    ASSERT_TRUE(count != NULL && (int)count->valuedouble == 2);

    cJSON *procs = cJSON_GetObjectItem(root, "processes");
    ASSERT_TRUE(procs != NULL && cJSON_IsArray(procs));
    ASSERT_EQ_INT(2, cJSON_GetArraySize(procs));

    cJSON *p0 = cJSON_GetArrayItem(procs, 0);
    ASSERT_EQ_INT(1, (int)cJSON_GetObjectItem(p0, "pid")->valuedouble);
    ASSERT_EQ_STR("systemd", cJSON_GetObjectItem(p0, "name")->valuestring);

    cJSON_Delete(root);
    free(json_str);

    /* Test NULL snapshot */
    ASSERT_TRUE(json_serialize_processes(NULL, PROC_SORT_CPU, 0) == NULL);
}

static void test_pid_info_serialization(void)
{
    proc_info_t info;
    memset(&info, 0, sizeof(info));
    info.pid = 42;
    info.ppid = 1;
    info.pgid = 42;
    info.sid = 42;
    info.uid = 1000;
    strncpy(info.name, "my_service", sizeof(info.name) - 1);
    info.state_char = 'R';
    info.state_str = "Running";
    info.num_threads = 4;
    info.vsize_bytes = 100000000;
    info.rss_bytes = 50000000;
    info.utime_ticks = 100;
    info.stime_ticks = 50;
    info.cpu_percent = 12.5;

    char *json_str = json_serialize_pid_info(&info);
    ASSERT_TRUE(json_str != NULL);

    cJSON *root = cJSON_Parse(json_str);
    ASSERT_TRUE(root != NULL);

    ASSERT_EQ_INT(42, (int)cJSON_GetObjectItem(root, "pid")->valuedouble);
    ASSERT_EQ_INT(1, (int)cJSON_GetObjectItem(root, "ppid")->valuedouble);
    ASSERT_EQ_STR("my_service", cJSON_GetObjectItem(root, "name")->valuestring);
    ASSERT_EQ_STR("R", cJSON_GetObjectItem(root, "state")->valuestring);
    ASSERT_EQ_STR("Running", cJSON_GetObjectItem(root, "state_str")->valuestring);
    ASSERT_EQ_INT(4, (int)cJSON_GetObjectItem(root, "threads")->valuedouble);

    cJSON_Delete(root);
    free(json_str);

    ASSERT_TRUE(json_serialize_pid_info(NULL) == NULL);
}

static void test_proc_tree_serialization(void)
{
    proc_tree_t tree;
    memset(&tree, 0, sizeof(tree));

    proc_tree_node_t n1, n2, n3;
    memset(&n1, 0, sizeof(n1));
    memset(&n2, 0, sizeof(n2));
    memset(&n3, 0, sizeof(n3));

    n1.pid = 1;
    n1.ppid = 0;
    strncpy(n1.name, "systemd", sizeof(n1.name) - 1);
    n1.state_char = 'S';

    n2.pid = 100;
    n2.ppid = 1;
    strncpy(n2.name, "bash", sizeof(n2.name) - 1);
    n2.state_char = 'S';

    n3.pid = 200;
    n3.ppid = 100;
    strncpy(n3.name, "smartshell", sizeof(n3.name) - 1);
    n3.state_char = 'R';

    proc_tree_node_t *n1_children[] = { &n2 };
    n1.children = n1_children;
    n1.num_children = 1;

    proc_tree_node_t *n2_children[] = { &n3 };
    n2.children = n2_children;
    n2.num_children = 1;

    proc_tree_node_t *nodes[] = { &n1, &n2, &n3 };
    tree.nodes = nodes;
    tree.count = 3;
    tree.capacity = 3;

    char *json_str = json_serialize_proc_tree(&tree);
    ASSERT_TRUE(json_str != NULL);

    cJSON *root = cJSON_Parse(json_str);
    ASSERT_TRUE(root != NULL);

    cJSON *total = cJSON_GetObjectItem(root, "total_nodes");
    ASSERT_TRUE(total != NULL && (int)total->valuedouble == 3);

    cJSON *roots = cJSON_GetObjectItem(root, "roots");
    ASSERT_TRUE(roots != NULL && cJSON_IsArray(roots));
    ASSERT_EQ_INT(1, cJSON_GetArraySize(roots));

    cJSON *root_node = cJSON_GetArrayItem(roots, 0);
    ASSERT_EQ_INT(1, (int)cJSON_GetObjectItem(root_node, "pid")->valuedouble);

    cJSON *r_children = cJSON_GetObjectItem(root_node, "children");
    ASSERT_TRUE(r_children != NULL && cJSON_GetArraySize(r_children) == 1);

    cJSON *child1 = cJSON_GetArrayItem(r_children, 0);
    ASSERT_EQ_INT(100, (int)cJSON_GetObjectItem(child1, "pid")->valuedouble);

    cJSON_Delete(root);
    free(json_str);

    ASSERT_TRUE(json_serialize_proc_tree(NULL) == NULL);
}

static void test_scheduler_simulate_parsing(void)
{
    sched_process_t procs[SCHED_MAX_CUSTOM_PROCESSES];
    int count = 0;
    char algo[32];
    int quantum = 0;
    char err[256];

    /* Valid FCFS */
    const char *valid_fcfs = "{\"algorithm\":\"fcfs\",\"processes\":[{\"pid\":1,\"arrival_time\":0,\"burst_time\":5,\"priority\":1},{\"pid\":2,\"arrival_time\":2,\"burst_time\":3,\"priority\":2}]}";
    ASSERT_TRUE(json_parse_scheduler_simulate(valid_fcfs, procs, &count, algo, sizeof(algo), &quantum, err, sizeof(err)));
    ASSERT_EQ_STR("fcfs", algo);
    ASSERT_EQ_INT(2, count);
    ASSERT_EQ_INT(1, procs[0].pid);
    ASSERT_EQ_INT(0, procs[0].arrival_time);
    ASSERT_EQ_INT(5, procs[0].burst_time);
    ASSERT_EQ_INT(1, procs[0].priority);
    ASSERT_EQ_INT(2, procs[1].pid);
    ASSERT_EQ_INT(2, procs[1].arrival_time);
    ASSERT_EQ_INT(3, procs[1].burst_time);
    ASSERT_EQ_INT(2, procs[1].priority);

    /* Valid RR with custom quantum */
    const char *valid_rr = "{\"algorithm\":\"RR\",\"quantum\":4,\"processes\":[{\"pid\":10,\"arrival_time\":0,\"burst_time\":10}]}";
    ASSERT_TRUE(json_parse_scheduler_simulate(valid_rr, procs, &count, algo, sizeof(algo), &quantum, err, sizeof(err)));
    ASSERT_EQ_STR("rr", algo);
    ASSERT_EQ_INT(4, quantum);
    ASSERT_EQ_INT(1, count);
    ASSERT_EQ_INT(10, procs[0].pid);

    /* Invalid: Malformed JSON */
    ASSERT_FALSE(json_parse_scheduler_simulate("{broken json", procs, &count, algo, sizeof(algo), &quantum, err, sizeof(err)));

    /* Invalid: Missing algorithm */
    ASSERT_FALSE(json_parse_scheduler_simulate("{\"processes\":[]}", procs, &count, algo, sizeof(algo), &quantum, err, sizeof(err)));

    /* Invalid: Unsupported algorithm */
    ASSERT_FALSE(json_parse_scheduler_simulate("{\"algorithm\":\"lottery\",\"processes\":[]}", procs, &count, algo, sizeof(algo), &quantum, err, sizeof(err)));

    /* Invalid: Empty processes */
    ASSERT_FALSE(json_parse_scheduler_simulate("{\"algorithm\":\"fcfs\",\"processes\":[]}", procs, &count, algo, sizeof(algo), &quantum, err, sizeof(err)));

    /* Invalid: Negative arrival time */
    const char *neg_arr = "{\"algorithm\":\"fcfs\",\"processes\":[{\"pid\":1,\"arrival_time\":-1,\"burst_time\":5}]}";
    ASSERT_FALSE(json_parse_scheduler_simulate(neg_arr, procs, &count, algo, sizeof(algo), &quantum, err, sizeof(err)));

    /* Invalid: Zero or negative burst time */
    const char *zero_bt = "{\"algorithm\":\"fcfs\",\"processes\":[{\"pid\":1,\"arrival_time\":0,\"burst_time\":0}]}";
    ASSERT_FALSE(json_parse_scheduler_simulate(zero_bt, procs, &count, algo, sizeof(algo), &quantum, err, sizeof(err)));

    /* Invalid: Duplicate PID */
    const char *dup_pid = "{\"algorithm\":\"fcfs\",\"processes\":[{\"pid\":1,\"arrival_time\":0,\"burst_time\":2},{\"pid\":1,\"arrival_time\":1,\"burst_time\":3}]}";
    ASSERT_FALSE(json_parse_scheduler_simulate(dup_pid, procs, &count, algo, sizeof(algo), &quantum, err, sizeof(err)));

    /* Invalid: Non-positive quantum */
    const char *bad_q = "{\"algorithm\":\"rr\",\"quantum\":-2,\"processes\":[{\"pid\":1,\"arrival_time\":0,\"burst_time\":2}]}";
    ASSERT_FALSE(json_parse_scheduler_simulate(bad_q, procs, &count, algo, sizeof(algo), &quantum, err, sizeof(err)));
}

static void test_scheduler_simulation_and_serialization(void)
{
    sched_process_t procs[2];
    sched_process_init(&procs[0], 1, 0, 4, 1);
    sched_process_init(&procs[1], 2, 1, 3, 2);

    sched_result_t result;
    sched_result_init(&result);
    ASSERT_EQ_INT(0, sched_schedule_fcfs(procs, 2, &result));

    char *json_str = json_serialize_sched_result("fcfs", 0, &result);
    ASSERT_TRUE(json_str != NULL);

    cJSON *root = cJSON_Parse(json_str);
    ASSERT_TRUE(root != NULL);

    cJSON *algo = cJSON_GetObjectItem(root, "algorithm");
    ASSERT_TRUE(algo != NULL && strcmp(algo->valuestring, "fcfs") == 0);

    cJSON *summary = cJSON_GetObjectItem(root, "summary");
    ASSERT_TRUE(summary != NULL);
    ASSERT_TRUE(cJSON_GetObjectItem(summary, "avg_waiting_time") != NULL);
    ASSERT_TRUE(cJSON_GetObjectItem(summary, "cpu_utilization") != NULL);

    cJSON *procs_arr = cJSON_GetObjectItem(root, "processes");
    ASSERT_TRUE(procs_arr != NULL && cJSON_GetArraySize(procs_arr) == 2);

    cJSON *timeline = cJSON_GetObjectItem(root, "timeline");
    ASSERT_TRUE(timeline != NULL && cJSON_IsArray(timeline));

    cJSON *gantt = cJSON_GetObjectItem(root, "gantt_chart");
    ASSERT_TRUE(gantt != NULL && cJSON_IsString(gantt));

    cJSON_Delete(root);
    free(json_str);
    sched_result_free(&result);
}

static void test_scheduler_compare_parsing_and_serialization(void)
{
    sched_process_t procs[SCHED_MAX_CUSTOM_PROCESSES];
    int count = 0;
    int quantum = 0;
    char err[256];

    const char *payload = "{\"quantum\":3,\"processes\":[{\"pid\":1,\"arrival_time\":0,\"burst_time\":4,\"priority\":1},{\"pid\":2,\"arrival_time\":2,\"burst_time\":2,\"priority\":2}]}";
    ASSERT_TRUE(json_parse_scheduler_compare(payload, procs, &count, &quantum, err, sizeof(err)));
    ASSERT_EQ_INT(3, quantum);
    ASSERT_EQ_INT(2, count);

    sched_compare_result_t cmp;
    ASSERT_EQ_INT(0, sched_compare_all(procs, count, quantum, &cmp));

    char *json_str = json_serialize_sched_compare(&cmp);
    ASSERT_TRUE(json_str != NULL);

    cJSON *root = cJSON_Parse(json_str);
    ASSERT_TRUE(root != NULL);

    cJSON *results = cJSON_GetObjectItem(root, "results");
    ASSERT_TRUE(results != NULL);
    ASSERT_TRUE(cJSON_GetObjectItem(results, "fcfs") != NULL);
    ASSERT_TRUE(cJSON_GetObjectItem(results, "sjf") != NULL);
    ASSERT_TRUE(cJSON_GetObjectItem(results, "rr") != NULL);
    ASSERT_TRUE(cJSON_GetObjectItem(results, "priority") != NULL);
    ASSERT_TRUE(cJSON_GetObjectItem(results, "mlfq") != NULL);

    cJSON *table = cJSON_GetObjectItem(root, "comparison_table");
    ASSERT_TRUE(table != NULL && cJSON_IsString(table));

    cJSON_Delete(root);
    free(json_str);
    sched_compare_result_free(&cmp);
}

int main(void)
{
    TEST_INIT();
    printf("Running Server JSON API Unit Tests:\n");

    RUN_TEST(test_sysinfo_serialization);
    RUN_TEST(test_metrics_serialization);
    RUN_TEST(test_processes_serialization);
    RUN_TEST(test_pid_info_serialization);
    RUN_TEST(test_proc_tree_serialization);
    RUN_TEST(test_scheduler_simulate_parsing);
    RUN_TEST(test_scheduler_simulation_and_serialization);
    RUN_TEST(test_scheduler_compare_parsing_and_serialization);

    TEST_REPORT();
}
