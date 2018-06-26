# brpc external dependencies

def brpc_workspace():
  native.http_archive(
      name = "com_google_protobuf",
      strip_prefix = "protobuf-ab8edf1dbe2237b4717869eaab11a2998541ad8d",
      url = "https://github.com/google/protobuf/archive/ab8edf1dbe2237b4717869eaab11a2998541ad8d.tar.gz",
  )
  
  
  native.http_archive(
      name = "com_github_gflags_gflags",
      strip_prefix = "gflags-46f73f88b18aee341538c0dfc22b1710a6abedef",
      url = "https://github.com/gflags/gflags/archive/46f73f88b18aee341538c0dfc22b1710a6abedef.tar.gz",
  )
 
  native.bind(
      name = "gflags",
      actual = "@com_github_gflags_gflags//:gflags",
  )
  
  native.new_http_archive(
      name = "com_github_google_leveldb",
      build_file = str(Label("//:leveldb.BUILD")),
      strip_prefix = "leveldb-a53934a3ae1244679f812d998a4f16f2c7f309a6",
      url = "https://github.com/google/leveldb/archive/a53934a3ae1244679f812d998a4f16f2c7f309a6.tar.gz"
  )

  native.new_http_archive(
      name = "com_github_google_glog",
      build_file = str(Label("//:glog.BUILD")),
      strip_prefix = "glog-a6a166db069520dbbd653c97c2e5b12e08a8bb26",
      url = "https://github.com/google/glog/archive/a6a166db069520dbbd653c97c2e5b12e08a8bb26.tar.gz"
  )
  
  native.http_archive(
      name = "com_google_googletest",
      strip_prefix = "googletest-0fe96607d85cf3a25ac40da369db62bbee2939a5",
      url = "https://github.com/google/googletest/archive/0fe96607d85cf3a25ac40da369db62bbee2939a5.tar.gz",
  )
