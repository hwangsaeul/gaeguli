subdir('resources')

sources = [
  'adaptor-demo.c',
  'http-server.c',
  'main.c',
  'traffic-control.c',
  gresources
]

executable('gaeguli-adaptor-demo', sources,
  dependencies: [
    libgaeguli_dep,
    dependency('libsoup-2.4'),
    dependency('json-glib-1.0'),
    dependency('libnl-route-3.0'),
  ],
)
