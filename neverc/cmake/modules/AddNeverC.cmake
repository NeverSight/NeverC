include(GNUInstallDirs)
include(LLVMDistributionSupport)

macro(set_neverc_windows_version_resource_properties name)
  if(DEFINED windows_resource_file)
    set_windows_version_resource_properties(${name} ${windows_resource_file}
      VERSION_MAJOR ${NEVERC_VERSION_MAJOR}
      VERSION_MINOR ${NEVERC_VERSION_MINOR}
      VERSION_PATCHLEVEL ${NEVERC_VERSION_PATCHLEVEL}
      VERSION_STRING "${NEVERC_VERSION}"
      PRODUCT_NAME "neverc")
  endif()
endmacro()

macro(add_neverc_subdirectory name)
  add_llvm_subdirectory(NEVERC TOOL ${name})
endmacro()

macro(add_neverc_library name)
  cmake_parse_arguments(ARG
    "SHARED;STATIC;INSTALL_WITH_TOOLCHAIN"
    ""
    "ADDITIONAL_HEADERS"
    ${ARGN})
  set(srcs)
  if(MSVC_IDE OR XCODE)
    # Add public headers
    file(RELATIVE_PATH lib_path
      ${NEVERC_SOURCE_DIR}/lib/
      ${CMAKE_CURRENT_SOURCE_DIR}
    )
    if(NOT lib_path MATCHES "^[.][.]")
      file( GLOB_RECURSE headers
        ${NEVERC_SOURCE_DIR}/include/neverc/${lib_path}/*.h
        ${NEVERC_SOURCE_DIR}/include/neverc/${lib_path}/*.def
      )
      set_source_files_properties(${headers} PROPERTIES HEADER_FILE_ONLY ON)

      if(headers)
        set(srcs ${headers})
      endif()
    endif()
  endif(MSVC_IDE OR XCODE)
  if(srcs OR ARG_ADDITIONAL_HEADERS)
    set(srcs
      ADDITIONAL_HEADERS
      ${srcs}
      ${ARG_ADDITIONAL_HEADERS} # It may contain unparsed unknown args.
      )
  endif()

  if(ARG_SHARED AND ARG_STATIC)
    set(LIBTYPE SHARED STATIC)
  elseif(ARG_SHARED)
    set(LIBTYPE SHARED)
  else()
    # llvm_add_library ignores BUILD_SHARED_LIBS if STATIC is explicitly set,
    # so we need to handle it here.
    if(BUILD_SHARED_LIBS)
      set(LIBTYPE SHARED)
    else()
      set(LIBTYPE STATIC)
    endif()
    if(NOT XCODE)
      # The Xcode generator doesn't handle object libraries correctly.
      list(APPEND LIBTYPE OBJECT)
    endif()
    set_property(GLOBAL APPEND PROPERTY NEVERC_STATIC_LIBS ${name})
  endif()
  llvm_add_library(${name} ${LIBTYPE} ${ARG_UNPARSED_ARGUMENTS} ${srcs})

  set(libs ${name})
  if(ARG_SHARED AND ARG_STATIC)
    list(APPEND libs ${name}_static)
  endif()

  foreach(lib ${libs})
    if(TARGET ${lib})
      target_link_libraries(${lib} INTERFACE ${LLVM_COMMON_LIBS})

      if (NOT LLVM_INSTALL_TOOLCHAIN_ONLY OR ARG_INSTALL_WITH_TOOLCHAIN)
        get_target_export_arg(${name} NeverC export_to_neverctargets UMBRELLA neverc-libraries)
        install(TARGETS ${lib}
          COMPONENT ${lib}
          ${export_to_neverctargets}
          LIBRARY DESTINATION lib${LLVM_LIBDIR_SUFFIX}
          ARCHIVE DESTINATION lib${LLVM_LIBDIR_SUFFIX}
          RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

        if (NOT LLVM_ENABLE_IDE)
          add_llvm_install_targets(install-${lib}
                                   DEPENDS ${lib}
                                   COMPONENT ${lib})
        endif()

        set_property(GLOBAL APPEND PROPERTY NEVERC_LIBS ${lib})
      endif()
      set_property(GLOBAL APPEND PROPERTY NEVERC_EXPORTS ${lib})
    else()
      # Add empty "phony" target
      add_custom_target(${lib})
    endif()
  endforeach()

  set_target_properties(${name} PROPERTIES FOLDER "NeverC libraries")
  set_neverc_windows_version_resource_properties(${name})
endmacro(add_neverc_library)

macro(add_neverc_executable name)
  add_llvm_executable( ${name} ${ARGN} )
  set_target_properties(${name} PROPERTIES FOLDER "NeverC executables")
  set_neverc_windows_version_resource_properties(${name})
endmacro(add_neverc_executable)

macro(add_neverc_tool name)
  cmake_parse_arguments(ARG "DEPENDS;GENERATE_DRIVER" "" "" ${ARGN})
  if (NOT NEVERC_BUILD_TOOLS)
    set(EXCLUDE_FROM_ALL ON)
  endif()
  if(ARG_GENERATE_DRIVER
     AND LLVM_TOOL_LLVM_DRIVER_BUILD
     AND (NOT LLVM_DISTRIBUTION_COMPONENTS OR ${name} IN_LIST LLVM_DISTRIBUTION_COMPONENTS)
    )
    set(get_obj_args ${ARGN})
    list(FILTER get_obj_args EXCLUDE REGEX "^SUPPORT_PLUGINS$")
    generate_llvm_objects(${name} ${get_obj_args})
    add_custom_target(${name} DEPENDS llvm-driver neverc-resource-headers)
  else()
    add_neverc_executable(${name} ${ARGN})
    add_dependencies(${name} neverc-resource-headers)

    if (NEVERC_BUILD_TOOLS)
      get_target_export_arg(${name} NeverC export_to_neverctargets)
      install(TARGETS ${name}
        ${export_to_neverctargets}
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT ${name})

      if(NOT LLVM_ENABLE_IDE)
        add_llvm_install_targets(install-${name}
                                 DEPENDS ${name}
                                 COMPONENT ${name})
      endif()
      set_property(GLOBAL APPEND PROPERTY NEVERC_EXPORTS ${name})
    endif()
  endif()
endmacro()

macro(add_neverc_symlink name dest)
  get_property(LLVM_DRIVER_TOOLS GLOBAL PROPERTY LLVM_DRIVER_TOOLS)
  if(LLVM_TOOL_LLVM_DRIVER_BUILD
     AND ${dest} IN_LIST LLVM_DRIVER_TOOLS
     AND (NOT LLVM_DISTRIBUTION_COMPONENTS OR ${dest} IN_LIST LLVM_DISTRIBUTION_COMPONENTS)
    )
    set_property(GLOBAL APPEND PROPERTY LLVM_DRIVER_TOOL_ALIASES_${dest} ${name})
  else()
    llvm_add_tool_symlink(NEVERC ${name} ${dest} ALWAYS_GENERATE)
    llvm_install_symlink(NEVERC ${name} ${dest} ALWAYS_GENERATE)
  endif()
endmacro()

function(neverc_target_link_libraries target type)
  if (TARGET obj.${target})
    target_link_libraries(obj.${target} ${ARGN})
  endif()

  get_property(LLVM_DRIVER_TOOLS GLOBAL PROPERTY LLVM_DRIVER_TOOLS)
  if(LLVM_TOOL_LLVM_DRIVER_BUILD AND ${target} IN_LIST LLVM_DRIVER_TOOLS)
    set(target llvm-driver)
  endif()

  if (NEVERC_LINK_DYLIB)
    target_link_libraries(${target} ${type} neverc-cpp)
  else()
    target_link_libraries(${target} ${type} ${ARGN})
  endif()
endfunction()
