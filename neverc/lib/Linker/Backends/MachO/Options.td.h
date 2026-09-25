/////////
// Prefixes

#ifdef PREFIX
#define COMMA ,
PREFIX(prefix_0, {llvm::StringLiteral("")})
PREFIX(prefix_3, {llvm::StringLiteral("-") COMMA llvm::StringLiteral("")})
PREFIX(prefix_2, {llvm::StringLiteral("--") COMMA llvm::StringLiteral("")})
PREFIX(prefix_1, {llvm::StringLiteral("--") COMMA llvm::StringLiteral("-")
                      COMMA llvm::StringLiteral("")})
PREFIX(prefix_4, {llvm::StringLiteral("-") COMMA llvm::StringLiteral("--")
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
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "bundle", grp_bundle, Group,
       INVALID, INVALID, nullptr, 0, 0, 0, "CREATING A BUNDLE", nullptr,
       nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "content", grp_content, Group,
       INVALID, INVALID, nullptr, 0, 0, 0, "ADDITIONAL CONTENT", nullptr,
       nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "deprecated", grp_deprecated,
       Group, INVALID, INVALID, nullptr, 0, 0, 0, "DEPRECATED", nullptr,
       nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "dylib", grp_dylib, Group,
       INVALID, INVALID, nullptr, 0, 0, 0, "DYNAMIC LIBRARIES (DYLIB)", nullptr,
       nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "ignored", grp_ignored, Group,
       INVALID, INVALID, nullptr, 0, 0, 0, "IGNORED", nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "ignored_silently",
       grp_ignored_silently, Group, INVALID, INVALID, nullptr, 0, 0, 0,
       "IGNORED SILENTLY", nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "introspect", grp_introspect,
       Group, INVALID, INVALID, nullptr, 0, 0, 0, "INTROSPECTING THE LINKER",
       nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "kind", grp_neverc_ext, Group,
       INVALID, INVALID, nullptr, 0, 0, 0, "NEVERC LINKER EXTENSIONS", nullptr,
       nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "kind", grp_kind, Group, INVALID,
       INVALID, nullptr, 0, 0, 0, "OUTPUT KIND", nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "libs", grp_libs, Group, INVALID,
       INVALID, nullptr, 0, 0, 0, "LIBRARIES", nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "main", grp_main, Group, INVALID,
       INVALID, nullptr, 0, 0, 0, "MAIN EXECUTABLE", nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "object", grp_object, Group,
       INVALID, INVALID, nullptr, 0, 0, 0, "CREATING AN OBJECT FILE", nullptr,
       nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "obsolete", grp_obsolete, Group,
       INVALID, INVALID, nullptr, 0, 0, 0, "OBSOLETE", nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "opts", grp_opts, Group, INVALID,
       INVALID, nullptr, 0, 0, 0, "OPTIMIZATIONS", nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "rare", grp_rare, Group, INVALID,
       INVALID, nullptr, 0, 0, 0, "RARELY USED", nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "resolve", grp_resolve, Group,
       INVALID, INVALID, nullptr, 0, 0, 0, "SYMBOL RESOLUTION", nullptr,
       nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "symtab", grp_symtab, Group,
       INVALID, INVALID, nullptr, 0, 0, 0, "SYMBOL TABLE", nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "undocumented", grp_undocumented,
       Group, INVALID, INVALID, nullptr, 0, 0, 0, "UNDOCUMENTED", nullptr,
       nullptr)

OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "version", grp_version, Group,
       INVALID, INVALID, nullptr, 0, 0, 0, "VERSION TARGETING", nullptr,
       nullptr)

//////////
// Options

OPTION(prefix_0, "<input>", INPUT, Input, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_0, "<unknown>", UNKNOWN, Unknown, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--add-ast-path", add_ast_path, Separate, grp_symtab, INVALID,
       nullptr, 0, DefaultVis, 0, "AST paths will be emitted as STABS",
       "<path>", nullptr)
OPTION(prefix_1, "--add-empty-section", add_empty_section, MultiArg,
       grp_content, INVALID, nullptr, 0, DefaultVis, 2,
       "Create an empty <section> in <segment>", "<segment> <section>", nullptr)
OPTION(prefix_3, "-add_ast_path", anonymous_300, Separate, INVALID,
       add_ast_path, nullptr, 0, DefaultVis, 0, "Alias for --add-ast-path",
       "<path>", nullptr)
OPTION(prefix_3, "-add_empty_section", anonymous_301, MultiArg, INVALID,
       add_empty_section, nullptr, 0, DefaultVis, 2,
       "Alias for --add-empty-section", "<segment> <section>", nullptr)
OPTION(prefix_3, "-add_linker_option", add_linker_option, Flag,
       grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-add_source_version", add_source_version, Flag,
       grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Emit an LC_SOURCE_VERSION load command", nullptr, nullptr)
OPTION(prefix_3, "-add_split_seg_info", add_split_seg_info, Flag,
       grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_1, "--adhoc-codesign", adhoc_codesign, Flag, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Write an ad-hoc code signature to the output file (default for arm64 "
       "binaries)",
       nullptr, nullptr)
OPTION(prefix_3, "-adhoc_codesign", anonymous_302, Flag, INVALID,
       adhoc_codesign, nullptr, 0, DefaultVis, 0, "Alias for --adhoc-codesign",
       nullptr, nullptr)
OPTION(prefix_3, "-alias_list", alias_list, Separate, grp_resolve, INVALID,
       nullptr, 0, DefaultVis, 0, "Create symbol aliases specified in <file>",
       "<file>", nullptr)
OPTION(prefix_1, "--alias", alias, MultiArg, grp_resolve, INVALID, nullptr, 0,
       DefaultVis, 2, "Create a symbol alias with default global visibility",
       "<symbol_name> <alternate_name>", nullptr)
OPTION(prefix_1, "--all-load", all_load, Flag, grp_libs, INVALID, nullptr, 0,
       DefaultVis, 0, "Load all members of all static archive libraries",
       nullptr, nullptr)
OPTION(prefix_3, "-all_load", anonymous_303, Flag, INVALID, all_load, nullptr,
       0, DefaultVis, 0, "Alias for --all-load", nullptr, nullptr)
OPTION(prefix_3, "-allow_dead_duplicates", allow_dead_duplicates, Flag,
       grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Ignore duplicate symbols that dead stripping removes", nullptr, nullptr)
OPTION(prefix_3, "-allow_heap_execute", allow_heap_execute, Flag, grp_rare,
       INVALID, nullptr, 0, DefaultVis, 0,
       "On i386, allow any page to execute code", nullptr, nullptr)
OPTION(prefix_3, "-allow_simulator_linking_to_macosx_dylibs",
       allow_simulator_linking_to_macosx_dylibs, Flag, grp_undocumented,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-allow_stack_execute", allow_stack_execute, Flag, grp_main,
       INVALID, nullptr, 0, DefaultVis, 0, "Mark stack segment as executable",
       nullptr, nullptr)
OPTION(
    prefix_3, "-allow_sub_type_mismatches", allow_sub_type_mismatches, Flag,
    grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
    "Permit mixing objects compiled for different CPU subtypes (always done)",
    nullptr, nullptr)
OPTION(prefix_3, "-allowable_client", allowable_client, Separate, grp_rare,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Specify <name> of a dylib, framework, or executable that is allowed to "
       "link to this dylib",
       "<name>", nullptr)
OPTION(prefix_1, "--application-extension", application_extension, Flag,
       grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Mark output as safe for use in an application extension, and validate "
       "that linked dylibs are safe",
       nullptr, nullptr)
OPTION(prefix_3, "-application_extension", anonymous_304, Flag, INVALID,
       application_extension, nullptr, 0, DefaultVis, 0,
       "Alias for --application-extension", nullptr, nullptr)
OPTION(prefix_1, "--arch-errors-fatal", arch_errors_fatal, Flag, grp_rare,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Escalate to errors any warnings about inputs whose architecture does "
       "not match the -arch option",
       nullptr, nullptr)
OPTION(prefix_1, "--arch-multiple", arch_multiple, Flag, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Augment error and warning messages with the architecture name", nullptr,
       nullptr)
OPTION(prefix_3, "-arch_errors_fatal", anonymous_305, Flag, INVALID,
       arch_errors_fatal, nullptr, 0, DefaultVis, 0,
       "Alias for --arch-errors-fatal", nullptr, nullptr)
OPTION(prefix_3, "-arch_multiple", anonymous_306, Flag, INVALID, arch_multiple,
       nullptr, 0, DefaultVis, 0, "Alias for --arch-multiple", nullptr, nullptr)
OPTION(prefix_3, "-arch", arch, Separate, grp_kind, INVALID, nullptr, 0,
       DefaultVis, 0, "The architecture (e.g. ppc, ppc64, i386, x86_64)",
       "<arch_name>", nullptr)
OPTION(prefix_3, "-A", A, Separate, grp_obsolete, INVALID, nullptr, HelpHidden,
       DefaultVis, 0, "This option is obsolete in the native linker",
       "<basefile>", nullptr)
OPTION(prefix_3, "-bind_at_load", bind_at_load, Flag, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Tell dyld to bind all symbols at load time, rather than lazily",
       nullptr, nullptr)
OPTION(prefix_3, "-bitcode_bundle", bitcode_bundle, Flag, grp_obsolete, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Obsolete since the App Store no longer supports binaries with embedded "
       "bitcode",
       nullptr, nullptr)
OPTION(prefix_3, "-bitcode_hide_symbols", bitcode_hide_symbols, Flag,
       grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Obsolete since the App Store no longer supports binaries with embedded "
       "bitcode",
       nullptr, nullptr)
OPTION(prefix_3, "-bitcode_process_mode", bitcode_process_mode, Separate,
       grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Obsolete since the App Store no longer supports binaries with embedded "
       "bitcode",
       nullptr, nullptr)
OPTION(prefix_3, "-bitcode_symbol_map", bitcode_symbol_map, Separate,
       grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Obsolete since the App Store no longer supports binaries with embedded "
       "bitcode",
       "<path>", nullptr)
OPTION(prefix_3, "-bitcode_verify", bitcode_verify, Flag, grp_obsolete, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Obsolete since the App Store no longer supports binaries with embedded "
       "bitcode",
       nullptr, nullptr)
OPTION(prefix_3, "-bridgeos_version_min", bridgeos_version_min, Separate,
       grp_version, INVALID, nullptr, 0, DefaultVis, 0,
       "Oldest bridgeOS version for which linked output is usable", "<version>",
       nullptr)
OPTION(prefix_1, "--bundle-loader", bundle_loader, Separate, grp_bundle,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Resolve undefined symbols from <executable>", "<executable>", nullptr)
OPTION(prefix_3, "-bundle_loader", anonymous_307, Separate, INVALID,
       bundle_loader, nullptr, 0, DefaultVis, 0, "Alias for --bundle-loader",
       "<executable>", nullptr)
OPTION(prefix_3, "-bundle", bundle, Flag, grp_kind, INVALID, nullptr, 0,
       DefaultVis, 0, "Produce a bundle", nullptr, nullptr)
OPTION(prefix_3, "-b", b, Flag, grp_obsolete, INVALID, nullptr, HelpHidden,
       DefaultVis, 0, "This option is obsolete in the native linker", nullptr,
       nullptr)
OPTION(prefix_2, "--call-graph-profile-sort", call_graph_profile_sort, Flag,
       grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0,
       "Reorder sections with call graph profile (default)", nullptr, nullptr)
OPTION(prefix_3, "-classic_linker", classic_linker, Flag, grp_undocumented,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-client_name", client_name, Separate, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Specifies a <name> this client should match with the -allowable_client "
       "<name> in an explicitly linked dylib",
       "<name>", nullptr)
OPTION(prefix_2, "--color-diagnostics=", color_diagnostics_eq, Joined,
       grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0,
       "Use colors in diagnostics (default: auto)", "[auto,always,never]",
       nullptr)
OPTION(prefix_2, "--color-diagnostics", color_diagnostics, Flag, grp_neverc_ext,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Alias for --color-diagnostics=always", nullptr, nullptr)
OPTION(prefix_3, "-commons", commons, Separate, grp_resolve, INVALID, nullptr,
       0, DefaultVis, 0,
       "Resolve tentative definitions in dylibs according to <treatment>: "
       "ignore_dylibs, use_dylibs, error (default is ignore_dylibs)",
       "<treatment>", nullptr)
OPTION(prefix_1, "--compatibility-version", compatibility_version, Separate,
       grp_dylib, INVALID, nullptr, 0, DefaultVis, 0,
       "Compatibility <version> of this library", "<version>", nullptr)
OPTION(prefix_3, "-compatibility_version", anonymous_308, Separate, INVALID,
       compatibility_version, nullptr, 0, DefaultVis, 0,
       "Alias for --compatibility-version", "<version>", nullptr)
OPTION(prefix_1, "--current-version", current_version, Separate, grp_dylib,
       INVALID, nullptr, 0, DefaultVis, 0, "Current <version> of this library",
       "<version>", nullptr)
OPTION(prefix_3, "-current_version", anonymous_309, Separate, INVALID,
       current_version, nullptr, 0, DefaultVis, 0,
       "Alias for --current-version", "<version>", nullptr)
OPTION(prefix_1, "--data-const", data_const, Flag, grp_rare, INVALID, nullptr,
       0, DefaultVis, 0,
       "Force migration of readonly data into __DATA_CONST segment", nullptr,
       nullptr)
OPTION(prefix_1, "--data-in-code-info", data_in_code_info, Flag,
       grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Emit data-in-code information (default)", nullptr, nullptr)
OPTION(prefix_3, "-data_const", anonymous_310, Flag, INVALID, data_const,
       nullptr, 0, DefaultVis, 0, "Alias for --data-const", nullptr, nullptr)
OPTION(prefix_3, "-data_in_code_info", anonymous_311, Flag, INVALID,
       data_in_code_info, nullptr, 0, DefaultVis, 0,
       "Alias for --data-in-code-info", nullptr, nullptr)
OPTION(prefix_2, "--dead-strip-duplicates", dead_strip_duplicates, Flag,
       grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not error on duplicate symbols that will be dead stripped.", nullptr,
       nullptr)
OPTION(
    prefix_1, "--dead-strip-dylibs", dead_strip_dylibs, Flag, grp_rare, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Remove dylibs that are unreachable by the entry point or exported symbols",
    nullptr, nullptr)
OPTION(prefix_3, "-dead_strip_dylibs", anonymous_312, Flag, INVALID,
       dead_strip_dylibs, nullptr, 0, DefaultVis, 0,
       "Alias for --dead-strip-dylibs", nullptr, nullptr)
OPTION(prefix_3, "-dead_strip", dead_strip, Flag, grp_opts, INVALID, nullptr, 0,
       DefaultVis, 0, "Remove unreachable functions and data", nullptr, nullptr)
OPTION(prefix_3, "-debug_snapshot", debug_snapshot, Flag, grp_undocumented,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-debug_variant", debug_variant, Flag, grp_ignored_silently,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Do not warn about issues that are only problems for binaries shipping "
       "to customers.",
       nullptr, nullptr)
OPTION(prefix_2, "--deduplicate-strings", deduplicate_strings, Flag,
       grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0,
       "Enable string deduplication", nullptr, nullptr)
OPTION(prefix_3, "-demangle", demangle, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Demangle symbol names in diagnostics", nullptr, nullptr)
OPTION(prefix_1, "--dependency-info", dependency_info, Separate, grp_introspect,
       INVALID, nullptr, 0, DefaultVis, 0, "Dump dependency info", "<path>",
       nullptr)
OPTION(prefix_3, "-dependency_info", anonymous_313, Separate, INVALID,
       dependency_info, nullptr, 0, DefaultVis, 0,
       "Alias for --dependency-info", "<path>", nullptr)
OPTION(prefix_3, "-dependent_dr_info", dependent_dr_info, Flag, grp_obsolete,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-dirty_data_list", dirty_data_list, Separate, grp_rare,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Specify data symbols in <path> destined for the __DATA_DIRTY segment",
       "<path>", nullptr)
OPTION(prefix_3, "-dot", dot, Separate, grp_rare, INVALID, nullptr, HelpHidden,
       DefaultVis, 0,
       "Write a graph of symbol dependencies to <path> as a .dot file viewable "
       "with GraphViz",
       "<path>", nullptr)
OPTION(prefix_3, "-driverkit_version_min", driverkit_version_min, Separate,
       grp_version, INVALID, nullptr, 0, DefaultVis, 0,
       "Oldest DriverKit version for which linked output is usable",
       "<version>", nullptr)
OPTION(prefix_3, "-dtrace", dtrace, Separate, grp_content, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "Enable DTrace static probes according to declarations in <script>",
       "<script>", nullptr)
OPTION(prefix_1, "--dyld-env", dyld_env, Separate, grp_rare, INVALID, nullptr,
       0, DefaultVis, 0, "Specifies a LC_DYLD_ENVIRONMENT variable value pair.",
       "<dyld_env_var>", nullptr)
OPTION(prefix_3, "-dyld_env", anonymous_314, Separate, INVALID, dyld_env,
       nullptr, 0, DefaultVis, 0, "Alias for --dyld-env", "<dyld_env_var>",
       nullptr)
OPTION(prefix_3, "-dylib_compatibility_version", dylib_compatibility_version,
       Separate, grp_dylib, compatibility_version, nullptr, HelpHidden,
       DefaultVis, 0, "Alias for -compatibility_version", "<version>", nullptr)
OPTION(prefix_3, "-dylib_current_version", dylib_current_version, Separate,
       grp_dylib, current_version, nullptr, HelpHidden, DefaultVis, 0,
       "Alias for -current_version", "<version>", nullptr)
OPTION(prefix_3, "-dylib_file", dylib_file, Separate, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Specify <current_path> as different from where a dylib normally "
       "resides at <install_path>",
       "<install_path:current_path>", nullptr)
OPTION(prefix_3, "-dylib_install_name", dylib_install_name, Separate, grp_dylib,
       install_name, nullptr, 0, DefaultVis, 0, "Alias for -install_name",
       "<name>", nullptr)
OPTION(prefix_3, "-dylib", dylib, Flag, grp_kind, INVALID, nullptr, 0,
       DefaultVis, 0, "Produce a shared library", nullptr, nullptr)
OPTION(prefix_3, "-dylinker_install_name", dylinker_install_name, Separate,
       grp_dylib, install_name, nullptr, 0, DefaultVis, 0,
       "Alias for -install_name", "<name>", nullptr)
OPTION(prefix_3, "-dylinker", dylinker, Flag, grp_kind, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "Produce a dylinker only used when building dyld", nullptr, nullptr)
OPTION(prefix_3, "-dynamic", dynamic, Flag, grp_kind, INVALID, nullptr, 0,
       DefaultVis, 0, "Link dynamically (default)", nullptr, nullptr)
OPTION(prefix_3, "-d", d, Flag, grp_object, INVALID, nullptr, 0, DefaultVis, 0,
       "Force tentative into real definitions for common symbols", nullptr,
       nullptr)
OPTION(prefix_1, "--encryptable", encryptable, Flag, grp_undocumented, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Generate the LC_ENCRYPTION_INFO load command", nullptr, nullptr)
OPTION(prefix_2, "--end-lib", end_lib, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "End a grouping of objects that should be treated as if they were "
       "together in an archive",
       nullptr, nullptr)
OPTION(prefix_2, "--error-limit=", error_limit_eq, Joined, grp_neverc_ext,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Maximum number of errors to print before exiting (default: 20)",
       nullptr, nullptr)
OPTION(prefix_3, "-executable_path", executable_path, Separate,
       grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Resolve @executable_path in dependent dylibs against <path>", "<path>",
       nullptr)
OPTION(prefix_3, "-execute", execute, Flag, grp_kind, INVALID, nullptr, 0,
       DefaultVis, 0, "Produce a main executable (default)", nullptr, nullptr)
OPTION(prefix_1, "--export-dynamic", export_dynamic, Flag, grp_main, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Preserve all global symbols during LTO and when dead-stripping "
       "executables",
       nullptr, nullptr)
OPTION(prefix_3, "-export_dynamic", anonymous_315, Flag, INVALID,
       export_dynamic, nullptr, 0, DefaultVis, 0, "Alias for --export-dynamic",
       nullptr, nullptr)
OPTION(prefix_1, "--exported-symbols-list", exported_symbols_list, Separate,
       grp_resolve, INVALID, nullptr, 0, DefaultVis, 0,
       "Symbols specified in <file> remain global, while others become private "
       "externs",
       "<file>", nullptr)
OPTION(prefix_1, "--exported-symbol", exported_symbol, Separate, grp_resolve,
       INVALID, nullptr, 0, DefaultVis, 0,
       "<symbol> remains global, while others become private externs",
       "<symbol>", nullptr)
OPTION(prefix_3, "-exported_symbols_list", anonymous_316, Separate, INVALID,
       exported_symbols_list, nullptr, 0, DefaultVis, 0,
       "Alias for --exported-symbols-list", "<file>", nullptr)
OPTION(prefix_3, "-exported_symbols_order", exported_symbols_order, Separate,
       grp_opts, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Specify frequently-used symbols in <file> to optimize symbol exports",
       "<file>", nullptr)
OPTION(prefix_3, "-exported_symbol", anonymous_317, Separate, INVALID,
       exported_symbol, nullptr, 0, DefaultVis, 0,
       "Alias for --exported-symbol", "<symbol>", nullptr)
OPTION(prefix_3, "-e", e, Separate, grp_rare, INVALID, nullptr, 0, DefaultVis,
       0,
       "Make <symbol> the entry point of an executable (default is \"start\" "
       "from crt1.o)",
       "<symbol>", nullptr)
OPTION(prefix_3, "-fatal_warnings", fatal_warnings, Flag, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0, "Treat warnings as errors", nullptr, nullptr)
OPTION(prefix_1, "--filelist", filelist, Separate, grp_content, INVALID,
       nullptr, 0, DefaultVis, 0, "Read names of files to link from <file>",
       "<file>", nullptr)
OPTION(prefix_1, "--final-output", final_output, Separate, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Specify dylib install name if --install-name is not used; used by "
       "compiler driver for multiple -arch arguments",
       "<name>", nullptr)
OPTION(prefix_3, "-final_output", anonymous_318, Separate, INVALID,
       final_output, nullptr, 0, DefaultVis, 0, "Alias for --final-output",
       "<name>", nullptr)
OPTION(prefix_1, "--fixup-chains", fixup_chains, Flag, grp_undocumented,
       INVALID, nullptr, 0, DefaultVis, 0, "Emit chained fixups", nullptr,
       nullptr)
OPTION(prefix_3, "-fixup_chains_section", fixup_chains_section, Flag,
       grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-fixup_chains", anonymous_319, Flag, INVALID, fixup_chains,
       nullptr, 0, DefaultVis, 0, "Alias for --fixup-chains", nullptr, nullptr)
OPTION(prefix_1, "--flat-namespace", flat_namespace, Flag, grp_resolve, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Resolve symbols from all dylibs, both direct and transitive. Do not "
       "record source libraries: dyld must re-search at runtime and use the "
       "first definition found",
       nullptr, nullptr)
OPTION(prefix_3, "-flat_namespace", anonymous_320, Flag, INVALID,
       flat_namespace, nullptr, 0, DefaultVis, 0, "Alias for --flat-namespace",
       nullptr, nullptr)
OPTION(prefix_3, "-flto-codegen-only", flto_codegen_only, Flag,
       grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_1, "--force-load", force_load, Separate, grp_libs, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Load all members static archive library at <path>", "<path>", nullptr)
OPTION(prefix_3, "-force_cpusubtype_ALL", force_cpusubtype_ALL, Flag, grp_rare,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Mark binary as runnable on any PowerPC, ignoring any PowerPC cpu "
       "requirements encoded in the object files",
       nullptr, nullptr)
OPTION(prefix_3, "-force_flat_namespace", force_flat_namespace, Flag, grp_rare,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Tell dyld to use a flat namespace on this executable and all its "
       "dependent dylibs & bundles",
       nullptr, nullptr)
OPTION(prefix_3, "-force_load_swift_libs", force_load_swift_libs, Flag,
       grp_libs, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Apply -force_load to libraries listed in LC_LINKER_OPTIONS whose names "
       "start with 'swift'",
       nullptr, nullptr)
OPTION(prefix_3, "-force_load", anonymous_321, Separate, INVALID, force_load,
       nullptr, 0, DefaultVis, 0, "Alias for --force-load", "<path>", nullptr)
OPTION(prefix_3, "-force_symbol_not_weak", force_symbol_not_weak, Flag,
       grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-force_symbol_weak", force_symbol_weak, Flag,
       grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-force_symbols_coalesce_list", force_symbols_coalesce_list,
       Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-force_symbols_not_weak_list", force_symbols_not_weak_list,
       Separate, grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-force_symbols_weak_list", force_symbols_weak_list, Separate,
       grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_2, "--fork", fork, Flag, grp_neverc_ext, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Report the result as soon as the output is complete and release "
       "memory in the background (default)",
       nullptr, nullptr)
OPTION(prefix_1, "--framework", framework, Separate, grp_libs, INVALID, nullptr,
       0, DefaultVis, 0,
       "Search for <name>.framework/<name> on the framework search path",
       "<name>", nullptr)
OPTION(prefix_1, "--function-starts", function_starts, Flag, grp_undocumented,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Create table of function start addresses (default)", nullptr, nullptr)
OPTION(prefix_3, "-function_starts", anonymous_322, Flag, INVALID,
       function_starts, nullptr, 0, DefaultVis, 0,
       "Alias for --function-starts", nullptr, nullptr)
OPTION(prefix_3, "-fvmlib", fvmlib, Flag, grp_obsolete, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-F", F, JoinedOrSeparate, grp_libs, INVALID, nullptr, 0,
       DefaultVis, 0, "Add dir to the framework search path", "<dir>", nullptr)
OPTION(prefix_1, "--headerpad-max-install-names", headerpad_max_install_names,
       Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Allocate extra space so all load-command paths can expand to "
       "MAXPATHLEN via install_name_tool",
       nullptr, nullptr)
OPTION(prefix_3, "-headerpad_max_install_names", anonymous_323, Flag, INVALID,
       headerpad_max_install_names, nullptr, 0, DefaultVis, 0,
       "Alias for --headerpad-max-install-names", nullptr, nullptr)
OPTION(prefix_1, "--headerpad", headerpad, Separate, grp_rare, INVALID, nullptr,
       0, DefaultVis, 0,
       "Allocate hex <size> extra space for future expansion of the load "
       "commands via install_name_tool (default is 0x20)",
       "<size>", nullptr)
OPTION(prefix_2, "--help-hidden", help_hidden, Flag, grp_neverc_ext, INVALID,
       nullptr, 0, DefaultVis, 0, "Display help for hidden options", nullptr,
       nullptr)
OPTION(prefix_4, "-help", help, Flag, grp_neverc_ext, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_3, "-hidden-l", hidden_l, Joined, grp_libs, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Like -l<name>, but load all symbols with hidden visibility", "<name>",
       nullptr)
OPTION(prefix_2, "--icf=", icf_eq, Joined, grp_neverc_ext, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Set level for identical code folding (default: none). Possible "
       "values:\\n  none        - Disable ICF\\n  safe        - Only folds "
       "non-address significant functions (as described by `__addrsig` "
       "section)\\n  safe_thunks - Like safe, but replaces address-significant "
       "functions with thunks\\n  all         - Fold all identical functions",
       "[none,safe,safe_thunks,all]", nullptr)
OPTION(prefix_2, "--ignore-auto-link-option=", ignore_auto_link_option_eq,
       Joined, grp_neverc_ext, ignore_auto_link_option, nullptr, 0, DefaultVis,
       0,
       "Ignore a single auto-linked library or framework. Useful to ignore "
       "invalid options that the native linker ignores",
       nullptr, nullptr)
OPTION(prefix_2, "--ignore-auto-link-option", ignore_auto_link_option, Separate,
       grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "--ignore-auto-link", ignore_auto_link, Flag, grp_rare,
       INVALID, nullptr, 0, DefaultVis, 0, "Ignore LC_LINKER_OPTIONs", nullptr,
       nullptr)
OPTION(prefix_1, "--ignore-optimization-hints", ignore_optimization_hints, Flag,
       grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Ignore Linker Optimization Hints", nullptr, nullptr)
OPTION(prefix_3, "-ignore_auto_link", anonymous_324, Flag, INVALID,
       ignore_auto_link, nullptr, 0, DefaultVis, 0,
       "Alias for --ignore-auto-link", nullptr, nullptr)
OPTION(prefix_3, "-ignore_optimization_hints", anonymous_325, Flag, INVALID,
       ignore_optimization_hints, nullptr, 0, DefaultVis, 0,
       "Alias for --ignore-optimization-hints", nullptr, nullptr)
OPTION(prefix_3, "-image_base", image_base, Separate, grp_opts, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Preferred hex load address for a dylib or bundle.", "<address>",
       nullptr)
OPTION(prefix_1, "--init-offsets", init_offsets, Flag, grp_undocumented,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Store __TEXT segment offsets of static initializers", nullptr, nullptr)
OPTION(prefix_3, "-init_offsets", anonymous_326, Flag, INVALID, init_offsets,
       nullptr, 0, DefaultVis, 0, "Alias for --init-offsets", nullptr, nullptr)
OPTION(prefix_3, "-init", init, Separate, grp_rare, INVALID, nullptr, 0,
       DefaultVis, 0, "Run <symbol> as the first initializer in a dylib",
       "<symbol>", nullptr)
OPTION(prefix_1, "--install-name", install_name, Separate, grp_dylib, INVALID,
       nullptr, 0, DefaultVis, 0, "Set an internal install path in a dylib",
       "<name>", nullptr)
OPTION(prefix_3, "-install_name", anonymous_327, Separate, INVALID,
       install_name, nullptr, 0, DefaultVis, 0, "Alias for --install-name",
       "<name>", nullptr)
OPTION(prefix_3, "-interposable_list", interposable_list, Separate, grp_rare,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Access global symbols listed in <path> indirectly", "<path>", nullptr)
OPTION(prefix_3, "-interposable", interposable, Flag, grp_rare, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Indirects access to all to exported symbols in a dylib", nullptr,
       nullptr)
OPTION(prefix_3, "-ios_simulator_version_min", ios_simulator_version_min,
       Separate, grp_version, INVALID, nullptr, 0, DefaultVis, 0,
       "Oldest iOS simulator version for which linked output is usable",
       "<version>", nullptr)
OPTION(prefix_3, "-ios_version_min", ios_version_min, Separate, grp_version,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Oldest iOS version for which linked output is usable", "<version>",
       nullptr)
OPTION(prefix_3, "-iosmac_version_min", iosmac_version_min, Separate,
       grp_version, maccatalyst_version_min, nullptr, HelpHidden, DefaultVis, 0,
       "Alias for -maccatalyst_version_min", "<version>", nullptr)
OPTION(prefix_3, "-iphoneos_version_min", iphoneos_version_min, Separate,
       grp_version, ios_version_min, nullptr, HelpHidden, DefaultVis, 0,
       "Alias for -ios_version_min", "<version>", nullptr)
OPTION(prefix_3, "-i", i, Flag, grp_undocumented, INVALID, nullptr, HelpHidden,
       DefaultVis, 0, "This option is undocumented in the native linker",
       nullptr, nullptr)
OPTION(prefix_3, "-keep_dwarf_unwind", keep_dwarf_unwind, Flag,
       grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-keep_private_externs", keep_private_externs, Flag,
       grp_object, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Do not convert private external symbols to static symbols (only valid "
       "with -r)",
       nullptr, nullptr)
OPTION(prefix_3, "-keep_relocs", keep_relocs, Flag, grp_rare, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "Retain section-based relocation records in the output, which are "
       "ignored at runtime by dyld",
       nullptr, nullptr)
OPTION(prefix_3, "-kext_objects_dir", kext_objects_dir, Flag, grp_undocumented,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-kexts_use_stubs", kexts_use_stubs, Flag, grp_undocumented,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-kext", kext, Flag, grp_undocumented, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-lazy-l", lazy_l, Joined, grp_deprecated, INVALID, nullptr, 0,
       DefaultVis, 0,
       "This option is deprecated and is now an alias for -l<path>.", "<name>",
       nullptr)
OPTION(prefix_3, "-lazy_framework", lazy_framework, Separate, grp_deprecated,
       INVALID, nullptr, 0, DefaultVis, 0,
       "This option is deprecated and is now an alias for -framework.",
       "<name>", nullptr)
OPTION(prefix_3, "-lazy_library", lazy_library, Separate, grp_deprecated,
       INVALID, nullptr, 0, DefaultVis, 0,
       "This option is deprecated and is now an alias for regular linking",
       "<path>", nullptr)
OPTION(prefix_1, "--load-hidden", load_hidden, Separate, grp_libs, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Load all symbols from static library with hidden visibility", "<path>",
       nullptr)
OPTION(prefix_2, "--load-pass-plugin=", load_pass_plugins_eq, Joined,
       grp_neverc_ext, load_pass_plugins, nullptr, 0, DefaultVis, 0,
       "Load passes from plugin library", nullptr, nullptr)
OPTION(prefix_2, "--load-pass-plugin", load_pass_plugins, Separate,
       grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_3, "-load_hidden", anonymous_328, Separate, INVALID, load_hidden,
       nullptr, 0, DefaultVis, 0, "Alias for --load-hidden", "<path>", nullptr)
OPTION(prefix_2, "--lto-CGO", lto_CGO, Joined, grp_neverc_ext, INVALID, nullptr,
       0, DefaultVis, 0, "Set codegen optimization level for LTO (default: 2)",
       "<cgopt-level>", nullptr)
OPTION(prefix_2, "--lto-debug-pass-manager", lto_debug_pass_manager, Flag,
       grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0,
       "Debug new pass manager", nullptr, nullptr)
OPTION(prefix_2, "--lto-newpm-passes=", lto_newpm_passes, Joined,
       grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0,
       "Passes to run during LTO", nullptr, nullptr)
OPTION(prefix_2, "--lto-O", lto_O, Joined, grp_neverc_ext, INVALID, nullptr, 0,
       DefaultVis, 0, "Set optimization level for LTO (default: 2)",
       "<opt-level>", nullptr)
OPTION(prefix_3, "-lto_library", lto_library, Separate, grp_obsolete, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Obsolete. the native linker supports LTO directly, without using an "
       "external dylib.",
       "<path>", nullptr)
OPTION(prefix_3, "-L", L, JoinedOrSeparate, grp_libs, INVALID, nullptr, 0,
       DefaultVis, 0, "Add dir to the library search path", "<dir>", nullptr)
OPTION(prefix_3, "-l", l, Joined, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Search for lib<name>.dylib or lib<name>.a on the library search path",
       "<name>", nullptr)
OPTION(prefix_3, "-maccatalyst_version_min", maccatalyst_version_min, Separate,
       grp_version, INVALID, nullptr, 0, DefaultVis, 0,
       "Oldest MacCatalyst version for which linked output is usable",
       "<version>", nullptr)
OPTION(prefix_3, "-macos_version_min", macos_version_min, Separate, grp_version,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Oldest macOS version for which linked output is usable", "<version>",
       nullptr)
OPTION(prefix_3, "-macosx_version_min", macosx_version_min, Separate,
       grp_version, macos_version_min, nullptr, HelpHidden, DefaultVis, 0,
       "Alias for -macos_version_min", "<version>", nullptr)
OPTION(prefix_3, "-map", map, Separate, grp_introspect, INVALID, nullptr, 0,
       DefaultVis, 0, "Writes all symbols and their addresses to <path>",
       "<path>", nullptr)
OPTION(prefix_1, "--mark-dead-strippable-dylib", mark_dead_strippable_dylib,
       Flag, grp_dylib, INVALID, nullptr, 0, DefaultVis, 0,
       "Mark output dylib as dead-strippable: When a client links against it "
       "but does not use any of its symbols, the dylib will not be added to "
       "the client's list of needed dylibs",
       nullptr, nullptr)
OPTION(prefix_3, "-mark_dead_strippable_dylib", anonymous_329, Flag, INVALID,
       mark_dead_strippable_dylib, nullptr, 0, DefaultVis, 0,
       "Alias for --mark-dead-strippable-dylib", nullptr, nullptr)
OPTION(prefix_3, "-max_default_common_align", max_default_common_align,
       Separate, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Reduce maximum alignment for common symbols to a hex power-of-2 "
       "<boundary>",
       "<boundary>", nullptr)
OPTION(prefix_3, "-mcpu", mcpu, Separate, grp_rare, INVALID, nullptr, 0,
       DefaultVis, 0, "Processor family target for LTO code generation",
       nullptr, nullptr)
OPTION(prefix_3, "-merge_zero_fill_sections", merge_zero_fill_sections, Flag,
       grp_opts, INVALID, nullptr, 0, DefaultVis, 0,
       "Merge all zeroed data into the __zerofill section", nullptr, nullptr)
OPTION(prefix_3, "-mllvm", mllvm, Separate, grp_rare, INVALID, nullptr, 0,
       DefaultVis, 0, "Options to pass to LLVM", nullptr, nullptr)
OPTION(prefix_3, "-move_to_ro_segment", move_to_ro_segment, MultiArg, grp_rare,
       INVALID, nullptr, HelpHidden, DefaultVis, 2,
       "Move code symbols listed in <path> to another <segment>",
       "<segment> <path>", nullptr)
OPTION(prefix_3, "-move_to_rw_segment", move_to_rw_segment, MultiArg, grp_rare,
       INVALID, nullptr, HelpHidden, DefaultVis, 2,
       "Move data symbols listed in <path> to another <segment>",
       "<segment> <path>", nullptr)
OPTION(prefix_3, "-multi_module", multi_module, Flag, grp_rare, interposable,
       nullptr, HelpHidden, DefaultVis, 0, "Alias for -interposable", nullptr,
       nullptr)
OPTION(prefix_3, "-multiply_defined_unused", multiply_defined_unused, Separate,
       grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", "<treatment>", nullptr)
OPTION(prefix_3, "-multiply_defined", multiply_defined, Separate, grp_obsolete,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", "<treatment>", nullptr)
OPTION(prefix_3, "-M", M, Flag, grp_ignored, INVALID, nullptr, HelpHidden,
       DefaultVis, 0, "This option is ignored in the native linker", nullptr,
       nullptr)
OPTION(prefix_3, "-m", m, Flag, grp_obsolete, INVALID, nullptr, HelpHidden,
       DefaultVis, 0, "This option is obsolete in the native linker", nullptr,
       nullptr)
OPTION(prefix_1, "--needed-framework", needed_framework, Separate, grp_libs,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Like --framework <name>, but link <name> even if none of its symbols "
       "are used and --dead-strip-dylibs is active",
       "<name>", nullptr)
OPTION(prefix_1, "--needed-library", needed_library, Separate, grp_libs,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Like bare <path>, but link library even if its symbols are not used "
       "and --dead-strip-dylibs is active",
       "<path>", nullptr)
OPTION(prefix_3, "-needed-l", needed_l, Joined, grp_libs, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Like -l<name>, but link library even if its symbols are not used and "
       "-dead_strip_dylibs is active",
       "<name>", nullptr)
OPTION(prefix_3, "-needed_framework", anonymous_330, Separate, INVALID,
       needed_framework, nullptr, 0, DefaultVis, 0,
       "Alias for --needed-framework", "<name>", nullptr)
OPTION(prefix_3, "-needed_library", anonymous_331, Separate, INVALID,
       needed_library, nullptr, 0, DefaultVis, 0, "Alias for --needed-library",
       "<path>", nullptr)
OPTION(prefix_3, "-new_linker", new_linker, Flag, grp_ignored, INVALID, nullptr,
       HelpHidden, DefaultVis, 0, "This option is ignored in the native linker",
       nullptr, nullptr)
OPTION(prefix_1, "--no-adhoc-codesign", no_adhoc_codesign, Flag, grp_rare,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Do not write an ad-hoc code signature to the output file (default for "
       "x86_64 binaries)",
       nullptr, nullptr)
OPTION(prefix_1, "--no-application-extension", no_application_extension, Flag,
       grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Disable application extension functionality (default)", nullptr,
       nullptr)
OPTION(prefix_2, "--no-call-graph-profile-sort", no_call_graph_profile_sort,
       Flag, grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not reorder sections with call graph profile", nullptr, nullptr)
OPTION(prefix_2, "--no-color-diagnostics", no_color_diagnostics, Flag,
       grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0,
       "Alias for --color-diagnostics=never", nullptr, nullptr)
OPTION(prefix_1, "--no-data-const", no_data_const, Flag, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Block migration of readonly data away from __DATA segment", nullptr,
       nullptr)
OPTION(prefix_1, "--no-data-in-code-info", no_data_in_code_info, Flag,
       grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not emit data-in-code information", nullptr, nullptr)
OPTION(prefix_2, "--no-deduplicate-strings", no_deduplicate_strings, Flag,
       grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0,
       "Disable string deduplication. This helps uncover cases of comparing "
       "string addresses instead of equality and might have a link time "
       "performance benefit.",
       nullptr, nullptr)
OPTION(prefix_3, "-no-deduplicate-symbol-strings",
       no_deduplicate_symbol_strings, Flag, grp_rare, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Do not deduplicate strings in the symbol string table. Might result in "
       "larger binaries but slightly faster link times.",
       nullptr, nullptr)
OPTION(prefix_1, "--no-encryption", no_encryption, Flag, grp_undocumented,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Do not generate the LC_ENCRYPTION_INFO load command", nullptr, nullptr)
OPTION(prefix_1, "--no-exported-symbols", no_exported_symbols, Flag,
       grp_resolve, INVALID, nullptr, 0, DefaultVis, 0,
       "Don't export any symbols from the binary, useful for main executables "
       "that don't have plugins",
       nullptr, nullptr)
OPTION(prefix_1, "--no-fixup-chains", no_fixup_chains, Flag, grp_undocumented,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Emit fixup information as classic dyld opcodes", nullptr, nullptr)
OPTION(prefix_2, "--no-fork", no_fork, Flag, grp_neverc_ext, INVALID,
       nullptr, 0, DefaultVis, 0, "Exit only after all memory is released",
       nullptr, nullptr)
OPTION(prefix_1, "--no-function-starts", no_function_starts, Flag, grp_rare,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Do not create table of function start addresses", nullptr, nullptr)
OPTION(prefix_1, "--no-implicit-dylibs", no_implicit_dylibs, Flag, grp_opts,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Do not optimize public dylib transitive symbol references", nullptr,
       nullptr)
OPTION(prefix_1, "--no-uuid", no_uuid, Flag, grp_rare, INVALID, nullptr, 0,
       DefaultVis, 0, "Do not generate the LC_UUID load command", nullptr,
       nullptr)
OPTION(prefix_2, "--no-warn-duplicate-rpath", no_warn_duplicate_rpath, Flag,
       grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not warn if the same -rpath is specified multiple times", nullptr,
       nullptr)
OPTION(prefix_2, "--no-warn-dylib-install-name", no_warn_dylib_install_name,
       Flag, grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not warn on -install_name if -dylib is not passed (default)",
       nullptr, nullptr)
OPTION(prefix_3, "-no_adhoc_codesign", anonymous_332, Flag, INVALID,
       no_adhoc_codesign, nullptr, 0, DefaultVis, 0,
       "Alias for --no-adhoc-codesign", nullptr, nullptr)
OPTION(prefix_3, "-no_application_extension", anonymous_333, Flag, INVALID,
       no_application_extension, nullptr, 0, DefaultVis, 0,
       "Alias for --no-application-extension", nullptr, nullptr)
OPTION(prefix_3, "-no_arch_warnings", no_arch_warnings, Flag, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Suppresses warnings about inputs whose architecture does not match the "
       "-arch option",
       nullptr, nullptr)
OPTION(prefix_3, "-no_branch_islands", no_branch_islands, Flag, grp_opts,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Disable infra for branches beyond the maximum branch distance.",
       nullptr, nullptr)
OPTION(prefix_3, "-no_compact_linkedit", no_compact_linkedit, Flag,
       grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-no_compact_unwind", no_compact_unwind, Flag,
       grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-no_data_const", anonymous_334, Flag, INVALID, no_data_const,
       nullptr, 0, DefaultVis, 0, "Alias for --no-data-const", nullptr, nullptr)
OPTION(prefix_3, "-no_data_in_code_info", anonymous_335, Flag, INVALID,
       no_data_in_code_info, nullptr, 0, DefaultVis, 0,
       "Alias for --no-data-in-code-info", nullptr, nullptr)
OPTION(prefix_3, "-no_dead_strip_inits_and_terms",
       no_dead_strip_inits_and_terms, Flag, grp_deprecated, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "Unnecessary option: initialization and termination are roots of the "
       "dead strip graph, so never dead stripped",
       nullptr, nullptr)
OPTION(prefix_3, "-no_deduplicate", no_deduplicate, Flag, grp_opts, icf_eq,
       "none\0", 0, DefaultVis, 0,
       "Disable code deduplication (synonym for `--icf=none')", nullptr,
       nullptr)
OPTION(prefix_3, "-no_dependent_dr_info", no_dependent_dr_info, Flag,
       grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-no_dtrace_dof", no_dtrace_dof, Flag, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0, "Disable dtrace-dof processing (default).",
       nullptr, nullptr)
OPTION(prefix_3, "-no_eh_labels", no_eh_labels, Flag, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Emit no .eh labels on frame entries with -r (always done)", nullptr,
       nullptr)
OPTION(prefix_3, "-no_encryption", anonymous_336, Flag, INVALID, no_encryption,
       nullptr, 0, DefaultVis, 0, "Alias for --no-encryption", nullptr, nullptr)
OPTION(prefix_3, "-no_exported_symbols", anonymous_337, Flag, INVALID,
       no_exported_symbols, nullptr, 0, DefaultVis, 0,
       "Alias for --no-exported-symbols", nullptr, nullptr)
OPTION(prefix_3, "-no_fixup_chains", anonymous_338, Flag, INVALID,
       no_fixup_chains, nullptr, 0, DefaultVis, 0,
       "Alias for --no-fixup-chains", nullptr, nullptr)
OPTION(prefix_3, "-no_function_starts", anonymous_339, Flag, INVALID,
       no_function_starts, nullptr, 0, DefaultVis, 0,
       "Alias for --no-function-starts", nullptr, nullptr)
OPTION(prefix_3, "-no_implicit_dylibs", anonymous_340, Flag, INVALID,
       no_implicit_dylibs, nullptr, 0, DefaultVis, 0,
       "Alias for --no-implicit-dylibs", nullptr, nullptr)
OPTION(prefix_3, "-no_inits", no_inits, Flag, grp_rare, INVALID, nullptr, 0,
       DefaultVis, 0, "Fail if the output contains static initializers",
       nullptr, nullptr)
OPTION(prefix_3, "-no_keep_dwarf_unwind", no_keep_dwarf_unwind, Flag,
       grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-no_kext_objects", no_kext_objects, Flag, grp_undocumented,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-no_new_main", no_new_main, Flag, grp_undocumented, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-no_objc_category_merging", no_objc_category_merging, Flag,
       grp_neverc_ext, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Do not merge Objective-C categories", nullptr, nullptr)
OPTION(prefix_3, "-no_objc_relative_method_lists",
       no_objc_relative_method_lists, Flag, grp_undocumented, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "Don't emit relative method lists (use traditional representation)",
       nullptr, nullptr)
OPTION(prefix_3, "-no_order_data", no_order_data, Flag, grp_opts, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Disable default reordering of global data accessed at launch time",
       nullptr, nullptr)
OPTION(prefix_3, "-no_order_inits", no_order_inits, Flag, grp_opts, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Leave static initializers in place when ordering (always done)",
       nullptr, nullptr)
OPTION(prefix_3, "-no_pie", no_pie, Flag, grp_main, INVALID, nullptr, 0,
       DefaultVis, 0, "Do not build a position independent executable", nullptr,
       nullptr)
OPTION(prefix_3, "-no_source_version", no_source_version, Flag,
       grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not emit an LC_SOURCE_VERSION load command (default)", nullptr,
       nullptr)
OPTION(prefix_3, "-no_uuid", anonymous_341, Flag, INVALID, no_uuid, nullptr, 0,
       DefaultVis, 0, "Alias for --no-uuid", nullptr, nullptr)
OPTION(prefix_3, "-no_warn_duplicate_libraries", no_warn_duplicate_libraries,
       Flag, grp_ignored_silently, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Do not warn if the input contains duplicate library options.", nullptr,
       nullptr)
OPTION(prefix_3, "-no_warn_inits", no_warn_inits, Flag, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Do not warn about static initializers (none are warned about)", nullptr,
       nullptr)
OPTION(prefix_3, "-no_weak_exports", no_weak_exports, Flag, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Fail if the linked image contains weak external symbols", nullptr,
       nullptr)
OPTION(prefix_3, "-no_weak_imports", no_weak_imports, Flag, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Fail if any symbols are weak imports, allowed to be NULL at runtime",
       nullptr, nullptr)
OPTION(prefix_3, "-no_zero_fill_sections", no_zero_fill_sections, Flag,
       grp_opts, INVALID, nullptr, 0, DefaultVis, 0,
       "Explicitly store zeroed data in the final image", nullptr, nullptr)
OPTION(prefix_1, "--noall-load", noall_load, Flag, grp_libs, INVALID, nullptr,
       0, DefaultVis, 0,
       "Don't load all static members from archives, this is the default, this "
       "negates --all-load",
       nullptr, nullptr)
OPTION(prefix_3, "-noall_load", anonymous_342, Flag, INVALID, noall_load,
       nullptr, 0, DefaultVis, 0, "Alias for --noall-load", nullptr, nullptr)
OPTION(prefix_3, "-nofixprebinding", nofixprebinding, Flag, grp_obsolete,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-nomultidefs", nomultidefs, Flag, grp_obsolete, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", nullptr, nullptr)
OPTION(prefix_1, "--non-global-symbols-no-strip-list",
       non_global_symbols_no_strip_list, Separate, grp_symtab, INVALID, nullptr,
       0, DefaultVis, 0,
       "Specify in <path> the non-global symbols that should remain in the "
       "output symbol table",
       "<path>", nullptr)
OPTION(prefix_1, "--non-global-symbols-strip-list",
       non_global_symbols_strip_list, Separate, grp_symtab, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Specify in <path> the non-global symbols that should be removed from "
       "the output symbol table",
       "<path>", nullptr)
OPTION(prefix_3, "-non_global_symbols_no_strip_list", anonymous_343, Separate,
       INVALID, non_global_symbols_no_strip_list, nullptr, 0, DefaultVis, 0,
       "Alias for --non-global-symbols-no-strip-list", "<path>", nullptr)
OPTION(prefix_3, "-non_global_symbols_strip_list", anonymous_344, Separate,
       INVALID, non_global_symbols_strip_list, nullptr, 0, DefaultVis, 0,
       "Alias for --non-global-symbols-strip-list", "<path>", nullptr)
OPTION(prefix_3, "-noprebind_all_twolevel_modules",
       noprebind_all_twolevel_modules, Flag, grp_obsolete, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-noprebind", noprebind, Flag, grp_obsolete, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-noseglinkedit", noseglinkedit, Flag, grp_obsolete, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-not_for_dyld_shared_cache", not_for_dyld_shared_cache, Flag,
       grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Prevent system dylibs from being placed into the dylib shared cache",
       nullptr, nullptr)
OPTION(prefix_3, "-objc_abi_version", objc_abi_version, Separate,
       grp_ignored_silently, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option only applies to i386 in the native linker", nullptr,
       nullptr)
OPTION(prefix_3, "-objc_category_merging", objc_category_merging, Flag,
       grp_neverc_ext, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Merge Objective-C categories that share the same base class", nullptr,
       nullptr)
OPTION(prefix_3, "-objc_gc_compaction", objc_gc_compaction, Flag, grp_rare,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Mark the Objective-C image as compatible with compacting garbage "
       "collection",
       nullptr, nullptr)
OPTION(prefix_3, "-objc_gc_only", objc_gc_only, Flag, grp_rare, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Verify that all code was compiled with -fobjc-gc-only", nullptr,
       nullptr)
OPTION(prefix_3, "-objc_gc", objc_gc, Flag, grp_rare, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "Verify that all code was compiled with -fobjc-gc or -fobjc-gc-only",
       nullptr, nullptr)
OPTION(prefix_3, "-objc_relative_method_lists", objc_relative_method_lists,
       Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Emit relative method lists (more compact representation)", nullptr,
       nullptr)
OPTION(prefix_3, "-objc_stubs_fast", objc_stubs_fast, Flag, grp_rare, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Produce larger stubs for Objective-C method calls with fewer jumps "
       "(default).",
       nullptr, nullptr)
OPTION(prefix_3, "-objc_stubs_small", objc_stubs_small, Flag, grp_rare, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Produce smaller stubs for Objective-C method calls with more jumps.",
       nullptr, nullptr)
OPTION(prefix_3, "-ObjC", ObjC, Flag, grp_libs, INVALID, nullptr, HelpHidden,
       DefaultVis, 0,
       "Load all members of static archives that are an Objective-C class or "
       "category.",
       nullptr, nullptr)
OPTION(prefix_1, "--order-file", order_file, Separate, grp_opts, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Layout functions and data according to specification in <file>",
       "<file>", nullptr)
OPTION(prefix_3, "-order_file_statistics", order_file_statistics, Flag,
       grp_introspect, INVALID, nullptr, 0, DefaultVis, 0,
       "Logs information about -order_file", nullptr, nullptr)
OPTION(prefix_3, "-order_file", anonymous_345, Separate, INVALID, order_file,
       nullptr, 0, DefaultVis, 0, "Alias for --order-file", "<file>", nullptr)
OPTION(prefix_1, "--oso-prefix", oso_prefix, Separate, grp_symtab, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Remove the prefix <path> from OSO symbols in the debug map", "<path>",
       nullptr)
OPTION(prefix_3, "-oso_prefix", anonymous_346, Separate, INVALID, oso_prefix,
       nullptr, 0, DefaultVis, 0, "Alias for --oso-prefix", "<path>", nullptr)
OPTION(prefix_2, "--override=", override_eq, Joined, grp_neverc_ext, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Allow symbol to override any other definition without error",
       "<symbol>", nullptr)
OPTION(prefix_3, "-O", O, JoinedOrSeparate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Optimize output file size", nullptr, nullptr)
OPTION(prefix_3, "-o", o, Separate, grp_kind, INVALID, nullptr, 0, DefaultVis,
       0, "The name of the output file (default: `a.out')", "<path>", nullptr)
OPTION(prefix_3, "-page_align_data_atoms", page_align_data_atoms, Flag,
       grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Distribute global variables on separate pages so page used/dirty "
       "status can guide creation of an order file to cluster commonly "
       "used/dirty globals",
       nullptr, nullptr)
OPTION(
    prefix_1, "--pagezero-size", pagezero_size, Separate, grp_main, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Size of unreadable segment at address zero is hex <size> (default is 4GB)",
    "<size>", nullptr)
OPTION(prefix_3, "-pagezero_size", anonymous_347, Separate, INVALID,
       pagezero_size, nullptr, 0, DefaultVis, 0, "Alias for --pagezero-size",
       "<size>", nullptr)
OPTION(prefix_3, "-pause", pause, Flag, grp_undocumented, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-pie", pie, Flag, grp_main, INVALID, nullptr, 0, DefaultVis,
       0, "Build a position independent executable (default)", nullptr, nullptr)
OPTION(
    prefix_3, "-platform_version", platform_version, MultiArg, grp_version,
    INVALID, nullptr, 0, DefaultVis, 3,
    "Platform (e.g., macos, ios, tvos, watchos, xros, bridgeos, mac-catalyst, "
    "ios-sim, tvos-sim, watchos-sim, xros-sim, driverkit) and version numbers",
    "<platform> <min_version> <sdk_version>", nullptr)
OPTION(prefix_3, "-prebind_all_twolevel_modules", prebind_all_twolevel_modules,
       Flag, grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-prebind_allow_overlap", prebind_allow_overlap, Flag,
       grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-prebind", prebind, Flag, grp_obsolete, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-preload", preload, Flag, grp_kind, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "Produce an unsegmented binary for embedded systems", nullptr, nullptr)
OPTION(
    prefix_2, "--print-dylib-search", print_dylib_search, Flag, grp_neverc_ext,
    INVALID, nullptr, 0, DefaultVis, 0,
    "Print which paths the native linker searched when trying to find dylibs",
    nullptr, nullptr)
OPTION(prefix_2, "--print-symbol-order=", print_symbol_order_eq, Joined,
       grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0,
       "Print a symbol order specified by --call-graph-profile-sort into the "
       "specified file",
       nullptr, nullptr)
OPTION(prefix_3, "-print_statistics", print_statistics, Flag, grp_introspect,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Log the linker's CPU time and peak memory usage", nullptr, nullptr)
OPTION(prefix_3, "-private_bundle", private_bundle, Flag, grp_obsolete, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-random_uuid", random_uuid, Flag, grp_undocumented, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Use a random LC_UUID instead of one derived from the output", nullptr,
       nullptr)
OPTION(prefix_3, "-read_only_relocs", read_only_relocs, Separate, grp_rare,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Handle relocations that modify read-only pages according to "
       "<treatment> of warning, error, or suppress (i.e., allow)",
       "<treatment>", nullptr)
OPTION(prefix_3, "-read_only_stubs", read_only_stubs, Flag, grp_rare, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "On i386, make the __IMPORT segment of a final linked image read-only",
       nullptr, nullptr)
OPTION(prefix_1, "--reexport-framework", reexport_framework, Separate, grp_libs,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Like --framework <name>, but export all symbols of <name> from the "
       "newly created library",
       "<name>", nullptr)
OPTION(prefix_1, "--reexport-library", reexport_library, Separate, grp_libs,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Like bare <path>, but export all symbols of <path> from newly created "
       "library",
       "<path>", nullptr)
OPTION(prefix_3, "-reexport-l", reexport_l, Joined, grp_libs, INVALID, nullptr,
       0, DefaultVis, 0,
       "Like -l<name>, but export all symbols of <name> from newly created "
       "library",
       "<name>", nullptr)
OPTION(prefix_3, "-reexport_framework", anonymous_348, Separate, INVALID,
       reexport_framework, nullptr, 0, DefaultVis, 0,
       "Alias for --reexport-framework", "<name>", nullptr)
OPTION(prefix_3, "-reexport_library", anonymous_349, Separate, INVALID,
       reexport_library, nullptr, 0, DefaultVis, 0,
       "Alias for --reexport-library", "<path>", nullptr)
OPTION(prefix_3, "-reexported_symbols_list", reexported_symbols_list, Separate,
       grp_resolve, INVALID, nullptr, 0, DefaultVis, 0,
       "Symbols from dependent dylibs specified in <file> are reexported by "
       "this dylib",
       "<file>", nullptr)
OPTION(prefix_1, "--rename-section", rename_section, MultiArg, grp_rare,
       INVALID, nullptr, 0, DefaultVis, 4,
       "Rename <from_segment>/<from_section> as <to_segment>/<to_section>",
       "<from_segment> <from_section> <to_segment> <to_section>", nullptr)
OPTION(prefix_1, "--rename-segment", rename_segment, MultiArg, grp_rare,
       INVALID, nullptr, 0, DefaultVis, 2,
       "Rename <from_segment> as <to_segment>", "<from_segment> <to_segment>",
       nullptr)
OPTION(prefix_3, "-rename_section", anonymous_350, MultiArg, INVALID,
       rename_section, nullptr, 0, DefaultVis, 4, "Alias for --rename-section",
       "<from_segment> <from_section> <to_segment> <to_section>", nullptr)
OPTION(prefix_3, "-rename_segment", anonymous_351, MultiArg, INVALID,
       rename_segment, nullptr, 0, DefaultVis, 2, "Alias for --rename-segment",
       "<from_segment> <to_segment>", nullptr)
OPTION(prefix_3, "-reproducible", reproducible, Flag, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Make the output reproducible by removing timestamps and other "
       "non-deterministic data. This is the default behavior.",
       nullptr, nullptr)
OPTION(prefix_3, "-root_safe", root_safe, Flag, grp_rare, INVALID, nullptr, 0,
       DefaultVis, 0, "Set the MH_ROOT_SAFE bit in the mach-o header", nullptr,
       nullptr)
OPTION(
    prefix_1, "--rpath", rpath, Separate, grp_resolve, INVALID, nullptr, 0,
    DefaultVis, 0,
    "Add <path> to dyld search list for dylibs with load path prefix `@rpath/'",
    "<path>", nullptr)
OPTION(prefix_3, "-run_init_lazily", run_init_lazily, Flag, grp_obsolete,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-r", r, Flag, grp_kind, INVALID, nullptr, 0, DefaultVis, 0,
       "Merge multiple object files into one, retaining relocations", nullptr,
       nullptr)
OPTION(prefix_3, "-save-temps", save_temps, Flag, grp_introspect, INVALID,
       nullptr, 0, DefaultVis, 0, "Save intermediate LTO compilation results",
       nullptr, nullptr)
OPTION(prefix_3, "-sdk_version", sdk_version, Separate, grp_version, INVALID,
       nullptr, 0, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_1, "--search-dylibs-first", search_dylibs_first, Flag, grp_libs,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Search for lib<name>.dylib on first pass, then for lib<name>.a on "
       "second pass through search path (default for Xcode 3 and earlier)",
       nullptr, nullptr)
OPTION(prefix_1, "--search-paths-first", search_paths_first, Flag, grp_libs,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Search for lib<name>.dylib and lib<name>.a at each step in traversing "
       "search path (default for Xcode 4 and later)",
       nullptr, nullptr)
OPTION(prefix_3, "-search_dylibs_first", anonymous_352, Flag, INVALID,
       search_dylibs_first, nullptr, 0, DefaultVis, 0,
       "Alias for --search-dylibs-first", nullptr, nullptr)
OPTION(prefix_3, "-search_paths_first", anonymous_353, Flag, INVALID,
       search_paths_first, nullptr, 0, DefaultVis, 0,
       "Alias for --search-paths-first", nullptr, nullptr)
OPTION(prefix_3, "-sect_diff_relocs", sect_diff_relocs, Separate, grp_obsolete,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", "<treatment>", nullptr)
OPTION(prefix_1, "--sectalign", sectalign, MultiArg, grp_rare, INVALID, nullptr,
       0, DefaultVis, 3,
       "Align <section> within <segment> to hex power-of-2 <boundary>",
       "<segment> <section> <boundary>", nullptr)
OPTION(prefix_1, "--sectcreate", sectcreate, MultiArg, grp_content, INVALID,
       nullptr, 0, DefaultVis, 3,
       "Create <section> in <segment> from the contents of <file>",
       "<segment> <section> <file>", nullptr)
OPTION(prefix_3, "-section_order", section_order, MultiArg, grp_rare, INVALID,
       nullptr, HelpHidden, DefaultVis, 2,
       "With -preload, specify layout sequence of colon-separated <sections> "
       "in <segment>",
       "<segment> <sections>", nullptr)
OPTION(prefix_3, "-sectobjectsymbols", sectobjectsymbols, MultiArg,
       grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 2,
       "This option is obsolete in the native linker", "<segname> <sectname>",
       nullptr)
OPTION(prefix_3, "-sectorder_detail", sectorder_detail, Flag, grp_obsolete,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-sectorder", sectorder, MultiArg, grp_obsolete, INVALID,
       nullptr, HelpHidden, DefaultVis, 3,
       "Obsolete. Replaced by more general -order_file option",
       "<segname> <sectname> <orderfile>", nullptr)
OPTION(prefix_3, "-seg1addr", seg1addr, Separate, grp_opts, image_base, nullptr,
       HelpHidden, DefaultVis, 0, "Alias for -image_base", "<address>", nullptr)
OPTION(prefix_3, "-seg_addr_table_filename", seg_addr_table_filename, Separate,
       grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", "<path>", nullptr)
OPTION(prefix_3, "-seg_page_size", seg_page_size, MultiArg, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 2,
       "Specifies the page <size> for <segment>. Segment size will be a "
       "multiple of its page size",
       "<segment> <size>", nullptr)
OPTION(
    prefix_3, "-segaddr", segaddr, MultiArg, grp_rare, INVALID, nullptr, 0,
    DefaultVis, 2,
    "Specify the starting hex <address> at a 4KiB page boundary for <segment>",
    "<segment> <address>", nullptr)
OPTION(prefix_3, "-segalign", segalign, Separate, grp_rare, INVALID, nullptr, 0,
       DefaultVis, 0, "Align all segments to hex power-of-2 <boundary>",
       "<boundary>", nullptr)
OPTION(prefix_3, "-segcreate", segcreate, MultiArg, grp_content, sectcreate,
       nullptr, 0, DefaultVis, 3, "Alias for -sectcreate",
       "<segment> <section> <file>", nullptr)
OPTION(prefix_3, "-seglinkedit", seglinkedit, Flag, grp_obsolete, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-segment_order", segment_order, Separate, grp_rare, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "With -preload, specify layout sequence of colon-separated <segments>",
       "<colon_separated_segment_list>", nullptr)
OPTION(prefix_1, "--segprot", segprot, MultiArg, grp_rare, INVALID, nullptr, 0,
       DefaultVis, 3,
       "Specifies the <max> and <init> virtual memory protection of <segment> "
       "as r/w/x/-seg_addr_table path",
       "<segment> <max> <init>", nullptr)
OPTION(prefix_3, "-segs_read_only_addr", segs_read_only_addr, Separate,
       grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete", "<address>", nullptr)
OPTION(prefix_3, "-segs_read_write_addr", segs_read_write_addr, Separate,
       grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete", "<address>", nullptr)
OPTION(prefix_3, "-setuid_safe", setuid_safe, Flag, grp_rare, INVALID, nullptr,
       0, DefaultVis, 0, "Set the MH_SETUID_SAFE bit in the mach-o header",
       nullptr, nullptr)
OPTION(prefix_3, "-simulator_support", simulator_support, Flag,
       grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-single_module", single_module, Flag, grp_deprecated, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Unnecessary option: this is already the default", nullptr, nullptr)
OPTION(prefix_3, "-Si", Si, Flag, grp_obsolete, INVALID, nullptr, HelpHidden,
       DefaultVis, 0, "This option is obsolete in the native linker", nullptr,
       nullptr)
OPTION(prefix_3, "-slow_stubs", slow_stubs, Flag, grp_obsolete, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-snapshot_dir", snapshot_dir, Flag, grp_undocumented, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-Sn", Sn, Flag, grp_obsolete, INVALID, nullptr, HelpHidden,
       DefaultVis, 0, "This option is obsolete in the native linker", nullptr,
       nullptr)
OPTION(prefix_3, "-source_version", source_version, Separate, grp_undocumented,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Record <version> (A.B.C.D.E) in an LC_SOURCE_VERSION load command",
       "<version>", nullptr)
OPTION(prefix_3, "-Sp", Sp, Flag, grp_obsolete, INVALID, nullptr, HelpHidden,
       DefaultVis, 0, "This option is obsolete in the native linker", nullptr,
       nullptr)
OPTION(prefix_3, "-stack_addr", stack_addr, Separate, grp_obsolete, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete: LC_MAIN leaves the stack to the system",
       "<address>", nullptr)
OPTION(
    prefix_3, "-stack_size", stack_size, Separate, grp_main, INVALID, nullptr,
    0, DefaultVis, 0,
    "Maximum hex stack size for the main thread in a program. (default is 8MB)",
    "<size>", nullptr)
OPTION(prefix_2, "--start-lib", start_lib, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Start a grouping of objects that should be treated as if they were "
       "together in an archive",
       nullptr, nullptr)
OPTION(prefix_3, "-static", static, Flag, grp_kind, INVALID, nullptr, 0,
       DefaultVis, 0, "Link statically", nullptr, nullptr)
OPTION(prefix_2, "--strict-auto-link", strict_auto_link, Flag, grp_neverc_ext,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Always warn for missing frameworks or libraries if they are loaded via "
       "LC_LINKER_OPTIONS",
       nullptr, nullptr)
OPTION(prefix_1, "--sub-library", sub_library, Separate, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0, "Re-export the dylib as <name>", "<name>",
       nullptr)
OPTION(prefix_1, "--sub-umbrella", sub_umbrella, Separate, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0, "Re-export the framework as <name>", "<name>",
       nullptr)
OPTION(prefix_3, "-sub_library", anonymous_354, Separate, INVALID, sub_library,
       nullptr, 0, DefaultVis, 0, "Alias for --sub-library", "<name>", nullptr)
OPTION(prefix_3, "-sub_umbrella", anonymous_355, Separate, INVALID,
       sub_umbrella, nullptr, 0, DefaultVis, 0, "Alias for --sub-umbrella",
       "<name>", nullptr)
OPTION(prefix_3, "-syslibroot", syslibroot, Separate, grp_libs, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Prepend <rootdir> to all library and framework search paths",
       "<rootdir>", nullptr)
OPTION(prefix_3, "-S", S, Flag, grp_symtab, INVALID, nullptr, 0, DefaultVis, 0,
       "Strip debug information (STABS or DWARF) from the output", nullptr,
       nullptr)
OPTION(prefix_3, "-s", s, Flag, grp_obsolete, INVALID, nullptr, HelpHidden,
       DefaultVis, 0, "This option is obsolete in the native linker", nullptr,
       nullptr)
OPTION(prefix_3, "-text_exec", text_exec, Flag, grp_rare, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "Rename __segment TEXT to __TEXT_EXEC for sections __text and __stubs",
       nullptr, nullptr)
OPTION(prefix_3, "-threaded_starts_section", threaded_starts_section, Flag,
       grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_2, "--threads=", threads_eq, Joined, grp_neverc_ext, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Number of worker threads; 1 disables multi-threading (default: "
       "chosen from the input size and available CPUs)",
       "<number>", nullptr)
OPTION(prefix_2, "--time-trace-granularity=", time_trace_granularity_eq, Joined,
       grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0,
       "Minimum time granularity (in microseconds) traced by time profiler",
       nullptr, nullptr)
OPTION(prefix_2, "--time-trace=", time_trace_eq, Joined, grp_neverc_ext,
       INVALID, nullptr, 0, DefaultVis, 0, "Record time trace to <file>",
       "<file>", nullptr)
OPTION(prefix_2, "--time-trace", anonymous_356, Flag, grp_neverc_ext,
       time_trace_eq, nullptr, 0, DefaultVis, 0,
       "Record time trace to file next to output", nullptr, nullptr)
OPTION(prefix_3, "-trace_symbol_layout", trace_symbol_layout, Flag, grp_rare,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Show where and why symbols move, as specified by -move_to_ro_segment, "
       "-move_to_rw_segment, -rename_section, and -rename_segment",
       nullptr, nullptr)
OPTION(prefix_3, "-tvos_version_min", tvos_version_min, Separate, grp_version,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Oldest tvOS version for which linked output is usable", "<version>",
       nullptr)
OPTION(prefix_1, "--twolevel-namespace", twolevel_namespace, Flag, grp_resolve,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Make dyld look up symbols by (dylib,name) pairs (default)", nullptr,
       nullptr)
OPTION(prefix_3, "-twolevel_namespace_hints", twolevel_namespace_hints, Flag,
       grp_obsolete, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is obsolete in the native linker", nullptr, nullptr)
OPTION(prefix_3, "-twolevel_namespace", anonymous_357, Flag, INVALID,
       twolevel_namespace, nullptr, 0, DefaultVis, 0,
       "Alias for --twolevel-namespace", nullptr, nullptr)
OPTION(prefix_3, "-t", t, Flag, grp_introspect, INVALID, nullptr, 0, DefaultVis,
       0, "Log every file the linker loads: object, archive, and dylib",
       nullptr, nullptr)
OPTION(prefix_3, "-uikitformac_version_min", uikitformac_version_min, Separate,
       grp_version, maccatalyst_version_min, nullptr, HelpHidden, DefaultVis, 0,
       "Alias for -maccatalyst_version_min", "<version>", nullptr)
OPTION(prefix_1, "--umbrella", umbrella, Separate, grp_rare, INVALID, nullptr,
       0, DefaultVis, 0,
       "Re-export this dylib through the umbrella framework <name>", "<name>",
       nullptr)
OPTION(prefix_3, "-unaligned_pointers", unaligned_pointers, Separate, grp_rare,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Handle unaligned pointers in __DATA segments according to <treatment>: "
       "warning, error, or suppress (default for arm64e is error, otherwise "
       "suppress)",
       "<treatment>", nullptr)
OPTION(prefix_1, "--undefined", undefined, Separate, grp_resolve, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Handle undefined symbols according to <treatment>: error, warning, "
       "suppress, or dynamic_lookup (default is error)",
       "<treatment>", nullptr)
OPTION(prefix_1, "--unexported-symbols-list", unexported_symbols_list, Separate,
       grp_resolve, INVALID, nullptr, 0, DefaultVis, 0,
       "Global symbols specified in <file> become private externs", "<file>",
       nullptr)
OPTION(prefix_1, "--unexported-symbol", unexported_symbol, Separate,
       grp_resolve, INVALID, nullptr, 0, DefaultVis, 0,
       "Global <symbol> becomes private extern", "<symbol>", nullptr)
OPTION(prefix_3, "-unexported_symbols_list", anonymous_358, Separate, INVALID,
       unexported_symbols_list, nullptr, 0, DefaultVis, 0,
       "Alias for --unexported-symbols-list", "<file>", nullptr)
OPTION(prefix_3, "-unexported_symbol", anonymous_359, Separate, INVALID,
       unexported_symbol, nullptr, 0, DefaultVis, 0,
       "Alias for --unexported-symbol", "<symbol>", nullptr)
OPTION(prefix_3, "-upward-l", upward_l, Joined, grp_libs, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Like -l<name>, but specify dylib as an upward dependency", "<name>",
       nullptr)
OPTION(
    prefix_3, "-upward_framework", upward_framework, Separate, grp_libs,
    INVALID, nullptr, 0, DefaultVis, 0,
    "Like -framework <name>, but specify the framework as an upward dependency",
    "<name>", nullptr)
OPTION(prefix_3, "-upward_library", upward_library, Separate, grp_libs, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Like bare <path>, but specify dylib as an upward dependency", "<path>",
       nullptr)
OPTION(prefix_3, "-U", U, Separate, grp_resolve, INVALID, nullptr, 0,
       DefaultVis, 0, "Allow <symbol> to have no definition", "<symbol>",
       nullptr)
OPTION(prefix_3, "-u", u, Separate, grp_resolve, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Require that <symbol> be defined for the link to succeed", "<symbol>",
       nullptr)
OPTION(prefix_3, "-verbose_deduplicate", verbose_deduplicate, Flag, grp_rare,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Print function names eliminated by deduplication and the total size of "
       "code savings",
       nullptr, nullptr)
OPTION(prefix_3, "-verbose_optimization_hints", verbose_optimization_hints,
       Flag, grp_undocumented, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "This option is undocumented in the native linker", nullptr, nullptr)
OPTION(prefix_2, "--verbose", verbose, Flag, grp_neverc_ext, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_3, "-version_details", version_details, Flag, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0, "Print the linker version in JSON form",
       nullptr, nullptr)
OPTION(prefix_3, "-version_load_command", version_load_command, Flag,
       grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Emit the minimum OS version load command (always done)", nullptr,
       nullptr)
OPTION(prefix_2, "--version", version, Flag, grp_neverc_ext, INVALID, nullptr,
       0, DefaultVis, 0, "Display the version number and exit", nullptr,
       nullptr)
OPTION(prefix_3, "-v", v, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Print the linker version and search paths in addition to linking",
       nullptr, nullptr)
OPTION(prefix_2, "--warn-duplicate-rpath", warn_duplicate_rpath, Flag,
       grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0,
       "Warn if the same -rpath is specified multiple times (default)", nullptr,
       nullptr)
OPTION(prefix_2, "--warn-dylib-install-name", warn_dylib_install_name, Flag,
       grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0,
       "Warn on -install_name if -dylib is not passed", nullptr, nullptr)
OPTION(prefix_3, "-warn_commons", warn_commons, Flag, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Warn when a tentative definition in an object file matches an external "
       "symbol in a dylib, which often means \\\"extern\\\" is missing from a "
       "variable declaration in a header file",
       nullptr, nullptr)
OPTION(prefix_3, "-warn_compact_unwind", warn_compact_unwind, Flag, grp_rare,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Warn for each FDE that cannot compact into the __unwind_info section "
       "and must remain in the __eh_frame section",
       nullptr, nullptr)
OPTION(prefix_3, "-warn_stabs", warn_stabs, Flag, grp_rare, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Warn when an input's own stabs debug entries are dropped", nullptr,
       nullptr)
OPTION(prefix_3, "-warn_weak_exports", warn_weak_exports, Flag, grp_rare,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Warn if the linked image contains weak external symbols", nullptr,
       nullptr)
OPTION(prefix_3, "-watchos_version_min", watchos_version_min, Separate,
       grp_version, INVALID, nullptr, 0, DefaultVis, 0,
       "Oldest watchOS version for which linked output is usable", "<version>",
       nullptr)
OPTION(prefix_1, "--weak-framework", weak_framework, Separate, grp_libs,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Like --framework <name>, but mark framework and its references as weak "
       "imports",
       "<name>", nullptr)
OPTION(prefix_1, "--weak-library", weak_library, Separate, grp_libs, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Like bare <path>, but mark library and its references as weak imports",
       "<path>", nullptr)
OPTION(prefix_3, "-weak-l", weak_l, Joined, grp_libs, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Like -l<name>, but mark library and its references as weak imports",
       "<name>", nullptr)
OPTION(prefix_3, "-weak_framework", anonymous_360, Separate, INVALID,
       weak_framework, nullptr, 0, DefaultVis, 0, "Alias for --weak-framework",
       "<name>", nullptr)
OPTION(prefix_3, "-weak_library", anonymous_361, Separate, INVALID,
       weak_library, nullptr, 0, DefaultVis, 0, "Alias for --weak-library",
       "<path>", nullptr)
OPTION(prefix_3, "-weak_reference_mismatches", weak_reference_mismatches,
       Separate, grp_rare, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Resolve symbol imports of conflicting weakness according to "
       "<treatment> as weak, non-weak, or error (default is non-weak)",
       "<treatment>", nullptr)
OPTION(prefix_3, "-whatsloaded", whatsloaded, Flag, grp_introspect, INVALID,
       nullptr, 0, DefaultVis, 0, "Logs only the object files the linker loads",
       nullptr, nullptr)
OPTION(prefix_1, "--why-live", why_live, Separate, grp_introspect, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Log a chain of references to <symbol>, for use with --dead-strip",
       "<symbol>", nullptr)
OPTION(prefix_1, "--why-load", why_load, Flag, grp_introspect, INVALID, nullptr,
       0, DefaultVis, 0,
       "Log why each object file is loaded from a static library", nullptr,
       nullptr)
OPTION(prefix_3, "-why_live", anonymous_362, Separate, INVALID, why_live,
       nullptr, 0, DefaultVis, 0, "Alias for --why-live", "<symbol>", nullptr)
OPTION(prefix_3, "-why_load", anonymous_363, Flag, INVALID, why_load, nullptr,
       0, DefaultVis, 0, "Alias for --why-load", nullptr, nullptr)
OPTION(prefix_3, "-whyload", whyload, Flag, grp_introspect, why_load, nullptr,
       0, DefaultVis, 0, "Alias for -why_load", nullptr, nullptr)
OPTION(prefix_3, "-w", w, Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Suppress all warnings", nullptr, nullptr)
OPTION(prefix_3, "-X", X, Flag, grp_obsolete, INVALID, nullptr, HelpHidden,
       DefaultVis, 0, "This option is obsolete in the native linker", nullptr,
       nullptr)
OPTION(prefix_3, "-x", x, Flag, grp_symtab, INVALID, nullptr, 0, DefaultVis, 0,
       "Exclude non-global symbols from the output symbol table", nullptr,
       nullptr)
OPTION(prefix_3, "-Y", Y, Separate, grp_obsolete, INVALID, nullptr, HelpHidden,
       DefaultVis, 0, "This option is obsolete in the native linker",
       "<number>", nullptr)
OPTION(prefix_3, "-y", y, Joined, grp_obsolete, INVALID, nullptr, HelpHidden,
       DefaultVis, 0, "This option is obsolete in the native linker",
       "<symbol>", nullptr)
OPTION(
    prefix_3, "-Z", Z, Flag, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
    "Remove standard directories from the library and framework search paths",
    nullptr, nullptr)
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
