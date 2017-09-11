[connections服务](http://brpc.baidu.com:8765/connections)可以查看所有的连接。一个典型的页面如下：

server_socket_count: 5

| CreatedTime                | RemoteSide          | SSL  | Protocol  | fd   | BytesIn/s | In/s | BytesOut/s | Out/s | BytesIn/m | In/m | BytesOut/m | Out/m | SocketId |
| -------------------------- | ------------------- | ---- | --------- | ---- | --------- | ---- | ---------- | ----- | --------- | ---- | ---------- | ----- | -------- |
| 2015/09/21-21:32:09.630840 | 172.22.38.217:51379 | No   | http      | 19   | 1300      | 1    | 269        | 1     | 68844     | 53   | 115860     | 53    | 257      |
| 2015/09/21-21:32:09.630857 | 172.22.38.217:51380 | No   | http      | 20   | 1308      | 1    | 5766       | 1     | 68884     | 53   | 129978     | 53    | 258      |
| 2015/09/21-21:32:09.630880 | 172.22.38.217:51381 | No   | http      | 21   | 1292      | 1    | 1447       | 1     | 67672     | 52   | 143414     | 52    | 259      |
| 2015/09/21-21:32:01.324587 | 127.0.0.1:55385     | No   | baidu_std | 15   | 1480      | 20   | 880        | 20    | 88020     | 1192 | 52260      | 1192  | 512      |
| 2015/09/21-21:32:01.325969 | 127.0.0.1:55387     | No   | baidu_std | 17   | 4016      | 40   | 1554       | 40    | 238879    | 2384 | 92660      | 2384  | 1024     |

channel_socket_count: 1

| CreatedTime                | RemoteSide     | SSL  | Protocol  | fd   | BytesIn/s | In/s | BytesOut/s | Out/s | BytesIn/m | In/m | BytesOut/m | Out/m | SocketId |
| -------------------------- | -------------- | ---- | --------- | ---- | --------- | ---- | ---------- | ----- | --------- | ---- | ---------- | ----- | -------- |
| 2015/09/21-21:32:01.325870 | 127.0.0.1:8765 | No   | baidu_std | 16   | 1554      | 40   | 4016       | 40    | 92660     | 2384 | 238879     | 2384  | 0        |

channel_short_socket_count: 0

上述信息分为三段：

- 第一段是server接受(accept)的连接。
- 第二段是server与下游的单连接（使用brpc::Channel建立），fd为-1的是虚拟连接，对应第三段中所有相同RemoteSide的连接。
- 第三段是server与下游的短连接或连接池(pooled connections)，这些连接从属于第二段中的相同RemoteSide的虚拟连接。

表格标题的含义：

- RemoteSide : 远端的ip和端口。
- SSL：是否使用SSL加密，若为Yes的话，一般是HTTPS连接。
- Protocol : 使用的协议，可能为baidu_std hulu_pbrpc sofa_pbrpc memcache http public_pbrpc nova_pbrpc nshead_server等。
- fd : file descriptor（文件描述符），可能为-1。
- BytesIn/s : 上一秒读入的字节数
- In/s : 上一秒读入的消息数（消息是对request和response的统称）
- BytesOut/s : 上一秒写出的字节数
- Out/s : 上一秒写出的消息数
- BytesIn/m: 上一分钟读入的字节数
- In/m: 上一分钟读入的消息数
- BytesOut/m: 上一分钟写出的字节数
- Out/m: 上一分钟写出的消息数
- SocketId ：内部id，用于debug，用户不用关心。



典型截图分别如下所示：

单连接：![img](../images/single_conn.png)

连接池：![img](../images/pooled_conn.png)

短连接：![img](../images/short_conn.png)

