# SmartShell Job Control & Process Management (Phase 6)

SmartShell provides complete POSIX process-group job control, background execution, and job management.

---

## 1. Background Execution (`&`)

SmartShell recognizes an unquoted trailing `&` as a background control operator:

```sh
smartshell> sleep 30 &
[1] 12345
smartshell>
```

### Key Semantics:
- **Operator Placement**: `&` must appear as the trailing token of a command or pipeline. It is not passed as an argument to `execvp()`.
- **Quoted `&`**: `&` inside single quotes (`'&'`) or double quotes (`"&"`) remains literal.
- **Immediate Return**: The shell does not wait for background jobs or assign them the controlling terminal (`tcsetpgrp`). The shell immediately displays the prompt.
- **Pipelines**: A pipeline followed by `&` (e.g., `sleep 30 | cat &`) is registered as **one** logical background job sharing a single process group.
- **Syntax Validation**: Inputs such as `&`, `sleep 10 &&`, `sleep 10 & foo`, or `sleep 10 & | cat` are rejected with descriptive syntax errors.

---

## 2. Job Specifications (`%N`)

SmartShell identifies jobs using job specifications:
- `%N`: Identifies job by Job ID (JID `N`, e.g., `%1`, `%2`).
- Plain integer `PID` (only for `kill`, e.g., `12345`): Identifies an individual OS process.

Invalid specifications (e.g., `%`, `%abc`, `%-1`, `%0`, or nonexistent `%99`) produce explicit error messages:
```sh
smartshell: fg: %invalid: invalid job specification
smartshell: fg: %99: no such job
```

---

## 3. The `jobs` Built-in Command

The `jobs` command lists active and stopped background jobs:

```sh
smartshell> jobs
[1]  Running    sleep 30 &
[2]  Stopped    sleep 40
```

### Features:
- Displays JID, state (`Running`, `Stopped`), and command line.
- Completed jobs are **never** shown as active jobs. Completed jobs generate a notification once and are automatically cleaned up from the job table.

---

## 4. The `fg` Built-in Command

Brings a background or stopped job to the foreground:

```sh
smartshell> fg %1
sleep 30 &
```

### Execution Flow:
1. Resolves job via `%N` (or defaults to the most recent job if no argument is provided).
2. Prints the job's command line.
3. If stopped, resumes all processes in the job by sending `SIGCONT` to the **entire process group** (`kill(-pgid, SIGCONT)`).
4. Hands over terminal ownership to the child process group via `tcsetpgrp()`.
5. Waits for the job state transition using the async-signal-safe `SIGCHLD` self-pipe engine (`wait_for_job()`).
6. On job completion, safely cleans up and frees the job. If stopped again (e.g., via `Ctrl-Z`), retains the job in the table as `Stopped`.
7. Restores terminal ownership (`tcsetpgrp`) and terminal modes (`tcsetattr`) to the shell.

---

## 5. The `bg` Built-in Command

Resumes a stopped job in the background:

```sh
smartshell> sleep 30
^Z
[1]+  Stopped  sleep 30
smartshell> bg %1
[1]+ sleep 30 &
smartshell>
```

### Execution Flow:
1. Resolves the job via `%N` (or defaults to the latest stopped job).
2. Verifies that the job is currently `Stopped`.
3. Marks the job state as `Running` and background (`is_foreground = false`).
4. Prints confirmation: `[JID]+ command &`.
5. Sends `SIGCONT` to the entire process group (`kill(-pgid, SIGCONT)`).
6. Returns immediately to the prompt without transferring terminal ownership.

---

## 6. The `kill` Built-in Command

Sends signals to jobs or OS processes:

```sh
smartshell> kill %1
smartshell> kill -TERM %1
smartshell> kill -9 12345
smartshell> kill -STOP %2
smartshell> kill -CONT %2
```

### Features:
- **Job Control**: Target specifications starting with `%` send the signal to the job's negative process group (`kill(-pgid, sig)`), ensuring all pipeline processes receive the signal.
- **Process IDs**: Target specifications with numeric PIDs send the signal to that individual process (`kill(pid, sig)`).
- **Safety**: Prevents targeting the shell's own process ID.
- **Supported Signals**:
  - By name: `-HUP`, `-INT`, `-QUIT`, `-KILL`, `-TERM`, `-STOP`, `-CONT`, `-TSTP`, `-USR1`, `-USR2`.
  - By number: `-1` through `-64`.
  - Default: `SIGTERM` (15).

---

## 7. Built-in Behavior with `&`

Built-in commands execute directly within the shell process. Stateful and job-control built-ins cannot run in the background and are explicitly rejected:
```sh
smartshell> cd /tmp &
smartshell: cd: cannot run in background
smartshell> exit &
smartshell: exit: cannot run in background
smartshell> fg &
smartshell: fg: cannot run in background
smartshell> bg &
smartshell: bg: cannot run in background
```
Stateless built-ins (`pwd`, `help`) with `&` are forked as background jobs and execute in subshells.

---

## 8. Process Groups & Pipeline Semantics

In SmartShell:
$$\text{One Pipeline} = \text{One Job} = \text{One Process Group}$$

- Child processes are assigned to the group leader (`pids[0]`) via `setpgid()`.
- Race conditions between parent and children are eliminated by calling `setpgid()` in both parent and child, with `SIGCHLD` blocked across `fork()` and job table registration.
- Signals (`SIGCONT`, `SIGTERM`, `SIGTSTP`, `SIGINT`) operate on the entire process group (`-pgid`), maintaining synchronized state across multi-process pipelines.
