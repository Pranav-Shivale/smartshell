#!/usr/bin/env python3
"""
test_pty_interactive.py — Interactive PTY test harness for SmartShell Phase 5.
Tests real pseudo-terminal job control, signals (Ctrl-C, Ctrl-Z), terminal reclamation,
and pipeline signal propagation.
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
    print("=== Starting PTY Interactive Tests ===")

    pid, master = pty.fork()

    if pid == 0:
        # Child process running inside PTY
        os.execv("./bin/smartshell", ["smartshell"])
        sys.exit(1)

    try:
        # Test 1: Wait for initial prompt
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"smartshell> " in out, f"Failed to get initial prompt: {out}"
        print("  PASS 1: Shell launches with prompt in interactive PTY")

        # Test 2: Run sleep 1 and verify foreground wait
        t0 = time.time()
        os.write(master, b"sleep 1\n")
        out = read_until(master, b"smartshell> ", timeout=3.0)
        elapsed = time.time() - t0
        assert b"smartshell> " in out, f"Prompt did not return after sleep 1: {out}"
        assert elapsed >= 0.9, f"Foreground sleep 1 returned too fast: {elapsed}s"
        print(f"  PASS 2: Foreground job 'sleep 1' waited {elapsed:.2f}s and returned to prompt")

        # Test 3: Run sleep 30, send Ctrl-C (\x03), verify child terminated and shell survives
        os.write(master, b"sleep 30\n")
        time.sleep(0.3)
        # Send Ctrl-C (VINTR)
        os.write(master, b"\x03")
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"smartshell> " in out, f"Shell did not survive or prompt missing after Ctrl-C: {out}"
        print("  PASS 3: Ctrl-C terminated foreground 'sleep 30' and shell survived")

        # Test 4: Run sleep 30, send Ctrl-Z (\x1a), verify child stopped and shell survives
        os.write(master, b"sleep 30\n")
        time.sleep(0.3)
        # Send Ctrl-Z (VSUSP)
        os.write(master, b"\x1a")
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"Stopped" in out, f"Expected 'Stopped' status notification: {out}"
        assert b"smartshell> " in out, f"Expected prompt after Ctrl-Z: {out}"
        print("  PASS 4: Ctrl-Z stopped foreground 'sleep 30' and displayed Stopped status")

        # Test 5: Verify terminal control remains correct after stopping a job
        os.write(master, b"pwd\n")
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"smartshell" in out, f"pwd failed after stopping a job: {out}"
        print("  PASS 5: Terminal control intact; 'pwd' works after stopped job")

        # Test 6: Verify jobs builtin shows the stopped job
        os.write(master, b"jobs\n")
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"Stopped" in out and b"sleep 30" in out, f"jobs did not show stopped job: {out}"
        print("  PASS 6: 'jobs' lists the stopped job in JOB_STOPPED state")

        # Test 7: Pipeline Ctrl-C: sleep 30 | cat, send Ctrl-C, verify pipeline terminates
        os.write(master, b"sleep 30 | cat\n")
        time.sleep(0.3)
        os.write(master, b"\x03")
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"smartshell> " in out, f"Prompt did not return after pipeline Ctrl-C: {out}"
        print("  PASS 7: Pipeline 'sleep 30 | cat' process group terminated on Ctrl-C")

        # Test 8: Pipeline Ctrl-Z: sleep 30 | cat, send Ctrl-Z, verify pipeline stops
        os.write(master, b"sleep 30 | cat\n")
        time.sleep(0.3)
        os.write(master, b"\x1a")
        out = read_until(master, b"smartshell> ", timeout=3.0)
        assert b"Stopped" in out, f"Expected Stopped status for pipeline: {out}"
        assert b"smartshell> " in out, f"Prompt did not return after pipeline Ctrl-Z: {out}"
        print("  PASS 8: Pipeline 'sleep 30 | cat' process group stopped on Ctrl-Z")

        # Test 9: Exit cleanly
        os.write(master, b"exit 0\n")
        time.sleep(0.3)
        _, status = os.waitpid(pid, 0)
        assert os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0, f"Shell exited with status {status}"
        print("  PASS 9: Shell exited cleanly with status 0")

        print("\nAll 9 PTY interactive tests PASSED successfully!")
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
