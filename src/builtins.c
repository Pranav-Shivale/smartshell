/*
 * builtins.c — Built-in commands implementation for SmartShell.
 */

#include "builtins.h"
#include "job.h"
#include "proc_monitor.h"
#include "scheduler.h"
#include "signals.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

bool is_builtin(const char *name)
{
    if (name == NULL) {
        return false;
    }
    return (strcmp(name, "cd") == 0 ||
            strcmp(name, "pwd") == 0 ||
            strcmp(name, "exit") == 0 ||
            strcmp(name, "quit") == 0 ||
            strcmp(name, "help") == 0 ||
            strcmp(name, "jobs") == 0 ||
            strcmp(name, "fg") == 0 ||
            strcmp(name, "bg") == 0 ||
            strcmp(name, "kill") == 0 ||
            strcmp(name, "monitor") == 0 ||
            strcmp(name, "schedule") == 0);
}

static int builtin_cd(const command_t *cmd)
{
    const char *target = NULL;

    if (cmd->argc == 1 || strcmp(cmd->argv[1], "~") == 0) {
        target = getenv("HOME");
        if (target == NULL || *target == '\0') {
            fprintf(stderr, "smartshell: cd: HOME not set\n");
            return 1;
        }
    } else if (cmd->argc == 2) {
        target = cmd->argv[1];
    } else {
        fprintf(stderr, "smartshell: cd: too many arguments\n");
        return 1;
    }

    if (chdir(target) != 0) {
        fprintf(stderr, "smartshell: cd: %s: %s\n", target, strerror(errno));
        return 1;
    }

    return 0;
}

static int builtin_pwd(void)
{
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
        return 0;
    }

    fprintf(stderr, "smartshell: pwd: %s\n", strerror(errno));
    return 1;
}

static int builtin_help(void)
{
    printf("SmartShell — Built-in Commands:\n");
    printf("  cd [dir]       Change the working directory (defaults to HOME)\n");
    printf("  pwd            Print the current working directory\n");
    printf("  jobs           List active jobs\n");
    printf("  fg [%%jid]      Bring a job to the foreground\n");
    printf("  bg [%%jid]      Resume a stopped job in the background\n");
    printf("  kill [-sig] <pid|%%jid> Send a signal to a process or job\n");
    printf("  monitor [pid]  Monitor system overview, shell jobs, or process details via /proc\n");
    printf("  schedule <alg> Simulate CPU scheduling algorithms (fcfs, sjf, rr, priority, mlfq)\n");
    printf("  help           Display this help message\n");
    printf("  exit [code]    Exit the shell with optional status (0-255)\n");
    printf("  quit [code]    Alias for exit\n\n");
    printf("External commands are located via PATH and executed in the foreground.\n");
    return 0;
}

static int builtin_exit(const command_t *cmd, bool *should_exit, int *exit_code)
{
    int code = 0;

    if (cmd->argc > 1) {
        char *endptr = NULL;
        errno = 0;
        long val = strtol(cmd->argv[1], &endptr, 10);

        if (errno != 0 || endptr == cmd->argv[1] || *endptr != '\0') {
            fprintf(stderr, "smartshell: %s: %s: numeric argument required\n",
                    cmd->argv[0], cmd->argv[1]);
            if (should_exit != NULL) {
                *should_exit = true;
            }
            if (exit_code != NULL) {
                *exit_code = 2;
            }
            return 2;
        }

        if (cmd->argc > 2) {
            fprintf(stderr, "smartshell: %s: too many arguments\n", cmd->argv[0]);
            if (should_exit != NULL) {
                *should_exit = false;
            }
            return 1;
        }

        code = (int)(val & 0xFF);
    }

    if (should_exit != NULL) {
        *should_exit = true;
    }
    if (exit_code != NULL) {
        *exit_code = code;
    }

    return 0;
}

/*
 * Resolve a job specification (%N) or return default/current job if arg is NULL.
 */
static job_t *resolve_job_spec(const char *cmd_name, const char *arg)
{
    if (arg == NULL) {
        job_t *job = job_get_current();
        if (job == NULL) {
            fprintf(stderr, "smartshell: %s: current: no such job\n", cmd_name);
            return NULL;
        }
        return job;
    }

    if (arg[0] != '%') {
        fprintf(stderr, "smartshell: %s: %s: no such job\n", cmd_name, arg);
        return NULL;
    }

    if (arg[1] == '\0') {
        fprintf(stderr, "smartshell: %s: %s: invalid job specification\n", cmd_name, arg);
        return NULL;
    }

    char *endptr = NULL;
    long jid = strtol(arg + 1, &endptr, 10);
    if (*endptr != '\0' || jid <= 0) {
        fprintf(stderr, "smartshell: %s: %s: invalid job specification\n", cmd_name, arg);
        return NULL;
    }

    job_t *job = job_find_by_jid((int)jid);
    if (job == NULL) {
        fprintf(stderr, "smartshell: %s: %%%ld: no such job\n", cmd_name, jid);
        return NULL;
    }

    return job;
}

static int builtin_fg(const command_t *cmd)
{
    const char *arg = (cmd->argc > 1) ? cmd->argv[1] : NULL;
    if (cmd->argc > 2) {
        fprintf(stderr, "smartshell: fg: too many arguments\n");
        return 1;
    }

    job_t *job = resolve_job_spec("fg", arg);
    if (job == NULL) {
        return 1;
    }

    /* Print job command line */
    printf("%s\n", job->command_line);
    fflush(stdout);

    /* Mark as foreground */
    job->is_foreground = true;

    /* If stopped, send SIGCONT to entire process group */
    if (job->state == JOB_STOPPED) {
        for (int i = 0; i < job->process_count; i++) {
            if (job->processes[i].state == PROC_STOPPED) {
                job->processes[i].state = PROC_RUNNING;
            }
        }
        job->state = JOB_RUNNING;
        job->notified = false;

        if (kill(-job->pgid, SIGCONT) < 0) {
            fprintf(stderr, "smartshell: fg: failed to send SIGCONT: %s\n", strerror(errno));
            return 1;
        }
    }

    /* Give terminal ownership to the job */
    if (shell_is_interactive_mode()) {
        shell_give_terminal_to(job->pgid);
    }

    /* Wait for the job state transition */
    int status = wait_for_job(job);

    /* Reclaim terminal ownership for the shell */
    if (shell_is_interactive_mode()) {
        shell_reclaim_terminal();
    }

    if (job_is_completed(job)) {
        job_remove(job->jid);
    } else if (job_is_stopped(job)) {
        printf("\n[%d]+  Stopped  %s\n", job->jid, job->command_line);
        fflush(stdout);
        job->is_foreground = false;
        job->notified = true;
    }

    return status;
}

static int builtin_bg(const command_t *cmd)
{
    const char *arg = (cmd->argc > 1) ? cmd->argv[1] : NULL;
    if (cmd->argc > 2) {
        fprintf(stderr, "smartshell: bg: too many arguments\n");
        return 1;
    }

    job_t *job = resolve_job_spec("bg", arg);
    if (job == NULL) {
        return 1;
    }

    if (job->state != JOB_STOPPED) {
        fprintf(stderr, "smartshell: bg: job %d already in background\n", job->jid);
        return 1;
    }

    /* Mark as running in background */
    job->is_foreground = false;
    for (int i = 0; i < job->process_count; i++) {
        if (job->processes[i].state == PROC_STOPPED) {
            job->processes[i].state = PROC_RUNNING;
        }
    }
    job->state = JOB_RUNNING;
    job->notified = false;

    /* Print notification */
    printf("[%d]+ %s &\n", job->jid, job->command_line);
    fflush(stdout);

    /* Send SIGCONT to entire process group */
    if (kill(-job->pgid, SIGCONT) < 0) {
        fprintf(stderr, "smartshell: bg: failed to send SIGCONT: %s\n", strerror(errno));
        return 1;
    }

    return 0;
}

static int parse_signal(const char *str)
{
    if (str == NULL || *str != '-') {
        return -1;
    }
    const char *sigstr = str + 1;
    if (*sigstr == '\0') {
        return -1;
    }

    if (strncasecmp(sigstr, "SIG", 3) == 0) {
        sigstr += 3;
    }

    if (strcasecmp(sigstr, "HUP") == 0)   return SIGHUP;
    if (strcasecmp(sigstr, "INT") == 0)   return SIGINT;
    if (strcasecmp(sigstr, "QUIT") == 0)  return SIGQUIT;
    if (strcasecmp(sigstr, "KILL") == 0)  return SIGKILL;
    if (strcasecmp(sigstr, "TERM") == 0)  return SIGTERM;
    if (strcasecmp(sigstr, "STOP") == 0)  return SIGSTOP;
    if (strcasecmp(sigstr, "CONT") == 0)  return SIGCONT;
    if (strcasecmp(sigstr, "TSTP") == 0)  return SIGTSTP;
    if (strcasecmp(sigstr, "USR1") == 0)  return SIGUSR1;
    if (strcasecmp(sigstr, "USR2") == 0)  return SIGUSR2;

    char *endptr = NULL;
    long num = strtol(sigstr, &endptr, 10);
    if (*endptr == '\0' && num > 0 && num < 64) {
        return (int)num;
    }

    return -1;
}

static int builtin_kill(const command_t *cmd)
{
    if (cmd->argc < 2) {
        fprintf(stderr, "smartshell: kill: usage: kill [-s sigspec | -sigspec] [pid | %%jobspec] ...\n");
        return 1;
    }

    int sig = SIGTERM;
    int target_idx = 1;

    /* Check for signal flag */
    if (cmd->argv[1][0] == '-') {
        sig = parse_signal(cmd->argv[1]);
        if (sig < 0) {
            fprintf(stderr, "smartshell: kill: %s: invalid signal specification\n", cmd->argv[1]);
            return 1;
        }
        target_idx = 2;
    }

    if (target_idx >= cmd->argc) {
        fprintf(stderr, "smartshell: kill: usage: kill [-s sigspec | -sigspec] [pid | %%jobspec] ...\n");
        return 1;
    }

    int ret = 0;
    for (int i = target_idx; i < cmd->argc; i++) {
        const char *target = cmd->argv[i];

        if (target[0] == '%') {
            /* Job specification */
            char *endptr = NULL;
            long jid = strtol(target + 1, &endptr, 10);
            if (*endptr != '\0' || jid <= 0) {
                fprintf(stderr, "smartshell: kill: %s: invalid job specification\n", target);
                ret = 1;
                continue;
            }

            job_t *job = job_find_by_jid((int)jid);
            if (job == NULL) {
                fprintf(stderr, "smartshell: kill: %s: no such job\n", target);
                ret = 1;
                continue;
            }

            /* Send signal to entire process group */
            if (kill(-job->pgid, sig) < 0) {
                fprintf(stderr, "smartshell: kill: (%d) - %s\n", (int)job->pgid, strerror(errno));
                ret = 1;
            }
        } else {
            /* PID specification */
            char *endptr = NULL;
            long pid = strtol(target, &endptr, 10);
            if (*endptr != '\0') {
                fprintf(stderr, "smartshell: kill: %s: arguments must be process or job IDs\n", target);
                ret = 1;
                continue;
            }

            if (pid == (long)getpid()) {
                fprintf(stderr, "smartshell: kill: cannot kill shell process\n");
                ret = 1;
                continue;
            }
            if (pid <= 0) {
                fprintf(stderr, "smartshell: kill: %ld: invalid process ID\n", pid);
                ret = 1;
                continue;
            }

            if (kill((pid_t)pid, sig) < 0) {
                fprintf(stderr, "smartshell: kill: (%ld) - %s\n", pid, strerror(errno));
                ret = 1;
            }
        }
    }

    return ret;
}

#define SCHED_DEMO_WORKLOAD_COUNT 5

static void init_demo_workload(sched_process_t *workload)
{
    /* Virtual process workload: PID, arrival_time, burst_time, priority */
    sched_process_init(&workload[0], 1, 0,  8, 3);
    sched_process_init(&workload[1], 2, 1,  4, 1);
    sched_process_init(&workload[2], 3, 3,  9, 4);
    sched_process_init(&workload[3], 4, 5,  2, 2);
    sched_process_init(&workload[4], 5, 26, 4, 2);
}

static int builtin_schedule_help(void)
{
    printf("Usage: schedule <algorithm> [args]\n\n");
    printf("Simulate educational CPU scheduling algorithms on a deterministic virtual workload.\n");
    printf("NOTE: This is a user-space simulation; it does not alter Linux's real CPU scheduler.\n\n");
    printf("Available algorithms:\n");
    printf("  schedule fcfs          First-Come, First-Served (non-preemptive)\n");
    printf("  schedule sjf           Shortest Job First (non-preemptive)\n");
    printf("  schedule rr [quantum]  Round Robin (preemptive, default quantum: %d)\n", SCHED_DEFAULT_RR_QUANTUM);
    printf("  schedule priority      Priority scheduling (non-preemptive, lower number = higher priority)\n");
    printf("  schedule mlfq          Multilevel Feedback Queue (3 queues: RR4, RR8, FCFS; boost every 200 ticks)\n");
    printf("  schedule help          Display this help message\n");
    return 0;
}

static void print_schedule_results(const char *algo_name, bool is_rr, int quantum,
                                   const sched_process_t *workload, int count,
                                   const sched_result_t *res)
{
    printf("SmartShell CPU Scheduling Simulation (User-Space Lab)\n");
    printf("Algorithm:  %s\n", algo_name);
    if (is_rr) {
        printf("RR Quantum: %d\n", quantum);
    }
    printf("\n");

    printf("Workload:\n");
    printf("  PID  Arrival  Burst  Priority\n");
    for (int i = 0; i < count; i++) {
        printf("  %3d  %7d  %5d  %8d\n",
               workload[i].pid, workload[i].arrival_time,
               workload[i].burst_time, workload[i].priority);
    }
    printf("\n");

    printf("Gantt Chart:\n");
    char *gantt = sched_render_gantt(res);
    if (gantt != NULL) {
        printf("%s", gantt);
        free(gantt);
    } else {
        printf("  (none)\n");
    }
    printf("\n");

    printf("Process Metrics:\n");
    printf("  PID   AT   BT   PR   ST   CT   WT  TAT   RT\n");
    for (int i = 0; i < res->process_count; i++) {
        const sched_process_t *p = &res->processes[i];
        printf("  %3d  %3d  %3d  %3d  %3d  %3d  %3d  %3d  %3d\n",
               p->pid, p->arrival_time, p->burst_time, p->priority,
               p->start_time, p->completion_time,
               p->waiting_time, p->turnaround_time, p->response_time);
    }
    printf("\n");

    printf("Summary:\n");
    printf("  Total Elapsed Time:      %d\n", res->total_elapsed_time);
    printf("  Total Busy Time:         %d\n", res->total_busy_time);
    printf("  Total Idle Time:         %d\n", res->total_idle_time);
    printf("  CPU Utilization:         %.2f%%\n", res->cpu_utilization);
    printf("  Average Waiting Time:    %.2f\n", res->avg_waiting_time);
    printf("  Average Turnaround Time: %.2f\n", res->avg_turnaround_time);
    printf("  Average Response Time:   %.2f\n", res->avg_response_time);
}

static int builtin_schedule(const command_t *cmd)
{
    if (cmd == NULL || cmd->argv == NULL || cmd->argc < 1) {
        return 1;
    }

    if (cmd->argc == 1 || strcmp(cmd->argv[1], "help") == 0) {
        if (cmd->argc > 2) {
            fprintf(stderr, "smartshell: schedule: too many arguments\n");
            return 1;
        }
        return builtin_schedule_help();
    }

    const char *alg = cmd->argv[1];
    int quantum = SCHED_DEFAULT_RR_QUANTUM;
    bool is_rr = false;

    if (strcmp(alg, "fcfs") == 0) {
        if (cmd->argc > 2) {
            fprintf(stderr, "smartshell: schedule: too many arguments\n");
            return 1;
        }
    } else if (strcmp(alg, "sjf") == 0) {
        if (cmd->argc > 2) {
            fprintf(stderr, "smartshell: schedule: too many arguments\n");
            return 1;
        }
    } else if (strcmp(alg, "priority") == 0) {
        if (cmd->argc > 2) {
            fprintf(stderr, "smartshell: schedule: too many arguments\n");
            return 1;
        }
    } else if (strcmp(alg, "mlfq") == 0) {
        if (cmd->argc > 2) {
            fprintf(stderr, "smartshell: schedule: too many arguments\n");
            return 1;
        }
    } else if (strcmp(alg, "rr") == 0) {
        is_rr = true;
        if (cmd->argc == 2) {
            quantum = SCHED_DEFAULT_RR_QUANTUM;
        } else if (cmd->argc == 3) {
            char *endptr = NULL;
            errno = 0;
            long val = strtol(cmd->argv[2], &endptr, 10);
            if (errno != 0 || endptr == cmd->argv[2] || *endptr != '\0' || val <= 0 || val > 100000) {
                fprintf(stderr, "smartshell: schedule: invalid quantum '%s': must be a positive integer\n",
                        cmd->argv[2]);
                return 1;
            }
            quantum = (int)val;
        } else {
            fprintf(stderr, "smartshell: schedule: too many arguments\n");
            return 1;
        }
    } else {
        fprintf(stderr, "smartshell: schedule: unknown algorithm '%s'\n", alg);
        fprintf(stderr, "Run 'schedule help' for available algorithms.\n");
        return 1;
    }

    sched_process_t workload[SCHED_DEMO_WORKLOAD_COUNT];
    init_demo_workload(workload);

    sched_result_t res;
    sched_result_init(&res);
    int rc = -1;

    if (strcmp(alg, "fcfs") == 0) {
        rc = sched_schedule_fcfs(workload, SCHED_DEMO_WORKLOAD_COUNT, &res);
    } else if (strcmp(alg, "sjf") == 0) {
        rc = sched_schedule_sjf(workload, SCHED_DEMO_WORKLOAD_COUNT, &res);
    } else if (strcmp(alg, "priority") == 0) {
        rc = sched_schedule_priority(workload, SCHED_DEMO_WORKLOAD_COUNT, &res);
    } else if (strcmp(alg, "rr") == 0) {
        rc = sched_schedule_rr(workload, SCHED_DEMO_WORKLOAD_COUNT, quantum, &res);
    } else if (strcmp(alg, "mlfq") == 0) {
        rc = sched_schedule_mlfq(workload, SCHED_DEMO_WORKLOAD_COUNT, &res);
    }

    if (rc != 0) {
        fprintf(stderr, "smartshell: schedule: simulation failed\n");
        sched_result_free(&res);
        return 1;
    }

    const char *disp_name = alg;
    if (strcmp(alg, "fcfs") == 0) disp_name = "First-Come, First-Served (FCFS)";
    else if (strcmp(alg, "sjf") == 0) disp_name = "Shortest Job First (SJF)";
    else if (strcmp(alg, "priority") == 0) disp_name = "Non-Preemptive Priority";
    else if (strcmp(alg, "rr") == 0) disp_name = "Round Robin (RR)";
    else if (strcmp(alg, "mlfq") == 0) disp_name = "Multilevel Feedback Queue (MLFQ)";

    print_schedule_results(disp_name, is_rr, quantum, workload, SCHED_DEMO_WORKLOAD_COUNT, &res);
    sched_result_free(&res);

    return 0;
}

int execute_builtin(const command_t *cmd, bool *should_exit, int *exit_code)
{
    if (should_exit != NULL) {
        *should_exit = false;
    }
    if (exit_code != NULL) {
        *exit_code = 0;
    }

    if (cmd == NULL || cmd->argv == NULL || cmd->argv[0] == NULL) {
        return 0;
    }

    const char *name = cmd->argv[0];

    if (strcmp(name, "cd") == 0) {
        return builtin_cd(cmd);
    }
    if (strcmp(name, "pwd") == 0) {
        return builtin_pwd();
    }
    if (strcmp(name, "jobs") == 0) {
        signals_process_chld_events();
        job_notify_and_cleanup();
        job_table_print();
        return 0;
    }
    if (strcmp(name, "fg") == 0) {
        return builtin_fg(cmd);
    }
    if (strcmp(name, "bg") == 0) {
        return builtin_bg(cmd);
    }
    if (strcmp(name, "kill") == 0) {
        return builtin_kill(cmd);
    }
    if (strcmp(name, "monitor") == 0) {
        return builtin_monitor(cmd);
    }
    if (strcmp(name, "schedule") == 0) {
        return builtin_schedule(cmd);
    }
    if (strcmp(name, "help") == 0) {
        return builtin_help();
    }
    if (strcmp(name, "exit") == 0 || strcmp(name, "quit") == 0) {
        return builtin_exit(cmd, should_exit, exit_code);
    }

    return 0;
}
