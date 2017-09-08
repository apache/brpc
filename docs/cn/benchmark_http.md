可代替[ab](https://httpd.apache.org/docs/2.2/programs/ab.html)测试http server极限性能。ab功能较多但年代久远，有时本身可能会成为瓶颈。benchmark_http基本上就是一个brpc http client，性能很高，功能较少，一般压测够用了。

使用方法：

首先你得[下载和编译](getting_started.md)了brpc源码，然后去example/http_c++目录编译，成功后应该能看到benchmark_http。
