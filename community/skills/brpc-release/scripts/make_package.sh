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
# Build and sign an Apache bRPC source release in one RM-operated command:
# local tag -> tarball -> GPG signature -> sha512 -> self-check.
#
# Usage:
#   BRPCUSERNAME=<apache-id> make_package.sh <version> [options]
#
# Options:
#   --user <apache-id>   signing identity (default: $BRPCUSERNAME)
#   --repo <dir>         release-branch checkout (default: current git root)
#   --outdir <dir>       artifact directory (default: ~/brpc_release/<version>)
#
# The AI model must not execute this script because it invokes interactive GPG
# signing. The RM runs it in a terminal and enters the private-key passphrase.
# This script NEVER pushes a tag, commits SVN changes, or changes Git
# user.name/user.email.

set -euo pipefail

VERSION=""
APACHE_ID=${BRPCUSERNAME:-}
REPO=""
OUTDIR=""

while (( $# )); do
    case $1 in
        --user)    APACHE_ID=$2; shift 2 ;;
        --repo)    REPO=$2;      shift 2 ;;
        --outdir)  OUTDIR=$2;    shift 2 ;;
        -h|--help) sed -n '20,35p' "$0"; exit 0 ;;
        -*) echo "error: unknown option '$1'" >&2; exit 2 ;;
        *)  VERSION=$1; shift ;;
    esac
done

if [[ -z ${VERSION} ]]; then
    echo "usage: BRPCUSERNAME=<apache-id> $(basename "$0") <version> [--repo dir] [--outdir dir]" >&2
    exit 2
fi
if ! [[ ${VERSION} =~ ^[0-9]+\.[0-9]+\.[0-9]+(-rc[0-9]+)?$ ]]; then
    echo "error: '${VERSION}' is not a valid version" >&2
    exit 2
fi
if [[ -z ${APACHE_ID} ]]; then
    echo "error: no Apache ID; set BRPCUSERNAME or pass --user <apache-id>" >&2
    exit 2
fi
if ! [[ ${APACHE_ID} =~ ^[A-Za-z0-9._-]+$ ]]; then
    echo "error: invalid Apache ID '${APACHE_ID}'" >&2
    exit 2
fi

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=${REPO:-$(git rev-parse --show-toplevel)}
OUTDIR=${OUTDIR:-${HOME}/brpc_release/${VERSION}}
PREFIX="apache-brpc-${VERSION}-src"
TARBALL="${PREFIX}.tar.gz"

# GNU coreutils is not a given on macOS.
if   command -v sha512sum  >/dev/null 2>&1; then SHA512=(sha512sum)
elif command -v gsha512sum >/dev/null 2>&1; then SHA512=(gsha512sum)
elif command -v shasum     >/dev/null 2>&1; then SHA512=(shasum -a 512)
else echo "error: need one of sha512sum, gsha512sum, shasum" >&2; exit 1
fi

step() { printf '\n==> %s\n' "$*"; }

cd "${REPO}"

step "Checking the working tree"
echo "    repo   : ${REPO}"
echo "    branch : $(git rev-parse --abbrev-ref HEAD)"
echo "    head   : $(git rev-parse --short HEAD)"
echo "    outdir : ${OUTDIR}"

if [[ -n $(git status --porcelain) ]]; then
    echo "error: working tree is dirty; commit or restore changes first" >&2
    git status --short >&2
    exit 1
fi

branch=$(git rev-parse --abbrev-ref HEAD)
expected_branch="release-${VERSION%.*}"
if [[ ${branch} != "${expected_branch}" ]]; then
    echo "error: on '${branch}', expected '${expected_branch}'" >&2
    exit 1
fi

# Do not change Git identity. An existing identity is needed only because the
# local release tag is annotated; the RM owns repository configuration.
if ! git config user.email >/dev/null || ! git config user.name >/dev/null; then
    echo "error: git identity is not configured; the RM must resolve this if desired" >&2
    echo "       this script will not modify git user.name or user.email" >&2
    exit 1
fi

step "Checking version files"
"${SCRIPT_DIR}/bump_version.sh" "${VERSION}" --check

step "Tagging ${VERSION} (local only)"
if git rev-parse -q --verify "refs/tags/${VERSION}" >/dev/null; then
    tagged=$(git rev-parse "${VERSION}^{commit}")
    head=$(git rev-parse HEAD)
    if [[ ${tagged} != "${head}" ]]; then
        echo "error: tag ${VERSION} already exists at ${tagged:0:12} but HEAD is ${head:0:12}" >&2
        echo "       if the tag was never pushed, the RM may drop it with: git tag -d ${VERSION}" >&2
        exit 1
    fi
    echo "    tag already exists at HEAD, reusing"
else
    git tag -a "${VERSION}" -m "release ${VERSION}"
    echo "    created"
fi
COMMIT=$(git rev-parse "${VERSION}^{commit}")

step "Building ${TARBALL}"
mkdir -p "${OUTDIR}"
git archive --format=tar.gz "${VERSION}" \
    --prefix="${PREFIX}/" --output="${OUTDIR}/${TARBALL}"
echo "    $(cd "${OUTDIR}" && du -h "${TARBALL}" | cut -f1)"

cd "${OUTDIR}"

step "Signing as ${APACHE_ID}@apache.org"
echo "    GPG will ask the RM for the private-key passphrase"
rm -f "${TARBALL}.asc"
gpg -u "${APACHE_ID}@apache.org" --armor \
    --output "${TARBALL}.asc" --detach-sign "${TARBALL}"

step "Verifying GPG signature"
gpg --verify "${TARBALL}.asc" "${TARBALL}"

# Run from the tarball directory so the checksum records a bare filename.
step "Generating ${TARBALL}.sha512"
"${SHA512[@]}" "${TARBALL}" > "${TARBALL}.sha512"

step "Checking SHA512"
"${SHA512[@]}" --check "${TARBALL}.sha512"

cat <<EOF

==> Artifacts in ${OUTDIR}
$(ls -1 "${PREFIX}"* | sed 's/^/    /')

    Release Commit ID: ${COMMIT}
      (this is what goes in the [VOTE] mail, not the branch HEAD)

==> Next, run these yourself -- this script will not:

    cd "${REPO}" && git push origin --tags

    mkdir -p ~/brpc_release/svn/dev/brpc/${VERSION}
    cp "${OUTDIR}/${TARBALL}"{,.asc,.sha512} ~/brpc_release/svn/dev/brpc/${VERSION}/
    cd ~/brpc_release/svn/dev/brpc && svn add --force . \
      && svn --username="${APACHE_ID}" commit -m "release ${VERSION}"

    Then verify the uploaded candidate:
    "${SCRIPT_DIR}/verify_package.sh" https://dist.apache.org/repos/dist/dev/brpc/${VERSION}/
EOF
