cc_binary(
    name = "protoc_gen_nos_client_cpp",
    srcs = [
        "main.cpp",
    ],
    copts = [
        "-std=c++11",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_protobuf//:protoc_lib",
        "@nugget_host_generic//nugget/proto:nugget_protobuf_options_cc_proto",
    ],
)
