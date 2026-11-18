/*
 * json_api.h — JSON serialization and deserialization module for SmartShell web server.
 *
 * Exposes process monitoring, system information, process tree, and CPU scheduling
 * data structures as clean JSON payloads using cJSON.
 */

#ifndef SMARTSHELL_SERVER_JSON_API_H
#define SMARTSHELL_SERVER_JSON_API_H

#include "proc_monitor.h"
#include "scheduler.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * Serialize system information into a newly allocated JSON string.
 * Caller must free() the returned string. Returns NULL on failure.
 */
char *json_serialize_sysinfo(const proc_sysinfo_t *info);

/*
 * Serialize real-time CPU and memory metrics into a newly allocated JSON string.
 * Caller must free() the returned string. Returns NULL on failure.
 */
char *json_serialize_metrics(double cpu_util, const sys_mem_t *mem);

/*
 * Serialize process snapshot into a newly allocated JSON string.
 * Supports limit (0 for unlimited).
 * Caller must free() the returned string. Returns NULL on failure.
 */
char *json_serialize_processes(const proc_snapshot_t *snap, proc_sort_mode_t sort_mode, size_t limit);

/*
 * Serialize single process details into a newly allocated JSON string.
 * Caller must free() the returned string. Returns NULL on failure.
 */
char *json_serialize_pid_info(const proc_info_t *info);

/*
 * Serialize process tree hierarchy into a newly allocated JSON string.
 * Caller must free() the returned string. Returns NULL on failure.
 */
char *json_serialize_proc_tree(const proc_tree_t *tree);

/*
 * Parse scheduler simulation request JSON.
 *
 * Parameters:
 *   json_str    — JSON payload string
 *   out_procs   — buffer to store parsed processes (at least SCHED_MAX_CUSTOM_PROCESSES)
 *   out_count   — pointer to store number of processes parsed
 *   out_algo    — buffer to store algorithm name ("fcfs", "sjf", "rr", "priority", "mlfq")
 *   algo_size   — size of out_algo buffer
 *   out_quantum — pointer to store RR quantum
 *   err_buf     — buffer to store error message if parsing fails
 *   err_size    — size of err_buf
 *
 * Returns true on success, false on validation error.
 */
bool json_parse_scheduler_simulate(const char *json_str,
                                   sched_process_t *out_procs,
                                   int *out_count,
                                   char *out_algo,
                                   size_t algo_size,
                                   int *out_quantum,
                                   char *err_buf,
                                   size_t err_size);

/*
 * Serialize single algorithm simulation result into a newly allocated JSON string.
 * Caller must free() the returned string. Returns NULL on failure.
 */
char *json_serialize_sched_result(const char *algo_name,
                                  int quantum,
                                  const sched_result_t *result);

/*
 * Parse scheduler comparison request JSON.
 *
 * Parameters:
 *   json_str    — JSON payload string
 *   out_procs   — buffer to store parsed processes
 *   out_count   — pointer to store number of processes parsed
 *   out_quantum — pointer to store RR quantum
 *   err_buf     — buffer to store error message
 *   err_size    — size of err_buf
 *
 * Returns true on success, false on validation error.
 */
bool json_parse_scheduler_compare(const char *json_str,
                                  sched_process_t *out_procs,
                                  int *out_count,
                                  int *out_quantum,
                                  char *err_buf,
                                  size_t err_size);

/*
 * Serialize algorithm comparison result into a newly allocated JSON string.
 * Caller must free() the returned string. Returns NULL on failure.
 */
char *json_serialize_sched_compare(const sched_compare_result_t *cmp);

#include "server/pty_session.h"

/*
 * Serialize a single PTY terminal session into a newly allocated JSON string.
 * Caller must free() the returned string. Returns NULL on failure.
 */
char *json_serialize_terminal_session(const pty_session_t *session);

/*
 * Serialize a list of PTY terminal sessions into a newly allocated JSON string.
 * Caller must free() the returned string. Returns NULL on failure.
 */
char *json_serialize_terminal_session_list(const pty_session_t *sessions, int count);

/*
 * Parse optional cols and rows from terminal session creation request body.
 * If body is empty or NULL, defaults to 80x24.
 * Returns true on success, false if JSON is malformed.
 */
bool json_parse_terminal_create(const char *json_str, unsigned short *cols, unsigned short *rows);

#endif /* SMARTSHELL_SERVER_JSON_API_H */

