#!/usr/bin/python3
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
