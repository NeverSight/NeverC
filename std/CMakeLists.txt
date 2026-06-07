# NeverC Standard Library — build static archive libneverc_std.a
# Each .c file is compiled to a separate .o so the linker can selectively
# pull in only the functions actually referenced by the user program.

file(GLOB_RECURSE STD_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.c")

add_library(neverc_std STATIC ${STD_SOURCES})
target_include_directories(neverc_std PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
set_target_properties(neverc_std PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  OUTPUT_NAME "neverc_std"
)

# Install std headers + sources + static library
install(DIRECTORY include/ DESTINATION std/include COMPONENT neverc-std)
install(DIRECTORY src/     DESTINATION std/src     COMPONENT neverc-std)
install(FILES manifest.json DESTINATION std        COMPONENT neverc-std)
install(TARGETS neverc_std
  ARCHIVE DESTINATION std/lib COMPONENT neverc-std
)
