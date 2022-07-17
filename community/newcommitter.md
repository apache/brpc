# 这里记录发展Committer 和 PPMC成员的流程和参考网站信息

## 1. 如何发展committer


### 成为committer的路程
1. 提名者在private@brpc中发起讨论和投票，投票通过即OK （最少3+1， +1 > -1)，投票邮件模版https://community.apache.org/newcommitter.html#committer-vote-template
2. 提名者发送close vote邮件给private@brpc和private@incubator ,标题可以为subject [RESULT][VOTE]，close邮件模版https://community.apache.org/newcommitter.html#close-vote
3. 被提名者填写[CLA](https://www.apache.org/licenses/contributor-agreements.html), 个人贡献者需要下载[ICLA](https://www.apache.org/licenses/icla.pdf)填写个人信息并签名，发送电子版给 secretary@apache.org。个人信息填写项（除签名外）可以使用 PDF 阅读器或浏览器填写，填写后保存进行签名。签名方式支持：
   - 打印 pdf 文件，手写签名后扫描为电子版；
   - 使用支持手写的设备进行电子签名；
   - 使用 `gpg` 进行电子签名，即对填写好个人基本信息的 pdf 文件进行操作（需要提前生成与登记邮箱匹配的公钥/密钥对）：`gpg --armor --detach-sign icla.pdf`；
   - 使用 `DocuSign` 进行签名；
4. 提名者发送announce邮件到dev@brpc.apache.org 


### 如何赋予committer在github上的权限

1. 加为committer
https://whimsy.apache.org/roster/ppmc/brpc

2. 让他设置github id
https://id.apache.org/

3. 让他访问该网址，获得github的权限
https://gitbox.apache.org/setup/


###  apache 官网new committer相关的文档

* https://community.apache.org/newcommitter.html

* https://infra.apache.org/new-committers-guide.html

* https://juejin.cn/post/6844903788982042632

### Suggested steps from secretary@apache.org
Please do these things:

1. Hold the discussion and vote on your private@ list. This avoids any issues related to personnel, which should remain private.
2. If the vote is successful, announce the result to the private@ list with a new email thread with subject [RESULT][VOTE]. This makes it easier for secretary to find the result of the vote in order to request the account at the time of the filing of the ICLA.
3. Only if the candidate accepts committership, announce the new committer on your dev@ list.

Doing these things will make everyone's job easier.



## 2. 如何把committer变成为PPMC

### 流程参考：Apache官网文档
* https://incubator.apache.org/guides/ppmc.html#voting_in_a_new_ppmc_member
* https://community.apache.org/newcommitter.html
* https://incubator.apache.org/guides/ppmc.html#podling_project_management_committee_ppmc

### 实际流程
1. 在private@brpc中发起讨论，如果没有反对，则继续
2. 在private@brpc中发起投票
3. 在private@brpc中发邮件，结束投票，并通知private@incubator.apache.org
4. 在private@brpc中和dev中announce new PPMC
5. 设定他的权限，通过访问https://whimsy.apache.org/roster/ppmc/brpc
6. 帮他订阅private邮件组，参见https://whimsy.apache.org/committers/moderationhelper.cgi

