[中文版](../cn/server_push.md)

# Server push

What "server push" refers to is: server sends a message to client after occurrence of an event, rather than passively replying the client as in ordinary RPCs. Following two methods are recommended to implement server push in brpc.

## Remote event

Similar to local event, remote event is divided into two steps: registration and notification. The client sends an asynchronous RPC to the server for registration, and puts the event-handling code in the RPC callback. The RPC is also a part of the waiting for the notification, namely the server does not respond directly after receiving the request, instead it does not call done->Run() (to notify the client) until the local event triggers. As we see, the server is also asynchronous. If the connection is broken during the process, the client fails soon and may choose to retry another server or end the RPC. Server should be aware of the disconnection by using Controller.NotifyOnFailed() and delete useless done in-time.

This pattern is similar to [long polling](https://en.wikipedia.org/wiki/Push_technology#Long_polling) in some sense, sounds old but probably still be the most effective method. At first glance "server push" is server visiting client, opposite with ordinary client visiting server. But do you notice that, the response sent from server back to client is also in the opposite direction of "client visiting server"? In order to understand differences between response and push, let's analyze the process with the presumption that "client may receive messages from the server at any time".

* Client has to understand the messages from server anyway, otherwise the messaging is pointless.
* Client also needs to know how to deal with the message. If the client does not have the handling code, the message is still useless.

In other words, the client should "be prepared" to the messages from the server, and the "preparation" often relies on programming contexts that client just faces. Regarding different factors, it's more universal for client to inform server for "I am ready" (registered) first, then the server notifies the client with events later, in which case **the "push" is just a "response"**, with a very long or even infinite timeout.

In some scenarios, the registration can be ignored, such as the [push promise](https://tools.ietf.org/html/rfc7540#section-8.2) in http2, which does not require the web browser(client) to register with the server, because both client and server know that the task between them is to let the client download necessary resources ASAP. Since each resource has a unique URI, the server can directly push resources and URI to the client, which caches the resources to avoid repeated accesses. Similarly, protocols capable of "two-way communication" probably improves efficiencies of pushing in given scenarios such as multimedia streaming or fixed-format key/value pairs etc, rather than implementing universal pushing. The client knows how to handle messages that may be pushed by servers, so extra registrations are not required. The push can still be treated as a "response": to the request that client already and always promised server.

## Restful callback

The client wants a given URL to be called with necessary parameters when the event occurs. In this pattern, server can reply the client directly when it receives the registration request, because the event is not triggered by the end of the RPC. In addition, since the callback is just a URL, which can be stored in databases and message queues, this pattern is very flexible and widely used in business systems.

URL and parameters must contain enough information to make the callback know that which notification corresponds to which request, because the client may care about more than one event at the same time, or an event may be registered more than once due to network jitter or restarting machines etc. If the URL path is fixed, the client should place an unique ID in the registration request and the server should put the ID unchanged in response. Or the client generates an unique path for each registration so that each request is distinguishable from each other naturally. These methods are actually same in essense, just different positions for unique identifiers.

Callbacks should deal with [idempotence](https://en.wikipedia.org/wiki/Idempotence). The server may retry and send more than one notifications after encountering network problems. If the first notification has been successful, follow-up notifications should not have effects. The "remote event" pattern guarantees idempotency at the layer of RPC, which ensures that the done is called once and only once.

In order to avoid missing important notifications, users often need to combine RPC and message queues flexibly. RPC is much better at latency and efficiency than message queue, but due to limited memory, server needs to stop sending RPC after several failed retries and do more important things with the memory. It's better to move the notification task into a persistent message queue at the moment, which is capable of retrying during a very long time. With the idempotence in URL callback, most notifications are sent reliably and in-time.