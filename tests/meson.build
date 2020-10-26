subdir('common/gaeguli/test')
subdir('adaptor-demo')

tests = [
  'test-pipeline',
  'test-adaptor',
  'test-target',
]

foreach t: tests
  installed_test = '@0@.test'.format(t)

  exe = executable(
    t, '@0@.c'.format(t),
    c_args: '-DG_LOG_DOMAIN="gaeguli-tests"',
    dependencies: [ libgaeguli_dep, libgaeguli_test_common_dep ],
    install: false,
  )

  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())

  test(
    t, exe,
    args: [ '--tap', '-k' ],
    env: env,
    timeout: 120,
    is_parallel: false
  )
endforeach

debugenv = environment()
debugenv.set('GST_DEBUG', '3')
add_test_setup('debug', env: debugenv)
