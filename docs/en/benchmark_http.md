It can replace [ab](https://httpd.apache.org/docs/2.2/programs/ab.html) to test the extreme performance of http server. Ab has many functions but is old, and sometimes it may become a bottleneck. Benchmark_http is basically a brpc http client with high performance and few functions. Generally, it is sufficient for stress testing.

Instructions:

First you have to [download and compile](getting_started.md) the brpc source code, then go to the example/http_c++ directory to compile, you should see benchmark_http after success.