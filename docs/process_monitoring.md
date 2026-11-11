# SmartShell Process & System Explorer (Phase 7 & Phase 11 Upgrade)

SmartShell provides a comprehensive `/proc`-based process and system monitoring subsystem with process tree visualization, live process monitoring, sorting, and real-time hardware/OS introspection.

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

### 3. Sorting Mode: `monitor --sort cpu` / `monitor --sort memory`
Displays system overview followed by the top system processes sorted dynamically by CPU percentage or memory usage (RSS):

```text
smartshell> monitor --sort cpu
--- System Overview ---
CPU Utilization:  4.2%
Memory:           Used: 4.88 GB / 15.31 GB (31.9%) | Free: 2.84 GB | Available: 10.66 GB

--- Process Explorer (sorted by CPU) ---
PID     COMMAND            STATE    CPU%        MEM       VIRT THREADS
1234    worker             R       45.2%    12.4 MB    32.1 MB       4
5678    smartshell         R        1.2%     4.1 MB    18.2 MB       1
1       systemd            S        0.0%    13.2 MB   168.4 MB       1
```

Supported flags:
- `monitor --sort cpu`, `monitor -s cpu`, `monitor --sort=cpu`: Sort by CPU usage descending.
- `monitor --sort memory`, `monitor -s mem`, `monitor --sort=memory`: Sort by resident memory (RSS) descending.

### 4. Live Watch Mode: `monitor --watch`
Enters an interactive full-screen live process monitor refreshing every 1 second:

```text
smartshell> monitor --watch
--- System Overview ---
CPU Utilization:  5.1%
Memory:           Used: 4.88 GB / 15.31 GB (31.9%) | Free: 2.84 GB | Available: 10.66 GB

--- Process Explorer (sorted by CPU) ---
PID     COMMAND            STATE    CPU%        MEM       VIRT THREADS
1234    worker             R       45.2%    12.4 MB    32.1 MB       4
5678    smartshell         R        1.2%     4.1 MB    18.2 MB       1
...

[Refresh: 1s | Sort: CPU | Press 'q' to quit, 'c' sort CPU, 'm' sort MEM]
```

Interactive Keybindings:
- `q` or `Q`: Immediately exit watch mode and cleanly restore original terminal settings.
- `c` or `C`: Switch sorting order to CPU usage.
- `m` or `M`: Switch sorting order to Memory (RSS).
- `Ctrl+C` (`SIGINT`): Handled safely; interrupts the watch loop, restores terminal attributes, and returns to shell prompt.
- Terminal Resizing (`SIGWINCH`): Automatically adjusts displayed row count based on terminal dimensions.

---

## 6. Process Tree Explorer (`process-tree`)

SmartShell reconstructs the full process hierarchy by traversing `/proc` and linking parent-child relationships via `PID` and `PPID`.

### Syntax:
```text
process-tree
process-tree <pid>
```

### Features:
1. **Full Process Tree**: When run without arguments, displays the entire hierarchy starting from root process(es) (e.g. PID 1 / init / systemd) and any unparented roots:
   ```text
   smartshell> process-tree
   1 systemd [S]
   |-- 124 systemd-journald [S]
   |-- 456 sshd [S]
   |   \-- 789 bash [S]
   |       \-- 1011 smartshell [R]
   \-- 2000 cron [S]
   ```
2. **Subtree Inspection**: When given a PID (`process-tree <pid>`), isolates and displays only the tree rooted at that specific process.
3. **Cycle and Loop Detection**: Uses visited tracking flags to eliminate duplicate visits and prevent infinite recursion in the presence of corrupted or cyclic process structures.
4. **Deterministic Ordering**: Sorts child processes by PID ascending for repeatable, structured display.
5. **Decoupled Operation**: Does not mutate or query SmartShell's internal job table.

---

## 7. System Information (`sysinfo`)

The `sysinfo` built-in inspects the host Linux system, hardware resources, `/proc` virtual filesystem, and shell runtime environment using standard POSIX and Linux system interfaces:

```text
smartshell> sysinfo
SmartShell System Information
=============================
Operating System:     Linux 6.6.137+
Kernel Version:       #42~24.04.1-Ubuntu SMP PREEMPT_DYNAMIC
Machine Architecture: x86_64
CPU Cores:            16 online
Total Memory:         15.31 GB (16056732 kB)
Available Memory:     10.69 GB (11210616 kB)
Free Memory:          7.70 GB (8077332 kB)
Swap Memory:          4.00 GB total / 4.00 GB free
SmartShell PID:       35
Parent PID (PPID):    33
Process Group (PGID): 33
Session ID (SID):     33
Terminal Mode:        Interactive (/dev/pts/1, foreground PGID: 33)
/proc Accessibility:  Accessible (virtual filesystem mounted and readable)
```

### Metrics Reported:
- **Operating System & Kernel**: Retrieved via `uname()` (`sysname`, `release`, `version`).
- **Machine Architecture**: Retrieved via `uname()` (`machine`).
- **CPU Cores**: Online and configured core counts via `sysconf(_SC_NPROCESSORS_ONLN)` and `sysconf(_SC_NPROCESSORS_CONF)`.
- **Memory Statistics**: Total physical memory, available memory, free memory, and swap space parsed directly from `/proc/meminfo`.
- **Shell Process Hierarchy**: SmartShell PID (`getpid()`), Parent PID (`getppid()`), Process Group ID (`getpgrp()`), and Session ID (`getsid()`).
- **Terminal Mode**: Reports whether the shell is operating in an interactive terminal or non-interactive batch/pipeline environment, along with device name and foreground PGID.
- **`/proc` Accessibility**: Validates accessibility of `/proc`, `/proc/stat`, and `/proc/meminfo`.

---

## 8. Limitations & Error Handling

- **Point-in-Time Sampling**: Metrics represent snapshots at sample time. Short-lived bursts occurring between samples are not captured.
- **Process Disappearance**: Processes may terminate between directory enumeration and reading `/proc/<pid>/stat`. The subsystem safely skips vanished processes without memory leaks or crashes.
- **Permissions**: Reading certain fields in `/proc/<pid>/` for processes owned by other users or the root user may return partial information or require elevated permissions.
