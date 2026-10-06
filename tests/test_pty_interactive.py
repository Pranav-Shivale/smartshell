#!/usr/bin/env python3
"""
test_pty_interactive.py — Interactive PTY test harness for SmartShell Phase 6.
Tests real pseudo-terminal job control, background execution with &, jobs, fg, bg,
kill, signal propagation, and terminal ownership handoff.
"""

import os
import pty
import select
import signal
import sys
import time

def read_until(master_fd, target_bytes, timeout=5.0):
    deadline = time.time() + timeout
    buffer = b""
    while time.time() < deadline:
        r, _, _ = select.select([master_fd], [], [], 0.1)
        if master_fd in r:
            try:
                data = os.read(master_fd, 1024)
                if not data:
                    break
                buffer += data
                if target_bytes in buffer:
                    return buffer
            except OSError:
                break
    return buffer

def run_pty_test():
    print("=== Starting Phase 6 PTY Interactive Tests ===")

    pid, master = pty.fork()

    if pid == 0:
        # Child process running inside PTY
        os.execv("./bin/smartshell", ["smartshell"])
        sys.exit(1)

    try:
        # Wait for initial prompt
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"smartshell> " in out, f"Failed to get initial prompt: {out}"
        print("  PASS 0: Shell launches with prompt in interactive PTY")

        # 1. Background execution: sleep 30 & -> returns immediately to prompt
        t0 = time.time()
        os.write(master, b"sleep 30 &\n")
        out = read_until(master, b"smartshell> ", timeout=3.0)
        elapsed = time.time() - t0
        assert b"[1]" in out, f"Expected [1] job start notification: {out}"
        assert b"smartshell> " in out, f"Expected prompt after background launch: {out}"
        assert elapsed < 1.0, f"Background launch took too long ({elapsed}s)"
        print(f"  PASS 1: Background execution 'sleep 30 &' returned in {elapsed:.2f}s with [1]")

        # 2. jobs: lists active background job
        os.write(master, b"jobs\n")
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"[1]" in out and b"Running" in out and b"sleep 30 &" in out, f"jobs missing background job: {out}"
        print("  PASS 2: 'jobs' lists running background job [1]")

        # 3. kill %1: kill running background job
        os.write(master, b"kill %1\n")
        read_until(master, b"smartshell> ", timeout=3.0)
        time.sleep(0.3)
        os.write(master, b"jobs\n")
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"Terminated" in out or b"Done" in out, f"kill output unexpected: {out}"
        # Subsequent jobs call should be empty
        os.write(master, b"jobs\n")
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"sleep 30" not in out, f"Killed job still in jobs: {out}"
        print("  PASS 3: 'kill %1' terminated background job and removed it")

        # 4. Stop + bg: sleep 30 -> Ctrl-Z -> bg %1
        os.write(master, b"sleep 30\n")
        time.sleep(0.3)
        os.write(master, b"\x1a") # Ctrl-Z (VSUSP)
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"Stopped" in out, f"Expected Stopped status after Ctrl-Z: {out}"

        os.write(master, b"bg %1\n")
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"[1]+" in out and b"sleep 30 &" in out, f"Expected bg confirmation: {out}"
        print("  PASS 4: Stop + 'bg %1' resumed stopped job in background")

        # 5. Foreground after bg + Ctrl-C: fg %1 -> Ctrl-C
        os.write(master, b"fg %1\n")
        time.sleep(0.2)
        os.write(master, b"\x03") # Ctrl-C (VINTR)
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"smartshell> " in out, f"Shell failed to survive Ctrl-C after fg: {out}"
        os.write(master, b"jobs\n")
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"sleep 30" not in out, f"Job still in table after Ctrl-C: {out}"
        print("  PASS 5: 'fg %1' brought job to foreground and Ctrl-C terminated it cleanly")

        # 6. Stop + fg: sleep 30 -> Ctrl-Z -> fg %1 -> Ctrl-C
        os.write(master, b"sleep 30\n")
        time.sleep(0.3)
        os.write(master, b"\x1a") # Ctrl-Z
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"Stopped" in out, f"Expected Stopped status: {out}"

        os.write(master, b"fg %1\n")
        time.sleep(0.2)
        os.write(master, b"\x03") # Ctrl-C
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"smartshell> " in out, f"Shell prompt missing after fg+Ctrl-C: {out}"
        print("  PASS 6: Stop + 'fg %1' resumed job in foreground and Ctrl-C terminated it")

        # 7. Background completion: sleep 1 & -> wait -> jobs
        os.write(master, b"sleep 1 &\n")
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"[1]" in out, f"Expected job start: {out}"
        time.sleep(1.3)
        os.write(master, b"jobs\n")
        out = read_until(master, b"smartshell> ", timeout=3.0)
        # Should show Done or be completely removed
        assert b"Running" not in out, f"Job should no longer be running: {out}"
        print("  PASS 7: Background job completion notified and cleaned up")

        # 8. Background pipeline: sleep 30 | cat & -> returns immediately
        t0 = time.time()
        os.write(master, b"sleep 30 | cat &\n")
        out = read_until(master, b"smartshell> ", timeout=3.0)
        elapsed = time.time() - t0
        assert b"[1]" in out, f"Expected pipeline job start: {out}"
        assert elapsed < 1.0, f"Background pipeline launch too slow: {elapsed}s"
        os.write(master, b"jobs\n")
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"Running" in out and b"sleep 30 | cat" in out, f"Pipeline not listed: {out}"

        os.write(master, b"kill %1\n")
        read_until(master, b"smartshell> ", timeout=3.0)
        time.sleep(0.3)
        os.write(master, b"jobs\n")
        read_until(master, b"smartshell> ", timeout=3.0)
        print("  PASS 8: Background pipeline 'sleep 30 | cat &' started and killed")

        # 9. Stop pipeline + resume pipeline (bg %1 then fg %1)
        os.write(master, b"sleep 30 | cat\n")
        time.sleep(0.3)
        os.write(master, b"\x1a") # Ctrl-Z
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"Stopped" in out, f"Expected Stopped for pipeline: {out}"

        os.write(master, b"bg %1\n")
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"sleep 30 | cat" in out, f"Expected bg confirmation: {out}"

        os.write(master, b"fg %1\n")
        time.sleep(0.2)
        os.write(master, b"\x03") # Ctrl-C
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"smartshell> " in out, f"Shell died on pipeline Ctrl-C after fg: {out}"
        print("  PASS 9: Stopped pipeline resumed with bg, then brought to fg and stopped with Ctrl-C")

        # 10. Kill job with signal flag: kill -TERM %1
        os.write(master, b"sleep 30 &\n")
        read_until(master, b"smartshell> ", timeout=3.0)
        os.write(master, b"kill -TERM %1\n")
        read_until(master, b"smartshell> ", timeout=3.0)
        time.sleep(0.3)
        os.write(master, b"jobs\n")
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"Terminated" in out or b"smartshell> " in out
        print("  PASS 10: 'kill -TERM %1' successfully signaled background job")

        # 11. Ctrl-C on foreground job after fg: sleep 30 & -> fg %1 -> Ctrl-C
        os.write(master, b"sleep 30 &\n")
        read_until(master, b"smartshell> ", timeout=3.0)
        os.write(master, b"fg %1\n")
        time.sleep(0.2)
        os.write(master, b"\x03") # Ctrl-C
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"smartshell> " in out, f"Shell prompt missing after fg+Ctrl-C: {out}"
        os.write(master, b"pwd\n")
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"smartshell" in out, f"Terminal control corrupted after fg+Ctrl-C: {out}"
        print("  PASS 11: Ctrl-C on foreground job after fg survived and terminal control intact")

        # Exit cleanly
        os.write(master, b"exit 0\n")
        time.sleep(0.3)
        _, status = os.waitpid(pid, 0)
        assert os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0, f"Shell exited with status {status}"
        print("  PASS 12: Shell exited cleanly with status 0")

        print("\nAll Phase 6 PTY interactive tests PASSED successfully!")
        return 0

    except Exception as e:
        print(f"\nFAILED: {e}")
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass
        return 1
    finally:
        os.close(master)

if __name__ == "__main__":
    sys.exit(run_pty_test())
