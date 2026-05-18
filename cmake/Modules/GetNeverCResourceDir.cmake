# get NeverC resource directory
#
# usage:
#  get_neverc_resource_dir(out_var [PREFIX prefix] [SUBDIR subdirectory])
#
# user can use `PREFIX` to prepend some path to it or use `SUBDIR` to
# get subdirectory under NeverC resource dir

function(get_neverc_resource_dir out_var)
  cmake_parse_arguments(ARG "" "PREFIX;SUBDIR" "" ${ARGN})

  if(DEFINED NEVERC_RESOURCE_DIR AND NOT NEVERC_RESOURCE_DIR STREQUAL "")
    set(ret_dir bin/${NEVERC_RESOURCE_DIR})
  else()
    if (NOT NEVERC_VERSION_MAJOR)
      string(REGEX MATCH "^[0-9]+" NEVERC_VERSION_MAJOR ${PACKAGE_VERSION})
    endif()
    set(ret_dir lib${LLVM_LIBDIR_SUFFIX}/neverc/${NEVERC_VERSION_MAJOR})
  endif()

  if(ARG_PREFIX)
    set(ret_dir ${ARG_PREFIX}/${ret_dir})
  endif()
  if(ARG_SUBDIR)
    set(ret_dir ${ret_dir}/${ARG_SUBDIR})
  endif()

  set(${out_var} ${ret_dir} PARENT_SCOPE)
endfunction()
