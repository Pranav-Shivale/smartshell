# SmartShell /proc Process Monitoring (Phase 7)

SmartShell provides a dedicated `/proc`-based process and system monitoring subsystem.

---

## 1. Overview & Architecture

The process monitoring module (`include/proc_monitor.h`, `src/proc_monitor.c`) inspects the Linux virtual filesystem `/proc` to gather system and process metrics.

### Key Architectural Separation:
- **SmartShell Job Table**: Tracks shell-managed jobs, pipelines, process groups, and interactive states (`Running`, `Stopped`, `Done`).
- **Linux `/proc` Subsystem**: Reads kernel-managed OS-level metrics (CPU ticks, memory pages, threads, kernel states).
- **Decoupled Design**: The `/proc` monitoring logic is completely separated from the parser, executor, signals, and scheduler layers.

---

## 2. Process Information Collection

For any given process ID (`PID`), SmartShell reads:
- `/proc/<pid>/stat`: Kernel process statistics
- `/proc/<pid>/status`: Human-readable process status and security attributes

### Attributes Collected:
- **PID**: Process identifier.
- **Command / Name**: Process name. Handled cleanly even if the command contains spaces or nested parentheses (e.g., `(worker (bg) thread)`).
- **Process State**: Parsed from state character and mapped to standard states:
  - `R` — Running
  - `S` — Sleeping (interruptible wait)
  - `D` — Disk Sleep (uninterruptible wait)
  - `Z` — Zombie
  - `T` — Stopped (job control or signal)
  - `t` — Tracing stop
  - `X` / `x` — Dead
  - `I` — Idle (kernel thread)
- **PPID**: Parent process ID.
- **PGID**: Process group ID.
- **SID**: Session ID.
- **UID**: User identifier.
- **Thread Count**: Number of threads in the process.
- **Virtual Memory (`vsize`)**: Total virtual address space in bytes and human-readable format.
- **Resident Set Size (`RSS`)**: Physical memory in pages converted to bytes via `sysconf(_SC_PAGESIZE)`.
- **CPU Time**:
  - `utime`: User CPU time in clock ticks and seconds.
  - `stime`: System (kernel) CPU time in clock ticks and seconds.
- **CPU Utilization (%)**: Sampled dynamically over a defined window.

---

## 3. System CPU & Memory Information

### System CPU (`/proc/stat`):
Parses the aggregate `cpu` line:
```
cpu  user nice system idle iowait irq softirq steal
```
All counters use 64-bit unsigned integers (`unsigned long long`).

### CPU Utilization Calculation:
To avoid inaccuracies and busy loops, CPU utilization is calculated from two snapshots separated by a 100ms interval (`nanosleep`):

$$\Delta_{\text{total}} = \text{total}_2 - \text{total}_1$$
$$\Delta_{\text{idle}} = \text{idle\_all}_2 - \text{idle\_all}_1$$
$$\text{CPU Utilization} = 100 \times \frac{\Delta_{\text{total}} - \Delta_{\text{idle}}}{\Delta_{\text{total}}}$$

If $\Delta_{\text{total}} \le 0$, utilization safely evaluates to $0.0\%$.

### System Memory (`/proc/meminfo`):
Parses:
- `MemTotal`: Total physical RAM.
- `MemFree`: Unused RAM.
- `MemAvailable`: Estimate of RAM available for starting new applications.
- `Buffers` / `Cached`: In-memory filesystem caches.
- `SwapTotal` / `SwapFree`: Swap space usage.

---

## 4. Process CPU Utilization Formula

Process CPU usage is measured over a 100ms wall-clock window:

1. Snapshot $t_1$: Record $T_1 = \text{utime}_1 + \text{stime}_1$ and monotonic timestamp $W_1$.
2. Sleep 100ms via `nanosleep()`.
3. Snapshot $t_2$: Record $T_2 = \text{utime}_2 + \text{stime}_2$ and monotonic timestamp $W_2$.
4. Compute:
   $$\Delta_{\text{ticks}} = T_2 - T_1$$
   $$\Delta_{\text{proc\_sec}} = \frac{\Delta_{\text{ticks}}}{\text{sysconf}(\_SC\_CLK\_TCK)}$$
   $$\Delta_{\text{wall\_sec}} = W_2 - W_1$$
   $$\text{Process CPU \%} = \frac{\Delta_{\text{proc\_sec}}}{\Delta_{\text{wall\_sec}}} \times 100$$

If the process terminates or disappears during sampling, the monitor reports the termination cleanly without crashing or leaking memory.

---

## 5. The `monitor` Built-in Command

### 1. Summary Mode: `monitor`
Displays system overview and active SmartShell jobs:
```
smartshell> monitor
--- System Overview ---
CPU Utilization:  3.2%
Memory:           Used: 4.88 GB / 15.31 GB (31.9%) | Free: 2.84 GB | Available: 10.66 GB
Swap:             Used: 0 B / 2.00 GB | Free: 2.00 GB

--- SmartShell Managed Processes ---
JID   PID     STATE        PGID    MEM        COMMAND
[1]   35035   Sleeping     35035   2.1 MB     sleep
```

### 2. Process Details Mode: `monitor <pid>`
Inspects any valid PID on the system:
```
smartshell> monitor 1
Process Information (PID 1):
  Command:            systemd
  State:              S (Sleeping)
  Parent PID (PPID):  0
  Process Group ID:   1
  Session ID:         1
  User ID (UID):      0
  Threads:            1
  Virtual Memory:     23.4 MB (24489984 bytes)
  Resident Memory:    14.7 MB (15400960 bytes)
  User CPU Time:      202 ticks (2.02s)
  System CPU Time:    163 ticks (1.63s)
  CPU Utilization:    0.0% (sampled over 100ms)
```

### 3. Error Handling:
- Nonexistent PID:
  ```
  smartshell> monitor 999999
  smartshell: monitor: process 999999 not found
  ```
- Invalid PID string:
  ```
  smartshell> monitor abc
  smartshell: monitor: abc: invalid process ID
  ```

---

## 6. Limitations of `/proc` Monitoring

- **Point-in-Time Sampling**: Metrics represent snapshots at sample time. Short-lived bursts occurring between samples are not captured.
- **Process Race Conditions**: A process can terminate at any point between reading `/proc/<pid>/stat` and `/proc/<pid>/status`. The subsystem handles `ENOENT`/`ESRCH` gracefully.
- **Permissions**: Reading certain fields in `/proc/<pid>/` for processes owned by other users or the root user may return partial information or require elevated permissions.
