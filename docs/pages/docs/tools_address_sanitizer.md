# AddressSanitizer介绍

​    AddressSanitizer最初由google研发，简称asan, 用于运行时检测C/C++程序中的内存错误，相比较传统工具如valgind，运行速度快，检测到错误之后，输出信息非常详细，可以通过add2line符号化输出，从而直接定位到代码行，方便快速的定位问题；

   官方doc: <http://clang.llvm.org/docs/AddressSanitizer.html>

# AddressSanitizer可以检测的错误类型

- 堆、栈及全局变量的越界访问；


- use-after-free；
- use-after-return；
- double-free, invalid free；
- 内存泄露；

# AddressSanitizer支持的平台

- Linux i386/x86_64 
- OS X 10.7 - 10.11 (i386/x86_64)
- iOS Simulator
- Android ARM
- FreeBSD i386/x86_64

# AddressSanitizer如何使用

## **4.1环境配置**

- gcc4.8及以上版本 (gcc4.8已经集成了asan功能)；
- asan不支持tcmalloc,所以代码中请确保关闭该功能；

## **4.2 使用方法（针对C++）**

- 在COMAKE文件中添加asan编译参数： -fPIC -fsanitize=address  -fno-omit-frame-pointer
- 在COMAKE文件中添加asan链接参数：-lasan

# 使用示例

1. ** **为了验证asan的功能是否生效，我们手动在测试代码client.cpp的中添加了内存越界访问的错误代码，该代码中依赖baidu-rpc生成的静态链接库libbdrpc.a；
2. 预期：启动client及server之后，输出内存越界错误信息，表明asan的配置已经生效；

## **5.1 在测试代码client.cpp中添加内存越界代码**

**      **在测试代码client.cpp中添加以下内存越界代码：

​      ![img](http://wiki.baidu.com/download/attachments/120898866/mc.png?version=1&modificationDate=1436440081000&api=v2)

## **5.2 使用asan检测测试代码及baidu-rpc内存错误**

- 在baidu-rpc源码及测试代码client.cpp中，修改COMAKE文件，添加asan编译及链接的参数：

​       ![img](http://wiki.baidu.com/download/attachments/120898866/22.png?version=1&modificationDate=1436440186000&api=v2)

- 保存之后，执行comake2 -UB && comake2 && make -j10,生成静态链接库 libbdrpc.a；


- 运行测试代码的可执行文件echo_client及echo_server,输出内存越界的错误提示，表明环境设置成功；

  ![img](http://wiki.baidu.com/download/attachments/120898866/cc.png?version=1&modificationDate=1436440721000&api=v2)

- 使用addr2line符号化输出，直接定位到代码行，根据个人需求，可以直接使用addr2line，也可以写脚本实现：

  ![img](http://wiki.baidu.com/download/attachments/120898866/tt.png?version=1&modificationDate=1436440921000&api=v2)

- 以上则表明环境配置成功，如果代码中有内存越界等问题的话，asan会检测出来，并直接输出到标准错误；