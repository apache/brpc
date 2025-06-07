def generate_brpc_unit_tests(name, srcs, deps, copts):
    tests = []
    for s in srcs:
        tgt = s.replace(".cpp", "")
        native.cc_test(
            name = tgt,
            srcs = [s],
            copts = copts,
            deps  = deps,
        )
        tests.append(":" + tgt)

    native.test_suite(
        name  = name,
        tests = tests,
    )