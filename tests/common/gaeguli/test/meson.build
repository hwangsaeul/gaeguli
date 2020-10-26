sources = [
  'receiver.c',
]

headers = [
  'receiver.h',
]

libgaeguli_test_common_includes = include_directories('../..')

libgaeguli_test_common = library(
  'gaeguli-test-common-@0@'.format(apiversion),
  sources,
  version: libversion,
  soversion: soversion,
  include_directories: libgaeguli_test_common_includes,
  dependencies: [ libgaeguli_dep ],
  install: true
)

install_headers(headers, subdir: join_paths(gaeguli_install_header_subdir, 'test'))

pkg.generate(libgaeguli_test_common,
  description : 'A SRT Video Streaming Library (test ancillaries)',
)

libgaeguli_test_common_dep = declare_dependency(
  link_with: libgaeguli_test_common,
  include_directories: libgaeguli_test_common_includes
)
