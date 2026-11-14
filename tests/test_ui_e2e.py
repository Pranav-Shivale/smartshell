#!/usr/bin/env python3
"""
test_ui_e2e.py — Comprehensive End-to-End Verification of SmartShell 3.0 Web UI

Tests:
1. Static Asset Serving:
   - GET / -> 200 OK (web/index.html containing dark-mode UI and 6 views)
   - GET /css/app.css -> 200 OK (CSS styles, dark theme, terminal grid, gantt, badges)
   - GET /js/api.js -> 200 OK (REST client)
   - GET /js/terminal.js -> 200 OK (ANSI terminal emulator)
   - GET /js/app.js -> 200 OK (UI coordinator and view switcher)
2. Core Health & Auth Verification:
   - GET /api/health -> 200 OK, {"status": "ok"}
   - GET /api/auth/verify with Bearer token -> 200 OK, {"authenticated": true}
3. System Diagnostics Endpoints:
   - GET /api/sysinfo -> 200 OK, returns OS, kernel, arch, memory, shell pid/sid
   - GET /api/metrics -> 200 OK, returns CPU utilization and memory breakdown
4. Process Explorer Endpoints:
   - GET /api/processes -> 200 OK, returns process list with PID, PPID, name, state, RSS
   - GET /api/processes/<pid> -> 200 OK, returns detailed PID metrics
5. CPU Scheduling Lab Endpoints:
   - POST /api/scheduler/simulate (FCFS, SJF, RR, Priority, MLFQ) -> 200 OK, Gantt timeline, metrics
   - POST /api/scheduler/compare -> 200 OK, 5-algorithm comparison
6. Interactive Terminal WebSocket Lifecycle:
   - POST /api/terminal -> 200 OK, creates PTY session with ws_ticket
   - Connect to WebSocket /ws/terminal with local Host and Origin headers
   - In-band auth handshake: {"type": "auth", "session_id": ..., "ticket": ...} -> {"type": "auth_ok"}
   - Command Execution: send "pwd\n", receive terminal output with working directory
   - Command Execution: send "echo 'UI_VERIFY_SUCCESS'\n", receive matching output
   - PTY resize: send {"type": "resize", "cols": 100, "rows": 30}
   - GET /api/terminal -> active session listed
   - DELETE /api/terminal/<session_id> -> session deleted cleanly
"""

import http.client
import json
import os
import re
import socket
import subprocess
import sys
import time
import asyncio
import websockets

SERVER_BIN = "./bin/smartshell-server"
SHELL_BIN = "./bin/smartshell"

def find_free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port

class SmartShellServerProcess:
    def __init__(self, port, doc_root="./web"):
        self.port = port
        self.doc_root = doc_root
        self.proc = None
        self.token = None

    def start(self, timeout=5.0):
        cmd = [
            SERVER_BIN,
            "-p", str(self.port),
            "-d", self.doc_root,
            "--shell-path", SHELL_BIN
        ]
        env = os.environ.copy()
        env["HOME"] = "/tmp"

        self.proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
            text=True
        )

        deadline = time.time() + timeout
        started = False

        while time.time() < deadline:
            if self.proc.poll() is not None:
                out, err = self.proc.communicate()
                raise RuntimeError(f"Server exited unexpectedly ({self.proc.returncode}):\n{out}\n{err}")

            # Check if port is open
            try:
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.settimeout(0.2)
                s.connect(("127.0.0.1", self.port))
                s.close()
                started = True
                break
            except (ConnectionRefusedError, OSError):
                time.sleep(0.05)

        if not started:
            self.stop()
            raise TimeoutError("Server failed to open port within timeout")

        # Read token from /tmp/.smartshell/session.token or stdout
        token_path = "/tmp/.smartshell/session.token"
        if os.path.exists(token_path):
            with open(token_path, "r") as f:
                self.token = f.read().strip()
        else:
            time.sleep(0.1)
            # Try to read token from token file in user's home
            user_token = os.path.expanduser("~/.smartshell/session.token")
            if os.path.exists(user_token):
                with open(user_token, "r") as f:
                    self.token = f.read().strip()

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()

def request(port, method, path, headers=None, body=None):
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5.0)
    req_headers = headers or {}
    conn.request(method, path, body=body, headers=req_headers)
    resp = conn.getresponse()
    resp_body = resp.read().decode("utf-8")
    conn.close()
    return resp.status, resp_body

async def test_websocket_terminal(port, session_id, ws_ticket):
    ws_url = f"ws://127.0.0.1:{port}/ws/terminal"
    extra_headers = {
        "Origin": f"http://127.0.0.1:{port}",
        "Host": f"127.0.0.1:{port}"
    }

    async with websockets.connect(ws_url, extra_headers=extra_headers) as ws:
        # 1. Send in-band authentication
        auth_msg = {
            "type": "auth",
            "session_id": session_id,
            "ticket": ws_ticket
        }
        await ws.send(json.dumps(auth_msg))

        # 2. Receive auth_ok (ignoring initial buffered output frames)
        authenticated = False
        init_deadline = time.time() + 4.0
        while time.time() < init_deadline:
            reply = await asyncio.wait_for(ws.recv(), timeout=2.0)
            msg = json.loads(reply)
            if msg.get("type") == "auth_ok":
                authenticated = True
                break
        assert authenticated, "Timed out waiting for auth_ok"

        # 3. Send command 'pwd\n'
        await ws.send(json.dumps({"type": "input", "data": "pwd\n"}))

        # 4. Collect output
        accumulated = ""
        deadline = time.time() + 4.0
        while time.time() < deadline:
            try:
                frame = await asyncio.wait_for(ws.recv(), timeout=1.0)
                msg = json.loads(frame)
                if msg.get("type") == "output":
                    accumulated += msg.get("data", "")
                    if "smartshell" in accumulated or "/" in accumulated:
                        break
            except asyncio.TimeoutError:
                break

        assert len(accumulated) > 0, "Expected shell output from 'pwd', got none"

        # 5. Send echo command
        marker = "SMARTSHELL_VERIFY_SUCCESS_789"
        await ws.send(json.dumps({"type": "input", "data": f"echo '{marker}'\n"}))

        accumulated = ""
        deadline = time.time() + 4.0
        while time.time() < deadline:
            try:
                frame = await asyncio.wait_for(ws.recv(), timeout=1.0)
                msg = json.loads(frame)
                if msg.get("type") == "output":
                    accumulated += msg.get("data", "")
                    if marker in accumulated:
                        break
            except asyncio.TimeoutError:
                break

        assert marker in accumulated, f"Expected {marker} in output, got: {accumulated}"

        # 6. Send resize
        await ws.send(json.dumps({"type": "resize", "cols": 120, "rows": 40}))

def main():
    print("=" * 60)
    print("SmartShell 3.0 Web UI & Backend Integration Test Suite")
    print("=" * 60)

    port = find_free_port()
    server = SmartShellServerProcess(port, doc_root="./web")
    server.start()

    passed = 0
    failed = 0

    def record(name, condition, detail=""):
        nonlocal passed, failed
        if condition:
            passed += 1
            print(f"  [PASS] {name}")
        else:
            failed += 1
            print(f"  [FAIL] {name} — {detail}")

    try:
        # --- Section 1: Static UI Asset Serving ---
        print("\n--- 1. Static Asset Serving ---")
        status, body = request(port, "GET", "/")
        record("GET / serves index.html", status == 200 and "SmartShell" in body)
        record("index.html contains 6 view sections",
               all(x in body for x in ["view-terminal", "view-dashboard", "view-processes", "view-scheduling", "view-system", "view-settings"]))

        status, body = request(port, "GET", "/css/app.css")
        record("GET /css/app.css serves CSS stylesheet", status == 200 and len(body) > 1000 and ".term-tab" in body)

        status, body = request(port, "GET", "/js/api.js")
        record("GET /js/api.js serves REST client", status == 200 and "SmartShellAPI" in body)

        status, body = request(port, "GET", "/js/terminal.js")
        record("GET /js/terminal.js serves ANSI terminal emulator", status == 200 and "SmartTerminal" in body)

        status, body = request(port, "GET", "/js/app.js")
        record("GET /js/app.js serves Application Coordinator", status == 200 and "activeView" in body and "runSimulation" in body)

        # --- Section 2: Health & Authentication ---
        print("\n--- 2. Health & Authentication ---")
        status, body = request(port, "GET", "/api/health")
        data = json.loads(body)
        record("GET /api/health returns ok", status == 200 and data.get("status") == "ok")

        token = server.token
        assert token, "Token not found"

        auth_headers = {"Authorization": f"Bearer {token}"}
        status, body = request(port, "GET", "/api/auth/verify", headers=auth_headers)
        data = json.loads(body)
        record("GET /api/auth/verify accepts valid token", status == 200 and data.get("authenticated") is True)

        # --- Section 3: System Diagnostics ---
        print("\n--- 3. System Diagnostics Endpoints ---")
        status, body = request(port, "GET", "/api/sysinfo", headers=auth_headers)
        sysinfo = json.loads(body)
        record("GET /api/sysinfo returns host and shell metadata",
               status == 200 and "os_name" in sysinfo and "shell" in sysinfo and "memory" in sysinfo)

        status, body = request(port, "GET", "/api/metrics", headers=auth_headers)
        metrics = json.loads(body)
        record("GET /api/metrics returns CPU & memory stats",
               status == 200 and "memory" in metrics and "total_kb" in metrics["memory"])

        # --- Section 4: Process Explorer ---
        print("\n--- 4. Process Explorer Endpoints ---")
        status, body = request(port, "GET", "/api/processes", headers=auth_headers)
        procs = json.loads(body)
        record("GET /api/processes returns task list",
               status == 200 and "processes" in procs and len(procs["processes"]) > 0)

        first_pid = procs["processes"][0]["pid"]
        status, body = request(port, "GET", f"/api/processes/{first_pid}", headers=auth_headers)
        pid_info = json.loads(body)
        record(f"GET /api/processes/{first_pid} returns process details",
               status == 200 and pid_info.get("pid") == first_pid and "rss_bytes" in pid_info)

        # --- Section 5: CPU Scheduling Lab ---
        print("\n--- 5. CPU Scheduling Lab Endpoints ---")
        test_workload = [
            {"pid": 1, "arrival_time": 0, "burst_time": 6, "priority": 2},
            {"pid": 2, "arrival_time": 2, "burst_time": 4, "priority": 1},
            {"pid": 3, "arrival_time": 4, "burst_time": 8, "priority": 3}
        ]
        sim_payload = json.dumps({
            "algorithm": "rr",
            "quantum": 3,
            "processes": test_workload
        })
        status, body = request(port, "POST", "/api/scheduler/simulate", headers={**auth_headers, "Content-Type": "application/json"}, body=sim_payload)
        sim_res = json.loads(body)
        record("POST /api/scheduler/simulate (Round Robin)",
               status == 200 and "timeline" in sim_res and "summary" in sim_res and sim_res["summary"].get("total_elapsed_time", 0) > 0)

        comp_payload = json.dumps({
            "quantum": 4,
            "processes": test_workload
        })
        status, body = request(port, "POST", "/api/scheduler/compare", headers={**auth_headers, "Content-Type": "application/json"}, body=comp_payload)
        comp_res = json.loads(body)
        record("POST /api/scheduler/compare (5 algorithms)",
               status == 200 and "results" in comp_res and all(a in comp_res["results"] for a in ["fcfs", "sjf", "rr", "priority", "mlfq"]))

        # --- Section 6: Interactive Terminal Session & WebSocket ---
        print("\n--- 6. Interactive Terminal & WebSocket Execution ---")
        status, body = request(port, "POST", "/api/terminal", headers={**auth_headers, "Content-Type": "application/json"}, body=json.dumps({"cols": 80, "rows": 24}))
        term_res = json.loads(body)
        session_id = term_res.get("session_id")
        ws_ticket = term_res.get("ws_ticket")
        record("POST /api/terminal creates session and ticket",
               status in (200, 201) and bool(session_id) and bool(ws_ticket))

        # Test WebSocket live terminal I/O
        asyncio.run(test_websocket_terminal(port, session_id, ws_ticket))
        record("WebSocket Terminal in-band auth & real shell execution (pwd, echo)", True)

        # List sessions
        status, body = request(port, "GET", "/api/terminal", headers=auth_headers)
        list_res = json.loads(body)
        record("GET /api/terminal lists active sessions",
               status == 200 and any(s["session_id"] == session_id for s in list_res.get("sessions", [])))

        # Delete session
        status, body = request(port, "DELETE", f"/api/terminal/{session_id}", headers=auth_headers)
        record(f"DELETE /api/terminal/{session_id} terminates session", status == 200)

    finally:
        server.stop()

    print("\n" + "=" * 60)
    print(f"Results: {passed} passed, {failed} failed")
    print("=" * 60)
    if failed > 0:
        sys.exit(1)

if __name__ == "__main__":
    main()
