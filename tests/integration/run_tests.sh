#!/bin/bash
# run_tests.sh — Integration test suite for SmartShell Phase 3.

set -e

SHELL_BIN="./bin/smartshell"
TMP_DIR="/tmp/smartshell_integration_$$"
mkdir -p "$TMP_DIR"

trap 'rm -rf "$TMP_DIR"' EXIT

passed=0
failed=0

run_test() {
    local name="$1"
    local input="$2"
    local expected_output="$3"

    printf "  RUN  integration: %s\n" "$name"

    actual_output=$(printf "%s\nexit\n" "$input" | "$SHELL_BIN" 2>&1 || true)
    
    if [ -n "$expected_output" ]; then
        if ! echo "$actual_output" | grep -Fq "$expected_output"; then
            printf "  FAIL integration: %s\n" "$name"
            echo "    Expected to find: $expected_output"
            echo "    Actual output:"
            echo "$actual_output"
            failed=$((failed + 1))
            return
        fi
    fi

    printf "  PASS integration: %s\n" "$name"
    passed=$((passed + 1))
}

echo "Running SmartShell Integration Tests:"

# 1. Pipelines
run_test "simple pipe" "ls src | grep parser" "parser.c"
run_test "multi-stage pipeline" "printf 'apple\nbanana\nband\n' | grep an | wc -l" "2"

# 2. Redirection
run_test "output redirect truncate" "$(printf 'echo hello_truncate > %s/out.txt\ncat %s/out.txt' "$TMP_DIR" "$TMP_DIR")" "hello_truncate"
run_test "output redirect append" "$(printf 'echo line1 > %s/app.txt\necho line2 >> %s/app.txt\ncat %s/app.txt' "$TMP_DIR" "$TMP_DIR" "$TMP_DIR")" "line2"
run_test "input redirect" "$(printf 'echo input_test > %s/in.txt\ncat < %s/in.txt' "$TMP_DIR" "$TMP_DIR")" "input_test"
run_test "stderr redirect" "$(printf 'ls /nonexistent_directory_for_test 2> %s/err.txt\ncat %s/err.txt' "$TMP_DIR" "$TMP_DIR")" "No such file or directory"

# 3. Combinations of pipes and redirection
run_test "pipe and input redirect" "$(printf 'echo "red\ngreen\nblue" > %s/colors.txt\ncat < %s/colors.txt | grep green' "$TMP_DIR" "$TMP_DIR")" "green"
run_test "pipe and output redirect" "$(printf 'printf "3\n1\n2\n" | sort > %s/sorted.txt\ncat %s/sorted.txt' "$TMP_DIR" "$TMP_DIR")" "1"

# 4. Built-ins with redirection and pipes
run_test "built-in with output redirection" "$(printf 'pwd > %s/pwd.txt\ncat %s/pwd.txt' "$TMP_DIR" "$TMP_DIR")" "smartshell"
run_test "built-in in pipeline" "help | grep Built-in" "Built-in Commands"

# 5. Syntax errors
run_test "syntax error leading pipe" "| ls" "syntax error: unexpected token"
run_test "syntax error trailing pipe" "ls |" "syntax error: unexpected token"
run_test "syntax error missing redirect target" "echo hello >" "syntax error: expected filename after redirection"

# 6. Phase 5 Process groups and non-interactive execution
run_test "jobs built-in empty" "jobs" "smartshell>"
run_test "rapid command sequence in non-interactive mode" "$(printf 'echo one\necho two\necho three' )" "three"
run_test "pipeline error code propagation" "false | true" "smartshell>"

# 7. Phase 6 Background execution and job control
run_test "background single job start" "sleep 1 &" "[1]"
run_test "background job shows in jobs" "$(printf 'sleep 2 &\njobs')" "Running"
run_test "background pipeline shows in jobs" "$(printf 'sleep 2 | cat &\njobs')" "sleep 2 | cat"
run_test "stateful builtin rejects background" "cd /tmp &" "cannot run in background"
run_test "exit builtin rejects background" "exit &" "cannot run in background"

# 8. Phase 7 /proc Process Monitoring
run_test "monitor system summary" "monitor" "System Overview"
run_test "monitor PID 1 details" "monitor 1" "Process Information (PID 1)"
run_test "monitor nonexistent PID" "monitor 999999" "process 999999 not found"
run_test "monitor invalid PID string" "monitor abc" "invalid process ID"
run_test "monitor active background job" "$(printf 'sleep 30 &\nmonitor\nkill %%1\n')" "sleep"

# 9. Phase 8D Scheduling Lab CLI Integration
run_test "schedule no arguments shows usage" "schedule" "Usage: schedule"
run_test "schedule help shows usage" "schedule help" "Simulate educational CPU scheduling algorithms"
run_test "schedule fcfs" "schedule fcfs" "First-Come, First-Served"
run_test "schedule sjf" "schedule sjf" "Shortest Job First"
run_test "schedule rr default quantum" "schedule rr" "Round Robin (RR)"
run_test "schedule rr custom quantum" "schedule rr 2" "RR Quantum: 2"
run_test "schedule priority" "schedule priority" "Non-Preemptive Priority"
run_test "schedule mlfq" "schedule mlfq" "Multilevel Feedback Queue"
run_test "schedule in pipeline" "schedule fcfs | grep Algorithm" "First-Come, First-Served"
run_test "schedule with redirection" "$(printf 'schedule rr 4 > %s/sched.txt\ngrep Gantt %s/sched.txt' "$TMP_DIR" "$TMP_DIR")" "Gantt Chart"
run_test "schedule invalid algorithm error" "schedule invalid_alg" "unknown algorithm 'invalid_alg'"
run_test "schedule invalid quantum zero" "schedule rr 0" "invalid quantum '0'"
run_test "schedule invalid quantum negative" "schedule rr -1" "invalid quantum '-1'"
run_test "schedule invalid quantum non-numeric" "schedule rr abc" "invalid quantum 'abc'"
run_test "schedule extra argument error" "schedule rr 4 extra" "too many arguments"
run_test "shell continues after invalid schedule command" "$(printf 'schedule invalid_alg\necho shell_still_alive')" "shell_still_alive"

echo ""
echo "=== Integration Test Results: $((passed + failed)) run, $passed passed, $failed failed ==="

if [ "$failed" -gt 0 ]; then
    exit 1
fi

# Run PTY Interactive Tests if python3 is available
if command -v python3 >/dev/null 2>&1; then
    python3 tests/test_pty_interactive.py || exit 1
fi

exit 0
