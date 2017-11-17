# Cetcd is a C client for etcd

[![License](https://img.shields.io/badge/license-Apache2.0-blue.svg)](LICENSE)
[![Gitter](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/shafreeck/cetcd?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge)
[![Stories in Ready](https://badge.waffle.io/shafreeck/cetcd.png?label=ready&title=Ready)](https://waffle.io/shafreeck/cetcd)
[![wercker status](https://app.wercker.com/status/cc417f5b6e093b4d0b962580a3d022cb/s/master "wercker status")](https://app.wercker.com/project/bykey/cc417f5b6e093b4d0b962580a3d022cb)

<!-- START doctoc generated TOC please keep comment here to allow auto update -->
<!-- DON'T EDIT THIS SECTION, INSTEAD RE-RUN doctoc TO UPDATE -->
**Table of Contents**  *generated with [DocToc](https://github.com/thlorenz/doctoc)*

- [Cetcd is a C client for etcd](#cetcd-is-a-c-client-for-etcd)
  - [Status](#status)
  - [Features](#features)
  - [Deps](#deps)
  - [Install](#install)
  - [Link](#link)
  - [Examples](#examples)
  - [Usage](#usage)
    - [Create an array to store the etcd addresses](#create-an-array-to-store-the-etcd-addresses)
    - [Init the cetcd_client](#init-the-cetcd_client)
    - [Set a key](#set-a-key)
    - [Get a key](#get-a-key)
    - [List a directory](#list-a-directory)
    - [Clean all resources](#clean-all-resources)

<!-- END doctoc generated TOC please keep comment here to allow auto update -->

## Status
 cetcd is on active development. It aims to be used in production environment and to supply full features of etcd.
 **Any issues or pull requests are welcome!**

## Features
 * Round-robin load balance and failover
 * Full support for etcd keys space apis
 * Multiple concurrent watchers support

## Deps
 cetcd use [sds](https://github.com/antirez/sds) as a dynamic string utility.  It is licensed in sds/LICENSE.
 sds is interaged in cetcd's source code, so you don't have to install it before.

 [yajl](https://github.com/lloyd/yajl) is a powerful json stream parsing libaray. We use the stream apis to
 parse response from cetcd. It is already integrated as a third-party dependency, so you are not necessary to
 install it before.

 [curl](http://curl.haxx.se/download.html) is required to issue HTTP requests in cetcd

## Install

Install curl if needed
on Ubuntu
```
apt-get install libcurl4-openssl-dev
```
or on CentOS
```
yum install libcurl-devel
```
then
 ```
 make
 make install
 ```
 It default installs to /usr/local.

 Use
 ```
 make install prefix=/path
 ```
 to specify your custom path.

## Link
 Use `-lcetcd` to link the library

## Examples
* [cetcd_get.c](examples/cetcd_get.c)
* [cetcd_lsdir.c](examples/cetcd_lsdir.c)
* [multi_watch.c](examples/multi_watch.c)

## Usage
cetcd_array is an expandable dynamic array. It is used to pass etcd cluster addresses, and return cetcd response nodes

### Create an array to store the etcd addresses
```
    cetcd_array addrs;

    cetcd_array_init(&addrs, 3);
    cetcd_array_append(&addrs, "127.0.0.1:2379");
    cetcd_array_append(&addrs, "127.0.0.1:2378");
    cetcd_array_append(&addrs, "127.0.0.1:2377");
```

cetcd_client is a context cetcd uses to issue requests, you should init it with etcd addresses
### Init the cetcd_client
```
    cetcd_client cli;
    cetcd_client_init(&cli, &addrs);
```

Then you can issue an cetcd request which reply with an cetcd response
### Set a key
```
    cetcd_response *resp;
    resp = cetcd_set(&cli, "/service/redis", "hello cetcd", 0);
    if(resp->err) {
        printf("error :%d, %s (%s)\n", resp->err->ecode, resp->err->message, resp->err->cause);
    }
    cetcd_response_release(resp);
```

### Get a key
```
    cetcd_response *resp;
    resp = cetcd_get(&cli, "/service/redis");
    if(resp->err) {
        printf("error :%d, %s (%s)\n", resp->err->ecode, resp->err->message, resp->err->cause);
    }
    cetcd_response_release(resp);
```
### List a directory
```
    cetcd_response *resp;
    resp = cetcd_lsdir(&cli, "/service", 1, 1);
    if(resp->err) {
        printf("error :%d, %s (%s)\n", resp->err->ecode, resp->err->message, resp->err->cause);
    }
    cetcd_response_print(resp);
    cetcd_response_release(resp);
```

### Clean all resources
```
    cetcd_array_destory(&addrs);
    cetcd_client_destroy(&cli);
```
See [examples/cetcdget.c](https://github.com/shafreeck/cetcd/blob/master/examples/cetcdget.c) for more detailes
