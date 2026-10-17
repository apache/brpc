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

"""Run the focused FlatBuffers ON gates; never accept empty or skipped tests."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tarfile
import time
import xml.etree.ElementTree as ET

# Keep the FlatBuffers pin synchronized with MODULE.bazel and WORKSPACE.
FLATBUFFERS_VERSION = "25.2.10"
FLATBUFFERS_SHA256 = "b9c2df49707c57a48fc0923d52b8c73beb72d675f9d44b2211e4569be40a7421"
GTEST_VERSION = "1.14.0"
GTEST_SHA256 = "8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7"
TESTS = {"brpc_flatbuffers_unittest": 21, "brpc_flatbuffers_protocol_unittest": 33}


def list_gtests(text):
    names = set()
    suite = None
    for line in text.splitlines():
        value = line.split("#", 1)[0].strip()
        if not value:
            continue
        if not line[0].isspace() and value.endswith("."):
            suite = value
        elif line[0].isspace() and suite and re.fullmatch(r"[A-Za-z0-9_/]+", value):
            name = suite + value
            if any(part.startswith("DISABLED_") for part in re.split(r"[./]", name)):
                raise ValueError("Disabled test in ON gate: " + name)
            if name in names:
                raise ValueError("Duplicate discovered test: " + name)
            names.add(name)
    if not names:
        raise ValueError("No GoogleTest cases discovered")
    return names


def xml_cases(path):
    path = Path(path)
    if not path.is_file() or path.stat().st_size > 16 * 1024 * 1024:
        raise ValueError("Missing or oversized test XML: " + str(path))
    root = ET.parse(path).getroot()
    for suite in root.iter():
        if suite.tag in ("testsuites", "testsuite"):
            for attribute in ("failures", "errors", "disabled", "skipped"):
                if int(suite.get(attribute, "0")) != 0:
                    raise ValueError("Nonzero " + attribute + " in " + str(path))
    cases = list(root.iter("testcase"))
    if not cases:
        raise ValueError("Test XML contains no cases: " + str(path))
    for case in cases:
        if (case.find("failure") is not None or case.find("error") is not None or
                case.find("skipped") is not None or case.get("status") == "notrun" or
                case.get("result") in ("skipped", "suppressed")):
            raise ValueError("Failed or unexecuted case: " + str(case.attrib))
    return root, cases


def verify_gtests(xml, listing, minimum):
    expected = list_gtests(Path(listing).read_text())
    root, cases = xml_cases(xml)
    actual = [case.get("classname", "") + "." + case.get("name", "") for case in cases]
    if len(expected) < minimum or len(actual) != len(set(actual)) or set(actual) != expected:
        raise ValueError("Executed cases differ from discovery or minimum: " + str(xml))
    if int(root.get("tests", "-1")) != len(actual):
        raise ValueError("Incorrect XML test count: " + str(xml))
    return {"executed": len(actual), "failed": 0, "skipped": 0}


def verify_ctest(xml, required):
    _, cases = xml_cases(xml)
    names = [case.get("name", "") for case in cases]
    if len(names) != len(set(names)) or not set(required) <= set(names):
        raise ValueError("Required CTest cases did not execute: " + str(required))
    return {"executed": len(names), "names": names, "failed": 0, "skipped": 0}


def extract_archive(archive, digest, destination, prefix):
    archive = Path(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
        raise ValueError("Dependency checksum mismatch: " + str(archive))
    destination = Path(destination)
    if destination.exists():
        raise ValueError("Refusing to overwrite dependency directory")
    with tarfile.open(archive, "r:gz") as source:
        members = source.getmembers()
        if len(members) > 30000 or sum(item.size for item in members) > 512 * 1024 * 1024:
            raise ValueError("Dependency archive exceeds extraction limits")
        paths = set()
        links = {}
        for member in members:
            path = PurePosixPath(member.name)
            if (not path.parts or path.is_absolute() or ".." in path.parts or
                    path.parts[0] != prefix or path in paths or
                    not (member.isdir() or member.isfile() or member.issym())):
                raise ValueError("Unsafe dependency archive member: " + member.name)
            paths.add(path)
            if member.issym():
                if len(path.parts) < 2:
                    raise ValueError("Archive root must not be a symlink")
                links[path] = member
        for path in paths:
            if any(parent in links for parent in path.parents):
                raise ValueError("Archive member traverses a symlink: " + str(path))
        for path, member in links.items():
            target = PurePosixPath(member.linkname)
            if not member.linkname or target.is_absolute():
                raise ValueError("Unsafe archive symlink: " + member.name)
            parts = list(path.parent.parts)
            for part in target.parts:
                if part == "..":
                    if len(parts) <= 1:
                        raise ValueError("Escaping archive symlink: " + member.name)
                    parts.pop()
                else:
                    parts.append(part)
                if not parts or parts[0] != prefix or PurePosixPath(*parts) in links:
                    raise ValueError("Escaping or chained archive symlink: " + member.name)
        destination.mkdir(parents=True)
        # Create links only after all regular data, so extraction never writes
        # through a symlink. The pinned archives need no hardlinks/link chains.
        source.extractall(destination, members=[member for member in members if not member.issym()])
        for path, member in links.items():
            link = destination / str(path)
            link.parent.mkdir(parents=True, exist_ok=True)
            link.symlink_to(member.linkname)
    return destination / prefix


def check_enabled(header):
    if not re.search(r"^\s*#\s*define\s+BRPC_WITH_FLATBUFFERS\s+1\s*$",
                     Path(header).read_text(), re.MULTILINE):
        raise ValueError("FlatBuffers is not enabled in " + str(header))


def signal_group(process, signum):
    try:
        os.killpg(process.pid, signum)
        return True
    except ProcessLookupError:
        return False


def stop_process(process, grace=5):
    # The group may outlive its leader; waiting for the leader alone is not
    # sufficient when a compiler/test child ignores SIGTERM.
    if signal_group(process, signal.SIGTERM):
        try:
            process.wait(timeout=grace)
        except subprocess.TimeoutExpired:
            pass
        signal_group(process, signal.SIGKILL)
    process.wait()


class Runner:
    def __init__(self, args):
        self.args = args
        self.source = args.source.resolve(strict=True)
        self.work = args.work.resolve()
        if self.work == self.source or self.source in self.work.parents:
            raise ValueError("--work must be outside the source checkout")
        self.work.mkdir(parents=True, exist_ok=False)
        self.evidence = self.work / "evidence"
        self.evidence.mkdir()
        self.env = dict(os.environ)
        for name in list(self.env):
            if name.startswith("GTEST_"):
                self.env.pop(name)
        self.env.update(GTEST_FILTER="*", GTEST_REPEAT="1")
        self.steps = []
        self.results = {}
        self.compiler = self.env.get("CC", "cc")
        self.cxx = self.env.get("CXX", "c++")
        self.prefixes = [path.resolve(strict=True) for path in args.dependency_prefix]

    def save(self, status):
        (self.evidence / "summary.json").write_text(json.dumps({
            "status": status, "system": self.args.build_system,
            "source": str(self.source), "steps": self.steps, "tests": self.results,
        }, indent=2) + "\n")

    def step(self, name, command, cwd=None, timeout=900, env=None):
        command = [str(part) for part in command]
        log = self.evidence / (name + ".log")
        directory = Path(cwd or self.source)
        (self.evidence / (name + ".command")).write_text(
            "cwd=" + str(directory) + "\n" + shlex.join(command) + "\n")
        print("START", name, flush=True)
        start = time.monotonic()
        code = 1
        with log.open("w") as output:
            process = subprocess.Popen(command, cwd=directory, env=env or self.env,
                                       stdout=output, stderr=subprocess.STDOUT,
                                       start_new_session=True)
            try:
                code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                stop_process(process)
                code = 124
                print("Step exceeded timeout", file=output)
            except BaseException:
                stop_process(process)
                raise
            finally:
                self.steps.append({"name": name, "exitcode": code,
                                   "seconds": round(time.monotonic() - start, 3)})
                (self.evidence / (name + ".exitcode")).write_text(str(code) + "\n")
                self.save("running")
        print("END", name, "exit=" + str(code), flush=True)
        if code != 0:
            raise RuntimeError(name + " failed; see " + str(log))
        return log

    def download(self, name, version, digest):
        archive = self.work / (name + ".tar.gz")
        self.step("download-" + name, ["curl", "--fail", "--location", "--retry", "2",
                  "--connect-timeout", "20", "--max-time", "180",
                  "https://github.com/google/" + name + "/archive/refs/tags/v" + version + ".tar.gz",
                  "--output", archive], timeout=600)
        return extract_archive(archive, digest, self.work / (name + "-source"), name + "-" + version)

    def prepare_dependencies(self):
        if self.args.flatbuffers_prefix:
            self.fb = self.args.flatbuffers_prefix.resolve(strict=True)
        else:
            source = self.download("flatbuffers", FLATBUFFERS_VERSION, FLATBUFFERS_SHA256)
            self.fb = self.work / "dependencies"
            build = self.work / "flatbuffers-build"
            self.step("flatbuffers-configure", ["cmake", "-S", source, "-B", build,
                "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
                "-DCMAKE_INSTALL_LIBDIR=lib", "-DCMAKE_INSTALL_PREFIX=" + str(self.fb),
                "-DFLATBUFFERS_BUILD_TESTS=OFF", "-DFLATBUFFERS_BUILD_FLATC=ON",
                "-DFLATBUFFERS_BUILD_FLATLIB=ON", "-DFLATBUFFERS_BUILD_SHAREDLIB=OFF",
                "-DFLATBUFFERS_INSTALL=ON", "-DFLATBUFFERS_LIBCXX_WITH_CLANG=OFF"])
            self.step("flatbuffers-build", ["cmake", "--build", build, "--parallel", self.args.jobs])
            self.step("flatbuffers-install", ["cmake", "--install", build])
        self.flatc = self.fb / "bin/flatc"
        version = self.step("flatc-version", [self.flatc, "--version"], timeout=10).read_text().strip()
        base = (self.fb / "include/flatbuffers/base.h").read_text()
        numbers = [re.search(r"#define\s+FLATBUFFERS_VERSION_" + part + r"\s+(\d+)", base)
                   for part in ("MAJOR", "MINOR", "REVISION")]
        if (version != "flatc version " + FLATBUFFERS_VERSION or
                not all(numbers) or ".".join(match.group(1) for match in numbers) != FLATBUFFERS_VERSION):
            raise ValueError("Use matching pinned FlatBuffers headers and compiler")
        self.fb_library = self.fb / "lib/libflatbuffers.a"
        if not self.fb_library.is_file():
            raise ValueError("The generator needs " + str(self.fb_library))
        self.gtest = (self.args.gtest_source.resolve(strict=True) if self.args.gtest_source else
                      self.download("googletest", GTEST_VERSION, GTEST_SHA256))
        self.prefixes.insert(0, self.fb)
        if self.args.build_system == "make":
            build = self.work / "gtest-build"
            gtest_prefix = self.work / "gtest-prefix"
            self.step("gtest-configure", ["cmake", "-S", self.gtest, "-B", build,
                "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_INSTALL_LIBDIR=lib",
                "-DBUILD_SHARED_LIBS=OFF", "-DCMAKE_INSTALL_PREFIX=" + str(gtest_prefix)])
            self.step("gtest-build", ["cmake", "--build", build, "--parallel", self.args.jobs])
            self.step("gtest-install", ["cmake", "--install", build])
            self.prefixes.insert(0, gtest_prefix)
        self.env["PATH"] = os.pathsep.join(str(prefix / "bin") for prefix in self.prefixes) + os.pathsep + self.env.get("PATH", "")
        self.step("protoc-version", [self.args.protoc or shutil.which("protoc", path=self.env["PATH"]), "--version"], timeout=10)

    def common_cmake(self):
        options = ["-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
                   "-DCMAKE_PREFIX_PATH=" + ";".join(str(prefix) for prefix in self.prefixes),
                   "-DCMAKE_C_COMPILER=" + self.compiler, "-DCMAKE_CXX_COMPILER=" + self.cxx]
        for prefix in self.prefixes:
            if (prefix / "include/openssl/ssl.h").is_file():
                options.append("-DOPENSSL_ROOT_DIR=" + str(prefix))
                break
        if self.args.protoc:
            options.append("-DProtobuf_PROTOC_EXECUTABLE=" + str(self.args.protoc.resolve(strict=True)))
        return options

    def check_gtest(self, name, binary, ctest_build=None):
        listing = self.step(name + "-list", [binary, "--gtest_list_tests"], timeout=30)
        xml = self.evidence / (name + ".xml")
        env = dict(self.env, GTEST_OUTPUT="xml:" + str(xml))
        command = (["ctest", "--test-dir", ctest_build, "--no-tests=error", "--output-on-failure",
                    "--timeout", "300", "-R", "^" + name + "$"] if ctest_build else [binary])
        self.step(name, command, timeout=330, env=env)
        self.results[name] = verify_gtests(xml, listing, TESTS[name])
        self.save("running")

    def ctest(self, name, build, required):
        xml = self.evidence / (name + ".xml")
        self.step(name, ["ctest", "--test-dir", build, "--no-tests=error", "--output-on-failure",
                        "--timeout", "300", "--output-junit", xml], timeout=600)
        self.results[name] = verify_ctest(xml, required)
        self.save("running")

    def run_cmake(self):
        build = self.work / "build"
        self.step("configure", ["cmake", "-S", self.source, "-B", build] + self.common_cmake() + [
            "-DWITH_FLATBUFFERS=ON", "-DBUILD_UNIT_TESTS=ON", "-DBUILD_BRPC_TOOLS=OFF",
            "-DBUILD_SHARED_LIBS=ON",
            "-DDOWNLOAD_GTEST=OFF", "-DBRPC_SYSTEM_GTEST_SOURCE_DIR=" + str(self.gtest),
            "-DFLATBUFFERS_INCLUDE_DIR=" + str(self.fb / "include"),
            "-DFLATBUFFERS_FLATC_EXECUTABLE=" + str(self.flatc)])
        check_enabled(build / "output/include/butil/config.h")
        self.step("build", ["cmake", "--build", build, "--target", *TESTS, "brpc-shared",
                            "--parallel", self.args.jobs])
        for name in TESTS:
            self.check_gtest(name, build / "test" / name, build)
        codegen = self.work / "codegen"
        shared_library = "libbrpc.dylib" if sys.platform == "darwin" else "libbrpc.so"
        self.step("codegen-configure", ["cmake", "-S", self.source / "tools/flatbuffers", "-B", codegen] + self.common_cmake() + [
            "-DBUILD_TESTING=ON", "-DBRPC_CODEGEN_BRPC_LIBRARY=" + str(build / "output/lib" / shared_library),
            "-DFLATBUFFERS_INCLUDE_DIR=" + str(self.fb / "include"),
            "-DFLATBUFFERS_LIBRARY=" + str(self.fb_library), "-DFLATC_EXECUTABLE=" + str(self.flatc)])
        self.step("codegen-build", ["cmake", "--build", codegen, "--parallel", self.args.jobs])
        self.ctest("codegen", codegen, {"flatbuffers_codegen_acceptance", "flatbuffers_codegen_runtime"})
        example = self.work / "example"
        self.step("example-configure", ["cmake", "-S", self.source / "example/benchmark_fb", "-B", example] + self.common_cmake() + [
            "-DBUILD_TESTING=ON", "-DBRPC_ROOT=" + str(build / "output"),
            "-DFLATBUFFERS_INCLUDE_DIR=" + str(self.fb / "include"),
            "-DFLATC_EXECUTABLE=" + str(self.flatc),
            "-DBRPC_FLATC_EXECUTABLE=" + str(codegen / "brpc_flatc")])
        self.step("example-build", ["cmake", "--build", example, "--parallel", self.args.jobs])
        self.ctest("example", example, {"benchmark_fb_smoke"})

    def copy_source(self):
        listing = self.step("source-files", ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"])
        destination = self.work / "source"
        destination.mkdir()
        count = 0
        for name in set(listing.read_bytes().split(b"\0")):
            if not name:
                continue
            relative = Path(os.fsdecode(name))
            path = self.source / relative
            if (relative.is_absolute() or ".." in relative.parts or path.is_symlink() or
                    self.source not in path.resolve().parents):
                raise ValueError("Unsafe checkout path: " + str(relative))
            if not path.exists():
                continue
            if not path.is_file():
                raise ValueError("Only regular source files are supported: " + str(relative))
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
            count += 1
        if not count:
            raise ValueError("The source must be a nonempty Git checkout")
        self.env["CCACHE_BASEDIR"] = str(destination)
        return destination

    def run_make(self):
        source = self.copy_source()
        headers = [str(prefix / "include") for prefix in self.prefixes] + ["/usr/include"]
        libs = [str(prefix / "lib") for prefix in self.prefixes] + ["/usr/lib", "/usr/lib64"]
        self.step("configure", ["sh", "config_brpc.sh", "--with-flatbuffers",
            "--headers=" + " ".join(headers), "--libs=" + " ".join(libs),
            "--cc=" + self.compiler, "--cxx=" + self.cxx], cwd=source)
        self.step("build", ["make", "-j" + str(self.args.jobs)], cwd=source)
        check_enabled(source / "output/include/butil/config.h")
        self.step("test-build", ["make", "-C", "test", "-j" + str(self.args.jobs),
                                "FLATC=" + str(self.flatc), *TESTS], cwd=source)
        variable = "DYLD_LIBRARY_PATH" if sys.platform == "darwin" else "LD_LIBRARY_PATH"
        self.env[variable] = os.pathsep.join([str(source / "test")] +
                                           [str(prefix / "lib") for prefix in self.prefixes])
        for name in TESTS:
            self.check_gtest(name, source / "test" / name)

    def run_bazel(self):
        source = self.copy_source()
        command = ["bazel", "--batch", "--output_user_root=" + str(self.work / "bazel-state"),
            "test", "--define=BRPC_WITH_FLATBUFFERS=true", "--jobs=" + str(self.args.jobs),
            "--local_test_jobs=1", "--test_timeout=300", "--cache_test_results=no",
            "--runs_per_test=1", "--flaky_test_attempts=1", "--test_output=errors",
            "--test_env=GTEST_FILTER=*", "--test_env=GTEST_REPEAT=1"]
        try:
            self.step("bazel-tests", command + ["//test:" + name for name in TESTS], cwd=source, timeout=1800)
        finally:
            for name in TESTS:
                for filename in ("test.xml", "test.log"):
                    path = source / "bazel-testlogs/test" / name / filename
                    if path.is_file():
                        shutil.copy2(path, self.evidence / (name + "." + filename))
        for name in TESTS:
            listing = self.step(name + "-list", [source / "bazel-bin/test" / name, "--gtest_list_tests"], cwd=source, timeout=30)
            self.results[name] = verify_gtests(self.evidence / (name + ".test.xml"), listing, TESTS[name])
            self.save("running")

    def run(self):
        self.step("compiler-version", [self.cxx, "--version"], timeout=10)
        self.step("cmake-version", ["cmake", "--version"], timeout=10)
        if self.args.build_system == "bazel":
            self.step("bazel-version", ["bazel", "--version"], timeout=30)
            self.run_bazel()
        else:
            self.prepare_dependencies()
            getattr(self, "run_" + self.args.build_system)()
        self.save("passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-system", choices=("cmake", "make", "bazel"), required=True)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--flatbuffers-prefix", type=Path)
    parser.add_argument("--gtest-source", type=Path)
    parser.add_argument("--dependency-prefix", type=Path, action="append", default=[])
    parser.add_argument("--protoc", type=Path)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 16:
        parser.error("--jobs must be in 1..16")
    runner = None
    try:
        runner = Runner(args)
        runner.run()
    except (Exception, KeyboardInterrupt) as error:
        if runner:
            runner.save("failed")
            (runner.evidence / "failure.txt").write_text(str(error) + "\n")
        print("FlatBuffers ON gate failed:", error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
    sys.exit(main())
