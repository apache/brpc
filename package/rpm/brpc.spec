#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements. See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership. The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License. You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied. See the License for the
# specific language governing permissions and limitations
# under the License.
#

Name:		brpc
Version:	1.2.0
Release:	1%{?dist}
Summary:	An industrial-grade RPC framework used throughout Baidu, with 1,000,000+ instances(not counting clients) and thousands kinds of services.

Group:		Development
License:	Apache2
URL:		https://github.com/apache/incubator-brpc
Source0:	incubator-brpc-%{version}.tar.gz

# https://access.redhat.com/solutions/519993
%global  _filter_GLIBC_PRIVATE 1
%global __filter_GLIBC_PRIVATE 1

%if 0%{?fedora} >= 15 || 0%{?rhel} >= 7
%global use_devtoolset 0
%else
%global use_devtoolset 1
%endif

%if 0%{?use_devtoolset}
BuildRequires: devtoolset-8-gcc-c++
%define __strip /opt/rh/devtoolset-8/root/usr/bin/strip
%endif

BuildRequires:	gflags-devel >= 2.1
BuildRequires:	protobuf-devel >= 2.4
BuildRequires:	leveldb-devel
BuildRequires:	openssl-devel

%description
An industrial-grade RPC framework used throughout Baidu, with 1,000,000+ instances(not counting clients) and thousands kinds of services.
"brpc" means "better RPC".

%package devel
Summary: The %{name} headers and shared development libraries
Requires: %{name} = %{version}-%{release}
%description devel
Headers and shared object symbolic links for the %{name} library.

%package static
Summary: The %{name} static development libraries
Requires: brpc-devel = %{version}-%{release}
%description static
Static %{name} libraries.

%prep
%setup -n incubator-%{name}-%{version}


%build
mkdir -p %{_target_platform}

pushd %{_target_platform}

%if 0%{?use_devtoolset}
. /opt/rh/devtoolset-8/enable
%endif

%{cmake} ..

make %{?_smp_mflags}
popd

%install
rm -rf $RPM_BUILD_ROOT

pushd %{_target_platform}
%make_install
popd

%clean
rm -rf $RPM_BUILD_ROOT

%files
%{_libdir}/libbrpc.so

%files devel
%{_includedir}/*
%{_libdir}/pkgconfig/*

%files static
%{_libdir}/libbrpc.a

%changelog

