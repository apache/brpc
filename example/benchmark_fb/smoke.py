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

"""Bounded functional smoke: no network dependency and no throughput measurement."""

import argparse
import json
import os
from pathlib import Path
import re
import selectors
import signal
import subprocess
import sys
import tempfile
import time


def remaining(deadline):
    seconds = deadline - time.monotonic()
    if seconds <= 0:
        raise RuntimeError("smoke deadline exceeded")
    return seconds


def wait_ready(process, deadline):
    buffer = b""
    os.set_blocking(process.stdout.fileno(), False)
    with selectors.DefaultSelector() as selector:
        selector.register(process.stdout, selectors.EVENT_READ)
        while True:
            if process.poll() is not None:
                raise RuntimeError("server exited before readiness")
            events = selector.select(min(0.2, remaining(deadline)))
            if not events:
                continue
            chunk = os.read(process.stdout.fileno(), 4096)
            if not chunk:
                raise RuntimeError("server closed stdout before readiness")
            buffer += chunk
            if len(buffer) > 65536:
                raise RuntimeError("unexpectedly large server readiness output")
            while b"\n" in buffer:
                line, buffer = buffer.split(b"\n", 1)
                match = re.fullmatch(rb"BRPC_FB_READY (127\.0\.0\.1):([0-9]{1,5})", line)
                if match:
                    port = int(match.group(2))
                    if not 1 <= port <= 65535:
                        raise RuntimeError("server published an invalid port")
                    return "127.0.0.1:" + str(port)


def run_client(binary, endpoint, deadline, *, count, size, attachment,
               threads=1, omit=False, corrupt=False, connection="single"):
    command = [
        str(binary), "--server=" + endpoint,
        "--request_count=" + str(count), "--thread_num=" + str(threads),
        "--request_size=" + str(size), "--attachment_size=" + str(attachment),
        "--connection_type=" + connection,
        "--omit_message=" + str(omit).lower(),
        "--corrupt_request=" + str(corrupt).lower(),
        "--timeout_ms=1000", "--deadline_ms=5000",
    ]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, timeout=min(8.0, remaining(deadline)), check=False)
    if result.returncode != 0:
        raise RuntimeError("client failed ({}):\n{}\n{}".format(
            result.returncode, result.stdout, result.stderr))
    try:
        report = json.loads(result.stdout)
    except (ValueError, TypeError) as error:
        raise RuntimeError("invalid client summary: " + result.stdout) from error
    expected = {"completed": count, "successes": 0 if corrupt else count,
                "expected_rejections": count if corrupt else 0, "failures": 0}
    if report != expected:
        raise RuntimeError("unexpected client result: " + repr(report))


def stop_owned_server(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)
            raise RuntimeError("server required SIGKILL instead of a clean Stop/Join")
    if process.returncode != 0:
        raise RuntimeError("server exited with status " + str(process.returncode))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True, help="benchmark_fb_server executable")
    parser.add_argument("--client", required=True, help="benchmark_fb_client executable")
    args = parser.parse_args()
    process = None
    failure = None
    deadline = time.monotonic() + 35
    with tempfile.TemporaryFile(mode="w+b") as server_log:
        try:
            server = Path(args.server).resolve(strict=True)
            client = Path(args.client).resolve(strict=True)
            process = subprocess.Popen(
                [str(server), "--listen_addr=127.0.0.1:0", "--duration_s=45",
                 "--max_concurrency=8"],
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=server_log,
                start_new_session=True,
            )
            endpoint = wait_ready(process, min(deadline, time.monotonic() + 10))
            run_client(client, endpoint, deadline,
                       count=6, size=8193, attachment=257, threads=2)
            run_client(client, endpoint, deadline,
                       count=2, size=0, attachment=31, connection="pooled")
            run_client(client, endpoint, deadline,
                       count=2, size=0, attachment=0, omit=True, connection="short")
            run_client(client, endpoint, deadline,
                       count=2, size=17, attachment=5, corrupt=True)
            # Keep the same server alive after the malformed requests.
            run_client(client, endpoint, deadline, count=3, size=33, attachment=9)
            if process.poll() is not None:
                raise RuntimeError("server exited during the smoke")
        except (Exception, KeyboardInterrupt) as error:
            failure = str(error) or type(error).__name__
        finally:
            if process is not None:
                try:
                    stop_owned_server(process)
                except Exception as error:
                    failure = failure or str(error)
                if process.stdout is not None:
                    process.stdout.close()
            if failure:
                server_log.seek(0)
                log = server_log.read(65536).decode("utf-8", errors="replace")
                sys.stderr.write("benchmark_fb smoke failed: " + failure + "\n" + log)
    if failure:
        return 1
    print("benchmark_fb smoke passed: 13 verified replies, 2 schema rejections, clean shutdown")
    return 0


def handle_termination(signum, frame):
    raise KeyboardInterrupt()


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, handle_termination)
    sys.exit(main())
