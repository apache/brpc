def _package_impl(ctx):
  table = {
    "bin": ctx.files.bin,
    "include": ctx.files.include,
    "lib": ctx.files.lib,
    "etc": ctx.files.etc,
    "data": ctx.files.data,
    "conf": ctx.files.conf,
    "scripts": ctx.files.scripts,
    "module": ctx.files.module,
    "log": ctx.files.log,
    "proc": ctx.files.proc,
  }
  outputs = []
  for name, files in table.items():
    if len(files) == 0:
      continue

    if name in ("data", "conf", "scripts"):
      targets = [ ctx.actions.declare_file("%s/%s/%s" % (ctx.attr.name, name,"/".join( f.dirname.split("/")[2:] + [f.basename]) ))
              for f in files ]
    else:
      targets = [ ctx.actions.declare_file("%s/%s/%s" % (ctx.attr.name, name, f.basename))
              for f in files ]
    cmds = " && ".join(['cp %s %s' % (s.path, t.path)
                        for s, t in zip(files, targets)])
    outputs.extend(targets)
    ctx.actions.run_shell(outputs=targets, inputs=files, command=cmds)

  tar_file = ctx.actions.declare_file("%s.tgz" % ctx.attr.name)
  tar_cmd = "tar -hczf %s -C %s %s" % (tar_file.path, tar_file.dirname, ctx.attr.name)
  ctx.actions.run_shell(outputs=[tar_file], inputs=files + outputs, command=tar_cmd)
  return struct(files=depset([tar_file]))

package = rule(
  implementation=_package_impl,
  attrs={
    "bin": attr.label_list(allow_files=True),
    "include": attr.label_list(allow_files=True),
    "lib": attr.label_list(allow_files=True),
    "etc": attr.label_list(allow_files=True),
    "scripts": attr.label_list(allow_files=True),
    "data": attr.label_list(allow_files=True),
    "module": attr.label_list(allow_files=True),
    "log": attr.label_list(allow_files=True),
    "proc": attr.label_list(allow_files=True),
    "conf": attr.label_list(allow_files=True),
  },
)

