---
name: brpc-release
description: Drive an Apache bRPC release end to end - confirm the release slot, draft Release Notes, bump version files, cut the tag, build/sign/checksum the source tarball, stage it to Apache SVN dist/dev, verify the candidate, and draft the VOTE/RESULT/ANNOUNCE mails. Use when cutting, resuming, or verifying a bRPC release ("发版 <version>", "release apache brpc", "准备 RC", "写投票邮件", "验证发布包", "check the release candidate").
---

# Apache bRPC 发版

面向 Release Manager（RM）的发版助手。权威流程是 `community/release_cn.md`（英文 `community/release_en.md`），
本 skill 不取代它，只做三件事：**编排顺序、自动化机械步骤、在不可逆操作前刹车**。

## 铁律：这些命令永远不要代 RM 执行

发版有大量对外且不可撤回的动作。遇到下面这些，**把命令打印出来让 RM 自己跑**，然后等待他明确回报成功；
未收到成功确认前不得进入下一步：

| 动作 | 为什么不能代跑 |
|---|---|
| `git push origin --tags` | tag 推到 apache/brpc 后立刻被镜像和下游 CI 抓取，回退要惊动 ASF INFRA |
| `svn commit` / `svn mv` | 写入 Apache 官方分发仓库；`dist/release` 下的内容会同步到全球镜像 |
| `gpg --detach-sign` | 模型不能执行签名，也不能接触私钥口令；必须由 RM 在交互式终端执行 |
| `git config user.name` / `git config user.email` | 发版不要求修改 Git 身份；模型不得读取后重写、覆盖或临时修改用户配置 |
| 发送任何邮件 | `dev@` / `announce@` 是公开存档，发出即无法撤回 |
| 发布 GitHub Release | 对外可见 |

改版本号、打本地 tag、打包、算校验和、生成草稿这些**本地可逆操作**可以直接做。

## 安全条款与强制检查

发版助手必须采用 fail-closed 原则：前置条件不明确或检查失败时立即停止，不猜测、不绕过，也不以
`--force`、临时改脚本或跳过校验的方式继续。

在任何会修改仓库或生成候选包的步骤前，必须逐项检查：

1. 当前目录是预期的 Apache bRPC Git 仓库，且远端指向可识别的 bRPC 仓库；
2. 目标版本符合 `X.Y.Z` 或 `X.Y.Z-rcNN`，目标分支严格为 `release-X.Y`；
3. 当前分支已是目标 release 分支；minor 首发分支必须从 `origin/master` 创建，不能先改版本号再切分支；
4. 工作区没有与本次步骤无关的未提交修改；发现用户修改时不得覆盖、丢弃或自动 stash；
5. tag 不存在，或已存在且明确指向预期 commit；不得静默覆盖或移动 tag；
6. 待发布 commit 来自目标 release 分支，且版本文件检查全部通过；
7. 即将执行的操作不在上方“永远不要代 RM 执行”的列表中。

所有脚本必须使用严格模式、校验外部命令返回值，并将错误输出到 stderr。涉及版本号、分支、tag、
产物路径的参数不得直接拼接成未经验证的命令。日志和草稿不得包含私钥、密码、token、cookie 或其他凭据。
发版沿用仓库已有的 Git 身份，不要求 Apache 邮箱；模型不得执行任何修改本地或全局 Git 用户名、邮箱的命令。

## 依赖与登录态检查

每次启动、恢复会话，以及执行依赖外部服务的步骤前，必须先检查当前步骤所需的命令、网络、账号权限和
登录态。基础检查包括 `git`、`bash`；阶段 2/4 检查 `gpg` 和 `${APACHE_ID}@apache.org` 私钥；阶段 5/8
检查 `svn` 与 Apache SVN 认证；阶段 6 检查网络和 SHA-512 工具；阶段 7 检查 `gh`、GitHub 登录态和对
`apache/brpc` 的访问权限。

检查失败时，将对应步骤标记为阻塞，汇报已完成项和其他可选下一步，等待 RM 选择；不得跳过检查、代填密码、
token 或私钥口令。具体命令和失败处理见 `community/skills/README.md` 的“执行前检查”。

## 每次开工前先做这三件事

1. **读状态文件** `~/brpc_release/<version>/STATE.md`。发版横跨数周，绝大多数会话是接着上次继续，
   不要默认从头开始。文件不存在时，用 `references/state_template.md` 建一个。
2. **确认版本号、RM 和 Apache ID**：查 `community/release_schedule.md` 里的排期表，并向 RM 询问
   Apache ID（仅用户名，不询问密码、token 或其他凭据）。Apache ID 用于以 `--username` 参数检出和操作
   Apache SVN 仓库，并写入状态文件。补丁版本 `${MAJOR}.${MINOR}.${PATCH}` 在已有的
   `release-${MAJOR}.${MINOR}` 分支上发；minor 首发版本 `${MAJOR}.${MINOR}.0` 要从 master 拉新分支。
   **先创建或切换 release 分支，再做任何版本号修改**；不得在 master 或其他工作分支先改版本号。
3. **确认目录约定**（与 `release_cn.md` 一致，脚本也按这个假设）：

   | 路径 | 用途 |
   |---|---|
   | `~/brpc` | release 分支的 clone，打包在这里做 |
   | `~/brpc_release/svn/dev/brpc` | Apache SVN `dist/dev` 工作副本 |
   | `~/brpc_release/<version>/` | 本次发版的草稿、邮件、状态文件 |

每完成一个阶段，**立刻回写 STATE.md**（勾选 + 记下关键产物，如 tag 的 commit id、投票邮件链接）。

## 进度汇报与下一步选择

每完成一个步骤或收到 RM 对手工步骤的成功回报后，助手必须先读取并回写 `STATE.md`，再向 RM 汇报：

1. **本次完成**：列出刚完成的阶段或子步骤，以及产物路径、tag commit id、候选包 URL 等关键结果；
2. **当前进度**：列出已完成项和未完成项；对于被跳过的可选项，明确标记为“跳过”及原因；
3. **前置条件/阻塞项**：说明下一步是否需要 RM 执行 GPG、push tag、SVN commit/mv、发邮件或其他外部操作；
4. **可选下一步**：只列出当前前置条件已满足的步骤，按推荐顺序编号；如果工作可并行，明确标为“可并行”；
5. **等待选择**：停止执行，询问 RM 选择哪一项。除非 RM 明确指示连续执行，否则助手不能自动进入下一阶段。

推荐使用以下固定格式，保证跨会话可恢复：

```text
当前发版进度：<version>
本次完成：
- [x] <刚完成的步骤与关键产物>

已完成：<编号列表>
未完成：<编号列表>
阻塞项：<无 / 需要 RM 执行的动作>

可选下一步：
1. <推荐步骤>
2. <可并行步骤>
3. <其他已满足前置条件的步骤>

请回复序号或直接说明希望继续的工作。
```

RM 可以随时选择任何**前置条件已满足**的未完成项，例如“先生成 Release Notes”“先验证本地产物”或“继续阶段 5”。
助手应先核对依赖关系，满足则执行并更新状态；不满足则解释所缺前置条件，并继续等待 RM 选择。

## 阶段地图

流程分 9 个阶段，跨度约 2 周。详细检查项在 `references/checklist.md`，邮件模板在
`references/mail_templates.md`——**按需读取，不要一次性全读进来**。

### 阶段 1 — 确认发布范围（约 1 周）

和社区确认本次发布范围，合入“计划发布但还没进来”的 PR，并冻结候选 commit 范围。
此时不生成 Release Notes；分支、版本和候选包尚未稳定，提前生成容易遗漏或反复返工。

### 阶段 2 — GPG 准备（非首次发版跳过）

先检查有没有可用的密钥：

```bash
gpg --list-secret-keys --keyid-format=long <apache-id>@apache.org
```

有输出就跳过。没有则照 `references/checklist.md` 的「GPG 首次设置」走完四步：建 4096 位 RSA 密钥
（邮箱必须是 Apache 邮箱）→ 发布公钥到 keyserver → 把 fingerprint 填进 https://id.apache.org →
把公钥追加进 SVN 的 `KEYS` 文件。

### 阶段 3 — 拉发版分支 + 改版本号

顺序不可颠倒：**先拉/切换发版分支，确认当前分支正确，再改版本号**。脚本会强制检查目标 release
分支、仓库身份和工作区状态；任一检查不通过都不会写文件。

```bash
export VERSION="<version>"
export APACHE_ID="<apache-id>"
export RELEASE_BRANCH="release-${VERSION%.*}"

# minor 首发版本才需要从 master 拉新分支
# 如工作区已有未提交修改，应先提交或暂存，不能为了切分支而提前改版本号
git checkout -b "$RELEASE_BRANCH" origin/master
git branch --show-current  # 必须输出 $RELEASE_BRANCH

# 改发布所需的硬编码版本号（幂等，可重复跑）
scripts/bump_version.sh "$VERSION"

# 只校验不修改
scripts/bump_version.sh "$VERSION" --check
```

脚本只更新发布元数据及用户文档；`example/build_with_bazel_module/MODULE.bazel` 中示例模块自身的
`version` 和示例依赖的 bRPC `version` 都不跟随本次发版修改。脚本结尾会扫描其他旧版本号残留。
另外**检查 `NOTICE` 的年份**（`Copyright 2018-<当年>`），年初发版时尤其容易漏。

发版过程中发现问题，一律在 release 分支上改，不要回 master。

### 阶段 4 — 打 tag、打包、签名、校验和

模型不得执行本阶段脚本，只能根据已确认的 Apache ID 和版本号向 RM 提供完整命令：

```bash
cd ~/brpc
BRPCUSERNAME="$APACHE_ID" community/skills/brpc-release/scripts/make_package.sh "$VERSION"
```

由 RM 在交互式终端执行。脚本会一次完成：校验工作区和版本号 → 创建**本地** tag → 生成源码包 →
调用 GPG 并等待 RM 输入私钥口令 → 验证签名 → 生成并验证 `.sha512`。脚本不会修改 Git 用户名或邮箱，
也不会推送 tag。

模型提供命令后必须停止。只有 RM 明确回报脚本成功，并提供输出中的 tag commit id 后，才把阶段 4
标为完成并进入下一阶段；失败或结果不明确时不得继续。`git push origin --tags` 仍由 RM 单独执行。

### 阶段 5 — 上传到 Apache SVN dist/dev

如果状态文件中没有 Apache ID，先向 RM 询问；只记录 ID，**不得询问、记录或代填密码**。首次使用时，
可代 RM 使用 Apache ID 检出 SVN 工作副本（认证需要密码时由 RM 在终端交互输入）：

```bash
export BRPCUSERNAME="$APACHE_ID"
mkdir -p ~/brpc_release/svn/dev/
svn --username="$BRPCUSERNAME" co https://dist.apache.org/repos/dist/dev/brpc/ ~/brpc_release/svn/dev/brpc
```

按 `references/checklist.md` 的「SVN 上传」小节，把三个文件（`.tar.gz` / `.asc` / `.sha512`）
放进 `~/brpc_release/svn/dev/brpc/<version>/`，`svn add` 之后**由 RM 执行 `svn commit`**。
首次发版的 RM 还要先把公钥追加进 `KEYS` 并一起提交。

### 阶段 6 — 验证候选包

```bash
# 验证已上传到 dist/dev 的包（联网下载并全面校验）
scripts/verify_package.sh "https://dist.apache.org/repos/dist/dev/brpc/${VERSION}/"

# 或验证本地产物
scripts/verify_package.sh "${HOME}/brpc_release/${VERSION}"
```

对标仓库里已有的 `community/apache-package-validator.sh`（那个脚本依赖 wget 和 GNU coreutils），
本脚本是可在 macOS 直接运行的独立替代实现，并额外做一项文档里要求但那个脚本没覆盖的
检查：**源码包与 GitHub tag 逐文件 diff**。

自己验过一遍再发投票邮件——PMC 常见的 -1 原因见 `references/checklist.md`。

### 阶段 7 — 准备 Release Notes 并发起投票（至少 72 小时）

候选包验证通过后，以最终 tag 为边界生成 Release Notes 草稿：

```bash
export PREV_VERSION="<previous-version>"
# 使用本地 release tag 作为终点；tag 无需推送到 GitHub。
scripts/release_notes.sh "$PREV_VERSION" "$VERSION" \
  > "${HOME}/brpc_release/${VERSION}/notes-draft.md"
```

脚本以最终 tag 范围内的 GitHub PR 为唯一条目来源：每个合并 PR 只输出一项，并从 GitHub 获取 PR 标题和
作者；输出格式为 `@GitHub-ID #PR-id`，不生成显式 PR URL，GitHub Release 会自动识别链接。不会再按 commit
逐条生成。分类仍是 Feature / Bugfix / Enhancement / Other 的启发式猜测，必须人工核对完整性和准确性；没有
关联 GitHub PR 的 commit 会在草稿注释中单独提示，供人工确认归属。脚本按每批最多 50 个 PR 的 GraphQL
请求加载元数据，避免逐 commit 查询导致长时间等待。按渠道准备：
- GitHub Release：完整 Release Notes，逐项保留 `by @GitHub-ID (#PR-id)`，不附显式链接；分类内按功能主题
  排序。同一 GitHub ID 的不同功能或修复必须作为独立条目保留；
- 微信公众号：与 GitHub Release 保持相同的“新功能 / Bug 修复 / 功能增强”分类和 PR 条目粒度，每项保留
  `by @GitHub-ID (#PR-id)`，不写 PR URL。阅读相关 PR 的说明和必要上下文后，以自然中文润色功能描述；
  不逐字直译、不照搬生硬的提交标题，无法确认含义时不臆测；
- VOTE 邮件的 Release Note：只挑重要 PR，Feature 优先，不粘贴完整列表；
- ANNOUNCE 邮件：只写几条主要变化，不标贡献者和 PR 编号。

从 `references/mail_templates.md` 取 `[VOTE]` 模板，填好版本号、精选 Release Note、
**tag 的 commit id**，草稿写到 `~/brpc_release/<version>/vote-mail.txt`，**由 RM 发到
dev@brpc.apache.org**。邮件发出后，在 [dev@brpc.apache.org 邮件归档](https://lists.apache.org/list.html?dev@brpc.apache.org)
找到该 `[VOTE]` 邮件并复制其永久链接，立即写入 `STATE.md` 的“投票邮件链接”。

需要至少 72 小时 + 3 张 PMC binding +1。够票后从同一归档页面找到 `[RESULT][VOTE]` 邮件的永久链接，
回写到 `STATE.md` 的“已完成记录”，再用 `[RESULT][VOTE]` 模板宣布结果。

投票没过：在 release 分支修问题，回到阶段 3 重新打包，版本号不变但要重打 tag。

### 阶段 8 — 完成发布

依次（每一步都由 RM 执行）：

1. **PMC 成员**把包从 `dist/dev` 移到 `dist/release`（`svn mv`）
2. 在 GitHub 对应 tag 上创建 Release，标题统一为 `Apache bRPC ${VERSION}`
3. 等包同步到 Apache 镜像后，更新 https://brpc.apache.org/docs/downloadbrpc/
   （在 `apache/brpc-website` 仓库，中英文都要改）
   - 签名和哈希链接前缀：`https://downloads.apache.org/brpc/`
   - 代码包链接前缀：`https://dlcdn.apache.org/brpc/`
4. 用 `[ANNOUNCE]` 模板发信到 `dev@brpc.apache.org` 和 `announce@apache.org`
   —— 必须用**个人 apache 邮箱**、必须**纯文本格式**，announce@ 要人工审核约一天

### 阶段 9 — 收尾

- 把 release 分支合回 master
- 更新 `community/release_schedule.md` 里本次发版的实际日期
- 微信公众号等外部渠道（可选）

完成上述事项并确认正式发布可下载后，助手应汇报可清理的本地发版产物，并由 RM 自行决定是否执行。
**不得自动删除**，也不得删除 Git 分支、tag、SVN 工作副本或 `STATE.md`。建议 RM 在确认无需保留本地
归档、且 `STATE.md` 已记录 tag commit id、SHA512、投票链接等关键事实后，执行：

```bash
export VERSION="<version>"

# 仅删除本次生成的本地源码包、签名、哈希和草稿；保留 STATE.md 作为发版记录。
rm -f "${HOME}/brpc_release/${VERSION}"/apache-brpc-"${VERSION}"-src.tar.gz{,.asc,.sha512}
rm -f "${HOME}/brpc_release/${VERSION}"/notes-draft.md \
      "${HOME}/brpc_release/${VERSION}"/wechat-draft.md \
      "${HOME}/brpc_release/${VERSION}"/{vote-mail,result-mail,announce-mail}.txt

# 若确认不再需要整个本次草稿目录，先检查内容，再由 RM 手工删除。
ls -la "${HOME}/brpc_release/${VERSION}"
# rm -rf "${HOME}/brpc_release/${VERSION}"
```

SVN 工作副本 `~/brpc_release/svn/dev/brpc` 通常应保留供下次发版复用；若 RM 明确要清理，先确认不存在
未提交修改（`svn status`），再由 RM 手工删除该工作副本。

## 常见坑

- **改了版本号却漏了文件**：一定跑 `bump_version.sh --check`，别手改。
- **在 master 上打 tag**：所有发版操作都在 `release-<major.minor>` 分支。
- **sha512 文件里带路径**：`sha512sum` 必须在 tarball 所在目录执行，否则校验方 `--check` 会失败。
  脚本已处理。
- **macOS 没有 `sha512sum`**：脚本会自动回退到 `gsha512sum` 或 `shasum -a 512`。
- **投票邮件写错 commit id**：填 tag 指向的 commit，不是分支 HEAD。
- **只回 `+1` 不附检查项**：PMC 投票必须列出检查了哪些项。
