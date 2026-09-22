# 社区维护工作的 AI Agent Skills

这里放给社区维护者使用的通用 AI agent skill，可供 GitHub Copilot、Claude Code、Codex、Cursor
等支持 skill 的编码助手使用。每个 skill 是一个自包含目录，权威流程仍然是 `community/` 下对应的
`.md` 文档；skill 只负责把流程编排起来、自动化机械步骤、并在不可逆操作前刹车。

| Skill | 用途 | 对应文档 |
|---|---|---|
| `brpc-release` | Apache bRPC 发版全流程 | `community/release_cn.md`、`release_en.md`、`releasecheck.md` |

## 安装指南

Skill 可以安装在项目级目录（仅当前仓库生效），也可以安装在用户级目录（所有项目生效）。推荐使用
符号链接，仓库里的 skill 更新后无需重新复制。以下命令均在 bRPC 仓库根目录执行。

### Claude Code

项目级目录为 `.claude/skills/`：

```bash
mkdir -p .claude/skills
ln -sfn ../../community/skills/brpc-release .claude/skills/brpc-release
```

用户级安装：

```bash
mkdir -p ~/.claude/skills
ln -sfn "$(pwd)/community/skills/brpc-release" ~/.claude/skills/brpc-release
```

### GitHub Copilot

项目级目录为 `.github/skills/`：

```bash
mkdir -p .github/skills
ln -sfn ../../community/skills/brpc-release .github/skills/brpc-release
```

### Codex

项目级目录为 `.agents/skills/`，用户级目录为 `~/.agents/skills/`：

```bash
mkdir -p .agents/skills
ln -sfn ../../community/skills/brpc-release .agents/skills/brpc-release

# 或安装到用户级
mkdir -p ~/.agents/skills
ln -sfn "$(pwd)/community/skills/brpc-release" ~/.agents/skills/brpc-release
```

### Cursor

项目级目录为 `.cursor/skills/`，用户级目录为 `~/.cursor/skills/`：

```bash
mkdir -p .cursor/skills
ln -sfn ../../community/skills/brpc-release .cursor/skills/brpc-release

# 或安装到用户级
mkdir -p ~/.cursor/skills
ln -sfn "$(pwd)/community/skills/brpc-release" ~/.cursor/skills/brpc-release
```

安装后重新打开会话，在 bRPC 仓库中输入「发版 <version>」「验证一下这个发布包」等请求即可触发。
支持斜杠命令的客户端也可以尝试 `/brpc-release`。如果客户端没有识别到 skill，请确认：

1. 安装目录中的 `brpc-release/SKILL.md` 可访问；
2. 符号链接没有失效；
3. 客户端版本支持 Agent Skills，并已重新加载项目或会话；
4. 客户端采用了不同的 skill 目录时，以该客户端当前版本的配置说明为准。

## 依赖

| 依赖 | 用途 | 必需角色/阶段 |
|---|---|---|
| Git | 读取提交范围、创建 release 分支和本地 tag、生成源码包 | RM；阶段 1–4、9 |
| Bash（macOS/Linux） | 运行版本更新、Release Notes、打包和验包脚本 | RM、校验者 |
| GitHub CLI `gh` | 批量查询合并 PR 的标题、作者和编号以生成 PR 维度 Release Notes；首次使用需执行 `gh auth login` | RM；阶段 7 |
| GnuPG `gpg` | RM 交互式签名候选包，以及签名验证 | RM；阶段 2、4；校验者验证签名 |
| Subversion `svn` | 检出 Apache `dist/dev` 工作副本、提交候选包、移动正式发布包 | RM；阶段 5、8 |
| SHA-512 工具 | 生成和校验候选包哈希；脚本依次支持 `sha512sum`、`gsha512sum` 或 `shasum -a 512` | RM；阶段 4；校验者验证哈希 |
| 网络访问 | 访问 GitHub、Apache `dist/dev`、下载候选包和查询 PR | RM、校验者；按需 |

`make_package.sh` 会调用 GPG 签名，必须由 RM 在交互式终端运行；`svn commit`、`svn mv`、推 tag、发邮件和发布 GitHub Release 也必须由 RM 手工执行。

### 执行前检查

每次启动 skill、恢复发版会话或执行依赖外部服务的步骤前，助手都必须先检查所需依赖和登录态；缺失时只说明
安装、登录或授权方式，不能代填密码、token、私钥口令或其他凭据。建议按当前角色和计划步骤执行：

```bash
# 基础依赖：所有脚本均需要
command -v git
command -v bash

# 按 PR 生成 Release Notes：需要 GitHub CLI 已登录且可访问目标仓库
command -v gh
gh auth status --hostname github.com
gh repo view apache/brpc --json nameWithOwner >/dev/null

# RM 打包签名：确认 GnuPG 及对应 Apache ID 的私钥可用
command -v gpg
gpg --list-secret-keys --keyid-format=long "${APACHE_ID}@apache.org"

# RM 上传候选包：确认 SVN 客户端及 Apache SVN 认证可用
command -v svn
svn --username="$APACHE_ID" info https://dist.apache.org/repos/dist/dev/brpc/ >/dev/null

# 打包或验包：确认至少存在一种 SHA-512 工具
command -v sha512sum || command -v gsha512sum || command -v shasum
```

检查结果处理：

- `gh auth status` 失败：提示用户在其终端执行 `gh auth login --hostname github.com`，完成后重新检查；
- `gpg --list-secret-keys` 没有目标私钥：回到 GPG 准备流程，不进入打包；
- `svn info` 要求认证或失败：由 RM 在交互式终端完成认证或确认权限后再继续；
- SHA-512 工具缺失：先安装可用实现，再执行打包或验包；
- 网络、仓库权限或登录态不满足：将该步骤标记为阻塞，汇报当前已完成和可选的其他步骤，等待 RM 选择。

## 使用指南

`brpc-release` 支持两种使用角色。开始新会话时应明确自己的角色，避免校验者误触发发版写操作。

### Release Manager（RM）

RM 负责准备并发布候选版本。下面示例中的 `<version>`、`<previous-version>` 和 `<apache-id>` 都是占位符；使用前请替换为本次版本、上一版本和自己的 Apache ID（例如 `your-id`），不要填写密码、token 或其他凭据。可以这样开始：

```text
使用 brpc-release，以 RM 角色发布 <version>，我的 Apache ID 是 <apache-id>。
先读取已有状态并告诉我下一步；遇到 GPG 签名、push tag、SVN commit/mv、发邮件或发布 GitHub Release时停止，给我命令，等我确认成功后再继续。
```

助手会读取 `~/brpc_release/<version>/STATE.md`；若文件不存在则创建，然后按阶段继续。RM 需要准备：

- 本次版本号、Apache ID 和上一版本号；
- 可访问 Apache SVN 的账号；
- 可用的 GPG 私钥；
- 对 `apache/brpc` 和发布网站所需的操作权限。

职责边界：

| 助手可以执行 | 必须由 RM 执行 |
|---|---|
| 前置检查、创建 release 分支、更新版本文件、整理 SVN 工作副本、验证候选包 | `make_package.sh`（含本地 tag、打包、GPG 签名和校验） |
| 生成 Release Notes 和邮件草稿 | push tag、SVN commit/mv、发送邮件、发布 GitHub Release |

阶段 4 由助手提供命令，RM 在交互式终端一次执行完成：

```bash
export VERSION="<version>"
export APACHE_ID="<apache-id>"
cd ~/brpc
BRPCUSERNAME="$APACHE_ID" community/skills/brpc-release/scripts/make_package.sh "$VERSION"
```

脚本执行期间由 GPG pinentry 向 RM 获取私钥口令。RM 明确回报脚本成功并提供 tag commit id 后，助手会
更新状态文件，汇报已完成和未完成事项，并列出当前可选下一步；RM 选择后才继续，不会自动进入下一阶段。

中断数小时或数天后，可以输入以下指令恢复，不需要从头开始：

```text
使用 brpc-release，以 RM 角色继续发布 <version>。先读取状态文件，只执行尚未完成的步骤。
```

### 候选包校验者（Verifier）

校验者只检查候选包，不创建分支、不改版本号、不打 tag，也不向 SVN 或 GitHub 写入内容。可以直接使用
公开的 `dist/dev` 地址：

```text
使用 brpc-release，以校验者角色验证 Apache bRPC <version> 候选包。只执行只读校验，不修改仓库、不签名、不上传或发布任何内容；最后给我校验结果和投票建议。
```

也可以手工执行：

```bash
export VERSION="<version>"
community/skills/brpc-release/scripts/verify_package.sh \
  "https://dist.apache.org/repos/dist/dev/brpc/${VERSION}/"
```

校验范围包括下载链接、SHA512、GPG 签名、源码包命名、许可证文件、归档内容，以及源码包与 GitHub tag
的逐文件差异。校验者应根据实际结果独立投票，并在回复 `+1` 或 `-1` 时列出检查项；助手不能代发邮件。
校验公开候选包通常不需要 Apache ID，只有访问受认证资源时才由用户在终端交互认证。

校验脚本只会创建临时工作目录，并在退出时自动删除该临时目录及其中下载、解压的校验副本；**不会**删除
传入的本地产物目录、SVN 工作副本、Git tag、分支或任何正式发布文件。

## 脚本可以脱离 AI 助手单独用

`scripts/` 下的脚本都是普通 bash，不依赖任何 AI 助手，手工发版时照样能用：

```bash
export VERSION="<version>"
export PREV_VERSION="<previous-version>"
export APACHE_ID="<apache-id>"

community/skills/brpc-release/scripts/bump_version.sh "$VERSION"
community/skills/brpc-release/scripts/bump_version.sh "$VERSION" --check
# 通过 GitHub PR（而不是 commit）生成草稿；终点使用本地 release tag，无需推送到 GitHub；需要先完成 gh auth login
community/skills/brpc-release/scripts/release_notes.sh "$PREV_VERSION" "$VERSION"
# 以下命令必须由 RM 在交互式终端执行：
BRPCUSERNAME="$APACHE_ID" community/skills/brpc-release/scripts/make_package.sh "$VERSION"
community/skills/brpc-release/scripts/verify_package.sh \
  "https://dist.apache.org/repos/dist/dev/brpc/${VERSION}/"
```

macOS 和 Linux 都验证过（sha512 工具会在 `sha512sum` / `gsha512sum` / `shasum -a 512` 之间自动选择）。

> `verify_package.sh` 是 `community/apache-package-validator.sh` 的可移植替代：
> 后者依赖 wget 和 GNU coreutils，在 macOS 上跑不起来。两者检查项基本一致，
> `verify_package.sh` 另外做了「源码包 vs GitHub tag」的逐文件 diff。

## 改动须知

发版流程变了，`community/release_cn.md`（权威）和这里的 skill 要一起改，否则 skill 会带偏 RM。
版本号硬编码的位置变了，记得同步 `scripts/bump_version.sh` 里的文件表和 `references/checklist.md` 的清单。
