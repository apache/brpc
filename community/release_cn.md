# brpc 发布 apache release 版本流程 step by step

## 准备工作

### 1. 确认 Release Notes

该阶段会持续一周左右，主要工作包含：
- 确认 Release Notes 中是否有遗漏项以及各项的描述是否准确、恰当；
- 合入计划发布但未及时处理的 PR。

Release Notes 需要提供中英两个版本，用于不同的对外发布渠道。

模板如下：
```
[Release Notes]

Feature:
- new feature 1;
- new feature 2;
- ...

Bugfix:
- fix bug 1;
- fix bug 2;
- ...

Enhancement:
- improve blabla

Other:
- ...
```

### 2. 设置 GPG（非首次发版的同学请直接跳过此步骤）

#### 1. 安装 GPG

通常 Linux 发行版中会集成 `GnuPG` 工具，OSX 可以使用 [`brew`](https://brew.sh/) 安装。
```bash
brew install gnupg
```

也可以直接在[GnuPG 官网](https://www.gnupg.org/download/index.html)下载相应的安装包。`GnuPG` 的 1.x 版本和 2.x 版本的命令有细微差别，下面以 `GnuPG-2.3.1` 版本（OSX）为例进行说明。

安装完成后，执行以下命令查看版本号。
```bash
gpg --version
```

#### 2. 创建 key

安装完成后，执行以下命令创建 key。

```bash
gpg --full-gen-key
```

根据提示完成创建 key，注意邮箱要使用 Apache 邮件地址，`Real Name` 使用姓名 Pinyin、Apache ID 或 GitHub ID 等均可：
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

#### 3. 查看生成的 key

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

#### 4. 将公钥公布到服务器

命令如下：

```bash
gpg --keyserver hkps://pgp.mit.edu --send-key C30F211F071894258497F46392E18A11B6585834
```
keyserver 也可以用 `hkps://keys.openpgp.org` 或 `hkps://keyserver.ubuntu.com`，Web 上都提供了比较方便的查询接口。

#### 5. 生成 fingerprint 并上传到 Apache 用户信息中

由于公钥服务器没有检查机制，任何人都可以用你的名义上传公钥，所以没有办法保证服务器上的公钥的可靠性。通常，你可以在⽹站上公布一个公钥指纹，让其他⼈核对下载到的公钥是否为真。fingerprint 参数生成公钥指纹。

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

将上面的 fingerprint `C30F 211F 0718 9425 8497  F463 92E1 8A11 B658 5834` 粘贴到⾃己 Apache ⽤户信息 https://id.apache.org 的 `OpenPGP Public Key Primary Fingerprint:` 字段中。

## 制作发布包

### 1. 拉出发版分支

如果是发布新的 2 位版本，如 `1.0.0`，则需要从 master 拉出新的分支 `release-1.0`。

如果是在已有的 2 位版本上发布新的 3 位版本，如 `1.0.1` 版本，则只需要在已有的 `release-1.0` 分支上修改加上要发布的内容。

发版过程中的操作都在 release 分支（如：`release-1.0`）上操作，如果发版过程发现代码有问题需要修改，也在该分支上进行修改。发版完成后，将该分支合回 master 分支。

### 2. 更新 `NOTICE` 文件

检查 `NOTICE` 文件中的年份是否需要更新，通常在年初发版时需重点关注。

### 3. 编辑版本号相关文件

#### 更新 `RELEASE_VERSION` 文件

编辑项目根目录下 `RELEASE_VERSION` 文件，更新版本号，并提交至代码仓库，本文以 `1.0.0` 版本为例，文件内容为：

```
1.0.0
```

#### 更新 `CMakeLists.txt` 文件
编辑项目根目录下 `CMakeLists.txt` 文件，更新版本号，并提交至代码仓库，本文以 `1.0.0` 版本为例，修改 `BRPC_VERSION` 为：

```
set(BRPC_VERSION 1.0.0)
```

#### 更新 `package/rpm/brpc.spec` 文件

编辑项目根目录下 `package/rpm/brpc.spec` 文件，更新版本号，并提交至代码仓库，本文以 `1.0.0` 版本为例，修改 `Version` 为：

```
Version:	1.0.0
```

#### 更新 `MODULE.bazel` 文件

编辑项目根目录下 `MODULE.bazel` 文件，更新版本号，并提交至代码仓库，本文以 `1.0.0` 版本为例，修改 `version` 为：

```
# in MODULE.bazel
module(
  ...
  version = '1.0.0',
  ...
)
```

### 4. 创建发布 tag
```bash
export BRPCVERSION=1.0.0
export BRPCBRANCH=1.0
export BRPCUSERNAME=lorinlee
```
拉取发布分支，创建并推送 tag
```bash
git clone -b release-$BRPCBRANCH git@github.com:apache/brpc.git ~/brpc

cd ~/brpc

git tag -a $BRPCVERSION -m "release $BRPCVERSION"

git push origin --tags
```

### 5. 打包发布包

```bash
git archive --format=tar $BRPCVERSION --prefix=apache-brpc-$BRPCVERSION-src/ | gzip > apache-brpc-$BRPCVERSION-src.tar.gz
```
或
```bash
git archive --format=tar.gz $BRPCVERSION --prefix=apache-brpc-$BRPCVERSION-src/ --output=apache-brpc-$BRPCVERSION-src.tar.gz
```

### 6. 生成签名文件

```bash
gpg -u $BRPCUSERNAME@apache.org --armor --output apache-brpc-$BRPCVERSION-src.tar.gz.asc --detach-sign apache-brpc-$BRPCVERSION-src.tar.gz

gpg --verify apache-brpc-$BRPCVERSION-src.tar.gz.asc apache-brpc-$BRPCVERSION-src.tar.gz
```

### 7. 生成哈希文件

```bash
sha512sum apache-brpc-$BRPCVERSION-src.tar.gz > apache-brpc-$BRPCVERSION-src.tar.gz.sha512

sha512sum --check apache-brpc-$BRPCVERSION-src.tar.gz.sha512
```

## 发布至 Apache SVN 仓库

### 1. 检出 dist/dev 下的 brpc 仓库目录

如无本地工作目录，则先创建本地工作目录。将 Apache SVN 仓库克隆下来，username 需要使用自己的 Apache LDAP 用户名。

```bash
mkdir -p ~/brpc_svn/dev/

cd ~/brpc_svn/dev/

svn --username=$BRPCUSERNAME co https://dist.apache.org/repos/dist/dev/brpc/

cd ~/brpc_svn/dev/brpc
```

### 2. 添加 GPG 公钥

仅第一次部署的账号需要添加，只要 KEYS 中包含已经部署过的账户的公钥即可。

```bash
(gpg --list-sigs $BRPCUSERNAME && gpg -a --export $BRPCUSERNAME) >> KEYS
```

注意：当有多个名相同的 key 时，可以指定完整邮件地址或者公钥来导出指定的公钥信息。如：
```bash
(gpg --list-sigs $BRPCUSERNAME@apache.org && gpg -a --export $BRPCUSERNAME@apache.org) >> KEYS
```
或：
```bash
(gpg --list-sigs C30F211F071894258497F46392E18A11B6585834 && gpg -a --export C30F211F071894258497F46392E18A11B6585834) >> KEYS
```

### 3. 将待发布的代码包添加至 SVN 目录

```bash
mkdir -p ~/brpc_svn/dev/brpc/$BRPCVERSION

cd ~/brpc_svn/dev/brpc/$BRPCVERSION

cp ~/brpc/apache-brpc-$BRPCVERSION-src.tar.gz ~/brpc_svn/dev/brpc/$BRPCVERSION

cp ~/brpc/apache-brpc-$BRPCVERSION-src.tar.gz.asc ~/brpc_svn/dev/brpc/$BRPCVERSION

cp ~/brpc/apache-brpc-$BRPCVERSION-src.tar.gz.sha512 ~/brpc_svn/dev/brpc/$BRPCVERSION
```

### 4. 提交 SVN

退回到上级目录，使用 Apache LDAP 账号提交 SVN。

```bash
cd ~/brpc_svn/dev/brpc

svn add *

svn --username=$BRPCUSERNAME commit -m "release $BRPCVERSION"
```

## 检查发布结果
```bash
cd ~/brpc_svn/dev/brpc/$BRPCVERSION
```
### 1. 检查 sha512 哈希

```bash
sha512sum --check apache-brpc-$BRPCVERSION-src.tar.gz.sha512
```

### 2. 检查 GPG 签名

首先导入发布人公钥。从 svn 仓库导入 KEYS 到本地环境。（发布版本的人不需要再导入，帮助做验证的人需要导入，用户名填发版人的即可）

```bash
curl https://dist.apache.org/repos/dist/dev/brpc/KEYS >> KEYS

gpg --import KEYS
```

设置信任该用户的签名，执行以下命令，填写发布人的用户名
```bash
gpg --edit-key $BRPCUSERNAME
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

然后进行 gpg 签名检查。
```bash
gpg --verify apache-brpc-$BRPCVERSION-src.tar.gz.asc apache-brpc-$BRPCVERSION-src.tar.gz
```

### 3. 检查发布内容

#### 1. 对比源码包与 github 上的 tag 内容差异

```bash
curl -Lo tag-$BRPCVERSION.tar.gz https://github.com/apache/brpc/archive/refs/tags/$BRPCVERSION.tar.gz

tar xvzf tag-$BRPCVERSION.tar.gz

tar xvzf apache-brpc-$BRPCVERSION-src.tar.gz

diff -r brpc-$BRPCVERSION apache-brpc-$BRPCVERSION-src
```

#### 2. 检查源码包的文件内容

- 检查源码包是否包含由于包含不必要文件，致使 tarball 过于庞大
- 存在 LICENSE 和 NOTICE 文件，并且 NOTICE 文件中的年份正确
- 只存在文本文件，不存在二进制文件
- 所有文件的开头都有 ASF 许可证
- 能够正确编译，单元测试可以通过
- 检查是否有多余文件或文件夹，例如空文件夹等
- 检查第三方依赖许可证：
  - 第三方依赖的许可证兼容
  - 所有第三方依赖的许可证都在 LICENSE 文件中声名
  - 依赖许可证的完整版全部在 license 目录
  - 如果依赖的是 Apache 许可证并且存在 NOTICE 文件，那么这些 NOTICE 文件也需要加入到版本的 NOTICE 文件中

## 在 Apache bRPC 社区发起投票

该阶段会持续至少 3 天。

### 1. 投票阶段

1. 发起投票邮件到 dev@brpc.apache.org。PMC 需要先按文档检查版本的正确性，然后再进行投票。经过至少 72 小时并统计到 3 个 +1 PMC member 票后，方可进入下一阶段。
2. 宣布投票结果，发起投票结果邮件到 dev@brpc.apache.org。

### 2. 投票邮件模板

#### Apache bRPC 社区投票邮件模板

标题：
```
[VOTE] Release Apache bRPC 1.0.0
```

正文：
```
Hi Apache bRPC Community,

This is a call for vote to release Apache bRPC version 1.0.0

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

注：`Release Commit ID` 填写当前 tag（1.0.0） 的 commit id。

#### Apache bRPC 社区宣布结果邮件模板

标题：
```
[RESULT] [VOTE] Release Apache bRPC 1.0.0
```

正文：
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

### 3. 投票未通过

若社区投票未通过，则在 release 分支对代码仓库进行修改，重新打包，发起投票。

## 完成发布

### 1. 将软件包从 Apache SVN 仓库的 dist/dev 目录移至 dist/release 目录

注意：该过程需要 PMC 成员进行，投票通过后可以联系 PMC 成员进行相关操作，首次发版的成员还需要更新公钥信息 KEYS。

```bash
svn mv https://dist.apache.org/repos/dist/dev/brpc/$BRPCVERSION https://dist.apache.org/repos/dist/release/brpc/$BRPCVERSION -m "release brpc $BRPCVERSION"
```

### 2. 创建 GitHub 版本发布页

在 [GitHub Releases 页面](https://github.com/apache/brpc/tags)的对应 tag 上点击，创建新的 Release，编辑版本号及版本说明。注意Release title统一根据本次版本号写为Apache bRPC 1.x.0，并点击 Publish release。

### 3. 更新网站下载页

等待并确认新的发布版本同步至 Apache 镜像后，更新如下页面：<https://brpc.apache.org/docs/downloadbrpc/>, 更新方式在 <https://github.com/apache/brpc-website/> 仓库中，注意中英文都要更新。

GPG 签名文件和哈希校验文件的下载链接应该使用这个前缀：https://downloads.apache.org/brpc/

代码包的下载链接应该使用这个前缀：https://dlcdn.apache.org/brpc/

### 4. 发送邮件通知新版发布

发送邮件到 dev@brpc.apache.org 和 announce@apache.org 通知完成版本发布。

注意：发邮件账号必须使用**个人 apache 邮箱**，且邮件内容必须是**纯文本格式**（可在 gmail 选择"纯文本模式"）。announce@apache.org 邮件组需要经过人工审核才能送达，发出邮件后请耐心等待，一般会在一天之内通过。

个人 apache 邮箱配置方式可以参考：https://shenyu.apache.org/zh/community/use-apache-email
注意：SMTP 服务器需要填 `mail-relay.apache.org`。

通知邮件模板如下：

标题：
```
[ANNOUNCE] Apache bRPC 1.0.0 released
```

正文：
注：`Brief notes of this release` 仅需列出本次发版的主要变更，且无需指出对应贡献人和 PR 编号，建议参考下之前的 Announce 邮件。
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

### 5. 其他对外公共账号

#### 微信公众号

参考 <https://mp.weixin.qq.com/s/DeFhpAV_AYsn_Xd1ylPTSg>。  
建议先在腾讯文档中编辑后粘贴至公众号，统一字体大小和格式，参考 [腾讯文档：bRPC 1.11.0](https://docs.qq.com/doc/DYmZ2Tnpub1lySWZO?_t=1730208105245&u=31460cd039dd4461877a61ab9f56be1f)


### 6. 更新 master 分支

发版完成后，将 release 分支合并到 master 分支。
