/*
 * proc_monitor.c — Linux /proc process and system monitoring implementation.
 */

#include "proc_monitor.h"
#include "job.h"
#include "utils.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

const char *proc_state_to_string(char state_char)
{
    switch (state_char) {
        case 'R': return "Running";
        case 'S': return "Sleeping";
        case 'D': return "Disk Sleep";
        case 'Z': return "Zombie";
        case 'T': return "Stopped";
        case 't': return "Tracing Stop";
        case 'X':
        case 'x': return "Dead";
        case 'K': return "Wakekill";
        case 'W': return "Waking";
        case 'P': return "Parked";
        case 'I': return "Idle";
        default:  return "Unknown";
    }
}

void proc_format_bytes(unsigned long long bytes, char *buf, size_t bufsize)
{
    if (buf == NULL || bufsize == 0) {
        return;
    }
    if (bytes < 1024ULL) {
        snprintf(buf, bufsize, "%llu B", bytes);
    } else if (bytes < 1024ULL * 1024ULL) {
        snprintf(buf, bufsize, "%.1f KB", (double)bytes / 1024.0);
    } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        snprintf(buf, bufsize, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else {
        snprintf(buf, bufsize, "%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    }
}

void proc_format_kb(unsigned long long kb, char *buf, size_t bufsize)
{
    proc_format_bytes(kb * 1024ULL, buf, bufsize);
}

/*
 * Parse /proc/<pid>/stat.
 * Format: pid (comm) state ppid pgrp session tty_nr tpgid flags minflt ... utime stime ... vsize rss ...
 */
bool proc_parse_pid_stat(const char *stat_str, proc_info_t *info)
{
    if (stat_str == NULL || info == NULL) {
        return false;
    }

    const char *open_paren = strchr(stat_str, '(');
    const char *close_paren = strrchr(stat_str, ')');

    if (open_paren == NULL || close_paren == NULL || close_paren <= open_paren) {
        return false;
    }

    /* Parse PID before '(' */
    char *endptr = NULL;
    long pid_val = strtol(stat_str, &endptr, 10);
    if (endptr == stat_str || endptr > open_paren || pid_val <= 0) {
        return false;
    }
    info->pid = (pid_t)pid_val;

    /* Extract process name enclosed in '(' and ')' */
    size_t name_len = (size_t)(close_paren - (open_paren + 1));
    if (name_len >= sizeof(info->name)) {
        name_len = sizeof(info->name) - 1;
    }
    memcpy(info->name, open_paren + 1, name_len);
    info->name[name_len] = '\0';

    /* Parse space-delimited fields after ')' */
    const char *rest = close_paren + 1;
    char state = '\0';
    int ppid = 0, pgrp = 0, sid = 0;
    unsigned long utime = 0, stime = 0, vsize = 0;
    long num_threads = 0, rss_pages = 0;

    int matched = sscanf(rest,
        " %c %d %d %d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d %*d %*d %ld %*d %*u %lu %ld",
        &state, &ppid, &pgrp, &sid, &utime, &stime, &num_threads, &vsize, &rss_pages);

    if (matched < 9) {
        return false;
    }

    info->state_char = state;
    info->state_str = proc_state_to_string(state);
    info->ppid = (pid_t)ppid;
    info->pgid = (pid_t)pgrp;
    info->sid = (pid_t)sid;
    info->utime_ticks = utime;
    info->stime_ticks = stime;
    info->num_threads = num_threads;
    info->vsize_bytes = vsize;

    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        page_size = 4096;
    }
    info->rss_bytes = (rss_pages > 0) ? (unsigned long)(rss_pages * page_size) : 0;

    return true;
}

/*
 * Parse /proc/<pid>/status for Uid, Threads, VmSize, VmRSS.
 */
bool proc_parse_pid_status(const char *status_str, proc_info_t *info)
{
    if (status_str == NULL || info == NULL) {
        return false;
    }

    const char *p = status_str;
    while (*p != '\0') {
        if (strncmp(p, "Uid:", 4) == 0) {
            unsigned int ruid = 0;
            if (sscanf(p + 4, "%u", &ruid) == 1) {
                info->uid = (uid_t)ruid;
            }
        } else if (strncmp(p, "Threads:", 8) == 0) {
            long threads = 0;
            if (sscanf(p + 8, "%ld", &threads) == 1 && threads > 0) {
                info->num_threads = threads;
            }
        } else if (strncmp(p, "VmSize:", 7) == 0) {
            unsigned long vmsize_kb = 0;
            if (sscanf(p + 7, "%lu", &vmsize_kb) == 1) {
                info->vsize_bytes = vmsize_kb * 1024UL;
            }
        } else if (strncmp(p, "VmRSS:", 6) == 0) {
            unsigned long vmrss_kb = 0;
            if (sscanf(p + 6, "%lu", &vmrss_kb) == 1) {
                info->rss_bytes = vmrss_kb * 1024UL;
            }
        }

        const char *next = strchr(p, '\n');
        if (next == NULL) {
            break;
        }
        p = next + 1;
    }

    return true;
}

/*
 * Parse /proc/stat CPU counters.
 */
bool proc_parse_proc_stat(const char *stat_str, sys_cpu_t *cpu)
{
    if (stat_str == NULL || cpu == NULL) {
        return false;
    }

    memset(cpu, 0, sizeof(*cpu));

    const char *p = stat_str;
    while (*p != '\0') {
        if (p[0] == 'c' && p[1] == 'p' && p[2] == 'u' && (p[3] == ' ' || p[3] == '\t')) {
            break;
        }
        const char *next = strchr(p, '\n');
        if (next == NULL) {
            return false;
        }
        p = next + 1;
    }

    if (*p == '\0') {
        return false;
    }

    p += 3;
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    int matched = sscanf(p,
        "%llu %llu %llu %llu %llu %llu %llu %llu",
        &cpu->user, &cpu->nice, &cpu->system, &cpu->idle,
        &cpu->iowait, &cpu->irq, &cpu->softirq, &cpu->steal);

    if (matched < 4) {
        return false;
    }

    cpu->idle_all = cpu->idle + cpu->iowait;
    cpu->total = cpu->user + cpu->nice + cpu->system + cpu->idle +
                 cpu->iowait + cpu->irq + cpu->softirq + cpu->steal;

    return true;
}

/*
 * Parse /proc/meminfo.
 */
bool proc_parse_meminfo(const char *meminfo_str, sys_mem_t *mem)
{
    if (meminfo_str == NULL || mem == NULL) {
        return false;
    }

    memset(mem, 0, sizeof(*mem));
    bool found_total = false;

    const char *p = meminfo_str;
    while (*p != '\0') {
        if (strncmp(p, "MemTotal:", 9) == 0) {
            if (sscanf(p + 9, "%llu", &mem->mem_total_kb) == 1) {
                found_total = true;
            }
        } else if (strncmp(p, "MemFree:", 8) == 0) {
            sscanf(p + 8, "%llu", &mem->mem_free_kb);
        } else if (strncmp(p, "MemAvailable:", 13) == 0) {
            sscanf(p + 13, "%llu", &mem->mem_available_kb);
        } else if (strncmp(p, "Buffers:", 8) == 0) {
            sscanf(p + 8, "%llu", &mem->buffers_kb);
        } else if (strncmp(p, "Cached:", 7) == 0) {
            sscanf(p + 7, "%llu", &mem->cached_kb);
        } else if (strncmp(p, "SwapTotal:", 10) == 0) {
            sscanf(p + 10, "%llu", &mem->swap_total_kb);
        } else if (strncmp(p, "SwapFree:", 9) == 0) {
            sscanf(p + 9, "%llu", &mem->swap_free_kb);
        }

        const char *next = strchr(p, '\n');
        if (next == NULL) {
            break;
        }
        p = next + 1;
    }

    return found_total;
}

/*
 * Calculate CPU utilization percentage from two snapshots:
 *   total_delta = total2 - total1
 *   idle_delta  = idle2 - idle1
 *   utilization = 100 * (total_delta - idle_delta) / total_delta
 */
double proc_calc_cpu_utilization(const sys_cpu_t *s1, const sys_cpu_t *s2)
{
    if (s1 == NULL || s2 == NULL) {
        return 0.0;
    }

    if (s2->total <= s1->total) {
        return 0.0;
    }

    unsigned long long total_delta = s2->total - s1->total;
    unsigned long long idle_delta = (s2->idle_all >= s1->idle_all) ?
                                    (s2->idle_all - s1->idle_all) : 0;

    if (idle_delta > total_delta) {
        idle_delta = total_delta;
    }

    double util = 100.0 * (double)(total_delta - idle_delta) / (double)total_delta;
    if (util < 0.0) util = 0.0;
    if (util > 100.0) util = 100.0;
    return util;
}

/*
 * Safely read a small text file into a bounded buffer.
 */
static bool read_file_contents(const char *path, char *buf, size_t bufsize)
{
    if (path == NULL || buf == NULL || bufsize == 0) {
        return false;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    ssize_t n = read(fd, buf, bufsize - 1);
    close(fd);

    if (n <= 0) {
        return false;
    }

    buf[n] = '\0';
    return true;
}

bool proc_read_pid_info(pid_t pid, proc_info_t *info)
{
    if (pid <= 0 || info == NULL) {
        return false;
    }

    memset(info, 0, sizeof(*info));
    info->pid = pid;

    char stat_path[64];
    snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", (int)pid);

    char stat_buf[2048];
    if (!read_file_contents(stat_path, stat_buf, sizeof(stat_buf))) {
        return false;
    }

    if (!proc_parse_pid_stat(stat_buf, info)) {
        return false;
    }

    char status_path[64];
    snprintf(status_path, sizeof(status_path), "/proc/%d/status", (int)pid);

    char status_buf[4096];
    if (read_file_contents(status_path, status_buf, sizeof(status_buf))) {
        proc_parse_pid_status(status_buf, info);
    }

    return true;
}

bool proc_read_sys_cpu(sys_cpu_t *cpu)
{
    char buf[1024];
    if (!read_file_contents("/proc/stat", buf, sizeof(buf))) {
        return false;
    }
    return proc_parse_proc_stat(buf, cpu);
}

bool proc_read_sys_mem(sys_mem_t *mem)
{
    char buf[2048];
    if (!read_file_contents("/proc/meminfo", buf, sizeof(buf))) {
        return false;
    }
    return proc_parse_meminfo(buf, mem);
}

double proc_sample_sys_cpu(unsigned int interval_ms)
{
    sys_cpu_t s1, s2;
    if (!proc_read_sys_cpu(&s1)) {
        return 0.0;
    }

    struct timespec req;
    req.tv_sec = interval_ms / 1000;
    req.tv_nsec = (long)(interval_ms % 1000) * 1000000L;
    nanosleep(&req, NULL);

    if (!proc_read_sys_cpu(&s2)) {
        return 0.0;
    }

    return proc_calc_cpu_utilization(&s1, &s2);
}

bool proc_sample_pid_info(pid_t pid, unsigned int interval_ms, proc_info_t *info)
{
    if (pid <= 0 || info == NULL) {
        return false;
    }

    proc_info_t i1, i2;
    struct timespec ts1, ts2;

    clock_gettime(CLOCK_MONOTONIC, &ts1);
    if (!proc_read_pid_info(pid, &i1)) {
        return false;
    }

    struct timespec req;
    req.tv_sec = interval_ms / 1000;
    req.tv_nsec = (long)(interval_ms % 1000) * 1000000L;
    nanosleep(&req, NULL);

    clock_gettime(CLOCK_MONOTONIC, &ts2);
    if (!proc_read_pid_info(pid, &i2)) {
        return false;
    }

    *info = i2;

    long clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0) {
        clk_tck = 100;
    }

    unsigned long ticks1 = i1.utime_ticks + i1.stime_ticks;
    unsigned long ticks2 = i2.utime_ticks + i2.stime_ticks;
    unsigned long delta_ticks = (ticks2 >= ticks1) ? (ticks2 - ticks1) : 0;

    double proc_seconds = (double)delta_ticks / (double)clk_tck;
    double wall_seconds = (double)(ts2.tv_sec - ts1.tv_sec) +
                          (double)(ts2.tv_nsec - ts1.tv_nsec) / 1e9;

    if (wall_seconds > 0.0001) {
        info->cpu_percent = (proc_seconds / wall_seconds) * 100.0;
    } else {
        info->cpu_percent = 0.0;
    }

    if (info->cpu_percent < 0.0) {
        info->cpu_percent = 0.0;
    }

    return true;
}

int proc_monitor_display_summary(void)
{
    /* 1. Read memory */
    sys_mem_t mem;
    bool has_mem = proc_read_sys_mem(&mem);

    /* 2. Sample CPU utilization */
    double cpu_util = proc_sample_sys_cpu(PROC_DEFAULT_SAMPLE_MS);

    printf("--- System Overview ---\n");
    printf("CPU Utilization:  %.1f%%\n", cpu_util);

    if (has_mem && mem.mem_total_kb > 0) {
        char total_buf[32], avail_buf[32], used_buf[32], free_buf[32];
        unsigned long long used_kb = (mem.mem_total_kb >= mem.mem_available_kb) ?
                                     (mem.mem_total_kb - mem.mem_available_kb) : 0;
        double used_pct = 100.0 * (double)used_kb / (double)mem.mem_total_kb;

        proc_format_kb(mem.mem_total_kb, total_buf, sizeof(total_buf));
        proc_format_kb(used_kb, used_buf, sizeof(used_buf));
        proc_format_kb(mem.mem_free_kb, free_buf, sizeof(free_buf));
        proc_format_kb(mem.mem_available_kb, avail_buf, sizeof(avail_buf));

        printf("Memory:           Used: %s / %s (%.1f%%) | Free: %s | Available: %s\n",
               used_buf, total_buf, used_pct, free_buf, avail_buf);

        if (mem.swap_total_kb > 0) {
            char swap_total_buf[32], swap_free_buf[32], swap_used_buf[32];
            unsigned long long swap_used_kb = (mem.swap_total_kb >= mem.swap_free_kb) ?
                                              (mem.swap_total_kb - mem.swap_free_kb) : 0;
            proc_format_kb(mem.swap_total_kb, swap_total_buf, sizeof(swap_total_buf));
            proc_format_kb(swap_used_kb, swap_used_buf, sizeof(swap_used_buf));
            proc_format_kb(mem.swap_free_kb, swap_free_buf, sizeof(swap_free_buf));
            printf("Swap:             Used: %s / %s | Free: %s\n",
                   swap_used_buf, swap_total_buf, swap_free_buf);
        }
    }

    printf("\n--- SmartShell Managed Processes ---\n");
    printf("%-5s %-7s %-12s %-7s %-10s %s\n",
           "JID", "PID", "STATE", "PGID", "MEM", "COMMAND");

    int active_procs = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        job_t *job = job_get_by_index(i);
        if (job == NULL || job->state == JOB_COMPLETED) {
            continue;
        }

        for (int p = 0; p < job->process_count; p++) {
            pid_t pid = job->processes[p].pid;
            proc_info_t info;
            char mem_buf[32];

            if (proc_read_pid_info(pid, &info)) {
                proc_format_bytes(info.rss_bytes, mem_buf, sizeof(mem_buf));
                printf("[%d]   %-7d %-12s %-7d %-10s %s\n",
                       job->jid,
                       (int)info.pid,
                       info.state_str,
                       (int)info.pgid,
                       mem_buf,
                       info.name);
            } else {
                printf("[%d]   %-7d %-12s %-7d %-10s %s\n",
                       job->jid,
                       (int)pid,
                       "Exited",
                       (int)job->pgid,
                       "N/A",
                       job->command_line);
            }
            active_procs++;
        }
    }

    if (active_procs == 0) {
        printf("No active jobs in shell. (Use 'monitor <pid>' to inspect any OS process)\n");
    }

    return 0;
}

int proc_monitor_display_pid(pid_t pid)
{
    if (pid <= 0) {
        fprintf(stderr, "smartshell: monitor: %d: invalid process ID\n", (int)pid);
        return 1;
    }

    proc_info_t info;
    if (!proc_sample_pid_info(pid, PROC_DEFAULT_SAMPLE_MS, &info)) {
        fprintf(stderr, "smartshell: monitor: process %d not found\n", (int)pid);
        return 1;
    }

    char vsize_buf[32], rss_buf[32];
    proc_format_bytes(info.vsize_bytes, vsize_buf, sizeof(vsize_buf));
    proc_format_bytes(info.rss_bytes, rss_buf, sizeof(rss_buf));

    long clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0) {
        clk_tck = 100;
    }
    double utime_sec = (double)info.utime_ticks / (double)clk_tck;
    double stime_sec = (double)info.stime_ticks / (double)clk_tck;

    printf("Process Information (PID %d):\n", (int)info.pid);
    printf("  Command:            %s\n", info.name);
    printf("  State:              %c (%s)\n", info.state_char, info.state_str);
    printf("  Parent PID (PPID):  %d\n", (int)info.ppid);
    printf("  Process Group ID:   %d\n", (int)info.pgid);
    printf("  Session ID:         %d\n", (int)info.sid);
    printf("  User ID (UID):      %u\n", (unsigned int)info.uid);
    printf("  Threads:            %ld\n", info.num_threads);
    printf("  Virtual Memory:     %s (%lu bytes)\n", vsize_buf, info.vsize_bytes);
    printf("  Resident Memory:    %s (%lu bytes)\n", rss_buf, info.rss_bytes);
    printf("  User CPU Time:      %lu ticks (%.2fs)\n", info.utime_ticks, utime_sec);
    printf("  System CPU Time:    %lu ticks (%.2fs)\n", info.stime_ticks, stime_sec);
    printf("  CPU Utilization:    %.1f%% (sampled over %dms)\n", info.cpu_percent, PROC_DEFAULT_SAMPLE_MS);

    return 0;
}

/* ========================================================================= */
/* Process Tree Implementation                                               */
/* ========================================================================= */

static int compare_nodes_by_pid(const void *a, const void *b)
{
    const proc_tree_node_t *na = *(const proc_tree_node_t * const *)a;
    const proc_tree_node_t *nb = *(const proc_tree_node_t * const *)b;
    if (na->pid < nb->pid) return -1;
    if (na->pid > nb->pid) return 1;
    return 0;
}

proc_tree_node_t *proc_tree_find_node(const proc_tree_t *tree, pid_t pid)
{
    if (tree == NULL || tree->nodes == NULL || tree->count == 0 || pid <= 0) {
        return NULL;
    }

    size_t low = 0;
    size_t high = tree->count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (tree->nodes[mid]->pid == pid) {
            return tree->nodes[mid];
        }
        if (tree->nodes[mid]->pid < pid) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return NULL;
}

void proc_tree_free(proc_tree_t *tree)
{
    if (tree == NULL) {
        return;
    }
    if (tree->nodes != NULL) {
        for (size_t i = 0; i < tree->count; i++) {
            if (tree->nodes[i] != NULL) {
                free(tree->nodes[i]->children);
                free(tree->nodes[i]);
            }
        }
        free(tree->nodes);
    }
    free(tree);
}

proc_tree_t *proc_tree_collect(void)
{
    DIR *dir = opendir("/proc");
    if (dir == NULL) {
        return NULL;
    }

    proc_tree_t *tree = xmalloc(sizeof(proc_tree_t));
    tree->count = 0;
    tree->capacity = 128;
    tree->nodes = xmalloc(tree->capacity * sizeof(proc_tree_node_t *));

    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        if (!isdigit((unsigned char)ent->d_name[0])) {
            continue;
        }

        bool all_digits = true;
        for (const char *c = ent->d_name; *c != '\0'; c++) {
            if (!isdigit((unsigned char)*c)) {
                all_digits = false;
                break;
            }
        }
        if (!all_digits) {
            continue;
        }

        long pid_val = strtol(ent->d_name, NULL, 10);
        if (pid_val <= 0 || pid_val > 99999999) {
            continue;
        }
        pid_t pid = (pid_t)pid_val;

        char stat_path[64];
        snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", (int)pid);
        char stat_buf[2048];
        if (!read_file_contents(stat_path, stat_buf, sizeof(stat_buf))) {
            continue;
        }

        proc_info_t info;
        if (!proc_parse_pid_stat(stat_buf, &info)) {
            continue;
        }

        proc_tree_node_t *node = xmalloc(sizeof(proc_tree_node_t));
        node->pid = info.pid;
        node->ppid = info.ppid;
        strncpy(node->name, info.name, sizeof(node->name) - 1);
        node->name[sizeof(node->name) - 1] = '\0';
        node->state_char = info.state_char;
        node->children = NULL;
        node->num_children = 0;
        node->children_cap = 0;
        node->visited = false;

        if (tree->count == tree->capacity) {
            tree->capacity *= 2;
            tree->nodes = xrealloc(tree->nodes, tree->capacity * sizeof(proc_tree_node_t *));
        }
        tree->nodes[tree->count++] = node;
    }
    closedir(dir);

    if (tree->count == 0) {
        return tree;
    }

    /* Sort nodes by PID ascending for fast binary search and deterministic hierarchy */
    qsort(tree->nodes, tree->count, sizeof(proc_tree_node_t *), compare_nodes_by_pid);

    /* Connect parent-child links */
    for (size_t i = 0; i < tree->count; i++) {
        proc_tree_node_t *child = tree->nodes[i];
        if (child->ppid > 0 && child->ppid != child->pid) {
            proc_tree_node_t *parent = proc_tree_find_node(tree, child->ppid);
            if (parent != NULL) {
                if (parent->num_children == parent->children_cap) {
                    size_t new_cap = (parent->children_cap == 0) ? 4 : parent->children_cap * 2;
                    parent->children = xrealloc(parent->children, new_cap * sizeof(proc_tree_node_t *));
                    parent->children_cap = new_cap;
                }
                parent->children[parent->num_children++] = child;
            }
        }
    }

    return tree;
}

static void proc_tree_print_node(proc_tree_node_t *node, const char *prefix, bool is_last, bool is_root)
{
    if (node == NULL) {
        return;
    }

    if (node->visited) {
        printf("%s%s%d %s [%c] (cycle detected)\n",
               prefix,
               is_root ? "" : (is_last ? "\\-- " : "|-- "),
               (int)node->pid, node->name, node->state_char);
        return;
    }
    node->visited = true;

    if (is_root) {
        printf("%d %s [%c]\n", (int)node->pid, node->name, node->state_char);
    } else {
        printf("%s%s%d %s [%c]\n",
               prefix,
               is_last ? "\\-- " : "|-- ",
               (int)node->pid, node->name, node->state_char);
    }

    char next_prefix[1024];
    if (is_root) {
        next_prefix[0] = '\0';
    } else {
        snprintf(next_prefix, sizeof(next_prefix), "%s%s",
                 prefix, is_last ? "    " : "|   ");
    }

    for (size_t i = 0; i < node->num_children; i++) {
        bool last_child = (i + 1 == node->num_children);
        proc_tree_print_node(node->children[i], next_prefix, last_child, false);
    }
}

int proc_tree_display(pid_t target_pid)
{
    if (target_pid < 0) {
        fprintf(stderr, "smartshell: process-tree: %d: invalid process ID\n", (int)target_pid);
        return 1;
    }

    proc_tree_t *tree = proc_tree_collect();
    if (tree == NULL) {
        fprintf(stderr, "smartshell: process-tree: cannot access /proc\n");
        return 1;
    }

    if (tree->count == 0) {
        fprintf(stderr, "smartshell: process-tree: no processes found\n");
        proc_tree_free(tree);
        return 1;
    }

    if (target_pid > 0) {
        proc_tree_node_t *target = proc_tree_find_node(tree, target_pid);
        if (target == NULL) {
            fprintf(stderr, "smartshell: process-tree: process %d not found\n", (int)target_pid);
            proc_tree_free(tree);
            return 1;
        }
        proc_tree_print_node(target, "", true, true);
        proc_tree_free(tree);
        return 0;
    }

    /* Target PID <= 0: print entire forest */
    /* 1. Print PID 1 if present */
    proc_tree_node_t *init_node = proc_tree_find_node(tree, 1);
    if (init_node != NULL) {
        proc_tree_print_node(init_node, "", true, true);
    }

    /* 2. Print any remaining root nodes (PPID <= 0 or parent not found) */
    for (size_t i = 0; i < tree->count; i++) {
        proc_tree_node_t *n = tree->nodes[i];
        if (n->visited) continue;

        bool is_root = (n->ppid <= 0 || proc_tree_find_node(tree, n->ppid) == NULL);
        if (is_root) {
            proc_tree_print_node(n, "", true, true);
        }
    }

    /* 3. Cycle fallback for any unvisited nodes */
    for (size_t i = 0; i < tree->count; i++) {
        if (!tree->nodes[i]->visited) {
            proc_tree_print_node(tree->nodes[i], "", true, true);
        }
    }

    proc_tree_free(tree);
    return 0;
}

int builtin_process_tree(const command_t *cmd)
{
    if (cmd == NULL) {
        return 1;
    }

    if (cmd->argc == 1) {
        return proc_tree_display(0);
    }

    if (cmd->argc == 2) {
        char *endptr = NULL;
        long val = strtol(cmd->argv[1], &endptr, 10);
        if (*endptr != '\0' || val <= 0) {
            fprintf(stderr, "smartshell: process-tree: %s: invalid process ID\n", cmd->argv[1]);
            return 1;
        }
        return proc_tree_display((pid_t)val);
    }

    fprintf(stderr, "smartshell: process-tree: too many arguments\n");
    return 1;
}

/* ========================================================================= */
/* Live Process Monitor & Sorting Implementation                             */
/* ========================================================================= */

typedef struct {
    pid_t         pid;
    char          name[PROC_NAME_MAX];
    char          state_char;
    const char   *state_str;
    pid_t         ppid;
    pid_t         pgid;
    long          num_threads;
    unsigned long vsize_bytes;
    unsigned long rss_bytes;
    unsigned long utime_ticks;
    unsigned long stime_ticks;
    double        cpu_percent;
} proc_snap_entry_t;

typedef struct {
    proc_snap_entry_t *entries;
    size_t             count;
    size_t             capacity;
    struct timespec    timestamp;
    sys_cpu_t          sys_cpu;
} proc_snapshot_t;

static proc_snapshot_t *proc_snapshot_create(void)
{
    proc_snapshot_t *snap = xmalloc(sizeof(proc_snapshot_t));
    snap->count = 0;
    snap->capacity = 128;
    snap->entries = xmalloc(snap->capacity * sizeof(proc_snap_entry_t));
    clock_gettime(CLOCK_MONOTONIC, &snap->timestamp);
    memset(&snap->sys_cpu, 0, sizeof(snap->sys_cpu));
    proc_read_sys_cpu(&snap->sys_cpu);
    return snap;
}

static void proc_snapshot_free(proc_snapshot_t *snap)
{
    if (snap == NULL) return;
    free(snap->entries);
    free(snap);
}

static void proc_snapshot_populate(proc_snapshot_t *snap)
{
    DIR *dir = opendir("/proc");
    if (dir == NULL) return;

    snap->count = 0;
    clock_gettime(CLOCK_MONOTONIC, &snap->timestamp);
    proc_read_sys_cpu(&snap->sys_cpu);

    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        if (!isdigit((unsigned char)ent->d_name[0])) continue;

        bool all_digits = true;
        for (const char *c = ent->d_name; *c != '\0'; c++) {
            if (!isdigit((unsigned char)*c)) {
                all_digits = false;
                break;
            }
        }
        if (!all_digits) continue;

        long pid_val = strtol(ent->d_name, NULL, 10);
        if (pid_val <= 0 || pid_val > 99999999) continue;
        pid_t pid = (pid_t)pid_val;

        char stat_path[64];
        snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", (int)pid);
        char stat_buf[2048];
        if (!read_file_contents(stat_path, stat_buf, sizeof(stat_buf))) {
            continue;
        }

        proc_info_t info;
        if (!proc_parse_pid_stat(stat_buf, &info)) {
            continue;
        }

        if (snap->count == snap->capacity) {
            snap->capacity *= 2;
            snap->entries = xrealloc(snap->entries, snap->capacity * sizeof(proc_snap_entry_t));
        }

        proc_snap_entry_t *e = &snap->entries[snap->count++];
        e->pid = info.pid;
        strncpy(e->name, info.name, sizeof(e->name) - 1);
        e->name[sizeof(e->name) - 1] = '\0';
        e->state_char = info.state_char;
        e->state_str = info.state_str;
        e->ppid = info.ppid;
        e->pgid = info.pgid;
        e->num_threads = info.num_threads;
        e->vsize_bytes = info.vsize_bytes;
        e->rss_bytes = info.rss_bytes;
        e->utime_ticks = info.utime_ticks;
        e->stime_ticks = info.stime_ticks;
        e->cpu_percent = 0.0;
    }
    closedir(dir);
}

static void proc_snapshot_calc_deltas(const proc_snapshot_t *prev, proc_snapshot_t *curr)
{
    if (curr == NULL) return;

    long clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0) clk_tck = 100;

    double wall_seconds = 1.0;
    if (prev != NULL) {
        wall_seconds = (double)(curr->timestamp.tv_sec - prev->timestamp.tv_sec) +
                       (double)(curr->timestamp.tv_nsec - prev->timestamp.tv_nsec) / 1e9;
    }
    if (wall_seconds < 0.001) wall_seconds = 0.001;

    for (size_t i = 0; i < curr->count; i++) {
        proc_snap_entry_t *e = &curr->entries[i];
        e->cpu_percent = 0.0;

        if (prev != NULL) {
            for (size_t j = 0; j < prev->count; j++) {
                if (prev->entries[j].pid == e->pid) {
                    unsigned long prev_ticks = prev->entries[j].utime_ticks + prev->entries[j].stime_ticks;
                    unsigned long curr_ticks = e->utime_ticks + e->stime_ticks;
                    unsigned long delta_ticks = (curr_ticks >= prev_ticks) ? (curr_ticks - prev_ticks) : 0;
                    double proc_sec = (double)delta_ticks / (double)clk_tck;
                    e->cpu_percent = (proc_sec / wall_seconds) * 100.0;
                    if (e->cpu_percent < 0.0) e->cpu_percent = 0.0;
                    break;
                }
            }
        }
    }
}

static int compare_entries_cpu(const void *a, const void *b)
{
    const proc_snap_entry_t *ea = (const proc_snap_entry_t *)a;
    const proc_snap_entry_t *eb = (const proc_snap_entry_t *)b;
    if (ea->cpu_percent > eb->cpu_percent) return -1;
    if (ea->cpu_percent < eb->cpu_percent) return 1;
    if (ea->rss_bytes > eb->rss_bytes) return -1;
    if (ea->rss_bytes < eb->rss_bytes) return 1;
    if (ea->pid < eb->pid) return -1;
    if (ea->pid > eb->pid) return 1;
    return 0;
}

static int compare_entries_mem(const void *a, const void *b)
{
    const proc_snap_entry_t *ea = (const proc_snap_entry_t *)a;
    const proc_snap_entry_t *eb = (const proc_snap_entry_t *)b;
    if (ea->rss_bytes > eb->rss_bytes) return -1;
    if (ea->rss_bytes < eb->rss_bytes) return 1;
    if (ea->cpu_percent > eb->cpu_percent) return -1;
    if (ea->cpu_percent < eb->cpu_percent) return 1;
    if (ea->pid < eb->pid) return -1;
    if (ea->pid > eb->pid) return 1;
    return 0;
}

static void proc_monitor_print_table(const proc_snapshot_t *snap, proc_sort_mode_t sort_mode, size_t max_rows)
{
    printf("\n--- Process Explorer (sorted by %s) ---\n",
           sort_mode == PROC_SORT_CPU ? "CPU" : "Memory");
    printf("%-7s %-18s %-6s %6s %10s %10s %7s\n",
           "PID", "COMMAND", "STATE", "CPU%", "MEM", "VIRT", "THREADS");

    size_t rows = (snap->count < max_rows) ? snap->count : max_rows;
    char mem_buf[32], virt_buf[32];

    for (size_t i = 0; i < rows; i++) {
        const proc_snap_entry_t *e = &snap->entries[i];
        proc_format_bytes(e->rss_bytes, mem_buf, sizeof(mem_buf));
        proc_format_bytes(e->vsize_bytes, virt_buf, sizeof(virt_buf));

        printf("%-7d %-18s %-6c %5.1f%% %10s %10s %7ld\n",
               (int)e->pid,
               e->name,
               e->state_char,
               e->cpu_percent,
               mem_buf,
               virt_buf,
               e->num_threads);
    }
}

int proc_monitor_display_sorted(proc_sort_mode_t sort_mode)
{
    proc_snapshot_t *s1 = proc_snapshot_create();
    proc_snapshot_populate(s1);

    struct timespec req;
    req.tv_sec = 0;
    req.tv_nsec = 100 * 1000000L; /* 100ms delta sample */
    nanosleep(&req, NULL);

    proc_snapshot_t *s2 = proc_snapshot_create();
    proc_snapshot_populate(s2);

    proc_snapshot_calc_deltas(s1, s2);

    if (sort_mode == PROC_SORT_CPU) {
        qsort(s2->entries, s2->count, sizeof(proc_snap_entry_t), compare_entries_cpu);
    } else {
        qsort(s2->entries, s2->count, sizeof(proc_snap_entry_t), compare_entries_mem);
    }

    double sys_util = proc_calc_cpu_utilization(&s1->sys_cpu, &s2->sys_cpu);
    sys_mem_t mem;
    bool has_mem = proc_read_sys_mem(&mem);

    printf("--- System Overview ---\n");
    printf("CPU Utilization:  %.1f%%\n", sys_util);
    if (has_mem && mem.mem_total_kb > 0) {
        char total_buf[32], avail_buf[32], used_buf[32], free_buf[32];
        unsigned long long used_kb = (mem.mem_total_kb >= mem.mem_available_kb) ?
                                     (mem.mem_total_kb - mem.mem_available_kb) : 0;
        double used_pct = 100.0 * (double)used_kb / (double)mem.mem_total_kb;
        proc_format_kb(mem.mem_total_kb, total_buf, sizeof(total_buf));
        proc_format_kb(used_kb, used_buf, sizeof(used_buf));
        proc_format_kb(mem.mem_free_kb, free_buf, sizeof(free_buf));
        proc_format_kb(mem.mem_available_kb, avail_buf, sizeof(avail_buf));
        printf("Memory:           Used: %s / %s (%.1f%%) | Free: %s | Available: %s\n",
               used_buf, total_buf, used_pct, free_buf, avail_buf);
    }

    proc_monitor_print_table(s2, sort_mode, 20);

    proc_snapshot_free(s1);
    proc_snapshot_free(s2);
    return 0;
}

static volatile sig_atomic_t g_watch_stop = 0;

static void watch_sigint_handler(int sig)
{
    (void)sig;
    g_watch_stop = 1;
}

int proc_monitor_watch(proc_sort_mode_t sort_mode, pid_t target_pid)
{
    g_watch_stop = 0;

    struct sigaction sa_int, old_sa_int;
    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = watch_sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, &old_sa_int);

    struct termios orig_termios, raw_termios;
    bool is_interactive_in = (isatty(STDIN_FILENO) == 1);
    bool is_interactive_out = (isatty(STDOUT_FILENO) == 1);

    if (is_interactive_in) {
        if (tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
            raw_termios = orig_termios;
            raw_termios.c_lflag &= ~(ICANON | ECHO);
            raw_termios.c_cc[VMIN] = 0;
            raw_termios.c_cc[VTIME] = 0;
            tcsetattr(STDIN_FILENO, TCSANOW, &raw_termios);
        }
    }

    proc_snapshot_t *prev_snap = NULL;
    proc_snapshot_t *curr_snap = NULL;

    while (!g_watch_stop) {
        if (target_pid > 0) {
            if (is_interactive_out) {
                printf("\033[H\033[2J\033[H");
            }
            int ret = proc_monitor_display_pid(target_pid);
            if (ret != 0) {
                printf("\nProcess %d has terminated.\n", (int)target_pid);
                break;
            }
            printf("\n[Refresh: 1s | Watching PID %d | Press 'q' to quit]\n", (int)target_pid);
            fflush(stdout);
        } else {
            curr_snap = proc_snapshot_create();
            proc_snapshot_populate(curr_snap);
            proc_snapshot_calc_deltas(prev_snap, curr_snap);

            if (sort_mode == PROC_SORT_CPU) {
                qsort(curr_snap->entries, curr_snap->count, sizeof(proc_snap_entry_t), compare_entries_cpu);
            } else {
                qsort(curr_snap->entries, curr_snap->count, sizeof(proc_snap_entry_t), compare_entries_mem);
            }

            if (is_interactive_out) {
                printf("\033[H\033[2J\033[H");
            }

            double sys_util = 0.0;
            if (prev_snap != NULL) {
                sys_util = proc_calc_cpu_utilization(&prev_snap->sys_cpu, &curr_snap->sys_cpu);
            }
            sys_mem_t mem;
            bool has_mem = proc_read_sys_mem(&mem);

            printf("--- System Overview ---\n");
            printf("CPU Utilization:  %.1f%%\n", sys_util);
            if (has_mem && mem.mem_total_kb > 0) {
                char total_buf[32], avail_buf[32], used_buf[32], free_buf[32];
                unsigned long long used_kb = (mem.mem_total_kb >= mem.mem_available_kb) ?
                                             (mem.mem_total_kb - mem.mem_available_kb) : 0;
                double used_pct = 100.0 * (double)used_kb / (double)mem.mem_total_kb;
                proc_format_kb(mem.mem_total_kb, total_buf, sizeof(total_buf));
                proc_format_kb(used_kb, used_buf, sizeof(used_buf));
                proc_format_kb(mem.mem_free_kb, free_buf, sizeof(free_buf));
                proc_format_kb(mem.mem_available_kb, avail_buf, sizeof(avail_buf));
                printf("Memory:           Used: %s / %s (%.1f%%) | Free: %s | Available: %s\n",
                       used_buf, total_buf, used_pct, free_buf, avail_buf);
            }

            size_t max_rows = 20;
            struct winsize ws;
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 10) {
                max_rows = ws.ws_row - 9;
                if (max_rows < 5) max_rows = 5;
            }

            proc_monitor_print_table(curr_snap, sort_mode, max_rows);

            printf("\n[Refresh: 1s | Sort: %s | Press 'q' to quit, 'c' sort CPU, 'm' sort MEM]\n",
                   sort_mode == PROC_SORT_CPU ? "CPU" : "Memory");
            fflush(stdout);

            if (prev_snap != NULL) {
                proc_snapshot_free(prev_snap);
            }
            prev_snap = curr_snap;
            curr_snap = NULL;
        }

        if (!is_interactive_in) {
            /* In non-interactive mode (batch/pipe/script), complete 1 frame and exit cleanly */
            break;
        }

        struct pollfd pfd;
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        int p_ret = poll(&pfd, 1, 1000);

        if (p_ret > 0) {
            if (pfd.revents & (POLLHUP | POLLERR)) {
                break;
            }
            if (pfd.revents & POLLIN) {
                char ch = 0;
                ssize_t n = read(STDIN_FILENO, &ch, 1);
                if (n <= 0) {
                    break;
                } else if (ch == 'q' || ch == 'Q') {
                    break;
                } else if (ch == 'c' || ch == 'C') {
                    sort_mode = PROC_SORT_CPU;
                } else if (ch == 'm' || ch == 'M') {
                    sort_mode = PROC_SORT_MEM;
                }
            }
        } else if (p_ret < 0) {
            if (errno == EINTR && g_watch_stop) {
                break;
            }
        }
    }

    if (prev_snap != NULL) {
        proc_snapshot_free(prev_snap);
    }
    if (curr_snap != NULL) {
        proc_snapshot_free(curr_snap);
    }

    if (is_interactive_in) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    }

    sigaction(SIGINT, &old_sa_int, NULL);

    if (is_interactive_out) {
        printf("\n");
    }

    return 0;
}

int builtin_monitor(const command_t *cmd)
{
    if (cmd == NULL) {
        return 1;
    }

    if (cmd->argc == 1) {
        return proc_monitor_display_summary();
    }

    bool opt_watch = false;
    proc_sort_mode_t sort_mode = PROC_SORT_CPU;
    bool sort_specified = false;
    long target_pid = -1;

    for (int i = 1; i < cmd->argc; i++) {
        const char *arg = cmd->argv[i];
        if (strcmp(arg, "--watch") == 0 || strcmp(arg, "-w") == 0) {
            opt_watch = true;
        } else if (strcmp(arg, "--sort") == 0 || strcmp(arg, "-s") == 0) {
            if (i + 1 >= cmd->argc) {
                fprintf(stderr, "smartshell: monitor: option '%s' requires an argument\n", arg);
                return 1;
            }
            i++;
            const char *val = cmd->argv[i];
            if (strcasecmp(val, "cpu") == 0) {
                sort_mode = PROC_SORT_CPU;
                sort_specified = true;
            } else if (strcasecmp(val, "memory") == 0 || strcasecmp(val, "mem") == 0) {
                sort_mode = PROC_SORT_MEM;
                sort_specified = true;
            } else {
                fprintf(stderr, "smartshell: monitor: invalid sort field '%s' (expected 'cpu' or 'memory')\n", val);
                return 1;
            }
        } else if (strncmp(arg, "--sort=", 7) == 0) {
            const char *val = arg + 7;
            if (strcasecmp(val, "cpu") == 0) {
                sort_mode = PROC_SORT_CPU;
                sort_specified = true;
            } else if (strcasecmp(val, "memory") == 0 || strcasecmp(val, "mem") == 0) {
                sort_mode = PROC_SORT_MEM;
                sort_specified = true;
            } else {
                fprintf(stderr, "smartshell: monitor: invalid sort field '%s' (expected 'cpu' or 'memory')\n", val);
                return 1;
            }
        } else if (arg[0] == '-') {
            fprintf(stderr, "smartshell: monitor: unrecognized option '%s'\n", arg);
            return 1;
        } else {
            char *endptr = NULL;
            long val = strtol(arg, &endptr, 10);
            if (*endptr != '\0' || val <= 0) {
                fprintf(stderr, "smartshell: monitor: %s: invalid process ID\n", arg);
                return 1;
            }
            if (target_pid > 0) {
                fprintf(stderr, "smartshell: monitor: too many arguments\n");
                return 1;
            }
            target_pid = val;
        }
    }

    if (opt_watch) {
        return proc_monitor_watch(sort_mode, (pid_t)target_pid);
    }

    if (sort_specified) {
        return proc_monitor_display_sorted(sort_mode);
    }

    if (target_pid > 0) {
        return proc_monitor_display_pid((pid_t)target_pid);
    }

    return proc_monitor_display_summary();
}

/* ========================================================================= */
/* System Information Implementation                                         */
/* ========================================================================= */

int proc_display_sysinfo(void)
{
    printf("SmartShell System Information\n");
    printf("=============================\n");

    /* 1. Operating system and kernel release via uname */
    struct utsname u;
    if (uname(&u) == 0) {
        printf("Operating System:     %s %s\n", u.sysname, u.release);
        printf("Kernel Version:       %s\n", u.version);
        printf("Machine Architecture: %s\n", u.machine);
    } else {
        printf("Operating System:     Unknown (%s)\n", strerror(errno));
    }

    /* 2. CPU core count */
    long cpus_onln = sysconf(_SC_NPROCESSORS_ONLN);
    long cpus_conf = sysconf(_SC_NPROCESSORS_CONF);
    if (cpus_onln > 0) {
        if (cpus_conf > 0 && cpus_conf != cpus_onln) {
            printf("CPU Cores:            %ld online (%ld configured)\n", cpus_onln, cpus_conf);
        } else {
            printf("CPU Cores:            %ld online\n", cpus_onln);
        }
    } else {
        printf("CPU Cores:            Unknown\n");
    }

    /* 3. Memory information from /proc/meminfo */
    sys_mem_t mem;
    if (proc_read_sys_mem(&mem) && mem.mem_total_kb > 0) {
        char total_buf[32], avail_buf[32], free_buf[32];
        proc_format_kb(mem.mem_total_kb, total_buf, sizeof(total_buf));
        proc_format_kb(mem.mem_available_kb, avail_buf, sizeof(avail_buf));
        proc_format_kb(mem.mem_free_kb, free_buf, sizeof(free_buf));

        printf("Total Memory:         %s (%llu kB)\n", total_buf, mem.mem_total_kb);
        printf("Available Memory:     %s (%llu kB)\n", avail_buf, mem.mem_available_kb);
        printf("Free Memory:          %s (%llu kB)\n", free_buf, mem.mem_free_kb);
        if (mem.swap_total_kb > 0) {
            char swap_total_buf[32], swap_free_buf[32];
            proc_format_kb(mem.swap_total_kb, swap_total_buf, sizeof(swap_total_buf));
            proc_format_kb(mem.swap_free_kb, swap_free_buf, sizeof(swap_free_buf));
            printf("Swap Memory:          %s total / %s free\n", swap_total_buf, swap_free_buf);
        }
    } else {
        printf("Memory Information:   Unavailable\n");
    }

    /* 4. SmartShell PID, PPID, PGID, SID */
    pid_t pid = getpid();
    pid_t ppid = getppid();
    pid_t pgid = getpgrp();
    pid_t sid = getsid(0);

    printf("SmartShell PID:       %d\n", (int)pid);
    printf("Parent PID (PPID):    %d\n", (int)ppid);
    printf("Process Group (PGID): %d\n", (int)pgid);
    printf("Session ID (SID):     %d\n", (int)sid);

    /* 5. Terminal status */
    if (isatty(STDIN_FILENO)) {
        char *term_name = ttyname(STDIN_FILENO);
        pid_t fg_pgrp = tcgetpgrp(STDIN_FILENO);
        printf("Terminal Mode:        Interactive (%s, foreground PGID: %d)\n",
               term_name ? term_name : "tty", (int)fg_pgrp);
    } else {
        printf("Terminal Mode:        Non-interactive (standard input is not a terminal)\n");
    }

    /* 6. /proc filesystem accessibility */
    bool proc_dir = (access("/proc", R_OK | X_OK) == 0);
    bool proc_stat = (access("/proc/stat", R_OK) == 0);
    bool proc_meminfo = (access("/proc/meminfo", R_OK) == 0);

    if (proc_dir && proc_stat && proc_meminfo) {
        printf("/proc Accessibility:  Accessible (virtual filesystem mounted and readable)\n");
    } else if (proc_dir) {
        printf("/proc Accessibility:  Partially accessible (directory readable, restricted files)\n");
    } else {
        printf("/proc Accessibility:  Inaccessible (%s)\n", strerror(errno));
    }

    return 0;
}

int builtin_sysinfo(const command_t *cmd)
{
    if (cmd == NULL) {
        return 1;
    }

    if (cmd->argc > 1) {
        fprintf(stderr, "smartshell: sysinfo: too many arguments\n");
        return 1;
    }

    return proc_display_sysinfo();
}
