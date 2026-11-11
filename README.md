# SmartShell

**SmartShell** is an educational, POSIX-compliant Linux shell written in C11. Developed for operating systems coursework, it demonstrates core systems programming concepts including command parsing, pipeline execution, file redirection, POSIX process-group job control, terminal ownership management, `/proc` virtual filesystem inspection, and a discrete-time user-space CPU scheduling simulator.

---

## Features

SmartShell provides a complete, modern academic implementation of fundamental operating system principles:

- **Interactive REPL**: Clean command-line interface with prompt (`smartshell> `), robust EOF/signal handling, and line editing.
- **Robust Command Parsing**:
  - Quoting semantics: Single quotes (`'...'`) for raw literals, double quotes (`"..."`) for space preservation.
  - Backslash escape handling outside quotes.
  - Full syntax error detection and reporting.
- **I/O Redirections**:
  - Input redirection (`< file`).
  - Output truncation redirection (`> file`).
  - Output append redirection (`>> file`).
  - Standard error redirection (`2> file`).
- **Arbitrary Pipelines**:
  - Multistage pipelines (`cmd1 | cmd2 | cmd3 | ...`).
  - Coordinated inter-process communication via UNIX pipes (`pipe()`, `dup2()`).
  - Pipelines with mixed redirections and built-ins.
- **POSIX Job Control & Terminal Management**:
  - Independent process groups for every pipeline (`setpgid`).
  - Foreground terminal handoff and reclamation (`tcsetpgrp`).
  - Background execution operator (`&`).
  - Signal management: `SIGINT` (Ctrl+C), `SIGTSTP` (Ctrl+Z), `SIGCHLD`, `SIGTTIN`, `SIGTTOU`.
  - Asynchronous child reaping with an async-signal-safe self-pipe pattern.
  - Job management commands: `jobs`, `fg %N`, `bg %N`, and `kill`.
- **Built-in Commands**:
  - `cd` (supports `$HOME` and `cd -`), `pwd`, `exit`, `quit`, `help`.
  - Fully integrated with I/O redirections.
- **Linux `/proc` Process Monitoring & System Explorer**:
  - Full process hierarchy tree visualization (`process-tree` and `process-tree <pid>`) with loop and cycle protection.
  - Interactive live watch mode (`monitor --watch`) refreshing every second with terminal restoration on exit.
  - System-wide process explorer with dynamic CPU and memory sorting (`monitor --sort cpu`, `monitor --sort memory`).
  - Detailed single-process inspection (`monitor <pid>`) and shell-managed job accounting.
  - Host hardware, kernel, memory, and shell runtime environment reporting (`sysinfo`).
- **Educational CPU Scheduling Laboratory**:
  - Pure user-space discrete integer-time simulator.
  - Implements **FCFS**, **SJF**, **Round Robin (RR)**, non-preemptive **Priority**, and **Multilevel Feedback Queue (MLFQ)**.
  - Interactive custom workload builder (`schedule custom`) with bounded validation for process counts (1-32), arrival times, bursts, and priorities.
  - Side-by-side algorithm comparison table (`schedule compare`) evaluating all five policies on identical workloads with configurable Round Robin quantum.
  - Multi-row wrapped textual Gantt chart rendering with aligned time markers and narrow terminal support.
  - Calculation of turnaround, waiting, response times, and CPU utilization.

---

## Architecture

SmartShell follows a clean, modular design:

| Subsystem | Source Files | Description |
| :--- | :--- | :--- |
| **Shell REPL** | `src/main.c`, `src/shell.c`, `include/shell.h` | Entry point, interactive event loop, prompt, and terminal mode management. |
| **Parser** | `src/parser.c`, `include/parser.h` | Tokenizer, quote handling, escape resolution, and AST generation. |
| **Executor** | `src/executor.c`, `include/executor.h` | Pipeline creation, fork/exec, pipe routing, and exit status collection. |
| **Job Control** | `src/job.c`, `src/signals.c`, `include/job.h`, `include/signals.h` | Job table, process groups, terminal arbitration, and async-signal-safe self-pipe. |
| **Redirection** | `src/redirect.c`, `include/redirect.h` | Descriptor manipulation, file opening modes, and `dup2` management. |
| **Built-ins** | `src/builtins.c`, `include/builtins.h` | Implementation of `cd`, `pwd`, `jobs`, `fg`, `bg`, `kill`, `help`, `exit`, etc. |
| **Process Monitor** | `src/proc_monitor.c`, `include/proc_monitor.h` | Linux `/proc` filesystem reader, CPU delta sampler, and memory parser. |
| **Scheduler** | `src/scheduler.c`, `include/scheduler.h` | Discrete-event virtual CPU scheduling algorithms, metrics, and Gantt renderer. |
| **Utilities** | `src/utils.c`, `include/utils.h` | Safe memory allocators (`xmalloc`, `xcalloc`, `xstrdup`) and string helpers. |

---

## Build

Compile SmartShell using the provided Makefile:

```bash
# Standard release build (-std=c11 -Wall -Wextra -Werror -pedantic)
make

# Debug build with symbols (-g -O0 -DDEBUG)
make clean && make debug

# Sanitizer build (AddressSanitizer + UndefinedBehaviorSanitizer)
make clean && make asan
```

---

## Run

Launch the shell binary from the project root:

```bash
./bin/smartshell
```

To run in batch mode or pass commands directly:

```bash
echo "schedule fcfs" | ./bin/smartshell
```

---

## Example Commands

### 1. Navigation and Basic Commands
```sh
smartshell> pwd
/home/user/smartshell

smartshell> cd /tmp
smartshell> pwd
/tmp
smartshell> cd -
/home/user/smartshell
```

### 2. Pipelines and Redirection
```sh
smartshell> echo "apple" > fruits.txt
smartshell> echo "banana" >> fruits.txt
smartshell> cat < fruits.txt | grep banana
banana

smartshell> cat fruits.txt | wc -l > count.txt
smartshell> cat count.txt
2
```

### 3. Job Control
```sh
smartshell> sleep 30 &
[1] 14205

smartshell> jobs
[1]  Running    sleep 30 &

smartshell> fg %1
sleep 30
^Z
[1]+  Stopped  sleep 30

smartshell> bg %1
[1]+  Running  sleep 30 &

smartshell> kill %1
[1]+  Terminated  sleep 30 &
```

### 4. Linux `/proc` Monitoring & System Explorer
```sh
# System overview and managed jobs
smartshell> monitor

# Detailed statistics for a specific process
smartshell> monitor 1
Process Information (PID 1):
  Command:            systemd
  State:              S (Sleeping)
  Parent PID (PPID):  0
  Process Group ID:   1
  Threads:            1
  Virtual Memory:     168.4 MB (176586752 bytes)
  Resident Memory:    13.2 MB (13844480 bytes)
  User CPU Time:      12 ticks (0.12s)
  System CPU Time:    25 ticks (0.25s)
  CPU Utilization:    0.0% (sampled over 100ms)

# Sort system processes by CPU utilization
smartshell> monitor --sort cpu

# Sort system processes by physical memory (RSS)
smartshell> monitor --sort memory

# Live full-screen monitor refreshing every second (press 'q' to quit)
smartshell> monitor --watch

# Display complete process hierarchy tree
smartshell> process-tree

# Display subtree rooted at specific PID
smartshell> process-tree 1

# Comprehensive real-time system and runtime explorer
smartshell> sysinfo
SmartShell System Information
=============================
Operating System:     Linux 6.6.137+
Kernel Version:       #42~24.04.1-Ubuntu SMP PREEMPT_DYNAMIC
Machine Architecture: x86_64
CPU Cores:            16 online
Total Memory:         15.31 GB (16056732 kB)
Available Memory:     10.69 GB (11210616 kB)
SmartShell PID:       35
Terminal Mode:        Interactive (/dev/pts/1, foreground PGID: 35)
/proc Accessibility:  Accessible (virtual filesystem mounted and readable)
```

### 5. CPU Scheduling Laboratory
```sh
# First-Come, First-Served
smartshell> schedule fcfs

# Shortest Job First
smartshell> schedule sjf

# Round Robin (default quantum 4)
smartshell> schedule rr

# Round Robin with custom quantum 2
smartshell> schedule rr 2

# Non-preemptive Priority scheduling
smartshell> schedule priority

# Multilevel Feedback Queue (3 priority levels, 200-tick boost)
smartshell> schedule mlfq

# Interactive custom workload builder
smartshell> schedule custom

# Algorithm comparison table (built-in demo workload)
smartshell> schedule compare demo

# Algorithm comparison table with custom quantum
smartshell> schedule compare demo 2

# Interactive comparison (choose demo vs custom workload)
smartshell> schedule compare

# Help and usage
smartshell> schedule help
```

---

## Scheduling Laboratory

SmartShell includes an interactive CPU scheduling laboratory designed for educational demonstration and analysis. It evaluates how different scheduling algorithms handle a diverse 5-process virtual workload containing varying arrival times, burst lengths, priorities, and an idle gap, or user-defined custom workloads:

- **FCFS**: Non-preemptive first-come, first-served execution.
- **SJF**: Shortest job first with tie-breaking by arrival time and PID.
- **Round Robin**: Time-sliced execution with custom quantum support ($1 \le Q \le 100$).
- **Priority**: Static priority ordering where smaller numbers denote higher priority.
- **MLFQ**: Three priority levels (Q0 quantum 4, Q1 quantum 8, Q2 FCFS) with inter-queue preemption and a global priority boost every 200 virtual ticks to eliminate starvation.
- **Custom Workloads (`schedule custom`)**: Interactive wizard allowing arbitrary process counts (1–32), arrival times, CPU bursts, and priorities with strict input validation.
- **Algorithm Comparison (`schedule compare`)**: Runs all 5 algorithms on identical unmutated workload copies and produces a side-by-side performance table.

The simulator outputs a text-based Gantt chart with precise tick markers and multi-row terminal wrapping, detailed per-process metrics (Waiting Time, Turnaround Time, Response Time), and system summary statistics (CPU Utilization %, Average WT, Average TAT, Average RT).

---

## Testing

SmartShell contains a comprehensive, multi-tiered test suite:

```bash
# 1. Run all unit tests (145 unit tests across 8 suites)
make test

# 2. Run unit tests under AddressSanitizer and UndefinedBehaviorSanitizer
make test-asan

# 3. Run all integration tests and interactive PTY tests
make test-integration
```

All 145 unit tests, 92 integration tests, and 14 pseudo-terminal (PTY) job-control tests pass with 0 failures and zero sanitizer errors.

---

## Limitations

> [!IMPORTANT]
> **SmartShell does NOT replace, alter, or interact with the Linux kernel's real CPU scheduler (such as CFS or EEVDF).**
>
> The scheduling laboratory is strictly an **educational user-space simulation**.
> - It does not schedule real Linux processes, threads, or shell jobs.
> - It does not invoke `fork()`, `exec()`, `kill()`, or `tcsetpgrp()` to simulate scheduling decisions.
> - It does not use wall-clock delays (`sleep()`, `nanosleep()`); virtual time advances through discrete mathematical simulation.
> - All actual shell processes executed by SmartShell are scheduled exclusively by the underlying Linux operating system kernel.
