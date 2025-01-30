# brpc apache release guide step by step

## Preparation

### 1. Confirm the release notes

It will cost about 1 week or so to finish this phase, during this phase:
* The content of the `Release Notes` will be confirmed,
  - no missing changes;
  - and the description is clear;
* The PRs those are scheduled for the release can be merged.

The `Release Notes` template:
```
[Release Notes]

Feature:
- ...

Bugfix:
- ...

Enhancement:
- ...

Other:
- ...
```

### 2. Setup GPG(Please skip this step if you are not a first time distributor)

#### 1. Install GPG

The popular Linux distributions should have the GnuPG pre-installed. With OSX, you can install GnuPG using [`brew`](https://brew.sh/):
```bash
brew install gnupg
```

You can also download the installation package from [the GnuPG official website](https://www.gnupg.org/download/index.html). The commands of GnuPG version 1.x and version 2.x are slightly different. The following instructions take the `GnuPG-2.3.1` version (OSX) as an example.

After the installation is complete, execute the following command to check the version number.
```bash
gpg --version
```

#### 2. Create key

After the installation is complete, execute the following command to create a key.

```bash
gpg --full-gen-key
```

Complete the key creation according to the prompts. Note that the mailbox should use the Apache email address, and the `Real Name` can use Apache ID or GitHub ID:

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

Real name: LorinLee
Email address: lorinlee@apache.org
Comment: lorinlee's key
You selected this USER-ID:
    "LorinLee (lorinlee's key) <lorinlee@apache.org>"

Change (N)ame, (C)omment, (E)mail or (O)kay/(Q)uit? O
You need a Passphrase to protect your secret key. # Input password

We need to generate a lot of random bytes. It is a good idea to perform
some other action (type on the keyboard, move the mouse, utilize the
disks) during the prime generation; this gives the random number
generator a better chance to gain enough entropy.
gpg: key 92E18A11B6585834 marked as ultimately trusted
gpg: revocation certificate stored as '/Users/lilei/.gnupg/openpgp-revocs.d/C30F211F071894258497F46392E18A11B6585834.rev'
public and secret key created and signed.

pub   rsa4096 2021-10-17 [SC]
      C30F211F071894258497F46392E18A11B6585834
uid                      LorinLee (lorinlee's key) <lorinlee@apache.org>
sub   rsa4096 2021-10-17 [E]
```

#### 3. Check the generated key

```bash
gpg --list-keys
```

output:

```
gpg: checking the trustdb
gpg: marginals needed: 3  completes needed: 1  trust model: pgp
gpg: depth: 0  valid:   2  signed:   0  trust: 0-, 0q, 0n, 0m, 0f, 2u
/Users/lilei/.gnupg/pubring.kbx
----------------------------------
pub   rsa4096 2021-10-17 [SC]
      C30F211F071894258497F46392E18A11B6585834
uid           [ultimate] LorinLee (lorinlee's key) <lorinlee@apache.org>
sub   rsa4096 2021-10-17 [E]
```

Note that `C30F211F071894258497F46392E18A11B6585834` is the public key.

#### 4. Publish the public key to server

Execute the following command:

```bash
gpg --keyserver hkps://pgp.mit.edu --send-key C30F211F071894258497F46392E18A11B6585834
```

The keyserver can also use hkps://keys.openpgp.org or hkps://keyserver.ubuntu.com，which provide web page to view the keys.

#### 5. Generate fingerprint and upload to Apache user profile

Since the public key server has no verifying mechanism, anyone can upload the public key in your name, so there is no way to guarantee the reliability of the public key on the server. Usually, you can publish a public key fingerprint on your website and let others check the downloaded public key.

Execute the following command to view the fingerprint.
```
gpg --fingerprint lorinlee # user id
```

output:
```
/Users/lilei/.gnupg/pubring.kbx
----------------------------------
pub   rsa4096 2021-10-17 [SC]
      C30F 211F 0718 9425 8497  F463 92E1 8A11 B658 5834
uid           [ultimate] LorinLee (lorinlee's key) <lorinlee@apache.org>
sub   rsa4096 2021-10-17 [E]
```

Paste the above fingerprint `C30F 211F 0718 9425 8497 F463 92E1 8A11 B658 5834` into the `OpenPGP Public Key Primary Fingerprint:` field of your Apache user information on https://id.apache.org.

## Make the release

### 1. Create release branch

If you are releasing a new MAJOR/MINOR version, like `1.0.0`, you need to create a new branch `release-1.0` from master.

If you are releasing a new PATCH version from existing MINOR version, like `1.0.1`, you only need to modify the existing `release-1.0` branch and add the content to be released.

The code modification during the release process are performed on the release branch (such as `release-1.0`). After the release is complete, please merge the release branch back into the master branch.

### 2. Update the `NOTICE` file

Check and update the YEAR field of the `NOTIEC` file.

### 3. Update version in source code

#### Update `RELEASE_VERSION` file

Edit the `RELEASE_VERSION` file in the project root directory, update the version number, and submit it to the code repository. For example, the `1.0.0` version of the file is:

```
1.0.0
```

#### Update the `CMakeLists.txt` file

Edit the `CMakeLists.txt` file in the project root directory, update the version number, and submit it to the code repository. For example:

```
set(BRPC_VERSION 1.0.0)
```

#### Update the `package/rpm/brpc.spec` file

Edit the `/package/rpm/brpc.spec` file in the project root directory, update the version number, and submit it to the code repository. For example:

```
Version:	1.0.0
```

#### Update the `MODULE.bazel` file

Edit the `MODULE.bazel` file in the project root directory, update the version number, and submit it to the code repository. For example:

```
# in MODULE.bazel
module(
  ...
  version = '1.0.0',
  ...
)
```

### 4. Create releasing tag

Pull the release branch to tag, for example:

```bash
git clone -b release-1.0 git@github.com:apache/brpc.git ~/brpc

cd ~/brpc

git tag -a 1.0.0 -m "release 1.0.0"

git push origin --tags
```

### 5. Create releasing package

```bash
git archive --format=tar 1.0.0 --prefix=apache-brpc-1.0.0-src/ | gzip > apache-brpc-1.0.0-src.tar.gz
```
or
```bash
git archive --format=tar.gz 1.0.0 --prefix=apache-brpc-1.0.0-src/ --output=apache-brpc-1.0.0-src.tar.gz
```

### 6. Generate GPG signature

```bash
gpg -u lorinlee@apache.org --armor --output apache-brpc-1.0.0-src.tar.gz.asc --detach-sign apache-brpc-1.0.0-src.tar.gz

gpg --verify apache-brpc-1.0.0-src.tar.gz.asc apache-brpc-1.0.0-src.tar.gz
```

### 7. Generate SHA512 checksum

```bash
sha512sum apache-brpc-1.0.0-src.tar.gz > apache-brpc-1.0.0-src.tar.gz.sha512

sha512sum --check apache-brpc-1.0.0-src.tar.gz.sha512
```

## Publish to Apache SVN repository

### 1. Checkout dist/dev/brpc directory

If there is no local working directory, create a local working directory first. Checkout the Apache SVN repository, username needs to use your own Apache LDAP username:

```bash
mkdir -p ~/brpc_svn/dev/

cd ~/brpc_svn/dev/

svn --username=lorinlee co https://dist.apache.org/repos/dist/dev/brpc/

cd ~/brpc_svn/dev/brpc
```

### 2. Add GPG public key

A new release manager must add the key into KEYS file for the first time.

```
(gpg --list-sigs lorinlee && gpg -a --export lorinlee) >> KEYS
```

Note: if you have more than one keys that use the name, you should explicitly specify the key by email or fingerprint to export.

By email
```bash
(gpg --list-sigs lorinlee@apache.org && gpg -a --export lorinlee@apache.org) >> KEYS
```
By fingerprint:
```bash
(gpg --list-sigs C30F211F071894258497F46392E18A11B6585834 && gpg -a --export C30F211F071894258497F46392E18A11B6585834) >> KEYS
```

### 3. Add the releasing package to SVN directory

```bash
mkdir -p ~/brpc_svn/dev/brpc/1.0.0

cd ~/brpc_svn/dev/brpc/1.0.0

cp ~/brpc/apache-brpc-1.0.0-src.tar.gz ~/brpc_svn/dev/brpc/1.0.0

cp ~/brpc/apache-brpc-1.0.0-src.tar.gz.asc ~/brpc_svn/dev/brpc/1.0.0

cp ~/brpc/apache-brpc-1.0.0-src.tar.gz.sha512 ~/brpc_svn/dev/brpc/1.0.0
```

### 4. Submit SVN

Return to the parent directory and use the Apache LDAP account to submit SVN

```bash
cd ~/brpc_svn/dev/brpc

svn add *

svn --username=lorinlee commit -m "release 1.0.0"
```

## Verify release
```bash
cd ~/brpc_svn/dev/brpc/1.0.0
```
### 1. Verify SHA512 checksum

```bash
sha512sum --check apache-brpc-1.0.0-src.tar.gz.sha512
```

### 2. Verify GPG signature

First import the publisher's public key. Import KEYS from the svn repository to the local. (The person who releases the version does not need to import it again. The person who verify needs to import it.)

```bash
curl https://dist.apache.org/repos/dist/dev/brpc/KEYS >> KEYS

gpg --import KEYS
```

Trust the signature of publisher, execute the following command using the publisher's user name

```bash
gpg --edit-key lorinlee
```

output:
```
gpg (GnuPG) 2.3.1; Copyright (C) 2021 Free Software Foundation, Inc.
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.

Secret key is available.

gpg> trust

Please decide how far you trust this user to correctly verify other users' keys
(by looking at passports, checking fingerprints from different sources, etc.)

  1 = I don't know or won't say
  2 = I do NOT trust
  3 = I trust marginally
  4 = I trust fully
  5 = I trust ultimately
  m = back to the main menu

Your decision? 5
Do you really want to set this key to ultimate trust? (y/N) y

gpg> save
```

Then verify the GPG signature:
```
gpg --verify apache-brpc-1.0.0-src.tar.gz.asc apache-brpc-1.0.0-src.tar.gz
```

### 3. Check release content

#### 1. Compare the difference between the candidate source package and the package download through github tag page

Should use the `tar.gz` format tarball.

```bash
curl -Lo tag-1.0.0.tar.gz https://github.com/apache/brpc/archive/refs/tags/1.0.0.tar.gz

tar xvzf tag-1.0.0.tar.gz

tar xvzf apache-brpc-1.0.0-src.tar.gz

diff -r brpc-1.0.0 apache-brpc-1.0.0-src
```

#### 2. Check file content

- Check whether the source code package contains unnecessary files, which makes the tarball too large
- LICENSE and NOTICE files exist
- The year in the NOTICE file is correct
- Only text files exist, no binary files exist
- All files have an ASF license at the beginning
- Source code can be compiled correctly, and the unit test can pass
- Check whether there are redundant files or folders, such as empty folders
- Check for third-party dependency licenses:
   - Third party dependency license compatibility
   - All licenses of third-party dependency are declared in the LICENSE file
   - The complete version of the dependency license is in the license directory
   - If third-party dependency have the Apache license and have NOTICE files, these NOTICE files also need to be added to the releasing NOTICE file

## Vote in the Apache bRPC community

This stage will cost 3+ days.

### 1. Vote stage

1. Send a voting email to `dev@brpc.apache.org`. PMC needs to check the correctness of the version according to the document before voting. After at least 72 hours and 3 +1 PMC member votes, you can move to the next stage.
2. Announce the voting result and send the voting result to dev@brpc.apache.org.

### 2. Vote email template

1. Apache bRPC community vote email template

Title:
```
[VOTE] Release Apache bRPC 1.0.0
```

Content:

Note: `Release Commit ID` fills in the commit ID of the last commit of the current release branch.

```
Hi Apache bRPC Community,

This is a call for vote to release Apache bRPC version
1.0.0

[Release Note]
- xxx

The release candidates:
https://dist.apache.org/repos/dist/dev/brpc/1.0.0/

Git tag for the release:
https://github.com/apache/brpc/releases/tag/1.0.0

Release Commit ID:
https://github.com/apache/brpc/commit/xxx

Keys to verify the Release Candidate:
https://dist.apache.org/repos/dist/dev/brpc/KEYS

The vote will be open for at least 72 hours or until the necessary number of
votes are reached.

Please vote accordingly:
[ ] +1 approve
[ ] +0 no opinion
[ ] -1 disapprove with the reason

PMC vote is +1 binding, all others are +1 non-binding.

Checklist for reference:
[ ] Download links are valid.
[ ] Checksums and PGP signatures are valid.
[ ] Source code distributions have correct names matching the current
release.
[ ] LICENSE and NOTICE files are correct for each brpc repo.
[ ] All files have license headers if necessary.
[ ] No compiled archives bundled in source archive.

Regards,
LorinLee
```

2. Apache bRPC community announcement of vote result template

Title:
```
[Result] [VOTE] Release Apache bRPC 1.0.0
```

Content:
```
Hi all,

The vote to release Apache bRPC 1.0.0 has passed.

The vote PASSED with 3 binding +1, 3 non binding +1 and no -1 votes:

Binding votes:
- xxx
- yyy
- zzz

Non-binding votes:
- aaa
- bbb
- ccc

Vote thread: xxx (vote email link in https://lists.apache.org/)

Thank you to all the above members to help us to verify and vote for
the 1.0.0 release. I will process to publish the release and send ANNOUNCE.

Regards,
LorinLee
```

### 3. Vote not passed

If the community vote is not passed, please modify the code of the release branch, package and vote again.


## Finish the release

### 1. Move the release package from Apache SVN directory dist/dev to dist/release

Note: PMC members only, and it will also update the `KEYS` if changed.

```
svn mv https://dist.apache.org/repos/dist/dev/brpc/1.0.0 https://dist.apache.org/repos/dist/release/brpc/1.0.0 -m "release brpc 1.0.0"
```

### 2. Create github release

1. On the [GitHub Releases page](https://github.com/apache/brpc/tags) Click the corresponding version of to create a new Release
2. Edit the version number and version description, and click `Publish release`

### 3. Update download page

After waiting and confirming that the new release is synchronized to the Apache image, update the following page: <https://brpc.apache.org/docs/downloadbrpc/> by change the code in <https://github.com/apache/brpc-website/>. Please update both Chinese and English.

The download links of GPG signature files and hash check files should use this prefix: `https://downloads.apache.org/brpc/`

The download link of the code package should use this prefix: `https://dlcdn.apache.org/brpc/`

### 4. Send email to announce release finished

Send mail to `dev@brpc.apache.org` and `announce@apache.org` to announce the completion of release.

Note: The email account must use **personal apache email**, and the email content must be **plain text format** ("plain text mode" can be selected in gmail). And email to `announce@apache.org` mail group will be delivered after manual review. Please wait patiently after sending the email, and it will be passed within one day.

See https://shenyu.apache.org/community/use-apache-email for more details to setup a personal apache account email, the `SMTP` server should be `mail-relay.apache.org`.

The announcement email template:

Title:
```
[ANNOUNCE] Apache bRPC 1.0.0 released
```

Content:

Note: `Brief notes of this release` It is only necessary to list the main changes of this release, without corresponding contributors and PR numbers. It is recommended to refer to the previous announcement email.

```
Hi all,

The Apache bRPC community is glad to announce the new release
of Apache bRPC 1.0.0.

Apache bRPC is an Industrial-grade RPC framework using C++ Language,
which is often used in high performance systems such as Search, Storage,
Machine learning, Advertisement, Recommendation etc.

Brief notes of this release:
- xxx
- yyy
- zzz

More details regarding Apache brpc can be found at:
https://brpc.apache.org/

The release is available for download at:
https://brpc.apache.org/download/

The release notes can be found here:
https://github.com/apache/brpc/releases/tag/1.0.0

Website: https://brpc.apache.org/

Apache bRPC Resources:
- Issue: https://github.com/apache/brpc/issues/
- Mailing list: dev@brpc.apache.org
- Documents: https://brpc.apache.org/docs/

We would like to thank all contributors of the Apache bRPC community
who made this release possible!


Best Regards,
Apache bRPC Community
```

### 5. Publish WeChat official account announcement

Reference <https://mp.weixin.qq.com/s/DeFhpAV_AYsn_Xd1ylPTSg>.

### 6. Update master branch

After the release is completed, merge the release branch into the `master` branch.
