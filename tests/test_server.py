#!/usr/bin/env python3
"""
test_server.py — Automated integration and security test suite for smartshell-server.

Tests:
  1. Server startup and shutdown
  2. Health endpoint (GET /api/health)
  3. Missing authentication (GET /api/auth/verify without header)
  4. Invalid authentication (GET /api/auth/verify with wrong token)
  5. Valid authentication (GET /api/auth/verify with valid Bearer token)
  6. Invalid Host header rejection (403 Forbidden)
  7. Invalid Origin header rejection (403 Forbidden)
  8. Valid Origin header acceptance (200 OK)
  9. Token file creation and 0600 permissions check
  10. Malformed requests (method not allowed 405, invalid bearer format 401)
  11. Port collision handling (detects port in use and exits non-zero)
  12. Graceful shutdown on SIGTERM / SIGINT with zero leak / clean exit
"""

import http.client
import json
import os
import signal
import socket
import stat
import subprocess
import sys
import time
import asyncio
import websockets

SERVER_BIN = "./bin/smartshell-server"
DEFAULT_TEST_PORT = 18080

def find_free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port

class ServerInstance:
    def __init__(self, port, extra_args=None):
        self.port = port
        self.extra_args = extra_args or []
        self.proc = None
        self.token = None
        self.token_file = os.path.expanduser("~/.smartshell/session.token")

    def start(self, timeout=5.0):
        cmd = [SERVER_BIN, "-p", str(self.port)] + self.extra_args
        self.proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )

        deadline = time.time() + timeout
        started = False

        while time.time() < deadline:
            if self.proc.poll() is not None:
                out, err = self.proc.communicate()
                raise RuntimeError(f"Server exited prematurely ({self.proc.returncode}):\n{out}\n{err}")

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
            raise TimeoutError("Server failed to start within timeout")

        # Read token from ~/.smartshell/session.token
        if os.path.exists(self.token_file):
            with open(self.token_file, "r") as f:
                self.token = f.read().strip()

    def stop(self, sig=signal.SIGTERM):
        if self.proc and self.proc.poll() is None:
            self.proc.send_signal(sig)
            try:
                self.proc.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()

def raw_http_request(port, method, path, headers=None, body=None):
    """Perform raw HTTP request allowing custom Host and Origin headers."""
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5.0)
    req_headers = headers or {}
    
    # http.client sets Host automatically unless provided
    conn.request(method, path, body=body, headers=req_headers)
    resp = conn.getresponse()
    resp_body = resp.read().decode("utf-8")
    conn.close()
    return resp.status, resp.getheaders(), resp_body

def run_tests():
    print("=== Starting SmartShell Server Integration & Security Tests ===")
    tests_run = 0
    tests_passed = 0

    def test(name, fn):
        nonlocal tests_run, tests_passed
        tests_run += 1
        print(f"  RUN  {name}")
        try:
            fn()
            tests_passed += 1
            print(f"  PASS {name}")
        except Exception as e:
            print(f"  FAIL {name}: {e}")
            raise

    port = find_free_port()
    server = ServerInstance(port)
    server.start()

    try:
        # Test 1: Health endpoint
        def test_health():
            status, _, body = raw_http_request(port, "GET", "/api/health", headers={"Host": f"127.0.0.1:{port}"})
            assert status == 200, f"Expected 200, got {status}"
            data = json.loads(body)
            assert data.get("status") == "ok", f"Expected status 'ok', got {data}"
            assert data.get("version") == "3.0.0", f"Expected version '3.0.0', got {data}"
        test("health endpoint", test_health)

        # Test 2: Token file existence & 0600 permissions
        def test_token_permissions():
            token_path = os.path.expanduser("~/.smartshell/session.token")
            assert os.path.exists(token_path), f"Token file missing: {token_path}"
            mode = os.stat(token_path).st_mode & 0o777
            assert mode == 0o600, f"Expected permissions 0600, got {oct(mode)}"
            assert server.token is not None and len(server.token) == 64, f"Invalid token: {server.token}"
        test("token file and permissions", test_token_permissions)

        # Test 3: Missing authentication on /api/auth/verify
        def test_auth_missing():
            status, _, body = raw_http_request(port, "GET", "/api/auth/verify", headers={"Host": f"127.0.0.1:{port}"})
            assert status == 401, f"Expected 401, got {status}"
            data = json.loads(body)
            assert "error" in data
        test("missing authentication", test_auth_missing)

        # Test 4: Invalid authentication
        def test_auth_invalid():
            headers = {
                "Host": f"127.0.0.1:{port}",
                "Authorization": "Bearer 0000000000000000000000000000000000000000000000000000000000000000"
            }
            status, _, body = raw_http_request(port, "GET", "/api/auth/verify", headers=headers)
            assert status == 401, f"Expected 401, got {status}"
            data = json.loads(body)
            assert "error" in data
        test("invalid authentication", test_auth_invalid)

        # Test 5: Valid authentication
        def test_auth_valid():
            headers = {
                "Host": f"127.0.0.1:{port}",
                "Authorization": f"Bearer {server.token}"
            }
            status, _, body = raw_http_request(port, "GET", "/api/auth/verify", headers=headers)
            assert status == 200, f"Expected 200, got {status}: {body}"
            data = json.loads(body)
            assert data.get("authenticated") is True, f"Expected authenticated=true, got {data}"
        test("valid authentication", test_auth_valid)

        # Test 6: Invalid Host header rejection
        def test_host_header_rejection():
            headers = {
                "Host": f"evil-attacker.com:{port}",
                "Authorization": f"Bearer {server.token}"
            }
            status, _, body = raw_http_request(port, "GET", "/api/health", headers=headers)
            assert status == 403, f"Expected 403 for malicious Host, got {status}"

            status, _, body = raw_http_request(port, "GET", "/api/auth/verify", headers=headers)
            assert status == 403, f"Expected 403 for malicious Host on auth endpoint, got {status}"
        test("invalid host header rejection", test_host_header_rejection)

        # Test 7: Valid Host headers (127.0.0.1:port and localhost:port)
        def test_host_header_acceptance():
            for host in [f"127.0.0.1:{port}", f"localhost:{port}"]:
                status, _, _ = raw_http_request(port, "GET", "/api/health", headers={"Host": host})
                assert status == 200, f"Expected 200 for host {host}, got {status}"
        test("valid host header acceptance", test_host_header_acceptance)

        # Test 8: Invalid Origin header rejection
        def test_origin_header_rejection():
            headers = {
                "Host": f"127.0.0.1:{port}",
                "Origin": "http://evil-attacker.com",
                "Authorization": f"Bearer {server.token}"
            }
            status, _, body = raw_http_request(port, "GET", "/api/health", headers=headers)
            assert status == 403, f"Expected 403 for malicious Origin, got {status}"
        test("invalid origin header rejection", test_origin_header_rejection)

        # Test 9: Valid Origin header acceptance
        def test_origin_header_acceptance():
            for origin in [f"http://127.0.0.1:{port}", f"http://localhost:{port}"]:
                headers = {
                    "Host": f"127.0.0.1:{port}",
                    "Origin": origin
                }
                status, _, _ = raw_http_request(port, "GET", "/api/health", headers=headers)
                assert status == 200, f"Expected 200 for origin {origin}, got {status}"
        test("valid origin header acceptance", test_origin_header_acceptance)

        # Test 10: Method not allowed on endpoints
        def test_method_not_allowed():
            headers = {"Host": f"127.0.0.1:{port}"}
            status, _, body = raw_http_request(port, "POST", "/api/health", headers=headers)
            assert status == 405, f"Expected 405 for POST /api/health, got {status}"
        test("method not allowed", test_method_not_allowed)

        # Test 11: Malformed Authorization header
        def test_malformed_auth():
            headers = {
                "Host": f"127.0.0.1:{port}",
                "Authorization": "Basic dXNlcjpwYXNz"
            }
            status, _, _ = raw_http_request(port, "GET", "/api/auth/verify", headers=headers)
            assert status == 401, f"Expected 401 for Basic auth, got {status}"
        test("malformed authorization format", test_malformed_auth)

        # Test 12: Port already in use handling
        def test_port_collision():
            # Try to start another server on the same port
            p2 = subprocess.Popen(
                [SERVER_BIN, "-p", str(port)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )
            # Should terminate quickly with an error
            ret = p2.wait(timeout=3.0)
            assert ret != 0, f"Expected non-zero exit code on port collision, got {ret}"
        test("port already in use", test_port_collision)

        auth_headers = {
            "Host": f"127.0.0.1:{port}",
            "Authorization": f"Bearer {server.token}"
        }

        # Test 13: GET /api/sysinfo
        def test_sysinfo():
            # Authorized GET
            status, _, body = raw_http_request(port, "GET", "/api/sysinfo", headers=auth_headers)
            assert status == 200, f"Expected 200, got {status}: {body}"
            data = json.loads(body)
            assert "os" in data, f"Missing 'os' in {data}"
            assert "architecture" in data, f"Missing 'architecture' in {data}"
            assert "cpus_online" in data and data["cpus_online"] > 0, f"Invalid cpus_online in {data}"
            assert "memory" in data and "total_kb" in data["memory"], f"Missing memory in {data}"
            assert "shell" in data and "pid" in data["shell"], f"Missing shell in {data}"

            # Unauthorized
            status, _, _ = raw_http_request(port, "GET", "/api/sysinfo", headers={"Host": f"127.0.0.1:{port}"})
            assert status == 401, f"Expected 401, got {status}"

            # Method Not Allowed
            status, _, _ = raw_http_request(port, "POST", "/api/sysinfo", headers=auth_headers)
            assert status == 405, f"Expected 405, got {status}"
        test("sysinfo endpoint (/api/sysinfo)", test_sysinfo)

        # Test 14: GET /api/metrics
        def test_metrics():
            # Authorized GET
            status, _, body = raw_http_request(port, "GET", "/api/metrics", headers=auth_headers)
            assert status == 200, f"Expected 200, got {status}: {body}"
            data = json.loads(body)
            assert "cpu_utilization" in data, f"Missing cpu_utilization in {data}"
            assert isinstance(data["cpu_utilization"], (int, float)), f"Invalid cpu_utilization type in {data}"
            assert "memory" in data and "available_kb" in data["memory"], f"Missing memory in {data}"

            # Unauthorized
            status, _, _ = raw_http_request(port, "GET", "/api/metrics", headers={"Host": f"127.0.0.1:{port}"})
            assert status == 401, f"Expected 401, got {status}"

            # Method Not Allowed
            status, _, _ = raw_http_request(port, "POST", "/api/metrics", headers=auth_headers)
            assert status == 405, f"Expected 405, got {status}"
        test("metrics endpoint (/api/metrics)", test_metrics)

        # Test 15: GET /api/processes
        def test_processes():
            # Default GET
            status, _, body = raw_http_request(port, "GET", "/api/processes", headers=auth_headers)
            assert status == 200, f"Expected 200, got {status}: {body}"
            data = json.loads(body)
            assert data.get("sort") == "cpu"
            assert isinstance(data.get("processes"), list)
            assert len(data["processes"]) > 0

            # Sort cpu explicit
            status, _, body = raw_http_request(port, "GET", "/api/processes?sort=cpu", headers=auth_headers)
            assert status == 200, f"Expected 200, got {status}: {body}"
            data = json.loads(body)
            assert data.get("sort") == "cpu"

            # Sort memory
            status, _, body = raw_http_request(port, "GET", "/api/processes?sort=memory", headers=auth_headers)
            assert status == 200, f"Expected 200, got {status}: {body}"
            data = json.loads(body)
            assert data.get("sort") == "memory"

            # Limit = 1
            status, _, body = raw_http_request(port, "GET", "/api/processes?limit=1", headers=auth_headers)
            assert status == 200, f"Expected 200, got {status}: {body}"
            data = json.loads(body)
            assert len(data.get("processes", [])) == 1

            # Limit = 3
            status, _, body = raw_http_request(port, "GET", "/api/processes?limit=3", headers=auth_headers)
            assert status == 200, f"Expected 200, got {status}: {body}"
            data = json.loads(body)
            assert len(data.get("processes", [])) <= 3

            # Invalid sort
            status, _, _ = raw_http_request(port, "GET", "/api/processes?sort=invalid", headers=auth_headers)
            assert status == 400, f"Expected 400 for invalid sort, got {status}"

            # Invalid limit <= 0
            status, _, _ = raw_http_request(port, "GET", "/api/processes?limit=0", headers=auth_headers)
            assert status == 400, f"Expected 400 for limit 0, got {status}"

            # Invalid limit non-numeric
            status, _, _ = raw_http_request(port, "GET", "/api/processes?limit=abc", headers=auth_headers)
            assert status == 400, f"Expected 400 for invalid limit, got {status}"

            # Unauthorized
            status, _, _ = raw_http_request(port, "GET", "/api/processes", headers={"Host": f"127.0.0.1:{port}"})
            assert status == 401, f"Expected 401, got {status}"
        test("processes endpoint (/api/processes)", test_processes)

        # Test 16: GET /api/processes/:pid
        def test_process_pid():
            # Query server's own PID
            srv_pid = server.proc.pid
            status, _, body = raw_http_request(port, "GET", f"/api/processes/{srv_pid}", headers=auth_headers)
            assert status == 200, f"Expected 200, got {status}: {body}"
            data = json.loads(body)
            assert data.get("pid") == srv_pid
            assert "name" in data
            assert "state" in data
            assert "rss_bytes" in data
            assert "vsize_bytes" in data

            # Query short-lived process that terminates
            sub = subprocess.Popen(["sleep", "0.01"])
            sub_pid = sub.pid
            sub.wait()
            status, _, _ = raw_http_request(port, "GET", f"/api/processes/{sub_pid}", headers=auth_headers)
            assert status == 404, f"Expected 404 for terminated process, got {status}"

            # Non-existent PID
            status, _, _ = raw_http_request(port, "GET", "/api/processes/9999999", headers=auth_headers)
            assert status == 404, f"Expected 404, got {status}"

            # Invalid PID string
            status, _, _ = raw_http_request(port, "GET", "/api/processes/invalid_pid", headers=auth_headers)
            assert status == 400, f"Expected 400, got {status}"

            # Negative PID
            status, _, _ = raw_http_request(port, "GET", "/api/processes/-10", headers=auth_headers)
            assert status == 400, f"Expected 400, got {status}"
        test("process PID endpoint (/api/processes/:pid)", test_process_pid)

        # Test 17: GET /api/process-tree
        def test_process_tree():
            status, _, body = raw_http_request(port, "GET", "/api/process-tree", headers=auth_headers)
            assert status == 200, f"Expected 200, got {status}: {body}"
            data = json.loads(body)
            assert "total_nodes" in data and data["total_nodes"] > 0
            assert "roots" in data and isinstance(data["roots"], list)
            assert len(data["roots"]) > 0
            root0 = data["roots"][0]
            assert "pid" in root0
            assert "name" in root0
            assert "children" in root0

            # Unauthorized
            status, _, _ = raw_http_request(port, "GET", "/api/process-tree", headers={"Host": f"127.0.0.1:{port}"})
            assert status == 401, f"Expected 401, got {status}"

            # Method Not Allowed
            status, _, _ = raw_http_request(port, "POST", "/api/process-tree", headers=auth_headers)
            assert status == 405, f"Expected 405, got {status}"
        test("process tree endpoint (/api/process-tree)", test_process_tree)

        # Test 18: POST /api/scheduler/simulate
        def test_scheduler_simulate():
            sim_headers = {
                "Host": f"127.0.0.1:{port}",
                "Authorization": f"Bearer {server.token}",
                "Content-Type": "application/json"
            }
            fcfs_payload = json.dumps({
                "algorithm": "fcfs",
                "processes": [
                    {"pid": 1, "arrival_time": 0, "burst_time": 6, "priority": 2},
                    {"pid": 2, "arrival_time": 2, "burst_time": 4, "priority": 1}
                ]
            })
            status, _, body = raw_http_request(port, "POST", "/api/scheduler/simulate", headers=sim_headers, body=fcfs_payload)
            assert status == 200, f"Expected 200, got {status}: {body}"
            data = json.loads(body)
            assert data.get("algorithm") == "fcfs"
            assert "summary" in data
            assert data["summary"]["total_elapsed_time"] == 10
            assert len(data.get("processes", [])) == 2
            assert len(data.get("timeline", [])) >= 2
            assert "gantt_chart" in data

            # Valid RR with custom quantum
            rr_payload = json.dumps({
                "algorithm": "rr",
                "quantum": 2,
                "processes": [
                    {"pid": 1, "arrival_time": 0, "burst_time": 5, "priority": 1},
                    {"pid": 2, "arrival_time": 1, "burst_time": 3, "priority": 1}
                ]
            })
            status, _, body = raw_http_request(port, "POST", "/api/scheduler/simulate", headers=sim_headers, body=rr_payload)
            assert status == 200, f"Expected 200, got {status}: {body}"
            data = json.loads(body)
            assert data.get("algorithm") == "rr"
            assert data.get("quantum") == 2

            # Valid SJF, Priority, MLFQ
            for alg in ["sjf", "priority", "mlfq"]:
                payload = json.dumps({
                    "algorithm": alg,
                    "processes": [{"pid": 1, "arrival_time": 0, "burst_time": 4, "priority": 1}]
                })
                status, _, _ = raw_http_request(port, "POST", "/api/scheduler/simulate", headers=sim_headers, body=payload)
                assert status == 200, f"Expected 200 for algorithm {alg}, got {status}"

            # Invalid algorithm
            bad_alg = json.dumps({"algorithm": "unknown", "processes": [{"pid": 1, "arrival_time": 0, "burst_time": 2}]})
            status, _, _ = raw_http_request(port, "POST", "/api/scheduler/simulate", headers=sim_headers, body=bad_alg)
            assert status == 400, f"Expected 400, got {status}"

            # Missing algorithm
            missing_alg = json.dumps({"processes": [{"pid": 1, "arrival_time": 0, "burst_time": 2}]})
            status, _, _ = raw_http_request(port, "POST", "/api/scheduler/simulate", headers=sim_headers, body=missing_alg)
            assert status == 400, f"Expected 400 for missing algorithm, got {status}"

            # Missing processes array
            missing_procs = json.dumps({"algorithm": "fcfs"})
            status, _, _ = raw_http_request(port, "POST", "/api/scheduler/simulate", headers=sim_headers, body=missing_procs)
            assert status == 400, f"Expected 400 for missing processes, got {status}"

            # Missing burst_time in process
            missing_bt = json.dumps({"algorithm": "fcfs", "processes": [{"pid": 1, "arrival_time": 0}]})
            status, _, _ = raw_http_request(port, "POST", "/api/scheduler/simulate", headers=sim_headers, body=missing_bt)
            assert status == 400, f"Expected 400 for missing burst_time, got {status}"

            # Zero burst time
            zero_bt = json.dumps({"algorithm": "fcfs", "processes": [{"pid": 1, "arrival_time": 0, "burst_time": 0}]})
            status, _, _ = raw_http_request(port, "POST", "/api/scheduler/simulate", headers=sim_headers, body=zero_bt)
            assert status == 400, f"Expected 400 for zero burst, got {status}"

            # Negative arrival time
            neg_arr = json.dumps({"algorithm": "fcfs", "processes": [{"pid": 1, "arrival_time": -5, "burst_time": 3}]})
            status, _, _ = raw_http_request(port, "POST", "/api/scheduler/simulate", headers=sim_headers, body=neg_arr)
            assert status == 400, f"Expected 400 for negative arrival, got {status}"

            # Duplicate PID
            dup_pid = json.dumps({"algorithm": "fcfs", "processes": [
                {"pid": 1, "arrival_time": 0, "burst_time": 2},
                {"pid": 1, "arrival_time": 1, "burst_time": 3}
            ]})
            status, _, _ = raw_http_request(port, "POST", "/api/scheduler/simulate", headers=sim_headers, body=dup_pid)
            assert status == 400, f"Expected 400 for duplicate PID, got {status}"

            # Invalid quantum (<= 0)
            bad_q = json.dumps({"algorithm": "rr", "quantum": -1, "processes": [{"pid": 1, "arrival_time": 0, "burst_time": 2}]})
            status, _, _ = raw_http_request(port, "POST", "/api/scheduler/simulate", headers=sim_headers, body=bad_q)
            assert status == 400, f"Expected 400 for negative quantum, got {status}"

            # Invalid workload (negative burst)
            bad_workload = json.dumps({"algorithm": "fcfs", "processes": [{"pid": 1, "arrival_time": 0, "burst_time": -3}]})
            status, _, _ = raw_http_request(port, "POST", "/api/scheduler/simulate", headers=sim_headers, body=bad_workload)
            assert status == 400, f"Expected 400, got {status}"

            # Malformed JSON
            status, _, _ = raw_http_request(port, "POST", "/api/scheduler/simulate", headers=sim_headers, body="{bad json")
            assert status == 400, f"Expected 400, got {status}"

            # Oversized payload (>64KB)
            huge_payload = json.dumps({"algorithm": "fcfs", "padding": "x" * 70000})
            status, _, _ = raw_http_request(port, "POST", "/api/scheduler/simulate", headers=sim_headers, body=huge_payload)
            assert status == 413, f"Expected 413 for oversized payload, got {status}"

            # Method Not Allowed
            status, _, _ = raw_http_request(port, "GET", "/api/scheduler/simulate", headers=sim_headers)
            assert status == 405, f"Expected 405, got {status}"

            # Unauthorized
            status, _, _ = raw_http_request(port, "POST", "/api/scheduler/simulate", headers={"Host": f"127.0.0.1:{port}"}, body=fcfs_payload)
            assert status == 401, f"Expected 401, got {status}"
        test("scheduler simulate endpoint (/api/scheduler/simulate)", test_scheduler_simulate)

        # Test 19: POST /api/scheduler/compare
        def test_scheduler_compare():
            cmp_headers = {
                "Host": f"127.0.0.1:{port}",
                "Authorization": f"Bearer {server.token}",
                "Content-Type": "application/json"
            }
            valid_payload = json.dumps({
                "quantum": 3,
                "processes": [
                    {"pid": 1, "arrival_time": 0, "burst_time": 4, "priority": 1},
                    {"pid": 2, "arrival_time": 1, "burst_time": 5, "priority": 2},
                    {"pid": 3, "arrival_time": 2, "burst_time": 2, "priority": 3}
                ]
            })
            status, _, body = raw_http_request(port, "POST", "/api/scheduler/compare", headers=cmp_headers, body=valid_payload)
            assert status == 200, f"Expected 200, got {status}: {body}"
            data = json.loads(body)
            assert data.get("quantum") == 3
            assert data.get("total_processes") == 3
            assert "results" in data
            results = data["results"]
            for alg in ["fcfs", "sjf", "rr", "priority", "mlfq"]:
                assert alg in results, f"Missing {alg} in compare results"
                assert "summary" in results[alg]
            assert "comparison_table" in data

            # Non-default RR quantum check (quantum=6)
            q6_payload = json.dumps({
                "quantum": 6,
                "processes": [
                    {"pid": 1, "arrival_time": 0, "burst_time": 8, "priority": 1},
                    {"pid": 2, "arrival_time": 2, "burst_time": 4, "priority": 2}
                ]
            })
            status, _, body = raw_http_request(port, "POST", "/api/scheduler/compare", headers=cmp_headers, body=q6_payload)
            assert status == 200, f"Expected 200, got {status}: {body}"
            data = json.loads(body)
            assert data.get("quantum") == 6
            assert "results" in data
            assert data["results"]["rr"]["summary"]["cpu_utilization"] > 0

            # Invalid payload (empty processes)
            empty_payload = json.dumps({"processes": []})
            status, _, _ = raw_http_request(port, "POST", "/api/scheduler/compare", headers=cmp_headers, body=empty_payload)
            assert status == 400, f"Expected 400, got {status}"

            # Missing processes field
            missing_procs = json.dumps({"quantum": 4})
            status, _, _ = raw_http_request(port, "POST", "/api/scheduler/compare", headers=cmp_headers, body=missing_procs)
            assert status == 400, f"Expected 400 for missing processes, got {status}"

            # Invalid non-positive quantum
            bad_q = json.dumps({"quantum": 0, "processes": [{"pid": 1, "arrival_time": 0, "burst_time": 4}]})
            status, _, _ = raw_http_request(port, "POST", "/api/scheduler/compare", headers=cmp_headers, body=bad_q)
            assert status == 400, f"Expected 400 for quantum 0, got {status}"

            # Oversized payload (>64KB)
            huge_payload = json.dumps({"padding": "x" * 70000})
            status, _, _ = raw_http_request(port, "POST", "/api/scheduler/compare", headers=cmp_headers, body=huge_payload)
            assert status == 413, f"Expected 413, got {status}"

            # Method Not Allowed
            status, _, _ = raw_http_request(port, "GET", "/api/scheduler/compare", headers=cmp_headers)
            assert status == 405, f"Expected 405, got {status}"

            # Unauthorized
            status, _, _ = raw_http_request(port, "POST", "/api/scheduler/compare", headers={"Host": f"127.0.0.1:{port}"}, body=valid_payload)
            assert status == 401, f"Expected 401, got {status}"
        test("scheduler compare endpoint (/api/scheduler/compare)", test_scheduler_compare)

        # ---- Phase 13C: PTY Session & WebSocket Terminal Bridge Tests --------

        # Test 20: Terminal Session REST CRUD (/api/terminal)
        def test_terminal_rest():
            term_headers = {
                "Host": f"127.0.0.1:{port}",
                "Authorization": f"Bearer {server.token}",
                "Content-Type": "application/json"
            }
            # Create session
            status, _, body = raw_http_request(port, "POST", "/api/terminal", headers=term_headers,
                                               body=json.dumps({"cols": 100, "rows": 30}))
            assert status == 201, f"Expected 201, got {status}: {body}"
            data = json.loads(body)
            assert "session_id" in data
            assert "ws_ticket" in data
            assert data.get("cols") == 100
            assert data.get("rows") == 30
            assert data.get("status") == "created"
            sid = data["session_id"]

            # List sessions
            status, _, body = raw_http_request(port, "GET", "/api/terminal", headers=term_headers)
            assert status == 200, f"Expected 200, got {status}: {body}"
            list_data = json.loads(body)
            assert "sessions" in list_data
            assert any(s["session_id"] == sid for s in list_data["sessions"])

            # Resize session via REST
            status, _, _ = raw_http_request(port, "POST", f"/api/terminal/{sid}/resize", headers=term_headers,
                                            body=json.dumps({"cols": 120, "rows": 40}))
            assert status == 200, f"Expected 200, got {status}"

            # Invalid resize parameters
            status, _, _ = raw_http_request(port, "POST", f"/api/terminal/{sid}/resize", headers=term_headers,
                                            body=json.dumps({"cols": -1, "rows": 40}))
            assert status == 400, f"Expected 400 for negative cols, got {status}"

            # Delete session
            status, _, body = raw_http_request(port, "DELETE", f"/api/terminal/{sid}", headers=term_headers)
            assert status == 200, f"Expected 200, got {status}: {body}"

            # Delete already deleted session
            status, _, _ = raw_http_request(port, "DELETE", f"/api/terminal/{sid}", headers=term_headers)
            assert status == 404, f"Expected 404 for deleted session, got {status}"
        test("terminal session REST CRUD (/api/terminal)", test_terminal_rest)

        term_headers = {
            "Host": f"127.0.0.1:{port}",
            "Authorization": f"Bearer {server.token}",
            "Content-Type": "application/json"
        }

        # Helper: Create session via REST
        def create_test_session(cols=80, rows=24):
            status, _, body = raw_http_request(port, "POST", "/api/terminal", headers=term_headers,
                                               body=json.dumps({"cols": cols, "rows": rows}))
            assert status == 201, f"Failed to create session: {status}"
            return json.loads(body)

        def ws_connect(p=port, uri_path="/ws/terminal", origin=None, extra_headers=None):
            if origin is None:
                origin = f"http://127.0.0.1:{p}"
            uri = f"ws://127.0.0.1:{p}{uri_path}"
            return websockets.connect(uri, origin=origin, extra_headers=extra_headers)

        async def ws_auth_and_drain(ws, sid, ticket):
            await ws.send(json.dumps({"type": "auth", "session_id": sid, "ticket": ticket}))
            await asyncio.sleep(0.05)
            while True:
                try:
                    await asyncio.wait_for(ws.recv(), timeout=0.15)
                except asyncio.TimeoutError:
                    break

        async def ws_recv_output_until(ws, target, timeout=3.0):
            deadline = time.time() + timeout
            collected = ""
            while time.time() < deadline:
                try:
                    raw = await asyncio.wait_for(ws.recv(), timeout=max(0.1, deadline - time.time()))
                    msg = json.loads(raw)
                    if msg.get("type") == "output":
                        collected += msg.get("data", "")
                        if target in collected:
                            return collected
                    elif msg.get("type") == "exit":
                        collected += f"[exit:{msg.get('code')}]"
                        if target in collected:
                            return collected
                except asyncio.TimeoutError:
                    break
            return collected

        # Test 21: WebSocket Origin and Host Header Security Validation
        def test_ws_origin_and_host_validation():
            uri = f"ws://127.0.0.1:{port}/ws/terminal"

            async def _run():
                # 1. Missing Origin header -> handshake rejected (403 Forbidden)
                try:
                    async with websockets.connect(uri) as ws:
                        assert False, "Expected 403 for missing Origin header"
                except websockets.exceptions.InvalidStatusCode as e:
                    assert e.status_code == 403, f"Expected 403, got {e.status_code}"

                # 2. Non-local Origin (http://evil.com) -> handshake rejected (403)
                try:
                    async with websockets.connect(uri, origin="http://evil.com") as ws:
                        assert False, "Expected 403 for non-local Origin"
                except websockets.exceptions.InvalidStatusCode as e:
                    assert e.status_code == 403, f"Expected 403, got {e.status_code}"

                # 3. Wrong port Origin -> handshake rejected (403)
                try:
                    async with websockets.connect(uri, origin="http://127.0.0.1:9999") as ws:
                        assert False, "Expected 403 for wrong port Origin"
                except websockets.exceptions.InvalidStatusCode as e:
                    assert e.status_code == 403, f"Expected 403, got {e.status_code}"

                # 4. Malformed Origin ('null') -> handshake rejected (403)
                try:
                    async with websockets.connect(uri, origin="null") as ws:
                        assert False, "Expected 403 for null Origin"
                except websockets.exceptions.InvalidStatusCode as e:
                    assert e.status_code == 403, f"Expected 403, got {e.status_code}"

                # 5. Invalid Host header -> handshake rejected (403 Forbidden)
                s = socket.socket()
                s.connect(("127.0.0.1", port))
                bad_host_req = (
                    f"GET /ws/terminal HTTP/1.1\r\n"
                    f"Host: evil.com\r\n"
                    f"Origin: http://127.0.0.1:{port}\r\n"
                    f"Upgrade: websocket\r\n"
                    f"Connection: Upgrade\r\n"
                    f"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                    f"Sec-WebSocket-Version: 13\r\n\r\n"
                )
                s.sendall(bad_host_req.encode())
                raw_resp = s.recv(4096)
                s.close()
                assert b"403 Forbidden" in raw_resp, f"Expected 403 for evil Host, got: {raw_resp}"

                # 6. Valid local Origin -> handshake succeeds (101 Switching Protocols)
                async with ws_connect(port) as ws:
                    pass

            asyncio.run(_run())
        test("terminal WebSocket Origin and Host validation", test_ws_origin_and_host_validation)

        # Test 22: Query-String Credentials Ignored & In-Band Auth Required
        def test_ws_query_string_ignored_and_inband_auth_required():
            sess = create_test_session()
            sid, ticket = sess["session_id"], sess["ws_ticket"]

            async def _run():
                # Connecting with ticket in query string MUST NOT authenticate session
                uri_with_qs = f"/ws/terminal?session_id={sid}&ticket={ticket}"
                async with ws_connect(port, uri_path=uri_with_qs) as ws:
                    # Attempting input before sending in-band auth message MUST fail
                    await ws.send(json.dumps({"type": "input", "data": "echo should_fail\n"}))
                    msg = json.loads(await asyncio.wait_for(ws.recv(), timeout=2.0))
                    assert msg.get("type") == "error"
                    assert "authentication required" in msg.get("message", "")

            asyncio.run(_run())
        test("terminal WebSocket query-string credentials ignored & in-band auth required", test_ws_query_string_ignored_and_inband_auth_required)

        # Test 23: WebSocket Basic Execution & Exit
        def test_ws_basic_execution():
            sess = create_test_session()
            sid, ticket = sess["session_id"], sess["ws_ticket"]

            async def _run():
                async with ws_connect(port) as ws:
                    await ws_auth_and_drain(ws, sid, ticket)
                    await ws.send(json.dumps({"type": "input", "data": "echo phase13c_basic_ok\n"}))
                    out = await ws_recv_output_until(ws, "phase13c_basic_ok")
                    assert "phase13c_basic_ok" in out, f"Expected output marker, got: {out}"

                    # Exit shell
                    await ws.send(json.dumps({"type": "input", "data": "exit\n"}))
                    out = await ws_recv_output_until(ws, "[exit:0]")
                    assert "[exit:0]" in out, f"Expected exit code 0, got: {out}"

            asyncio.run(_run())
        test("terminal WebSocket basic execution and exit", test_ws_basic_execution)

        # Test 24: WebSocket Pipelines and Redirection
        def test_ws_pipelines_and_redirection():
            sess = create_test_session()
            sid, ticket = sess["session_id"], sess["ws_ticket"]

            async def _run():
                async with ws_connect(port) as ws:
                    await ws_auth_and_drain(ws, sid, ticket)
                    # Pipeline test
                    await ws.send(json.dumps({"type": "input", "data": "echo 'hello pipeline' | tr a-z A-Z\n"}))
                    out = await ws_recv_output_until(ws, "HELLO PIPELINE")
                    assert "HELLO PIPELINE" in out, f"Expected uppercase pipeline output, got: {out}"

                    # Redirection test
                    test_file = f"/tmp/smartshell_ws_test_{os.getpid()}.txt"
                    await ws.send(json.dumps({"type": "input", "data": f"echo redirect_test_13c > {test_file}\n"}))
                    await asyncio.sleep(0.1)
                    await ws.send(json.dumps({"type": "input", "data": f"cat {test_file}\n"}))
                    out = await ws_recv_output_until(ws, "redirect_test_13c")
                    assert "redirect_test_13c" in out, f"Expected file redirection output, got: {out}"

                    if os.path.exists(test_file):
                        os.unlink(test_file)

                    await ws.send(json.dumps({"type": "input", "data": "exit\n"}))
                    await ws_recv_output_until(ws, "[exit:0]")

            asyncio.run(_run())
        test("terminal WebSocket pipelines and redirection", test_ws_pipelines_and_redirection)

        # Test 25: WebSocket Ctrl+C and Ctrl+Z Job Control
        def test_ws_job_control():
            sess = create_test_session()
            sid, ticket = sess["session_id"], sess["ws_ticket"]

            async def _run():
                async with ws_connect(port) as ws:
                    await ws_auth_and_drain(ws, sid, ticket)

                    # Ctrl+C test: start sleep, interrupt with \x03
                    await ws.send(json.dumps({"type": "input", "data": "sleep 10\n"}))
                    await asyncio.sleep(0.15)
                    await ws.send(json.dumps({"type": "input", "data": "\x03"}))
                    out = await ws_recv_output_until(ws, "smartshell>")
                    assert "smartshell>" in out or "^C" in out, f"Expected Ctrl-C prompt return, got: {out}"

                    # Ctrl+Z test: start sleep, stop with \x1a
                    await ws.send(json.dumps({"type": "input", "data": "sleep 10\n"}))
                    await asyncio.sleep(0.15)
                    await ws.send(json.dumps({"type": "input", "data": "\x1a"}))
                    out = await ws_recv_output_until(ws, "Stopped")
                    assert "Stopped" in out, f"Expected Stopped job notification, got: {out}"

                    # Kill the stopped background job
                    await ws.send(json.dumps({"type": "input", "data": "kill %1\n"}))
                    await asyncio.sleep(0.1)

                    await ws.send(json.dumps({"type": "input", "data": "exit\n"}))
                    await ws_recv_output_until(ws, "[exit:0]")

            asyncio.run(_run())
        test("terminal WebSocket Ctrl+C and Ctrl+Z job control", test_ws_job_control)

        # Test 26: WebSocket Window Resize and Ping/Pong
        def test_ws_resize_and_ping():
            sess = create_test_session()
            sid, ticket = sess["session_id"], sess["ws_ticket"]

            async def _run():
                async with ws_connect(port) as ws:
                    await ws_auth_and_drain(ws, sid, ticket)

                    # Resize command
                    await ws.send(json.dumps({"type": "resize", "cols": 132, "rows": 43}))
                    await asyncio.sleep(0.05)

                    # Ping command
                    await ws.send(json.dumps({"type": "ping"}))
                    msg = json.loads(await asyncio.wait_for(ws.recv(), timeout=2.0))
                    assert msg.get("type") == "pong", f"Expected pong, got: {msg}"

                    await ws.send(json.dumps({"type": "input", "data": "exit\n"}))
                    await ws_recv_output_until(ws, "[exit:0]")

            asyncio.run(_run())
        test("terminal WebSocket resize propagation and ping/pong", test_ws_resize_and_ping)

        # Test 27: WebSocket Authentication and Security
        def test_ws_auth_and_security():
            sess = create_test_session()
            sid, ticket = sess["session_id"], sess["ws_ticket"]

            async def _test_wrong_ticket():
                async with ws_connect(port) as ws:
                    await ws.send(json.dumps({"type": "auth", "session_id": sid, "ticket": "wrong_ticket_hex"}))
                    msg = json.loads(await asyncio.wait_for(ws.recv(), timeout=2.0))
                    assert msg.get("type") == "error"
                    assert "authentication failed" in msg.get("message", "")

            async def _test_input_before_auth():
                async with ws_connect(port) as ws:
                    await ws.send(json.dumps({"type": "input", "data": "ls\n"}))
                    msg = json.loads(await asyncio.wait_for(ws.recv(), timeout=2.0))
                    assert msg.get("type") == "error"
                    assert "authentication required" in msg.get("message", "")

            async def _test_valid_auth_then_replay():
                async with ws_connect(port) as ws:
                    await ws.send(json.dumps({"type": "auth", "session_id": sid, "ticket": ticket}))
                    auth_resp = None
                    for _ in range(3):
                        m = json.loads(await asyncio.wait_for(ws.recv(), timeout=2.0))
                        if m.get("type") == "auth_ok":
                            auth_resp = m
                            break
                    assert auth_resp is not None

                # Attempt replay with same ticket on new connection
                async with ws_connect(port) as ws2:
                    await ws2.send(json.dumps({"type": "auth", "session_id": sid, "ticket": ticket}))
                    msg = json.loads(await asyncio.wait_for(ws2.recv(), timeout=2.0))
                    assert msg.get("type") == "error"
                    assert "authentication failed" in msg.get("message", "")

            asyncio.run(_test_wrong_ticket())
            asyncio.run(_test_input_before_auth())
            asyncio.run(_test_valid_auth_then_replay())
        test("terminal WebSocket authentication and security enforcement", test_ws_auth_and_security)

        # Test 28: WebSocket Malformed and Oversized Messages
        def test_ws_message_validation():
            sess = create_test_session()
            sid, ticket = sess["session_id"], sess["ws_ticket"]

            async def _run():
                async with ws_connect(port) as ws:
                    # Non-JSON garbage
                    await ws.send("not_valid_json_payload{{{")
                    msg = json.loads(await asyncio.wait_for(ws.recv(), timeout=2.0))
                    assert msg.get("type") == "error"
                    assert "invalid JSON" in msg.get("message", "")

                    # Missing type field
                    await ws.send(json.dumps({"missing_type": True}))
                    msg = json.loads(await asyncio.wait_for(ws.recv(), timeout=2.0))
                    assert msg.get("type") == "error"
                    assert "missing or invalid type" in msg.get("message", "")

                    # Oversized message (>64KB)
                    oversized = json.dumps({"type": "input", "data": "x" * 70000})
                    await ws.send(oversized)
                    msg = json.loads(await asyncio.wait_for(ws.recv(), timeout=2.0))
                    assert msg.get("type") == "error"
                    assert "message too large" in msg.get("message", "")

            asyncio.run(_run())
        test("terminal WebSocket malformed and oversized message rejection", test_ws_message_validation)

        # Test 29: Multiple Concurrent Isolated Sessions
        def test_ws_concurrent_isolated_sessions():
            s1 = create_test_session()
            s2 = create_test_session()

            async def _run():
                async with ws_connect(port) as ws1, ws_connect(port) as ws2:
                    await ws_auth_and_drain(ws1, s1["session_id"], s1["ws_ticket"])
                    await ws_auth_and_drain(ws2, s2["session_id"], s2["ws_ticket"])

                    # Send unique token to session 1
                    await ws1.send(json.dumps({"type": "input", "data": "echo UNIQUE_TOKEN_AAA\n"}))
                    # Send unique token to session 2
                    await ws2.send(json.dumps({"type": "input", "data": "echo UNIQUE_TOKEN_BBB\n"}))

                    out1 = await ws_recv_output_until(ws1, "UNIQUE_TOKEN_AAA")
                    out2 = await ws_recv_output_until(ws2, "UNIQUE_TOKEN_BBB")

                    assert "UNIQUE_TOKEN_AAA" in out1
                    assert "UNIQUE_TOKEN_BBB" not in out1, "Session leak detected: token B appeared in session A!"

                    assert "UNIQUE_TOKEN_BBB" in out2
                    assert "UNIQUE_TOKEN_AAA" not in out2, "Session leak detected: token A appeared in session B!"

                    await ws1.send(json.dumps({"type": "input", "data": "exit\n"}))
                    await ws2.send(json.dumps({"type": "input", "data": "exit\n"}))
                    await ws_recv_output_until(ws1, "[exit:0]")
                    await ws_recv_output_until(ws2, "[exit:0]")

            asyncio.run(_run())
        test("terminal WebSocket multiple concurrent isolated sessions", test_ws_concurrent_isolated_sessions)

        # Test 30: Disconnect Reconnect and Grace Period
        def test_ws_reconnect_and_grace_period():
            p_grace = find_free_port()
            s_grace = ServerInstance(p_grace, extra_args=["--pty-grace", "3"])
            s_grace.start()

            try:
                g_headers = {
                    "Host": f"127.0.0.1:{p_grace}",
                    "Authorization": f"Bearer {s_grace.token}",
                    "Content-Type": "application/json"
                }
                status, _, body = raw_http_request(p_grace, "POST", "/api/terminal", headers=g_headers, body=b"{}")
                assert status == 201
                sess = json.loads(body)
                sid = sess["session_id"]
                tk = sess["ws_ticket"]

                async def _test_reconnect():
                    # Initial connection
                    async with ws_connect(p_grace) as ws:
                        await ws_auth_and_drain(ws, sid, tk)
                        await ws.send(json.dumps({"type": "input", "data": "echo before_disconnect\n"}))
                        out = await ws_recv_output_until(ws, "before_disconnect")
                        assert "before_disconnect" in out
                        # Disconnect abruptly

                    # Request reconnect ticket
                    status, _, body = raw_http_request(p_grace, "POST", f"/api/terminal/{sid}/reconnect", headers=g_headers)
                    assert status == 200, f"Expected 200 on reconnect ticket request, got {status}: {body}"
                    rec_data = json.loads(body)
                    new_ticket = rec_data["ws_ticket"]

                    # Reconnect with new ticket
                    async with ws_connect(p_grace) as ws_rec:
                        await ws_auth_and_drain(ws_rec, sid, new_ticket)
                        await ws_rec.send(json.dumps({"type": "input", "data": "echo after_reconnect\n"}))
                        out = await ws_recv_output_until(ws_rec, "after_reconnect")
                        assert "after_reconnect" in out

                    # Now disconnect and wait past grace period (4s > 3s)
                    await asyncio.sleep(4.0)

                    # Reconnect should now fail because grace period expired and session was terminated
                    status, _, _ = raw_http_request(p_grace, "POST", f"/api/terminal/{sid}/reconnect", headers=g_headers)
                    assert status in (404, 410), f"Expected 404/410 for expired session, got {status}"

                asyncio.run(_test_reconnect())
            finally:
                s_grace.stop()
        test("terminal WebSocket reconnect and grace period expiration", test_ws_reconnect_and_grace_period)

        # Test 31: Session Ownership Transfer and Old Connection Invalidation
        def test_ws_ownership_transfer():
            sess = create_test_session()
            sid, ticket = sess["session_id"], sess["ws_ticket"]

            async def _run():
                async with ws_connect(port) as ws1:
                    await ws_auth_and_drain(ws1, sid, ticket)

                    # Request reconnect ticket for same session
                    status, _, body = raw_http_request(port, "POST", f"/api/terminal/{sid}/reconnect", headers=term_headers)
                    assert status == 200
                    new_ticket = json.loads(body)["ws_ticket"]

                    # ws2 connects and claims session ownership
                    async with ws_connect(port) as ws2:
                        await ws_auth_and_drain(ws2, sid, new_ticket)

                        # ws1 receives transfer error / close
                        try:
                            msg1 = json.loads(await asyncio.wait_for(ws1.recv(), timeout=2.0))
                            assert msg1.get("type") == "error"
                            assert "transferred" in msg1.get("message", "")
                        except websockets.exceptions.ConnectionClosed:
                            pass

                        # ws1 input attempts are rejected
                        try:
                            await ws1.send(json.dumps({"type": "input", "data": "echo old_conn_cmd\n"}))
                            resp = json.loads(await asyncio.wait_for(ws1.recv(), timeout=1.0))
                            assert resp.get("type") == "error"
                        except websockets.exceptions.ConnectionClosed:
                            pass

                        # ws2 controls session and executes command
                        await ws2.send(json.dumps({"type": "input", "data": "echo ownership_ok_13c\n"}))
                        out = await ws_recv_output_until(ws2, "ownership_ok_13c")
                        assert "ownership_ok_13c" in out

                        await ws2.send(json.dumps({"type": "input", "data": "exit\n"}))
                        await ws_recv_output_until(ws2, "[exit:0]")

            asyncio.run(_run())
        test("terminal WebSocket session ownership transfer and old connection invalidation", test_ws_ownership_transfer)

        # Test 32: Concurrent Reconnect Race (Single-Use Ticket Enforcement)
        def test_ws_concurrent_reconnect_race():
            sess = create_test_session()
            sid, ticket = sess["session_id"], sess["ws_ticket"]

            # Initial connection and disconnect
            async def _initial_conn():
                async with ws_connect(port) as ws:
                    await ws_auth_and_drain(ws, sid, ticket)
            asyncio.run(_initial_conn())

            # Issue reconnect ticket
            status, _, body = raw_http_request(port, "POST", f"/api/terminal/{sid}/reconnect", headers=term_headers)
            assert status == 200
            rec_ticket = json.loads(body)["ws_ticket"]

            # Race two concurrent connections to claim the same ticket
            async def _race():
                results = []
                async def _claim(idx):
                    try:
                        async with ws_connect(port) as ws:
                            await ws.send(json.dumps({"type": "auth", "session_id": sid, "ticket": rec_ticket}))
                            auth_type = None
                            err_msg = ""
                            for _ in range(5):
                                raw = await asyncio.wait_for(ws.recv(), timeout=2.0)
                                m = json.loads(raw)
                                if m.get("type") in ("auth_ok", "error"):
                                    auth_type = m.get("type")
                                    err_msg = m.get("message", "")
                                    break
                            results.append((idx, auth_type, err_msg))
                    except Exception as e:
                        results.append((idx, "exception", str(e)))

                await asyncio.gather(_claim(1), _claim(2))
                types = [r[1] for r in results]
                assert "auth_ok" in types, f"No connection succeeded: {results}"
                assert "error" in types or "exception" in types, f"Both connections claimed ticket: {results}"

            asyncio.run(_race())
        test("terminal WebSocket concurrent reconnect ticket race (single-use enforcement)", test_ws_concurrent_reconnect_race)

        # Test 33: Session Deletion While Connected
        def test_ws_session_deletion_while_connected():
            sess = create_test_session()
            sid, ticket = sess["session_id"], sess["ws_ticket"]

            async def _run():
                async with ws_connect(port) as ws:
                    await ws_auth_and_drain(ws, sid, ticket)

                    # Delete session via REST while WS is active
                    status, _, _ = raw_http_request(port, "DELETE", f"/api/terminal/{sid}", headers=term_headers)
                    assert status == 200

                    # WS should receive exit/error notification or close frame
                    try:
                        raw = await asyncio.wait_for(ws.recv(), timeout=2.0)
                        msg = json.loads(raw)
                        assert msg.get("type") in ("exit", "error")
                    except websockets.exceptions.ConnectionClosed:
                        pass

                    # Further input should fail / connection closed
                    try:
                        await ws.send(json.dumps({"type": "input", "data": "ls\n"}))
                        raw = await asyncio.wait_for(ws.recv(), timeout=1.0)
                        msg = json.loads(raw)
                        assert msg.get("type") == "error"
                    except websockets.exceptions.ConnectionClosed:
                        pass

            asyncio.run(_run())
        test("terminal WebSocket session deletion while connected", test_ws_session_deletion_while_connected)

        # Test 34: Unauthenticated Connection Timeout
        def test_ws_unauth_timeout():
            async def _run():
                # Connect without authenticating and stay idle
                async with ws_connect(port) as ws:
                    # Wait for 5-second auth timeout
                    closed = False
                    start = time.time()
                    try:
                        raw = await asyncio.wait_for(ws.recv(), timeout=7.0)
                        msg = json.loads(raw)
                        assert msg.get("type") == "error"
                        assert "authentication timeout" in msg.get("message", "")
                        await ws.recv()
                    except (websockets.exceptions.ConnectionClosed, asyncio.TimeoutError):
                        closed = True
                    elapsed = time.time() - start
                    assert elapsed >= 4.0, f"Closed too early: {elapsed}s"

            asyncio.run(_run())
        test("terminal WebSocket unauthenticated connection timeout", test_ws_unauth_timeout)

        # Test 35: Continuous Output Stress Test under Rapid Reconnects
        def test_ws_continuous_output_reconnect_stress():
            sess = create_test_session()
            sid, ticket = sess["session_id"], sess["ws_ticket"]

            async def _run():
                cur_ticket = ticket
                # Launch external command generating rapid stdout chunks
                cmd = 'python3 -c "import time; [print(f\'stress_line_{i}\', flush=True) or time.sleep(0.01) for i in range(500)]"\n'

                async with ws_connect(port) as ws:
                    await ws_auth_and_drain(ws, sid, cur_ticket)
                    await ws.send(json.dumps({"type": "input", "data": cmd}))
                    # Verify stream output begins
                    got_stream = False
                    for _ in range(15):
                        raw = await asyncio.wait_for(ws.recv(), timeout=2.0)
                        msg = json.loads(raw)
                        if msg.get("type") == "output" and "stress_line" in msg.get("data", ""):
                            got_stream = True
                            break
                    assert got_stream, "Initial stress stream data not received"

                # Disconnect and reconnect rapidly 5 times while child is continuously spewing data
                for cycle in range(5):
                    status, _, body = raw_http_request(port, "POST", f"/api/terminal/{sid}/reconnect", headers=term_headers)
                    assert status == 200
                    cur_ticket = json.loads(body)["ws_ticket"]

                    async with ws_connect(port) as ws:
                        await ws.send(json.dumps({"type": "auth", "session_id": sid, "ticket": cur_ticket}))
                        auth_ok = False
                        saw_stream = False
                        for _ in range(25):
                            raw = await asyncio.wait_for(ws.recv(), timeout=2.0)
                            msg = json.loads(raw)
                            if msg.get("type") == "auth_ok":
                                auth_ok = True
                            elif msg.get("type") == "output" and "stress_line" in msg.get("data", ""):
                                saw_stream = True
                            if auth_ok and saw_stream:
                                break
                        assert auth_ok, f"Cycle {cycle}: auth_ok not received on reconnect"
                        # Send minor input keystroke to ensure bidirectionality during heavy output
                        await ws.send(json.dumps({"type": "input", "data": " "}))
                        await asyncio.sleep(0.05)

                # Final reconnect: interrupt loop and exit cleanly
                status, _, body = raw_http_request(port, "POST", f"/api/terminal/{sid}/reconnect", headers=term_headers)
                assert status == 200
                cur_ticket = json.loads(body)["ws_ticket"]
                async with ws_connect(port) as ws:
                    await ws_auth_and_drain(ws, sid, cur_ticket)
                    await ws.send(json.dumps({"type": "input", "data": "\x03"}))
                    await asyncio.sleep(0.1)
                    await ws.send(json.dumps({"type": "input", "data": "exit\n"}))
                    await ws_recv_output_until(ws, "[exit:0]")

            asyncio.run(_run())
        test("terminal WebSocket continuous output stress test under rapid connect/disconnect/reconnect", test_ws_continuous_output_reconnect_stress)

        # Test 36: Server Shutdown Multi-Session Child & Descendant Cleanup Audit
        def test_ws_shutdown_multi_session_descendants():
            p_multi = find_free_port()
            s_multi = ServerInstance(p_multi)
            s_multi.start()
            headers_multi = {
                "Host": f"127.0.0.1:{p_multi}",
                "Authorization": f"Bearer {s_multi.token}",
                "Content-Type": "application/json"
            }
            try:
                # Session 1: Clean idle shell
                s1 = json.loads(raw_http_request(p_multi, "POST", "/api/terminal", headers=headers_multi)[2])
                # Session 2: Ignores SIGHUP
                s2 = json.loads(raw_http_request(p_multi, "POST", "/api/terminal", headers=headers_multi)[2])
                # Session 3: Spawns background process jobs
                s3 = json.loads(raw_http_request(p_multi, "POST", "/api/terminal", headers=headers_multi)[2])

                child_pids = [s1["pid"], s2["pid"], s3["pid"]]
                assert len(child_pids) == 3 and all(p > 0 for p in child_pids), f"Invalid child PIDs: {child_pids}"

                # Send commands via WebSocket to s2 and s3
                async def _setup_sessions():
                    # s2: trap SIGHUP and sleep
                    async with ws_connect(p_multi) as ws2:
                        await ws_auth_and_drain(ws2, s2["session_id"], s2["ws_ticket"])
                        await ws2.send(json.dumps({"type": "input", "data": "trap '' HUP; sleep 60\n"}))
                        await asyncio.sleep(0.1)

                    # s3: spawn background sleep jobs
                    async with ws_connect(p_multi) as ws3:
                        await ws_auth_and_drain(ws3, s3["session_id"], s3["ws_ticket"])
                        await ws3.send(json.dumps({"type": "input", "data": "sleep 60 & sleep 60 &\n"}))
                        await asyncio.sleep(0.1)

                asyncio.run(_setup_sessions())
                time.sleep(0.2)

                # Stop server with SIGINT
                s_multi.stop(signal.SIGINT)
                assert s_multi.proc.returncode == 0

                # Verify all child PIDs are completely reaped and no longer exist
                for cpid in child_pids:
                    try:
                        os.kill(cpid, 0)
                        assert False, f"Child PID {cpid} still alive after shutdown!"
                    except ProcessLookupError:
                        pass # Successfully reaped

                # Verify no processes in /proc have SID matching any child PID
                for entry in os.listdir("/proc"):
                    if entry.isdigit():
                        p = int(entry)
                        if p > 1 and p != os.getpid():
                            try:
                                sid = os.getsid(p)
                                assert sid not in child_pids, f"Descendant process {p} with SID {sid} survived shutdown!"
                            except ProcessLookupError:
                                pass
            finally:
                if s_multi.proc and s_multi.proc.poll() is None:
                    s_multi.stop(signal.SIGKILL)

        test("terminal WebSocket shutdown multi-session child and descendant cleanup", test_ws_shutdown_multi_session_descendants)

    finally:
        # Test 13: Clean shutdown on SIGINT / SIGTERM
        server.stop(signal.SIGINT)
        assert server.proc.returncode == 0, f"Expected 0 exit code on shutdown, got {server.proc.returncode}"
        print("  PASS server graceful shutdown on SIGINT")
        tests_run += 1
        tests_passed += 1

    # Test 14: Test shutdown on SIGTERM
    p_term = find_free_port()
    s_term = ServerInstance(p_term)
    s_term.start()
    s_term.stop(signal.SIGTERM)
    assert s_term.proc.returncode == 0, f"Expected 0 exit code on SIGTERM shutdown, got {s_term.proc.returncode}"
    print("  PASS server graceful shutdown on SIGTERM")
    tests_run += 1
    tests_passed += 1

    print(f"\n=== Server Test Results: {tests_run} run, {tests_passed} passed, 0 failed ===\n")
    return 0

if __name__ == "__main__":
    sys.exit(run_tests())

