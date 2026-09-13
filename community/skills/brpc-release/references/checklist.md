# 发版检查清单

`SKILL.md` 里省略的细节都在这里。按需查阅对应小节，不必通读。

---

## GPG 首次设置

只有**从未发过版**的 RM 需要做。已有密钥的直接跳到「SVN 上传」。

> **口令安全铁律**：模型不得执行 `make_package.sh` 或任何签名命令，只能把完整的脚本命令提供给 RM。
> RM 在交互式终端运行脚本，由 GPG pinentry 获取私钥口令。任何要求用 `--passphrase`、
> `--pinentry-mode loopback` 或环境变量传递口令的做法都要**拒绝**——私钥口令绝不落盘、
> 绝不进命令行历史、绝不交给 agent。脚本会完成签名、签名验证和 SHA512 校验；RM 明确回报成功前，
> 模型不得进入下一阶段。

### 1. 安装

```bash
brew install gnupg          # macOS
# Linux 发行版通常自带 GnuPG
gpg --version
```

### 2. 创建密钥

```bash
gpg --full-gen-key
```

交互式提示的关键选择：

| 提示 | 填什么 |
|---|---|
| kind of key | `1`（RSA and RSA） |
| keysize | `4096` |
| valid for | `0`（永不过期） |
| Real name | 姓名拼音 / Apache ID / GitHub ID 均可 |
| Email address | **必须是 `<apache-id>@apache.org`** |
| Passphrase | 设一个并记牢，后面每次签名都要输 |

生成后会打印公钥 ID，形如 `C30F211F071894258497F46392E18A11B6585834`。

### 3. 发布公钥到 keyserver

```bash
gpg --keyserver hkps://pgp.mit.edu --send-key <公钥ID>
```

`hkps://keys.openpgp.org` 和 `hkps://keyserver.ubuntu.com` 也可以，都提供 Web 查询界面。

### 4. 登记 fingerprint

```bash
gpg --fingerprint <用户ID>
```

把输出里带空格的指纹（如 `C30F 211F 0718 9425 8497  F463 92E1 8A11 B658 5834`）粘贴到
https://id.apache.org 的 `OpenPGP Public Key Primary Fingerprint:` 字段。

公钥服务器没有校验机制，任何人都能以你的名义上传公钥，所以必须在 Apache 官方渠道公布指纹供人核对。

### 5. 把公钥追加进 SVN 的 KEYS

在 `~/brpc_release/svn/dev/brpc` 目录下：

```bash
(gpg --list-sigs $BRPCUSERNAME && gpg -a --export $BRPCUSERNAME) >> KEYS
```

同名密钥有多个时，用完整邮箱或公钥 ID 指定：

```bash
(gpg --list-sigs $BRPCUSERNAME@apache.org && gpg -a --export $BRPCUSERNAME@apache.org) >> KEYS
# 或
(gpg --list-sigs <公钥ID> && gpg -a --export <公钥ID>) >> KEYS
```

---

## 版本号文件清单

`bump_version.sh` 覆盖下面全部 7 个文件。手改容易遗漏，应优先使用脚本并执行 `--check` 校验。

| 文件 | 形式 |
|---|---|
| `RELEASE_VERSION` | 整个文件就是版本号 |
| `CMakeLists.txt` | `set(BRPC_VERSION 1.18.0)` |
| `package/rpm/brpc.spec` | `Version:\t1.18.0` |
| `CLAUDE.md` | `Current version: 1.18.0.` |
| `MODULE.bazel` | `module(... version = '1.18.0' ...)` |
| `docs/cn/bazel_support.md` | `bazel_dep(name = "brpc", version = "1.18.0", ...)` |
| `docs/en/bazel_support.md` | 同上 |

`example/build_with_bazel_module/MODULE.bazel` 是示例配置，其中的 module 版本和 bRPC 依赖版本不随本次发版修改。

另外手工确认：

- `NOTICE` 的年份是 `Copyright 2018-<当前年份>`（年初发版必查）

发版不要求修改 Git 身份。模型和脚本不得执行 `git config user.name` 或 `git config user.email`；
若创建 annotated tag 时发现身份未配置，只提示 RM 自行处理并停止。

---

## SVN 上传

先从状态文件读取 Apache ID；若尚未记录，则向 RM 询问。这里只需要 Apache ID（用户名），不要询问或保存
Apache LDAP 密码。`svn` 需要认证时，由 RM 在终端中交互输入密码。

```bash
export BRPCVERSION=1.18.0
export BRPCUSERNAME=<你的 apache id>

# 首次：检出 dist/dev 工作副本
mkdir -p ~/brpc_release/svn/dev/
svn --username=$BRPCUSERNAME co https://dist.apache.org/repos/dist/dev/brpc/ ~/brpc_release/svn/dev/brpc

# 放入三个产物（源目录是 make_package.sh 的输出目录）
mkdir -p ~/brpc_release/svn/dev/brpc/$BRPCVERSION
cp ~/brpc_release/$BRPCVERSION/apache-brpc-$BRPCVERSION-src.tar.gz{,.asc,.sha512} \
   ~/brpc_release/svn/dev/brpc/$BRPCVERSION/

cd ~/brpc_release/svn/dev/brpc
svn add --force .
```

最后一步**由 RM 亲自执行**：

```bash
svn --username=$BRPCUSERNAME commit -m "release $BRPCVERSION"
```

首次发版的 RM 记得把「GPG 首次设置」第 5 步生成的 `KEYS` 变更一起提交。

---

## 候选包检查项

`verify_package.sh` 自动覆盖的：

- [ ] 下载链接有效
- [ ] sha512 哈希正确
- [ ] GPG 签名正确
- [ ] 包内 `RELEASE_VERSION` 和 `CMakeLists.txt` 的版本号与本次发布一致
- [ ] 存在 `LICENSE` 和 `NOTICE`
- [ ] 不含编译产物 / 意外的二进制文件
- [ ] 源码包内容与 GitHub tag 完全一致

需要**人工判断**的：

- [ ] `NOTICE` 年份正确
- [ ] 源码包体积合理，没有夹带无关文件
- [ ] 所有源文件都有 ASF License 头（本仓库用 skywalking-eyes 在 CI 里查，配置见 `.licenserc.yaml`）
- [ ] 能正常编译，单测通过
- [ ] 没有空目录等多余文件夹
- [ ] 第三方依赖许可证：
  - 许可证兼容（见下方分类）
  - 所有第三方依赖都在 `LICENSE` 中声明
  - 依赖许可证全文都在 `licenses/` 目录
  - 依赖若是 Apache 许可证且带 `NOTICE`，其 NOTICE 内容要并入本项目 `NOTICE`

### ASF 许可证分类

| 类别 | 含义 | 例子 |
|---|---|---|
| Category A | 允许 | Apache-2.0, BSD-3-Clause, MIT |
| Category B | 允许依赖，但不允许放进源码包 | EPL, MPL, CDDL |
| Category X | **禁止** | GPL, LGPL, CC Non-Commercial |

### 导入他人公钥做验证

帮别人验包时需要先导入并信任发布人公钥（RM 验自己的包不需要）：

```bash
curl https://dist.apache.org/repos/dist/dev/brpc/KEYS | gpg --import
gpg --edit-key <发布人用户名>
# gpg> trust  → 选 5（ultimate）→ y → save
```

### 常见的 -1 原因

- 包名不对，和当前发布版本对不上
- 签名或哈希校验失败
- 源码包里混进了编译产物或 jar/so 等二进制
- 缺 `LICENSE` / `NOTICE`，或 NOTICE 年份过期
- 源文件缺 ASF License 头
- 引入了 Category X 许可证的依赖

---

## PMC 投票回复格式

**不要只回 `+1`**，必须列出实际检查了哪些项：

```
+1 (binding)

I checked:
- LICENSE and NOTICE are good
- signatures and hashes correct
- All ASF files have ASF headers
- no unexpected binary files
- source distribution matches the git tag
- builds and unit tests pass
```

投 `-1` 同样必须给出明确理由。

清单来源：Incubator PMC Chair Justin 在 ApacheCon North America 2019 的分享
（https://training.apache.org/topics/ApacheWay/NavigatingASFIncubator/index.html），
详见 `community/releasecheck.md`。
