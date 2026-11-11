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

