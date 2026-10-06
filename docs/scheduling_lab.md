# SmartShell CPU Scheduling Lab (Phase 8A)

The SmartShell CPU Scheduling Lab is an educational simulation environment designed to demonstrate classical operating system CPU scheduling concepts in user space.

> [!IMPORTANT]
> **Educational Simulation Notice:**  
> **SmartShell does not control Linux's real CPU scheduler.**  
> This module is an educational user-space simulation that models CPU scheduling algorithms using discrete integer simulated time. It does not modify, replace, or interact with the Linux kernel's CPU scheduler, nor does it use OS processes (`fork`, `exec`), signals (`kill`), terminal controls, or wall-clock sleeping (`sleep`, `nanosleep`) to simulate process execution.

---

## 1. Architectural Principles

1. **Complete Decoupling**: The scheduling simulator (`include/scheduler.h`, `src/scheduler.c`) is completely separated from:
   - Shell command execution (`executor.c`)
   - POSIX job control and process groups (`job.c`, `signals.c`)
   - Linux `/proc` monitoring subsystem (`proc_monitor.c`)
   The shell job table is never polluted or modified by virtual simulation jobs.
2. **Mathematical Simulated Time**: Time is measured in non-negative discrete integer clock ticks ($t = 0, 1, 2, \dots$) and advances mathematically as virtual processes are scheduled.
3. **Determinism**: Identical workload configurations will always produce identical timelines and metrics without reliance on randomness or unordered data structures.

---

## 2. Virtual Process Representation

A virtual process is represented by `sched_process_t`:

```c
typedef struct {
    int pid;              /* Process identifier (positive integer) */
    int arrival_time;     /* Simulated arrival time (>= 0) */
    int burst_time;       /* Total CPU burst requirement (> 0) */
    int remaining_time;   /* Remaining CPU time needed to complete */
    int priority;         /* Static priority level */
    int start_time;       /* First time process received CPU (or SCHED_TIME_UNSET) */
    int completion_time;  /* Time process finished execution (or SCHED_TIME_UNSET) */
    int waiting_time;     /* Total time spent waiting in ready queue */
    int turnaround_time;  /* Total time from arrival to completion */
    int response_time;    /* Time from arrival to first execution */
} sched_process_t;
```

Sentinel value `SCHED_TIME_UNSET` (`-1`) indicates a timing metric that has not yet been set (e.g., when a process has not yet been selected to run or completed).

---

## 3. Structured Execution Timeline

The simulation records an inspectable, structured execution history:

```c
typedef struct {
    int pid;              /* Executing process PID or SCHED_PID_IDLE */
    int start_time;       /* Simulated start tick of this interval */
    int end_time;         /* Simulated end tick of this interval */
} sched_timeline_entry_t;
```

- When the CPU is actively executing a process, `pid` matches the virtual process PID.
- When no process is ready to execute (e.g. before the first arrival or during gaps between job completions and subsequent arrivals), `pid` is set to `SCHED_PID_IDLE` (`-1`).
- The timeline forms a continuous partition of time intervals $[0, \text{total\_elapsed\_time}]$.
- This programmatic timeline structure provides the exact data representation needed for future Gantt chart visualization.

Example timeline sequence:
$$\text{P1} [0, 3] \longrightarrow \text{P2} [3, 7] \longrightarrow \text{IDLE} [7, 10] \longrightarrow \text{P3} [10, 14]$$

---

## 4. Algorithms Implemented

### A. First-Come, First-Served (FCFS)
- **Policy**: Non-preemptive.
- **Selection**: Among all uncompleted processes that have arrived (`arrival_time <= current_time`), the process with the earliest `arrival_time` is selected.
- **Tie-Breaking**: If multiple processes arrive at the identical tick, the process with the lowest `pid` is selected.
- **Idle Periods**: If no process is ready at the current tick, the CPU records an idle slice until the next arrival tick.

### B. Shortest Job First (SJF)
- **Policy**: Non-preemptive.
- **Selection**: Only processes that have already arrived (`arrival_time <= current_time`) are eligible. The process with the shortest `burst_time` is selected.
- **Tie-Breaking**:
  1. Shortest `burst_time`
  2. Earliest `arrival_time`
  3. Lowest `pid`
- **Non-Preemptive Invariant**: Once a process starts execution, it continues until its burst completes, even if a shorter job arrives during its execution.
- **Idle Periods**: If the ready queue is empty, the simulator advances time to the next arrival, logging an idle slice.

---

## 5. Metric Calculations & Formulas

For each virtual process $i$:
- **Turnaround Time ($TAT$)**: Total elapsed time from arrival to completion.
  $$TAT = \text{Completion Time} - \text{Arrival Time}$$
- **Waiting Time ($WT$)**: Total time spent waiting in the ready queue.
  $$WT = TAT - \text{Burst Time}$$
- **Response Time ($RT$)**: Time from arrival until the process first gains the CPU.
  $$RT = \text{First Start Time} - \text{Arrival Time}$$

*(Note: For non-preemptive algorithms, $RT = WT$ because processes execute continuously once started.)*

### System-Wide Summary Metrics
- **Total Elapsed Time ($T_{\text{elapsed}}$)**: Virtual time span from $0$ to the completion of all jobs.
- **Total Busy Time ($T_{\text{busy}}$)**: Sum of all completed process bursts ($\sum \text{burst\_time}$).
- **Total Idle Time ($T_{\text{idle}}$)**: $T_{\text{elapsed}} - T_{\text{busy}}$.
- **CPU Utilization (%)**:
  $$\text{CPU Utilization} = \begin{cases} \frac{T_{\text{busy}}}{T_{\text{elapsed}}} \times 100\% & \text{if } T_{\text{elapsed}} > 0 \\ 0.0\% & \text{if } T_{\text{elapsed}} = 0 \end{cases}$$
- **Averages**:
  $$\text{Average Waiting Time} = \frac{\sum WT}{N}, \quad \text{Average Turnaround Time} = \frac{\sum TAT}{N}, \quad \text{Average Response Time} = \frac{\sum RT}{N}$$
  *(Evaluated safely as $0.0$ when $N = 0$.)*
