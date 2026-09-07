include_guard(GLOBAL)
include(ExternalProject)

# A host archive may contain LLVM bitcode newer than the pinned frontend, or
# MSVC's proprietary /GL representation. Never ask LLVM 20 to parse either.
function(_neverc_cpp_host_audit_arguments output)
  if(MSVC)
    # COFF's linker-member tables contain the complete public definition index,
    # including /GL objects that no llvm-nm version can decode.
    set(${output} --host-format coff-index PARENT_SCOPE)
    return()
  endif()
  set(NEVERC_CPP_HOST_NM "" CACHE FILEPATH
    "Host archive reader compatible with the build compiler, including its LTO format")
  if(NEVERC_CPP_HOST_NM)
    get_filename_component(_host_nm "${NEVERC_CPP_HOST_NM}" ABSOLUTE)
  else()
    get_filename_component(_compiler "${CMAKE_CXX_COMPILER}" REALPATH)
    get_filename_component(_compiler_dir "${_compiler}" DIRECTORY)
    string(REGEX MATCH "^[0-9]+" _compiler_major "${CMAKE_CXX_COMPILER_VERSION}")
    unset(_host_nm)
    unset(_host_nm CACHE)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      find_program(_host_nm NAMES "llvm-nm-${_compiler_major}" llvm-nm
        PATHS "${_compiler_dir}" NO_DEFAULT_PATH)
      if(NOT _host_nm)
        find_program(_host_nm NAMES "llvm-nm-${_compiler_major}")
      endif()
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      # gcc-nm arranges the matching GCC LTO plugin for GNU nm.
      get_filename_component(_compiler_name "${_compiler}" NAME)
      string(REGEX REPLACE "(g\\+\\+|gcc)(-[0-9.]+)?$" "gcc-nm\\2"
        _gcc_nm_name "${_compiler_name}")
      find_program(_host_nm NAMES "${_gcc_nm_name}" "gcc-nm-${_compiler_major}" gcc-nm
        PATHS "${_compiler_dir}" NO_DEFAULT_PATH)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
      find_program(_host_nm NAMES llvm-nm PATHS "${_compiler_dir}" NO_DEFAULT_PATH)
    endif()
    if(NOT _host_nm)
      if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
        # Xcode generally ships Apple nm rather than llvm-nm. The private
        # reader can inspect ordinary Mach-O objects without an extra tool.
        # If its bitcode reader cannot read an Apple LTO archive, PRE_LINK
        # still fails closed and requests an explicit compatible reader.
        set(${output} --host-format nm PARENT_SCOPE)
        return()
      endif()
      message(FATAL_ERROR
        "Builtin C++ host ABI audit needs the build compiler's symbol reader. "
        "Install matching llvm-nm (Clang) or gcc-nm (GCC), or set "
        "NEVERC_CPP_HOST_NM explicitly.")
    endif()
  endif()
  execute_process(COMMAND "${_host_nm}" --version
    RESULT_VARIABLE _nm_status OUTPUT_VARIABLE _nm_version ERROR_VARIABLE _nm_error)
  if(NOT _nm_status EQUAL 0)
    message(FATAL_ERROR "Cannot inspect NEVERC_CPP_HOST_NM: ${_nm_error}")
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    string(REGEX MATCH "^[0-9]+" _compiler_major "${CMAKE_CXX_COMPILER_VERSION}")
    string(REGEX MATCH "LLVM version ([0-9]+)" _nm_match "${_nm_version}")
    if(NOT _nm_match OR NOT CMAKE_MATCH_1 STREQUAL _compiler_major)
      message(FATAL_ERROR
        "The host ABI audit requires llvm-nm ${_compiler_major} for this Clang "
        "toolchain. Set NEVERC_CPP_HOST_NM to its llvm-nm executable.")
    endif()
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND
         NOT _nm_version MATCHES "LLVM version")
    message(FATAL_ERROR "AppleClang host audit requires llvm-nm, not Apple nm")
  endif()
  set(${output} --host-format nm --host-nm "${_host_nm}" PARENT_SCOPE)
endfunction()

# Clang uses a private, pinned LLVM. The compiler's LLVM has a different ABI;
# importing upstream's CMake targets into this build would also mix headers.
function(neverc_setup_builtin_cpp_frontend)
  if(TARGET nevercCppFrontend)
    return()
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  _neverc_cpp_host_audit_arguments(_host_audit_arguments)
  set(NEVERC_CPP_LLVM_SOURCE_ARCHIVE "" CACHE FILEPATH
    "Offline llvm-project-20.1.8.src.tar.xz archive (the release hash is enforced)")
  set(NEVERC_CPP_BUILD_JOBS "4" CACHE STRING
    "Maximum parallel jobs for the private C++ frontend build")
  if(NOT NEVERC_CPP_BUILD_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "NEVERC_CPP_BUILD_JOBS must be a positive integer")
  endif()
  get_filename_component(_neverc_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../.." ABSOLUTE)
  set(_frontend "${_neverc_dir}/lib/Translate/Cpp/Frontend")
  set(_deps "${CMAKE_BINARY_DIR}/_deps")
  set(_build "${_deps}/neverc-cpp-build")
  set(_source "${_deps}/neverc-cpp-src")
  set(_prefix "${_build}/NeverCCppPrivatePrefix.h")
  # Debug affects the default MSVC CRT and STL iterator ABI, even across a C
  # entry point: standard-library template symbols remain process-wide.
  set(_private_config "$<IF:$<CONFIG:Debug>,Debug,Release>")
  set(_archive_name "${CMAKE_STATIC_LIBRARY_PREFIX}nevercCppFrontend${CMAKE_STATIC_LIBRARY_SUFFIX}")
  set(_archive "${_build}/lib/${_private_config}/${_archive_name}")
  set(_url "https://github.com/llvm/llvm-project/releases/download/llvmorg-20.1.8/llvm-project-20.1.8.src.tar.xz")
  if(NEVERC_CPP_LLVM_SOURCE_ARCHIVE)
    get_filename_component(_url "${NEVERC_CPP_LLVM_SOURCE_ARCHIVE}" ABSOLUTE)
    if(NOT EXISTS "${_url}")
      message(FATAL_ERROR "NEVERC_CPP_LLVM_SOURCE_ARCHIVE does not exist: ${_url}")
    endif()
  endif()
  if(MSVC)
    set(_prefix_flags "/FI\"${_prefix}\"")
  else()
    set(_prefix_flags "-include \"${_prefix}\"")
  endif()
  set(_platform_args)
  foreach(_var CMAKE_OSX_ARCHITECTURES CMAKE_OSX_SYSROOT
      CMAKE_OSX_DEPLOYMENT_TARGET CMAKE_MSVC_RUNTIME_LIBRARY
      CMAKE_C_COMPILER_TARGET CMAKE_CXX_COMPILER_TARGET CMAKE_SYSROOT
      LLVM_ENABLE_LIBCXX LLVM_STATIC_LINK_CXX_STDLIB)
    if(DEFINED ${_var} AND NOT "${${_var}}" STREQUAL "")
      string(REPLACE ";" "|" _value "${${_var}}")
      list(APPEND _platform_args "-D${_var}:STRING=${_value}")
    endif()
  endforeach()
  if(CMAKE_CROSSCOMPILING)
    list(APPEND _platform_args
      "-DCMAKE_SYSTEM_NAME:STRING=${CMAKE_SYSTEM_NAME}"
      "-DCMAKE_SYSTEM_PROCESSOR:STRING=${CMAKE_SYSTEM_PROCESSOR}")
  endif()
  cmake_policy(PUSH)
  if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
  endif()
  ExternalProject_Add(nevercCppFrontendBuild
    URL "${_url}"
    URL_HASH SHA256=6898f963c8e938981e6c4a302e83ec5beb4630147c7311183cf61069af16333d
    DOWNLOAD_DIR "${_deps}/neverc-cpp-downloads"
    SOURCE_DIR "${_source}"
    BINARY_DIR "${_build}"
    SOURCE_SUBDIR llvm
    LIST_SEPARATOR "|"
    PATCH_COMMAND "${Python3_EXECUTABLE}" "${_frontend}/IsolateSymbols.py"
      --source <SOURCE_DIR> --output "${_prefix}"
    CMAKE_ARGS
      "-DCMAKE_BUILD_TYPE:STRING=${_private_config}"
      "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
      "-DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}"
      "-DCMAKE_C_FLAGS:STRING=${_prefix_flags}"
      "-DCMAKE_CXX_FLAGS:STRING=${_prefix_flags}"
      "-DPython3_EXECUTABLE:FILEPATH=${Python3_EXECUTABLE}"
      "-DLLVM_ENABLE_PROJECTS:STRING=clang"
      "-DLLVM_EXTERNAL_PROJECTS:STRING=neverc_cpp"
      "-DLLVM_EXTERNAL_NEVERC_CPP_SOURCE_DIR:PATH=${_frontend}"
      "-DNEVERC_CPP_REPOSITORY_ROOT:PATH=${_neverc_dir}/.."
      "-DLLVM_TARGETS_TO_BUILD:STRING="
      "-DLLVM_DEFAULT_TARGET_TRIPLE:STRING=${LLVM_DEFAULT_TARGET_TRIPLE}"
      "-DLLVM_ENABLE_RUNTIMES:STRING="
      "-DLLVM_BUILD_LLVM_DYLIB:BOOL=OFF"
      "-DLLVM_LINK_LLVM_DYLIB:BOOL=OFF"
      "-DCLANG_LINK_CLANG_DYLIB:BOOL=OFF"
      "-DBUILD_SHARED_LIBS:BOOL=OFF"
      "-DLLVM_ENABLE_PLUGINS:BOOL=OFF"
      "-DCLANG_PLUGIN_SUPPORT:BOOL=OFF"
      "-DLLVM_DISABLE_ASSEMBLY_FILES:BOOL=ON"
      "-DLLVM_ENABLE_ZLIB:STRING=OFF"
      "-DLLVM_ENABLE_ZSTD:STRING=OFF"
      "-DLLVM_ENABLE_LIBXML2:STRING=OFF"
      "-DLLVM_ENABLE_LIBEDIT:BOOL=OFF"
      "-DLLVM_ENABLE_LIBPFM:BOOL=OFF"
      "-DLLVM_ENABLE_CURL:STRING=OFF"
      "-DLLVM_ENABLE_HTTPLIB:STRING=OFF"
      "-DLLVM_ENABLE_Z3_SOLVER:BOOL=OFF"
      "-DLLVM_INCLUDE_TESTS:BOOL=OFF"
      "-DLLVM_INCLUDE_BENCHMARKS:BOOL=OFF"
      "-DLLVM_INCLUDE_EXAMPLES:BOOL=OFF"
      "-DLLVM_BUILD_TOOLS:BOOL=OFF"
      "-DCLANG_BUILD_TOOLS:BOOL=OFF"
      "-DCLANG_INCLUDE_TESTS:BOOL=OFF"
      ${_platform_args}
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR>
      --config "${_private_config}" --target nevercCppFrontendBundle
      --parallel "${NEVERC_CPP_BUILD_JOBS}"
    BUILD_ALWAYS TRUE
    BUILD_BYPRODUCTS "${_archive}"
    INSTALL_COMMAND "")
  ExternalProject_Add_StepDependencies(nevercCppFrontendBuild patch
    "${_frontend}/IsolateSymbols.py")
  cmake_policy(POP)
  add_library(nevercCppFrontend STATIC IMPORTED GLOBAL)
  set_target_properties(nevercCppFrontend PROPERTIES
    IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
    IMPORTED_LOCATION "${_build}/lib/Release/${_archive_name}"
    IMPORTED_LOCATION_DEBUG "${_build}/lib/Debug/${_archive_name}"
    IMPORTED_LOCATION_RELEASE "${_build}/lib/Release/${_archive_name}"
    NEVERC_CPP_AUDIT_PYTHON "${Python3_EXECUTABLE}"
    NEVERC_CPP_AUDIT_SCRIPT "${_frontend}/AuditArchive.py"
    NEVERC_CPP_AUDIT_HOST_ARGUMENTS "${_host_audit_arguments}"
    NEVERC_CPP_AUDIT_NM_FILE "${_build}/neverc-cpp-nm-${_private_config}.txt"
    NEVERC_CPP_AUDIT_PREFIX "${_prefix}")
  # Cover built-in and user-provided host configurations, including the empty
  # single-config default, without ever mapping a Release build to Debug.
  foreach(_host_config IN LISTS CMAKE_CONFIGURATION_TYPES CMAKE_BUILD_TYPE)
    string(TOUPPER "${_host_config}" _host_config_upper)
    if(_host_config_upper STREQUAL "DEBUG")
      set(_mapped_config DEBUG)
    else()
      set(_mapped_config RELEASE)
    endif()
    set_property(TARGET nevercCppFrontend PROPERTY
      "MAP_IMPORTED_CONFIG_${_host_config_upper}" "${_mapped_config}")
  endforeach()
  set_target_properties(nevercCppFrontend PROPERTIES
    MAP_IMPORTED_CONFIG_NOCONFIG RELEASE
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
    MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE)
  add_dependencies(nevercCppFrontend nevercCppFrontendBuild)
  if(WIN32)
    target_link_libraries(nevercCppFrontend INTERFACE
      psapi shell32 ole32 uuid advapi32 ws2_32 ntdll version)
  else()
    find_package(Threads REQUIRED)
    target_link_libraries(nevercCppFrontend INTERFACE Threads::Threads ${CMAKE_DL_LIBS} m)
  endif()
  install(FILES "${_source}/LICENSE.TXT"
    DESTINATION "share/neverc/licenses/llvm-project-20.1.8" COMPONENT neverc)
  install(FILES "${_source}/llvm/lib/Support/BLAKE3/LICENSE"
    DESTINATION "share/neverc/licenses/llvm-project-20.1.8/BLAKE3" COMPONENT neverc)
  install(FILES "${_build}/NeverCCppThirdPartyNotices.txt"
    "${_frontend}/UPSTREAM-SOURCE.txt"
    DESTINATION "share/neverc/licenses/llvm-project-20.1.8" COMPONENT neverc)
  install(DIRECTORY "${_neverc_dir}/lib/Translate/Cpp/SDK/"
    DESTINATION "share/neverc/licenses/cpp-sdk" COMPONENT neverc
    PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE)
endfunction()

# Call in the directory that creates the final executable, after add_executable.
# Its ordinary link dependencies build the host archives before PRE_LINK runs.
# Checking both private definitions and references prevents a symbol silently
# binding to the host LLVM even if the static linker would accept the program.
function(neverc_check_builtin_cpp_frontend target)
  if(NOT TARGET "${target}" OR NOT TARGET nevercCppFrontend OR NOT TARGET LLVMCore)
    message(FATAL_ERROR "Builtin C++ ABI audit requires the executable and both LLVM builds")
  endif()
  foreach(_property PYTHON SCRIPT NM_FILE PREFIX HOST_ARGUMENTS)
    get_target_property(_audit_${_property} nevercCppFrontend
      "NEVERC_CPP_AUDIT_${_property}")
  endforeach()
  add_custom_command(TARGET "${target}" PRE_LINK
    COMMAND "${_audit_PYTHON}" "${_audit_SCRIPT}"
      --nm-file "${_audit_NM_FILE}"
      --archive "$<TARGET_FILE:nevercCppFrontend>"
      --prefix-header "${_audit_PREFIX}"
      --host-lib-dir "$<TARGET_FILE_DIR:LLVMCore>"
      ${_audit_HOST_ARGUMENTS}
    VERBATIM)
  set_property(TARGET "${target}" APPEND PROPERTY LINK_DEPENDS
    "${_audit_SCRIPT}"
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../lib/Translate/Cpp/Frontend/HostCoffSymbols.py")
endfunction()
