Name:		brpc
Version:	0.9.7
Release:	1%{?dist}
Summary:	An industrial-grade RPC framework used throughout Baidu, with 1,000,000+ instances(not counting clients) and thousands kinds of services.

Group:		Development
License:	Apache2
URL:		https://github.com/apache/incubator-brpc
Source0:	incubator-brpc-%{name}.tar.gz

%global __filter_GLIBC_PRIVATE 1

%if 0%{?fedora} >= 15 || 0%{?rhel} >= 7
%global use_devtoolset 0
%else
%global use_devtoolset 1
%endif

%if 0%{?use_devtoolset}
BuildRequires: devtoolset-8-gcc-c++
%endif

BuildRequires:	gflags-devel
BuildRequires:	protobuf-devel
BuildRequires:	leveldb-devel
BuildRequires:	openssl-devel
Requires:	gflags
Requires:	protobuf
Requires:	leveldb
Requires:	openssl-libs

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

