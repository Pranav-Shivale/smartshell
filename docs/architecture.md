# SmartShell Architecture and Subsystem Design

SmartShell is an educational, POSIX-compliant Linux shell written in C11. It combines traditional UNIX command-line features (pipelines, redirections, process group job control) with an integrated `/proc` process monitor and a user-space CPU scheduling laboratory.

---

## 1. High-Level Architecture

SmartShell is structured into cleanly decoupled, modular components:

```
                    +--------------------------------+
                    |            main.c              |
                    +---------------+----------------+
                                    |
                    +---------------+----------------+
                    |            shell.c             |
                    |         (REPL Engine)          |
                    +---+--------+--------+--------+-+
                        |        |        |        |
        +---------------+   +----+----+   |   +----+----------+
        |                   |         |   |   |               |
+-------v-------+   +-------v---+     |   | +-v-----------+ + v------------+
|   parser.c    |   | builtins.c|     |   | |job.c        | |proc_monitor.c|
| Tokenizer &   |   | cd, pwd,  |     |   | |signals.c    | |/proc reader &|
| AST Generator |   | exit, etc.|     |   | |Job Control  | |CPU sampler   |
+-------+-------+   +-----+-----+     |   | +-------------+ +--------------+
        |                 |           |   |
        +--------+--------+           |   |   +---------------+
                 |                    |   +-->| scheduler.c   |
                 v                    |       | CPU Simulator |
        +----------------+            |       +---------------+
        |   executor.c   |<-----------+
        | Pipelines,     |
        | Redirections,  |
        | Process Groups |
        +----------------+
```

---

## 2. Core Subsystems

### 2.1 REPL & Terminal Controller (`shell.c`, `main.c`)
- **Initialization**: Configures the job table, signal handlers, and interactive terminal state (`tcgetattr`, process group assignment).
- **Event Loop**:
  1. Reaps terminated and stopped children asynchronously (`signals_process_chld_events`).
  2. Notifies the user of completed background jobs and reclaims job table slots (`job_notify_and_cleanup`).
  3. Displays prompt (`smartshell> `) and reads line input (up to 4096 bytes).
  4. Parses commands and dispatches to built-ins or pipeline executor.
- **Clean Teardown**: Restores original terminal modes, cleans up self-pipes, and releases allocated job tables.

### 2.2 Parser & AST Generator (`parser.c`, `parser.h`)
- Handles quote processing (`'...'` single quotes preserve literal characters; `"..."` double quotes preserve spaces).
- Resolves backslash escapes outside quotes.
- Parses input redirection (`<`), output redirection (`>`, `>>`), and standard error redirection (`2>`).
- Splits multistage pipelines (`|`) into linked `command_t` nodes.
- Detects the trailing background control operator `&`.
- Strictly validates syntax errors (leading/trailing pipes, missing redirect targets, empty commands).

### 2.3 Command & Pipeline Executor (`executor.c`, `executor.h`)
- Implements arbitrary-length pipelines ($A \mid B \mid C \dots$).
- Creates inter-process communication pipes with `pipe()`.
- Forks child processes and establishes **POSIX process groups**:
  - The first child in a pipeline becomes the process group leader (`setpgid(0, 0)`).
  - All subsequent children join the leader's group (`setpgid(pid, leader_pgid)`).
  - Race conditions between parent and child are avoided by calling `setpgid()` in both processes.
- Manages terminal ownership via `tcsetpgrp()`:
  - Foreground jobs are given terminal control; the shell reclaims control upon job completion or suspension.
  - Background jobs never seize terminal control.
- Redirects standard streams via `dup2()` and safely closes pipe descriptors.

### 2.4 Job Control & Signals (`job.c`, `signals.c`, `job.h`, `signals.h`)
- **Job Table**: Tracks up to 64 active jobs, each containing up to 16 pipeline processes.
- **Async-Signal-Safe SIGCHLD**:
  - `sigchld_handler` strictly invokes `waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)` and writes to a non-blocking **self-pipe**.
  - Events are buffered in a lock-free circular ring buffer without calling non-reentrant functions like `malloc` or `printf`.
- **Self-Pipe Mechanism**: Wakes up `wait_for_job()` via `poll()`, completely avoiding signal-handler race conditions.
- **User Job Control**: Full implementation of `jobs`, `fg %N`, `bg %N`, and `kill %N` or `kill -SIGNAL PID/%N`.

### 2.5 Built-in Commands (`builtins.c`, `builtins.h`)
- `cd [dir|-]`: Changes directory (supports `$HOME` default, `~`, and `cd -` for `$OLDPWD`).
- `pwd`: Outputs current working directory via `getcwd()`.
- `exit` / `quit [code]`: Cleanly terminates shell session with optional status.
- `help`: Interactive documentation of available commands.
- `jobs`: Lists active background and stopped jobs.
- `fg [job_spec]`: Resumes or brings job to foreground.
- `bg [job_spec]`: Resumes stopped job in background.
- `kill [-sig] <pid|job_spec>`: Sends signals to processes or process groups.
- `monitor [pid]`: Dispatches to `/proc` monitoring engine (supports `--watch` and `--sort cpu|memory`).
- `process-tree [pid]`: Displays `/proc` process hierarchy tree with cycle prevention.
- `sysinfo`: Displays real hardware, OS, memory, and shell runtime environment metrics.
- `schedule <algo> [args]`: Dispatches to CPU scheduling simulation laboratory (supports `custom` workloads and `compare` table).

### 2.6 Linux `/proc` Process Monitor & System Explorer (`proc_monitor.c`, `proc_monitor.h`)
- **Inspection Engine**: Parses virtual filesystem `/proc` without external utilities like `ps` or `top`.
- **Process Inspection**: Reads `/proc/<pid>/stat` and `/proc/<pid>/status`, correctly parsing commands with spaces or nested parentheses.
- **System Sampling**: Computes accurate CPU utilization from `/proc/stat` using a 100ms delta sampling window.
- **Process Hierarchy Tree**: Scans `/proc`, models parent-child relationships via `PID` and `PPID`, prevents infinite loops with visited tracking, and displays clean ASCII hierarchy trees.
- **Live Process Monitoring**: Refreshes every 1 second in non-blocking raw terminal mode with interactive 'q'/'c'/'m' keys, automatic terminal dimension adjustment, and safe terminal restoration on exit or `SIGINT`.
- **System Information Explorer**: Inspects host kernel, architecture, active cores, memory accounting, shell process group, and `/proc` accessibility using POSIX interfaces.

### 2.7 Educational CPU Scheduling Simulator (`scheduler.c`, `scheduler.h`)
- **User-Space Mathematical Model**: Pure discrete integer-time simulation of CPU scheduling algorithms.
- **Algorithms Supported**:
  - Non-preemptive First-Come, First-Served (**FCFS**).
  - Non-preemptive Shortest Job First (**SJF**).
  - Preemptive Round Robin (**RR**) with configurable quantum.
  - Non-preemptive static **Priority** scheduling.
  - Multilevel Feedback Queue (**MLFQ**) with 3 priority queues, quantum boundaries, and periodic starvation prevention boost.
- **Custom Workloads (`schedule custom`)**: Interactive builder supporting 1–32 processes with strict bounds validation on arrivals, bursts, and priorities.
- **Side-by-Side Comparison (`schedule compare`)**: Evaluates all 5 scheduling policies simultaneously on identical, unmutated workloads and produces a formatted comparative metrics table.
- **Deterministic Virtual Time**: No system sleep, threads, signals, or kernel scheduler modifications.
- **Presentation**: Textual Gantt chart rendering with aligned time boundaries, multi-row terminal wrapping, per-process metrics, and system summary statistics.

---

## 3. Concurrency, Signal Safety & Resource Management

- **Descriptor Leak Prevention**: All internal descriptors (e.g. self-pipe ends) use `FD_CLOEXEC`.
- **Signal Masking**: `SIGCHLD` is blocked during fork, job registration, and terminal handoff to prevent race conditions.
- **Memory Safety**: Rigorously verified with GCC AddressSanitizer (`-fsanitize=address,undefined`) with zero memory leaks, use-after-free, or out-of-bounds accesses.
- **Clean Teardown**: Upon shell termination, all open file descriptors, signal dispositions, and terminal modes are safely restored.
