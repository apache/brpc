如果你的程序只使用了baidu-rpc的client或根本没有使用baidu-rpc，但你也想使用baidu-rpc的内置服务，只要在程序中启动一个空的server就行了，这种server我们称为**dummy server**。

# 使用了baidu-rpc的client

只要在程序运行目录建立dummy_server.port文件，填入一个端口号（比如8888），程序会马上在这个端口上启动一个dummy server。在浏览器中访问它的内置服务，便可看到同进程内的所有bvar。
![img](http://wiki.baidu.com/download/attachments/71337189/image2015-12-25%2017%3A46%3A20.png?version=1&modificationDate=1451036781000&api=v2)
![img](http://wiki.baidu.com/download/attachments/71337189/image2015-12-25%2017%3A47%3A30.png?version=1&modificationDate=1451036850000&api=v2)
![img](http://wiki.baidu.com/download/attachments/71337189/image2015-12-25%2017%3A48%3A24.png?version=1&modificationDate=1451036904000&api=v2)

# 没有使用baidu-rpc

你必须手动加入dummy server。你得先查看[Getting Started](http://wiki.baidu.com/display/RPC/Getting+Started)如何下载和编译baidu-rpc，然后在程序入口处加入如下代码片段：

```c++
#include <baidu/rpc/server.h>
 
...
 
int main() {
    ...
    baidu::rpc::Server dummy_server;
    baidu::rpc::ServerOptions dummy_server_options;
    dummy_server_options.num_threads = 0;  // 不要改变寄主程序的线程数。
    if (dummy_server.Start(8888/*port*/, &dummy_server_options) != 0) {
        LOG(FATAL) << "Fail to start dummy server";
        return -1;
    }
    ...
}
```

r31803之后加入dummy server更容易了，只要一行：

```c++
#include <baidu/rpc/server.h>
 
...
 
int main() {
    ...
    baidu::rpc::StartDummyServerAt(8888/*port*/);
    ...
}
```