# brpc external dependencies

def brpc_workspace():
  native.http_archive(
      name = "com_google_protobuf",
      strip_prefix = "protobuf-b04e5cba356212e4e8c66c61bbe0c3a20537c5b9",
      url = "https://github.com/google/protobuf/archive/b04e5cba356212e4e8c66c61bbe0c3a20537c5b9.tar.gz",
  )
  
  
  native.http_archive(
      name = "com_github_gflags_gflags",
      strip_prefix = "gflags-46f73f88b18aee341538c0dfc22b1710a6abedef",
      url = "https://github.com/gflags/gflags/archive/46f73f88b18aee341538c0dfc22b1710a6abedef.tar.gz",
  )
  
  
  native.new_http_archive(
      name = "com_github_google_leveldb",
      build_file = str(Label("//:leveldb.BUILD")),
      strip_prefix = "leveldb-a53934a3ae1244679f812d998a4f16f2c7f309a6",
      url = "https://github.com/google/leveldb/archive/a53934a3ae1244679f812d998a4f16f2c7f309a6.tar.gz"
  )

