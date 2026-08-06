# Provision the exact CPython development/runtime distribution used by NeverC
# Python plugins. Python3_EXECUTABLE remains independent build-tool Python.

include_guard(GLOBAL)

set(NEVERC_MANAGED_CPYTHON_VERSION "3.12.10")
set(NEVERC_MANAGED_CPYTHON_RELEASE "20250409")
set(_NEVERC_MANAGED_CPYTHON_LICENSE_SOURCE_COMMIT
    "186813f11c67ce8f6b424b4c355f7052204a04cc")
set(_NEVERC_MANAGED_CPYTHON_LICENSE_SOURCE_SHA256
    "df5b86f1c04f5ad2342dc5e4d141db1e6be83ebae9d6bf3469823d583302fa28")

function(_neverc_managed_cpython_normalize_arch value output)
  string(TOLOWER "${value}" _arch)
  if(_arch MATCHES "^(arm64|aarch64)$")
    set(_arch "arm64")
  elseif(_arch MATCHES "^(amd64|x86_64|x64)$")
    set(_arch "x86_64")
  endif()
  set(${output} "${_arch}" PARENT_SCOPE)
endfunction()

function(_neverc_managed_cpython_comparison_path value system output)
  # CMake spells native Windows paths with '/', while the managed interpreter
  # reports os.path.realpath() with '\\'. Normalize both representations before
  # applying containment or equality checks. Windows path comparisons are also
  # case-insensitive.
  set(_path "${value}")
  string(REPLACE "\\" "/" _path "${_path}")
  cmake_path(NORMAL_PATH _path)
  if(system STREQUAL "Windows")
    string(TOLOWER "${_path}" _path)
  endif()
  set(${output} "${_path}" PARENT_SCOPE)
endfunction()

function(neverc_managed_cpython_artifact
         system processor output_url output_sha256 output_layout
         output_archive_name)
  unset(_url)
  unset(_archive_name)
  unset(_layout)
  _neverc_managed_cpython_normalize_arch("${processor}" _arch)
  set(_pbs_base
      "https://github.com/astral-sh/python-build-standalone/releases/download/${NEVERC_MANAGED_CPYTHON_RELEASE}")
  set(_pbs_prefix
      "cpython-${NEVERC_MANAGED_CPYTHON_VERSION}%2B${NEVERC_MANAGED_CPYTHON_RELEASE}")

  if(system STREQUAL "Darwin" AND _arch STREQUAL "arm64")
    set(_artifact "aarch64-apple-darwin")
    set(_sha256
        "0be1fe0b35a4d3c382141764ef16ed3b8cc2b4620b657f678daa7b7f8df39699")
  elseif(system STREQUAL "Darwin" AND _arch STREQUAL "x86_64")
    set(_artifact "x86_64-apple-darwin")
    set(_sha256
        "ad3bef94b6054adcf8e0a47886e21b00dfc6a37f22eea229cf0f8725bd0e1023")
  elseif(system STREQUAL "Linux" AND _arch STREQUAL "arm64")
    set(_artifact "aarch64-unknown-linux-gnu")
    set(_sha256
        "e1f450b77b81a250411855bb5e5cbd0f0acbc9ad46b5ea97f224452831bb3276")
  elseif(system STREQUAL "Linux" AND _arch STREQUAL "x86_64")
    set(_artifact "x86_64-unknown-linux-gnu")
    set(_sha256
        "8c59b9ac6bff2dc3934181d7bc82594f9f59a613afed8d72c9e89d7194e790ee")
  elseif(system STREQUAL "Windows" AND _arch STREQUAL "arm64")
    set(_artifact "arm64")
    set(_archive_name "python-${NEVERC_MANAGED_CPYTHON_VERSION}-arm64.zip")
    set(_url
        "https://www.python.org/ftp/python/${NEVERC_MANAGED_CPYTHON_VERSION}/${_archive_name}")
    set(_sha256
        "20a5b1a707d899ffdfc5e3086d7372f7cc95eeea344d48ae256047cb7075cf63")
    set(_layout "windows")
  elseif(system STREQUAL "Windows" AND _arch STREQUAL "x86_64")
    set(_artifact "amd64")
    set(_archive_name "python-${NEVERC_MANAGED_CPYTHON_VERSION}-amd64.zip")
    set(_url
        "https://www.python.org/ftp/python/${NEVERC_MANAGED_CPYTHON_VERSION}/${_archive_name}")
    set(_sha256
        "8649692de846c56a7189d6dae5c322ab20deb1b5908b6f39426b62a36f39415d")
    set(_layout "windows")
  else()
    message(FATAL_ERROR
      "NeverC managed CPython ${NEVERC_MANAGED_CPYTHON_VERSION} has no native "
      "distribution for ${system}/${processor}. Supported hosts are macOS, "
      "Linux, and Windows on x86_64 or arm64.")
  endif()

  if(NOT DEFINED _url)
    set(_archive_name
        "cpython-${NEVERC_MANAGED_CPYTHON_VERSION}+${NEVERC_MANAGED_CPYTHON_RELEASE}-${_artifact}-install_only_stripped.tar.gz")
    set(_url
        "${_pbs_base}/${_pbs_prefix}-${_artifact}-install_only_stripped.tar.gz")
    set(_layout "unix")
  endif()

  set(${output_url} "${_url}" PARENT_SCOPE)
  set(${output_sha256} "${_sha256}" PARENT_SCOPE)
  set(${output_layout} "${_layout}" PARENT_SCOPE)
  set(${output_archive_name} "${_archive_name}" PARENT_SCOPE)
endfunction()

function(_neverc_managed_cpython_paths
         root system output_executable output_include output_library
         output_runtime output_stdlib)
  if(system STREQUAL "Windows")
    set(_executable "${root}/python.exe")
    set(_include "${root}/include")
    set(_library "${root}/libs/python312.lib")
    set(_runtime "${root}/python312.dll")
    set(_stdlib "${root}/Lib")
  elseif(system STREQUAL "Darwin")
    set(_executable "${root}/bin/python3.12")
    set(_include "${root}/include/python3.12")
    set(_library "${root}/lib/libpython3.12.dylib")
    set(_runtime "${_library}")
    set(_stdlib "${root}/lib/python3.12")
  else()
    set(_executable "${root}/bin/python3.12")
    set(_include "${root}/include/python3.12")
    if(EXISTS "${root}/lib/libpython3.12.so.1.0")
      set(_library "${root}/lib/libpython3.12.so.1.0")
    else()
      set(_library "${root}/lib/libpython3.12.so")
    endif()
    set(_runtime "${_library}")
    set(_stdlib "${root}/lib/python3.12")
  endif()

  set(${output_executable} "${_executable}" PARENT_SCOPE)
  set(${output_include} "${_include}" PARENT_SCOPE)
  set(${output_library} "${_library}" PARENT_SCOPE)
  set(${output_runtime} "${_runtime}" PARENT_SCOPE)
  set(${output_stdlib} "${_stdlib}" PARENT_SCOPE)
endfunction()

function(_neverc_managed_cpython_require_files root system)
  _neverc_managed_cpython_paths(
    "${root}" "${system}"
    _executable _include _library _runtime _stdlib)
  set(_required
      "${_executable}"
      "${_include}/Python.h"
      "${_include}/pyconfig.h"
      "${_library}"
      "${_runtime}"
      "${_stdlib}/os.py")
  foreach(_path IN LISTS _required)
    if(NOT EXISTS "${_path}")
      message(FATAL_ERROR
        "NeverC managed CPython ${NEVERC_MANAGED_CPYTHON_VERSION} is incomplete: "
        "required artifact is missing: ${_path}")
    endif()
  endforeach()
endfunction()

function(_neverc_managed_cpython_fingerprint root system output)
  _neverc_managed_cpython_paths(
    "${root}" "${system}"
    _executable _include _library _runtime _stdlib)
  _neverc_managed_cpython_require_files("${root}" "${system}")
  set(_fingerprint_input "")
  foreach(_path IN ITEMS
      "${_executable}"
      "${_include}/Python.h"
      "${_include}/pyconfig.h"
      "${_library}"
      "${_runtime}"
      "${_stdlib}/os.py")
    file(SHA256 "${_path}" _sha256)
    string(APPEND _fingerprint_input "${_path}|${_sha256}\n")
  endforeach()
  string(SHA256 _fingerprint "${_fingerprint_input}")
  set(${output} "${_fingerprint}" PARENT_SCOPE)
endfunction()

function(_neverc_managed_cpython_download url sha256 destination label)
  get_filename_component(_directory "${destination}" DIRECTORY)
  file(MAKE_DIRECTORY "${_directory}")
  if(EXISTS "${destination}")
    file(SHA256 "${destination}" _actual_sha256)
    if(NOT _actual_sha256 STREQUAL sha256)
      file(REMOVE "${destination}")
    endif()
  endif()
  if(NOT EXISTS "${destination}")
    message(STATUS
      "NeverC: downloading ${label} for managed CPython "
      "${NEVERC_MANAGED_CPYTHON_VERSION}")
    file(DOWNLOAD
      "${url}" "${destination}"
      EXPECTED_HASH "SHA256=${sha256}"
      STATUS _status
      TLS_VERIFY ON
      SHOW_PROGRESS)
    list(GET _status 0 _result)
    list(GET _status 1 _detail)
    if(NOT _result EQUAL 0)
      file(REMOVE "${destination}")
      message(FATAL_ERROR
        "NeverC could not download ${label}: ${_detail}. For an offline "
        "build, set NEVERC_MANAGED_PYTHON_ROOT to a prepared exact CPython "
        "${NEVERC_MANAGED_CPYTHON_VERSION} development/runtime tree.")
    endif()
  endif()
  file(SHA256 "${destination}" _actual_sha256)
  if(NOT _actual_sha256 STREQUAL sha256)
    message(FATAL_ERROR
      "NeverC rejected ${label}: expected SHA-256 ${sha256}, got "
      "${_actual_sha256}")
  endif()
endfunction()

function(_neverc_managed_cpython_copy_licenses destination download_dir)
  set(_archive_name
      "python-build-standalone-${_NEVERC_MANAGED_CPYTHON_LICENSE_SOURCE_COMMIT}.tar.gz")
  set(_archive "${download_dir}/${_archive_name}")
  set(_url
      "https://github.com/astral-sh/python-build-standalone/archive/${_NEVERC_MANAGED_CPYTHON_LICENSE_SOURCE_COMMIT}.tar.gz")
  _neverc_managed_cpython_download(
    "${_url}"
    "${_NEVERC_MANAGED_CPYTHON_LICENSE_SOURCE_SHA256}"
    "${_archive}"
    "CPython distribution license bundle")

  string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _nonce)
  set(_extract "${CMAKE_BINARY_DIR}/_deps/neverc-cpython-licenses-${_nonce}")
  file(MAKE_DIRECTORY "${_extract}")
  file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_extract}")
  file(GLOB _license_files
       "${_extract}/python-build-standalone-*/LICENSE*.txt")
  if(NOT _license_files)
    file(REMOVE_RECURSE "${_extract}")
    message(FATAL_ERROR
      "NeverC managed CPython license archive contains no LICENSE*.txt files")
  endif()
  file(MAKE_DIRECTORY "${destination}/licenses")
  file(COPY ${_license_files} DESTINATION "${destination}/licenses")
  file(REMOVE_RECURSE "${_extract}")
endfunction()

function(_neverc_managed_cpython_repair_macos root library)
  find_program(_install_name_tool install_name_tool REQUIRED)
  find_program(_codesign codesign REQUIRED)
  execute_process(
    COMMAND "${_install_name_tool}" -id "@rpath/libpython3.12.dylib"
            "${library}"
    RESULT_VARIABLE _result
    ERROR_VARIABLE _error)
  if(NOT _result EQUAL 0)
    string(STRIP "${_error}" _error)
    message(FATAL_ERROR
      "NeverC could not make managed CPython relocatable: ${_error}")
  endif()
  execute_process(
    COMMAND "${_codesign}" --force --sign - "${library}"
    RESULT_VARIABLE _result
    ERROR_VARIABLE _error)
  if(NOT _result EQUAL 0)
    string(STRIP "${_error}" _error)
    message(FATAL_ERROR
      "NeverC could not ad-hoc sign managed CPython: ${_error}")
  endif()
endfunction()

function(_neverc_managed_cpython_validate_interpreter
         root system processor executable include_dir stdlib_dir)
  set(_script "${CMAKE_BINARY_DIR}/CMakeFiles/neverc-managed-cpython-check.py")
  file(WRITE "${_script}" [=[
import os
import platform
import sys
import sysconfig

print(platform.python_version())
print(platform.python_implementation())
print(platform.machine())
print(os.path.realpath(sys.base_prefix))
print(os.path.realpath(sysconfig.get_path("include")))
print(os.path.realpath(sysconfig.get_path("stdlib")))
]=])
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            --unset=PYTHONHOME --unset=PYTHONPATH --unset=PYTHONUSERBASE
            "${executable}" -I -S "${_script}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
  if(NOT _result EQUAL 0)
    string(STRIP "${_error}" _error)
    message(FATAL_ERROR
      "NeverC rejected managed CPython interpreter ${executable}: ${_error}")
  endif()
  string(REPLACE "\r\n" "\n" _output "${_output}")
  string(STRIP "${_output}" _output)
  string(REPLACE "\n" ";" _fields "${_output}")
  list(LENGTH _fields _field_count)
  if(NOT _field_count EQUAL 6)
    message(FATAL_ERROR
      "NeverC rejected managed CPython interpreter metadata: ${_output}")
  endif()
  list(GET _fields 0 _version)
  list(GET _fields 1 _implementation)
  list(GET _fields 2 _runtime_processor)
  list(GET _fields 3 _base_prefix)
  list(GET _fields 4 _reported_include)
  list(GET _fields 5 _reported_stdlib)

  if(NOT _version STREQUAL NEVERC_MANAGED_CPYTHON_VERSION)
    message(FATAL_ERROR
      "NeverC Python plugins require exact CPython "
      "${NEVERC_MANAGED_CPYTHON_VERSION}; managed interpreter reports ${_version}")
  endif()
  if(NOT _implementation STREQUAL "CPython")
    message(FATAL_ERROR
      "NeverC Python plugins require CPython; managed interpreter reports "
      "${_implementation}")
  endif()
  _neverc_managed_cpython_normalize_arch("${processor}" _expected_arch)
  _neverc_managed_cpython_normalize_arch("${_runtime_processor}" _actual_arch)
  if(NOT _actual_arch STREQUAL _expected_arch)
    message(FATAL_ERROR
      "NeverC managed CPython architecture mismatch: host is ${processor}, "
      "interpreter reports ${_runtime_processor}")
  endif()

  file(REAL_PATH "${root}" _root_real)
  file(REAL_PATH "${include_dir}" _include_real)
  file(REAL_PATH "${stdlib_dir}" _stdlib_real)
  _neverc_managed_cpython_comparison_path(
    "${_root_real}" "${system}" _root_compare)
  _neverc_managed_cpython_comparison_path(
    "${_include_real}" "${system}" _include_compare)
  _neverc_managed_cpython_comparison_path(
    "${_stdlib_real}" "${system}" _stdlib_compare)
  _neverc_managed_cpython_comparison_path(
    "${_base_prefix}" "${system}" _base_prefix_compare)
  _neverc_managed_cpython_comparison_path(
    "${_reported_include}" "${system}" _reported_include_compare)
  _neverc_managed_cpython_comparison_path(
    "${_reported_stdlib}" "${system}" _reported_stdlib_compare)
  foreach(_reported IN ITEMS "${_base_prefix_compare}"
                             "${_reported_include_compare}"
                             "${_reported_stdlib_compare}")
    cmake_path(IS_PREFIX _root_compare "${_reported}" NORMALIZE _inside_root)
    if(NOT _inside_root)
      message(FATAL_ERROR
        "NeverC rejected managed CPython path outside its staged root: "
        "${_reported} (root: ${_root_real})")
    endif()
  endforeach()
  if(NOT _reported_include_compare STREQUAL _include_compare)
    message(FATAL_ERROR
      "NeverC managed CPython include mismatch: selected ${_include_real}, "
      "interpreter reports ${_reported_include}")
  endif()
  if(NOT _reported_stdlib_compare STREQUAL _stdlib_compare)
    message(FATAL_ERROR
      "NeverC managed CPython stdlib mismatch: selected ${_stdlib_real}, "
      "interpreter reports ${_reported_stdlib}")
  endif()
endfunction()

function(_neverc_managed_cpython_validate_embed
         root system include_dir library runtime)
  set(_source "${CMAKE_BINARY_DIR}/CMakeFiles/neverc-managed-cpython-probe.c")
  file(WRITE "${_source}" [=[
#include <Python.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc != 2 || strcmp(PY_VERSION, "3.12.10") != 0)
    return 10;
  PyConfig config;
  PyConfig_InitPythonConfig(&config);
  config.use_environment = 0;
  config.site_import = 0;
  PyStatus status = PyConfig_SetBytesString(&config, &config.home, argv[1]);
  if (PyStatus_Exception(status)) {
    PyConfig_Clear(&config);
    return 11;
  }
  status = Py_InitializeFromConfig(&config);
  PyConfig_Clear(&config);
  if (PyStatus_Exception(status))
    return 12;
  const char *runtime_version = Py_GetVersion();
  if (!runtime_version || strncmp(runtime_version, "3.12.10", 7) != 0)
    return 13;
  printf("%s\n%s\n", runtime_version, Py_GetPlatform());
  return Py_FinalizeEx() < 0 ? 14 : 0;
}
]=])
  set(_probe_dir "${CMAKE_BINARY_DIR}/CMakeFiles/neverc-managed-cpython-probe")
  file(MAKE_DIRECTORY "${_probe_dir}")
  set(_probe "${_probe_dir}/probe${CMAKE_EXECUTABLE_SUFFIX}")
  set(_link_options)
  if(NOT system STREQUAL "Windows")
    get_filename_component(_library_dir "${library}" DIRECTORY)
    list(APPEND _link_options "-Wl,-rpath,${_library_dir}")
  endif()
  try_compile(
    _compiled
    "${_probe_dir}/build"
    "${_source}"
    CMAKE_FLAGS
      "-DCMAKE_C_STANDARD=11"
      "-DINCLUDE_DIRECTORIES:STRING=${include_dir}"
    LINK_LIBRARIES "${library}"
    LINK_OPTIONS ${_link_options}
    COPY_FILE "${_probe}"
    OUTPUT_VARIABLE _compile_output)
  if(NOT _compiled)
    message(FATAL_ERROR
      "NeverC rejected managed CPython headers/library: the native embed "
      "probe did not compile or link:\n${_compile_output}")
  endif()
  if(system STREQUAL "Windows")
    file(GLOB _runtime_dlls "${root}/*.dll")
    file(COPY ${_runtime_dlls} DESTINATION "${_probe_dir}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            --unset=PYTHONHOME --unset=PYTHONPATH --unset=PYTHONUSERBASE
            "${_probe}" "${root}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
  if(NOT _result EQUAL 0)
    string(STRIP "${_error}" _error)
    message(FATAL_ERROR
      "NeverC rejected managed CPython ABI/runtime (probe exit ${_result}): "
      "${_error}")
  endif()
  if(NOT _output MATCHES "^${NEVERC_MANAGED_CPYTHON_VERSION}")
    message(FATAL_ERROR
      "NeverC managed CPython embed probe reported an unexpected runtime: "
      "${_output}")
  endif()
endfunction()

function(_neverc_managed_cpython_validate root system processor)
  _neverc_managed_cpython_require_files("${root}" "${system}")
  _neverc_managed_cpython_paths(
    "${root}" "${system}"
    _executable _include _library _runtime _stdlib)
  _neverc_managed_cpython_validate_interpreter(
    "${root}" "${system}" "${processor}" "${_executable}"
    "${_include}" "${_stdlib}")
  _neverc_managed_cpython_validate_embed(
    "${root}" "${system}" "${_include}" "${_library}" "${_runtime}")
endfunction()

function(_neverc_managed_cpython_stage
         destination system processor layout archive archive_sha256 source_root
         download_dir)
  string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _nonce)
  set(_work "${CMAKE_BINARY_DIR}/_deps/neverc-cpython-stage-${_nonce}")
  set(_extract "${_work}/extract")
  set(_stage "${_work}/python")
  file(MAKE_DIRECTORY "${_extract}" "${_stage}")

  if(source_root)
    file(COPY "${source_root}/" DESTINATION "${_stage}")
  else()
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${_extract}")
    if(layout STREQUAL "unix")
      set(_archive_root "${_extract}/python")
    else()
      set(_archive_root "${_extract}")
    endif()
    if(NOT IS_DIRECTORY "${_archive_root}")
      file(REMOVE_RECURSE "${_work}")
      message(FATAL_ERROR
        "NeverC managed CPython archive has an unexpected layout: ${archive}")
    endif()
    file(COPY "${_archive_root}/" DESTINATION "${_stage}")
    _neverc_managed_cpython_copy_licenses("${_stage}" "${download_dir}")
  endif()

  _neverc_managed_cpython_paths(
    "${_stage}" "${system}"
    _executable _include _library _runtime _stdlib)
  if(system STREQUAL "Darwin")
    _neverc_managed_cpython_require_files("${_stage}" "${system}")
    _neverc_managed_cpython_repair_macos("${_stage}" "${_library}")
  endif()
  _neverc_managed_cpython_validate("${_stage}" "${system}" "${processor}")
  _neverc_managed_cpython_fingerprint("${_stage}" "${system}" _fingerprint)

  set(_stale "${CMAKE_BINARY_DIR}/_deps/neverc-cpython-stale-${_nonce}")
  if(EXISTS "${destination}")
    file(RENAME "${destination}" "${_stale}")
  endif()
  file(RENAME "${_stage}" "${destination}")
  file(WRITE "${destination}/.neverc-managed-cpython"
    "version=${NEVERC_MANAGED_CPYTHON_VERSION}\n"
    "artifact_sha256=${archive_sha256}\n"
    "fingerprint=${_fingerprint}\n")
  file(REMOVE_RECURSE "${_work}" "${_stale}")
endfunction()

function(neverc_setup_managed_cpython)
  if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR
      "NeverC managed CPython currently requires a native build so its exact "
      "interpreter/header/library ABI can be executed and validated. Disable "
      "Python plugins for a cross build or provide a native target packaging "
      "stage.")
  endif()

  set(NEVERC_MANAGED_PYTHON_ROOT "" CACHE PATH
      "Read-only exact CPython 3.12.10 development/runtime tree used instead of downloading")
  set(NEVERC_MANAGED_PYTHON_DOWNLOAD_DIR
      "${CMAKE_BINARY_DIR}/_deps/neverc-cpython-downloads" CACHE PATH
      "Cache for checksum-pinned NeverC managed CPython archives")
  mark_as_advanced(NEVERC_MANAGED_PYTHON_DOWNLOAD_DIR)

  neverc_managed_cpython_artifact(
    "${CMAKE_HOST_SYSTEM_NAME}" "${CMAKE_HOST_SYSTEM_PROCESSOR}"
    _url _archive_sha256 _layout _archive_name)
  set(_archive "${NEVERC_MANAGED_PYTHON_DOWNLOAD_DIR}/${_archive_name}")
  set(_staged_root "${CMAKE_BINARY_DIR}/python")
  set(_source_root "")

  if(NEVERC_MANAGED_PYTHON_ROOT)
    file(REAL_PATH "${NEVERC_MANAGED_PYTHON_ROOT}" _source_root)
    if(NOT IS_DIRECTORY "${_source_root}")
      message(FATAL_ERROR
        "NEVERC_MANAGED_PYTHON_ROOT is not a directory: "
        "${NEVERC_MANAGED_PYTHON_ROOT}")
    endif()
    _neverc_managed_cpython_require_files(
      "${_source_root}" "${CMAKE_HOST_SYSTEM_NAME}")
    _neverc_managed_cpython_paths(
      "${_source_root}" "${CMAKE_HOST_SYSTEM_NAME}"
      _source_executable _source_include _source_library _source_runtime
      _source_stdlib)
    _neverc_managed_cpython_validate_interpreter(
      "${_source_root}" "${CMAKE_HOST_SYSTEM_NAME}"
      "${CMAKE_HOST_SYSTEM_PROCESSOR}" "${_source_executable}"
      "${_source_include}" "${_source_stdlib}")
    _neverc_managed_cpython_fingerprint(
      "${_source_root}" "${CMAKE_HOST_SYSTEM_NAME}" _archive_sha256)
  else()
    _neverc_managed_cpython_download(
      "${_url}" "${_archive_sha256}" "${_archive}"
      "${CMAKE_HOST_SYSTEM_NAME}/${CMAKE_HOST_SYSTEM_PROCESSOR} runtime")
  endif()

  set(_reuse FALSE)
  set(_marker "${_staged_root}/.neverc-managed-cpython")
  if(EXISTS "${_marker}")
    file(STRINGS "${_marker}" _marker_lines)
    list(FIND _marker_lines
         "version=${NEVERC_MANAGED_CPYTHON_VERSION}" _version_index)
    list(FIND _marker_lines
         "artifact_sha256=${_archive_sha256}" _artifact_index)
    if(NOT _version_index EQUAL -1 AND NOT _artifact_index EQUAL -1)
      _neverc_managed_cpython_fingerprint(
        "${_staged_root}" "${CMAKE_HOST_SYSTEM_NAME}" _actual_fingerprint)
      list(FIND _marker_lines
           "fingerprint=${_actual_fingerprint}" _fingerprint_index)
      if(NOT _fingerprint_index EQUAL -1)
        _neverc_managed_cpython_validate(
          "${_staged_root}" "${CMAKE_HOST_SYSTEM_NAME}"
          "${CMAKE_HOST_SYSTEM_PROCESSOR}")
        set(_reuse TRUE)
      endif()
    endif()
  endif()

  if(NOT _reuse)
    _neverc_managed_cpython_stage(
      "${_staged_root}" "${CMAKE_HOST_SYSTEM_NAME}"
      "${CMAKE_HOST_SYSTEM_PROCESSOR}" "${_layout}" "${_archive}"
      "${_archive_sha256}" "${_source_root}"
      "${NEVERC_MANAGED_PYTHON_DOWNLOAD_DIR}")
  endif()

  _neverc_managed_cpython_paths(
    "${_staged_root}" "${CMAKE_HOST_SYSTEM_NAME}"
    _executable _include _library _runtime _stdlib)
  if(NOT TARGET NeverCPython::Python)
    add_library(NeverCPython::Python SHARED IMPORTED GLOBAL)
  endif()
  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set_target_properties(NeverCPython::Python PROPERTIES
      IMPORTED_IMPLIB "${_library}"
      IMPORTED_LOCATION "${_runtime}"
      INTERFACE_INCLUDE_DIRECTORIES "${_include}")
  else()
    set_target_properties(NeverCPython::Python PROPERTIES
      IMPORTED_LOCATION "${_library}"
      INTERFACE_INCLUDE_DIRECTORIES "${_include}")
  endif()

  get_filename_component(_library_dir "${_runtime}" DIRECTORY)
  set(NEVERC_MANAGED_PYTHON_STAGED_ROOT "${_staged_root}" PARENT_SCOPE)
  set(NEVERC_MANAGED_PYTHON_EXECUTABLE "${_executable}" PARENT_SCOPE)
  set(NEVERC_MANAGED_PYTHON_INCLUDE_DIR "${_include}" PARENT_SCOPE)
  set(NEVERC_MANAGED_PYTHON_LIBRARY "${_library}" PARENT_SCOPE)
  set(NEVERC_MANAGED_PYTHON_RUNTIME "${_runtime}" PARENT_SCOPE)
  set(NEVERC_MANAGED_PYTHON_LIBRARY_DIR "${_library_dir}" PARENT_SCOPE)
  set(NEVERC_MANAGED_PYTHON_STDLIB "${_stdlib}" PARENT_SCOPE)
  message(STATUS
    "NeverC: Python plugins use managed CPython "
    "${NEVERC_MANAGED_CPYTHON_VERSION} at ${_staged_root}")
endfunction()

function(neverc_stage_managed_python_dll target)
  if(WIN32 AND NEVERC_ENABLE_PYTHON_PLUGINS AND
     NEVERC_MANAGED_PYTHON_RUNTIME)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "${NEVERC_MANAGED_PYTHON_RUNTIME}"
              "$<TARGET_FILE_DIR:${target}>/python312.dll"
      VERBATIM)
  endif()
endfunction()
