/////////
// Prefixes

#ifdef PREFIX
#define COMMA ,
PREFIX(prefix_0, {llvm::StringLiteral("")})
PREFIX(prefix_3, {llvm::StringLiteral("-") COMMA llvm::StringLiteral("")})
PREFIX(prefix_2, {llvm::StringLiteral("--") COMMA llvm::StringLiteral("")})
PREFIX(prefix_1, {llvm::StringLiteral("--") COMMA llvm::StringLiteral("-")
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
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "dylib", grp_dylib, Group,
       INVALID, INVALID, nullptr, 0, 0, 0, "DYNAMIC LIBRARIES (DYLIB)", nullptr,
       nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "introspect", grp_introspect,
       Group, INVALID, INVALID, nullptr, 0, 0, 0, "INTROSPECTING THE LINKER",
       nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "libs", grp_libs, Group, INVALID,
       INVALID, nullptr, 0, 0, 0, "LIBRARIES", nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "main", grp_main, Group, INVALID,
       INVALID, nullptr, 0, 0, 0, "MAIN EXECUTABLE", nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "kind", grp_neverc_ext, Group,
       INVALID, INVALID, nullptr, 0, 0, 0, "NEVERC LINKER EXTENSIONS", nullptr,
       nullptr)
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
OPTION(prefix_1, "--adhoc-codesign", adhoc_codesign, Flag, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Write an ad-hoc code signature to the output file (default for arm64 "
       "binaries)",
       nullptr, nullptr)
OPTION(prefix_1, "--alias", alias, MultiArg, grp_resolve, INVALID, nullptr, 0,
       DefaultVis, 2, "Create a symbol alias with default global visibility",
       "<symbol_name> <alternate_name>", nullptr)
OPTION(prefix_1, "--all-load", all_load, Flag, grp_libs, INVALID, nullptr, 0,
       DefaultVis, 0, "Load all members of all static archive libraries",
       nullptr, nullptr)
OPTION(prefix_1, "--application-extension", application_extension, Flag,
       grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Mark output as safe for use in an application extension, and validate "
       "that linked dylibs are safe",
       nullptr, nullptr)
OPTION(prefix_1, "--arch-errors-fatal", arch_errors_fatal, Flag, grp_rare,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Escalate to errors any warnings about inputs whose architecture does "
       "not match the -arch option",
       nullptr, nullptr)
OPTION(prefix_1, "--arch-multiple", arch_multiple, Flag, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Augment error and warning messages with the architecture name", nullptr,
       nullptr)
OPTION(prefix_1, "--bundle-loader", bundle_loader, Separate, grp_bundle,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Resolve undefined symbols from <executable>", "<executable>", nullptr)
OPTION(prefix_1, "--compatibility-version", compatibility_version, Separate,
       grp_dylib, INVALID, nullptr, 0, DefaultVis, 0,
       "Compatibility <version> of this library", "<version>", nullptr)
OPTION(prefix_1, "--current-version", current_version, Separate, grp_dylib,
       INVALID, nullptr, 0, DefaultVis, 0, "Current <version> of this library",
       "<version>", nullptr)
OPTION(prefix_1, "--data-const", data_const, Flag, grp_rare, INVALID, nullptr,
       0, DefaultVis, 0,
       "Force migration of readonly data into __DATA_CONST segment", nullptr,
       nullptr)
OPTION(prefix_1, "--data-in-code-info", data_in_code_info, Flag,
       grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Emit data-in-code information (default)", nullptr, nullptr)
OPTION(prefix_2, "--dead-strip-duplicates", dead_strip_duplicates, Flag,
       grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not error on duplicate symbols that will be dead stripped.", nullptr,
       nullptr)
OPTION(
    prefix_1, "--dead-strip-dylibs", dead_strip_dylibs, Flag, grp_rare, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Remove dylibs that are unreachable by the entry point or exported symbols",
    nullptr, nullptr)
OPTION(prefix_2, "--deduplicate-strings", deduplicate_strings, Flag,
       grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0,
       "Enable string deduplication", nullptr, nullptr)
OPTION(prefix_1, "--dependency-info", dependency_info, Separate, grp_introspect,
       INVALID, nullptr, 0, DefaultVis, 0, "Dump dependency info", "<path>",
       nullptr)
OPTION(prefix_1, "--dyld-env", dyld_env, Separate, grp_rare, INVALID, nullptr,
       0, DefaultVis, 0, "Specifies a LC_DYLD_ENVIRONMENT variable value pair.",
       "<dyld_env_var>", nullptr)
OPTION(prefix_1, "--encryptable", encryptable, Flag, grp_undocumented, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Generate the LC_ENCRYPTION_INFO load command", nullptr, nullptr)
OPTION(prefix_2, "--end-lib", end_lib, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "End a grouping of objects that should be treated as if they were "
       "together in an archive",
       nullptr, nullptr)
OPTION(prefix_1, "--export-dynamic", export_dynamic, Flag, grp_main, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Preserve all global symbols during LTO and when dead-stripping "
       "executables",
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
OPTION(prefix_3, "-e", e, Separate, grp_rare, INVALID, nullptr, 0, DefaultVis,
       0,
       "Make <symbol> the entry point of an executable (default is \"start\" "
       "from crt1.o)",
       "<symbol>", nullptr)
OPTION(prefix_1, "--filelist", filelist, Separate, grp_content, INVALID,
       nullptr, 0, DefaultVis, 0, "Read names of files to link from <file>",
       "<file>", nullptr)
OPTION(prefix_1, "--final-output", final_output, Separate, grp_rare, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Specify dylib install name if --install-name is not used; used by "
       "compiler driver for multiple -arch arguments",
       "<name>", nullptr)
OPTION(prefix_1, "--fixup-chains", fixup_chains, Flag, grp_undocumented,
       INVALID, nullptr, 0, DefaultVis, 0, "Emit chained fixups", nullptr,
       nullptr)
OPTION(prefix_1, "--flat-namespace", flat_namespace, Flag, grp_resolve, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Resolve symbols from all dylibs, both direct and transitive. Do not "
       "record source libraries: dyld must re-search at runtime and use the "
       "first definition found",
       nullptr, nullptr)
OPTION(prefix_1, "--force-load", force_load, Separate, grp_libs, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Load all members static archive library at <path>", "<path>", nullptr)
OPTION(prefix_1, "--framework", framework, Separate, grp_libs, INVALID, nullptr,
       0, DefaultVis, 0,
       "Search for <name>.framework/<name> on the framework search path",
       "<name>", nullptr)
OPTION(prefix_1, "--function-starts", function_starts, Flag, grp_undocumented,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Create table of function start addresses (default)", nullptr, nullptr)
OPTION(prefix_3, "-F", F, JoinedOrSeparate, grp_libs, INVALID, nullptr, 0,
       DefaultVis, 0, "Add dir to the framework search path", "<dir>", nullptr)
OPTION(prefix_1, "--headerpad-max-install-names", headerpad_max_install_names,
       Flag, grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Allocate extra space so all load-command paths can expand to "
       "MAXPATHLEN via install_name_tool",
       nullptr, nullptr)
OPTION(prefix_1, "--headerpad", headerpad, Separate, grp_rare, INVALID, nullptr,
       0, DefaultVis, 0,
       "Allocate hex <size> extra space for future expansion of the load "
       "commands via install_name_tool (default is 0x20)",
       "<size>", nullptr)
OPTION(prefix_3, "-hidden-l", hidden_l, Joined, grp_libs, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Like -l<name>, but load all symbols with hidden visibility", "<name>",
       nullptr)
OPTION(prefix_2, "--ignore-auto-link-option", ignore_auto_link_option, Separate,
       grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "--ignore-auto-link", ignore_auto_link, Flag, grp_rare,
       INVALID, nullptr, 0, DefaultVis, 0, "Ignore LC_LINKER_OPTIONs", nullptr,
       nullptr)
OPTION(prefix_1, "--ignore-optimization-hints", ignore_optimization_hints, Flag,
       grp_undocumented, INVALID, nullptr, 0, DefaultVis, 0,
       "Ignore Linker Optimization Hints", nullptr, nullptr)
OPTION(prefix_1, "--init-offsets", init_offsets, Flag, grp_undocumented,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Store __TEXT segment offsets of static initializers", nullptr, nullptr)
OPTION(prefix_1, "--install-name", install_name, Separate, grp_dylib, INVALID,
       nullptr, 0, DefaultVis, 0, "Set an internal install path in a dylib",
       "<name>", nullptr)
OPTION(prefix_1, "--load-hidden", load_hidden, Separate, grp_libs, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Load all symbols from static library with hidden visibility", "<path>",
       nullptr)
OPTION(prefix_3, "-L", L, JoinedOrSeparate, grp_libs, INVALID, nullptr, 0,
       DefaultVis, 0, "Add dir to the library search path", "<dir>", nullptr)
OPTION(prefix_3, "-l", l, Joined, grp_libs, INVALID, nullptr, 0, DefaultVis, 0,
       "Search for lib<name>.dylib or lib<name>.a on the library search path",
       "<name>", nullptr)
OPTION(prefix_1, "--mark-dead-strippable-dylib", mark_dead_strippable_dylib,
       Flag, grp_dylib, INVALID, nullptr, 0, DefaultVis, 0,
       "Mark output dylib as dead-strippable: When a client links against it "
       "but does not use any of its symbols, the dylib will not be added to "
       "the client's list of needed dylibs",
       nullptr, nullptr)
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
OPTION(prefix_1, "--no-adhoc-codesign", no_adhoc_codesign, Flag, grp_rare,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Do not write an ad-hoc code signature to the output file (default for "
       "x86_64 binaries)",
       nullptr, nullptr)
OPTION(prefix_1, "--no-application-extension", no_application_extension, Flag,
       grp_rare, INVALID, nullptr, 0, DefaultVis, 0,
       "Disable application extension functionality (default)", nullptr,
       nullptr)
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
OPTION(prefix_2, "--no-warn-dylib-install-name", no_warn_dylib_install_name,
       Flag, grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not warn on -install_name if -dylib is not passed (default)",
       nullptr, nullptr)
OPTION(prefix_1, "--noall-load", noall_load, Flag, grp_libs, INVALID, nullptr,
       0, DefaultVis, 0,
       "Don't load all static members from archives, this is the default, this "
       "negates --all-load",
       nullptr, nullptr)
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
OPTION(prefix_1, "--order-file", order_file, Separate, grp_opts, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Layout functions and data according to specification in <file>",
       "<file>", nullptr)
OPTION(prefix_1, "--oso-prefix", oso_prefix, Separate, grp_symtab, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Remove the prefix <path> from OSO symbols in the debug map", "<path>",
       nullptr)
OPTION(prefix_2, "--override=", override_eq, Joined, grp_neverc_ext, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Allow symbol to override any other definition without error",
       "<symbol>", nullptr)
OPTION(
    prefix_1, "--pagezero-size", pagezero_size, Separate, grp_main, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Size of unreadable segment at address zero is hex <size> (default is 4GB)",
    "<size>", nullptr)
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
OPTION(prefix_1, "--rename-section", rename_section, MultiArg, grp_rare,
       INVALID, nullptr, 0, DefaultVis, 4,
       "Rename <from_segment>/<from_section> as <to_segment>/<to_section>",
       "<from_segment> <from_section> <to_segment> <to_section>", nullptr)
OPTION(prefix_1, "--rename-segment", rename_segment, MultiArg, grp_rare,
       INVALID, nullptr, 0, DefaultVis, 2,
       "Rename <from_segment> as <to_segment>", "<from_segment> <to_segment>",
       nullptr)
OPTION(
    prefix_1, "--rpath", rpath, Separate, grp_resolve, INVALID, nullptr, 0,
    DefaultVis, 0,
    "Add <path> to dyld search list for dylibs with load path prefix `@rpath/'",
    "<path>", nullptr)
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
OPTION(prefix_1, "--sectalign", sectalign, MultiArg, grp_rare, INVALID, nullptr,
       0, DefaultVis, 3,
       "Align <section> within <segment> to hex power-of-2 <boundary>",
       "<segment> <section> <boundary>", nullptr)
OPTION(prefix_1, "--sectcreate", sectcreate, MultiArg, grp_content, INVALID,
       nullptr, 0, DefaultVis, 3,
       "Create <section> in <segment> from the contents of <file>",
       "<segment> <section> <file>", nullptr)
OPTION(prefix_1, "--segprot", segprot, MultiArg, grp_rare, INVALID, nullptr, 0,
       DefaultVis, 3,
       "Specifies the <max> and <init> virtual memory protection of <segment> "
       "as r/w/x/-seg_addr_table path",
       "<segment> <max> <init>", nullptr)
OPTION(prefix_2, "--start-lib", start_lib, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Start a grouping of objects that should be treated as if they were "
       "together in an archive",
       nullptr, nullptr)
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
OPTION(prefix_1, "--twolevel-namespace", twolevel_namespace, Flag, grp_resolve,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Make dyld look up symbols by (dylib,name) pairs (default)", nullptr,
       nullptr)
OPTION(prefix_1, "--umbrella", umbrella, Separate, grp_rare, INVALID, nullptr,
       0, DefaultVis, 0,
       "Re-export this dylib through the umbrella framework <name>", "<name>",
       nullptr)
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
OPTION(prefix_3, "-U", U, Separate, grp_resolve, INVALID, nullptr, 0,
       DefaultVis, 0, "Allow <symbol> to have no definition", "<symbol>",
       nullptr)
OPTION(prefix_3, "-u", u, Separate, grp_resolve, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Require that <symbol> be defined for the link to succeed", "<symbol>",
       nullptr)
OPTION(prefix_2, "--warn-dylib-install-name", warn_dylib_install_name, Flag,
       grp_neverc_ext, INVALID, nullptr, 0, DefaultVis, 0,
       "Warn on -install_name if -dylib is not passed", nullptr, nullptr)
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
OPTION(prefix_1, "--why-live", why_live, Separate, grp_introspect, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Log a chain of references to <symbol>, for use with --dead-strip",
       "<symbol>", nullptr)
OPTION(prefix_1, "--why-load", why_load, Flag, grp_introspect, INVALID, nullptr,
       0, DefaultVis, 0,
       "Log why each object file is loaded from a static library", nullptr,
       nullptr)
OPTION(prefix_3, "-x", x, Flag, grp_symtab, INVALID, nullptr, 0, DefaultVis, 0,
       "Exclude non-global symbols from the output symbol table", nullptr,
       nullptr)
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
