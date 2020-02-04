# We only run a small set of tests on Windows for now.
# Override the parent directory's "unsupported" decision until we can handle
# all of its tests.
if config.host_os in ['Windows']:
  config.unsupported = False
else:
  config.unsupported = True
