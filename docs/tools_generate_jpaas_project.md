## 获取脚本

你需要[public/baidu-rpc/tools](http://websvn.work.baidu.com/repos/public/list/trunk/baidu-rpc/tools)目录下的makeproj, makeproj_template和shflags。它们可以被拷贝其他地方。

## 使用makeproj

```
$ ./makeproj -h
USAGE: ./makeproj [flags] args
flags:
  --path:  Repository <path> of your project. e.g. app/ecom/fcr/my_project (default: '.')
  --workroot:  Where to put your code tree (default: '')
  --namespace:  namespace of your code (default: 'example')
  --port:  default port of your server (default: 9000)
  -h,--[no]help:  show this help (default: false)
```

以~/jpaas_test为根目录建立代码路径为app/ecom/fcr/fancy_project的项目。（目录会自动建立）

```
$ makeproj --path app/ecom/fcr/fancy_project --workroot ~/jpaas_test
```

进入新建立的模块目录：

```
$ cd ~/jpaas_test/app/ecom/fcr/fancy_project/
$ ls
build.sh  COMAKE  fancy_project_client.cpp  fancy_project.proto  fancy_project_server.cpp  jpaas_control  Release
```

运行build.sh会下载依赖模块、编译、并生成output/bin/<server>.tar.gz，它可以部署至jpaas。如下图所示：

```
$ ls output/bin
fancy_project_client  fancy_project_server  fancy_project_server.tar.gz
```