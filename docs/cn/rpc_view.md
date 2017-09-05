rpc_view可以查看端口不在8000-8999的server的内置服务。之前如果一个服务的端口不在8000-8999，我们只能在命令行下使用curl查看它的内置服务，没有历史趋势和动态曲线，也无法点击链接，排查问题不方便。rpc_view是一个特殊的http proxy：把对它的所有访问都转为对目标server的访问。只要把rpc_view启动在8000-8999端口，我们就能通过它看到原本不能直接看到的server了。

# 获取工具

在终端中运行如下命令即可编译出最新版baidu-rpc包含的rpc_view工具.

```bash
PREVDIR=`pwd` && TEMPDIR=`mktemp -d -t build_rpc_press.XXXXXXXXXX` && mkdir $TEMPDIR/public && cd $TEMPDIR/public && svn co http://icode.baidu.com/repo/baidu/opensource/baidu-rpc/files/master/blob && cd baidu-rpc && comake2 -UB -J8 -j8 && comake2 -P && make -sj8 && cd tools/rpc_view && comake2 -P && make -sj8 && cp -f ./rpc_view $PREVDIR && cd $PREVDIR; rm -rf $TEMPDIR
```

编译完成后，rpc_press就会出现在当前目录下。如果编译出错，看[Getting Started](getting_started.md)。

 

也可以从[agile](http://agile.baidu.com/#/release/public/baidu-rpc)上获取产出，下面是获取版本r34466中的rpc_view的命令：

```bash
wget -r -nH --level=0 --cut-dirs=8 getprod@buildprod.scm.baidu.com:/temp/data/prod-64/public/baidu-rpc/d92a9fac91892a5f4784fc105e493933/r34466/output/bin/rpc_view  --user getprod --password getprod --preserve-permissions
```

在CentOS 6.3上如果出现找不到libssl.so.4的错误，可执行`ln -s /usr/lib64/libssl.so.6 libssl.so.4临时解决`

# 访问目标server

确保你的机器能访问目标server，开发机应该都可以，一些测试机可能不行。运行./rpc_view <server-address>就可以了。

比如：

```
$ ./rpc_view 10.46.130.53:9970
TRACE: 02-14 12:12:20:   * 0 src/brpc/server.cpp:762] Server[rpc_view_server] is serving on port=8888.
TRACE: 02-14 12:12:20:   * 0 src/brpc/server.cpp:771] Check out http://db-rpc-dev00.db01.baidu.com:8888 in web browser.
```

打开rpc_view在8888端口提供的页面（在secureCRT中按住ctrl点url）：

![img](http://wiki.baidu.com/download/attachments/167651918/image2016-2-14%2012%3A15%3A42.png?version=1&modificationDate=1455423342000&api=v2)

这个页面正是目标server的内置服务，右下角的提示告诉我们这是rpc_view提供的。这个页面和真实的内置服务基本是一样的，你可以做任何操作。

# 更换目标server

你可以随时停掉rpc_view并更换目标server，不过你觉得麻烦的话，也可以在浏览器上操作：给url加上?changetarget=<new-server-address>就行了。

假如我们之前停留在原目标server的/connections页面：

![img](http://wiki.baidu.com/download/attachments/167651918/image2016-2-14%2012%3A22%3A32.png?version=1&modificationDate=1455423752000&api=v2)

加上?changetarge后就跳到新目标server的/connections页面了。接下来点击其他tab都会显示新目标server的。

![img](http://wiki.baidu.com/download/attachments/167651918/image2016-2-14%2012%3A23%3A10.png?version=1&modificationDate=1455423790000&api=v2)
