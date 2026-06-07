# NeverC Standard Library — bitcode embedding build
#
# Each .c is compiled to LLVM bitcode, merged into a single .bc blob,
# and converted to an embedded C header for linking into the compiler.
# The old static-archive (libneverc_std.a) path has been removed.

# Install std headers + sources + manifest (no library target needed)
install(DIRECTORY include/ DESTINATION std/include COMPONENT neverc-std)
install(DIRECTORY src/     DESTINATION std/src     COMPONENT neverc-std)
install(FILES manifest.json DESTINATION std        COMPONENT neverc-std)
