# SmartShell CPU Scheduling Lab (Phase 8A & 8B)

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
    int priority;         /* Static priority level (lower value = higher priority) */
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
- In Round Robin, a single process PID can appear across multiple non-contiguous slices as it is preempted and resumed.

Example timeline sequence:
$$\text{P1} [0, 2] \longrightarrow \text{P2} [2, 4] \longrightarrow \text{P1} [4, 6] \longrightarrow \text{IDLE} [6, 9] \longrightarrow \text{P3} [9, 11]$$

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

### C. Round Robin (RR)
- **Policy**: Preemptive time-sliced execution.
- **Configurable Quantum**: Positive integer time quantum $Q > 0$ (default: `SCHED_DEFAULT_RR_QUANTUM = 4`). Rejects invalid quantum values $\le 0$.
- **Slice Duration**: $\min(\text{remaining\_time}, Q)$.
- **Exact Arrival and Requeue Ordering Policy**:
  1. Arrived processes enter a deterministic FIFO ready queue when `arrival_time <= current_time`.
  2. Simultaneous arrivals entering the ready queue are ordered by `arrival_time` ascending, breaking ties by lowest `pid`.
  3. During a running slice $(t_{\text{start}}, t_{\text{end}}]$, any newly arrived process with $\text{arrival\_time} \le t_{\text{end}}$ is appended to the ready queue **before** re-queuing the currently preempted process whose quantum expired.
  4. If a process finishes ($\text{remaining\_time} = 0$), it is marked completed and not re-queued.
  5. If the ready queue is empty, the simulator logs an idle slice to the earliest future arrival and enqueues all arrivals at that tick.

### D. Priority Scheduling
- **Policy**: Explicitly **NON-PREEMPTIVE**.
- **Priority Convention**: Lower numeric value indicates higher priority (e.g., priority $1 > \text{priority } 2$).
- **Selection**: Considers only processes that have already arrived (`arrival_time <= current_time`). Selects the uncompleted process with highest priority (lowest numeric value).
- **Tie-Breaking**:
  1. Lower numeric priority value
  2. Earlier `arrival_time`
  3. Lowest `pid`
- **Non-Preemptive Invariant**: Once selected, the process executes until its CPU burst completes. New arrivals with higher priority do **not** preempt the running job.
- **Idle Periods**: Handled consistently when the ready queue is empty before arrivals.

---

## 5. Metric Calculations & Formulas

For each virtual process $i$:
- **Turnaround Time ($TAT$)**: Total elapsed time from arrival to completion.
  $$TAT = \text{Completion Time} - \text{Arrival Time}$$
- **Waiting Time ($WT$)**: Total time spent waiting in the ready queue.
  $$WT = TAT - \text{Burst Time}$$
- **Response Time ($RT$)**: Time from arrival until the process first gains the CPU.
  $$RT = \text{First Start Time} - \text{Arrival Time}$$

*(Note: For non-preemptive algorithms, $RT = WT$. For preemptive Round Robin, $RT \le WT$ since processes may wait again between time slices.)*

### System-Wide Summary Metrics
- **Total Elapsed Time ($T_{\text{elapsed}}$)**: Virtual time span from $0$ to the completion of all jobs.
- **Total Busy Time ($T_{\text{busy}}$)**: Sum of all completed process bursts ($\sum \text{burst\_time}$).
- **Total Idle Time ($T_{\text{idle}}$)**: $T_{\text{elapsed}} - T_{\text{busy}}$.
- **CPU Utilization (%)**:
  $$\text{CPU Utilization} = \begin{cases} \frac{T_{\text{busy}}}{T_{\text{elapsed}}} \times 100\% & \text{if } T_{\text{elapsed}} > 0 \\ 0.0\% & \text{if } T_{\text{elapsed}} = 0 \end{cases}$$
- **Averages**:
  $$\text{Average Waiting Time} = \frac{\sum WT}{N}, \quad \text{Average Turnaround Time} = \frac{\sum TAT}{N}, \quad \text{Average Response Time} = \frac{\sum RT}{N}$$
  *(Evaluated safely as $0.0$ when $N = 0$.)*

---

## 6. Gantt Chart Textual Presentation

SmartShell provides a clean, dependency-free textual renderer for execution timelines:
- `char *sched_render_gantt(const sched_result_t *result);`
- `int sched_render_gantt_buf(const sched_result_t *result, char *buf, size_t size);`

### Format Specification:
- **Line 1**: Vertical bar demarcations with centered process labels (`|  P1  |  P2  | IDLE |  P1  |`).
- **Line 2**: Numerical time boundary markers exactly aligned with each vertical bar (`0      2      4      6      8`).
- **Idle Representation**: CPU idle intervals are clearly labeled with `IDLE`.
- **Preemption Support**: Correctly formats timelines where the same PID appears multiple times across non-contiguous intervals (as in Round Robin).
- **Pure Textual**: Does not emit ANSI color codes or terminal-dependent escapes, ensuring easy programmatic testing and log readability.
