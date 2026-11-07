#!/usr/bin/env python3
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

"""Check one SEND failure, peer cooldown, TCP admission and URMA recovery."""

import argparse
import csv
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import time
import urllib.request


def decode_bvar(text):
    match = re.match(r"^\w+\s*:\s*(.*)$", text.strip(), re.DOTALL)
    value = match.group(1) if match else text.strip()
    try:
        result = json.loads(value)
        return json.loads(result) if isinstance(result, str) else result
    except json.JSONDecodeError:
        # Text /vars may quote the JSON object without escaping its quotes.
        return json.loads(value[value.index("{"):value.rindex("}") + 1])


def read_samples(path):
    with path.open() as stream:
        return [list(map(int, row[:1] + row[2:])) for row in csv.reader(stream)
                if len(row) == 7 and row[0].isdigit()]


def wait_for(proc, predicate, timeout, description):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        if proc.poll() is not None:
            raise RuntimeError("Client exited while waiting for " + description)
        time.sleep(0.1)
    raise RuntimeError("Timed out waiting for " + description)


def validate_phases(baseline, cooldown, recovered, require_urma_reconnect=True):
    if any(s["mode"] != "auto" for s in (baseline, cooldown, recovered)):
        raise RuntimeError("Expected AUTO throughout the run")
    if len({s["epoch"] for s in (baseline, cooldown, recovered)}) != 1:
        raise RuntimeError("Recovery epoch changed during injection")
    if (baseline["negotiated_connections"] <= 0 or baseline["cooled_peers"] != 0
            or baseline["failures"]["send_post"] != 0):
        raise RuntimeError("Baseline must use URMA with no injected failure")
    if (cooldown["failures"]["send_post"] != 1 or cooldown["cooled_peers"] != 1
            or cooldown["peer_cooldown_admissions"] <= 0):
        raise RuntimeError("Expected one SEND failure and TCP peer-cooldown admission")
    negotiated = recovered["negotiated_connections"]
    previous = cooldown["negotiated_connections"]
    if (recovered["failures"]["send_post"] != 1 or recovered["cooled_peers"] != 0
            or negotiated < previous or (require_urma_reconnect and negotiated == previous)
            or recovered["peer_cooldown_admissions"] < cooldown["peer_cooldown_admissions"]):
        raise RuntimeError("Expected URMA negotiations to resume after cooldown")


def run(args):
    client = Path(args.client).resolve()
    injector = Path(args.injector).resolve()
    if not client.is_file() or not injector.is_file():
        raise RuntimeError("Build urma_recovery_client and urma_send_failure_injector first")
    output = Path(tempfile.mkdtemp(prefix="urma-injection-", dir=".")).resolve()
    marker = output / "inject.once"
    csv_path = output / "client.csv"
    log_path = output / "client.log"
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    # All metric requests go directly to this client's loopback listener.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def read_var(name):
        request = urllib.request.Request(
            "http://127.0.0.1:{}/vars/{}?console=1".format(port, name),
            headers={"Accept": "text/plain"})
        with opener.open(request, timeout=2) as response:
            text = response.read().decode()
        (output / ("latest-" + name + ".txt")).write_text(text)
        return decode_bvar(text)

    def record_phase(name, status):
        (output / (name + ".json")).write_text(json.dumps(status, indent=2))
        print("{}: send_post={} cooled_peers={} tcp={} negotiated={}".format(
            name, status["failures"]["send_post"], status["cooled_peers"],
            status["peer_cooldown_admissions"], status["negotiated_connections"]), flush=True)
        return status

    command = [str(client), "--transport=urma", "--mode=auto",
               "--server=" + args.server, "--urma_device=" + args.device,
               "--urma_tp_type=" + args.tp_type,
               "--urma_client_handshake_version=" + str(args.handshake_version),
               "--urma_buffer_count=4096", "--connection_type=" + args.connection_type,
               "--attachment_size=" + str(args.attachment_size),
               "--urma_use_polling=" + str(args.polling).lower(),
               "--interval_ms=200", "--timeout_ms=3000",
               "--seconds=" + str(args.seconds),
               "--cooldown_ms=" + str(args.cooldown_ms), "--dummy_port=" + str(port)]
    env = dict(os.environ, LD_PRELOAD=str(injector),
               BRPC_URMA_FAIL_ONCE_FILE=str(marker))
    proc = None
    print("Artifacts: " + str(output), flush=True)
    try:
        with csv_path.open("w") as stdout, log_path.open("w") as stderr:
            proc = subprocess.Popen(command, stdout=stdout, stderr=stderr, env=env)
            print("Client PID={}; waiting for successful URMA RPCs".format(proc.pid), flush=True)

            def ready():
                rows = read_samples(csv_path)
                if rows and rows[-1][2] != 0:
                    raise RuntimeError("RPC failed before injection")
                return rows and rows[-1][1] > 0 and rows[-1][3] > 0

            wait_for(proc, ready, args.startup_timeout, "successful URMA baseline")
            if read_var("pid") != proc.pid:
                raise RuntimeError("Metric listener PID does not match the test client")
            time.sleep(args.baseline_seconds)
            phases = {"baseline": record_phase("baseline", read_var("urma_recovery_status"))}
            baseline = phases["baseline"]
            if (baseline["failures"]["send_post"] or baseline["cooled_peers"]
                    or baseline["negotiated_connections"] <= 0):
                raise RuntimeError("Unexpected baseline metrics")
            marker.touch()

            def cooling():
                status = read_var("urma_recovery_status")
                return status if (status["failures"]["send_post"] == 1
                                  and status["cooled_peers"] == 1
                                  and status["peer_cooldown_admissions"] > 0) else None

            phases["cooldown"] = record_phase("cooldown", wait_for(
                proc, cooling, args.cooldown_ms / 1000, "peer cooldown and TCP admission"))
            require_urma_reconnect = args.connection_type == "short"
            recovery_success = None

            def recovering():
                nonlocal recovery_success
                status = read_var("urma_recovery_status")
                if status["cooled_peers"] != 0:
                    return None
                if require_urma_reconnect:
                    return status if (status["negotiated_connections"] >
                                      phases["cooldown"]["negotiated_connections"]) else None
                rows = read_samples(csv_path)
                if recovery_success is None:
                    recovery_success = rows[-1][1]
                return status if rows[-1][1] > recovery_success else None

            phases["recovered"] = record_phase("recovered", wait_for(
                proc, recovering, args.cooldown_ms / 1000 + 5, "URMA recovery"))
            validate_phases(**phases, require_urma_reconnect=require_urma_reconnect)
            failures_at_recovery = read_samples(csv_path)[-1][2]
            (output / "snapshots.json").write_text(json.dumps(
                {"pid": proc.pid, "port": port, "phases": phases}, indent=2))
            rc = proc.wait(timeout=args.seconds + 10)
        summary = re.search(r"completed success=(\d+) failure=(\d+)", csv_path.read_text())
        expected_failures = 1 if require_urma_reconnect else failures_at_recovery
        if (rc != 0 or not summary or int(summary[1]) == 0
                or expected_failures < 1 or int(summary[2]) != expected_failures):
            raise RuntimeError("Expected RPC recovery with no further failures afterward")
        if args.attachment_size:
            validation = re.search(r"validation_failures=(\d+)", csv_path.read_text())
            if not validation or int(validation[1]) != 0:
                raise RuntimeError("Attachment content validation failed")
        if log_path.read_text().count("injected one data SEND post failure") != 1:
            raise RuntimeError("Expected exactly one injection log")
        last = read_samples(csv_path)[-1]
        if last[5] != 0 or last[4] != phases["recovered"]["peer_cooldown_admissions"]:
            raise RuntimeError("TCP cooldown admissions must stop after URMA recovery")
        print(csv_path.read_text().splitlines()[-1])
        recovered_transport = "URMA" if require_urma_reconnect else "RPC"
        print("PASS: one SEND failure, TCP cooldown, and " + recovered_transport + " recovery")
    finally:
        if proc is not None and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True)
    parser.add_argument("--device", required=True)
    parser.add_argument("--client", default="build/urma-example/urma_recovery_client")
    parser.add_argument("--injector", default="build/urma-example/liburma_send_failure_injector.so")
    parser.add_argument("--tp-type", choices=("rtp", "ctp", "auto"), default="rtp")
    parser.add_argument("--handshake-version", type=int, choices=(2, 3), default=3)
    parser.add_argument("--connection-type", choices=("short", "single", "pooled"), default="short")
    parser.add_argument("--attachment-size", type=int, default=0)
    parser.add_argument("--polling", action="store_true")
    parser.add_argument("--seconds", type=int, default=40)
    parser.add_argument("--cooldown-ms", type=int, default=10000)
    parser.add_argument("--baseline-seconds", type=float, default=5)
    parser.add_argument("--startup-timeout", type=float, default=120)
    args = parser.parse_args()
    if (args.cooldown_ms <= 0 or args.attachment_size < 0
            or args.baseline_seconds < 0 or args.startup_timeout <= 0
            or args.seconds < args.baseline_seconds + args.cooldown_ms / 1000 + 5):
        parser.error("Duration must cover baseline, cooldown and at least five seconds of recovery")
    try:
        run(args)
    except (OSError, ValueError, KeyError, TypeError, RuntimeError, subprocess.TimeoutExpired) as error:
        parser.exit(1, "FAIL: {}\n".format(error))


if __name__ == "__main__":
    main()
