subdir('resources')

sources = [
  'adaptor-demo.c',
  'http-server.c',
  'main.c',
  gresources
]

executable('gaeguli-adaptor-demo', sources,
  dependencies: [
    libgaeguli_dep,
    dependency('libsoup-2.4'),
    dependency('json-glib-1.0')
  ],
)
