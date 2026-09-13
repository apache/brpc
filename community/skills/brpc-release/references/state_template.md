# bRPC {VERSION} 发版状态

复制到 `~/brpc_release/{VERSION}/STATE.md` 后逐项维护。每完成一步就回写——
发版横跨数周，这个文件是跨会话唯一的事实来源。

```
Release Manager : {RM}
Apache ID       : {apache-id}
版本号          : {VERSION}
发版分支        : release-{MAJOR.MINOR}
起始日期        : {YYYY-MM-DD}
上一个版本       : {PREV_VERSION}
```

## 关键产物

发出去之后再改代价很大，确定一个记一个。

```
Tag commit id   :
Tarball sha512  :
GPG key id      :
dist/dev URL    : https://dist.apache.org/repos/dist/dev/brpc/{VERSION}/
投票邮件链接     :
投票开始时间     :
```

## 当前状态

```
最后更新        : {YYYY-MM-DD HH:MM TZ}
最近完成        :
当前阻塞项      : 无
建议下一步      :
```

每完成一个步骤立即更新本节；“建议下一步”可列多项，并标注是否可并行。用户选择暂不处理的工作应保留在下方进度清单中，不能删除。

## 进度

- [ ] **1. 发布范围** — 待发 PR 已合入，候选 commit 范围已冻结
- [ ] **2. GPG** — 密钥可用，公钥已在 KEYS 中
- [ ] **3. 分支与版本号** — `bump_version.sh --check` 通过，NOTICE 年份已确认
- [ ] **4. 打包** — 本地 tag 已建、tarball/asc/sha512 已生成并自检通过
- [ ] **4b. 推 tag** — `git push origin --tags`（RM 执行）
- [ ] **5. 上传 SVN** — 三个文件已 `svn commit` 到 dist/dev（RM 执行）
- [ ] **6. 验包** — `verify_package.sh` 全绿，与 GitHub tag diff 无差异
- [ ] **7. Release Notes** — 最终 tag 范围的英文完整版和微信公众号中文版已定稿
- [ ] **7a. 发起投票** — 邮件已发到 dev@（RM 执行）
- [ ] **7b. 投票通过** — ≥72h 且 ≥3 张 PMC binding +1，RESULT 邮件已发
- [ ] **8a. 移到 dist/release** — `svn mv`（需 PMC 成员执行）
- [ ] **8b. GitHub Release** — 标题为 `Apache bRPC {VERSION}`
- [ ] **8c. 更新官网下载页** — brpc-website 仓库，中英文都改
- [ ] **8d. ANNOUNCE 邮件** — 发到 dev@ 和 announce@（RM 执行，纯文本）
- [ ] **9. 收尾** — release 分支合回 master，更新 release_schedule.md 实际日期；已向 RM 提示可选的本地产物清理

## 已完成记录

每次完成步骤后追加一行，记录关键产物或 RM 对外操作的确认结果。

| 时间 | 步骤 | 结果/产物 | 执行人 |
|---|---|---|---|
|  |  |  |  |

## 投票记录

| 投票人 | Binding | 票 | 备注 |
|---|---|---|---|
|  |  |  |  |

## 遇到的问题

<!-- 投票被 -1 或验包失败时记在这里，包括怎么修的，下次发版能省事 -->
