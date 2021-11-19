package(
    default_visibility=["//visibility:public"]
)

config_setting(
    name = "macos",
    values = {
        "cpu": "darwin",
    },
    visibility = ["//visibility:private"],
)

cc_library(
    name = "crypto",
    srcs = select({
        ":macos": ["lib/libcrypto.dylib"],
        "//conditions:default": []
    }),
    linkopts = select({
        ":macos" : [],
        "//conditions:default": ["-lcrypto"],
    }),
)

cc_library(
    name = "ssl",
    hdrs = select({
        ":macos": glob(["include/openssl/*.h"]),
        "//conditions:default": []
    }),
    srcs = select ({
        ":macos": ["lib/libssl.dylib"],
        "//conditions:default": []
    }),
    includes = ["include"],
    linkopts = select({
        ":macos" : [],
        "//conditions:default": ["-lssl"],
    }),
    deps = [":crypto"]
)
