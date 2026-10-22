# Copied from Homebrew/homebrew-core at commit cced3ef1d489fce4eba453a333619722c3a8dd17
# (Formula/p/protobuf@21.rb) with its deprecate!/disable! lines removed, so that it
# still installs after Homebrew removes protobuf@21. Used by .github/workflows/ci-macos.yml.
#
# BSD 2-Clause License
#
# Copyright (c) 2009-present, Homebrew contributors
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

class ProtobufAT21 < Formula
  desc "Protocol buffers (Google's data interchange format)"
  homepage "https://protobuf.dev/"
  url "https://github.com/protocolbuffers/protobuf/releases/download/v21.12/protobuf-all-21.12.tar.gz"
  sha256 "2c6a36c7b5a55accae063667ef3c55f2642e67476d96d355ff0acb13dbb47f09"
  license "BSD-3-Clause"
  revision 1

  bottle do
    sha256 cellar: :any,                 arm64_tahoe:   "65023d2a73ac89c385ff14782c38c1f629828d753d880f8e910a7ee19fc35e47"
    sha256 cellar: :any,                 arm64_sequoia: "9a941768e6f914b2c5a412728f3794a49a53b894b3cbe637809df93bc9b521e3"
    sha256 cellar: :any,                 arm64_sonoma:  "a906fb18cfff9e07a71c34d01c74676ea28290fa6c4cfa26f68e48449abf242a"
    sha256                               arm64_ventura: "f2997129a1170a2e472499cdfc829c03f8e8613826360837498fc42843f7bb94"
    sha256                               sonoma:        "f14d28a66a3801b28475e4913e9d5515074179ec8eec48c8fea5ec0c829e8f19"
    sha256                               ventura:       "2b155b3335e6db2264fbc0bfb5c1d89a3bc7ac2fd4a15d844d653df8b0aa2858"
    sha256 cellar: :any_skip_relocation, arm64_linux:   "db77181834de160a35c791b30f45a43cd01c6206784a1d5ff1e7591a52ded2a7"
    sha256 cellar: :any_skip_relocation, x86_64_linux:  "b2557598e344f5da522d3a819c63cd59a46ed043dc4d9aecfa9b721c162b7778"
  end

  keg_only :versioned_formula

  # Support for protoc 21.x (protobuf C++ 3.21.x) ended on 2024-03-31
  # Ref: https://protobuf.dev/support/version-support/#cpp

  depends_on "cmake" => :build
  depends_on "python@3.12" => [:build, :test]
  depends_on "python@3.13" => [:build, :test]

  uses_from_macos "zlib"

  # Fix build with python@3.11
  patch do
    url "https://github.com/protocolbuffers/protobuf/commit/da973aff2adab60a9e516d3202c111dbdde1a50f.patch?full_index=1"
    sha256 "911925e427a396fa5e54354db8324c0178f5c602b3f819f7d471bb569cc34f53"
    type :backport
    resolves "https://github.com/protocolbuffers/protobuf/pull/10403"
  end

  def pythons
    deps.map(&:to_formula)
        .select { |f| f.name.match?(/^python@\d\.\d+$/) }
        .map { |f| f.opt_libexec/"bin/python" }
  end

  def install
    cmake_args = %w[
      -Dprotobuf_BUILD_LIBPROTOC=ON
      -Dprotobuf_INSTALL_EXAMPLES=ON
      -Dprotobuf_BUILD_TESTS=OFF
    ]

    system "cmake", "-S", ".", "-B", "build",
                    "-DBUILD_SHARED_LIBS=ON",
                    "-Dprotobuf_BUILD_SHARED_LIBS=ON",
                    *cmake_args, *std_cmake_args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"

    pkgshare.install "editors/proto.vim"
    elisp.install "editors/protobuf-mode.el"

    ENV.append_to_cflags "-I#{include}"
    ENV.append_to_cflags "-L#{lib}"
    ENV["PROTOC"] = bin/"protoc"

    pip_args = ["--config-settings=--build-option=--cpp_implementation"]
    pythons.each do |python|
      pyext_dir = prefix/Language::Python.site_packages(python)/"google/protobuf/pyext"
      with_env(LDFLAGS: "-Wl,-rpath,#{rpath(source: pyext_dir)} #{ENV.ldflags}".strip) do
        system python, "-m", "pip", "install", *pip_args, *std_pip_args(build_isolation: true), "./python"
      end
    end

    system "cmake", "-S", ".", "-B", "static",
                    "-Dprotobuf_BUILD_SHARED_LIBS=OFF",
                    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
                    "-DWITH_PROTOC=#{bin}/protoc",
                     *cmake_args, *std_cmake_args
    system "cmake", "--build", "static"
    lib.install buildpath.glob("static/*.a")
  end

  test do
    testdata = <<~PROTO
      syntax = "proto3";
      package test;
      message TestCase {
        string name = 4;
      }
      message Test {
        repeated TestCase case = 1;
      }
    PROTO
    (testpath/"test.proto").write testdata
    system bin/"protoc", "test.proto", "--cpp_out=."

    pythons.each do |python|
      with_env(PYTHONPATH: (prefix/Language::Python.site_packages(python)).to_s) do
        system python, "-c", "import google.protobuf"
      end
    end
  end
end
