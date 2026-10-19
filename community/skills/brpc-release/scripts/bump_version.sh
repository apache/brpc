#!/usr/bin/env bash
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements. See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership. The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License. You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied. See the License for the
# specific language governing permissions and limitations
# under the License.
#
# Bump every hard-coded bRPC version string, then verify none was missed.
#
# Usage:
#   bump_version.sh <version>            # rewrite all version files
#   bump_version.sh <version> --check    # verify only, never write
#
# Idempotent: safe to re-run. Exits non-zero if any file ends up inconsistent.

set -euo pipefail

NEW_VERSION=${1:-}
MODE=${2:-write}

if [[ -z ${NEW_VERSION} || ${NEW_VERSION} == -* ]]; then
    echo "usage: $(basename "$0") <version> [--check]" >&2
    exit 2
fi

if ! [[ ${NEW_VERSION} =~ ^[0-9]+\.[0-9]+\.[0-9]+(-rc[0-9]+)?$ ]]; then
    echo "error: '${NEW_VERSION}' is not a valid version (expect 1.18.0 or 1.18.0-rc01)" >&2
    exit 2
fi

case ${MODE} in
    write|--check) ;;
    *) echo "error: unknown option '${MODE}'" >&2; exit 2 ;;
esac

ROOT=$(git rev-parse --show-toplevel)
cd "${ROOT}"

if [[ ! -f RELEASE_VERSION || ! -f CMakeLists.txt || ! -d src/brpc ]]; then
    echo "error: '${ROOT}' does not look like the Apache bRPC repository" >&2
    exit 1
fi

if ! git remote -v | grep -Eq '(^|[/:])([^/]+/)?brpc(\.git)?([[:space:]]|$)'; then
    echo "error: no recognizable bRPC git remote is configured" >&2
    exit 1
fi

# Never rewrite version files before switching to the release branch.  This is
# deliberately enforced here rather than relying only on the release guide.
if [[ ${MODE} == write ]]; then
    EXPECTED_BRANCH="release-${NEW_VERSION%.*}"
    CURRENT_BRANCH=$(git branch --show-current)
    if [[ ${CURRENT_BRANCH} != "${EXPECTED_BRANCH}" ]]; then
        echo "error: version files may only be rewritten on '${EXPECTED_BRANCH}' (current: '${CURRENT_BRANCH:-detached HEAD}')" >&2
        echo "create or switch to the release branch first" >&2
        exit 1
    fi

    # Refuse to mix a version bump with unrelated edits. Untracked files are
    # allowed because release tooling itself may not have been committed yet;
    # tracked modifications are never overwritten or stashed automatically.
    TRACKED_CHANGES=$(git status --short --untracked-files=no)
    if [[ -n ${TRACKED_CHANGES} ]]; then
        echo "error: tracked files have uncommitted changes; commit or restore them before bumping the version:" >&2
        printf '%s\n' "${TRACKED_CHANGES}" >&2
        exit 1
    fi
fi

OLD_VERSION=$(tr -d '[:space:]' < RELEASE_VERSION)
# Escape dots so the string is safe inside a regex.
OLD_RE=${OLD_VERSION//./\\.}
NEW_RE=${NEW_VERSION//./\\.}

failed=0
changed=0

note()  { printf '  %s\n' "$*"; }
fail()  { printf '  FAIL  %s\n' "$*" >&2; failed=1; }

# Rewrite one file in place, portably (GNU and BSD sed disagree about -i).
rewrite() {
    local file=$1 expr=$2 tmp
    [[ -f ${file} ]] || { fail "${file}: not found"; return; }
    tmp=$(mktemp)
    sed -E "${expr}" "${file}" > "${tmp}"
    if cmp -s "${file}" "${tmp}"; then
        rm -f "${tmp}"
    else
        cat "${tmp}" > "${file}"   # preserve the original file mode
        rm -f "${tmp}"
        changed=$((changed + 1))
    fi
}

# Assert the file now carries the new version.
expect() {
    local file=$1 pattern=$2
    if grep -Eq -- "${pattern}" "${file}"; then
        note "ok    ${file}"
    else
        fail "${file}: no line matching /${pattern}/"
    fi
}

if [[ ${MODE} == write ]]; then
    echo "Bumping ${OLD_VERSION} -> ${NEW_VERSION}"

    printf '%s\n' "${NEW_VERSION}" > RELEASE_VERSION

    rewrite CMakeLists.txt \
        "s/^set\(BRPC_VERSION[[:space:]]+[^)]*\)/set(BRPC_VERSION ${NEW_VERSION})/"

    rewrite package/rpm/brpc.spec \
        "s/^(Version:[[:space:]]*).*/\1${NEW_VERSION}/"

    rewrite CLAUDE.md \
        "s/(Current version:[[:space:]]*)[^.[:space:]]+(\.[^.[:space:]]+){2}/\1${NEW_VERSION}/"

    # Only the root module() block is the released module version. The example
    # module deliberately keeps its own version and dependency declaration.
    rewrite MODULE.bazel \
        "s/^([[:space:]]*version = ')[^']*(')/\1${NEW_VERSION}\2/"

    for doc in docs/cn/bazel_support.md docs/en/bazel_support.md; do
        rewrite "${doc}" \
            "s/(bazel_dep\(name = \"brpc\", version = \")[^\"]*/\1${NEW_VERSION}/"
    done

    echo "Rewrote ${changed} file(s)"
else
    echo "Checking version files against ${NEW_VERSION}"
fi

echo "Verifying:"
expect RELEASE_VERSION                            "^${NEW_RE}$"
expect CMakeLists.txt                             "^set\(BRPC_VERSION ${NEW_RE}\)"
expect package/rpm/brpc.spec                      "^Version:[[:space:]]*${NEW_RE}$"
expect CLAUDE.md                                  "Current version:[[:space:]]*${NEW_RE}\."
expect MODULE.bazel                               "^[[:space:]]*version = '${NEW_RE}',"
expect docs/cn/bazel_support.md                   "bazel_dep\(name = \"brpc\", version = \"${NEW_RE}\""
expect docs/en/bazel_support.md                   "bazel_dep\(name = \"brpc\", version = \"${NEW_RE}\""

# Catch anything the table above does not know about.
if [[ ${OLD_VERSION} != "${NEW_VERSION}" ]]; then
    echo "Scanning for leftover '${OLD_VERSION}':"
    leftover=$(git grep -n -E -- "${OLD_RE}" -- \
        ':!community/release_schedule.md' ':!community/skills' ':!registry' \
        ':!example/build_with_bazel_module/MODULE.bazel' || true)
    if [[ -n ${leftover} ]]; then
        printf '%s\n' "${leftover}" | sed 's/^/  /'
        echo "  ^ review these by hand; release_schedule.md and past release notes are expected to keep old versions" >&2
    else
        note "none"
    fi
fi

# NOTICE year is easy to forget in January releases.
notice_year=$(sed -n 's/^Copyright [0-9]*-\([0-9]*\).*/\1/p' NOTICE | head -1)
this_year=$(date +%Y)
if [[ ${notice_year} != "${this_year}" ]]; then
    echo "warning: NOTICE says 'Copyright ...-${notice_year}' but it is ${this_year}; update it if this release ships this year" >&2
fi

if (( failed )); then
    echo "FAILED: version files are inconsistent" >&2
    exit 1
fi

echo "All version files are at ${NEW_VERSION}"
