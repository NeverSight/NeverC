function(_setup_stage phase stage result)
  if(NOT _stage_report)
    return()
  endif()
  string(REPLACE "\\" "\\\\" _result_json "${result}")
  string(REPLACE "\"" "\\\"" _result_json "${_result_json}")
  string(REPLACE "\n" "\\n" _result_json "${_result_json}")
  string(REPLACE "\r" "\\r" _result_json "${_result_json}")
  file(WRITE "${_stage_report}"
    "{\"phase\":\"${phase}\",\"stage\":\"${stage}\",\"result\":\"${_result_json}\",\"input_sha256\":\"${_input_sha256}\",\"output_sha256\":\"${_output_sha256}\"}\n")
endfunction()

if(SETUP_CONTRACT_REPORT_DIR)
  if(NOT EXISTS "${SETUP_CONTRACT_REPORT_DIR}/manifest.json")
    message(FATAL_ERROR "Missing pending Setup contract report")
  endif()
  set(_stage_report "${SETUP_CONTRACT_REPORT_DIR}/production-stage.txt")
  set(_rewrite_report "${SETUP_CONTRACT_REPORT_DIR}/production-rewrite.txt")
  set(_audit_report "${SETUP_CONTRACT_REPORT_DIR}/production-audit.txt")
  set(_member_report "${SETUP_CONTRACT_REPORT_DIR}/production-members.txt")
  _setup_stage(started aggregate 0)
endif()
file(STRINGS "${INPUTS}" _archives)
foreach(_archive IN LISTS _archives)
  if(NOT EXISTS "${_archive}")
    _setup_stage(failed aggregate "missing input archive")
    message(FATAL_ERROR "Missing private frontend archive: ${_archive}")
  endif()
endforeach()
set(_temporary "${OUTPUT}.tmp")
get_filename_component(_output_dir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_dir}")
file(REMOVE "${_temporary}")
if(KIND STREQUAL "darwin")
  execute_process(COMMAND "${TOOL}" -static -o "${_temporary}" ${_archives}
    RESULT_VARIABLE _result)
elseif(KIND STREQUAL "msvc")
  set(_response "\"/OUT:${_temporary}\"\n")
  foreach(_archive IN LISTS _archives)
    string(APPEND _response "\"${_archive}\"\n")
  endforeach()
  file(WRITE "${OUTPUT}.rsp" "${_response}")
  execute_process(COMMAND "${TOOL}" /NOLOGO "@${OUTPUT}.rsp"
    RESULT_VARIABLE _result)
else()
  set(_mri "CREATE \"${_temporary}\"\n")
  foreach(_archive IN LISTS _archives)
    string(APPEND _mri "ADDLIB \"${_archive}\"\n")
  endforeach()
  string(APPEND _mri "SAVE\nEND\n")
  file(WRITE "${OUTPUT}.mri" "${_mri}")
  execute_process(COMMAND "${TOOL}" -M INPUT_FILE "${OUTPUT}.mri"
    RESULT_VARIABLE _result)
endif()
if(NOT _result EQUAL 0)
  _setup_stage(failed aggregate "${_result}")
  file(REMOVE "${_temporary}")
  message(FATAL_ERROR "Failed to aggregate private frontend static libraries")
endif()
if(COFF)
  # Both readers are targets of this same pinned private sub-build. Python is
  # the restricted equal-width writer; no objcopy serialization is involved.
  file(SHA256 "${_temporary}" _input_sha256)
  execute_process(COMMAND "${PYTHON}" -E -B "${WRITER_TESTS}"
    RESULT_VARIABLE _result TIMEOUT 120)
  if(NOT _result EQUAL 0)
    _setup_stage(failed writer-tests "${_result}")
    message(FATAL_ERROR "Setup archive writer tests failed: ${_result}")
  endif()
  if(SETUP_CONTRACT_REPORT_DIR)
    _setup_stage(started setup-contract 0)
    execute_process(COMMAND "${PYTHON}" -I -B "${TOOLCHAIN_TESTS}"
      --setup-contract --llvm-root "${HOST_LLVM_ROOT}" --target "${SETUP_TARGET}"
      --private-nm "${NM}" --private-readobj "${READOBJ}"
      --pinned-setup-header "${SETUP_HEADER}"
      --report-dir "${SETUP_CONTRACT_REPORT_DIR}"
      RESULT_VARIABLE _result TIMEOUT 660)
    if(NOT _result EQUAL 0)
      _setup_stage(failed setup-contract "${_result}")
      message(FATAL_ERROR "Setup archive/runtime contract failed: ${_result}")
    endif()
  else()
    set(_rewrite_report "${OUTPUT}.setup-rewrite.txt")
    set(_audit_report "${OUTPUT}.setup-audit.txt")
    set(_member_report "${OUTPUT}.setup-members.txt")
    # These are owned build intermediates. Preserve the final archive until
    # the replacement has passed all checks; failed reports remain inspectable.
    file(REMOVE "${_rewrite_report}")
  endif()
  set(_isolated "${OUTPUT}.isolated.tmp")
  file(REMOVE "${_isolated}")
  _setup_stage(started rewrite 0)
  execute_process(COMMAND "${PYTHON}" -E -B "${WRITER}"
    --input "${_temporary}" --output "${_isolated}" --nm "${NM}"
    --readobj "${READOBJ}" --report "${_rewrite_report}"
    RESULT_VARIABLE _result TIMEOUT 600)
  if(NOT _result EQUAL 0)
    _setup_stage(failed rewrite "${_result}")
    message(FATAL_ERROR "Setup archive rewrite failed: ${_result}")
  endif()
  file(SHA256 "${_isolated}" _output_sha256)
  _setup_stage(started audit 0)
  execute_process(COMMAND "${PYTHON}" -E -B "${AUDITOR}"
    --nm "${NM}" --archive "${_isolated}" --prefix-header "${PREFIX_HEADER}"
    --coff-readobj "${READOBJ}"
    RESULT_VARIABLE _result TIMEOUT 600
    OUTPUT_FILE "${_audit_report}" ERROR_FILE "${_audit_report}")
  if(NOT _result EQUAL 0)
    _setup_stage(failed audit "${_result}")
    message(FATAL_ERROR "Setup archive no-host audit failed: ${_result}; ${_audit_report}")
  endif()
  _setup_stage(started members 0)
  execute_process(COMMAND "${PYTHON}" -I -B "${MEMBER_TESTS}"
    RESULT_VARIABLE _result TIMEOUT 120)
  if(NOT _result EQUAL 0)
    _setup_stage(failed member-tests "${_result}")
    message(FATAL_ERROR "Private frontend member-reader tests failed: ${_result}")
  endif()
  execute_process(COMMAND "${PYTHON}" -I -B "${MEMBER_READER}"
    --archive "${_isolated}" --archiver "${MEMBER_ARCHIVER}" --kind "${MEMBER_KIND}"
    RESULT_VARIABLE _result TIMEOUT 120
    OUTPUT_FILE "${_member_report}" ERROR_FILE "${_member_report}")
  if(NOT _result EQUAL 0)
    _setup_stage(failed members "${_result}")
    message(FATAL_ERROR "Private frontend member verification failed: ${_result}; ${_member_report}")
  endif()
  set(_publish "${_isolated}")
else()
  set(_publish "${_temporary}")
endif()
_setup_stage(started publish 0)
# CMake 3.20 has no file(RENAME ... RESULT). Keep the declared minimum while
# retaining a failure result for the CI stage record.
execute_process(COMMAND "${CMAKE_COMMAND}" -E rename "${_publish}" "${OUTPUT}"
  RESULT_VARIABLE _result)
if(NOT _result STREQUAL "0")
  _setup_stage(failed publish "${_result}")
  message(FATAL_ERROR "Failed to publish private frontend archive: ${_result}")
endif()
if(COFF)
  file(REMOVE "${_temporary}")
  _setup_stage(published publish 0)
endif()
