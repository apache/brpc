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
# Verify an Apache bRPC release candidate and print a summary that can be
# pasted straight into a vote reply.
#
# Usage:
#   verify_package.sh <dist-url|local-dir> [--keys <url>] [--no-diff]
#
# Examples:
#   verify_package.sh https://dist.apache.org/repos/dist/dev/brpc/1.18.0/
#   verify_package.sh ~/brpc_release/1.18.0
#
# A portable companion to community/apache-package-validator.sh (which needs
# wget and GNU coreutils); this one runs on macOS too and additionally diffs
# the tarball against the GitHub tag.

set -uo pipefail

SOURCE=""
KEYS_URL="https://downloads.apache.org/brpc/KEYS"
DO_DIFF=1

while (( $# )); do
    case $1 in
        --keys)    KEYS_URL=$2; shift 2 ;;
        --no-diff) DO_DIFF=0;   shift ;;
        -h|--help) sed -n '20,33p' "$0"; exit 0 ;;
        -*) echo "error: unknown option '$1'" >&2; exit 2 ;;
        *)  SOURCE=$1; shift ;;
    esac
done

if [[ -z ${SOURCE} ]]; then
    echo "usage: $(basename "$0") <dist-url|local-dir> [--keys <url>] [--no-diff]" >&2
    exit 2
fi

if   command -v sha512sum  >/dev/null 2>&1; then SHA512=(sha512sum)
elif command -v gsha512sum >/dev/null 2>&1; then SHA512=(gsha512sum)
elif command -v shasum     >/dev/null 2>&1; then SHA512=(shasum -a 512)
else echo "error: need one of sha512sum, gsha512sum, shasum" >&2; exit 1
fi

r_link=' '; r_sum=' '; r_sig=' '; r_ver=' '; r_lic=' '; r_bin=' '; r_tag=' '
failures=0

pass() { printf '  PASS  %s\n' "$*"; }
fail() { printf '  FAIL  %s\n' "$*" >&2; failures=$((failures + 1)); }
warn() { printf '  WARN  %s\n' "$*" >&2; }
step() { printf '\n==> %s\n' "$*"; }

WORK=$(mktemp -d)
trap 'summary; rm -rf "${WORK}"' EXIT

summary() {
    cat <<EOF

------------------------------------------------------------------------
Paste-ready summary for the vote thread:

I checked:
- [${r_link}] the links of the package are valid;
- [${r_sum}] the checksum of the package is valid;
- [${r_sig}] the signature of the package is valid;
- [${r_ver}] RELEASE_VERSION in the source code matches the current release;
- [${r_lic}] LICENSE and NOTICE are present;
- [${r_bin}] no compiled archives bundled in the source archive;
- [${r_tag}] the source distribution matches the git tag.

Still to check by hand: it builds, unit tests pass, and third-party
license declarations are complete (see references/checklist.md).
------------------------------------------------------------------------
EOF
    if (( failures )); then
        echo "${failures} check(s) FAILED" >&2
    fi
}

# ---------------------------------------------------------------- fetch

step "Collecting artifacts"
if [[ -d ${SOURCE} ]]; then
    found=$(ls "${SOURCE}"/apache-brpc-*-src.tar.gz 2>/dev/null | head -1)
    if [[ -z ${found} ]]; then
        fail "no apache-brpc-*-src.tar.gz in ${SOURCE}"
        exit 1
    fi
    TARBALL=$(basename "${found}")
    VERSION=${TARBALL#apache-brpc-}; VERSION=${VERSION%-src.tar.gz}
    for suffix in "" .asc .sha512; do
        if [[ -f ${SOURCE}/${TARBALL}${suffix} ]]; then
            cp "${SOURCE}/${TARBALL}${suffix}" "${WORK}/"
        else
            fail "missing ${TARBALL}${suffix}"
        fi
    done
    (( failures == 0 )) && r_link='x'
    echo "    local: ${SOURCE} (version ${VERSION})"
else
    base=${SOURCE%/}
    VERSION=${base##*/}
    TARBALL="apache-brpc-${VERSION}-src.tar.gz"
    ok=1
    echo "    from ${base}"
    for suffix in "" .asc .sha512; do
        # dist.apache.org has no CDN in front of it and routinely crawls along
        # at a few KB/s, so show a progress bar for the tarball rather than
        # leaving the RM staring at a silent terminal for ten minutes.
        if [[ -z ${suffix} ]]; then
            curl_opts=(--progress-bar)
            echo "    downloading ${TARBALL} (dist.apache.org can be very slow)"
        else
            curl_opts=(-sS)
        fi
        if curl -fL "${curl_opts[@]}" --connect-timeout 20 --max-time 1800 \
            -o "${WORK}/${TARBALL}${suffix}" "${base}/${TARBALL}${suffix}"; then
            echo "    got ${TARBALL}${suffix}"
        else
            fail "cannot download ${base}/${TARBALL}${suffix}"
            ok=0
        fi
    done
    (( ok )) && r_link='x'
fi

cd "${WORK}" || exit 1
[[ -f ${TARBALL} ]] || { fail "no tarball to verify"; exit 1; }

# ---------------------------------------------------------------- checksum

step "sha512"
if [[ -f ${TARBALL}.sha512 ]]; then
    if grep -q '/' "${TARBALL}.sha512"; then
        warn "${TARBALL}.sha512 records a path, not a bare filename; --check may fail for others"
    fi
    if "${SHA512[@]}" --check "${TARBALL}.sha512"; then
        r_sum='x'; pass "checksum matches"
    else
        fail "checksum mismatch"
    fi
else
    fail "no .sha512 file"
fi

# ---------------------------------------------------------------- signature

step "GPG signature"
if [[ -f ${TARBALL}.asc ]]; then
    if ! curl -fsSL "${KEYS_URL}" | gpg --import 2>/dev/null; then
        warn "could not import KEYS from ${KEYS_URL}"
    fi
    # A candidate still in dist/dev may only have its key in the dev KEYS file.
    if [[ ${KEYS_URL} == *"/release/"* || ${KEYS_URL} == *"downloads.apache.org"* ]]; then
        curl -fsSL "https://dist.apache.org/repos/dist/dev/brpc/KEYS" \
            | gpg --import 2>/dev/null || true
    fi
    if gpg --verify "${TARBALL}.asc" "${TARBALL}"; then
        r_sig='x'; pass "signature is valid"
        echo "    (a 'no ultimately trusted keys' warning is expected and fine --"
        echo "     it means the key is not in your web of trust, not that the signature is bad)"
    else
        fail "signature does not verify"
    fi
else
    fail "no .asc file"
fi

# ---------------------------------------------------------------- contents

step "Unpacking"
tar -xzf "${TARBALL}" || { fail "cannot unpack ${TARBALL}"; exit 1; }
SRC="apache-brpc-${VERSION}-src"
[[ -d ${SRC} ]] || { fail "tarball does not contain ${SRC}/"; exit 1; }

step "Version consistency"
in_release=$(tr -d '[:space:]' < "${SRC}/RELEASE_VERSION" 2>/dev/null)
if [[ ${in_release} == "${VERSION}" ]] \
   && grep -q "set(BRPC_VERSION ${VERSION})" "${SRC}/CMakeLists.txt" 2>/dev/null; then
    r_ver='x'; pass "RELEASE_VERSION and CMakeLists.txt both say ${VERSION}"
else
    fail "version mismatch (RELEASE_VERSION='${in_release}', expected '${VERSION}')"
fi

step "LICENSE and NOTICE"
if [[ -f ${SRC}/LICENSE && -f ${SRC}/NOTICE ]]; then
    r_lic='x'; pass "both present"
    year=$(sed -n 's/^Copyright [0-9]*-\([0-9]*\).*/\1/p' "${SRC}/NOTICE" | head -1)
    [[ ${year} == "$(date +%Y)" ]] || warn "NOTICE copyright ends at ${year}, current year is $(date +%Y)"
else
    fail "LICENSE and/or NOTICE missing"
fi

step "Unexpected binaries"
# `certificate` covers test cert/key fixtures whose `file` output varies by
# platform (PEM on macOS, sometimes a bare "certificate"/DER blob elsewhere).
nontext=$(find "${SRC}" -type f -print0 \
    | xargs -0 file \
    | grep -v 'GIF\|JPEG\|PNG\|SVG\|PowerPoint\|Git\|JSON\|PEM\|certificate\|empty\|text\|XML' || true)

# Fuzzing seed corpora are opaque by design and are allowlisted in
# .licenserc.yaml; flagging them sends the RM chasing a phantom -1.
expected=$(printf '%s\n' "${nontext}" | grep 'test/fuzzing/fuzz_[^/]*_seed_corpus/' || true)
suspicious=$(printf '%s\n' "${nontext}" | grep -v 'test/fuzzing/fuzz_[^/]*_seed_corpus/' | grep -v '^$' || true)

if [[ -n ${expected} ]]; then
    note_count=$(printf '%s\n' "${expected}" | grep -c . || true)
    echo "    ${note_count} fuzzing seed corpus file(s) are binary by design (allowlisted in .licenserc.yaml)"
fi
if [[ -z ${suspicious} ]]; then
    r_bin='x'; pass "no compiled archives or stray binaries"
else
    fail "suspicious files:"
    printf '%s\n' "${suspicious}" | sed 's/^/        /' >&2
fi

# ---------------------------------------------------------------- vs tag

if (( DO_DIFF )); then
    step "Diff against the GitHub tag"
    if curl -fsSL -o "tag-${VERSION}.tar.gz" \
        "https://github.com/apache/brpc/archive/refs/tags/${VERSION}.tar.gz"; then
        tar -xzf "tag-${VERSION}.tar.gz"
        if diff -r "brpc-${VERSION}" "${SRC}" > tag.diff 2>&1; then
            r_tag='x'; pass "identical to the tag"
        else
            fail "differs from the tag:"
            head -40 tag.diff | sed 's/^/        /' >&2
        fi
    else
        warn "tag ${VERSION} not published on GitHub yet; skipping"
    fi
else
    warn "tag diff skipped (--no-diff)"
fi

exit $(( failures > 0 ))
