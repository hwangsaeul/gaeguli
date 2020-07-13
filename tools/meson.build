tools = [
  'pipeline'
]

tools_c_args = [
  '-DG_LOG_DOMAIN="gaeguli-tools"',
  '-DGAEGULI_COMPILATION',
]

foreach tool: tools
  exe_name = '@0@-@1@'.format(tool, apiversion)
  src_file = '@0@.c'.format(tool)

  executable(exe_name,
    src_file,
    install: true,
    include_directories: gaeguli_incs,
    dependencies : [ libgaeguli_dep ],
    c_args: tools_c_args,
  )
  
endforeach
