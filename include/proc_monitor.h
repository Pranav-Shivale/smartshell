/*
 * proc_monitor.h — Linux /proc process monitoring subsystem for SmartShell.
 */

#ifndef SMARTSHELL_PROC_MONITOR_H
#define SMARTSHELL_PROC_MONITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#include "parser.h"

#define PROC_NAME_MAX 256
#define PROC_DEFAULT_SAMPLE_MS 100

/* Process information collected from /proc/<pid>/stat and /proc/<pid>/status */
typedef struct {
    pid_t         pid;
    char          name[PROC_NAME_MAX];
    char          state_char;
    const char   *state_str;
    pid_t         ppid;
    pid_t         pgid;
    pid_t         sid;
    uid_t         uid;
    long          num_threads;
    unsigned long vsize_bytes;
    unsigned long rss_bytes;
    unsigned long utime_ticks;
    unsigned long stime_ticks;
    double        cpu_percent;
} proc_info_t;

/* System CPU counters from /proc/stat */
typedef struct {
    unsigned long long user;
    unsigned long long nice;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long irq;
    unsigned long long softirq;
    unsigned long long steal;
    unsigned long long total;
    unsigned long long idle_all;
} sys_cpu_t;

/* System memory information from /proc/meminfo */
typedef struct {
    unsigned long long mem_total_kb;
    unsigned long long mem_free_kb;
    unsigned long long mem_available_kb;
    unsigned long long buffers_kb;
    unsigned long long cached_kb;
    unsigned long long swap_total_kb;
    unsigned long long swap_free_kb;
} sys_mem_t;

/* Pure parser functions (testable with fixture strings) */
bool proc_parse_pid_stat(const char *stat_str, proc_info_t *info);
bool proc_parse_pid_status(const char *status_str, proc_info_t *info);
bool proc_parse_proc_stat(const char *stat_str, sys_cpu_t *cpu);
bool proc_parse_meminfo(const char *meminfo_str, sys_mem_t *mem);
double proc_calc_cpu_utilization(const sys_cpu_t *s1, const sys_cpu_t *s2);
const char *proc_state_to_string(char state_char);
void proc_format_bytes(unsigned long long bytes, char *buf, size_t bufsize);
void proc_format_kb(unsigned long long kb, char *buf, size_t bufsize);

/* Live Linux /proc reader functions */
bool proc_read_pid_info(pid_t pid, proc_info_t *info);
bool proc_read_sys_cpu(sys_cpu_t *cpu);
bool proc_read_sys_mem(sys_mem_t *mem);
double proc_sample_sys_cpu(unsigned int interval_ms);
bool proc_sample_pid_info(pid_t pid, unsigned int interval_ms, proc_info_t *info);

/* User-facing monitor command implementations */
int proc_monitor_display_summary(void);
int proc_monitor_display_pid(pid_t pid);
int builtin_monitor(const command_t *cmd);

/* Sorting modes for process monitor */
typedef enum {
    PROC_SORT_CPU = 0,
    PROC_SORT_MEM = 1
} proc_sort_mode_t;

/* Extended process monitor functions */
int proc_monitor_display_sorted(proc_sort_mode_t sort_mode);
int proc_monitor_watch(proc_sort_mode_t sort_mode, pid_t target_pid);

/* Process Tree node and hierarchy structure */
typedef struct proc_tree_node {
    pid_t                   pid;
    pid_t                   ppid;
    char                    name[PROC_NAME_MAX];
    char                    state_char;
    struct proc_tree_node **children;
    size_t                  num_children;
    size_t                  children_cap;
    bool                    visited;
} proc_tree_node_t;

typedef struct {
    proc_tree_node_t **nodes;
    size_t             count;
    size_t             capacity;
} proc_tree_t;

/* Process tree functions */
proc_tree_t *proc_tree_collect(void);
void proc_tree_free(proc_tree_t *tree);
proc_tree_node_t *proc_tree_find_node(const proc_tree_t *tree, pid_t pid);
int proc_tree_display(pid_t target_pid);
int builtin_process_tree(const command_t *cmd);

/* System information explorer functions */
int proc_display_sysinfo(void);
int builtin_sysinfo(const command_t *cmd);

#endif /* SMARTSHELL_PROC_MONITOR_H */
