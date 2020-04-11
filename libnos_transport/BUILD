cc_library(
    name = "libnos_transport",
    srcs = [
        "crc16.c",
        "transport.c",
    ],
    hdrs = [
        "crc16.h",
        "include/nos/transport.h",
    ],
    includes = [
        "include",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//:nos_headers",
        "//libnos_datagram",
    ],
)

cc_test(
    name = "libnos_transport_test",
    srcs = [
        "test/test.cpp",
    ],
    copts = [
        "-fsanitize=address",
        "-Ilibnos_transport",
    ],
    linkopts = ["-fsanitize=address"],
    deps = [
        ":libnos_transport",
        "//:nos_headers",
        "@gtest",
    ],
)
