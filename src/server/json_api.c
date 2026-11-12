/*
 * json_api.c — JSON serialization and deserialization implementation for SmartShell server.
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "server/json_api.h"
#include "utils.h"

#include <cJSON.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---- System Information & Metrics -------------------------------------- */

char *json_serialize_sysinfo(const proc_sysinfo_t *info)
{
    if (info == NULL) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;

    char full_os[128];
    snprintf(full_os, sizeof(full_os), "%s %s", info->os_name, info->kernel_release);

    cJSON_AddStringToObject(root, "os", full_os);
    cJSON_AddStringToObject(root, "os_name", info->os_name);
    cJSON_AddStringToObject(root, "kernel_release", info->kernel_release);
    cJSON_AddStringToObject(root, "kernel_version", info->kernel_version);
    cJSON_AddStringToObject(root, "architecture", info->machine_arch);
    cJSON_AddNumberToObject(root, "cpus_online", (double)info->cpus_online);
    cJSON_AddNumberToObject(root, "cpus_configured", (double)info->cpus_configured);
    cJSON_AddBoolToObject(root, "proc_accessible", info->proc_accessible);

    /* Memory object */
    cJSON *mem = cJSON_CreateObject();
    if (mem != NULL) {
        cJSON_AddNumberToObject(mem, "total_kb", (double)info->mem.mem_total_kb);
        cJSON_AddNumberToObject(mem, "available_kb", (double)info->mem.mem_available_kb);
        cJSON_AddNumberToObject(mem, "free_kb", (double)info->mem.mem_free_kb);
        cJSON_AddNumberToObject(mem, "swap_total_kb", (double)info->mem.swap_total_kb);
        cJSON_AddNumberToObject(mem, "swap_free_kb", (double)info->mem.swap_free_kb);
        cJSON_AddItemToObject(root, "memory", mem);
    }

    /* Shell process environment */
    cJSON *shell = cJSON_CreateObject();
    if (shell != NULL) {
        cJSON_AddNumberToObject(shell, "pid", (double)info->shell_pid);
        cJSON_AddNumberToObject(shell, "ppid", (double)info->shell_ppid);
        cJSON_AddNumberToObject(shell, "pgid", (double)info->shell_pgid);
        cJSON_AddNumberToObject(shell, "sid", (double)info->shell_sid);
        cJSON_AddItemToObject(root, "shell", shell);
    }

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

char *json_serialize_metrics(double cpu_util, const sys_mem_t *mem)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;

    if (cpu_util >= 0.0) {
        cJSON_AddNumberToObject(root, "cpu_utilization", cpu_util);
    } else {
        cJSON_AddNullToObject(root, "cpu_utilization");
    }

    if (mem != NULL) {
        cJSON *m = cJSON_CreateObject();
        if (m != NULL) {
            cJSON_AddNumberToObject(m, "total_kb", (double)mem->mem_total_kb);
            cJSON_AddNumberToObject(m, "available_kb", (double)mem->mem_available_kb);
            cJSON_AddNumberToObject(m, "free_kb", (double)mem->mem_free_kb);
            cJSON_AddNumberToObject(m, "buffers_kb", (double)mem->buffers_kb);
            cJSON_AddNumberToObject(m, "cached_kb", (double)mem->cached_kb);
            cJSON_AddNumberToObject(m, "swap_total_kb", (double)mem->swap_total_kb);
            cJSON_AddNumberToObject(m, "swap_free_kb", (double)mem->swap_free_kb);
            cJSON_AddItemToObject(root, "memory", m);
        }
    }

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

/* ---- Process Monitoring ------------------------------------------------ */

char *json_serialize_processes(const proc_snapshot_t *snap, proc_sort_mode_t sort_mode, size_t limit)
{
    if (snap == NULL) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;

    cJSON_AddStringToObject(root, "sort", sort_mode == PROC_SORT_CPU ? "cpu" : "memory");
    cJSON_AddNumberToObject(root, "total", (double)snap->count);

    size_t count = (limit > 0 && limit < snap->count) ? limit : snap->count;
    cJSON_AddNumberToObject(root, "count", (double)count);

    cJSON *arr = cJSON_CreateArray();
    if (arr == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    for (size_t i = 0; i < count; i++) {
        const proc_snap_entry_t *e = &snap->entries[i];
        cJSON *item = cJSON_CreateObject();
        if (item == NULL) continue;

        char state_str[2] = { e->state_char, '\0' };

        cJSON_AddNumberToObject(item, "pid", (double)e->pid);
        cJSON_AddNumberToObject(item, "ppid", (double)e->ppid);
        cJSON_AddNumberToObject(item, "pgid", (double)e->pgid);
        cJSON_AddStringToObject(item, "name", e->name);
        cJSON_AddStringToObject(item, "state", state_str);
        cJSON_AddStringToObject(item, "state_str", e->state_str ? e->state_str : proc_state_to_string(e->state_char));
        cJSON_AddNumberToObject(item, "cpu_percent", e->cpu_percent);
        cJSON_AddNumberToObject(item, "rss_bytes", (double)e->rss_bytes);
        cJSON_AddNumberToObject(item, "vsize_bytes", (double)e->vsize_bytes);
        cJSON_AddNumberToObject(item, "threads", (double)e->num_threads);

        cJSON_AddItemToArray(arr, item);
    }

    cJSON_AddItemToObject(root, "processes", arr);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

char *json_serialize_pid_info(const proc_info_t *info)
{
    if (info == NULL) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;

    char state_str[2] = { info->state_char, '\0' };

    cJSON_AddNumberToObject(root, "pid", (double)info->pid);
    cJSON_AddNumberToObject(root, "ppid", (double)info->ppid);
    cJSON_AddNumberToObject(root, "pgid", (double)info->pgid);
    cJSON_AddNumberToObject(root, "sid", (double)info->sid);
    cJSON_AddNumberToObject(root, "uid", (double)info->uid);
    cJSON_AddStringToObject(root, "name", info->name);
    cJSON_AddStringToObject(root, "state", state_str);
    cJSON_AddStringToObject(root, "state_str", info->state_str ? info->state_str : proc_state_to_string(info->state_char));
    cJSON_AddNumberToObject(root, "threads", (double)info->num_threads);
    cJSON_AddNumberToObject(root, "vsize_bytes", (double)info->vsize_bytes);
    cJSON_AddNumberToObject(root, "rss_bytes", (double)info->rss_bytes);
    cJSON_AddNumberToObject(root, "utime_ticks", (double)info->utime_ticks);
    cJSON_AddNumberToObject(root, "stime_ticks", (double)info->stime_ticks);
    cJSON_AddNumberToObject(root, "cpu_percent", info->cpu_percent);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

/* Helper to serialize a process tree node recursively with cycle detection */
static cJSON *serialize_tree_node(proc_tree_node_t *node, int depth)
{
    if (node == NULL || node->visited || depth > 64) {
        return NULL;
    }

    node->visited = true;

    cJSON *item = cJSON_CreateObject();
    if (item == NULL) return NULL;

    char state_str[2] = { node->state_char, '\0' };
    cJSON_AddNumberToObject(item, "pid", (double)node->pid);
    cJSON_AddNumberToObject(item, "ppid", (double)node->ppid);
    cJSON_AddStringToObject(item, "name", node->name);
    cJSON_AddStringToObject(item, "state", state_str);

    cJSON *children_arr = cJSON_CreateArray();
    if (children_arr != NULL) {
        for (size_t i = 0; i < node->num_children; i++) {
            cJSON *child_obj = serialize_tree_node(node->children[i], depth + 1);
            if (child_obj != NULL) {
                cJSON_AddItemToArray(children_arr, child_obj);
            }
        }
        cJSON_AddItemToObject(item, "children", children_arr);
    }

    return item;
}

char *json_serialize_proc_tree(const proc_tree_t *tree)
{
    if (tree == NULL) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;

    cJSON_AddNumberToObject(root, "total_nodes", (double)tree->count);

    /* Reset visited flags */
    for (size_t i = 0; i < tree->count; i++) {
        tree->nodes[i]->visited = false;
    }

    cJSON *roots_arr = cJSON_CreateArray();
    if (roots_arr == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    /* Find root nodes: PID 1 or nodes whose parent is not present in the tree */
    for (size_t i = 0; i < tree->count; i++) {
        proc_tree_node_t *node = tree->nodes[i];
        if (node->pid == 1 || node->ppid == 0 || proc_tree_find_node(tree, node->ppid) == NULL) {
            cJSON *node_obj = serialize_tree_node(node, 0);
            if (node_obj != NULL) {
                cJSON_AddItemToArray(roots_arr, node_obj);
            }
        }
    }

    /* Also capture any remaining unvisited nodes as roots to prevent missing processes */
    for (size_t i = 0; i < tree->count; i++) {
        proc_tree_node_t *node = tree->nodes[i];
        if (!node->visited) {
            cJSON *node_obj = serialize_tree_node(node, 0);
            if (node_obj != NULL) {
                cJSON_AddItemToArray(roots_arr, node_obj);
            }
        }
    }

    cJSON_AddItemToObject(root, "roots", roots_arr);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

/* ---- Scheduler Simulation & Comparison --------------------------------- */

static bool parse_workload_processes(const cJSON *procs_array,
                                    sched_process_t *out_procs,
                                    int *out_count,
                                    char *err_buf,
                                    size_t err_size)
{
    if (!cJSON_IsArray(procs_array)) {
        snprintf(err_buf, err_size, "field 'processes' must be an array");
        return false;
    }

    int count = cJSON_GetArraySize(procs_array);
    if (count <= 0) {
        snprintf(err_buf, err_size, "workload must contain at least 1 process");
        return false;
    }
    if (count > SCHED_MAX_CUSTOM_PROCESSES) {
        snprintf(err_buf, err_size, "workload exceeds maximum process limit of %d", SCHED_MAX_CUSTOM_PROCESSES);
        return false;
    }

    for (int i = 0; i < count; i++) {
        const cJSON *item = cJSON_GetArrayItem(procs_array, i);
        if (!cJSON_IsObject(item)) {
            snprintf(err_buf, err_size, "process at index %d must be an object", i);
            return false;
        }

        const cJSON *j_pid = cJSON_GetObjectItemCaseSensitive(item, "pid");
        const cJSON *j_at = cJSON_GetObjectItemCaseSensitive(item, "arrival_time");
        const cJSON *j_bt = cJSON_GetObjectItemCaseSensitive(item, "burst_time");
        const cJSON *j_pr = cJSON_GetObjectItemCaseSensitive(item, "priority");

        if (j_at == NULL || !cJSON_IsNumber(j_at)) {
            snprintf(err_buf, err_size, "process at index %d: missing or invalid 'arrival_time'", i);
            return false;
        }
        if (j_bt == NULL || !cJSON_IsNumber(j_bt)) {
            snprintf(err_buf, err_size, "process at index %d: missing or invalid 'burst_time'", i);
            return false;
        }

        int pid = (j_pid != NULL && cJSON_IsNumber(j_pid)) ? (int)j_pid->valuedouble : (i + 1);
        int arrival = (int)j_at->valuedouble;
        int burst = (int)j_bt->valuedouble;
        int priority = (j_pr != NULL && cJSON_IsNumber(j_pr)) ? (int)j_pr->valuedouble : 1;

        if (pid <= 0) {
            snprintf(err_buf, err_size, "process at index %d: 'pid' must be a positive integer", i);
            return false;
        }
        if (arrival < 0) {
            snprintf(err_buf, err_size, "process at index %d: 'arrival_time' cannot be negative", i);
            return false;
        }
        if (burst <= 0) {
            snprintf(err_buf, err_size, "process at index %d: 'burst_time' must be greater than zero", i);
            return false;
        }
        if (priority < 0) {
            snprintf(err_buf, err_size, "process at index %d: 'priority' cannot be negative", i);
            return false;
        }

        /* Check for duplicate PIDs */
        for (int k = 0; k < i; k++) {
            if (out_procs[k].pid == pid) {
                snprintf(err_buf, err_size, "duplicate process pid %d", pid);
                return false;
            }
        }

        sched_process_init(&out_procs[i], pid, arrival, burst, priority);
    }

    *out_count = count;
    return true;
}

bool json_parse_scheduler_simulate(const char *json_str,
                                   sched_process_t *out_procs,
                                   int *out_count,
                                   char *out_algo,
                                   size_t algo_size,
                                   int *out_quantum,
                                   char *err_buf,
                                   size_t err_size)
{
    if (json_str == NULL || out_procs == NULL || out_count == NULL || out_algo == NULL) {
        snprintf(err_buf, err_size, "invalid arguments");
        return false;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        snprintf(err_buf, err_size, "invalid or malformed JSON payload");
        return false;
    }

    const cJSON *j_algo = cJSON_GetObjectItemCaseSensitive(root, "algorithm");
    if (j_algo == NULL || !cJSON_IsString(j_algo) || j_algo->valuestring == NULL) {
        snprintf(err_buf, err_size, "missing or invalid 'algorithm' field");
        cJSON_Delete(root);
        return false;
    }

    const char *algo_raw = j_algo->valuestring;
    if (strcasecmp(algo_raw, "fcfs") == 0) {
        strncpy(out_algo, "fcfs", algo_size - 1);
    } else if (strcasecmp(algo_raw, "sjf") == 0) {
        strncpy(out_algo, "sjf", algo_size - 1);
    } else if (strcasecmp(algo_raw, "rr") == 0 || strcasecmp(algo_raw, "roundrobin") == 0) {
        strncpy(out_algo, "rr", algo_size - 1);
    } else if (strcasecmp(algo_raw, "priority") == 0) {
        strncpy(out_algo, "priority", algo_size - 1);
    } else if (strcasecmp(algo_raw, "mlfq") == 0) {
        strncpy(out_algo, "mlfq", algo_size - 1);
    } else {
        snprintf(err_buf, err_size, "unsupported algorithm '%s'; supported: fcfs, sjf, rr, priority, mlfq", algo_raw);
        cJSON_Delete(root);
        return false;
    }
    out_algo[algo_size - 1] = '\0';

    int quantum = SCHED_DEFAULT_RR_QUANTUM;
    const cJSON *j_q = cJSON_GetObjectItemCaseSensitive(root, "quantum");
    if (j_q != NULL) {
        if (!cJSON_IsNumber(j_q) || j_q->valuedouble <= 0.0) {
            snprintf(err_buf, err_size, "quantum must be a positive integer");
            cJSON_Delete(root);
            return false;
        }
        quantum = (int)j_q->valuedouble;
    }
    if (out_quantum != NULL) {
        *out_quantum = quantum;
    }

    const cJSON *j_procs = cJSON_GetObjectItemCaseSensitive(root, "processes");
    if (j_procs == NULL) {
        snprintf(err_buf, err_size, "missing 'processes' array");
        cJSON_Delete(root);
        return false;
    }

    bool ok = parse_workload_processes(j_procs, out_procs, out_count, err_buf, err_size);
    cJSON_Delete(root);
    return ok;
}

static cJSON *serialize_single_result_node(const sched_result_t *result)
{
    cJSON *node = cJSON_CreateObject();
    if (node == NULL) return NULL;

    /* Summary metrics */
    cJSON *summary = cJSON_CreateObject();
    if (summary != NULL) {
        cJSON_AddNumberToObject(summary, "total_elapsed_time", (double)result->total_elapsed_time);
        cJSON_AddNumberToObject(summary, "total_busy_time", (double)result->total_busy_time);
        cJSON_AddNumberToObject(summary, "total_idle_time", (double)result->total_idle_time);
        cJSON_AddNumberToObject(summary, "cpu_utilization", result->cpu_utilization);
        cJSON_AddNumberToObject(summary, "avg_waiting_time", result->avg_waiting_time);
        cJSON_AddNumberToObject(summary, "avg_turnaround_time", result->avg_turnaround_time);
        cJSON_AddNumberToObject(summary, "avg_response_time", result->avg_response_time);
        cJSON_AddItemToObject(node, "summary", summary);
    }

    /* Process list */
    cJSON *procs_arr = cJSON_CreateArray();
    if (procs_arr != NULL) {
        for (int i = 0; i < result->process_count; i++) {
            const sched_process_t *p = &result->processes[i];
            cJSON *p_obj = cJSON_CreateObject();
            if (p_obj == NULL) continue;

            cJSON_AddNumberToObject(p_obj, "pid", (double)p->pid);
            cJSON_AddNumberToObject(p_obj, "arrival_time", (double)p->arrival_time);
            cJSON_AddNumberToObject(p_obj, "burst_time", (double)p->burst_time);
            cJSON_AddNumberToObject(p_obj, "priority", (double)p->priority);
            cJSON_AddNumberToObject(p_obj, "start_time", (double)p->start_time);
            cJSON_AddNumberToObject(p_obj, "completion_time", (double)p->completion_time);
            cJSON_AddNumberToObject(p_obj, "waiting_time", (double)p->waiting_time);
            cJSON_AddNumberToObject(p_obj, "turnaround_time", (double)p->turnaround_time);
            cJSON_AddNumberToObject(p_obj, "response_time", (double)p->response_time);

            cJSON_AddItemToArray(procs_arr, p_obj);
        }
        cJSON_AddItemToObject(node, "processes", procs_arr);
    }

    /* Timeline slices */
    cJSON *timeline_arr = cJSON_CreateArray();
    if (timeline_arr != NULL) {
        for (int i = 0; i < result->timeline_count; i++) {
            const sched_timeline_entry_t *t = &result->timeline[i];
            cJSON *t_obj = cJSON_CreateObject();
            if (t_obj == NULL) continue;

            cJSON_AddNumberToObject(t_obj, "pid", (double)t->pid);
            cJSON_AddBoolToObject(t_obj, "is_idle", t->pid == SCHED_PID_IDLE);
            cJSON_AddNumberToObject(t_obj, "start_time", (double)t->start_time);
            cJSON_AddNumberToObject(t_obj, "end_time", (double)t->end_time);

            cJSON_AddItemToArray(timeline_arr, t_obj);
        }
        cJSON_AddItemToObject(node, "timeline", timeline_arr);
    }

    /* Rendered Gantt Chart string */
    char *gantt_str = sched_render_gantt(result);
    if (gantt_str != NULL) {
        cJSON_AddStringToObject(node, "gantt_chart", gantt_str);
        free(gantt_str);
    }

    return node;
}

char *json_serialize_sched_result(const char *algo_name,
                                  int quantum,
                                  const sched_result_t *result)
{
    if (result == NULL) return NULL;

    cJSON *root = serialize_single_result_node(result);
    if (root == NULL) return NULL;

    cJSON_AddStringToObject(root, "algorithm", algo_name ? algo_name : "unknown");
    if (quantum > 0) {
        cJSON_AddNumberToObject(root, "quantum", (double)quantum);
    }

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

bool json_parse_scheduler_compare(const char *json_str,
                                  sched_process_t *out_procs,
                                  int *out_count,
                                  int *out_quantum,
                                  char *err_buf,
                                  size_t err_size)
{
    if (json_str == NULL || out_procs == NULL || out_count == NULL) {
        snprintf(err_buf, err_size, "invalid arguments");
        return false;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        snprintf(err_buf, err_size, "invalid or malformed JSON payload");
        return false;
    }

    int quantum = SCHED_DEFAULT_RR_QUANTUM;
    const cJSON *j_q = cJSON_GetObjectItemCaseSensitive(root, "quantum");
    if (j_q != NULL) {
        if (!cJSON_IsNumber(j_q) || j_q->valuedouble <= 0.0) {
            snprintf(err_buf, err_size, "quantum must be a positive integer");
            cJSON_Delete(root);
            return false;
        }
        quantum = (int)j_q->valuedouble;
    }
    if (out_quantum != NULL) {
        *out_quantum = quantum;
    }

    const cJSON *j_procs = cJSON_GetObjectItemCaseSensitive(root, "processes");
    if (j_procs == NULL) {
        snprintf(err_buf, err_size, "missing 'processes' array");
        cJSON_Delete(root);
        return false;
    }

    bool ok = parse_workload_processes(j_procs, out_procs, out_count, err_buf, err_size);
    cJSON_Delete(root);
    return ok;
}

char *json_serialize_sched_compare(const sched_compare_result_t *cmp)
{
    if (cmp == NULL) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;

    cJSON_AddNumberToObject(root, "quantum", (double)cmp->rr_quantum);
    cJSON_AddNumberToObject(root, "total_processes", (double)cmp->process_count);
    cJSON_AddNumberToObject(root, "total_burst_time", (double)cmp->total_burst_time);

    cJSON *results = cJSON_CreateObject();
    if (results != NULL) {
        cJSON_AddItemToObject(results, "fcfs", serialize_single_result_node(&cmp->fcfs));
        cJSON_AddItemToObject(results, "sjf", serialize_single_result_node(&cmp->sjf));
        cJSON_AddItemToObject(results, "rr", serialize_single_result_node(&cmp->rr));
        cJSON_AddItemToObject(results, "priority", serialize_single_result_node(&cmp->priority));
        cJSON_AddItemToObject(results, "mlfq", serialize_single_result_node(&cmp->mlfq));
        cJSON_AddItemToObject(root, "results", results);
    }

    char *table_str = sched_render_comparison(cmp);
    if (table_str != NULL) {
        cJSON_AddStringToObject(root, "comparison_table", table_str);
        free(table_str);
    }

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

char *json_serialize_terminal_session(const pty_session_t *session)
{
    if (session == NULL) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;

    cJSON_AddStringToObject(root, "session_id", session->session_id);
    cJSON_AddStringToObject(root, "ws_ticket", session->ws_ticket);
    time_t now = time(NULL);
    long remaining = (long)(session->ticket_expires_at - now);
    if (remaining < 0) remaining = 0;
    cJSON_AddNumberToObject(root, "expires_in", (double)remaining);
    cJSON_AddNumberToObject(root, "pid", (double)session->child_pid);
    cJSON_AddStringToObject(root, "status", pty_session_status_str(session->status));
    cJSON_AddNumberToObject(root, "cols", (double)session->cols);
    cJSON_AddNumberToObject(root, "rows", (double)session->rows);
    cJSON_AddNumberToObject(root, "created_at", (double)session->created_at);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

char *json_serialize_terminal_session_list(const pty_session_t *sessions, int count)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;

    cJSON *arr = cJSON_CreateArray();
    if (arr == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        cJSON *node = cJSON_CreateObject();
        if (node != NULL) {
            cJSON_AddStringToObject(node, "session_id", sessions[i].session_id);
            cJSON_AddNumberToObject(node, "pid", (double)sessions[i].child_pid);
            cJSON_AddStringToObject(node, "status", pty_session_status_str(sessions[i].status));
            cJSON_AddNumberToObject(node, "cols", (double)sessions[i].cols);
            cJSON_AddNumberToObject(node, "rows", (double)sessions[i].rows);
            cJSON_AddNumberToObject(node, "created_at", (double)sessions[i].created_at);
            cJSON_AddItemToArray(arr, node);
        }
    }

    cJSON_AddItemToObject(root, "sessions", arr);
    cJSON_AddNumberToObject(root, "count", (double)count);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

bool json_parse_terminal_create(const char *json_str, unsigned short *cols, unsigned short *rows)
{
    if (cols != NULL) *cols = 80;
    if (rows != NULL) *rows = 24;

    if (json_str == NULL || json_str[0] == '\0') {
        return true;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        return false;
    }

    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    cJSON *c = cJSON_GetObjectItem(root, "cols");
    if (c != NULL) {
        if (!cJSON_IsNumber(c) || c->valueint <= 0 || c->valueint > 500) {
            cJSON_Delete(root);
            return false;
        }
        if (cols != NULL) *cols = (unsigned short)c->valueint;
    }

    cJSON *r = cJSON_GetObjectItem(root, "rows");
    if (r != NULL) {
        if (!cJSON_IsNumber(r) || r->valueint <= 0 || r->valueint > 200) {
            cJSON_Delete(root);
            return false;
        }
        if (rows != NULL) *rows = (unsigned short)r->valueint;
    }

    cJSON_Delete(root);
    return true;
}

