1. 每次CI前运行tools/switch_trunk确保baidu-rpc依赖的主要模块使用主干版本。依赖的模块有：public/common public/bvar public/iobuf public/bthread public/protobuf-json public/murmurhash public/mcpack2pb2
2. 每次CI前检查所有修改的文件。在svn下可用如下命令：`svn st | grep "^?.*\.\(cpp\|c\|cc\|h\|hpp\|sh\|py\|pl\|proto\|thrift\|java\)$"`。如有预期之外的文件改动，请咨询后再CI。
3. 每次CI前确认comake2和bcloud都可以无warning编译通过。
   1. comake2编译方法：comake2 -P && make -sj8
   2. bcloud编译方法：bcloud build
4. 每次CI前务必确认在gcc 4.8和3.4下都编译通过，最好也检查gcc 4.4下的编译情况。
   1. 用gcc 4.8编译：在~/.bashrc中加入`export PATH=/opt/compiler/gcc-4.8.2/bin:$PATH`，重新登陆后有效。
   2. 用gcc 3.4编译：一些老机器的/usr/bin/gcc默认是3.4。BCLOUD默认也是3.4（在2016/6/17是的）。
   3. 用gcc 4.4编译：centos 6.3的/usr/bin/gcc默认是4.4。
5. 每次CI前运行单测通过，运行方式：进入src/baidu/rpc/test目录comake2 -P，如果报错则运行comake2 -UB拉依赖（拉完依赖要运行tools/switch_trunk把依赖模块再切回主干），make -sj8，运行所有形如test_*的可执行程序。