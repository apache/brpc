def _flat_runfiles_impl(ctx):
    r = ctx.runfiles()
    for f in ctx.files.srcs:
        r = r.merge(ctx.runfiles(symlinks = {f.basename: f}))
    return DefaultInfo(runfiles = r)

flat_runfiles = rule(
    implementation = _flat_runfiles_impl,
    attrs = {
        "srcs": attr.label_list(allow_files = True),
    },
)
