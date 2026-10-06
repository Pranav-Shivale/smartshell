/*
 * proc_monitor.c — Linux /proc process and system monitoring implementation.
 */

#include "proc_monitor.h"
#include "job.h"
#include "utils.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

int builtin_monitor(const command_t *cmd)
{
    if (cmd == NULL) {
        return 1;
    }

    if (cmd->argc == 1) {
        return proc_monitor_display_summary();
    }

    if (cmd->argc == 2) {
        char *endptr = NULL;
        long pid_val = strtol(cmd->argv[1], &endptr, 10);
        if (*endptr != '\0' || pid_val <= 0) {
            fprintf(stderr, "smartshell: monitor: %s: invalid process ID\n", cmd->argv[1]);
            return 1;
        }
        return proc_monitor_display_pid((pid_t)pid_val);
    }

    fprintf(stderr, "smartshell: monitor: too many arguments\n");
    return 1;
}
