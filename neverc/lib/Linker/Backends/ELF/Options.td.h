/////////
// Prefixes

#ifdef PREFIX
#define COMMA ,
PREFIX(prefix_0, {llvm::StringLiteral("")})
PREFIX(prefix_1, {llvm::StringLiteral("-") COMMA llvm::StringLiteral("")})
PREFIX(prefix_4, {llvm::StringLiteral("-") COMMA llvm::StringLiteral("--")
                      COMMA llvm::StringLiteral("")})
PREFIX(prefix_3, {llvm::StringLiteral("--") COMMA llvm::StringLiteral("")})
PREFIX(prefix_2, {llvm::StringLiteral("--") COMMA llvm::StringLiteral("-")
                      COMMA llvm::StringLiteral("")})
#undef COMMA
#endif // PREFIX

/////////
// Prefix Union

#ifdef PREFIX_UNION
#define COMMA ,
PREFIX_UNION({llvm::StringLiteral("-") COMMA llvm::StringLiteral("--")
                  COMMA llvm::StringLiteral("")})
#undef COMMA
#endif // PREFIX_UNION

/////////
// ValuesCode

#ifdef OPTTABLE_VALUES_CODE
#endif
/////////
// Groups

#ifdef OPTION

//////////
// Options

OPTION(prefix_0, "<input>", INPUT, Input, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_0, "<unknown>", UNKNOWN, Unknown, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-(", anonymous_18, Flag, INVALID, start_group, nullptr, 0,
       DefaultVis, 0, "Alias for --start-group", nullptr, nullptr)
OPTION(prefix_1, "-)", anonymous_5, Flag, INVALID, end_group, nullptr, 0,
       DefaultVis, 0, "Alias for --end-group", nullptr, nullptr)
OPTION(prefix_2, "--allow-multiple-definition", allow_multiple_definition, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Allow multiple definitions", nullptr, nullptr)
OPTION(prefix_2, "--allow-shlib-undefined", allow_shlib_undefined, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Allow unresolved references in shared libraries (default when linking "
       "a shared library)",
       nullptr, nullptr)
OPTION(prefix_3, "--android-memtag-heap", android_memtag_heap, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Instruct the dynamic loader to enable MTE protection for the heap",
       nullptr, nullptr)
OPTION(
    prefix_3, "--android-memtag-mode=", android_memtag_mode_eq, Joined, INVALID,
    android_memtag_mode, nullptr, 0, DefaultVis, 0,
    "Instruct the dynamic loader to start under MTE mode {async, sync, none}",
    nullptr, nullptr)
OPTION(prefix_3, "--android-memtag-mode", android_memtag_mode, Separate,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_3, "--android-memtag-stack", android_memtag_stack, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Instruct the dynamic loader to prepare for MTE stack instrumentation",
       nullptr, nullptr)
OPTION(prefix_3, "--apply-dynamic-relocs", apply_dynamic_relocs, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Apply link-time values for dynamic relocations", nullptr, nullptr)
OPTION(prefix_2, "--as-needed", as_needed, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Only set DT_NEEDED for shared libraries if used",
       nullptr, nullptr)
OPTION(prefix_2, "--auxiliary=", auxiliary_eq, Joined, INVALID, auxiliary,
       nullptr, 0, DefaultVis, 0,
       "Set DT_AUXILIARY field to the specified name", nullptr, nullptr)
OPTION(prefix_2, "--auxiliary", auxiliary, Separate, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "--Bdynamic", Bdynamic, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Link against shared libraries (default)", nullptr,
       nullptr)
OPTION(prefix_2, "--Bno-symbolic", Bno_symbolic, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Don't bind default visibility defined symbols locally for -shared "
       "(default)",
       nullptr, nullptr)
OPTION(prefix_2, "--Bstatic", Bstatic, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Do not link against shared libraries", nullptr, nullptr)
OPTION(prefix_2, "--Bsymbolic-functions", Bsymbolic_functions, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Bind default visibility defined function symbols locally for -shared",
       nullptr, nullptr)
OPTION(prefix_2, "--Bsymbolic-non-weak-functions", Bsymbolic_non_weak_functions,
       Flag, INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Bind default visibility defined STB_GLOBAL function symbols locally "
       "for -shared",
       nullptr, nullptr)
OPTION(prefix_2, "--Bsymbolic-non-weak", Bsymbolic_non_weak, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Bind default visibility defined STB_GLOBAL symbols locally for -shared",
       nullptr, nullptr)
OPTION(prefix_2, "--Bsymbolic", Bsymbolic, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Bind default visibility defined symbols locally for -shared", nullptr,
       nullptr)
OPTION(prefix_1, "-b", anonymous_8, Separate, INVALID, format, nullptr, 0,
       DefaultVis, 0, "Alias for --format", nullptr, nullptr)
OPTION(prefix_3, "--check-dynamic-relocations", check_dynamic_relocations, Flag,
       INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Perform additional validation of the written dynamic relocations",
       nullptr, nullptr)
OPTION(prefix_2, "--check-sections", check_sections, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Check section addresses for overlaps (default)", nullptr, nullptr)
OPTION(prefix_3, "--cref", cref, Flag, INVALID, INVALID, nullptr, 0, DefaultVis,
       0,
       "Output cross reference table. If a map file is specified, print to the "
       "map file",
       nullptr, nullptr)
OPTION(prefix_2, "--defsym=", defsym_eq, Joined, INVALID, defsym, nullptr, 0,
       DefaultVis, 0, "Define a symbol alias", "<symbol>=<value>", nullptr)
OPTION(prefix_2, "--defsym", defsym, Separate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, "<symbol>=<value>", nullptr)
OPTION(prefix_3, "--dependency-file=", dependency_file_eq, Joined, INVALID,
       dependency_file, nullptr, 0, DefaultVis, 0, "Write a dependency file",
       "<file>", nullptr)
OPTION(prefix_3, "--dependency-file", dependency_file, Separate, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, "<file>", nullptr)
OPTION(prefix_3, "--dependent-libraries", dependent_libraries, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Process dependent library specifiers from input files (default)",
       nullptr, nullptr)
OPTION(prefix_2, "--disable-new-dtags", disable_new_dtags, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, "Disable new dynamic tags", nullptr,
       nullptr)
OPTION(prefix_2, "--discard-all", discard_all, Flag, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, "Delete all local symbols", nullptr, nullptr)
OPTION(prefix_2, "--discard-locals", discard_locals, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Delete temporary local symbols", nullptr,
       nullptr)
OPTION(prefix_2, "--discard-none", discard_none, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Keep all symbols in the symbol table",
       nullptr, nullptr)
OPTION(
    prefix_2, "--dynamic-list=", dynamic_list_eq, Joined, INVALID, dynamic_list,
    nullptr, 0, DefaultVis, 0,
    "Similar to --export-dynamic-symbol-list. When creating a shared object, "
    "this additionally implies -Bsymbolic but does not set DF_SYMBOLIC",
    "<file>", nullptr)
OPTION(prefix_2, "--dynamic-list", dynamic_list, Separate, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, "<file>", nullptr)
OPTION(prefix_2, "--emit-relocs", emit_relocs, Flag, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, "Generate relocations in output", nullptr, nullptr)
OPTION(prefix_2, "--enable-new-dtags", enable_new_dtags, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Enable new dynamic tags (default)", nullptr,
       nullptr)
OPTION(prefix_2, "--end-group", end_group, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Ignored for compatibility with GNU unless you pass --warn-backrefs",
       nullptr, nullptr)
OPTION(prefix_2, "--end-lib", end_lib, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "End a grouping of objects that should be treated as if they were "
       "together in an archive",
       nullptr, nullptr)
OPTION(prefix_2, "--entry=", entry_eq, Joined, INVALID, entry, nullptr, 0,
       DefaultVis, 0, "Name of entry point symbol", "<entry>", nullptr)
OPTION(prefix_2, "--entry", entry, Separate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, "<entry>", nullptr)
OPTION(prefix_3, "--error-handling-script=", error_handling_script_eq, Joined,
       INVALID, error_handling_script, nullptr, 0, DefaultVis, 0,
       "Specify an error handling script", nullptr, nullptr)
OPTION(prefix_3, "--error-handling-script", error_handling_script, Separate,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "--error-unresolved-symbols", error_unresolved_symbols, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Report unresolved symbols as errors", nullptr, nullptr)
OPTION(prefix_2, "--exclude-libs=", exclude_libs_eq, Joined, INVALID,
       exclude_libs, nullptr, 0, DefaultVis, 0,
       "Exclude static libraries from automatic export", nullptr, nullptr)
OPTION(prefix_2, "--exclude-libs", exclude_libs, Separate, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_3, "--execute-only", execute_only, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Mark executable sections unreadable",
       nullptr, nullptr)
OPTION(prefix_3, "--export-dynamic-symbol-list=", export_dynamic_symbol_list_eq,
       Joined, INVALID, export_dynamic_symbol_list, nullptr, 0, DefaultVis, 0,
       "Read a list of dynamic symbol patterns. Apply --export-dynamic-symbol "
       "on each pattern",
       "file", nullptr)
OPTION(prefix_3, "--export-dynamic-symbol-list", export_dynamic_symbol_list,
       Separate, INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, "file",
       nullptr)
OPTION(
    prefix_3, "--export-dynamic-symbol=", export_dynamic_symbol_eq, Joined,
    INVALID, export_dynamic_symbol, nullptr, 0, DefaultVis, 0,
    "(executable) Put matched symbols in the dynamic symbol table. (shared "
    "object) References to matched non-local STV_DEFAULT symbols shouldn't be "
    "bound to definitions within the shared object. Does not imply -Bsymbolic.",
    "glob", nullptr)
OPTION(prefix_3, "--export-dynamic-symbol", export_dynamic_symbol, Separate,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, "glob", nullptr)
OPTION(prefix_1, "-e", anonymous_6, JoinedOrSeparate, INVALID, entry, nullptr,
       0, DefaultVis, 0, "Alias for --entry", nullptr, nullptr)
OPTION(prefix_2, "--filter=", filter_eq, Joined, INVALID, filter, nullptr, 0,
       DefaultVis, 0, "Set DT_FILTER field to the specified name", nullptr,
       nullptr)
OPTION(prefix_2, "--filter", filter, Separate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "--fini=", fini_eq, Joined, INVALID, fini, nullptr, 0,
       DefaultVis, 0, "Specify a finalizer function", "<symbol>", nullptr)
OPTION(prefix_2, "--fini", fini, Separate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, "<symbol>", nullptr)
OPTION(prefix_2, "--fix-cortex-a53-843419", fix_cortex_a53_843419, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Apply fixes for AArch64 Cortex-A53 erratum 843419", nullptr, nullptr)
OPTION(prefix_2, "--format=", format_eq, Joined, INVALID, format, nullptr, 0,
       DefaultVis, 0,
       "Change the input format of the inputs following this option",
       "[default,elf,binary]", nullptr)
OPTION(prefix_2, "--format", format, Separate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, "[default,elf,binary]", nullptr)
OPTION(prefix_1, "-F", anonymous_7, Separate, INVALID, filter, nullptr, 0,
       DefaultVis, 0, "Alias for --filter", nullptr, nullptr)
OPTION(prefix_1, "-f", anonymous_0, Separate, INVALID, auxiliary, nullptr, 0,
       DefaultVis, 0, "Alias for --auxiliary", nullptr, nullptr)
OPTION(prefix_3, "--gdb-index", gdb_index, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Generate .gdb_index section", nullptr, nullptr)
OPTION(prefix_3, "--gnu-unique", gnu_unique, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Enable STB_GNU_UNIQUE symbol binding (default)", nullptr,
       nullptr)
OPTION(prefix_1, "-h", anonymous_17, JoinedOrSeparate, INVALID, soname, nullptr,
       0, DefaultVis, 0, "Alias for --soname", nullptr, nullptr)
OPTION(prefix_3, "--ignore-data-address-equality", ignore_data_address_equality,
       Flag, INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Allow the linker to break address equality of data", nullptr, nullptr)
OPTION(prefix_3, "--ignore-function-address-equality",
       ignore_function_address_equality, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Allow the linker to break address equality of functions",
       nullptr, nullptr)
OPTION(prefix_3, "--image-base=", image_base_eq, Joined, INVALID, image_base,
       nullptr, 0, DefaultVis, 0, "Set the base address", nullptr, nullptr)
OPTION(prefix_3, "--image-base", image_base, Separate, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "--init=", init_eq, Joined, INVALID, init, nullptr, 0,
       DefaultVis, 0, "Specify an initializer function", "<symbol>", nullptr)
OPTION(prefix_2, "--init", init, Separate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, "<symbol>", nullptr)
OPTION(prefix_2, "--just-symbols=", just_symbols_eq, Joined, INVALID,
       just_symbols, nullptr, 0, DefaultVis, 0, "Just link symbols", nullptr,
       nullptr)
OPTION(prefix_2, "--just-symbols", just_symbols, Separate, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "--keep-unique=", keep_unique_eq, Joined, INVALID, keep_unique,
       nullptr, 0, DefaultVis, 0, "Do not fold this symbol during ICF", nullptr,
       nullptr)
OPTION(prefix_2, "--keep-unique", keep_unique, Separate, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "--library-path=", anonymous_12, Joined, INVALID, library_path,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "--library-path", anonymous_11, Separate, INVALID,
       library_path, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "--library=", anonymous_10, Joined, INVALID, library, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "--library", anonymous_9, Separate, INVALID, library, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-L", library_path, JoinedOrSeparate, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Add <dir> to the library search path",
       "<dir>", nullptr)
OPTION(prefix_1, "-l", library, JoinedOrSeparate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Search for library <libname>", "<libname>", nullptr)
OPTION(prefix_3, "--mmap-output-file", mmap_output_file, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Mmap the output file for writing (default)",
       nullptr, nullptr)
OPTION(prefix_2, "--nmagic", nmagic, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Do not page align sections, link against static libraries.", "<magic>",
       nullptr)
OPTION(prefix_2, "--no-allow-multiple-definition", no_allow_multiple_definition,
       Flag, INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not allow multiple definitions (default)", nullptr, nullptr)
OPTION(prefix_2, "--no-allow-shlib-undefined", no_allow_shlib_undefined, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not allow unresolved references in shared libraries (default when "
       "linking an executable)",
       nullptr, nullptr)
OPTION(prefix_3, "--no-android-memtag-heap", no_android_memtag_heap, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_3, "--no-android-memtag-stack", no_android_memtag_stack, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_3, "--no-apply-dynamic-relocs", no_apply_dynamic_relocs, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not apply link-time values for dynamic relocations (default)",
       nullptr, nullptr)
OPTION(prefix_2, "--no-as-needed", no_as_needed, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Always set DT_NEEDED for shared libraries (default)", nullptr, nullptr)
OPTION(
    prefix_3, "--no-check-dynamic-relocations", no_check_dynamic_relocations,
    Flag, INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "Do not perform additional validation of the written dynamic relocations",
    nullptr, nullptr)
OPTION(prefix_2, "--no-check-sections", no_check_sections, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Do not check section addresses for overlaps", nullptr, nullptr)
OPTION(prefix_3, "--no-dependent-libraries", no_dependent_libraries, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Ignore dependent library specifiers from input files", nullptr, nullptr)
OPTION(prefix_3, "--no-execute-only", no_execute_only, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Mark executable sections readable (default)",
       nullptr, nullptr)
OPTION(prefix_3, "--no-gdb-index", no_gdb_index, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Do not generate .gdb_index section (default)", nullptr, nullptr)
OPTION(prefix_3, "--no-gnu-unique", no_gnu_unique, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Disable STB_GNU_UNIQUE symbol binding",
       nullptr, nullptr)
OPTION(prefix_3, "--no-mmap-output-file", no_mmap_output_file, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Do not mmap the output file for writing", nullptr, nullptr)
OPTION(prefix_2, "--no-nmagic", no_nmagic, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Page align sections (default)", "<magic>", nullptr)
OPTION(prefix_2, "--no-omagic", no_omagic, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Do not set the text data sections to be writable, page align sections "
       "(default)",
       "<magic>", nullptr)
OPTION(prefix_3, "--no-optimize-bb-jumps", no_optimize_bb_jumps, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Do not remove any direct jumps at the end to the next basic block "
       "(default)",
       nullptr, nullptr)
OPTION(prefix_3, "--no-relax", no_relax, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Disable target-specific relaxations", nullptr, nullptr)
OPTION(prefix_3, "--no-rosegment", no_rosegment, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Do not put read-only non-executable sections in their own segment",
       nullptr, nullptr)
OPTION(prefix_2, "--no-undefined-version", no_undefined_version, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Report version scripts that refer undefined symbols", nullptr, nullptr)
OPTION(
    prefix_2, "--no-undefined", no_undefined, Flag, INVALID, INVALID, nullptr,
    0, DefaultVis, 0,
    "Report unresolved symbols even if the linker is creating a shared library",
    nullptr, nullptr)
OPTION(prefix_3, "--no-use-android-relr-tags", no_use_android_relr_tags, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Use SHT_RELR / DT_RELR* tags (default)", nullptr, nullptr)
OPTION(prefix_3, "--no-warn-backrefs", no_warn_backrefs, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Do not warn about backward symbol references to extract archive "
       "members (default)",
       nullptr, nullptr)
OPTION(prefix_2, "--no-warn-common", no_warn_common, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Do not warn about duplicate common symbols (default)", nullptr, nullptr)
OPTION(prefix_3, "--no-warn-symbol-ordering", no_warn_symbol_ordering, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not warn about problems with the symbol ordering file", nullptr,
       nullptr)
OPTION(prefix_2, "--no-whole-archive", no_whole_archive, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Do not force load of all members in a static library (default)",
       nullptr, nullptr)
OPTION(prefix_2, "--noinhibit-exec", noinhibit_exec, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Retain the executable output file whenever it is still usable", nullptr,
       nullptr)
OPTION(prefix_1, "-N", anonymous_14, Flag, INVALID, omagic, nullptr, 0,
       DefaultVis, 0, "Alias for --omagic", nullptr, nullptr)
OPTION(prefix_1, "-n", anonymous_13, Flag, INVALID, nmagic, nullptr, 0,
       DefaultVis, 0, "Alias for --nmagic", nullptr, nullptr)
OPTION(prefix_3, "--oformat=", oformat_eq, Joined, INVALID, oformat, nullptr, 0,
       DefaultVis, 0, "Specify the binary format for the output object file",
       "[elf,binary]", nullptr)
OPTION(prefix_3, "--oformat", oformat, Separate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, "[elf,binary]", nullptr)
OPTION(prefix_3, "--omagic", omagic, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Set the text and data sections to be readable and writable, do not "
       "page align sections, link against static libraries",
       "<magic>", nullptr)
OPTION(prefix_3, "--optimize-bb-jumps", optimize_bb_jumps, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Remove direct jumps at the end to the next basic block", nullptr,
       nullptr)
OPTION(prefix_2, "--orphan-handling=", orphan_handling_eq, Joined, INVALID,
       orphan_handling, nullptr, 0, DefaultVis, 0,
       "Control how orphan sections are handled when linker script used",
       nullptr, nullptr)
OPTION(prefix_2, "--orphan-handling", orphan_handling, Separate, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "--override=", override_eq, Joined, INVALID, override, nullptr,
       0, DefaultVis, 0,
       "Allow symbol to override any other definition without error",
       "<symbol>", nullptr)
OPTION(prefix_2, "--override", override, Separate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, "<symbol>", nullptr)
OPTION(prefix_3, "--pack-dyn-relocs=", pack_dyn_relocs_eq, Joined, INVALID,
       pack_dyn_relocs, nullptr, 0, DefaultVis, 0,
       "Pack dynamic relocations in the given format",
       "[none,android,relr,android+relr]", nullptr)
OPTION(prefix_3, "--pack-dyn-relocs", pack_dyn_relocs, Separate, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr,
       "[none,android,relr,android+relr]", nullptr)
OPTION(prefix_3, "--package-metadata=", package_metadata, Joined, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, "Emit package metadata note",
       nullptr, nullptr)
OPTION(prefix_2, "--pic-veneer", pic_veneer, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Always generate position independent thunks (veneers)",
       nullptr, nullptr)
OPTION(prefix_2, "--pop-state", pop_state, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Restore the states saved by --push-state", nullptr,
       nullptr)
OPTION(prefix_2, "--print-archive-stats=", print_archive_stats, Joined, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Write archive usage statistics to the specified file. Print the "
       "numbers of members and extracted members for each archive",
       nullptr, nullptr)
OPTION(prefix_2, "--print-memory-usage", print_memory_usage, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, "Report target memory usage",
       nullptr, nullptr)
OPTION(prefix_2, "--push-state", push_state, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Save the current state of --as-needed, -static and --whole-archive",
       nullptr, nullptr)
OPTION(prefix_1, "-q", anonymous_4, Flag, INVALID, emit_relocs, nullptr, 0,
       DefaultVis, 0, "Alias for --emit-relocs", nullptr, nullptr)
OPTION(prefix_3, "--relax", relax, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Enable target-specific relaxations if supported (default)", nullptr,
       nullptr)
OPTION(prefix_3, "--remap-inputs-file=", remap_inputs_file, Joined, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Each line contains 'from-glob=to-file'. An input file matching "
       "<from-glob> is remapped to <to-file>",
       "<file>", nullptr)
OPTION(prefix_3, "--remap-inputs=", remap_inputs_eq, Joined, INVALID,
       remap_inputs, nullptr, 0, DefaultVis, 0,
       "Remap input files matching <from-glob> to <to-file>",
       "<from-glob>=<to-file>", nullptr)
OPTION(prefix_3, "--remap-inputs", remap_inputs, Separate, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, "<from-glob>=<to-file>", nullptr)
OPTION(prefix_2, "--retain-symbols-file=", retain_symbols_file_eq, Joined,
       INVALID, retain_symbols_file, nullptr, 0, DefaultVis, 0,
       "Retain only the symbols listed in the file", "<file>", nullptr)
OPTION(prefix_2, "--retain-symbols-file", retain_symbols_file, Separate,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, "<file>", nullptr)
OPTION(prefix_3, "--rosegment", rosegment, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Put read-only non-executable sections in their own segment (default)",
       nullptr, nullptr)
OPTION(prefix_2, "--rpath=", rpath_eq, Joined, INVALID, rpath, nullptr, 0,
       DefaultVis, 0, "Add a DT_RUNPATH to the output", nullptr, nullptr)
OPTION(prefix_2, "--rpath", rpath, Separate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-R", anonymous_15, JoinedOrSeparate, INVALID, rpath, nullptr,
       0, DefaultVis, 0, "Alias for --rpath", nullptr, nullptr)
OPTION(prefix_2, "--script=", script_eq, Joined, INVALID, script, nullptr, 0,
       DefaultVis, 0, "Read linker script", nullptr, nullptr)
OPTION(prefix_2, "--script", script, Separate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "--section-start=", section_start_eq, Joined, INVALID,
       section_start, nullptr, 0, DefaultVis, 0, "Set address of section",
       "<address>", nullptr)
OPTION(prefix_2, "--section-start", section_start, Separate, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, "<address>", nullptr)
OPTION(prefix_3, "--shuffle-sections=", shuffle_sections_eq, Joined, INVALID,
       shuffle_sections, nullptr, 0, DefaultVis, 0,
       "Shuffle matched sections using the given seed before mapping them to "
       "the output sections. If -1, reverse the section order. If 0, use a "
       "random seed",
       "<section-glob>=<seed>", nullptr)
OPTION(prefix_3, "--shuffle-sections", shuffle_sections, Separate, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, "<section-glob>=<seed>",
       nullptr)
OPTION(prefix_2, "--soname=", soname_eq, Joined, INVALID, soname, nullptr, 0,
       DefaultVis, 0, "Set DT_SONAME", nullptr, nullptr)
OPTION(prefix_2, "--soname", soname, Separate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "--sort-section=", sort_section_eq, Joined, INVALID,
       sort_section, nullptr, 0, DefaultVis, 0,
       "Specifies sections sorting rule when linkerscript is used", nullptr,
       nullptr)
OPTION(prefix_2, "--sort-section", sort_section, Separate, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "--split-stack-adjust-size=", split_stack_adjust_size_eq,
       Joined, INVALID, split_stack_adjust_size, nullptr, 0, DefaultVis, 0,
       "Specify adjustment to stack size when a split-stack function calls a "
       "non-split-stack function",
       "<value>", nullptr)
OPTION(prefix_2, "--split-stack-adjust-size", split_stack_adjust_size, Separate,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, "<value>", nullptr)
OPTION(prefix_2, "--start-group", start_group, Flag, INVALID, INVALID, nullptr,
       0, DefaultVis, 0,
       "Ignored for compatibility with GNU unless you pass --warn-backrefs",
       nullptr, nullptr)
OPTION(prefix_2, "--start-lib", start_lib, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Start a grouping of objects that should be treated as if they were "
       "together in an archive",
       nullptr, nullptr)
OPTION(prefix_2, "--static", anonymous_1, Flag, INVALID, Bstatic, nullptr, 0,
       DefaultVis, 0, "Alias for --Bstatic", nullptr, nullptr)
OPTION(prefix_3, "--symbol-ordering-file=", symbol_ordering_file_eq, Joined,
       INVALID, symbol_ordering_file, nullptr, 0, DefaultVis, 0,
       "Layout sections to place symbols in the order specified by symbol "
       "ordering file",
       nullptr, nullptr)
OPTION(prefix_3, "--symbol-ordering-file", symbol_ordering_file, Separate,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "--Tbss=", Tbss_eq, Joined, INVALID, Tbss, nullptr, 0,
       DefaultVis, 0, "Same as --section-start with .bss as the sectionname",
       nullptr, nullptr)
OPTION(prefix_2, "--Tbss", Tbss, Separate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "--Tdata=", Tdata_eq, Joined, INVALID, Tdata, nullptr, 0,
       DefaultVis, 0, "Same as --section-start with .data as the sectionname",
       nullptr, nullptr)
OPTION(prefix_2, "--Tdata", Tdata, Separate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "--trace-symbol=", trace_symbol_eq, Joined, INVALID,
       trace_symbol, nullptr, 0, DefaultVis, 0, "Trace references to symbols",
       nullptr, nullptr)
OPTION(prefix_2, "--trace-symbol", trace_symbol, Separate, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_4, "-Ttext-segment=", anonymous_19, Joined, INVALID,
       Ttext_segment, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_4, "-Ttext-segment", Ttext_segment, Separate, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "--Ttext=", Ttext_eq, Joined, INVALID, Ttext, nullptr, 0,
       DefaultVis, 0, "Same as --section-start with .text as the sectionname",
       nullptr, nullptr)
OPTION(prefix_2, "--Ttext", Ttext, Separate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-T", anonymous_16, JoinedOrSeparate, INVALID, script, nullptr,
       0, DefaultVis, 0, "Alias for --script", nullptr, nullptr)
OPTION(prefix_3, "--undefined-glob=", undefined_glob_eq, Joined, INVALID,
       undefined_glob, nullptr, 0, DefaultVis, 0,
       "Force undefined symbol during linking", "<pattern>", nullptr)
OPTION(prefix_3, "--undefined-glob", undefined_glob, Separate, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, "<pattern>", nullptr)
OPTION(prefix_2, "--undefined-version", undefined_version, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Allow unused version in version script (disabled by default)", nullptr,
       nullptr)
OPTION(prefix_2, "--undefined=", undefined_eq, Joined, INVALID, undefined,
       nullptr, 0, DefaultVis, 0, "Force undefined symbol during linking",
       "<symbol>", nullptr)
OPTION(prefix_2, "--undefined", undefined, Separate, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, "<symbol>", nullptr)
OPTION(prefix_2, "--unique", unique, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Creates a separate output section for every orphan input section",
       nullptr, nullptr)
OPTION(prefix_2, "--unresolved-symbols=", unresolved_symbols_eq, Joined,
       INVALID, unresolved_symbols, nullptr, 0, DefaultVis, 0,
       "Determine how to handle unresolved symbols", nullptr, nullptr)
OPTION(prefix_2, "--unresolved-symbols", unresolved_symbols, Separate, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_3, "--use-android-relr-tags", use_android_relr_tags, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Use SHT_ANDROID_RELR / DT_ANDROID_RELR* tags instead of SHT_RELR / "
       "DT_RELR*",
       nullptr, nullptr)
OPTION(prefix_1, "-u", anonymous_21, JoinedOrSeparate, INVALID, undefined,
       nullptr, 0, DefaultVis, 0, "Alias for --undefined", nullptr, nullptr)
OPTION(prefix_2, "--version-script=", version_script_eq, Joined, INVALID,
       version_script, nullptr, 0, DefaultVis, 0, "Read a version script",
       nullptr, nullptr)
OPTION(prefix_2, "--version-script", version_script, Separate, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_3, "--warn-backrefs-exclude=", warn_backrefs_exclude_eq, Joined,
       INVALID, warn_backrefs_exclude, nullptr, 0, DefaultVis, 0,
       "Glob describing an archive (or an object file within --start-lib) "
       "which should be ignored for --warn-backrefs.",
       "<glob>", nullptr)
OPTION(prefix_3, "--warn-backrefs-exclude", warn_backrefs_exclude, Separate,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, "<glob>", nullptr)
OPTION(prefix_3, "--warn-backrefs", warn_backrefs, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Warn about backward symbol references to extract archive members",
       nullptr, nullptr)
OPTION(prefix_2, "--warn-common", warn_common, Flag, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, "Warn about duplicate common symbols", nullptr,
       nullptr)
OPTION(prefix_3, "--warn-symbol-ordering", warn_symbol_ordering, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Warn about problems with the symbol ordering file (default)", nullptr,
       nullptr)
OPTION(prefix_2, "--warn-unresolved-symbols", warn_unresolved_symbols, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Report unresolved symbols as warnings", nullptr, nullptr)
OPTION(prefix_2, "--whole-archive", whole_archive, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Force load of all members in a static library", nullptr, nullptr)
OPTION(prefix_3, "--why-extract=", why_extract, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Print to a file about why archive members are extracted", nullptr,
       nullptr)
OPTION(prefix_2, "--wrap=", wrap_eq, Joined, INVALID, wrap, nullptr, 0,
       DefaultVis, 0,
       "Redirect symbol references to __wrap_symbol and __real_symbol "
       "references to symbol",
       "<symbol>", nullptr)
OPTION(prefix_2, "--wrap", wrap, Separate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, "<symbol>", nullptr)
OPTION(prefix_1, "-X", anonymous_3, Flag, INVALID, discard_locals, nullptr, 0,
       DefaultVis, 0, "Alias for --discard-locals", nullptr, nullptr)
OPTION(prefix_1, "-x", anonymous_2, Flag, INVALID, discard_all, nullptr, 0,
       DefaultVis, 0, "Alias for --discard-all", nullptr, nullptr)
OPTION(prefix_1, "-y", anonymous_20, JoinedOrSeparate, INVALID, trace_symbol,
       nullptr, 0, DefaultVis, 0, "Alias for --trace-symbol", nullptr, nullptr)
OPTION(prefix_1, "-z", z, JoinedOrSeparate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Linker option extensions", "<option>", nullptr)
#endif // OPTION

#ifdef SIMPLE_ENUM_VALUE_TABLE

struct SimpleEnumValue {
  const char *Name;
  unsigned Value;
};

struct SimpleEnumValueTable {
  const SimpleEnumValue *Table;
  unsigned Size;
};
static const SimpleEnumValueTable SimpleEnumValueTables[] = {};
static const unsigned SimpleEnumValueTablesSize =
    std::size(SimpleEnumValueTables);
#endif // SIMPLE_ENUM_VALUE_TABLE
