---
title: Overview
sidebar: brpc_sidebar
permalink: index.html
---

## 背景
我们的服务大都通过[TCP/IP协议](http://en.wikipedia.org/wiki/Internet_protocol_suite)相互访问，但TCP/IP只是往远端发送了一段二进制数据，相比日常需求还有很多问题需要抽象：

- 数据以什么格式传输？不同机器间，网络间可能是不同的字节序，把C++
  struct直接作为数据传输显然是不合适的；随着业务变化，数据字段往往要增加或删减，怎么兼容前后不同版本的格式？
- 一个TCP连接只能传输一份数据么？能复用吗？
- 如何连接集群？如何从集群中选出一台机器发送数据？
- 连接断开时应该干什么？万一server不发送回复怎么办？
- ...

[RPC](http://en.wikipedia.org/wiki/Remote_procedure_call)可以解决这些问题，它把网络交互类比为“client访问server上的函数”：client向server发送request后开始等待，直到server收到、处理、回复client后，client又再度恢复并根据response做出反应。

![img](http://wiki.baidu.com/download/thumbnails/71337070/Client-server.jpg?version=1&modificationDate=1427941174000&api=v2)

我们来看看上面的一些问题是如何解决的：

- RPC需要序列化，目前公司内定为protobuf。用户填写protobuf::Message类型的request，RPC结束后，从同为protobuf::Message类型的response中取出结果。protobuf有较好的前后兼容性，方便业务调整字段。我们强烈建议使用protobuf描述业务数据。
- 用户不需要关心连接是如何建立的，但用户可以选择不同的连接方式：短连接，连接池，单连接。
- 用户一般通过名字服务发现一个集群中的所有机器，常用的有[BNS](http://devops.baidu.com/new/bns/index.md)，以前有[galileo](http://wiki.babel.baidu.com/twiki/bin/view/Com/Gm/ResourceRouterHome)。用户可以指定负载均衡算法，让RPC每次选出一台机器发送请求。
- 连接断开时按用户的配置进行重试。如果server没有在给定时间内返回response，那么client会返回超时错误。



RPC框架作为最基本的网络通讯组件，需要具备优秀的稳定性、可维护性和扩展性。在开发本框架前，公司内外主要有如下RPC实现：

- UB

  ：com组(INF前身)在08年开发的RPC框架，目前仍广泛使用，经历了大量的线上考验，稳定性优秀。UB的主要问题是依赖众多、扩展性不佳，以及缺乏调试工具。用户往往只需要UB中的一部分功能而不得不依赖大量模块，由于百度的代码库不是单根，这带来了复杂的依赖打平问题。UB每个请求都要独占一个连接，在大规模服务中每台机器都需要保持大量的连接，限制了其使用场景（比如INF自己的分布式系统都不用UB）。UB只支持nshead+mcpack协议，也没有太多考虑扩展接口，所以增加新协议和新功能往往要调整大段代码，在实践中大部分人都“知难而退”了。UB也缺乏调试和运维接口，服务的运行状态对用户基本是黑盒，只能靠低效的打日志来追踪问题，产品线服务出现问题时总是要拉上UB的维护同学一起排查，效率很低。UB有多个变种：

  - ubrpc：INF在10年基于UB开发的RPC框架，用idl文件描述数据的schema，而不是在代码中手动打包。这个RPC有被使用，但不广泛。
  - [nova-pbrpc](http://websvn.work.baidu.com/repos/app_ecom_nova/list/trunk/public/pb-rpc)：网盟在12年基于UB开发的RPC框架，用protobuf代替mcpack作为序列化方法，协议是nshead
    + user's protobuf。
  - [public/pbrpc](http://websvn.work.baidu.com/repos/public/list/trunk/pbrpc)：INF在13年初基于UB开发的RPC框架，用protobuf代替mcpack作为序列化方法，但协议与nova-pbrpc不同，大致是nshead
    + meta protobuf。meta protobuf中有个string字段包含user's
protobuf。由于用户数据要序列化两次，这个RPC的性能很差，基本没有被推广开。

- [hulu-pbrpc](https://svn.baidu.com/public/trunk/hulu/pbrpc/)：INF在13年基于saber(kylin变种)和protobuf实现的RPC框架。hulu尝试区分了机制(pbrpc)和策略(huluwa)，减轻了依赖问题。但在实现上有较多问题：未封装的引用计数，混乱的生命周期，充斥的多线程问题(race
  conditions & ABA
problems)，运行质量很不可靠，比如hulu的短链接从来没有能正常运行过。其编程接口强依赖saber，很多功能的划分不够清晰，比如增加新协议就需要同时修改saber和pbrpc，扩展新功能仍然很困难。hulu增加了http协议的内置服务，可以提供一些简单的内部运行状态，但对于排查问题还比较简陋。hulu支持和一个server只保持一个连接，相比UB可以节省连接数，但由于多线程实现粗糙，读写不够并发，hulu的性能反而不如UB。

- [sofa-pbrpc](https://svn.baidu.com/public/trunk/sofa-pbrpc/)：PS在13年基于boost::asio和protobuf实现的RPC框架，这个库代码工整，接口清晰，支持同步和异步，有非HTTP协议的调试接口（最新版也支持HTTP了）。但sofa-pbrpc也有产品线自研框架的鲜明特点：不支持公司内的其他协议，对名字服务、负载均衡、服务认证、连接方式等多样化的需求的抽象不够一般化。sofa-pbrpc还对外发布了开源版本。

- thrift：facebook最早在07年开发的RPC框架，后转为[apache的开源项目](https://thrift.apache.org/)。包含独特的序列化格式和IDL，支持很多编程语言。thrift让网络上的开发者能完成基本的RPC通讯，但也仅限于此了。thrift的代码看似分层很清楚，client、server选择很多，但没有一个足够通用，每个server实现都只能解决很小一块场景，每个client都线程不安全。实际使用中非常麻烦。thrift的代码质量也比较差，接口和生成的代码都比较乱。

基于这些观察，我们感到有必要开发一套新的RPC框架，即**baidu-rpc**，以方便各类用户开发、调试、运维网络服务，满足产品线之间对平台化、接口化日益高涨的需求，能适应核数越来越多的硬件架构，性能远好于其他实现。请注意，本文档主要指C++版本的baidu-rpc。目前的[pbrpc4j](http://websvn.work.baidu.com/repos/public/list/trunk/javalib/pbrpc4j/?revision=HEAD&bypassEmpty=true)基于netty改造，功能较少，我们可能会也可能不会把它作为baidu-rpc的java版。python有[client实现](https://svn.baidu.com/public/tags/pythonlib/pbrpc/pbrpc_1-0-2-5_PD_BL/)和[server实现](http://wiki.baidu.com/display/RPC/Python+Server)。php目前只支持client。

## 目标

- 提供稳定的RPC框架。
- 适用各类业务场景，提供优秀的延时，吞吐，并发度，具备优秀的多核扩展性。
- 接口易懂，用户体验佳。有完备的调试和运维接口（HTTP）。
- 开发习惯好（注释、日志、changelog），有全面的单元、模块、集成测试，能作为编码示例。

## 假设

- 用户对访问延时，吞吐，并发度，分库等指标及功能有广泛而多样的需求。
- 机器以Intel x86_64为主。
- 下游服务规模可能到数万个节点，机器和网络状况多样化。

## 应用场景

几乎所有的网络交互。

RPC不是万能的抽象，否则我们也不需要TCP/IP这一层了。但是在我们绝大部分的网络交互中，RPC既能解决问题，又能隔离更底层的网络问题。对于RPC常见的质疑有：

- 我的数据非常大，用protobuf序列化太慢了。首先这可能是个伪命题，你得用[profiler](http://wiki.baidu.com/display/RPC/cpu+profiler)证明慢了才是真的慢，其次一些协议支持附件，你可以在传递protobuf请求时附带二进制数据。
- 我传输的是流数据，RPC表达不了。baidu-rpc支持[Streaming
  RPC](http://wiki.baidu.com/pages/viewpage.action?pageId=152229270)，这可以表达。
- 我的场景不需要回复。简单推理可知，你的场景中请求可丢可不丢，可处理也可不处理，因为client总是无法感知，你真的确认这是OK的？即使场景真的不需要，我们仍然建议用最小的结构体回复，因为这不大会是瓶颈，并且在出问题时让你有一些线索，否则真的是盲人摸象。

## 优势

### 使用更简单的接口

我们只有三个用户类：[Server](https://svn.baidu.com/public/trunk/baidu-rpc/src/baidu/rpc/server.h)，[Channel](https://svn.baidu.com/public/trunk/baidu-rpc/src/baidu/rpc/channel.h?revision=HEAD)，[Controller](https://svn.baidu.com/public/trunk/baidu-rpc/src/baidu/rpc/controller.h?revision=HEAD)，分别对应server端，client端，和调整参数集合。你不需要推敲诸如“Client怎么初始化”，“XXXManager有什么用”，“Context和Controller的关系是什么“之类的问题，你要做的很简单：

- 建服务就#include
  <[baidu/rpc/server.h](https://svn.baidu.com/public/trunk/baidu-rpc/src/baidu/rpc/server.h)>，并按照注释或例子使用Server对象。
- 访问服务就#include
  <[baidu/rpc/channel.h](https://svn.baidu.com/public/trunk/baidu-rpc/src/baidu/rpc/channel.h)>，并按照注释或例子使用Channel对象。
- 想控制一次RPC访问的参数，就看看[baidu/rpc/controller.h](https://svn.baidu.com/public/trunk/baidu-rpc/src/baidu/rpc/controller.h)中到底有些什么。请注意，这个类是Server和Channel共用的，其中分成了三段，分别标记为Client-side,
  Server-side和Both-side methods。

我们尝试让事情变得更加简单，以名字服务为例，在其他RPC实现中，你也许需要复制一长段晦涩的代码才可使用，而在baidu-rpc中访问BNS可以这么写"bns://node-name"，
本地文件列表可以这么写"file:///home/work/server.list"，相信不用我解释，你也能明白这些代表什么，这个字串可以放在配置文件中，方便地载入并使用。

### 访问各种服务，被各种服务访问

baidu-rpc能访问百度内所有基于protobuf的RPC server实现，能[访问ub
server](http://wiki.baidu.com/pages/viewpage.action?pageId=213828700)(idl/mcpack/compack)

baidu-rpc能被百度内所有基于protobuf的RPC
client访问，能被HTTP+json访问，通过一些额外代码可以被UB访问。

### 使服务更加可靠

我们的开发机制能持续地保证高质量：

- 分离机制(mechanism)和策略(policy)：baidu-rpc把可扩展的部分都抽象为了策略，并放在了[单独的目录](https://svn.baidu.com/public/trunk/baidu-rpc/src/baidu/rpc/policy/)，具体的协议支持，压缩算法，名字服务，负载均衡都是策略，如果你想二次开发，可以很容易找到模板并开始上手。就像在算法问题中O(M*N)问题变为了O(M+N)那样，这有效地降低了baidu-rpc整体的复杂度，使得我们可以把精力集中到最核心的代码上。
- 完整的单元和集成测试：baidu-rpc有完整的单元测试，集成测试，系统测试和性能测试，在我们每次check
  in代码后都会运行，这确保我们在开发阶段可以及时规避问题。
- 全面的调试和监控手段：baidu-rpc特别重视开发和运维，你可以使用浏览器或curl[查询服务器内部状态](http://wiki.baidu.com/display/RPC/Builtin+Services)，你也可以用pprof[分析在线服务的性能](http://wiki.baidu.com/display/RPC/cpu+profiler)。你可以[用bvar计数](http://wiki.baidu.com/display/RPC/bvar)，相比缓慢的ubmonitor几乎没有性能损耗，同时bvar能以/vars访问，这改变了我们监控在线服务的方式
- 新基础库：baidu-rpc依赖的[C++
  base](http://wiki.baidu.com/pages/viewpage.action?pageId=38035224)改造自chromium，是百度的新基础库。

### 获得更好的延时和吞吐

虽然大部分RPC实现都号称“高性能”，但仅仅是在测试中有不错的数字，在广泛的场景中确保高性能仍是困难的，为了做到这一点，baidu-rpc有比其他RPC实现更深的技术考量，会确保以最快的速度把请求分发到合适的位置，具体来说：

- **对不同客户端请求的读取和解析是完全并发的，用户也不用区分”IO线程“和”处理线程"**。听上去简单，但几乎没有做到的。大部分实现会区分“IO线程”和“处理线程”，并把[fd](http://en.wikipedia.org/wiki/File_descriptor)（对应一个客户端）散列到IO线程中去。这听上不错，实则很糟糕。首先当一个IO线程在读取其中的fd时，同一个线程中的fd都无法得到处理。有些实现(ubaserver)不扣除关闭的fd，那么在一段时间后，不同IO线程中的fd将很不均匀，一个fd的影响范围将更大。当一些解析变慢时，比如特别大的protobuf
  message，同一个IO线程中的其他fd都遭殃了。虽然不同IO线程间的fd是并发的，但由于你不太可能开太多IO线程（这类线程的事情很少，大部分时候都是闲着的），10个就比较多了，所以一个fd能影响到的”其他fd“仍有相当大的比例（10个即10%，在线检索可是要求99.99%的）。将来的RPC框架将广泛地用于多租户(multi-tenacy)环境，一个数据复杂的业务如果严重干扰到其他业务的SLA，将是无法接受的。在baidu-rpc中，对不同fd的读取是完全并发的，对同一个fd中不同消息的解析也是并发的**。**比如一个特别大的protobuf
message在解析时不会影响同一个客户端的其他消息，更不用提其他客户端的消息了。
- **只要cpu和工作线程有剩余，新请求会在O(1)时间内得到处理**。由于大部分业务请求都要花费很多毫秒，即使只有一个任务由于调度的原因而得不到及时的处理，都是无法接受的。以hulu为例，有两种任务分发模式：一种是分发到固定线程，另一种是通过condition。前者没有同步开销，对于大流量效果更好，但无法保证本要求。后者有高强度的同步开销，吞吐不会太高，但可以保证本要求。这让用户很纠结。在baidu-rpc中总是能保证本要求，并且性能好于hulu。
- **对同一fd和不同fd的写出是高度并发的**。当多个线程都要对一个fd写出时（常见于单连接），第一个线程会直接在原线程写出，其他线程会以[wait-free](http://en.wikipedia.org/wiki/Non-blocking_algorithm#Wait-freedom)的方式托付自己的写请求，多个线程在高度竞争下仍可以在1秒内对同一个fd写入500万个16字节的消息，这很可能是目前最高性能的同fd写出实现。对不同fd的写出总是并发的。值得一提的是，我们在未来可能会用类似的技术实现往磁盘写的Channel，以满足一些服务器对超高频率日志的需求。
- **服务器线程数自动调节**。传统的服务器需要根据下游延时的调整自身的线程数，否则吞吐可能会受影响，一般是通过配置。在baidu-rpc中，每个请求均运行在新建立的bthread中，请求结束后线程就结束了，所以天然会根据负载自动调节线程数。

baidu-rpc背后的技术和知识请阅读[深入RPC](http://wiki.baidu.com/display/RPC/RPC+in+Depth)，和其他RPC实现的性能对比请见[Benchmark](http://wiki.baidu.com/pages/viewpage.action?pageId=48480483)。

### 获得独有的功能

如果你有任何需求，都可以向我们提：这并不意味着我们会全盘接受，我们也许会提议另一种解决方案，或指出一些理解的误区，不过这终究可以使想法得到更多打磨，产生新的火花。灵活的内部结构使我们可以更快地添加新功能，有一些功能是其他RPC实现很难拥有的，举例来说：

- 编写和访问HTTP server：你写的任何protobuf
  server都可以在移动端用HTTP+json访问，你也可以写纯的HTTP/HTTPS server或者访问其他HTTP server。
- backup request: 又称tied requests，是一种降低请求延时的技术。
- [Locality-aware load balancer](http://wiki.baidu.com/display/RPC/Locality-aware+load+balancing) :
  这种负载均衡算法会优先选择最近的服务器，即同机 > 同rack > 同机房 >
跨机房，并在近节点发生故障时快速收敛，当节点大量混部时，这可以有效地降低延时，并规避网络抖动。
- [内置服务](http://wiki.baidu.com/display/RPC/Builtin+Services) :
  彻底改变开发和调试体验的强大工具！配合[bvar](http://wiki.baidu.com/display/RPC/bvar)更贴心。
- [组合访问](http://wiki.baidu.com/pages/viewpage.action?pageId=213828709)：并发访问，分库分环，从没有这么简单过。

{% include links.html %}
