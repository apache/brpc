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

import argparse
import base64
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import signal
import select
import subprocess
import sys
import time
import tarfile
import tempfile
import unittest
from unittest import mock
import xml.etree.ElementTree as ET

SCRIPT = Path(__file__).with_name("flatbuffers-on.py")
SPEC = importlib.util.spec_from_file_location("flatbuffers_on", SCRIPT)
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)


class GateTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.listing = self.root / "list.txt"
        self.listing.write_text("Suite.\n  First\n  Second\n")
        self.xml = self.root / "tests.xml"

    def report(self, names=("First", "Second"), **attributes):
        root = ET.Element("testsuites", tests=str(len(names)), failures="0", errors="0", disabled="0")
        root.attrib.update(attributes)
        suite = ET.SubElement(root, "testsuite", name="Suite")
        for name in names:
            ET.SubElement(suite, "testcase", classname="Suite", name=name,
                          status="run", result="completed")
        ET.ElementTree(root).write(self.xml)
        return root

    def save(self, root):
        ET.ElementTree(root).write(self.xml)

    def test_successful_report_matches_discovery(self):
        self.report()
        self.assertEqual(2, gate.verify_gtests(self.xml, self.listing, 2)["executed"])

    def test_parameterized_listing(self):
        self.assertEqual({"Typed/0.Works/1"}, gate.list_gtests(
            "Running main from gtest\nTyped/0. # TypeParam = int\n  Works/1 # GetParam = 1\n"))

    def test_empty_or_disabled_listing_rejected(self):
        for text in ("", "no tests", "DISABLED_Suite.\n  Test\n", "Suite.\n  DISABLED_Test\n"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                gate.list_gtests(text)

    def test_duplicate_listing_rejected(self):
        with self.assertRaises(ValueError):
            gate.list_gtests("Suite.\n  First\n  First\n")

    def test_empty_or_missing_xml_rejected(self):
        with self.assertRaises(ValueError):
            gate.verify_gtests(self.xml, self.listing, 1)
        self.report(())
        with self.assertRaises(ValueError):
            gate.verify_gtests(self.xml, self.listing, 1)

    def test_filtered_and_below_minimum_rejected(self):
        self.report(("First",))
        with self.assertRaises(ValueError):
            gate.verify_gtests(self.xml, self.listing, 1)
        self.report()
        with self.assertRaises(ValueError):
            gate.verify_gtests(self.xml, self.listing, 3)

    def test_failure_error_skip_disabled_rejected(self):
        for tag in ("failure", "error", "skipped"):
            root = self.report()
            ET.SubElement(next(root.iter("testcase")), tag)
            self.save(root)
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                gate.verify_gtests(self.xml, self.listing, 2)
        for attribute in ("failures", "errors", "disabled", "skipped"):
            self.report(**{attribute: "1"})
            with self.subTest(attribute=attribute), self.assertRaises(ValueError):
                gate.verify_gtests(self.xml, self.listing, 2)

    def test_unexecuted_and_duplicate_cases_rejected(self):
        root = self.report()
        next(root.iter("testcase")).set("status", "notrun")
        self.save(root)
        with self.assertRaises(ValueError):
            gate.verify_gtests(self.xml, self.listing, 2)
        self.report(("First", "First"))
        with self.assertRaises(ValueError):
            gate.verify_gtests(self.xml, self.listing, 2)

    def test_incorrect_root_count_rejected(self):
        self.report(tests="999")
        with self.assertRaises(ValueError):
            gate.verify_gtests(self.xml, self.listing, 2)

    def test_ctest_requires_runtime_and_acceptance(self):
        self.report(("acceptance", "runtime"))
        self.assertEqual(2, gate.verify_ctest(self.xml, {"acceptance", "runtime"})["executed"])
        self.report(("acceptance",))
        with self.assertRaises(ValueError):
            gate.verify_ctest(self.xml, {"acceptance", "runtime"})

    def archive(self, name="pkg/file", link=False):
        path = self.root / "archive.tar.gz"
        with tarfile.open(path, "w:gz") as archive:
            info = tarfile.TarInfo(name)
            if link:
                info.type = tarfile.SYMTYPE
                info.linkname = "/outside"
                archive.addfile(info)
            else:
                info.size = 4
                archive.addfile(info, io.BytesIO(b"data"))
        return path, hashlib.sha256(path.read_bytes()).hexdigest()

    def test_pinned_archive_extracts(self):
        path, digest = self.archive()
        extracted = gate.extract_archive(path, digest, self.root / "extract", "pkg")
        self.assertEqual(b"data", (extracted / "file").read_bytes())

    def test_archive_checksum_paths_and_links_rejected(self):
        path, _ = self.archive()
        with self.assertRaises(ValueError):
            gate.extract_archive(path, "0" * 64, self.root / "extract", "pkg")
        for name, link in (("../outside", False), ("pkg/../outside", False),
                           ("/absolute", False), ("wrong/file", False), ("pkg/link", True)):
            path, digest = self.archive(name, link)
            with self.subTest(name=name), self.assertRaises(ValueError):
                gate.extract_archive(path, digest, self.root / "extract", "pkg")
            self.assertFalse((self.root / "extract").exists())

    def linked_archive(self, entries):
        path = self.root / "linked.tar.gz"
        with tarfile.open(path, "w:gz") as archive:
            for name, target in entries:
                info = tarfile.TarInfo(name)
                if target is not None:
                    info.type = tarfile.SYMTYPE
                    info.linkname = target
                    archive.addfile(info)
                else:
                    info.size = 4
                    archive.addfile(info, io.BytesIO(b"data"))
        return path, hashlib.sha256(path.read_bytes()).hexdigest()

    def test_internal_archive_symlinks_extract_after_data(self):
        path, digest = self.linked_archive([
            ("pkg/java/src/test/java/Example", "../../../../tests/Example"),
            ("pkg/ts/package.json", "../package.json"),
            ("pkg/tests/Example/schema.fbs", None), ("pkg/package.json", None)])
        extracted = gate.extract_archive(path, digest, self.root / "extract", "pkg")
        self.assertTrue((extracted / "java/src/test/java/Example").is_symlink())
        self.assertEqual(b"data", (extracted / "java/src/test/java/Example/schema.fbs").read_bytes())
        self.assertEqual(b"data", (extracted / "ts/package.json").read_bytes())

    def test_escaping_chained_and_ancestor_links_rejected(self):
        cases = [
            [("pkg/link", "../../outside")], [("pkg", ".")],
            [("pkg/link", "next"), ("pkg/next", "file"), ("pkg/file", None)],
            [("pkg/dir", "target"), ("pkg/dir/file", None)],
            [("pkg/a", "."), ("pkg/link", "nested/../a/../outside")],
        ]
        for entries in cases:
            path, digest = self.linked_archive(entries)
            with self.subTest(entries=entries), self.assertRaises(ValueError):
                gate.extract_archive(path, digest, self.root / "extract", "pkg")
            self.assertFalse((self.root / "extract").exists())

    def test_duplicate_archive_members_rejected(self):
        path, digest = self.linked_archive([("pkg/file", None), ("pkg/file", None)])
        with self.assertRaises(ValueError):
            gate.extract_archive(path, digest, self.root / "extract", "pkg")
        self.assertFalse((self.root / "extract").exists())

    def runner(self):
        source = self.root / "source"
        source.mkdir()
        return gate.Runner(argparse.Namespace(source=source, work=self.root / "work",
                           build_system="cmake", dependency_prefix=[]))

    def test_work_directory_is_exclusive(self):
        runner = self.runner()
        with self.assertRaises(FileExistsError):
            gate.Runner(runner.args)

    def test_filter_environment_is_reset(self):
        with mock.patch.dict(os.environ, {"GTEST_FILTER": "Wrong.*", "GTEST_SHARD_INDEX": "2"}):
            runner = self.runner()
        self.assertEqual("*", runner.env["GTEST_FILTER"])
        self.assertNotIn("GTEST_SHARD_INDEX", runner.env)

    def test_work_inside_checkout_rejected(self):
        source = self.root / "checkout"
        source.mkdir()
        args = argparse.Namespace(source=source, work=source / "build", build_system="cmake", dependency_prefix=[])
        with self.assertRaises(ValueError):
            gate.Runner(args)

    def test_source_directory_link_cannot_escape_checkout(self):
        runner = self.runner()
        outside = self.root / "outside"
        outside.mkdir()
        (outside / "file").write_text("must not be copied")
        (runner.source / "link").symlink_to(outside, target_is_directory=True)
        self.listing.write_bytes(b"link/file\0")
        with mock.patch.object(runner, "step", return_value=self.listing):
            with self.assertRaises(ValueError):
                runner.copy_source()
        self.assertFalse((runner.work / "source/link/file").exists())

    def test_cmake_uses_explicit_openssl_prefix(self):
        runner = self.runner()
        runner.args.protoc = None
        self.assertFalse(any(option.startswith("-DOPENSSL_ROOT_DIR=")
                             for option in runner.common_cmake()))
        prefix = self.root / "openssl"
        (prefix / "include/openssl").mkdir(parents=True)
        (prefix / "include/openssl/ssl.h").write_text("header fixture")
        runner.prefixes = [self.root / "unrelated", prefix]
        self.assertIn("-DOPENSSL_ROOT_DIR=" + str(prefix), runner.common_cmake())

    def test_make_builds_supplied_gtest_in_private_prefix(self):
        runner = self.runner()
        prefix = self.root / "provided-fb"
        (prefix / "include/flatbuffers").mkdir(parents=True)
        (prefix / "include/flatbuffers/base.h").write_text(
            "#define FLATBUFFERS_VERSION_MAJOR 25\n#define FLATBUFFERS_VERSION_MINOR 2\n#define FLATBUFFERS_VERSION_REVISION 10\n")
        (prefix / "lib").mkdir()
        (prefix / "lib/libflatbuffers.a").touch()
        source = self.root / "provided-gtest"
        source.mkdir()
        runner.args.build_system = "make"
        runner.args.flatbuffers_prefix = prefix
        runner.args.gtest_source = source
        runner.args.jobs = 2
        runner.args.protoc = Path(sys.executable)
        version = self.root / "version.log"
        version.write_text("flatc version 25.2.10\n")
        with mock.patch.object(runner, "step", return_value=version) as step:
            with mock.patch.object(runner, "download") as download:
                runner.prepare_dependencies()
        download.assert_not_called()
        configure = next(call.args[1] for call in step.call_args_list
                         if call.args[0] == "gtest-configure")
        self.assertIn(source.resolve(), configure)
        self.assertIn("-DCMAKE_INSTALL_PREFIX=" + str(runner.work / "gtest-prefix"), configure)
        self.assertEqual(runner.work / "gtest-prefix", runner.prefixes[0])
        self.assertFalse((prefix / "include/gtest").exists())

    def exercise_process_group(self, leader_exits):
        ready = self.root / "child.ready"
        child_code = ("import signal,time,pathlib; signal.signal(signal.SIGTERM, signal.SIG_IGN); "
                      "pathlib.Path(" + repr(str(ready)) + ").touch(); time.sleep(60)")
        leader_code = ("import subprocess,sys,time,pathlib; "
                       "child=subprocess.Popen([sys.executable,'-c'," + repr(child_code) + "], "
                       "stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL); "
                       "print(child.pid,flush=True); "
                       + ("sys.exit(0)" if leader_exits else "time.sleep(60)"))
        process = subprocess.Popen([sys.executable, "-c", leader_code],
                                   stdout=subprocess.PIPE, text=True, start_new_session=True)
        try:
            readable, _, _ = select.select([process.stdout], [], [], 3)
            self.assertTrue(readable, "group leader did not publish child PID")
            child = int(process.stdout.readline())
            deadline = time.monotonic() + 3
            while not ready.exists() and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertTrue(ready.exists())
            if leader_exits:
                process.wait(timeout=3)
            gate.stop_process(process, grace=0.1)
            deadline = time.monotonic() + 3
            while True:
                result = subprocess.run(["ps", "-o", "stat=", "-p", str(child)],
                                        stdout=subprocess.PIPE, text=True, check=False)
                state = result.stdout.strip()
                if not state or state.startswith("Z"):
                    break
                self.assertLess(time.monotonic(), deadline, "owned child survived cleanup")
                time.sleep(0.02)
        finally:
            gate.signal_group(process, signal.SIGKILL)
            process.wait(timeout=3)
            process.stdout.close()

    def test_cleanup_after_group_leader_exits(self):
        self.exercise_process_group(True)

    def test_cleanup_kills_child_ignoring_term(self):
        self.exercise_process_group(False)

    def test_timeout_is_a_recorded_failure(self):
        runner = self.runner()
        with self.assertRaises(RuntimeError):
            runner.step("timeout", [sys.executable, "-c", "import time; time.sleep(60)"], timeout=0.05)
        self.assertEqual(124, json.loads((runner.evidence / "summary.json").read_text())["steps"][0]["exitcode"])

    def test_flatbuffers_pin_matches_both_bazel_definitions(self):
        root = SCRIPT.resolve().parents[2]
        for filename in ("MODULE.bazel", "WORKSPACE"):
            text = (root / filename).read_text()
            integrity = "sha256-" + base64.b64encode(bytes.fromhex(gate.FLATBUFFERS_SHA256)).decode()
            self.assertTrue(gate.FLATBUFFERS_SHA256 in text or integrity in text,
                            filename + ": FlatBuffers checksum differs")
            self.assertTrue("v" + gate.FLATBUFFERS_VERSION + ".tar.gz" in text,
                            filename + ": FlatBuffers version differs")

    def test_workflow_script_paths_and_characters(self):
        root = SCRIPT.resolve().parents[2]
        workflow = (root / ".github/workflows/flatbuffers-on.yml").read_text()
        self.assertFalse(any(ord(char) < 32 and char not in "\n\r\t" for char in workflow))
        for filename in ("flatbuffers-on.py", "test_flatbuffers_on.py"):
            self.assertIn(".github/scripts/" + filename, workflow)
            self.assertTrue((SCRIPT.parent / filename).is_file())
        self.assertNotIn("pull_request_target", workflow)
        self.assertNotIn("continue-on-error", workflow)


if __name__ == "__main__":
    unittest.main()
