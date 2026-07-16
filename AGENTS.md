<!--
SPDX-License-Identifier: Apache-2.0

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# Repository Guidelines

## Project Structure & Module Organization

Core C++ code lives in `src/`: `src/brpc`, `src/bthread`,
`src/butil`, `src/bvar`, `src/json2pb`, and `src/mcpack2pb`. Tests are in
`test/` and mirror module names. Samples are in `example/`; utilities
are in `tools/`. Documentation is under `docs/en` and `docs/cn`; packaging and
bindings live in `package/`, `homebrew-formula/`, `python/`, and `java/`.

## Build, Test, and Development Commands

- `sh config_brpc.sh --headers=/usr/include --libs=/usr/lib && make`: configure and build with Make.
- `cmake -B build && cmake --build build -j6`: configure and build with CMake.
- `cmake -B build -DBUILD_UNIT_TESTS=ON && cmake --build build -j6 && cd build && ctest`: run CMake tests.
- `cd test && make && sh run_tests.sh`: run the Make-based test suite.
- `bazel build //:brpc` and `bazel test //test/...`: build or test with Bazel.
- `cd example/echo_c++ && make && ./echo_server & ./echo_client`: smoke-test an example.

## Coding Style & Naming Conventions

Follow Google C++ style with 4-space indentation. Keep feature-specific code in
the relevant protocol or module, not broad files such as `server.cpp` or
`channel.cpp`, unless the behavior is general. Use existing names:
`*_unittest.cpp` or `*_unittest.cc`, module prefixes such as `brpc_`,
`bthread_`, and `bvar_`, and `.proto` files beside related code.

## Testing Guidelines

New behavior should include unit tests. Run the smallest relevant test first,
then the broader affected suite. Tests use Google Test and live in `test/`.
Some integration tests require Redis or MySQL and may skip when absent.

## Commit & Pull Request Guidelines

Recent history uses short imperative subjects such as `Fix bazel compile error
on macOS`. Keep commits focused, explain behavioral impact when needed, and
link issues. Pull requests should describe the change, list tests run, note
platform or dependency assumptions, and pass GitHub Actions.

## Security & Configuration Tips

Read `SECURITY.md` before reporting vulnerabilities. Avoid committing
build directories, local paths, credentials, or machine-specific configuration.
Prefer flags such as `--with-glog`, `--with-thrift`, `--with-asan`,
or the matching CMake/Bazel options.
