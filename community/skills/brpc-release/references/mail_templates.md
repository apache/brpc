# 发版邮件模板

三封邮件，按顺序发。全部用**个人 Apache 邮箱**、**纯文本格式**（Gmail 里选「纯文本模式」）。

Apache 邮箱配置参考 https://shenyu.apache.org/zh/community/use-apache-email ，
注意 SMTP 服务器要填 `mail-relay.apache.org`。

占位符：`{VERSION}` 版本号、`{COMMIT}` tag 指向的 commit id、`{RM}` 发布人名字。

---

## 1. 投票邮件 → dev@brpc.apache.org

**标题**

```
[VOTE] Release Apache bRPC {VERSION}
```

**正文**

```
Hi Apache bRPC Community,

This is a call for vote to release Apache bRPC version {VERSION}

[Release Note]

Features:

- {feature description} by @github-id (#1234)
- {feature description} by @github-id (#1235)

Bugfixes:

- {bugfix description} by @github-id (#1236)
- {bugfix description} by @github-id (#1237)

Enhancements:

- {enhancement description} by @github-id (#1238)

The release candidates:
https://dist.apache.org/repos/dist/dev/brpc/{VERSION}/

Git tag for the release:
https://github.com/apache/brpc/releases/tag/{VERSION}

Release Commit ID:
https://github.com/apache/brpc/commit/{COMMIT}

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
[ ] Source code distributions have correct names matching the current release.
[ ] LICENSE and NOTICE files are correct for each brpc repo.
[ ] All files have license headers if necessary.
[ ] No compiled archives bundled in source archive.

Regards,
{RM}
```

> `[Release Note]` 只挑重要 PR，Feature 优先，不要粘贴完整 Release Notes；完整列表留给 GitHub Release。
>
> `Release Commit ID` 填 **tag 指向的 commit**，不是分支 HEAD。
> 取值：`git rev-parse {VERSION}^{commit}`

投票邮件发出后，在 [dev@brpc.apache.org 邮件归档](https://lists.apache.org/list.html?dev@brpc.apache.org) 找到
对应 `[VOTE]` 邮件并复制其永久链接，填写到 `STATE.md` 的“投票邮件链接”。投票需开放至少 72 小时，
且收集到 **3 张 PMC binding +1** 才能进入下一步。

---

## 2. 投票回复模板

投票者应根据实际检查结果回复投票邮件。PMC 成员使用 `+1 (binding)`，非 PMC 成员使用
`+1 (non-binding)`；不得在未完成检查时勾选通过项。

**标题**

保持邮件客户端的回复标题，例如：

```
Re: [VOTE] Release Apache bRPC {VERSION}
```

**正文（PMC 成员）**

```text
+1 (binding)

I have checked:

[x] Download links are valid.
[x] Checksums and PGP signatures are valid.
[x] Source code distributions have correct names matching the current release.
[x] LICENSE and NOTICE files are correct for each brpc repo.
[x] All files have license headers if necessary.
[x] No compiled archives bundled in source archive.

Best regards,
{VOTER_NAME}
```

**正文（非 PMC 成员）**

将首行替换为：

```text
+1 (non-binding)
```

其余检查清单和签名保持不变。

> 按实际检查情况填写。若有未完成项，保留 `[ ]` 并说明原因；发现阻塞性问题时投 `-1` 并附可复现信息。
> 校验命令见 `verify_package.sh`，其输出可帮助填写上述清单。

---

## 3. 结果邮件 → dev@brpc.apache.org

**标题**

```
[RESULT] [VOTE] Release Apache bRPC {VERSION}
```

**正文**

```
Hi all,

The vote to release Apache bRPC {VERSION} has passed.

The vote PASSED with 3 binding +1, 3 non binding +1 and no -1 votes:

Binding votes:
- xxx
- yyy
- zzz

Non-binding votes:
- aaa
- bbb
- ccc

Vote thread: {VOTE_THREAD_URL}

Thank you to all the above members to help us to verify and vote for
the {VERSION} release. I will process to publish the release and send ANNOUNCE.

Regards,
{RM}
```

> 票数要按实际统计填写。`Vote thread` 填 `[VOTE]` 邮件的永久链接：在
> [dev@brpc.apache.org 邮件归档](https://lists.apache.org/list.html?dev@brpc.apache.org) 中找到该邮件后复制链接。
> `[RESULT][VOTE]` 发出后，也应复制其永久链接并写入 `STATE.md` 的“已完成记录”。

---

## 4. 发布公告 → dev@brpc.apache.org + announce@apache.org

在包已经从 `dist/dev` 移到 `dist/release`、GitHub Release 已发布之后再发。

**标题**

```
[ANNOUNCE] Apache bRPC {VERSION} released
```

**正文**

```
Hi all,

The Apache bRPC community is glad to announce the new release
of Apache bRPC {VERSION}.

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
https://github.com/apache/brpc/releases/tag/{VERSION}

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

> `Brief notes of this release` 只列本次的**主要**变更，不要贴完整 Release Notes，
> 也不用标注贡献人和 PR 编号。建议先翻一下 lists.apache.org 上之前的 ANNOUNCE 邮件对齐风格。
>
> `announce@apache.org` 需人工审核，发出后耐心等，一般一天内通过。

---

## Release Notes 模板

阶段 7 在候选包验证通过后，需分别准备 GitHub Release 英文版和微信公众号中文版。两者均以**合并 PR
为单位**：每个 PR 仅一项，不按该 PR 的多个 commit 分拆。`scripts/release_notes.sh` 生成的内容仅作为
待人工打磨的初稿。

### GitHub Release（英文）

参考 Apache bRPC 1.16.0 的结构：用一段简洁摘要概括本次版本主题，再按 `Features`、`Bugfixes`、
`Enhancements` 分类列出完整变更，最后致谢全部贡献者。每项使用 `@GitHub-ID #PR-id`；不要写显式 PR
URL，GitHub 会自动将 `#PR-id` 识别为链接。

```markdown
Apache bRPC {VERSION} is a {feature/maintenance} release that includes {one-sentence summary of the most important improvements}. This release {user-visible value summary}.

## Features

- {feature description} by @github-id (#1234)
- {feature description} by @github-id (#1235)

## Bugfixes

- {bugfix description} by @github-id (#1236)
- {bugfix description} by @github-id (#1237)

## Enhancements

- {enhancement description} by @github-id (#1238)

## Other

- {documentation, test, build, or maintenance description} by @github-id (#1239)
```

写作要求：

- 摘要面向用户说明版本价值，避免仅罗列内部实现；
- 变更描述使用动词开头的简洁英文，保留协议名、API、类名和配置项等必要技术术语；
- 完整列出所有应发布的合并 PR；文档、测试、CI、构建和维护类变更归入 `Other`；
- **变更条目**按 `Features`、`Bugfixes`、`Enhancements`、`Other` 分类；同一分类内按功能主题排序，并且仅合并重复描述的同一 PR；
  同一 GitHub ID 贡献了不同功能或修复时，必须保留为多条独立条目，不能按贡献者去重；
- Contributors 仅致谢所有贡献者，不重复罗列 GitHub ID；
- 无法确认 PR 含义时标记待确认，不得臆测。

### 微信公众号（中文）

微信公众号稿沿用 GitHub Release 的分类和条目粒度：先用一段发布摘要说明本次版本主题，再按“新功能”、
“Bug 修复”、“功能增强”和“其他”列出变更，最后统一致谢。每项保留 `by @GitHub-ID (#PR-id)`，不写显式 PR URL；
公众号不需要逐条展开链接。参考结构：

```markdown
# Apache bRPC {VERSION} 版本已发布

很高兴地通知大家，Apache bRPC {VERSION} 版本已发布，这是一次{版本定位}的版本，在{改进方向一}、
{改进方向二}与{改进方向三}方面均有显著改进。本次发布{总体价值总结}。

Apache bRPC 官网：https://brpc.apache.org
下载链接：https://brpc.apache.org/zh/download/
GitHub Release Tag：https://github.com/apache/brpc/releases/tag/{VERSION}

## {VERSION} 版本变更

### 新功能

- {新功能描述} by @github-id (#1234)
- {新功能描述} by @github-id (#1235)

### Bug 修复

- {修复描述} by @github-id (#1236)
- {修复描述} by @github-id (#1237)

### 功能增强

- {增强描述} by @github-id (#1238)

### 其他

- {文档、测试、构建或维护类描述} by @github-id (#1239)

感谢所有关心和为 Apache bRPC 做出贡献的开发者！
```

写作要求：

- 先阅读对应 PR 的标题、说明、讨论及必要代码上下文，理解变更动机、用户价值和影响范围后再写；
- 保持与 GitHub Release 一致的四类分类和 PR 条目粒度；同一 GitHub ID 的不同功能或修复仍单独成条；
- 中文条目以自然、准确的中文重新组织表达，不逐字直译、不照搬生硬的提交标题；
- 每项保留 `by @GitHub-ID (#PR-id)`，不写 PR URL；
- 保留协议名、API、类名和配置项等必要技术术语；无法确认含义时标记待确认，不得臆测。
