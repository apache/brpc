# Multi-protocol support in server side

brpc server supports all protocols in the same port, and it makes deployment and maintenance more convenient in most of the time. Since the format of different protocols is very different, it is hard to support all protocols in the same port unambiguously. In consider of decoupling and extensibility, it is also hard to build a multiplexer for all protocols. Thus our way is to classify all protocols into three categories and try one by one:

- First-class protocol: Special characters are marked in front of the protocol data, for example, the data of protocol [baidu_std](baidu_std.md) and hulu_pbrpc begins with 'PRPC' and 'HULU' respectively. Parser just check first four characters to know whether the protocol is matched. This class of protocol is checked first and all these protocols can share one TCP connection.
- Second-class protocol: Some complex protocols without special marked characters can only be detected after several input data are parsed. Currently only HTTP is classified into this category.
- Third-class protocol: Special characters are in the middle of the protocol data, such as the magic number of nshead protocol is the 25th-28th characters. It is complex to handle this case because without reading first 28 bytes, we cannot determine whether the protocol is nshead. If it is tried before http, http messages less than 28 bytes may not be parsed, since the parser consider it as an uncomplete nshead message.

Considering that there will be only one protocol in most connections, we record the result of last selection so that it will be tried first when further data comes. It reduces the overhead of matching protocols to nearly zero for long connections. Although the process of matching protocols will be run everytime for short connections, the bottleneck of short connections is not in here and this method is still fast enough. If there are lots of new protocols added into brpc in the future, we may consider some heuristic methods to match protocols.

# Multi-protocol support in client side

