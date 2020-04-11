workspace(name = "nugget_host_generic")

local_repository(
    name = "nugget_bazelrules",
    path = "../../bazel_rules",
)

load("@nugget_bazelrules//:deps.bzl", "nugget_deps")

nugget_deps("../..")
