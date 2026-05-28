add_custom_target(NeverCDriverOptions)

# ----------------------------------------------------------------------------
# Verify that Options.td.h is sorted to match LLVM's OptTable requirements.
#
# LLVM's OptTable performs a binary search on options at runtime and asserts
# (in Debug builds) that entries are sorted by StrCmpOptionNameIgnoreCase.
# Running the check at configure time catches mis-orderings before they
# reach the build / runtime stage.
#
# Set NEVERC_SKIP_OPTIONS_TD_SORT_CHECK=ON to bypass (e.g. during bisects).
# ----------------------------------------------------------------------------
option(NEVERC_SKIP_OPTIONS_TD_SORT_CHECK
       "Skip Options.td.h sort check during CMake configure" OFF)

if(NOT NEVERC_SKIP_OPTIONS_TD_SORT_CHECK)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    set(_options_td_sort_script
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../scripts/sort-options-td.py")
    set(_options_td_file
        "${CMAKE_CURRENT_SOURCE_DIR}/Options.td.h")

    if(EXISTS "${_options_td_sort_script}" AND EXISTS "${_options_td_file}")
      execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${_options_td_sort_script}" --check
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../../.."
        RESULT_VARIABLE _options_td_sort_result
        OUTPUT_VARIABLE _options_td_sort_output
        ERROR_VARIABLE  _options_td_sort_output)

      if(NOT _options_td_sort_result EQUAL 0)
        message(FATAL_ERROR
          "Options.td.h is not sorted in LLVM OptTable order:\n"
          "${_options_td_sort_output}\n"
          "Fix with:\n"
          "    python3 scripts/sort-options-td.py --write\n"
          "Or pass -DNEVERC_SKIP_OPTIONS_TD_SORT_CHECK=ON to skip.")
      else()
        message(STATUS "Options.td.h sort check: OK")
      endif()
    endif()
  else()
    message(WARNING
      "Python3 not found; skipping Options.td.h sort check. "
      "Install Python 3 or set -DNEVERC_SKIP_OPTIONS_TD_SORT_CHECK=ON.")
  endif()
endif()
