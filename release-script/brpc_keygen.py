#!/usr/bin/python3

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

import subprocess
import gnupg
import json
import sys
from pprint import pprint

'''
Requires the installation of gpg binary and python-gnupg python module
'''

if len(sys.argv) != 2:
    print("ERROR: MISSING RELEASE NUMBER")
    exit(1)

release_number = sys.argv[1]
print(f"----- GENERATING RELEASE ARTIFACT NUMBER: {release_number} -----")


print("READING VALUES FROM CONFIG")
with open("key_config.json", "r") as file:
    config = json.loads(file.read())

key_type = config.get("key_type")
key_length = config.get("key_length")
subkey_type = config.get("subkey_type")
name_email = config.get("name_email")
name, email = name_email.split()
passphrase = config.get("passphrase")
input_file = config.get("input_file")
output_file = config.get("output_file")


print("GENERATING GPG KEY")
gpg = gnupg.GPG('/usr/bin/gpg')
input_data = gpg.gen_key_input(key_type=key_type,
                               subkey_type=subkey_type,
                               key_length=key_length,
                               name_email=name_email,
                               passphrase=passphrase
                               )

key = gpg.gen_key(input_data)

print("SENDING GPG KEY TO KEYSERVER")
result = gpg.send_keys("hkps://pgp.mit.edu", key.fingerprint)
if (result.returncode != 0):
    print("ERROR: FAIL TO SEND KEY")
    exit(1)
else:
    print("SEND KEY SUCCESSFUL")


print("SIGNING ARTIFACT")
detached_sig = gpg.sign_file(
    input_file,
    keyid=key.fingerprint,
    detach=True,
    output=output_file,
    passphrase=passphrase,
    clearsign=False
)

with open(output_file, 'rb') as f:
    verified = gpg.verify_file(f, input_file)

if verified:
    print(f"SIGNATURE VERIFIED: {verified.username}")
else:
    print("ERROR: SIGNATURE VERIFICATION FAILED")

print(f"\n---- KEY FINGERPRINT---- \n{key.fingerprint}")
