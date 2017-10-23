Sometimes in order to ensure availability, we need to visit two services at the same time and get the result coming back first. There are several ways to achieve this in brpc:

# When backend servers can be hung in a naming service

Channel opens backup request. Channel sends the request to one of the servers and when the response is not returned after ChannelOptions.backup_request_ms ms, it sends to another server, taking the response that coming back first. After backup_request_ms is set up properly, in most of times only one request should be sent, causing no extra pressure to back-end services.

Read [example/backup_request_c++](https://github.com/brpc/brpc/blob/master/example/backup_request_c++) as example code. In this example, client sends backup request after 2ms and server sleeps for 20ms on purpose when the number of requests is even to trigger backup request.

After running, the log in client and server is as following. "Index" is the number of request. After the server receives the first request, it will sleep for 20ms on purpose. Then the client sends the request with the same index. The final delay is not affected by the intentional sleep.

![img](../images/backup_request_1.png)

![img](../images/backup_request_2.png)

/rpcz also shows that the client triggers backup request after 2ms and sends the second request.

![img](../images/backup_request_3.png)

# Choose proper backup_request_ms

