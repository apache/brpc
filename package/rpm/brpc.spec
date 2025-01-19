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
Version:	1.12.1
Release:	1%{?dist}
Summary:	Industrial-grade RPC framework using C++ Language.

Group:		Development
License:	Apache2
URL:		https://github.com/apache/brpc
Source0:	https://downloads.apache.org/brpc/%{version}/apache-brpc-%{version}-src.tar.gz

# https://access.redhat.com/solutions/519993
%global  _filter_GLIBC_PRIVATE 1
%global __filter_GLIBC_PRIVATE 1

%if 0%{?fedora} >= 15 || 0%{?rhel} >= 8
%global use_devtoolset 0
%else
%global use_devtoolset 1
%endif

%if 0%{?use_devtoolset}
BuildRequires: devtoolset-8-gcc-c++
%define __strip /opt/rh/devtoolset-8/root/usr/bin/strip
%endif

BuildRequires:	cmake
BuildRequires:	gcc
BuildRequires:	gcc-c++
BuildRequires:	gflags-devel >= 2.1
BuildRequires:	protobuf-devel >= 2.4
BuildRequires:	leveldb-devel
BuildRequires:	openssl-devel

%description
Apache bRPC is an Industrial-grade RPC framework using C++ Language,
which is often used in high performance systems such as Search, Storage,
Machine learning, Advertisement, Recommendation etc.

%package tools
Summary: The %{name} tools.
Requires: %{name} = %{version}-%{release}
%description tools
The %{name} tools.

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
%setup -n apache-%{name}-%{version}-src

%build
%if 0%{?use_devtoolset}
. /opt/rh/devtoolset-8/enable
%endif

%if 0%{?fedora} >= 33 || 0%{?rhel} >= 8
%{cmake} -DBUILD_BRPC_TOOLS:BOOLEAN=ON -DDOWNLOAD_GTEST:BOOLEAN=OFF
%{cmake_build}
%else
mkdir -p %{_target_platform}
pushd %{_target_platform}

%{cmake} -DBUILD_BRPC_TOOLS:BOOLEAN=ON -DDOWNLOAD_GTEST:BOOLEAN=OFF ..
make %{?_smp_mflags}

popd
%endif

%install
rm -rf $RPM_BUILD_ROOT

%if 0%{?fedora} >= 33 || 0%{?rhel} >= 8
%{cmake_install}
%else
pushd %{_target_platform}
%make_install
popd
%endif

%clean
rm -rf $RPM_BUILD_ROOT

%files
%{_libdir}/libbrpc.so

%files tools
%{_bindir}/*

%files devel
%{_includedir}/*
%{_libdir}/pkgconfig/*

%files static
%{_libdir}/libbrpc.a

%changelog

