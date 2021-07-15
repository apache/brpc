If you meet any problem or request a new feature, you're welcome to [create an issue](https://github.com/brpc/brpc/issues/new/choose).

If you can solve any of [the issues](https://github.com/brpc/brpc/issues), you're welcome to send the PR to us.

Before the PR:

* Make sure your code style conforms to [google C++ coding style](https://google.github.io/styleguide/cppguide.html). Indentation is preferred to be 4 spaces.
* The code appears where it should be. For example the code to support an extra protocol should not be put in general classes like server.cpp, channel.cpp, while a general modification would better not be hidden inside a very specific protocol.
* Has unittests.

After the PR:

* Make sure the [travis-ci](https://app.travis-ci.com/github/apache/incubator-brpc/pull_requests) passed.

# Chinese version

如果你遇到问题或需要新功能，欢迎[创建issue](https://github.com/brpc/brpc/issues/new/choose)。

如果你可以解决某个[issue](https://github.com/brpc/brpc/issues), 欢迎发送PR。

发送PR前请确认：

* 你的代码符合[google C++代码规范](https://google.github.io/styleguide/cppguide.html)。缩进最好为4个空格。
* 代码出现的位置和其定位相符。比如对于某特定协议的扩展代码不该出现在server.cpp, channel.cpp这些较为通用的类中，而一些非常通用的改动也不该深藏在某个特定协议的cpp中。
* 有对应的单测代码。

提交PR后请确认：

* [travis-ci](https://app.travis-ci.com/github/apache/incubator-brpc/pull_requests)成功通过。
