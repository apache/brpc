# **Improvement 2: Release Process Requires Manual Commands**

## **Docker File Commands**

The Dockerfile sets up the environment necessary to run the experiment.

```
cd /path/to/release/script
docker build . -t release_script
docker run -d release_script
docker container exec -it <container id> bash
## run testing commands shown below
docker stop <container id>
```

## **Automated Testing via Release Script**

```
hyperfine --runs 10 --export-json results.json './brpc_keygen.py 1.0.0'
```

The above script runs the command 10 times and returns the average run time in seconds, and generates a `results.json` with the runtime of each instance.

## **Manual Testing via GPG Commands**

Please run the following steps 10 times and compute the average in seconds.

```
time sh
gpg --full-gen-key
gpg --keyserver hkps://pgp.mit.edu --send-key <key id>
gpg --fingerprint test_name
gpg -u  test@test.com  --armor --output apache-brpc-1.0.0-src.tar.gz.asc --detach-sign apache-brpc-1.0.0-src.tar.gz
gpg --verify apache-brpc-1.0.0-src.tar.gz.asc apache-brpc-1.0.0-src.tar.gz
exit
```

For gpg -–full-gen-key, the following selections are made in sequence:

```
gpg (GnuPG) 2.3.1; Copyright (C) 2021 Free Software Foundation, Inc.
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.

Please select what kind of key you want:
   (1) RSA and RSA
   (2) DSA and Elgamal
   (3) DSA (sign only)
   (4) RSA (sign only)
   (9) ECC (sign and encrypt) *default*
  (10) ECC (sign only)
  (14) Existing key from card
Your selection? 1
RSA keys may be between 1024 and 4096 bits long.
What keysize do you want? (3072) 4096
Requested keysize is 4096 bits
Please specify how long the key should be valid.
         0 = key does not expire
      <n>  = key expires in n days
      <n>w = key expires in n weeks
      <n>m = key expires in n months
      <n>y = key expires in n years
Key is valid for? (0) 0
Key does not expire at all
Is this correct? (y/N) y

GnuPG needs to construct a user ID to identify your key.

Real name:test
Email address: test@test.com
Comment: test key
You selected this USER-ID:
    "test (test key) <test@test.com>"

Change (N)ame, (C)omment, (E)mail or (O)kay/(Q)uit? O
You need a Passphrase to protect your secret key. # Input password test password
```

### **Warning**

The experiment invovles uploading keys to a remote server. This is subjected to network conditions and your results may vary.

Occassionally, the operation to upload keys may hang. If this occurs, please stop the operation via ctrl+c and try again.

### **Note**

This script reads from `key_config.json` to populate values.
