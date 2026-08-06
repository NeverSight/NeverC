cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../modules/ManagedCPython.cmake")

function(assert_equal actual expected label)
  if(NOT "${actual}" STREQUAL "${expected}")
    message(FATAL_ERROR
      "Managed CPython contract mismatch for ${label}: "
      "expected '${expected}', got '${actual}'")
  endif()
endfunction()

assert_equal("${NEVERC_MANAGED_CPYTHON_VERSION}" "3.12.10" "version")
assert_equal("${NEVERC_MANAGED_CPYTHON_RELEASE}" "20250409" "release")
assert_equal("${NEVERC_MANAGED_CPYTHON_ACTIONS_RELEASE}"
             "3.12.10-14343898437" "actions/python-versions release")

_neverc_managed_cpython_comparison_path(
  "D:/a/NeverC/build/python/include" "Windows" _windows_cmake_path)
_neverc_managed_cpython_comparison_path(
  [=[D:\a\NeverC\build\python\include]=] "Windows" _windows_python_path)
_neverc_managed_cpython_comparison_path(
  "d:/A/neverc/build/python/include" "Windows" _windows_mixed_case_path)
assert_equal("${_windows_python_path}" "${_windows_cmake_path}"
             "Windows native separators")
assert_equal("${_windows_mixed_case_path}" "${_windows_cmake_path}"
             "Windows path case")

set(_cases
  "Darwin|arm64|aarch64-apple-darwin|0be1fe0b35a4d3c382141764ef16ed3b8cc2b4620b657f678daa7b7f8df39699|unix"
  "Darwin|x86_64|x86_64-apple-darwin|ad3bef94b6054adcf8e0a47886e21b00dfc6a37f22eea229cf0f8725bd0e1023|unix"
  "Linux|aarch64|linux-22.04-arm64|9f687d7707ece744e6b41a4184669c2e6adb1383c934fbadeaed5254d09de0ea|unix-root"
  "Linux|x86_64|linux-22.04-x64|f8e0109b3eeb6cb0a246725d16595793f5f3c882df77bd4acf195fbab64819ac|unix-root"
  "Windows|ARM64|arm64|20a5b1a707d899ffdfc5e3086d7372f7cc95eeea344d48ae256047cb7075cf63|windows"
  "Windows|AMD64|amd64|8649692de846c56a7189d6dae5c322ab20deb1b5908b6f39426b62a36f39415d|windows"
)

foreach(_case IN LISTS _cases)
  string(REPLACE "|" ";" _fields "${_case}")
  list(GET _fields 0 _system)
  list(GET _fields 1 _processor)
  list(GET _fields 2 _artifact_id)
  list(GET _fields 3 _expected_sha256)
  list(GET _fields 4 _expected_layout)

  neverc_managed_cpython_artifact(
    "${_system}" "${_processor}"
    _url _sha256 _layout _archive_name)

  if(NOT _url MATCHES "3\\.12\\.10")
    message(FATAL_ERROR "${_system}/${_processor} URL is not pinned: ${_url}")
  endif()
  if(NOT _url MATCHES "${_artifact_id}")
    message(FATAL_ERROR
      "${_system}/${_processor} URL uses the wrong artifact: ${_url}")
  endif()
  assert_equal("${_sha256}" "${_expected_sha256}"
               "${_system}/${_processor} SHA-256")
  assert_equal("${_layout}" "${_expected_layout}"
               "${_system}/${_processor} layout")
  if(NOT _archive_name MATCHES "3\\.12\\.10")
    message(FATAL_ERROR
      "${_system}/${_processor} archive name is not pinned: ${_archive_name}")
  endif()
endforeach()

message(STATUS "Managed CPython contract: all six native artifacts are pinned")
