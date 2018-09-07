# brpc Proposal

## Abstract

brpc is an industrial-grade RPC framework for building reliable and
high-performance services.

## Proposal

We propose to contribute the brpc codebase and associated artifacts
(e.g. documentation etc.) to the Apache Software Foundation, and aim to
 build an open community around it in the 'Apache Way'.


## Background

RPC framework is very important for Baidu's development when building high performance services.
but the old existing ones can not fulfill our needs. so we build one from scratch in 2013.
and many core services inside Baidu have adopted it.
we named it as baidu rpc ,then change it as brpc for short.
In 2017, we open source it and hope to get more adoption, and get more contribution from outside.

## Rationale

brpc has been approved inside baidu, since many high performance core service are using it.
and since its open source, it has been adopted by several other companies , including Iqiyi,Didi,Sougou,BiliBili etc.

## Current Status

brpc has been an open source project on GitHub (https://github.com/brpc/brpc) since 2017.

Currently it has more than 7.3k stars, 1.6k forks, and is one of the most popular repositories in topic of rpc category in GitHub rpc catelogy.
It has been widely used in Baidu, with 1,000,000+ instances and thousands kinds of services.
Besides, many other companies have already used it also, such as Iqiyi, Didi, Sougou, BiliBili etc.

### Meritocracy

brpc was originally created by Ge Jun and Chen zhangyi inside baidu from 2013,
since its opensource in 2017, it already operates on meritocracy principles.
it accepts multiple contribute from other companies.
And now,the core developers are from several different companies.

we will follow Apache way to encourage more developers to contribute in this project.
we know that only active and committed developers from a diverse set of backgrounds
can make brpc a successful project.


### Community

brpc has been building an active community since its open source. Currently,
the community includes over 31 contributors.
The core developers of brpc are listed below.

### Core Developers

* Ge Jun(https://github.com/jamesge jge666@gmail.com)
* Chen Zhangyi(https://github.com/chenzhangyi frozen.zju@gmail.com)
* Jiang Rujie(https://github.com/old-bear jrjbear@gmail.com)
* Zhu Jiashun(http://github.com/zyearn zhujiashun2010@gmail.com)
* Wang Yao(https://github.com/ipconfigme ipconfigme@gmail.com)

### Alignment

brpc is useful for building reliable and high-performance applications.
Since ASF has many famous performance-related and rpc-related projects,
we believe that ASF is a perfect choice to help brpc project to attract
more developers and users as well as having more cooperation with existing projects.

## Known Risks

### Orphaned Products

Since our core developers are from different companies and many companies are using it,
the risk of the project being abandoned is minimal.
For example, Baidu is extensively using it in their production environment
and many large corporations including Iqiyi, Didi, Sougou, BiliBili use it in their production applications.


### Inexperience with Open Source

brpc has been an active open source project for more than one year.
During that time, the project has attracted 30+ contributors and gained a lot of attentions.
The core developers are all active users and followers of open source.

### Homogenous Developers

brpc was created inside Baidu, but after brpc was open sourced, it received a lot of bug fixes and enhancements from other developers not working at Baidu.
and the core developers now are from different companies now.

### Reliance on Salaried Developers

Baidu invested in brpc as a general rpc framework used in company widely.
The core developers have been dedicated to this project for about four years
and after its opensource, developers around the world have involved in.
Besides, we want more developers and researchers to contribute to the project.

### An Excessive Fascination with the Apache Brand

The mission of brpc is to help developers build reliable and high-performance services quickly and easily.
It has been widely used in production environment throughout Baidu and after opensource, it has gained much attention and attracted developers all over the world.
Apache Brand is very respected .we are very honored to have the opportunity to join ASF, with the understanding that its brand policies being respected,
And We hope Apache can help us build the ecosystem around brpc and attract more developers.


## Documentation

The following links provide more information about brpc in open source:

Codebase at Github: https://github.com/brpc/brpc
Issue Tracking: https://github.com/brpc/brpc/issues
Overview: https://github.com/brpc/brpc/blob/master/docs/en/overview.md

## Initial Source

brpc has been developed since 2014 by a team of engineers at Baidu Inc.
We currently use Github to maintain our source code and track issues at https://github.com/brpc/brpc.
We need to move our repository to Apache infrastructure.

## Source and Intellectual Property Submission Plan

brpc source code is available under Apache V2 license and owned by Baidu.
We will work with the committers to get ICLAs signed. We will provide a Software Grant Agreement from an authorized signer per https://www.apache.org/licenses/software-grant-template.pdf

## External Dependencies

brpc has the following external dependencies.

* Google gflags (BSD)
* Google protobuf (BSD)
* Google leveldb (BSD)

## Required Resources

### Mailing List

There are currently no mailing lists. The usual mailing lists are expected to be set up when entering incubation:

* private@brpc.incubator.apache.org
* dev@brpc.incubator.apache.org
* commits@brpc.incubator.apache.org

### Git Repositories:

Upon entering incubation, we want to transfer the existing repo from https://github.com/brpc/brpc to Apache infrastructure like https://github.com/apache/incubator-brpc.

### Issue Tracking:

brpc currently uses GitHub to track issues. Would like to continue to do so while we discuss migration possibilities with the ASF Infra committee.

### Other Resources:

Currently brpc has no dedicated website except Github homepage. In the future the website url should be http://brpc.incubator.apache.org/ to follow apache incubator conventions.

## Sponsors

### Champion

* todo

### Mentors

* todo

### Sponsoring Entity

We are requesting the Incubator to sponsor this project.

