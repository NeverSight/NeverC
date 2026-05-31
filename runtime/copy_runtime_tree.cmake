# Copy a runtime asset tree without following symlinks.
# cmake -E copy_directory resolves symlinks and fails on Linux sysroots
# whose usr/lib/.../*.so links point at absolute /lib/... paths (valid on
# Linux, broken on macOS/Windows).  file(COPY) preserves symlinks as-is.
if(NOT DEFINED SRC OR NOT DEFINED DST)
  message(FATAL_ERROR "copy_runtime_tree.cmake requires -DSRC=... -DDST=...")
endif()
file(COPY "${SRC}/" DESTINATION "${DST}" USE_SOURCE_PERMISSIONS)
