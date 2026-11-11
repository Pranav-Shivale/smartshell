# SmartShell CPU Scheduling Lab (Phase 8 & Phase 10 Upgrade)

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

### E. Multilevel Feedback Queue (MLFQ)
- **Policy**: Preemptive priority scheduling across 3 distinct feedback queues with starvation prevention.
- **Queue Architecture**:
  1. **Q0 (Top Queue)**: Highest priority, Round Robin with time quantum $Q_0 = 4$ simulated ticks (`SCHED_MLFQ_Q0_QUANTUM = 4`).
  2. **Q1 (Middle Queue)**: Medium priority, Round Robin with time quantum $Q_1 = 8$ simulated ticks (`SCHED_MLFQ_Q1_QUANTUM = 8`).
  3. **Q2 (Bottom Queue)**: Lowest priority, First-Come, First-Served (FCFS), non-preemptive within Q2 (no quantum time-slicing).
- **Initial Placement**: Every virtual process enters **Q0** upon its simulated arrival time.
- **Queue Priority Invariant**: At every scheduling decision, the scheduler selects from the highest-priority non-empty queue ($Q_0 > Q_1 > Q_2$). Jobs in lower queues only execute when higher queues are completely empty.
- **Demotion Rules**:
  1. If a job in Q0 consumes its full 4-tick quantum allotment without completing, it is demoted to the tail of Q1.
  2. If a job in Q1 consumes its full 8-tick quantum allotment without completing, it is demoted to the tail of Q2.
  3. Jobs in Q2 execute FCFS and are never demoted further.
- **Higher-Queue Preemption**:
  - Global queue priority is strictly enforced: If a job in Q1 or Q2 is executing and an uncompleted process arrives in Q0, the lower-queue job is **preempted at the exact arrival tick**.
  - A preempted Q1 process retains its remaining unexpired quantum allotment and stays at the head of Q1; when higher queues empty, it resumes with its unexpired allotment.
  - A preempted Q2 process stays at the head of Q2; when higher queues empty, it resumes its FCFS execution.
  - Within Q2, scheduling is FCFS without voluntary quantum interruptions, but global MLFQ preemption by Q0 arrivals takes immediate precedence.
- **Priority Boost (Starvation Prevention)**:
  - To prevent long-running or CPU-intensive jobs in lower queues from starving indefinitely, a global priority boost triggers every 200 simulated ticks (`SCHED_MLFQ_BOOST_INTERVAL = 200` at $t = 200, 400, 600, \dots$).
  - **Exact Boost Timing**: The simulator clamps running slices so it never silently runs past a boost boundary.
  - At the boost tick $t_{\text{boost}}$:
    1. The active execution slice concludes at $t_{\text{boost}}$ (with demotion applied if quantum expired).
    2. All processes currently in Q1 are moved to the tail of Q0 in deterministic FIFO order.
    3. All processes currently in Q2 are moved to the tail of Q0 in deterministic FIFO order.
    4. All processes in Q0 receive a fresh Q0 quantum allotment ($4$ ticks).
    5. Newly arrived processes arriving at $t_{\text{boost}}$ enter the tail of Q0 (tie-broken by lowest PID).
- **Quantum-Boundary Arrival Ordering**:
  - Newly arrived processes enter Q0 **before** a demoted/requeued process is considered for lower-level execution.
- **Tie-Breaking**:
  - Simultaneous arrivals at the same simulated tick are ordered by `arrival_time` ascending, breaking ties by lowest `pid`.
- **Educational Simulation Notice**:
  - SmartShell does not control Linux's real CPU scheduler. MLFQ is modeled as a discrete integer mathematical simulation in user space.

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

---

## 7. Interactive Shell Command: `schedule`

SmartShell integrates the educational CPU scheduling laboratory into the interactive command line via the built-in `schedule` command.

### 7.1 Command Syntax & Options

```text
schedule <algorithm> [args]
schedule custom
schedule compare [demo [quantum]]
schedule help
```

Running `schedule` with no arguments or `schedule help` prints the usage summary and lists all supported algorithms.

#### Supported Subcommands:
- `schedule fcfs`: Non-preemptive First-Come, First-Served simulation on demo workload.
- `schedule sjf`: Non-preemptive Shortest Job First simulation on demo workload.
- `schedule rr [quantum]`: Preemptive Round Robin simulation on demo workload.
  - The `[quantum]` parameter is optional and defaults to `4`.
  - Allowed bounds: `1` to `100` simulated ticks.
  - Non-numeric or out-of-range quantum values return an error message without crashing or running the simulation.
- `schedule priority`: Non-preemptive static priority scheduling on demo workload.
- `schedule mlfq`: Multilevel Feedback Queue simulation on demo workload (3 priority levels: Q0 quantum 4, Q1 quantum 8, Q2 FCFS; global priority boost every 200 ticks).
- `schedule custom`: Interactively prompts for custom process count, arrival times, burst times, and priorities, then runs the selected algorithm or compares all algorithms.
- `schedule compare [demo [quantum]]`: Executes all 5 scheduling algorithms on the same workload and presents a unified performance comparison table. Can use the built-in demo workload or prompt for a custom workload.
- `schedule help`: Displays command usage, algorithm descriptions, and options.

### 7.2 Built-in Demo Workload

To provide a consistent, repeatable comparison across all scheduling policies, the `schedule` command runs against a carefully designed 5-process virtual workload:

| Virtual PID | Arrival Time | Burst Time | Priority | Description / Role |
| :---: | :---: | :---: | :---: | :--- |
| **P1** | 0 | 8 | 3 | Initial CPU-bound process; establishes baseline execution |
| **P2** | 1 | 4 | 1 | High-priority process arriving shortly after P1 |
| **P3** | 3 | 9 | 4 | Longest-running process; demonstrates quantum expiration and demotion |
| **P4** | 5 | 2 | 2 | Short interactive process; highlights SJF/MLFQ responsiveness |
| **P5** | 26 | 4 | 2 | Delayed arrival creating an explicit idle gap from tick 23 to 26 |

#### Why This Workload Was Chosen:
1. **Staggered Arrivals**: P2, P3, and P4 arrive while P1 is executing. This exposes ready queue management, queue reordering, and preemption differences.
2. **Burst Diversity**: Bursts range from very short (P4: 2) to long (P3: 9), demonstrating SJF efficiency and RR time-slicing.
3. **Priority Range**: Priorities range from 1 (highest) to 4 (lowest), demonstrating non-preemptive priority dispatching.
4. **Explicit Idle Interval**: With P1–P4 finishing by tick 23 and P5 arriving at tick 26, the CPU experiences 3 idle ticks ($[23, 26]$), verifying idle state handling across all algorithms.
5. **Distinct Execution Schedules**: Every algorithm produces a visibly different execution sequence and metric profile on this workload:
   - **FCFS**: `P1 (8) -> P2 (4) -> P3 (9) -> P4 (2) -> IDLE (3) -> P5 (4)`
   - **SJF**: `P1 (8) -> P4 (2) -> P2 (4) -> P3 (9) -> IDLE (3) -> P5 (4)`
   - **Priority**: `P1 (8) -> P2 (4) -> P4 (2) -> P3 (9) -> IDLE (3) -> P5 (4)`
   - **RR (Q=4)**: `P1 (4) -> P2 (4) -> P3 (4) -> P1 (4) -> P4 (2) -> P3 (5) -> IDLE (3) -> P5 (4)`
   - **MLFQ**: `P1 (4) -> P2 (4) -> P3 (4) -> P4 (2) -> P1 (4) -> P3 (5) -> IDLE (3) -> P5 (4)`

### 7.3 Output Format

The output of `schedule` consists of four cleanly formatted sections:
1. **Header & Parameters**: Identifies the algorithm and any algorithm-specific parameters (e.g., RR quantum).
2. **Workload Table**: Lists the virtual processes with their arrival times, burst times, and priorities.
3. **Gantt Chart**: Visual textual timeline with process blocks and exact time boundary markers.
4. **Process Metrics Table**: Detailed per-process statistics ($AT$, $BT$, $PR$, Start Time $ST$, Completion Time $CT$, Waiting Time $WT$, Turnaround Time $TAT$, Response Time $RT$).
5. **Summary Metrics**: System-wide performance metrics (Total Elapsed Time, Busy Time, Idle Time, CPU Utilization %, Average WT, Average TAT, Average RT).

### 7.4 Example Invocations and Sample Output

#### Example 1: `schedule fcfs`

```text
smartshell> schedule fcfs
SmartShell CPU Scheduling Simulation (User-Space Lab)
Algorithm:  First-Come, First-Served (FCFS)

Workload:
  PID  Arrival  Burst  Priority
    1        0      8         3
    2        1      4         1
    3        3      9         4
    4        5      2         2
    5       26      4         2

Gantt Chart:
| P1 | P2 | P3 | P4 | IDLE | P5 |
0    8    12   21   23     26   30

Process Metrics:
  PID   AT   BT   PR   ST   CT   WT  TAT   RT
    1    0    8    3    0    8    0    8    0
    2    1    4    1    8   12    7   11    7
    3    3    9    4   12   21    9   18    9
    4    5    2    2   21   23   16   18   16
    5   26    4    2   26   30    0    4    0

Summary:
  Total Elapsed Time:      30
  Total Busy Time:         27
  Total Idle Time:         3
  CPU Utilization:         90.00%
  Average Waiting Time:    6.40
  Average Turnaround Time: 11.80
  Average Response Time:   6.40
```

#### Example 2: `schedule rr 4`

```text
smartshell> schedule rr 4
SmartShell CPU Scheduling Simulation (User-Space Lab)
Algorithm:  Round Robin (RR)
RR Quantum: 4

Workload:
  PID  Arrival  Burst  Priority
    1        0      8         3
    2        1      4         1
    3        3      9         4
    4        5      2         2
    5       26      4         2

Gantt Chart:
| P1 | P2 | P3 | P1 | P4 | P3 | IDLE | P5 |
0    4    8    12   16   18   23     26   30

Process Metrics:
  PID   AT   BT   PR   ST   CT   WT  TAT   RT
    1    0    8    3    0   16    8   16    0
    2    1    4    1    4    8    3    7    3
    3    3    9    4    8   23   11   20    5
    4    5    2    2   16   18   11   13   11
    5   26    4    2   26   30    0    4    0

Summary:
  Total Elapsed Time:      30
  Total Busy Time:         27
  Total Idle Time:         3
  CPU Utilization:         90.00%
  Average Waiting Time:    6.60
  Average Turnaround Time: 12.00
  Average Response Time:   3.80
```

#### Example 3: `schedule mlfq`

```text
smartshell> schedule mlfq
SmartShell CPU Scheduling Simulation (User-Space Lab)
Algorithm:  Multilevel Feedback Queue (MLFQ)

Workload:
  PID  Arrival  Burst  Priority
    1        0      8         3
    2        1      4         1
    3        3      9         4
    4        5      2         2
    5       26      4         2

Gantt Chart:
| P1 | P2 | P3 | P4 | P1 | P3 | IDLE | P5 |
0    4    8    12   14   18   23     26   30

Process Metrics:
  PID   AT   BT   PR   ST   CT   WT  TAT   RT
    1    0    8    3    0   18   10   18    0
    2    1    4    1    4    8    3    7    3
    3    3    9    4    8   23   11   20    5
    4    5    2    2   12   14    7    9    7
    5   26    4    2   26   30    0    4    0

Summary:
  Total Elapsed Time:      30
  Total Busy Time:         27
  Total Idle Time:         3
  CPU Utilization:         90.00%
  Average Waiting Time:    6.20
  Average Turnaround Time: 11.60
  Average Response Time:   3.00
```

### 7.5 Educational Simulation Disclaimer

> [!IMPORTANT]
> The `schedule` command is purely a **user-space educational simulation**.
> - It does **not** alter, replace, or interact with Linux's real kernel CPU scheduler (such as CFS/EEVDF).
> - It does **not** spawn real Linux processes (`fork()`, `clone()`, `exec()`), send OS signals (`kill()`, `SIGSTOP`, `SIGCONT`), or manipulate terminal control groups (`tcsetpgrp()`).
> - It does **not** use real-time sleep calls (`sleep()`, `nanosleep()`, `usleep()`) to advance time; virtual time is represented as deterministic discrete integers and computed mathematically.
> - Real shell jobs and background pipelines executed by SmartShell continue to be scheduled solely by the Linux operating system kernel.

---

## 8. Custom Scheduling Workloads (`schedule custom`)

In SmartShell 2.0 (Phase 10), users can define arbitrary virtual workloads interactively to experiment with custom process configurations:

```text
smartshell> schedule custom
=== SmartShell Custom Workload Builder ===
Enter number of processes (1-32): 3

--- Process 1 (P1) ---
  Arrival time (>= 0): 0
  Burst time (> 0): 5
  Priority (>= 0, lower=higher priority): 2

--- Process 2 (P2) ---
  Arrival time (>= 0): 1
  Burst time (> 0): 3
  Priority (>= 0, lower=higher priority): 1

--- Process 3 (P3) ---
  Arrival time (>= 0): 2
  Burst time (> 0): 4
  Priority (>= 0, lower=higher priority): 3

Select algorithm to execute:
  1) First-Come, First-Served (FCFS)
  2) Shortest Job First (SJF)
  3) Round Robin (RR)
  4) Priority (Non-preemptive)
  5) Multilevel Feedback Queue (MLFQ)
  6) Compare all algorithms
Choose [1-6]: 6
```

### 8.1 Input Validation Rules

All interactive input is processed through strict validation:
- **Process Count ($N$)**: Bounded by `1` to `SCHED_MAX_CUSTOM_PROCESSES` (`32`). Values outside this range, non-numeric strings, or integer overflows are rejected with informative error messages.
- **Process Identifier ($PID$)**: Assigned automatically as sequential positive integers ($1, 2, \dots, N$), rendered as `P1, P2, ... PN`.
- **Arrival Time ($AT$)**: Must be a non-negative integer ($0 \le AT \le 100{,}000$). Negative arrival times are rejected.
- **Burst Time ($BT$)**: Must be a strictly positive integer ($1 \le BT \le 100{,}000$). Zero and negative burst times are rejected.
- **Priority ($PR$)**: Must be a non-negative integer ($0 \le PR \le 100{,}000$). Lower values indicate higher priority.
- **Integer Overflow**: All numerical conversions use `strtol` with `errno == ERANGE` checking and strict end-pointer validation.
- **EOF Handling**: If standard input encounters EOF (e.g., Ctrl+D or end of redirected input stream), the custom builder immediately cancels execution cleanly without memory leaks or crashes.
- **Re-prompting**: Invalid inputs trigger a descriptive warning and re-prompt the user without aborting the entire workload definition.

---

## 9. Algorithm Comparison (`schedule compare`)

The `schedule compare` command executes all five scheduling algorithms (FCFS, SJF, Round Robin, Priority, and MLFQ) against an identical workload and generates a unified side-by-side performance comparison table.

### 9.1 Usage

```text
schedule compare
schedule compare demo
schedule compare demo <quantum>
schedule compare <quantum>
```

- When run with `demo`, it immediately evaluates the built-in 5-process benchmark workload.
- When run interactively without arguments, it offers the choice between the demo workload and creating a custom workload, followed by an optional Round Robin quantum configuration (default: 4 ticks, bounds: 1 to 100 ticks).
- Command-line arguments allow specifying the Round Robin quantum directly (e.g. `schedule compare demo 2`).

### 9.2 Independent Workload Isolation

To ensure strict experimental validity, each algorithm receives an independent, unmutated copy of the initial workload. The comparison engine (`sched_compare_all`) guarantees:
1. Every algorithm starts from the exact same arrival times, burst requirements, and priorities.
2. Mutable runtime metrics (such as `remaining_time`, `start_time`, `completion_time`) are computed independently without cross-algorithm side effects.

### 9.3 Comparison Table Output Format

```text
=======================================================================================
                         SmartShell Algorithm Comparison Table                         
=======================================================================================
Workload: 5 processes | Round Robin Quantum: 4 ticks
MLFQ Policy: Q0 (RR Q=4), Q1 (RR Q=8), Q2 (FCFS) | Starvation Boost: 200 ticks
---------------------------------------------------------------------------------------
Algorithm        Avg Wait  Avg Turnaround  Avg Response   CPU Util  Elapsed  Busy  Idle
---------------------------------------------------------------------------------------
FCFS                 6.40           11.80          6.40     90.00%       30    27     3
SJF                  4.60           10.00          4.60     90.00%       30    27     3
Round Robin (Q=4)    6.60           12.00          3.80     90.00%       30    27     3
Priority             5.80           11.20          5.80     90.00%       30    27     3
MLFQ                 6.20           11.60          3.00     90.00%       30    27     3
---------------------------------------------------------------------------------------
Metric Key:
  Avg Wait:       Mean waiting time in ready queue (lower is better)
  Avg Turnaround: Mean time from arrival to completion (lower is better)
  Avg Response:   Mean time from arrival to first execution (lower is better)
  CPU Util:       Percentage of elapsed time the CPU executed virtual processes
=======================================================================================
```

---

## 10. Multi-Row Wrapped Gantt Charts

In earlier versions, execution timelines were rendered on a single horizontal line. For long workloads, small time quantums, or narrow terminals, lines would wrap awkwardly and break the visual alignment between process bars and numerical time markers.

SmartShell 2.0 introduces multi-row wrapped Gantt rendering:
- **API Function**:
  ```c
  char *sched_render_gantt_wrapped(const sched_result_t *result, int max_cols);
  int sched_render_gantt_wrapped_buf(const sched_result_t *result, char *buf, size_t size, int max_cols);
  ```
- **Default Column Limit**: `SCHED_GANTT_DEFAULT_MAX_COLS = 76` characters.
- **Row Partitioning**: The renderer calculates the exact character width of each slice box and boundary marker. When a slice would exceed `max_cols`, a clean row break is inserted.
- **Vertical Time Alignment**: Every row begins with its start time marker directly aligned beneath its starting vertical bar `|`, and concludes with its row-ending boundary time marker.
- **Large Timestamp Support**: Handles multi-digit simulated clock ticks gracefully by expanding label spacing where necessary to maintain strictly aligned vertical columns.
- **Backward Compatibility**: `sched_render_gantt()` and `sched_render_gantt_buf()` automatically forward to the wrapped implementation with `max_cols = 76`, ensuring existing callers and test cases remain 100% compatible.

