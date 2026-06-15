# Copy a runtime asset tree into the build/install staging area.
#
# Default (DEREFERENCE_SYMLINKS=OFF): preserve symlinks as-is via file(COPY).
# Linux sysroots use absolute usr/lib/*.so -> /lib/... links; following them
# on macOS/Windows would copy nothing useful.
#
# DEREFERENCE_SYMLINKS=ON: resolve relative symlinks to real files/directories.
# macOS SDK frameworks (Headers -> Versions/Current/...) and usr/lib/*.tbd stubs
# rely on in-tree relative links; materialising them makes the tree portable on
# Windows hosts and fixes 7z packaging in CI.
if(NOT DEFINED SRC OR NOT DEFINED DST)
  message(FATAL_ERROR "copy_runtime_tree.cmake requires -DSRC=... -DDST=...")
endif()

function(_runtime_copy_tree _src _dst)
  if(IS_SYMLINK "${_src}")
    file(READ_SYMLINK "${_src}" _link_tgt)
    if(IS_ABSOLUTE "${_link_tgt}")
      message(FATAL_ERROR
        "copy_runtime_tree: cannot dereference absolute symlink "
        "${_src} -> ${_link_tgt}")
    endif()
    get_filename_component(_resolved "${_src}/../${_link_tgt}" ABSOLUTE)
    if(NOT EXISTS "${_resolved}")
      message(WARNING
        "copy_runtime_tree: skipping broken symlink ${_src} -> ${_link_tgt}")
      return()
    endif()
    _runtime_copy_tree("${_resolved}" "${_dst}")
  elseif(IS_DIRECTORY "${_src}")
    file(MAKE_DIRECTORY "${_dst}")
    file(GLOB _entries RELATIVE "${_src}" "${_src}/*")
    foreach(_entry IN LISTS _entries)
      _runtime_copy_tree("${_src}/${_entry}" "${_dst}/${_entry}")
    endforeach()
  else()
    get_filename_component(_dst_parent "${_dst}" DIRECTORY)
    get_filename_component(_dst_name "${_dst}" NAME)
    file(MAKE_DIRECTORY "${_dst_parent}")
    file(COPY "${_src}" DESTINATION "${_dst_parent}" USE_SOURCE_PERMISSIONS)
    get_filename_component(_src_name "${_src}" NAME)
    if(NOT _src_name STREQUAL _dst_name)
      file(RENAME "${_dst_parent}/${_src_name}" "${_dst}")
    endif()
  endif()
endfunction()

if(DEREFERENCE_SYMLINKS)
  _runtime_copy_tree("${SRC}" "${DST}")
else()
  file(COPY "${SRC}/" DESTINATION "${DST}" USE_SOURCE_PERMISSIONS)
endif()
