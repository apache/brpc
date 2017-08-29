# 1 背景介绍

目 前公司的rpc框架有baidu-rpc、hulu、sofa、nova等，很多的业务都是基于这些框架搭建服务的，但是如果依赖的下游未ready的情 况下，很难开展联调测试，目前在进行功能验证时，只能人工的写一些mock代码来模拟下游，这种方式的代码复用率低，不同产品测试之间无法共享使用。此 外，pbrpc的模块的异常测试往往也是需要修改接口代码，改变接口行为，模拟异常，测试很难自动化；pbrpc mockserver就是解决在这些问题，具体的适用场景：

使用场景：

1. **模块功能验证**： 涉及上下游模块联调测试，但依赖的下游未ready情况下,可以快速的用pbprc_mockserver，模拟下游，测试模块自身功能；
2. **异常测试**：下游server的异常难构造，可以通过pbrpc_mockserver 来定制预期的response,构造各种异常场景、异常数据等；



# 2 使用示例

1. 获取工具：svn co  <https://svn.baidu.com/com-test/trunk/public/baidu-rpc/pbrpc_service_tools/pbrpc_mockserver/>
2. 生成一个简单的echo服务的mockserver，执行如下指令
```python
./gen_mock_server.py -s example.EchoService -m Echo -p .proto/echo.proto
```
- 当前data目录生成，json格式的输入和输出文件，**用户可以修改json 来自定义 response** 
```bash
ll data/
total 12
-rw-rw-r--  1 work work  26 Dec 23 20:26 example.EchoService.Echo.input.json
-rw-rw-r--  1 work work  26 Dec 23 20:26 example.EchoService.Echo.output.json
-rw-rw-r--  1 work work 202 Dec 23 20:26 example.EchoService.info
```

-  在src/mockserver/ 目录下生成mock 代码

3. 执行sh bulid.sh 编译生成mockserver
4. 启动mockserver   ./mockserver --port=9999  &

# 3 实现介绍

## 3.1 内部框架

![img](http://wiki.baidu.com/download/attachments/105293828/image2014-12-22%2019%3A53%3A41.png?version=1&modificationDate=1433828648000&api=v2)

## 3.2 功能详解

- ### 接收用户注册proto & service，生成response模版，支持用户自定义

用 户提供rpc的proto文件，并且提供需要mock的service name，二者缺一不可。 使用protobuf自带的protoc工具 基于proto文件生成c语言文件；根据service name来获取service的method，method的输入输出类型，根据输出类型获取响应的结构，生成用户可读的json格式的响应文件。 用户根据响应json文件的模板，自定义响应各个字段的内容；

模块工作流程如下：

1. Proto文件使用protoc生成c语言描述文件，编译进mockserver proto解析系统；
2. Mockserver proto解析系统根据service name，解析service中的各个method；
3. 根据method解析method的响应message类型；
4. 根据响应message类型，使用递归的方式遍历message的各个字段，包括嵌套的message
5. 生成key-value对形式的json格式的响应类型

- ### mockserver 源码的自动生成：

根据用户提供的proto，和用户需要mock的service，基于baidu-rpc的通用server的模版，自动生成指定service的mockserver源码。

- ### response自动填充功能：

Mockserver模块负责解析用户请求，根据用户请求的rpc协议，解析出用户请求的service及对应的method，根据proto及method的得出需要响应的message类型，工作流程如下：

1. Mockserver接收用户请求；
2. 解析用户请求，根据用户请求类型，分别使用不同的协议处理handler处理；
3. 解析出请求的service，及对应的method；
4. 根据method解析出request，并得出response的类型；
5. 根据response的类型，结合用户自定义的response类型的json串，填充response结构并转换为pb格式；
6. 组装响应，发送给客户端，完成server mock。