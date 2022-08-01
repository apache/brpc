brpc 发布apache release 版本流程step by step
===
概述：分为如下几个步骤
1. 事前准备：包括生成签名需要的key，github上打标签，修改version文件等
2. 发布软件包：包括制作source tarball，签名，上传到制定地点并验证
3. 第一次投票：在dev@brpc邮件群里投票
4. 第二次投票：在general@incubator.apache.org邮件群里投票
5. 发版通告：包括更新brpc网站，发邮件


# 签名准备

## 1. 安装 GPG
在[GnuPG官网](https://www.gnupg.org/download/index.html)下载安装包。 GnuPG的1.x版本和2.x版本的命令有细微差别，下列说明以`GnuPG-2.3.1`版本（OSX）为例。

安装完成后，执行以下命令查看版本号。
```bash
gpg --version
```

## 2. 创建 key

安装完成后，执行以下命令创建key。

```bash
gpg --full-gen-key
```

根据提示完成创建key，注意邮箱要使用Apache邮件地址：
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
You need a Passphrase to protect your secret key. # 输入密码

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

## 3. 查看生成的key

```bash
gpg --list-keys
```

执行结果：

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

其中 `C30F211F071894258497F46392E18A11B6585834` 为公钥ID。

## 4. 将公钥公布到服务器

命令如下：

```bash
gpg --keyserver hkp://pgp.mit.edu --send-key C30F211F071894258497F46392E18A11B6585834
```

## 5. 生成 fingerprint 并上传到 apache 用户信息中
由于公钥服务器没有检查机制，任何人都可以用你的名义上传公钥，所以没有办法保证服务器上的公钥的可靠性。通常，你可以在⽹站上公布一个公钥指纹，让其他⼈核对下载到的公钥是否为真。fingerprint参数生成公钥指纹。   

执行如下命令查看 fingerprint：
```
gpg --fingerprint lorinlee（用户ID）
```

输出如下：
```
/Users/lilei/.gnupg/pubring.kbx
----------------------------------
pub   rsa4096 2021-10-17 [SC]
      C30F 211F 0718 9425 8497  F463 92E1 8A11 B658 5834
uid           [ultimate] LorinLee (lorinlee's key) <lorinlee@apache.org>
sub   rsa4096 2021-10-17 [E]
```

将上面的 fingerprint 粘贴到⾃己的⽤户信息中: https://id.apache.org

# 发布包准备

## 1. 编辑 RELEASE_VERSION 文件

### 更新RELEASE_VERSION文件
编辑项目根目录下`RELEASE_VERSION`文件，更新版本号，并提交至代码仓库，本文以`1.0.0`版本为例，文件内容为：

```
1.0.0
```

### 更新CMakeLists.txt文件
编辑项目根目录下`CMakeLists.txt`文件，更新版本号，并提交至代码仓库，本文以`1.0.0`版本为例，修改BRPC_VERSION为：

```
set(BRPC_VERSION 1.0.0)
```

## 2. 创建发布 tag
拉取发布分支，并推送tag
```bash
git clone -b ${branch} git@github.com:apache/incubator-brpc.git ~/incubator-brpc

cd ~/incubator-brpc

git tag -a 1.0.0 -m "release 1.0.0"

git push origin --tags
```

## 3. 打包发布包

```bash
git archive --format=tar 1.0.0 --prefix=apache-brpc-1.0.0-incubating-src/ | gzip > apache-brpc-1.0.0-incubating-src.tar.gz
```

## 4. 生成签名文件

```bash
gpg -u lorinlee@apache.org --armor --output apache-brpc-1.0.0-incubating-src.tar.gz.asc --detach-sign apache-brpc-1.0.0-incubating-src.tar.gz

gpg --verify apache-brpc-1.0.0-incubating-src.tar.gz.asc apache-brpc-1.0.0-incubating-src.tar.gz

```

## 5. 生成哈希文件

```bash
sha512sum apache-brpc-1.0.0-incubating-src.tar.gz > apache-brpc-1.0.0-incubating-src.tar.gz.sha512

sha512sum --check apache-brpc-1.0.0-incubating-src.tar.gz.sha512
```

# 发布至Apache SVN仓库

## 1. 检出 dist/dev 下的 brpc 仓库目录

如无本地工作目录，则先创建本地工作目录。将Apache SVN仓库克隆下来，username需要使用自己的Apache LDAP用户名

```bash
mkdir -p ~/brpc_svn/dev/

cd ~/brpc_svn/dev/

svn --username=lorinlee co https://dist.apache.org/repos/dist/dev/incubator/brpc/

cd ~/brpc_svn/dev/brpc
```

## 2. 添加GPG公钥

仅第一次部署的账号需要添加，只要KEYS中包含已经部署过的账户的公钥即可。

```
(gpg --list-sigs lorinlee && gpg -a --export lorinlee) >> KEYS
```

## 3. 将待发布的代码包添加至SVN目录

```bash
mkdir -p ~/brpc_svn/dev/brpc/1.0.0

cd ~/brpc_svn/dev/brpc/1.0.0

cp ~/incubator-brpc/apache-brpc-1.0.0-incubating-src.tar.gz ~/brpc_svn/dev/brpc/1.0.0

cp ~/incubator-brpc/apache-brpc-1.0.0-incubating-src.tar.gz.asc ~/brpc_svn/dev/brpc/1.0.0

cp ~/incubator-brpc/apache-brpc-1.0.0-incubating-src.tar.gz.sha512 ~/brpc_svn/dev/brpc/1.0.0
```

## 4. 提交SVN

使用Apache LDAP账号提交SVN

```bash
svn add *

svn --username=lorinlee commit -m "release 1.0.0"
```

# 检查发布结果

## 1. 检查sha512哈希

```bash
sha512sum --check apache-brpc-1.0.0-incubating-src.tar.gz.sha512
```

## 2. 检查GPG签名
首先导入发布人公钥。从svn仓库导入KEYS到本地环境。（发布版本的人不需要再导入，帮助做验证的人需要导入，用户名填发版人的即可）

```bash
curl https://dist.apache.org/repos/dist/dev/incubator/brpc/KEYS >> KEYS

gpg --import KEYS
```

设置信任该用户的签名，执行以下命令，填写发布人的用户名
```bash
gpg --edit-key lorinlee
```

输出为
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

然后进行gpg签名检查。
```
gpg --verify apache-brpc-1.0.0-incubating-src.tar.gz.asc apache-brpc-1.0.0-incubating-src.tar.gz
```

## 3. 检查发布内容

### 1. 对比源码包与github上的tag内容差异

```bash
curl -Lo tag-1.0.0.tar.gz https://github.com/apache/incubator-brpc/archive/refs/tags/1.0.0.tar.gz

tar xvzf tag-1.0.0.tar.gz

tar xvzf apache-brpc-1.0.0-incubating-src.tar.gz

diff -r brpc-1.0.0 apache-brpc-1.0.0-incubating-src
```

### 2. 检查源码包的文件内容

- 检查源码包是否包含由于包含不必要文件，致使tarball过于庞大
- 存在LICENSE和NOTICE文件
- NOTICE文件中的年份正确
- 只存在文本文件，不存在二进制文件
- 所有文件的开头都有ASF许可证
- 能够正确编译，单元测试可以通过
- 检查是否有多余文件或文件夹，例如空文件夹等
- 检查第三方依赖许可证：
  - 第三方依赖的许可证兼容
  - 所有第三方依赖的许可证都在LICENSE文件中声名
  - 依赖许可证的完整版全部在license目录
  - 如果依赖的是Apache许可证并且存在NOTICE文件，那么这些NOTICE文件也需要加入到版本的NOTICE文件中

# 在Apache brpc社区发起投票

## 1. 投票阶段

1. brpc社区投票，发起投票邮件到dev@brpc.apache.org。PPMC需要先按文档检查版本的正确性，然后再进行投票。经过至少72小时并统计到3个+1 PPMC member票后，即可进入下一阶段。
2. 宣布投票结果，发起投票结果邮件到dev@brpc.apache.org。

## 2. 投票邮件模板

1. Apache brpc 社区投票邮件模板

标题：
```
[VOTE] Release Apache brpc (Incubating) 1.0.0
```

正文：
```
Hi Apache brpc (Incubating) Community,

This is a call for vote to release Apache brpc (Incubating) version
1.0.0

[Release Note]
- xxx

The release candidates:
https://dist.apache.org/repos/dist/dev/incubator/brpc/1.0.0/

Git tag for the release:
https://github.com/apache/incubator-brpc/releases/tag/1.0.0

Release Commit ID:
https://github.com/apache/incubator-brpc/commit/xxx

Keys to verify the Release Candidate:
https://dist.apache.org/repos/dist/dev/incubator/brpc/KEYS

The vote will be open for at least 72 hours or until necessary number of
votes are reached.

Please vote accordingly:
[ ] +1 approve
[ ] +0 no opinion
[ ] -1 disapprove with the reason

PMC vote is +1 binding, all others is +1 non-binding.

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

2. Apache brpc 社区宣布结果邮件模板

标题：
```
[Result] [VOTE] Release Apache brpc (Incubating) 1.0.0
```

正文：
```
Hi all,

The vote to release Apache brpc (Incubating) 1.0.0 has passed.

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

Thank you to all the above members to help us to verify and vote for the 1.0.0 release. We will move to IPMC voting shortly.

Regards,
LorinLee
```

## 3. 投票未通过

若社区投票未通过，则对代码仓库进行修改，重新打包，发起投票。

# 在Apache Incubator社区发起投票

## 1. 更新GPG签名

```
svn delete https://dist.apache.org/repos/dist/release/incubator/brpc/KEYS -m "delete KEYS"

svn cp https://dist.apache.org/repos/dist/dev/incubator/brpc/KEYS https://dist.apache.org/repos/dist/release/incubator/brpc/KEYS -m "update brpc KEYS"
```

提交完svn后，访问 <https://downloads.apache.org/incubator/brpc/KEYS>，检查内容有没有更新，可能需要等几分钟时间，等内容更新了，再继续下一步。

## 2. 投票阶段

1. Incubator社区投票，发起投票邮件到general@incubator.apache.org。IPMC会进行投票。经过至少72小时并统计到3个+1 IPMC member票后，即可进入下一阶段。
2. 宣布投票结果，发起投票结果邮件到general@incubator.apache.org。

## 3. 投票邮件模板

1. Apache Incubator 社区投票邮件模板

标题：
```
[VOTE] Release Apache brpc (Incubating) 1.0.0
```

正文：
```
Hi Incubator Community,

This is a call for a vote to release Apache brpc(Incubating) version
1.0.0.

The Apache brpc community has voted and approved the release of Apache
brpc (Incubating) 1.0.0.

We now kindly request the Incubator PMC members review and vote on this
incubator release.

brpc is an industrial-grade RPC framework with extremely high performance,
and it supports multiple protocols, full rpc features, and has many
convenient tools.

brpc community vote thread: xxx

Vote result thread: xxx

The release candidate:
https://dist.apache.org/repos/dist/dev/incubator/brpc/1.0.0/

This release has been signed with a PGP available here:
https://downloads.apache.org/incubator/brpc/KEYS

Git tag for the release:
https://github.com/apache/incubator-brpc/releases/tag/1.0.0

Build guide and get started instructions can be found at:
https://brpc.apache.org/docs/getting_started

The vote will be open for at least 72 hours or until the necessary number
of votes is reached.

Please vote accordingly:
[ ] +1 approve
[ ] +0 no opinion
[ ] -1 disapprove with the reason

Regards,
Lorin Lee
Apache brpc (Incubating) community
```

2. Apache Incubator 社区宣布结果邮件模板

标题：
```
[Result] [VOTE] Release Apache brpc (Incubating) 1.0.0
```

正文：
```
Hi Incubator Community,

Thanks to everyone that participated. The vote to release Apache
brpc (Incubating) version 1.0.0 in general@incubator.apache.org
is now closed.

Vote thread: xxx

The vote PASSED with 3 binding +1, 3 non binding +1 and no -1 votes:

Binding votes:
- xxx 
- yyy 
- zzz 

Non-binding votes:
- aaa
- bbb
- ccc

Many thanks for all our mentors helping us with the release procedure,
and all IPMC helped us to review and vote for Apache brpc(Incubating)
release. We will proceed with publishing the approved artifacts and
sending out the announcement soon.

Regards,
Lorin Lee
Apache brpc (Incubating) community
```

# 完成发布

## 1. 将发布包从Apache SVN仓库 dist/dev 移动至 dist/release

```
svn mv https://dist.apache.org/repos/dist/dev/incubator/brpc/1.0.0 https://dist.apache.org/repos/dist/release/incubator/brpc/1.0.0 -m "release brpc 1.0.0"
```

## 2. Github版本发布

在 GitHub Releases 页面的对应版本上点击 Edit
编辑版本号及版本说明，并点击 Publish release

## 3. 更新下载页面

等待并确认新的发布版本同步至 Apache 镜像后，更新如下页面：
`https://brpc.apache.org/docs/downloadbrpc/`，更新方式在 `https://github.com/apache/incubator-brpc-website/` 仓库中，注意中英文都要更新。

GPG签名文件和哈希校验文件的下载链接应该使用这个前缀：https://downloads.apache.org/incubator/brpc/

代码包的下载链接应该使用这个前缀：https://dlcdn.apache.org/incubator/brpc/

## 4. 发送邮件通知发布完成

发送邮件到dev@brpc.apache.org、general@incubator.apache.org、和announce@apache.org通知完成版本发布。

注意：发邮件账号必须使用**个人apache邮箱**，且邮件内容必须是**纯文本格式**（可在gmail选择"纯文本模式"），announce@apache.org 邮件组需要经过人工审核才能送达，发出邮件后请耐心等待，一般会在一天之内通过。

通知邮件模板如下：

标题：
```
[ANNOUNCE] Apache brpc (Incubating) 1.0.0 released
```

正文
```
Hi all,

The Apache brpc (Incubating) community is glad to announce the new release
of Apache brpc (Incubating) 1.0.0.

brpc is an industrial-grade RPC framework with extremely high performance,
and it supports multiple protocols, full rpc features, and has many
convenient tools. 

Brief notes of this release:
- xxx
- yyy
- zzz

More details regarding Apache brpc can be found at:
http://brpc.apache.org/

The release is available for download at:
https://brpc.apache.org/docs/downloadbrpc/

The release notes can be found here:
https://github.com/apache/incubator-brpc/releases/tag/1.0.0

Website: http://brpc.apache.org/

brpc(Incubating) Resources:
- Issue: https://github.com/apache/incubator-brpc/issues/
- Mailing list: dev@brpc.apache.org
- Documents: https://brpc.apache.org/docs/

We would like to thank all contributors of the Apache brpc community and
Incubating community who made this release possible!


Best Regards,
Apache brpc (Incubating) community
```
