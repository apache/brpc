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

set -e

g_package_link=${1}
g_package_keys=${2:-https://downloads.apache.org/incubator/brpc/KEYS}

g_valid_package_link=' '
g_valid_package_name=' '
g_valid_package_checksum=' '
g_valid_package_sig=' '
g_valid_package_content=' '
g_valid_package_license=' '
g_valid_package_binary=' '

summary() {
    cat <<EOF

- [${g_valid_package_link}] the links of the package are valid;
- [${g_valid_package_name}] 'incubating' in the name;
- [${g_valid_package_checksum}] the checksum of the package is valid;
- [${g_valid_package_sig}] the signature of the package is valid;
- [${g_valid_package_content}] RELEASE_VERSION in the source code matches the current release;
- [${g_valid_package_license}] DISCLAIMER, LICENSE and NOTICE are not absent, note that we use CI based on Skywalking-eyes to check the license;
- [${g_valid_package_binary}] no compiled archives bundled in the source archive.
EOF
}


on_exit() {
    summary
}

validate_package() {
    local ver=$(echo ${g_package_link%/} | rev | cut -d'/' -f1 | rev)
    local package_name="apache-brpc-${ver}-incubating-src.tar.gz"

    for suffix in "" ".asc" ".sha512"; do
        wget --quiet -c "${g_package_link%/}/${package_name}${suffix}"
    done

    g_valid_package_link='x'
    g_valid_package_name='x'

    sha512sum --status -c ${package_name}.sha512 \
        && g_valid_package_checksum='x'

    # Import keys published by the author,
    # and verify the package is signed by the author.
    # No need to trust the public keys.
    wget --quiet ${g_package_keys} -O - | gpg --import \
        && gpg --verify ${package_name}.asc \
        && g_valid_package_sig='x'

    tar -xf ${package_name}

    (
        pushd ${package_name%.tar.gz} > /dev/null 2>&1
        [[ -f RELEASE_VERSION ]] \
            && [[ $(cat RELEASE_VERSION) = "${ver}" ]] \
            && grep BRPC_VERSION CMakeLists.txt | grep -q ${ver}
    ) && g_valid_package_content='x'

    (
        pushd ${package_name%.tar.gz} > /dev/null 2>&1
        [[ -f DISCLAIMER && -f LICENSE && -f NOTICE ]]
    ) && g_valid_package_license='x'

    local has_unexpected_binary=
    for i in $(find ${package_name%.tar.gz} -type f); do
        file ${i} | grep -v 'GIF\|JPEG\|PNG\|SVG\|PowerPoint\|Git\|JSON\|PEM\|empty\|text' && has_unexpected_binary=1
    done
    [[ -z "${has_unexpected_binary}" ]] && g_valid_package_binary='x'
}

trap on_exit EXIT

validate_package
