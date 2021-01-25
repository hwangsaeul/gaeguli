gaeguli_target_worker_sources = [
  'gaeguli-target-worker.c',
]

gaeguli_target_worker_c_args = [
  '-DHAVE_CONFIG_H',
  '-D_GNU_SOURCE',
  '-DG_LOG_DOMAIN="gaeguli_target_worker"',
]

executable('gaeguli_target_worker',
  gaeguli_target_worker_sources,
  install: true,
  c_args : gaeguli_target_worker_c_args,
  include_directories : [gaeguli_incs],
  dependencies : [libgaeguli_dep],
  install_dir : gaeguli_bindir
)

gaeguli_pipeline_worker_sources = [
  'gaeguli-pipeline-worker.c',
]

gaeguli_pipeline_worker_c_args = [
  '-DHAVE_CONFIG_H',
  '-D_GNU_SOURCE',
  '-DG_LOG_DOMAIN="gaeguli_pipeline_worker"',
]

executable('gaeguli_pipeline_worker',
  gaeguli_pipeline_worker_sources,
  install: true,
  c_args : gaeguli_pipeline_worker_c_args,
  include_directories : [gaeguli_incs],
  dependencies : [libgaeguli_dep],
  install_dir : gaeguli_bindir
)