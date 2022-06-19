[English version](../en/server_push.md)

# Server push

server push指的是server端发生某事件后立刻向client端发送消息，而不是像通常的RPC访问中那样被动地回复client。brpc中推荐以如下两种方式实现推送。

# 远程事件

和本地事件类似，分为两步：注册和通知。client发送一个代表**事件注册**的异步RPC至server，处理事件的代码写在对应的RPC回调中。此RPC同时也在等待通知，server收到请求后不直接回复，而是等到对应的本地事件触发时才调用done->Run()**通知**client发生了事件。可以看到server也是异步的。这个过程中如果连接断开，client端的RPC一般会很快失败，client可选择重试或结束。server端应通过Controller.NotifyOnCancel()及时获知连接断开的消息，并删除无用的done。

这个模式在原理上类似[long polling](https://en.wikipedia.org/wiki/Push_technology#Long_polling)，听上去挺古老，但可能仍是最有效的推送方式。“server push“乍看是server访问client，与常见的client访问server方向相反，挺特殊的，但server发回client的response不也和“client访问server”方向相反么？为了理解response和push的区别，我们假定“client随时可能收到server推来的消息“，并推敲其中的细节：

* client首先得认识server发来的消息，否则是鸡同鸭讲。
* client还得知道怎么应对server发来的消息，如果client上没有对应的处理代码，仍然没用。

换句话说，client得对server消息“有准备”，这种“准备”还往往依赖client当时的上下文。综合来看，由client告知server“我准备好了”(注册)，之后server再通知client是更普适的模式，**这个模式中的"push"就是"response"**，一个超时很长或无限长RPC的response。

在一些非常明确的场景中，注册可以被简略，如http2中的[push promise](https://tools.ietf.org/html/rfc7540#section-8.2)并不需要浏览器(client)向server注册，因为client和server都知道它们之间的任务就是让client尽快地下载必要的资源。由于每个资源有唯一的URI，server可以直接向client推送资源及其URI，client看到这些资源时会缓存下来避免下次重复访问。类似的，一些协议提供的"双向通信"也是在限定的场景中提高推送效率，而不是实现通用的推送，比如多媒体流，格式固定的key/value对等。client默认能处理server所有可能推送的消息，以至于不需要额外的注册。但推送仍可被视作"response"：client和server早已约定好的请求的response。

# Restful回调

client希望在事件发生时调用一个给定的URL，并附上必要的参数。在这个模式中，server在收到client注册请求时可以直接回复，因为事件不由注册用RPC的结束触发。由于回调只是一个URL，可以存放于数据库或经消息队列流转，这个模式灵活性很高，在业务系统中使用广泛。

URL和参数中必须有足够的信息使回调知道这次调用对应某次注册，因为client未必一次只关心一个事件，即使一个事件也可能由于网络抖动、机器重启等因素注册多次。如果回调是固定路径，client应在注册请求中置入一个唯一ID，在回调时带回。或者client为每次注册生成唯一路径，自然也可以区分。本质上这两种形式是一样的，只是唯一标志符出现的位置不同。

回调应处理[幂等问题](https://en.wikipedia.org/wiki/Idempotence)，server为了确保不漏通知，在网络出现问题时往往会多次重试，如果第一次的通知已经成功了，后续的通知就应该不产生效果。上节“远程事件”模式中的幂等性由RPC代劳，它会确保done只被调用一次。

为了避免重要的通知被漏掉，用户往往还需灵活组合RPC和消息队列。RPC的时效性和开销都明显好于消息队列，但由于内存有限，在重试过一些次数后仍然失败的话，server就得把这部分内存空出来去做其他事情了。这时把通知放到消息队列中，利用其持久化能力做较长时间的重试直至成功，辅以回调的幂等性，就能使绝大部分通知既及时又不会被漏掉。