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

import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
import run_recovery_injection as runner


class RecoveryInjectionTest(unittest.TestCase):
    def test_decode_text_and_json_bvar_formats(self):
        value = {"mode": "auto", "failures": {"send_post": 1}}
        raw = json.dumps(value)
        for body in (raw, 'urma_recovery_status : "' + raw + '"',
                     "urma_recovery_status : " + json.dumps(raw)):
            self.assertEqual(value, runner.decode_bvar(body))
        self.assertEqual(123, runner.decode_bvar("pid : 123\n"))

    def test_samples_skip_headers_summary_and_partial_rows(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "client.csv"
            path.write_text("seconds,mode,success,failure\n"
                            "0,auto,1,0,1,0,0\n1,auto,"
                            "\ncompleted success=1 failure=0\n")
            self.assertEqual([[0, 1, 0, 1, 0, 0]], runner.read_samples(path))

    def phases(self):
        baseline = {"mode": "auto", "epoch": 2, "negotiated_connections": 10,
                    "cooled_peers": 0, "peer_cooldown_admissions": 0,
                    "failures": {"send_post": 0}}
        cooldown = copy.deepcopy(baseline)
        cooldown.update(cooled_peers=1, peer_cooldown_admissions=3)
        cooldown["failures"]["send_post"] = 1
        recovered = copy.deepcopy(cooldown)
        recovered.update(cooled_peers=0, negotiated_connections=12)
        return baseline, cooldown, recovered

    def test_validate_complete_recovery(self):
        runner.validate_phases(*self.phases())

    def test_persistent_connections_can_reuse_tcp_after_cooldown(self):
        baseline, cooldown, recovered = self.phases()
        recovered["negotiated_connections"] = cooldown["negotiated_connections"]
        runner.validate_phases(baseline, cooldown, recovered, require_urma_reconnect=False)
        with self.assertRaises(RuntimeError):
            runner.validate_phases(baseline, cooldown, recovered)

    def test_reject_stale_zero_failure_snapshot(self):
        baseline, cooldown, recovered = self.phases()
        cooldown["failures"]["send_post"] = 0
        with self.assertRaises(RuntimeError):
            runner.validate_phases(baseline, cooldown, recovered)

    def test_reject_no_recovery_and_epoch_changes(self):
        for field, value in (("negotiated_connections", 10), ("epoch", 3)):
            baseline, cooldown, recovered = self.phases()
            recovered[field] = value
            with self.assertRaises(RuntimeError):
                runner.validate_phases(baseline, cooldown, recovered)

    def test_wait_for_readiness_instead_of_fixed_startup_delay(self):
        proc = mock.Mock()
        proc.poll.return_value = None
        ready = mock.Mock(side_effect=[False, False, "ready"])
        with mock.patch.object(runner.time, "sleep"):
            self.assertEqual("ready", runner.wait_for(proc, ready, 1, "baseline"))
        self.assertEqual(3, ready.call_count)

    def test_exit_before_readiness_is_a_failure(self):
        proc = mock.Mock()
        proc.poll.return_value = 1
        with self.assertRaises(RuntimeError):
            runner.wait_for(proc, lambda: False, 1, "baseline")


if __name__ == "__main__":
    unittest.main()
