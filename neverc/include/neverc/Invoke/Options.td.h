/////////
// Prefixes

#ifdef PREFIX
#define COMMA ,
PREFIX(prefix_0, {llvm::StringLiteral("")})
PREFIX(prefix_1, {llvm::StringLiteral("-") COMMA llvm::StringLiteral("")})
PREFIX(prefix_2, {llvm::StringLiteral("-") COMMA llvm::StringLiteral("--")
                      COMMA llvm::StringLiteral("")})
PREFIX(prefix_3, {llvm::StringLiteral("--") COMMA llvm::StringLiteral("")})
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
#define VALUES_CODE std_EQ_Values

static constexpr const char VALUES_CODE[] =
#define LANGSTANDARD(id, name, lang, desc, features) name ","
#define LANGSTANDARD_ALIAS(id, alias) alias ","
#include "neverc/Foundation/LangOpts/LangStandards.def"
    ;

#undef VALUES_CODE
#endif
/////////
// Groups

#ifdef OPTION
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<action group>", Action_Group,
       Group, INVALID, INVALID, nullptr, 0, 0, 0, nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<CompileOnly group>",
       CompileOnly_Group, Group, INVALID, INVALID, nullptr, 0, 0, 0, nullptr,
       nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<g group>", DebugInfo_Group,
       Group, CompileOnly_Group, INVALID, nullptr, 0, 0, 0, nullptr, nullptr,
       nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<W/R group>", Diag_Group, Group,
       CompileOnly_Group, INVALID, nullptr, 0, 0, 0, nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<I group>", I_Group, Group,
       IncludePath_Group, INVALID, nullptr, 0, 0, 0, nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<I/i group>", IncludePath_Group,
       Group, Preprocessor_Group, INVALID, nullptr, 0, 0, 0, nullptr, nullptr,
       nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<T/e/s/t/u group>", Link_Group,
       Group, INVALID, INVALID, nullptr, 0, 0, 0, nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<LongDouble group>",
       LongDouble_Group, Group, m_Group, INVALID, nullptr, 0, 0, 0, nullptr,
       nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<M group>", M_Group, Group,
       Preprocessor_Group, INVALID, nullptr, 0, 0, 0, nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<O group>", O_Group, Group,
       CompileOnly_Group, INVALID, nullptr, 0, 0, 0, nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<Preprocessor group>",
       Preprocessor_Group, Group, CompileOnly_Group, INVALID, nullptr, 0, 0, 0,
       nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<R group>", R_Group, Group,
       Diag_Group, INVALID, nullptr, 0, 0, 0, nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<R (with value) group>",
       R_value_Group, Group, R_Group, INVALID, nullptr, 0, 0, 0, nullptr,
       nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<T group>", T_Group, Group,
       Link_Group, INVALID, nullptr, 0, 0, 0, nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<W group>", W_Group, Group,
       Diag_Group, INVALID, nullptr, 0, 0, 0, nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<W (with value) group>",
       W_value_Group, Group, W_Group, INVALID, nullptr, 0, 0, 0, nullptr,
       nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<d group>", d_Group, Group,
       Preprocessor_Group, INVALID, nullptr, 0, 0, 0, nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<f group>", f_Group, Group,
       CompileOnly_Group, INVALID, nullptr, 0, 0, 0, nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<f (neverc-only) group>",
       f_neverc_Group, Group, CompileOnly_Group, INVALID, nullptr, 0, 0, 0,
       nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<gN group>", gN_Group, Group,
       g_Group, INVALID, nullptr, 0, 0, 0, nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<gTune group>", gTune_Group,
       Group, g_Group, INVALID, nullptr, 0, 0, 0, nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<g group>", g_Group, Group,
       DebugInfo_Group, INVALID, nullptr, 0, 0, 0, nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<g flags group>", g_flags_Group,
       Group, DebugInfo_Group, INVALID, nullptr, 0, 0, 0, nullptr, nullptr,
       nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<ggdbN group>", ggdbN_Group,
       Group, gN_Group, INVALID, nullptr, 0, 0, 0, nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<i group>", i_Group, Group,
       IncludePath_Group, INVALID, nullptr, 0, 0, 0, nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<NeverC internal options>",
       internal_Group, Group, INVALID, INVALID, nullptr, 0, 0, 0, nullptr,
       nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(),
       "<NeverC debug/development internal options>", internal_debug_Group,
       Group, internal_Group, INVALID, nullptr, 0, 0, 0,
       "DEBUG/DEVELOPMENT OPTIONS", nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(),
       "<NeverC driver internal options>", internal_driver_Group, Group,
       internal_Group, INVALID, nullptr, 0, 0, 0, "DRIVER OPTIONS", nullptr,
       nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<m group>", m_Group, Group,
       CompileOnly_Group, INVALID, nullptr, 0, 0, 0, nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<aarch64 features group>",
       m_aarch64_Features_Group, Group, m_Group, INVALID, nullptr, 0, 0, 0,
       nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<x86 AVX10 features group>",
       m_x86_AVX10_Features_Group, Group, m_Group, INVALID, nullptr, 0, 0, 0,
       nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<x86 features group>",
       m_x86_Features_Group, Group, m_Group, INVALID, nullptr, 0, 0, 0, nullptr,
       nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(),
       "<MSVC cross-compilation options>", msvc_Group, Group, INVALID, INVALID,
       nullptr, 0, 0, 0, nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<MSVC runtime group>",
       msvc_runtime_Group, Group, msvc_Group, INVALID, nullptr, 0, 0, 0,
       nullptr, nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<neverc i group>",
       neverc_i_Group, Group, i_Group, INVALID, nullptr, 0, 0, 0, nullptr,
       nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<pedantic group>",
       pedantic_Group, Group, f_Group, INVALID, nullptr, 0, 0, 0, nullptr,
       nullptr, nullptr)
OPTION(llvm::ArrayRef<llvm::StringLiteral>(), "<u group>", u_Group, Group,
       Link_Group, INVALID, nullptr, 0, 0, 0, nullptr, nullptr, nullptr)

//////////
// Options

OPTION(prefix_0, "<input>", INPUT, Input, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_0, "<unknown>", UNKNOWN, Unknown, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-###", _HASH_HASH_HASH, Flag, INVALID, INVALID, nullptr,
       NoXarchOption, DefaultVis, 0,
       "Print (but do not run) the commands to run for this compilation",
       nullptr, nullptr)
OPTION(prefix_1, "-all_load", all__load, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-allowable_client", allowable__client, Separate, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "-ansi", ansi, Flag, CompileOnly_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-arch_errors_fatal", arch__errors__fatal, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-arch", arch, Separate, INVALID, INVALID, nullptr,
       NoXarchOption | TargetSpecific, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-as-secure-log-file", as_secure_log_file, Separate, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Emit .secure_log_unique directives to this filename.", nullptr, nullptr)
OPTION(prefix_3, "--autocomplete=", autocomplete, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-bind_at_load", bind__at__load, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-bundle_loader", bundle__loader, Separate, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-bundle", bundle, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-B", B, JoinedOrSeparate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Search $prefix$file for executables, libraries, and data files. If "
       "$prefix is a directory, search $prefix/$file",
       "<prefix>", nullptr)
OPTION(prefix_1, "-b", b, JoinedOrSeparate, Link_Group, INVALID, nullptr,
       LinkerInput, DefaultVis, 0, "Pass -b <arg> to the linker", "<arg>",
       nullptr)
OPTION(prefix_1, "-c-isystem", c_isystem, Separate, INVALID, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "Add directory to the C SYSTEM include search path", "<directory>",
       nullptr)
OPTION(prefix_1, "-canonical-prefixes", canonical_prefixes, Flag, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Use absolute paths for invoking subcommands (default)", nullptr,
       nullptr)
OPTION(prefix_1, "-ccc-gcc-name", ccc_gcc_name, Separate, internal_driver_Group,
       INVALID, nullptr, NoXarchOption | HelpHidden, DefaultVis, 0,
       "Name for native GCC compiler", "<gcc-path>", nullptr)
OPTION(prefix_1, "-ccc-install-dir", ccc_install_dir, Separate,
       internal_debug_Group, INVALID, nullptr, NoXarchOption | HelpHidden,
       DefaultVis, 0, "Simulate installation in the given directory", nullptr,
       nullptr)
OPTION(prefix_1, "-ccc-print-bindings", ccc_print_bindings, Flag,
       internal_debug_Group, INVALID, nullptr, NoXarchOption | HelpHidden,
       DefaultVis, 0, "Show bindings of tools to actions", nullptr, nullptr)
OPTION(prefix_1, "-ccc-print-phases", ccc_print_phases, Flag, INVALID, INVALID,
       nullptr, NoXarchOption | HelpHidden, DefaultVis, 0,
       "Dump list of actions to perform", nullptr, nullptr)
OPTION(prefix_1, "-ccc-", ccc_, Joined, internal_Group, INVALID, nullptr,
       Unsupported | HelpHidden, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-CC", CC, Flag, Preprocessor_Group, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Include comments from within macros in preprocessed output", nullptr,
       nullptr)
OPTION(prefix_1, "-cfguard-no-checks", cfguard_no_checks, Flag, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Emit Windows Control Flow Guard tables only (no checks)", nullptr,
       nullptr)
OPTION(prefix_1, "-cfguard", cfguard, Flag, INVALID, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "Emit Windows Control Flow Guard tables and checks", nullptr, nullptr)
OPTION(prefix_1, "-clear-ast-before-backend", clear_ast_before_backend, Flag,
       INVALID, INVALID, nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
       "Clear the NeverC AST before running backend code generation", nullptr,
       nullptr)
OPTION(prefix_1, "-client_name", client__name, JoinedOrSeparate, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-compatibility_version", compatibility__version,
       JoinedOrSeparate, INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-complex-range=", complex_range_EQ, Joined, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, "full,limited,improved")
OPTION(prefix_2, "-compress-debug-sections=", compress_debug_sections_EQ,
       Joined, INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "DWARF debug sections compression type", nullptr, "none,zlib,zstd")
OPTION(prefix_2, "-compress-debug-sections", compress_debug_sections, Flag,
       INVALID, compress_debug_sections_EQ, "zlib\0", HelpHidden, DefaultVis, 0,
       nullptr, nullptr, nullptr)
OPTION(prefix_3, "--config-system-dir=", config_system_dir_EQ, Joined, INVALID,
       INVALID, nullptr, NoXarchOption | HelpHidden, DefaultVis, 0,
       "System directory for configuration files", nullptr, nullptr)
OPTION(prefix_3, "--config-user-dir=", config_user_dir_EQ, Joined, INVALID,
       INVALID, nullptr, NoXarchOption | HelpHidden, DefaultVis, 0,
       "User directory for configuration files", nullptr, nullptr)
OPTION(prefix_3, "--config=", config, Joined, INVALID, INVALID, nullptr,
       NoXarchOption, DefaultVis, 0, "Specify configuration file", "<file>",
       nullptr)
OPTION(prefix_3, "--config", anonymous_22, Separate, INVALID, config, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-create-dll-debug", create_dll_debug, Flag, msvc_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "Create debug DLL", nullptr, nullptr)
OPTION(prefix_1, "-create-dll", create_dll, Flag, msvc_Group, INVALID, nullptr,
       0, DefaultVis, 0, "Create DLL", nullptr, nullptr)
OPTION(prefix_1, "-current_version", current__version, JoinedOrSeparate,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-C", C, Flag, Preprocessor_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Include comments in preprocessed output", nullptr,
       nullptr)
OPTION(prefix_1, "-c", c, Flag, Action_Group, INVALID, nullptr, NoXarchOption,
       DefaultVis, 0, "Only run preprocess, compile, and assemble steps",
       nullptr, nullptr)
OPTION(prefix_1, "-dA", dA, Flag, INVALID, fverbose_asm, nullptr, 0, DefaultVis,
       0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-dD", dD, Flag, d_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Print macro definitions in -E mode in addition to normal output",
       nullptr, nullptr)
OPTION(prefix_1, "-dead_strip", dead__strip, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-debug-info-kind=", debug_info_kind_EQ, Joined, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-debugger-tuning=", debugger_tuning_EQ, Joined, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0, nullptr, nullptr,
       "gdb,lldb")
OPTION(prefix_1, "-default-function-attr", default_function_attr, Separate,
       INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Apply given attribute to all functions", nullptr, nullptr)
OPTION(prefix_1, "-defsym", defsym, Separate, INVALID, INVALID, nullptr,
       HelpHidden, DefaultVis, 0, "Define a value for a symbol", nullptr,
       nullptr)
OPTION(prefix_1, "-dependency-file", dependency_file, Separate, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Filename (or -) to write dependency output to", nullptr, nullptr)
OPTION(prefix_3, "--dependent-lib=", dependent_lib, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Add dependent library", nullptr, nullptr)
OPTION(prefix_2, "-diasdkdir", diasdkdir, JoinedOrSeparate, msvc_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Path to the DIA SDK", "<dir>", nullptr)
OPTION(prefix_1, "-disable-free", disable_free, Flag, INVALID, INVALID, nullptr,
       HelpHidden, DefaultVis, 0, "Disable freeing of memory on exit", nullptr,
       nullptr)
OPTION(prefix_1, "-disable-lifetime-markers", disable_lifetimemarkers, Flag,
       INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Disable lifetime-markers emission even when optimizations are enabled",
       nullptr, nullptr)
OPTION(prefix_1, "-disable-llvm-optzns", disable_llvm_optzns, Flag, INVALID,
       disable_llvm_passes, nullptr, HelpHidden, DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-disable-llvm-passes", disable_llvm_passes, Flag, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Use together with -emit-llvm to get pristine LLVM IR from the frontend "
       "by not running any LLVM passes at all",
       nullptr, nullptr)
OPTION(prefix_1, "-disable-llvm-verifier", disable_llvm_verifier, Flag, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Don't run the LLVM IR verifier pass", nullptr, nullptr)
OPTION(prefix_1, "-disable-O0-optnone", disable_O0_optnone, Flag, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Disable adding the optnone attribute to functions at O0", nullptr,
       nullptr)
OPTION(prefix_1, "-disable-red-zone", disable_red_zone, Flag, INVALID, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Do not emit code that uses the red zone.", nullptr, nullptr)
OPTION(prefix_1, "-discard-value-names", discard_value_names, Flag, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Discard value names in LLVM IR", nullptr, nullptr)
OPTION(prefix_1, "-dI", dI, Flag, d_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Print include directives in -E mode in addition to normal output",
       nullptr, nullptr)
OPTION(prefix_1, "-dM", dM, Flag, d_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Print macro definitions in -E mode instead of normal output", nullptr,
       nullptr)
OPTION(prefix_1, "-dsym-dir", dsym_dir, JoinedOrSeparate, INVALID, INVALID,
       nullptr, NoXarchOption | RenderAsInput, DefaultVis, 0,
       "Directory to output dSYM's (if any) to", "<dir>", nullptr)
OPTION(prefix_1, "-dumpdir", dumpdir, Separate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Use <dumpfpx> as a prefix to form auxiliary and dump file names",
       "<dumppfx>", nullptr)
OPTION(prefix_1, "-dumpmachine", dumpmachine, Flag, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, "Display the compiler's target processor", nullptr,
       nullptr)
OPTION(prefix_1, "-dumpversion", dumpversion, Flag, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, "Display the version of the compiler", nullptr,
       nullptr)
OPTION(prefix_1, "-dwarf-debug-flags", dwarf_debug_flags, Separate, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "The string to embed in the Dwarf debug flags record.", nullptr, nullptr)
OPTION(prefix_1, "-dwarf-version=", dwarf_version_EQ, Joined, INVALID, INVALID,
       nullptr, HelpHidden, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_3, "--dyld-prefix=", _dyld_prefix_EQ, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-dylib_file", dylib__file, Separate, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-dylinker_install_name", dylinker__install__name,
       JoinedOrSeparate, INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-dylinker", dylinker, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-dynamiclib", dynamiclib, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-dynamic", dynamic, Flag, INVALID, INVALID, nullptr,
       NoArgumentUnused, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-D", D, JoinedOrSeparate, Preprocessor_Group, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Define <macro> to <value> (or 1 if <value> omitted)", "<macro>=<value>",
       nullptr)
OPTION(prefix_1, "-d", d_Flag, Flag, d_Group, INVALID, nullptr, 0, DefaultVis,
       0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-d", d_Joined, Joined, d_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-ehcontguard", ehcontguard, Flag, INVALID, INVALID, nullptr,
       HelpHidden, DefaultVis, 0, "Emit Windows EH Continuation Guard tables",
       nullptr, nullptr)
OPTION(prefix_1, "-emit-llvm-bc", emit_llvm_bc, Flag, Action_Group, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Build ASTs then convert to LLVM, emit .bc file", nullptr, nullptr)
OPTION(prefix_1, "-emit-llvm-uselists", emit_llvm_uselists, Flag, INVALID,
       INVALID, nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
       "Preserve order of LLVM use-lists when serializing", nullptr, nullptr)
OPTION(prefix_1, "-emit-llvm", emit_llvm, Flag, Action_Group, INVALID, nullptr,
       0, DefaultVis, 0,
       "Use the LLVM representation for assembler and object files", nullptr,
       nullptr)
OPTION(prefix_1, "-emit-obj", emit_obj, Flag, Action_Group, INVALID, nullptr,
       HelpHidden, DefaultVis, 0, "Emit native object files", nullptr, nullptr)
OPTION(prefix_3, "--emit-static-lib", emit_static_lib, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Enable linker job to emit a static library.",
       nullptr, nullptr)
OPTION(prefix_1, "-enable-noundef-analysis", enable_noundef_analysis, Flag,
       INVALID, INVALID, nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
       "Enable analyzing function argument and return types for mandatory "
       "definedness",
       nullptr, nullptr)
OPTION(prefix_1, "-enable-trivial-auto-var-init-zero",
       enable_trivial_var_init_zero, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Trivial automatic variable initialization to zero is only here for "
       "benchmarks, it'll eventually be removed, and I'm OK with that because "
       "I'm only using it to benchmark",
       nullptr, nullptr)
OPTION(prefix_3, "--end-no-unused-arguments", end_no_unused_arguments, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Start emitting warnings for unused driver arguments", nullptr, nullptr)
OPTION(prefix_1, "-Eonly", Eonly, Flag, Action_Group, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "Just run preprocessor, no output (for timings)", nullptr, nullptr)
OPTION(prefix_1, "-exception-model=", exception_model_EQ, Joined, INVALID,
       exception_model, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-exception-model", exception_model, Separate, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, "The exception model", nullptr,
       "dwarf,seh")
OPTION(prefix_1, "-exported_symbols_list", exported__symbols__list, Separate,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(
    prefix_2, "-external-env=", external_env, Joined, msvc_Group, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Add dirs in env var <var> to include search path with warnings suppressed",
    "<var>", nullptr)
OPTION(prefix_1, "-E", E, Flag, Action_Group, INVALID, nullptr, NoXarchOption,
       DefaultVis, 0, "Only run the preprocessor", nullptr, nullptr)
OPTION(prefix_1, "-e", e, Separate, Link_Group, INVALID, nullptr, LinkerInput,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-faddrsig", faddrsig, Flag, f_Group, INVALID, nullptr,
       HelpHidden | HelpHidden, DefaultVis | DefaultVis, 0,
       "Emit an address-significance table", nullptr, nullptr)
OPTION(prefix_1, "-falign-functions=", falign_functions_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-falign-functions", falign_functions, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-falign-loops=", falign_loops_EQ, Joined, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0,
       "N must be a power of two. Align loops to the boundary", "<N>", nullptr)
OPTION(prefix_1, "-fandroid-kernel-driver-mode", fandroid_kernel_driver_mode,
       Flag, f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Android kernel driver development mode", nullptr, nullptr)
OPTION(prefix_1, "-fansi-escape-codes", fansi_escape_codes, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Use ANSI escape codes for diagnostics", nullptr, nullptr)
OPTION(prefix_1, "-fapple-link-rtlib", fapple_link_rtlib, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Force linking the compiler builtins runtime library", nullptr, nullptr)
OPTION(prefix_1, "-fapply-global-visibility-to-externs",
       fapply_global_visibility_to_externs, Flag, INVALID, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "Apply global symbol visibility to external declarations without an "
       "explicit visibility",
       nullptr, nullptr)
OPTION(prefix_1, "-fapprox-func", fapprox_func, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis | DefaultVis, 0,
       "Allow certain math function calls to be replaced with an approximately "
       "equivalent calculation",
       nullptr, nullptr)
OPTION(prefix_1, "-fasm-blocks", fasm_blocks, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis | DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fasm", fasm, Flag, f_Group, INVALID, nullptr, 0, DefaultVis,
       0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fassociative-math", fassociative_math, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fasync-exceptions", fasync_exceptions, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Enable EH Asynchronous exceptions", nullptr, nullptr)
OPTION(prefix_1, "-fasynchronous-unwind-tables", fasynchronous_unwind_tables,
       Flag, f_Group, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-fauto-generate-bitcode", fauto_generate_bitcode, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Automatically generate bitcode", nullptr, nullptr)
OPTION(prefix_1, "-fauto-generate-ir", fauto_generate_ir, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "Automatically generate ir", nullptr,
       nullptr)
OPTION(prefix_1, "-fauto-import", fauto_import, Flag, f_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis, 0,
       "Enable code generation support for automatic dllimport (default)",
       nullptr, nullptr)
OPTION(prefix_1, "-fautolink", fautolink, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fbasic-block-sections=", fbasic_block_sections_EQ, Joined,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Place each function's basic blocks in unique sections (ELF Only)",
       nullptr, "all,labels,none,list=")
OPTION(prefix_1, "-fbfloat16-excess-precision=", fbfloat16_excess_precision_EQ,
       Joined, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Allows control over excess precision on targets where native support "
       "for BFloat16 precision types is not available. By default, excess "
       "precision is used to calculate intermediate results following the "
       "rules specified in ISO C99.",
       nullptr, "standard,fast,none")
OPTION(prefix_1, "-fbinutils-version=", fbinutils_version_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Produced object files can use all ELF features supported by this "
       "binutils version and newer. 'none' means that all ELF features can be "
       "used, regardless of binutils support. Defaults to 2.26.",
       "<major.minor>", nullptr)
OPTION(prefix_1, "-fbracket-depth=", fbracket_depth_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fbracket-depth", fbracket_depth, Separate, INVALID, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Maximum nesting level for parentheses, brackets, and braces", nullptr,
       nullptr)
OPTION(prefix_1, "-fbuild-id=", fbuild_id_EQ, Joined, f_Group, INVALID, nullptr,
       LinkOption, DefaultVis, 0, "Set build ID style in the linked output",
       nullptr, "none,fast,md5,sha1,uuid")
OPTION(prefix_1, "-fbuiltin-mimalloc", fbuiltin_mimalloc, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Enable mimalloc allocator override injection via IR bitcode embedding",
       nullptr, nullptr)
OPTION(prefix_1, "-fbuiltin-string", fbuiltin_string, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "Enable NeverC builtin string runtime prelude (value-typed `string`, "
       "NEVERC_STRING_* macros, s.format()/s.length()/s.find()/... inline "
       "helpers)",
       nullptr, nullptr)
OPTION(prefix_1, "-fbuiltin", fbuiltin, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fcall-graph-ordering-file=", fcall_graph_ordering_file_EQ,
       Joined, f_Group, INVALID, nullptr, LinkOption, DefaultVis, 0,
       "Use call graph from <file> for section reordering", "<file>", nullptr)
OPTION(prefix_1, "-fcall-graph-profile-sort=", fcall_graph_profile_sort_EQ,
       Joined, f_Group, INVALID, nullptr, LinkOption, DefaultVis, 0,
       "Set call-graph profile-guided section reordering algorithm", nullptr,
       "none,hfsort,cdsort")
OPTION(prefix_1, "-fcall-saved-x10", fcall_saved_x10, Flag,
       m_aarch64_Features_Group, INVALID, nullptr, TargetSpecific, DefaultVis,
       0, "Make the x10 register call-saved (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-fcall-saved-x11", fcall_saved_x11, Flag,
       m_aarch64_Features_Group, INVALID, nullptr, TargetSpecific, DefaultVis,
       0, "Make the x11 register call-saved (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-fcall-saved-x12", fcall_saved_x12, Flag,
       m_aarch64_Features_Group, INVALID, nullptr, TargetSpecific, DefaultVis,
       0, "Make the x12 register call-saved (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-fcall-saved-x13", fcall_saved_x13, Flag,
       m_aarch64_Features_Group, INVALID, nullptr, TargetSpecific, DefaultVis,
       0, "Make the x13 register call-saved (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-fcall-saved-x14", fcall_saved_x14, Flag,
       m_aarch64_Features_Group, INVALID, nullptr, TargetSpecific, DefaultVis,
       0, "Make the x14 register call-saved (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-fcall-saved-x15", fcall_saved_x15, Flag,
       m_aarch64_Features_Group, INVALID, nullptr, TargetSpecific, DefaultVis,
       0, "Make the x15 register call-saved (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-fcall-saved-x18", fcall_saved_x18, Flag,
       m_aarch64_Features_Group, INVALID, nullptr, TargetSpecific, DefaultVis,
       0, "Make the x18 register call-saved (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-fcall-saved-x8", fcall_saved_x8, Flag,
       m_aarch64_Features_Group, INVALID, nullptr, TargetSpecific, DefaultVis,
       0, "Make the x8 register call-saved (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-fcall-saved-x9", fcall_saved_x9, Flag,
       m_aarch64_Features_Group, INVALID, nullptr, TargetSpecific, DefaultVis,
       0, "Make the x9 register call-saved (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1,
       "-fcaret-diagnostics-max-lines=", fcaret_diagnostics_max_lines_EQ,
       Joined, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Set the maximum number of source lines to show in a caret diagnostic "
       "(0 = no limit).",
       nullptr, nullptr)
OPTION(prefix_1, "-fcaret-diagnostics", fcaret_diagnostics, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fcf-protection=", fcf_protection_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Instrument control-flow architecture protection", nullptr,
       "return,branch,full,none")
OPTION(prefix_1, "-fcf-protection", fcf_protection, Flag, f_Group,
       fcf_protection_EQ, "full\0", 0, DefaultVis, 0,
       "Enable cf-protection in 'full' mode", nullptr, nullptr)
OPTION(prefix_1, "-fcolor-diagnostics", fcolor_diagnostics, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "Enable colors in diagnostics",
       nullptr, nullptr)
OPTION(prefix_1, "-fcommon", fcommon, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Place uninitialized global variables in a common block",
       nullptr, nullptr)
OPTION(prefix_1, "-fconst-strings", fconst_strings, Flag, INVALID, INVALID,
       nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
       "Use a const qualified type for string literals in C", nullptr, nullptr)
OPTION(prefix_1, "-fconstexpr-backtrace-limit=", fconstexpr_backtrace_limit_EQ,
       Joined, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Set the maximum number of entries to print in a constexpr evaluation "
       "backtrace (0 = no limit)",
       nullptr, nullptr)
OPTION(prefix_1, "-fconstexpr-depth=", fconstexpr_depth_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Set the maximum depth of recursive constexpr function calls", nullptr,
       nullptr)
OPTION(prefix_1, "-fconstexpr-steps=", fconstexpr_steps_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Set the maximum number of steps in constexpr function evaluation",
       nullptr, nullptr)
OPTION(prefix_1, "-fcrash-diagnostics-dir=", fcrash_diagnostics_dir, Joined,
       f_neverc_Group, INVALID, nullptr, NoArgumentUnused, DefaultVis, 0,
       "Put crash-report files in <dir>", "<dir>", nullptr)
OPTION(prefix_1, "-fcrash-diagnostics=", fcrash_diagnostics_EQ, Joined,
       f_neverc_Group, INVALID, nullptr, NoArgumentUnused, DefaultVis, 0,
       "Set level of crash diagnostic reporting, (option: off, compiler, all)",
       nullptr, nullptr)
OPTION(prefix_1, "-fcrash-diagnostics", fcrash_diagnostics, Flag,
       f_neverc_Group, fcrash_diagnostics_EQ, "compiler\0", NoArgumentUnused,
       DefaultVis, 0, "Enable crash diagnostic reporting (default)", nullptr,
       nullptr)
OPTION(prefix_1, "-fcx-fortran-rules", fcx_fortran_rules, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Range reduction is enabled for complex arithmetic operations.", nullptr,
       nullptr)
OPTION(prefix_1, "-fcx-limited-range", fcx_limited_range, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Basic algebraic expansions of complex arithmetic operations involving "
       "are enabled.",
       nullptr, nullptr)
OPTION(prefix_1, "-fdata-sections", fdata_sections, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "Place each data in its own section", nullptr, nullptr)
OPTION(prefix_1, "-fdebug-compilation-dir=", fdebug_compilation_dir_EQ, Joined,
       f_Group, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "The compilation directory to embed in the debug info", nullptr, nullptr)
OPTION(prefix_1, "-fdebug-compilation-dir", fdebug_compilation_dir, Separate,
       f_Group, fdebug_compilation_dir_EQ, nullptr, 0, DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-fdebug-default-version=", fdebug_default_version, Joined,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Default DWARF version to use, if a -g option caused DWARF debug info "
       "to be produced",
       nullptr, nullptr)
OPTION(prefix_1, "-fdebug-pass-manager", fdebug_pass_manager, Flag, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Prints debug information for the new pass manager", nullptr, nullptr)
OPTION(prefix_1, "-fdebug-prefix-map=", fdebug_prefix_map_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "For paths in debug info, remap directory <old> to <new>. If multiple "
       "options match a path, the last option wins",
       "<old>=<new>", nullptr)
OPTION(prefix_1, "-fdebug-ranges-base-address", fdebug_ranges_base_address,
       Flag, f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Use DWARF base address selection entries in .debug_ranges", nullptr,
       nullptr)
OPTION(prefix_1, "-fdebug-types-section", fdebug_types_section, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Place debug types in their own section (ELF Only)", nullptr, nullptr)
OPTION(prefix_1, "-fdeclspec", fdeclspec, Flag, f_neverc_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0, "Allow __declspec as a keyword",
       nullptr, nullptr)
OPTION(prefix_1, "-fdefault-calling-conv=", fdefault_calling_conv_EQ, Joined,
       INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Set default calling convention", nullptr,
       "cdecl,fastcall,stdcall,vectorcall,regcall")
OPTION(prefix_1, "-fdefine-target-os-macros", fdefine_target_os_macros, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Enable predefined target OS macros", nullptr, nullptr)
OPTION(prefix_1, "-fdelete-null-pointer-checks", fdelete_null_pointer_checks,
       Flag, f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Treat usage of null pointers as undefined behavior (default)", nullptr,
       nullptr)
OPTION(prefix_1, "-fdenormal-fp-math-f32=", fdenormal_fp_math_f32_EQ, Joined,
       f_Group, INVALID, nullptr, HelpHidden, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-fdenormal-fp-math=", fdenormal_fp_math_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fdepfile-entry=", fdepfile_entry, Joined, f_neverc_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fdeprecated-macro", fdeprecated_macro, Flag, INVALID,
       INVALID, nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
       "Defines the __DEPRECATED macro", nullptr, nullptr)
OPTION(prefix_1, "-fdiagnostics-absolute-paths", fdiagnostics_absolute_paths,
       Flag, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Print absolute paths in diagnostics", nullptr, nullptr)
OPTION(prefix_1, "-fdiagnostics-color=", fdiagnostics_color_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fdiagnostics-color", anonymous_69, Flag, f_Group,
       fcolor_diagnostics, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fdiagnostics-fixit-info", fdiagnostics_fixit_info, Flag,
       f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-fdiagnostics-format=", fdiagnostics_format_EQ, Joined,
       f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-fdiagnostics-format", fdiagnostics_format, Separate, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Change diagnostic formatting to match IDE and command line tools",
       nullptr, "neverc,msvc,vi")
OPTION(prefix_1,
       "-fdiagnostics-hotness-threshold=", fdiagnostics_hotness_threshold_EQ,
       Joined, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Prevent optimization remarks from being output if they do not have at "
       "least this profile count. Use 'auto' to apply the threshold from "
       "profile summary",
       "<value>", nullptr)
OPTION(prefix_1, "-fdiagnostics-parseable-fixits",
       fdiagnostics_parseable_fixits, Flag, f_neverc_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Print fix-its in machine parseable form", nullptr,
       nullptr)
OPTION(prefix_1, "-fdiagnostics-print-source-range-info",
       fdiagnostics_print_source_range_info, Flag, f_neverc_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Print source range spans in numeric form",
       nullptr, nullptr)
OPTION(prefix_1, "-fdiagnostics-show-category=", fdiagnostics_show_category_EQ,
       Joined, f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-fdiagnostics-show-category", fdiagnostics_show_category,
       Separate, INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Print diagnostic category", nullptr, "none,id,name")
OPTION(prefix_1, "-fdiagnostics-show-hotness", fdiagnostics_show_hotness, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Enable profile hotness information in diagnostic line", nullptr,
       nullptr)
OPTION(prefix_1, "-fdiagnostics-show-line-numbers",
       fdiagnostics_show_line_numbers, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fdiagnostics-show-note-include-stack",
       fdiagnostics_show_note_include_stack, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0,
       "Display include stacks for diagnostic notes", nullptr, nullptr)
OPTION(prefix_1, "-fdiagnostics-show-option", fdiagnostics_show_option, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Print option name with mappable diagnostics", nullptr, nullptr)
OPTION(prefix_1, "-fdigraphs", fdigraphs, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0,
       "Enable alternative token representations '<:', ':>', '<%', '%>', '%:', "
       "'%:%:' (default)",
       nullptr, nullptr)
OPTION(prefix_1, "-fdirect-access-external-data", fdirect_access_external_data,
       Flag, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Don't use GOT indirection to reference external data symbols", nullptr,
       nullptr)
OPTION(prefix_1, "-fdirectives-only", fdirectives_only, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fdisable-cfi-check", fdisable_cfi_check, Flag,
       f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Disables the checks in CFI", nullptr, nullptr)
OPTION(prefix_1, "-fdisable-inline-opt", fdisable_inline_opt, Flag,
       f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Disables the optimization of inline functions", nullptr, nullptr)
OPTION(prefix_1, "-fdisable-try-stmt", fdisable_try_stmt, Flag, f_neverc_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "Disables the try statements",
       nullptr, nullptr)
OPTION(prefix_1, "-fdiscard-value-names", fdiscard_value_names, Flag,
       f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Discard value names in LLVM IR", nullptr, nullptr)
OPTION(prefix_1, "-fdollars-in-identifiers", fdollars_in_identifiers, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Allow '$' in identifiers", nullptr, nullptr)
OPTION(prefix_1, "-fdriver-only", fdriver_only, Flag, Action_Group, INVALID,
       nullptr, NoXarchOption, DefaultVis, 0, "Only run the driver.", nullptr,
       nullptr)
OPTION(prefix_1, "-fdwarf-directory-asm", fdwarf_directory_asm, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-feliminate-unused-debug-symbols",
       feliminate_unused_debug_symbols, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-feliminate-unused-debug-types",
       feliminate_unused_debug_types, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Do not emit  debug info for defined but unused types",
       nullptr, nullptr)
OPTION(prefix_1, "-femit-all-decls", femit_all_decls, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Emit all declarations, even if unused",
       nullptr, nullptr)
OPTION(prefix_1, "-femit-compact-unwind-non-canonical",
       femit_compact_unwind_non_canonical, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0,
       "Try emitting Compact-Unwind for non-canonical entries. Maybe overriden "
       "by other constraints",
       nullptr, nullptr)
OPTION(prefix_1, "-femit-dwarf-unwind=", femit_dwarf_unwind_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "When to emit DWARF unwind (EH frame) info", nullptr,
       "always,no-compact-unwind,default")
OPTION(prefix_1, "-femulated-tls", femulated_tls, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "Use emutls functions to access thread_local variables", nullptr,
       nullptr)
OPTION(prefix_1, "-ferror-limit=", ferror_limit_EQ, Joined, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(
    prefix_1, "-ferror-limit", ferror_limit, Separate, INVALID, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Set the maximum number of errors to emit before stopping (0 = no limit).",
    "<N>", nullptr)
OPTION(prefix_1, "-fexceptions", fexceptions, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis | DefaultVis, 0, "Enable support for exception handling",
       nullptr, nullptr)
OPTION(prefix_1, "-fexcess-precision=", fexcess_precision_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Allows control over excess precision on targets where native support "
       "for the precision types is not available. By default, excess precision "
       "is used to calculate intermediate results following the rules "
       "specified in ISO C99.",
       nullptr, "standard,fast,none")
OPTION(prefix_1, "-fexec-charset=", fexec_charset_EQ, Joined, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fexperimental-assignment-tracking=",
       fexperimental_assignment_tracking_EQ, Joined, f_Group, INVALID, nullptr,
       HelpHidden, DefaultVis, 0, nullptr, nullptr, "disabled,enabled,forced")
OPTION(prefix_1, "-fexperimental-isel", fexperimental_isel, Flag,
       f_neverc_Group, fglobal_isel, nullptr, 0, DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1,
       "-fexperimental-max-bitint-width=", fexperimental_max_bitint_width_EQ,
       Joined, f_Group, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Set the maximum bitwidth for _BitInt (this option is expected to be "
       "removed in the future)",
       "<N>", nullptr)
OPTION(prefix_1, "-fexperimental-strict-floating-point",
       fexperimental_strict_floating_point, Flag, f_neverc_Group, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Enables the use of non-default rounding modes and non-default "
       "exception handling on targets that are not currently ready.",
       nullptr, nullptr)
OPTION(prefix_1, "-fextend-arguments=", fextend_args_EQ, Joined, f_Group,
       INVALID, nullptr, NoArgumentUnused, DefaultVis, 0,
       "Controls how scalar integer arguments are extended in calls to "
       "unprototyped and varargs functions",
       nullptr, "32,64")
OPTION(prefix_1, "-ffast-math", ffast_math, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0,
       "Allow aggressive, lossy floating-point optimizations", nullptr, nullptr)
OPTION(prefix_1, "-ffile-compilation-dir=", ffile_compilation_dir_EQ, Joined,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "The compilation directory to embed in the debug info", nullptr, nullptr)
OPTION(prefix_1, "-ffile-prefix-map=", ffile_prefix_map_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "remap file source paths in debug info, predefined preprocessor macros "
       "and __builtin_FILE(). Implies -ffile-reproducible.",
       nullptr, nullptr)
OPTION(prefix_1, "-ffile-reproducible", ffile_reproducible, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Use the target's platform-specific path separator character when "
       "expanding the __FILE__ macro",
       nullptr, nullptr)
OPTION(prefix_1, "-ffine-grained-bitfield-accesses",
       ffine_grained_bitfield_accesses, Flag, f_neverc_Group, INVALID, nullptr,
       0, DefaultVis | DefaultVis, 0,
       "Use separate accesses for consecutive bitfield runs with legal widths "
       "and alignments.",
       nullptr, nullptr)
OPTION(prefix_1, "-ffinite-loops", ffinite_loops, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Assume all loops are finite.", nullptr,
       nullptr)
OPTION(prefix_1, "-ffinite-math-only", ffinite_math_only, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Allow floating-point optimizations that assume arguments and results "
       "are not NaNs or +-inf. This defines the "
       "\\_\\_FINITE\\_MATH\\_ONLY\\_\\_ preprocessor macro.",
       nullptr, nullptr)
OPTION(prefix_1, "-ffixed-point", ffixed_point, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis | DefaultVis, 0, "Enable fixed point types", nullptr,
       nullptr)
OPTION(prefix_1, "-ffixed-x10", ffixed_x10, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x10 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x11", ffixed_x11, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x11 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x12", ffixed_x12, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x12 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x13", ffixed_x13, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x13 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x14", ffixed_x14, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x14 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x15", ffixed_x15, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x15 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x16", ffixed_x16, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x16 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x17", ffixed_x17, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x17 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x18", ffixed_x18, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x18 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x19", ffixed_x19, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x19 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x1", ffixed_x1, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x1 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x20", ffixed_x20, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x20 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x21", ffixed_x21, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x21 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x22", ffixed_x22, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x22 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x23", ffixed_x23, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x23 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x24", ffixed_x24, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x24 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x25", ffixed_x25, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x25 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x26", ffixed_x26, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x26 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x27", ffixed_x27, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x27 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x28", ffixed_x28, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x28 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x29", ffixed_x29, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x29 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x2", ffixed_x2, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x2 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x30", ffixed_x30, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x30 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x31", ffixed_x31, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x31 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x3", ffixed_x3, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x3 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x4", ffixed_x4, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x4 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x5", ffixed_x5, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x5 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x6", ffixed_x6, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x6 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x7", ffixed_x7, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x7 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x8", ffixed_x8, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x8 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffixed-x9", ffixed_x9, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Reserve the x9 register (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-ffloat16-excess-precision=", ffloat16_excess_precision_EQ,
       Joined, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Allows control over excess precision on targets where native support "
       "for Float16 precision types is not available. By default, excess "
       "precision is used to calculate intermediate results following the "
       "rules specified in ISO C99.",
       nullptr, "standard,fast,none")
OPTION(prefix_1, "-fforce-dwarf-frame", fforce_dwarf_frame, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Always emit a debug frame section", nullptr, nullptr)
OPTION(prefix_1, "-fforce-enable-int128", fforce_enable_int128, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Enable support for int128_t type", nullptr, nullptr)
OPTION(prefix_1, "-ffp-contract=", ffp_contract, Joined, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Form fused FP ops (e.g. FMAs)", nullptr,
       "fast,on,off,fast-honor-pragmas")
OPTION(prefix_1, "-ffp-eval-method=", ffp_eval_method_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Specifies the evaluation method to use for floating-point arithmetic.",
       nullptr, "source,double,extended")
OPTION(prefix_1, "-ffp-exception-behavior=", ffp_exception_behavior_EQ, Joined,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Specifies the exception behavior of floating-point operations.",
       nullptr, "ignore,maytrap,strict")
OPTION(prefix_1, "-ffp-model=", ffp_model_EQ, Joined, f_Group, INVALID, nullptr,
       0, DefaultVis, 0,
       "Controls the semantics of floating-point calculations.", nullptr,
       nullptr)
OPTION(prefix_1, "-ffreestanding", ffreestanding, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Assert that the compilation takes place in a freestanding environment",
       nullptr, nullptr)
OPTION(prefix_1, "-ffunction-sections", ffunction_sections, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Place each function in its own section", nullptr, nullptr)
OPTION(prefix_1, "-fgc-sections", fgc_sections, Flag, f_Group, INVALID, nullptr,
       LinkOption, DefaultVis, 0,
       "Enable linker garbage collection of unused sections (default at -O1+)",
       nullptr, nullptr)
OPTION(prefix_1, "-fglobal-isel", fglobal_isel, Flag, f_neverc_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Enables the global instruction selector",
       nullptr, nullptr)
OPTION(prefix_1, "-fgnu-inline-asm", fgnu_inline_asm, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fgnu-keywords", fgnu_keywords, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "Allow GNU-extension keywords regardless of language standard", nullptr,
       nullptr)
OPTION(prefix_1, "-fgnu89-inline", fgnu89_inline, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0, "Use the gnu89 inline semantics",
       nullptr, nullptr)
OPTION(prefix_1, "-fgnuc-version=", fgnuc_version_EQ, Joined, f_Group, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Sets various macros to claim compatibility with the given GCC version "
       "(default is 4.2.1)",
       nullptr, nullptr)
OPTION(prefix_1, "-fhalf-no-semantic-interposition",
       fhalf_no_semantic_interposition, Flag, INVALID, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "Like -fno-semantic-interposition but don't use local aliases", nullptr,
       nullptr)
OPTION(prefix_1, "-fhonor-infinites", anonymous_248, Flag, INVALID,
       fhonor_infinities, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fhonor-infinities", fhonor_infinities, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Specify that floating-point optimizations are not allowed that assume "
       "arguments and results are not +-inf.",
       nullptr, nullptr)
OPTION(prefix_1, "-fhonor-nans", fhonor_nans, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis, 0,
       "Specify that floating-point optimizations are not allowed that assume "
       "arguments and results are not NANs.",
       nullptr, nullptr)
OPTION(prefix_1, "-fhosted", fhosted, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-ficf=", ficf_EQ, Joined, f_Group, INVALID, nullptr,
       LinkOption, DefaultVis, 0,
       "Set identical code folding level for the linker", nullptr,
       "none,safe,all")
OPTION(prefix_1, "-fident", anonymous_18, Flag, f_Group, Qy, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fignore-exceptions", fignore_exceptions, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Disable all exception mechanisms including SEH and async EH", nullptr,
       nullptr)
OPTION(prefix_1, "-filelist", filelist, Separate, Link_Group, INVALID, nullptr,
       LinkerInput, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-filetype", filetype, Separate, INVALID, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "Specify the output file type ('asm', 'null', or 'obj')", nullptr,
       nullptr)
OPTION(prefix_1, "-finline-functions", finline_functions, Flag, f_neverc_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "Inline suitable functions", nullptr,
       nullptr)
OPTION(prefix_1, "-finline-hint-functions", finline_hint_functions, Flag,
       f_neverc_Group, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Inline functions which are (explicitly or implicitly) marked inline",
       nullptr, nullptr)
OPTION(
    prefix_1, "-finline-max-stacksize=", finline_max_stacksize_EQ, Joined,
    f_Group, INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "Suppress inlining of functions whose stack size exceeds the given value",
    nullptr, nullptr)
OPTION(prefix_1, "-finput-charset=", finput_charset_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Specify the default character set for source files", nullptr, nullptr)
OPTION(prefix_1, "-fintegrated-as", fintegrated_as, Flag, f_Group, INVALID,
       nullptr, HelpHidden, DefaultVis | DefaultVis, 0,
       "Enable the integrated assembler", nullptr, nullptr)
OPTION(prefix_1, "-fintegrated-objemitter", fintegrated_objemitter, Flag,
       f_Group, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Use internal machine object code emitter.", nullptr, nullptr)
OPTION(prefix_1, "-fjmc", fjmc, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0, "Enable just-my-code debugging", nullptr,
       nullptr)
OPTION(prefix_1, "-fjump-tables", fjump_tables, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis | DefaultVis, 0, "Use jump tables for lowering switches",
       nullptr, nullptr)
OPTION(prefix_1, "-fjumptable-rdata", fjumptable_rdata, Flag, f_neverc_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Put switch case jump tables in .rdata", nullptr, nullptr)
OPTION(prefix_1, "-fkeep-persistent-storage-variables",
       fkeep_persistent_storage_variables, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Enable keeping all variables that have a persistent storage duration, "
       "including global, static and thread-local variables, to guarantee that "
       "they can be directly addressed",
       nullptr, nullptr)
OPTION(prefix_1, "-fkeep-static-consts", fkeep_static_consts, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Keep static const variables even if unused", nullptr, nullptr)
OPTION(prefix_1, "-fkeep-system-includes", fkeep_system_includes, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Instead of expanding system headers when emitting preprocessor output, "
       "preserve the #include directive. Useful when producing preprocessed "
       "output for test case reduction. May produce incorrect output if "
       "preprocessor symbols that control the included content (e.g. "
       "_XOPEN_SOURCE) are defined in the including source file. The "
       "portability of the resulting source to other compilation environments "
       "is not guaranteed.\n\nOnly valid with -E.",
       nullptr, nullptr)
OPTION(prefix_1, "-flat_namespace", flat__namespace, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-flax-vector-conversions=", flax_vector_conversions_EQ,
       Joined, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Enable implicit vector bit-casts", nullptr, "none,integer,all")
OPTION(prefix_1, "-flax-vector-conversions", flax_vector_conversions, Flag,
       f_Group, flax_vector_conversions_EQ, "integer\0", 0, DefaultVis, 0,
       nullptr, nullptr, nullptr)
OPTION(prefix_1, "-flimit-debug-info", flimit_debug_info, Flag, INVALID,
       fno_standalone_debug, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-flinker-map=", flinker_map_EQ, Joined, f_Group, INVALID,
       nullptr, LinkOption, DefaultVis, 0,
       "Write a link map to the specified file (use '-' for stdout)", "<file>",
       nullptr)
OPTION(prefix_1, "-flto=auto", flto_EQ_auto, Flag, f_Group, flto_EQ, "full\0",
       0, DefaultVis, 0, "Enable LTO in 'full' mode", nullptr, nullptr)
OPTION(prefix_1, "-flto=jobserver", flto_EQ_jobserver, Flag, f_Group, flto_EQ,
       "full\0", 0, DefaultVis, 0, "Enable LTO in 'full' mode", nullptr,
       nullptr)
OPTION(prefix_1, "-flto=", flto_EQ, Joined, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Set LTO mode", nullptr, "full")
OPTION(prefix_1, "-flto", flto, Flag, f_Group, flto_EQ, "full\0", 0, DefaultVis,
       0, "Enable LTO in 'full' mode", nullptr, nullptr)
OPTION(prefix_1, "-fmacro-backtrace-limit=", fmacro_backtrace_limit_EQ, Joined,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Set the maximum number of entries to print in a macro expansion "
       "backtrace (0 = no limit)",
       nullptr, nullptr)
OPTION(prefix_1, "-fmacro-prefix-map=", fmacro_prefix_map_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "remap file source paths in predefined preprocessor macros and "
       "__builtin_FILE(). Implies -ffile-reproducible.",
       nullptr, nullptr)
OPTION(prefix_1, "-fmath-errno", fmath_errno, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis | DefaultVis, 0,
       "Require math functions to indicate errors by setting errno", nullptr,
       nullptr)
OPTION(prefix_1, "-fmax-tokens=", fmax_tokens_EQ, Joined, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Max total number of preprocessed tokens for -Wmax-tokens.", nullptr,
       nullptr)
OPTION(prefix_1, "-fmax-type-align=", fmax_type_align_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Specify the maximum alignment to enforce on pointers lacking an "
       "explicit alignment",
       nullptr, nullptr)
OPTION(prefix_1, "-fmerge-all-constants", fmerge_all_constants, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Allow merging of constants", nullptr, nullptr)
OPTION(prefix_1, "-fmessage-length=", fmessage_length_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Format message diagnostics so that they fit within N columns", nullptr,
       nullptr)
OPTION(prefix_1, "-fminimize-whitespace", fminimize_whitespace, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Ignore the whitespace from the input file when emitting preprocessor "
       "output. It will only contain whitespace when necessary, e.g. to keep "
       "two minus signs from merging into to an increment operator. Useful "
       "with the -P option to normalize whitespace such that two files with "
       "only formatting changes are equal.\n\nOnly valid with -E on C-like "
       "inputs and incompatible with -traditional-cpp.",
       nullptr, nullptr)
OPTION(prefix_1, "-fms-buffer-security-check", fms_buffer_security_check, Flag,
       msvc_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Enable buffer security check (default for MSVC targets)", nullptr,
       nullptr)
OPTION(prefix_1, "-fms-compatibility-version=", fms_compatibility_version,
       Joined, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Dot-separated value representing the Microsoft compiler version number "
       "to report in _MSC_VER (0 = don't define it (default))",
       nullptr, nullptr)
OPTION(prefix_1, "-fms-compatibility", fms_compatibility, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Enable full Microsoft Visual C compatibility", nullptr, nullptr)
OPTION(prefix_1, "-fms-default-calling-conv=", fms_default_calling_conv, Joined,
       msvc_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Set default calling convention (cdecl, fastcall, stdcall, vectorcall, "
       "regcall)",
       nullptr, nullptr)
OPTION(prefix_1, "-fms-diagnostics=", fms_diagnostics, Joined, msvc_Group,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Set MSVC diagnostic style (caret, column, classic)", nullptr, nullptr)
OPTION(prefix_1, "-fms-exception-model=", fms_exception_model, Joined,
       msvc_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Set MSVC exception handling model", nullptr, nullptr)
OPTION(
    prefix_1, "-fms-extensions", fms_extensions, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Accept some non-standard constructs supported by the Microsoft compiler",
    nullptr, nullptr)
OPTION(prefix_1, "-fms-guard=", fms_guard, Joined, msvc_Group, INVALID, nullptr,
       0, DefaultVis, 0,
       "Enable Windows Control Flow Guard (cf, cf,nochecks, ehcont)", nullptr,
       nullptr)
OPTION(prefix_1, "-fms-hotpatch", fms_hotpatch, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis, 0,
       "Ensure that all functions can be hotpatched at runtime", nullptr,
       nullptr)
OPTION(prefix_1, "-fms-kernel", fms_kernel, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fms-omit-default-lib", fms_omit_default_lib, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fms-permissive", fms_permissive, Flag, msvc_Group, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Enable some non conforming code to compile", nullptr, nullptr)
OPTION(prefix_1, "-fms-runtime-lib=", fms_runtime_lib_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "Select Windows run-time library",
       nullptr, "static,static_dbg,dll,dll_dbg")
OPTION(prefix_1, "-fms-show-filenames", fms_show_filenames, Flag, msvc_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Print the name of each compiled file", nullptr, nullptr)
OPTION(prefix_1, "-fms-show-includes-user", fms_show_includes_user, Flag,
       msvc_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Like -fms-show-includes but omit system headers", nullptr, nullptr)
OPTION(prefix_1, "-fms-show-includes", fms_show_includes, Flag, msvc_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Print info about included files to stderr", nullptr, nullptr)
OPTION(prefix_1, "-fms-volatile", fms_volatile, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis | DefaultVis, 0,
       "Volatile loads and stores have acquire and release semantics", nullptr,
       nullptr)
OPTION(prefix_1, "-fmsc-version=", fmsc_version, Joined, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Microsoft compiler version number to report in _MSC_VER (0 = don't "
       "define it (default))",
       nullptr, nullptr)
OPTION(prefix_1, "-fnative-half-arguments-and-returns",
       fnative_half_arguments_and_returns, Flag, INVALID, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "Use the native __fp16 type for arguments and returns (and skip "
       "ABI-specific lowering)",
       nullptr, nullptr)
OPTION(prefix_1, "-fnative-half-type", fnative_half_type, Flag, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Use the native half type for __fp16 instead of promoting to float",
       nullptr, nullptr)
OPTION(prefix_1, "-fnested-functions", fnested_functions, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fneverc-types", fneverc_types, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "Enable NeverC Rust-style integer type keywords (u8/u16/u32/u64/u128, "
       "i8/i16/i32/i64/i128, isize/usize)",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-addrsig", fno_addrsig, Flag, f_Group, INVALID, nullptr,
       HelpHidden | HelpHidden, DefaultVis | DefaultVis, 0,
       "Don't emit an address-significance table", nullptr, nullptr)
OPTION(prefix_1, "-fno-align-functions", fno_align_functions, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-approx-func", fno_approx_func, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-asm-blocks", fno_asm_blocks, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-asm", fno_asm, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-associative-math", fno_associative_math, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-async-exceptions", fno_async_exceptions, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-asynchronous-unwind-tables",
       fno_asynchronous_unwind_tables, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-auto-import", fno_auto_import, Flag, f_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0,
       "Disable support for automatic dllimport in code generation and linking",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-autolink", fno_autolink, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis | DefaultVis, 0,
       "Disable generation of linker directives for automatic library linking",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-bitfield-type-align", fno_bitfield_type_align, Flag,
       INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Ignore bit-field types when aligning structures", nullptr, nullptr)
OPTION(prefix_1, "-fno-build-id", fno_build_id, Flag, f_Group, INVALID, nullptr,
       LinkOption, DefaultVis, 0, "Do not generate a build ID", nullptr,
       nullptr)
OPTION(prefix_1, "-fno-builtin-mimalloc", fno_builtin_mimalloc, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Disable mimalloc allocator override injection",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-builtin-string", fno_builtin_string, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Disable NeverC builtin string runtime prelude (value-typed `string`, "
       "NEVERC_STRING_* macros, s.format()/s.length()/s.find()/... inline "
       "helpers)",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-builtin-", fno_builtin_, Joined, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Disable implicit builtin knowledge of a specific function", nullptr,
       nullptr)
OPTION(prefix_1, "-fno-builtin", fno_builtin, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis, 0, "Disable implicit builtin knowledge of functions",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-call-graph-profile-sort", fno_call_graph_profile_sort,
       Flag, f_Group, INVALID, nullptr, LinkOption, DefaultVis, 0,
       "Disable call-graph profile-guided section reordering", nullptr, nullptr)
OPTION(prefix_1, "-fno-caret-diagnostics", fno_caret_diagnostics, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-color-diagnostics", fno_color_diagnostics, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "Disable colors in diagnostics",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-common", fno_common, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Compile common globals like normal definitions", nullptr,
       nullptr)
OPTION(prefix_1, "-fno-const-strings", fno_const_strings, Flag, INVALID,
       INVALID, nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
       "Don't use a const qualified type for string literals in C", nullptr,
       nullptr)
OPTION(prefix_1, "-fno-crash-diagnostics", fno_crash_diagnostics, Flag,
       f_neverc_Group, gen_reproducer_eq, "off\0", NoArgumentUnused, DefaultVis,
       0,
       "Disable auto-generation of preprocessed source files and a script for "
       "reproduction during a compiler crash",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-cx-fortran-rules", fno_cx_fortran_rules, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Range reduction is disabled for complex arithmetic operations.",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-cx-limited-range", fno_cx_limited_range, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Basic algebraic expansions of complex arithmetic operations involving "
       "are disabled.",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-data-sections", fno_data_sections, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-debug-pass-manager", fno_debug_pass_manager, Flag,
       INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Disables debug printing for the new pass manager", nullptr, nullptr)
OPTION(prefix_1, "-fno-debug-ranges-base-address",
       fno_debug_ranges_base_address, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-debug-types-section", fno_debug_types_section, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-declspec", fno_declspec, Flag, f_neverc_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "Disallow __declspec as a keyword", nullptr, nullptr)
OPTION(prefix_1, "-fno-define-target-os-macros", fno_define_target_os_macros,
       Flag, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Disable predefined target OS macros", nullptr, nullptr)
OPTION(prefix_1, "-fno-delete-null-pointer-checks",
       fno_delete_null_pointer_checks, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0,
       "Do not treat usage of null pointers as undefined behavior", nullptr,
       nullptr)
OPTION(prefix_1, "-fno-deprecated-macro", fno_deprecated_macro, Flag, INVALID,
       INVALID, nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
       "Undefines the __DEPRECATED macro", nullptr, nullptr)
OPTION(prefix_1, "-fno-diagnostics-color", anonymous_70, Flag, f_Group,
       fno_color_diagnostics, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-fno-diagnostics-fixit-info", fno_diagnostics_fixit_info,
       Flag, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not include fixit information in diagnostics", nullptr, nullptr)
OPTION(prefix_1, "-fno-diagnostics-show-hotness", fno_diagnostics_show_hotness,
       Flag, f_Group, INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-diagnostics-show-line-numbers",
       fno_diagnostics_show_line_numbers, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0,
       "Show line numbers in diagnostic code snippets", nullptr, nullptr)
OPTION(prefix_1, "-fno-diagnostics-show-note-include-stack",
       fno_diagnostics_show_note_include_stack, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-diagnostics-show-option", fno_diagnostics_show_option,
       Flag, f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0, "",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-diagnostics-use-presumed-location",
       fno_diagnostics_use_presumed_location, Flag, INVALID, INVALID, nullptr,
       0, DefaultVis, 0,
       "Ignore #line directives when displaying diagnostic locations", nullptr,
       nullptr)
OPTION(prefix_1, "-fno-digraphs", fno_digraphs, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis | DefaultVis, 0,
       "Disallow alternative token representations '<:', ':>', '<%', '%>', "
       "'%:', '%:%:'",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-direct-access-external-data",
       fno_direct_access_external_data, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Use GOT indirection to reference external data symbols",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-directives-only", fno_directives_only, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-discard-value-names", fno_discard_value_names, Flag,
       f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not discard value names in LLVM IR", nullptr, nullptr)
OPTION(prefix_1, "-fno-dollars-in-identifiers", fno_dollars_in_identifiers,
       Flag, f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Disallow '$' in identifiers", nullptr, nullptr)
OPTION(prefix_1, "-fno-dwarf-directory-asm", fno_dwarf_directory_asm, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0, "", nullptr,
       nullptr)
OPTION(prefix_1, "-fno-eliminate-unused-debug-symbols",
       fno_eliminate_unused_debug_symbols, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-eliminate-unused-debug-types",
       fno_eliminate_unused_debug_types, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0,
       "Emit  debug info for defined but unused types", nullptr, nullptr)
OPTION(prefix_1, "-fno-emit-compact-unwind-non-canonical",
       fno_emit_compact_unwind_non_canonical, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-emulated-tls", fno_emulated_tls, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-exceptions", fno_exceptions, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "Disable support for exception handling", nullptr, nullptr)
OPTION(prefix_1, "-fno-experimental-isel", fno_experimental_isel, Flag,
       f_neverc_Group, fno_global_isel, nullptr, 0, DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-fno-fast-math", fno_fast_math, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-file-reproducible", fno_file_reproducible, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Use the host's platform-specific path separator character when "
       "expanding the __FILE__ macro",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-fine-grained-bitfield-accesses",
       fno_fine_grained_bitfield_accesses, Flag, f_neverc_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "Use large-integer access for consecutive bitfield runs.", nullptr,
       nullptr)
OPTION(prefix_1, "-fno-finite-loops", fno_finite_loops, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Do not assume that any loop is finite.",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-finite-math-only", fno_finite_math_only, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-fixed-point", fno_fixed_point, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0, "Disable fixed point types",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-force-dwarf-frame", fno_force_dwarf_frame, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-force-enable-int128", fno_force_enable_int128, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Disable support for int128_t type", nullptr, nullptr)
OPTION(prefix_1, "-fno-function-sections", fno_function_sections, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-gc-sections", fno_gc_sections, Flag, f_Group, INVALID,
       nullptr, LinkOption, DefaultVis, 0,
       "Disable linker garbage collection of unused sections", nullptr, nullptr)
OPTION(prefix_1, "-fno-global-isel", fno_global_isel, Flag, f_neverc_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Disables the global instruction selector", nullptr, nullptr)
OPTION(prefix_1, "-fno-gnu-inline-asm", fno_gnu_inline_asm, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Disable GNU style inline asm", nullptr, nullptr)
OPTION(prefix_1, "-fno-gnu-keywords", fno_gnu_keywords, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-gnu89-inline", fno_gnu89_inline, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-honor-infinites", anonymous_249, Flag, INVALID,
       fno_honor_infinities, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-fno-honor-infinities", fno_honor_infinities, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-honor-nans", fno_honor_nans, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-ident", anonymous_19, Flag, f_Group, Qn, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-ignore-exceptions", fno_ignore_exceptions, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-inline-functions", fno_inline_functions, Flag,
       f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-fno-inline", fno_inline, Flag, f_neverc_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-integrated-as", fno_integrated_as, Flag, f_Group,
       INVALID, nullptr, HelpHidden, DefaultVis | DefaultVis, 0,
       "Disable the integrated assembler", nullptr, nullptr)
OPTION(prefix_1, "-fno-integrated-objemitter", fno_integrated_objemitter, Flag,
       f_Group, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Use external machine object code emitter.", nullptr, nullptr)
OPTION(prefix_1, "-fno-jmc", fno_jmc, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-jump-tables", fno_jump_tables, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "Do not use jump tables for lowering switches", nullptr, nullptr)
OPTION(prefix_1, "-fno-keep-persistent-storage-variables",
       fno_keep_persistent_storage_variables, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis, 0,
       "Disable keeping all variables that have a persistent storage duration, "
       "including global, static and thread-local variables, to guarantee that "
       "they can be directly addressed",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-keep-static-consts", fno_keep_static_consts, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Don't keep static const variables even if unused", nullptr, nullptr)
OPTION(prefix_1, "-fno-keep-system-includes", fno_keep_system_includes, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-knr-functions", fno_knr_functions, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Disable support for K&R C function declarations", nullptr, nullptr)
OPTION(prefix_1, "-fno-lax-vector-conversions", fno_lax_vector_conversions,
       Flag, f_Group, flax_vector_conversions_EQ, "none\0", 0, DefaultVis, 0,
       nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-limit-debug-info", fno_limit_debug_info, Flag, INVALID,
       fstandalone_debug, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-lto", fno_lto, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Disable LTO mode (default)", nullptr, nullptr)
OPTION(prefix_1, "-fno-math-builtin", fno_math_builtin, Flag, INVALID, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Disable implicit builtin knowledge of math functions", nullptr, nullptr)
OPTION(prefix_1, "-fno-math-errno", fno_math_errno, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-max-type-align", fno_max_type_align, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-merge-all-constants", fno_merge_all_constants, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Disallow merging of constants", nullptr, nullptr)
OPTION(prefix_1, "-fno-minimize-whitespace", fno_minimize_whitespace, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-ms-buffer-security-check", fno_ms_buffer_security_check,
       Flag, msvc_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Disable buffer security check", nullptr, nullptr)
OPTION(prefix_1, "-fno-ms-compatibility", fno_ms_compatibility, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-ms-extensions", fno_ms_extensions, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-ms-permissive", fno_ms_permissive, Flag, msvc_Group,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Disable non conforming code from compiling (default)", nullptr, nullptr)
OPTION(prefix_1, "-fno-ms-show-filenames", fno_ms_show_filenames, Flag,
       msvc_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not print the name of each compiled file", nullptr, nullptr)
OPTION(prefix_1, "-fno-ms-volatile", fno_ms_volatile, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-neverc-types", fno_neverc_types, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "Disable NeverC Rust-style integer type keywords (u8/u16/u32/u64/u128, "
       "i8/i16/i32/i64/i128, isize/usize)",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-omit-frame-pointer", fno_omit_frame_pointer, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-optimize-sibling-calls", fno_optimize_sibling_calls,
       Flag, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Disable tail call optimization, keeping the call stack accurate",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-pack-struct", fno_pack_struct, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-padding-on-unsigned-fixed-point",
       fno_padding_on_unsigned_fixed_point, Flag, INVALID, INVALID, nullptr,
       HelpHidden, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-parallel-codegen", fno_parallel_codegen, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "Disable parallel code generation",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-PIC", fno_PIC, Flag, f_Group, INVALID, nullptr, Ignored,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-pic", fno_pic, Flag, f_Group, INVALID, nullptr, Ignored,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-PIE", fno_PIE, Flag, f_Group, INVALID, nullptr, Ignored,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-pie", fno_pie, Flag, f_Group, INVALID, nullptr, Ignored,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-plt", fno_plt, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0,
       "Use GOT indirection instead of PLT to make external function calls "
       "(x86 only)",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-preserve-as-comments", fno_preserve_as_comments, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Do not preserve comments in inline assembly", nullptr, nullptr)
OPTION(prefix_1, "-fno-print-gc-sections", fno_print_gc_sections, Flag, f_Group,
       INVALID, nullptr, LinkOption, DefaultVis, 0,
       "Do not list removed sections (default)", nullptr, nullptr)
OPTION(prefix_1, "-fno-print-icf-sections", fno_print_icf_sections, Flag,
       f_Group, INVALID, nullptr, LinkOption, DefaultVis, 0,
       "Do not list folded sections (default)", nullptr, nullptr)
OPTION(prefix_1, "-fno-protect-parens", fno_protect_parens, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-reciprocal-math", fno_reciprocal_math, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-record-command-line", fno_record_command_line, Flag,
       f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-fno-record-gcc-switches", anonymous_73, Flag, INVALID,
       fno_record_command_line, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-fno-recovery-ast-type", fno_recovery_ast_type, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-recovery-ast", fno_recovery_ast, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-rounding-math", fno_rounding_math, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-save-optimization-record", fno_save_optimization_record,
       Flag, f_Group, INVALID, nullptr, NoArgumentUnused, DefaultVis, 0,
       nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-semantic-interposition", fno_semantic_interposition,
       Flag, f_Group, INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-shellcode-bad-byte-rewrite",
       fno_shellcode_bad_byte_rewrite, Flag, f_Group, INVALID, nullptr,
       NoXarchOption, DefaultVis, 0,
       "Skip the bad-byte rewriter and fall back to audit-only behaviour: any "
       "byte present in -fshellcode-bad-bytes= / -fshellcode-bad-byte-profile= "
       "triggers a hard finalize-time error.",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-shellcode", fno_shellcode, Flag, f_Group, INVALID,
       nullptr, NoXarchOption, DefaultVis, 0,
       "Disable shellcode compilation (undoes a preceding -fshellcode)",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-short-enums", fno_short_enums, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-short-wchar", fno_short_wchar, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Force wchar_t to be an unsigned int",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-show-column", fno_show_column, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "Do not include column number on diagnostics", nullptr, nullptr)
OPTION(prefix_1, "-fno-show-source-location", fno_show_source_location, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Do not include source location information with diagnostics", nullptr,
       nullptr)
OPTION(prefix_1, "-fno-signed-char", fno_signed_char, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0, "char is unsigned", nullptr,
       nullptr)
OPTION(prefix_1, "-fno-signed-wchar", fno_signed_wchar, Flag, INVALID, INVALID,
       nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
       "Use an unsigned type for wchar_t", nullptr, nullptr)
OPTION(prefix_1, "-fno-signed-zeros", fno_signed_zeros, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "Allow optimizations that ignore the sign of floating point zeros",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-slp-vectorize", fno_slp_vectorize, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-spell-checking", fno_spell_checking, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Disable spell-checking", nullptr, nullptr)
OPTION(prefix_1, "-fno-split-dwarf-inlining", fno_split_dwarf_inlining, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0, "", nullptr,
       nullptr)
OPTION(prefix_1, "-fno-split-machine-functions", fno_split_machine_functions,
       Flag, f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Disable late function splitting using profile information (x86 ELF)",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-split-stack", fno_split_stack, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0, "Wouldn't use segmented stack",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-stack-clash-protection", fno_stack_clash_protection,
       Flag, f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Disable stack clash protection", nullptr, nullptr)
OPTION(prefix_1, "-fno-stack-protector", fno_stack_protector, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Disable the use of stack protectors", nullptr, nullptr)
OPTION(prefix_1, "-fno-stack-size-section", fno_stack_size_section, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-standalone-debug", fno_standalone_debug, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Limit debug information produced to reduce size of debug binary",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-strict-aliasing", fno_strict_aliasing, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Disable optimizations based on strict aliasing rules", nullptr, nullptr)
OPTION(prefix_1, "-fno-strict-float-cast-overflow",
       fno_strict_float_cast_overflow, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0,
       "Relax language rules and try to match the behavior of the target's "
       "native float-to-int conversion instructions",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-strict-overflow", fno_strict_overflow, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-struct-path-tbaa", fno_struct_path_tbaa, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-temp-file", fno_temp_file, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Directly create compilation output files. This may lead to incorrect "
       "incremental builds if the compiler crashes",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-trapping-math", fno_trapping_math, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-tree-slp-vectorize", anonymous_482, Flag, INVALID,
       fno_slp_vectorize, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-tree-vectorize", anonymous_480, Flag, INVALID,
       fno_vectorize, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-trigraphs", fno_trigraphs, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Do not process trigraph sequences", nullptr,
       nullptr)
OPTION(prefix_1, "-fno-unique-basic-block-section-names",
       fno_unique_basic_block_section_names, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-unique-internal-linkage-names",
       fno_unique_internal_linkage_names, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-unique-section-names", fno_unique_section_names, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Don't use unique names for text and data sections", nullptr, nullptr)
OPTION(prefix_1, "-fno-unroll-loops", fno_unroll_loops, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Turn off loop unroller", nullptr, nullptr)
OPTION(prefix_1, "-fno-unsafe-math-optimizations",
       fno_unsafe_math_optimizations, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-unsigned-char", fno_unsigned_char, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-unwind-tables", fno_unwind_tables, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-use-cxa-atexit", fno_use_cxa_atexit, Flag, f_Group,
       INVALID, nullptr, HelpHidden, DefaultVis | DefaultVis, 0,
       "Don't use __cxa_atexit for registering cleanup functions", nullptr,
       nullptr)
OPTION(prefix_1, "-fno-use-init-array", fno_use_init_array, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Use .ctors/.dtors instead of .init_array/.fini_array", nullptr, nullptr)
OPTION(prefix_1, "-fno-use-line-directives", fno_use_line_directives, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-vectorize", fno_vectorize, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-verbose-asm", fno_verbose_asm, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-verify-intermediate-code", fno_verify_intermediate_code,
       Flag, f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Disable verification of LLVM IR", nullptr, nullptr)
OPTION(prefix_1, "-fno-visibility-from-dllstorageclass",
       fno_visibility_from_dllstorageclass, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fno-wrapv", fno_wrapv, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fno-zero-initialized-in-bss", fno_zero_initialized_in_bss,
       Flag, f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Don't place zero initialized data in BSS", nullptr, nullptr)
OPTION(prefix_1, "-fomit-frame-pointer", fomit_frame_pointer, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Omit the frame pointer from functions that don't need it. Some stack "
       "unwinding cases, such as profilers and sanitizers, may prefer "
       "specifying -fno-omit-frame-pointer. On many targets, -O1 and higher "
       "omit the frame pointer by default. -m[no-]omit-leaf-frame-pointer "
       "takes precedence for leaf functions",
       nullptr, nullptr)
OPTION(prefix_1, "-foptimization-record-file=", foptimization_record_file_EQ,
       Joined, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Specify the output name of the file containing the optimization "
       "remarks. Implies -fsave-optimization-record. On Darwin platforms, this "
       "cannot be used with multiple -arch <arch> options.",
       "<file>", nullptr)
OPTION(prefix_1,
       "-foptimization-record-passes=", foptimization_record_passes_EQ, Joined,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Only include passes which match a specified regular expression in the "
       "generated optimization record (by default, include all passes)",
       "<regex>", nullptr)
OPTION(prefix_1, "-foptimize-sibling-calls", foptimize_sibling_calls, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-force_cpusubtype_ALL", force__cpusubtype__ALL, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-force_flat_namespace", force__flat__namespace, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-force_load", force__load, Separate, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fpack-struct=", fpack_struct_EQ, Joined, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Specify the default maximum struct packing alignment", nullptr, nullptr)
OPTION(prefix_1, "-fpack-struct", fpack_struct, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fpadding-on-unsigned-fixed-point",
       fpadding_on_unsigned_fixed_point, Flag, INVALID, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "Force each unsigned fixed point type to have an extra bit of padding "
       "to align their scales with those of signed fixed point types",
       nullptr, nullptr)
OPTION(prefix_1, "-fparallel-codegen=", fparallel_codegen_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Split module into N partitions for parallel code generation (0 = "
       "auto-detect, 1 = serial)",
       nullptr, nullptr)
OPTION(prefix_1, "-fpass-plugin=", fpass_plugin_EQ, Joined, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Load pass plugin from a dynamic shared object file (only with new pass "
       "manager).",
       "<dsopath>", nullptr)
OPTION(prefix_1, "-fpatchable-function-entry-offset=",
       fpatchable_function_entry_offset_EQ, Joined, INVALID, INVALID, nullptr,
       HelpHidden, DefaultVis, 0, "Generate M NOPs before function entry",
       "<M>", nullptr)
OPTION(
    prefix_1, "-fpatchable-function-entry=", fpatchable_function_entry_EQ,
    Joined, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Generate M NOPs before function entry and N-M NOPs after function entry",
    "<N,M>", nullptr)
OPTION(prefix_1, "-fPIC", fPIC, Flag, f_Group, INVALID, nullptr, Ignored,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fpic", fpic, Flag, f_Group, INVALID, nullptr, Ignored,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fPIE", fPIE, Flag, f_Group, INVALID, nullptr, Ignored,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fpie", fpie, Flag, f_Group, INVALID, nullptr, Ignored,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fplt", fplt, Flag, f_Group, INVALID, nullptr, 0, DefaultVis,
       0, "", nullptr, nullptr)
OPTION(prefix_1, "-fpreserve-as-comments", fpreserve_as_comments, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fprint-arguments", fprint_arguments, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Print compiler arguments", nullptr, nullptr)
OPTION(prefix_1, "-fprint-gc-sections", fprint_gc_sections, Flag, f_Group,
       INVALID, nullptr, LinkOption, DefaultVis, 0,
       "List sections removed by linker garbage collection", nullptr, nullptr)
OPTION(prefix_1, "-fprint-icf-sections", fprint_icf_sections, Flag, f_Group,
       INVALID, nullptr, LinkOption, DefaultVis, 0,
       "List sections folded by identical code folding", nullptr, nullptr)
OPTION(prefix_1, "-fprint-symbol-order=", fprint_symbol_order_EQ, Joined,
       f_Group, INVALID, nullptr, LinkOption, DefaultVis, 0,
       "Write symbol order from call-graph profile sort to <file>", "<file>",
       nullptr)
OPTION(prefix_1, "-fprotect-parens", fprotect_parens, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "Determines whether the optimizer honors parentheses when "
       "floating-point expressions are evaluated",
       nullptr, nullptr)
OPTION(prefix_1, "-framework", framework, Separate, INVALID, INVALID, nullptr,
       LinkerInput, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1,
       "-frandomize-layout-seed-file=", frandomize_layout_seed_file_EQ, Joined,
       f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "File holding the seed used by the randomize structure layout feature",
       "<file>", nullptr)
OPTION(prefix_1, "-frandomize-layout-seed=", frandomize_layout_seed_EQ, Joined,
       f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "The seed used by the randomize structure layout feature", "<seed>",
       nullptr)
OPTION(prefix_1, "-freciprocal-math", freciprocal_math, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "Allow division operations to be reassociated", nullptr, nullptr)
OPTION(prefix_1, "-frecord-command-line", frecord_command_line, Flag,
       f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-frecord-gcc-switches", anonymous_72, Flag, INVALID,
       frecord_command_line, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-frecovery-ast-type", frecovery_ast_type, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Preserve the type for recovery expressions when possible", nullptr,
       nullptr)
OPTION(prefix_1, "-frecovery-ast", frecovery_ast, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Preserve expressions in AST rather than dropping them when "
       "encountering semantic errors",
       nullptr, nullptr)
OPTION(prefix_1, "-frounding-math", frounding_math, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fsanitize-ignorelist=", fsanitize_ignorelist_EQ, Joined,
       f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Path to ignorelist file for sanitizers", nullptr, nullptr)
OPTION(prefix_1, "-fsanitize-recover=", fsanitize_recover_EQ, CommaJoined,
       f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Enable recovery for specified sanitizers", nullptr, nullptr)
OPTION(prefix_1,
       "-fsanitize-system-ignorelist=", fsanitize_system_ignorelist_EQ, Joined,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Path to system ignorelist file for sanitizers", nullptr, nullptr)
OPTION(prefix_1, "-fsanitize-trap=", fsanitize_trap_EQ, CommaJoined,
       f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Enable trapping for specified sanitizers", nullptr, nullptr)
OPTION(prefix_1, "-fsanitize=", fsanitize_EQ, CommaJoined, f_neverc_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Turn on runtime checks for various forms of undefined or suspicious "
       "behavior. See user manual for available checks",
       "<check>", nullptr)
OPTION(prefix_1, "-fsave-optimization-record=", fsave_optimization_record_EQ,
       Joined, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Generate an optimization record file in a specific format", "<format>",
       nullptr)
OPTION(prefix_1, "-fsave-optimization-record", fsave_optimization_record, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Generate a YAML optimization record file", nullptr, nullptr)
OPTION(prefix_1, "-fseh-exceptions", fseh_exceptions, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Use SEH style exceptions", nullptr, nullptr)
OPTION(prefix_1, "-fsemantic-interposition", fsemantic_interposition, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0, "", nullptr,
       nullptr)
OPTION(prefix_1, "-fshellcode-align=", fshellcode_align_EQ, Joined, f_Group,
       INVALID, nullptr, NoXarchOption, DefaultVis, 0,
       "Pad the final shellcode .bin to the next multiple of <bytes> using the "
       "byte selected by -fshellcode-pad= (defaults to 0x00). Must be a power "
       "of two; defaults to 1 (no alignment).",
       nullptr, nullptr)
OPTION(prefix_1, "-fshellcode-all-blr", fshellcode_all_blr, Flag, f_Group,
       INVALID, nullptr, NoXarchOption, DefaultVis, 0,
       "Aggressive shellcode mode: rewrite every direct call into an indirect "
       "branch so no pc-relative external branch (ARM64_RELOC_BRANCH26 / "
       "IMAGE_REL_AMD64_REL32) can ever survive",
       nullptr, nullptr)
OPTION(prefix_1,
       "-fshellcode-bad-byte-profile=", fshellcode_bad_byte_profile_EQ, Joined,
       f_Group, INVALID, nullptr, NoXarchOption, DefaultVis, 0,
       "Reject the final shellcode .bin using a built-in bad-byte profile: "
       "null, c-string, http-newline, line, whitespace, or ascii-control",
       nullptr, nullptr)
OPTION(prefix_1, "-fshellcode-bad-byte-rewrite", fshellcode_bad_byte_rewrite,
       Flag, f_Group, INVALID, nullptr, NoXarchOption, DefaultVis, 0,
       "Before the bad-byte audit fires, walk every strategy registered "
       "through the shellcode plugin SDK "
       "(Plugin.h::registerBadByteRewriteStrategy) and let them rewrite raw "
       ".text bytes into bad-byte-free equivalents (default).",
       nullptr, nullptr)
OPTION(prefix_1, "-fshellcode-heap-arena", fshellcode_heap_arena,
       Flag, f_Group, INVALID, nullptr, NoXarchOption, DefaultVis, 0,
       "Rewrite malloc/free/calloc/realloc calls into a stack-resident "
       "arena allocator for small allocations (<= 64 KB). Large allocations "
       "fall back to the OS allocator (msvcrt.dll via PEB walk on Windows, "
       "mmap via syscall on Linux/macOS). Enabled by default in shellcode "
       "mode.",
       nullptr, nullptr)
OPTION(prefix_1, "-fno-shellcode-heap-arena", fno_shellcode_heap_arena,
       Flag, f_Group, INVALID, nullptr, NoXarchOption, DefaultVis, 0,
       "Disable the shellcode heap arena pass; malloc/free calls will be "
       "left as unresolved externals (original behaviour).",
       nullptr, nullptr)
OPTION(prefix_1, "-fshellcode-bad-bytes=", fshellcode_bad_bytes_EQ, Joined,
       f_Group, INVALID, nullptr, NoXarchOption, DefaultVis, 0,
       "Reject the final shellcode .bin if it contains any byte in the "
       "comma-separated hex list (example: -fshellcode-bad-bytes=00,0a,0d)",
       nullptr, nullptr)
OPTION(prefix_1, "-fshellcode-charset=", fshellcode_charset_EQ, Joined, f_Group,
       INVALID, nullptr, NoXarchOption, DefaultVis, 0,
       "Run the shellcode .text through the charset encoder registered under "
       "<name> via Plugin.h::registerCharsetEncoder, then prepend its "
       "target-specific decoder stub before the bad-byte audit fires. The "
       "compiler ships no built-in charsets; downstream libraries are expected "
       "to register printable / alphanumeric / custom alphabets through the "
       "plugin SDK. Unknown names are a hard error.",
       nullptr, nullptr)
OPTION(prefix_1, "-fshellcode-entry=", fshellcode_entry_EQ, Joined, f_Group,
       INVALID, nullptr, NoXarchOption, DefaultVis, 0,
       "Override the shellcode entry symbol name (default: main / _main / "
       "shellcode_entry)",
       nullptr, nullptr)
OPTION(prefix_1, "-fshellcode-keep-obj=", fshellcode_keep_obj_EQ, Joined,
       f_Group, INVALID, nullptr, NoXarchOption, DefaultVis, 0,
       "Copy the intermediate object file (Mach-O / ELF / COFF) to <path> so "
       "otool -rv / llvm-objdump -dr / dumpbin can inspect relocations",
       nullptr, nullptr)
OPTION(prefix_1, "-fshellcode-max-length=", fshellcode_max_length_EQ, Joined,
       f_Group, INVALID, nullptr, NoXarchOption, DefaultVis, 0,
       "Reject the final shellcode .bin if its length (after every other "
       "finalize step) exceeds the given byte count. Accepts plain decimal or "
       "0x-prefixed hex.",
       nullptr, nullptr)
OPTION(prefix_1, "-fshellcode-mir-obfuscate=", fshellcode_mir_obfuscate_EQ,
       Joined, f_Group, INVALID, nullptr, NoXarchOption, DefaultVis, 0,
       "Forwarded verbatim to the registered MIR-level shellcode obfuscation "
       "hooks (RunBeforePreEmit / RunAfterPreEmit). Useful when the IR-level "
       "and MIR-level obfuscators want to take different spec strings. "
       "Defaults to the -fshellcode-obfuscate= value when unset.",
       nullptr, nullptr)
OPTION(prefix_1, "-fshellcode-mode", fshellcode_mode, Flag, INVALID, INVALID,
       nullptr, HelpHidden, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fshellcode-obfuscate=", fshellcode_obfuscate_EQ, Joined,
       f_Group, INVALID, nullptr, NoXarchOption, DefaultVis, 0,
       "Forwarded verbatim to the registered shellcode IR-level obfuscation "
       "hooks (see the shellcode pipeline design doc shipped in the source "
       "tree). Meaningless unless an obfuscator library has linked in; the "
       "shellcode pipeline itself never interprets the value.",
       nullptr, nullptr)
OPTION(prefix_1, "-fshellcode-pad=", fshellcode_pad_EQ, Joined, f_Group,
       INVALID, nullptr, NoXarchOption, DefaultVis, 0,
       "Hex byte used for alignment / max-length padding (example: "
       "-fshellcode-pad=cc). Rejected when the byte also appears in the "
       "bad-byte set, or when neither -fshellcode-align= nor "
       "-fshellcode-max-length= is set.",
       nullptr, nullptr)
OPTION(prefix_1, "-fshellcode", fshellcode, Flag, f_Group, INVALID, nullptr,
       NoXarchOption | NoArgumentUnused, DefaultVis, 0,
       "Enable shellcode compilation: produce a flat .bin whose .text has zero "
       "relocations and no data section (supported on arm64/x86_64 across "
       "macOS/Linux/Android/Windows)",
       nullptr, nullptr)
OPTION(prefix_1, "-fshort-enums", fshort_enums, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis | DefaultVis, 0,
       "Allocate to an enum type only as many bytes as it needs for the "
       "declared range of possible values",
       nullptr, nullptr)
OPTION(prefix_1, "-fshort-wchar", fshort_wchar, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis, 0, "Force wchar_t to be a short unsigned int", nullptr,
       nullptr)
OPTION(prefix_1, "-fshow-column", fshow_column, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fshow-skipped-includes", fshow_skipped_includes, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Show skipped includes in -H output.", nullptr, nullptr)
OPTION(prefix_1, "-fshow-source-location", fshow_source_location, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fsigned-bitfields", fsigned_bitfields, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fsigned-char", fsigned_char, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis | DefaultVis, 0, "char is signed", nullptr, nullptr)
OPTION(prefix_1, "-fsigned-wchar", fsigned_wchar, Flag, INVALID, INVALID,
       nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
       "Use a signed type for wchar_t", nullptr, nullptr)
OPTION(prefix_1, "-fsigned-zeros", fsigned_zeros, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fslp-vectorize", fslp_vectorize, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Enable the superword-level parallelism vectorization passes", nullptr,
       nullptr)
OPTION(prefix_1, "-fspell-checking-limit=", fspell_checking_limit_EQ, Joined,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Set the maximum number of times to perform spell checking on "
       "unrecognized identifiers (0 = no limit)",
       nullptr, nullptr)
OPTION(prefix_1, "-fspell-checking", fspell_checking, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fsplit-dwarf-inlining", fsplit_dwarf_inlining, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Provide minimal debug info in the object/executable to facilitate "
       "online symbolication/stack traces in the absence of .dwo/.dwp files "
       "when using Split DWARF",
       nullptr, nullptr)
OPTION(prefix_1, "-fsplit-machine-functions", fsplit_machine_functions, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Enable late function splitting using profile information (x86 ELF)",
       nullptr, nullptr)
OPTION(prefix_1, "-fsplit-stack", fsplit_stack, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis | DefaultVis, 0, "Use segmented stack", nullptr, nullptr)
OPTION(prefix_1, "-fstack-clash-protection", fstack_clash_protection, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Enable stack clash protection", nullptr, nullptr)
OPTION(prefix_1, "-fstack-protector-all", fstack_protector_all, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Enable stack protectors for all functions", nullptr, nullptr)
OPTION(
    prefix_1, "-fstack-protector-strong", fstack_protector_strong, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Enable stack protectors for some functions vulnerable to stack smashing. "
    "Compared to -fstack-protector, this uses a stronger heuristic that "
    "includes functions containing arrays of any size (and any type), as well "
    "as any calls to alloca or the taking of an address from a local variable",
    nullptr, nullptr)
OPTION(
    prefix_1, "-fstack-protector", fstack_protector, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Enable stack protectors for some functions vulnerable to stack smashing. "
    "This uses a loose heuristic which considers functions vulnerable if they "
    "contain a char (or 8bit integer) array or constant sized calls to alloca "
    ", which are of greater size than ssp-buffer-size (default: 8 bytes). All "
    "variable sized calls to alloca are considered vulnerable. A function with "
    "a stack protector has a guard value added to the stack frame that is "
    "checked on function exit. The guard value must be positioned in the stack "
    "frame such that a buffer overflow from a vulnerable variable will "
    "overwrite the guard value before overwriting the function's return "
    "address. The reference stack guard value is stored in a global variable.",
    nullptr, nullptr)
OPTION(prefix_1, "-fstack-size-section", fstack_size_section, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Emit section containing metadata on function stack sizes", nullptr,
       nullptr)
OPTION(prefix_1, "-fstack-usage", fstack_usage, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis, 0,
       "Emit .su file containing information on function stack sizes", nullptr,
       nullptr)
OPTION(prefix_1, "-fstandalone-debug", fstandalone_debug, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Emit full debug info for all types used by the program", nullptr,
       nullptr)
OPTION(prefix_1, "-fstrict-aliasing", fstrict_aliasing, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Enable optimizations based on strict aliasing rules", nullptr, nullptr)
OPTION(prefix_1, "-fstrict-flex-arrays=", fstrict_flex_arrays_EQ, Joined,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Enable optimizations based on the strict definition of flexible arrays",
       "<n>", "0,1,2,3")
OPTION(prefix_1, "-fstrict-float-cast-overflow", fstrict_float_cast_overflow,
       Flag, f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Assume that overflowing float-to-int casts are undefined (default)",
       nullptr, nullptr)
OPTION(prefix_1, "-fstrict-overflow", fstrict_overflow, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fstruct-path-tbaa", fstruct_path_tbaa, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fsymbol-partition=", fsymbol_partition_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fsyntax-only", fsyntax_only, Flag, Action_Group, INVALID,
       nullptr, NoXarchOption, DefaultVis, 0,
       "Run the preprocessor, parser and semantic analysis stages", nullptr,
       nullptr)
OPTION(prefix_1, "-ftabstop=", ftabstop_EQ, Joined, f_Group, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-ftabstop", ftabstop, Separate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Set the tab stop distance.", "<N>", nullptr)
OPTION(prefix_1, "-ftime-report=", ftime_report_EQ, Joined, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0,
       "(For new pass manager) 'per-pass': one report for each pass; "
       "'per-pass-run': one report for each pass invocation",
       nullptr, "per-pass,per-pass-run")
OPTION(prefix_1, "-ftime-report", ftime_report, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-ftime-trace-granularity=", ftime_trace_granularity_EQ,
       Joined, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Minimum time granularity (in microseconds) traced by time profiler",
       nullptr, nullptr)
OPTION(prefix_1, "-ftime-trace=", ftime_trace_EQ, Joined, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Similar to -ftime-trace. Specify the JSON file or a directory which "
       "will contain the JSON file",
       nullptr, nullptr)
OPTION(prefix_1, "-ftime-trace", ftime_trace, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis, 0,
       "Turn on time profiler. Generates JSON file based on output filename.",
       nullptr, nullptr)
OPTION(prefix_1, "-ftls-model=", ftlsmodel_EQ, Joined, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       "global-dynamic,local-dynamic,initial-exec,local-exec")
OPTION(prefix_1, "-ftrap-function=", ftrap_function_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Issue call to specified function rather than a trap instruction",
       nullptr, nullptr)
OPTION(prefix_1, "-ftrapping-math", ftrapping_math, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-ftrapv-handler=", ftrapv_handler_EQ, Joined, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Specify the function to be called on overflow", "<function name>",
       nullptr)
OPTION(prefix_1, "-ftrapv-handler", ftrapv_handler, Separate, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-ftrapv", ftrapv, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Trap on integer overflow", nullptr, nullptr)
OPTION(prefix_1, "-ftreat-warnings-as-errors", ftreat_warnings_as_errors, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0, "Treat warnings as errors",
       nullptr, nullptr)
OPTION(prefix_1, "-ftree-slp-vectorize", anonymous_481, Flag, INVALID,
       fslp_vectorize, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-ftree-vectorize", anonymous_479, Flag, INVALID, fvectorize,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-ftrigraphs", ftrigraphs, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Process trigraph sequences", nullptr, nullptr)
OPTION(prefix_1,
       "-ftrivial-auto-var-init-stop-after=", ftrivial_auto_var_init_stop_after,
       Joined, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Stop initializing trivial automatic stack variables after the "
       "specified number of instances",
       nullptr, nullptr)
OPTION(
    prefix_1, "-ftrivial-auto-var-init=", ftrivial_auto_var_init, Joined,
    f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Initialize trivial automatic stack variables. Defaults to 'uninitialized'",
    nullptr, "uninitialized,zero,pattern")
OPTION(prefix_1, "-ftype-visibility=", ftype_visibility, Joined, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0, "Default type visibility",
       nullptr, "default,hidden,internal,protected")
OPTION(prefix_1, "-function-alignment", function_alignment, Separate, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "default alignment for functions", nullptr, nullptr)
OPTION(prefix_1, "-funique-basic-block-section-names",
       funique_basic_block_section_names, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0,
       "Use unique names for basic block sections (ELF Only)", nullptr, nullptr)
OPTION(prefix_1, "-funique-internal-linkage-names",
       funique_internal_linkage_names, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0,
       "Uniqueify Internal Linkage Symbol Names by appending the MD5 hash of "
       "the module path",
       nullptr, nullptr)
OPTION(prefix_1, "-funique-section-names", funique_section_names, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-funroll-loops", funroll_loops, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Turn on loop unroller", nullptr, nullptr)
OPTION(prefix_1, "-funsafe-math-optimizations", funsafe_math_optimizations,
       Flag, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Allow unsafe floating-point math optimizations which may decrease "
       "precision",
       nullptr, nullptr)
OPTION(prefix_1, "-funsigned-bitfields", funsigned_bitfields, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-funsigned-char", funsigned_char, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-funwind-tables=", funwind_tables_EQ, Joined, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Generate unwinding tables for all functions", nullptr, nullptr)
OPTION(prefix_1, "-funwind-tables", funwind_tables, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fuse-cxa-atexit", fuse_cxa_atexit, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fuse-init-array", fuse_init_array, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-fuse-ld=", fuse_ld_EQ, Joined, f_Group, INVALID, nullptr,
       LinkOption | HelpHidden, DefaultVis, 0,
       "Accepted for gcc build compatibility (e.g. Linux kernel Kbuild). "
       "NeverC always links in-process; this value is ignored.",
       "<name>", nullptr)
OPTION(prefix_1, "-fuse-line-directives", fuse_line_directives, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Use #line in preprocessed output", nullptr, nullptr)
OPTION(prefix_1, "-fveclib=", fveclib, Joined, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Use the given vector functions library", nullptr,
       "Accelerate,libmvec,MASSV,SVML,SLEEF,Darwin_libsystem_m,ArmPL,none")
OPTION(prefix_1, "-fvectorize", fvectorize, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Enable the loop vectorization passes", nullptr, nullptr)
OPTION(prefix_1, "-fverbose-asm", fverbose_asm, Flag, f_Group, INVALID, nullptr,
       0, DefaultVis, 0, "Generate verbose assembly output", nullptr, nullptr)
OPTION(prefix_1,
       "-fverify-debuginfo-preserve-export=", fverify_debuginfo_preserve_export,
       Joined, INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Export debug info (by testing original Debug Info) failures into "
       "specified (JSON) file (should be abs path as we use append mode to "
       "insert new JSON objects).",
       "<file>", nullptr)
OPTION(prefix_1, "-fverify-debuginfo-preserve", fverify_debuginfo_preserve,
       Flag, INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Enable Debug Info Metadata preservation testing in optimizations.",
       nullptr, nullptr)
OPTION(prefix_1, "-fverify-intermediate-code", fverify_intermediate_code, Flag,
       f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Enable verification of LLVM IR", nullptr, nullptr)
OPTION(prefix_1, "-fvisibility-dllexport=", fvisibility_dllexport_EQ, Joined,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "The visibility for dllexport definitions "
       "[-fvisibility-from-dllstorageclass]",
       nullptr, "default,hidden,internal,protected")
OPTION(prefix_1,
       "-fvisibility-externs-dllimport=", fvisibility_externs_dllimport_EQ,
       Joined, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "The visibility for dllimport external declarations "
       "[-fvisibility-from-dllstorageclass]",
       nullptr, "default,hidden,internal,protected")
OPTION(prefix_1, "-fvisibility-externs-nodllstorageclass=",
       fvisibility_externs_nodllstorageclass_EQ, Joined, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0,
       "The visibility for external declarations without an explicit DLL "
       "dllstorageclass [-fvisibility-from-dllstorageclass]",
       nullptr, "default,hidden,internal,protected")
OPTION(prefix_1, "-fvisibility-from-dllstorageclass",
       fvisibility_from_dllstorageclass, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0,
       "Set the visibility of symbols in the generated code from their DLL "
       "storage class",
       nullptr, nullptr)
OPTION(prefix_1, "-fvisibility-ms-compat", fvisibility_ms_compat, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Give global types 'default' visibility and global functions and "
       "variables 'hidden' visibility by default",
       nullptr, nullptr)
OPTION(prefix_1,
       "-fvisibility-nodllstorageclass=", fvisibility_nodllstorageclass_EQ,
       Joined, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "The visibility for definitions without an explicit DLL export class "
       "[-fvisibility-from-dllstorageclass]",
       nullptr, "default,hidden,internal,protected")
OPTION(prefix_1, "-fvisibility=", fvisibility_EQ, Joined, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Set the default symbol visibility for all global definitions", nullptr,
       "default,hidden,internal,protected")
OPTION(prefix_1, "-fwarn-stack-size=", fwarn_stack_size_EQ, Joined, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-fwchar-type=", fwchar_type_EQ, Joined, INVALID, INVALID,
       nullptr, HelpHidden, DefaultVis, 0, "Select underlying type for wchar_t",
       nullptr, "char,short,int")
OPTION(prefix_1, "-fwrapv", fwrapv, Flag, f_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Treat signed integer overflow as two's complement",
       nullptr, nullptr)
OPTION(prefix_1, "-fwritable-strings", fwritable_strings, Flag, f_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Store string literals as writable data", nullptr, nullptr)
OPTION(
    prefix_1, "-fzero-call-used-regs=", fzero_call_used_regs_EQ, Joined,
    f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Clear call-used registers upon function return (AArch64/x86 only)",
    nullptr,
    "skip,used-gpr-arg,used-gpr,used-arg,used,all-gpr-arg,all-gpr,all-arg,all")
OPTION(prefix_1, "-fzero-initialized-in-bss", fzero_initialized_in_bss, Flag,
       f_Group, INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-F", F, JoinedOrSeparate, INVALID, INVALID, nullptr,
       RenderJoined, DefaultVis, 0,
       "Add directory to framework include search path", nullptr, nullptr)
OPTION(prefix_1, "-g0", g0, Flag, gN_Group, INVALID, nullptr, 0, DefaultVis, 0,
       nullptr, nullptr, nullptr)
OPTION(prefix_1, "-g1", g1, Flag, gN_Group, gline_tables_only, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-g2", g2, Flag, gN_Group, INVALID, nullptr, 0, DefaultVis, 0,
       nullptr, nullptr, nullptr)
OPTION(prefix_1, "-g3", g3, Flag, gN_Group, INVALID, nullptr, 0, DefaultVis, 0,
       nullptr, nullptr, nullptr)
OPTION(prefix_3, "--gcc-install-dir=", gcc_install_dir_EQ, Joined, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Use GCC installation in the specified directory. The directory ends "
       "with path components like 'lib{,32,64}/gcc{,-cross}/$triple/$version'. "
       "Note: executables (e.g. ld) used by the compiler are not overridden by "
       "the selected GCC installation",
       nullptr, nullptr)
OPTION(prefix_3, "--gcc-toolchain=", gcc_toolchain, Joined, INVALID, INVALID,
       nullptr, NoXarchOption, DefaultVis, 0,
       "Specify a directory where the compiler can find 'include' and "
       "'lib{,32,64}/gcc{,-cross}/$triple/$version'. The compiler will use the "
       "GCC installation with the largest version",
       nullptr, nullptr)
OPTION(prefix_1, "-gcolumn-info", gcolumn_info, Flag, g_flags_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-gdwarf-2", gdwarf_2, Flag, g_Group, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Generate source-level debug information with dwarf version 2", nullptr,
       nullptr)
OPTION(prefix_1, "-gdwarf-3", gdwarf_3, Flag, g_Group, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Generate source-level debug information with dwarf version 3", nullptr,
       nullptr)
OPTION(prefix_1, "-gdwarf-4", gdwarf_4, Flag, g_Group, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Generate source-level debug information with dwarf version 4", nullptr,
       nullptr)
OPTION(prefix_1, "-gdwarf-5", gdwarf_5, Flag, g_Group, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Generate source-level debug information with dwarf version 5", nullptr,
       nullptr)
OPTION(prefix_1, "-gdwarf-aranges", gdwarf_aranges, Flag, g_flags_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-gdwarf32", gdwarf32, Flag, g_Group, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Enables DWARF32 format for ELF binaries, if debug information emission "
       "is enabled.",
       nullptr, nullptr)
OPTION(prefix_1, "-gdwarf64", gdwarf64, Flag, g_Group, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Enables DWARF64 format for ELF binaries, if debug information emission "
       "is enabled.",
       nullptr, nullptr)
OPTION(prefix_1, "-gdwarf", gdwarf, Flag, g_Group, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Generate source-level debug information with the default dwarf version",
       nullptr, nullptr)
OPTION(prefix_1, "-gembed-source", gembed_source, Flag, g_flags_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Embed source text in DWARF debug sections",
       nullptr, nullptr)
OPTION(prefix_1, "-gen-cdb-fragment-path", gen_cdb_fragment_path, Separate,
       internal_debug_Group, INVALID, nullptr, NoXarchOption | HelpHidden,
       DefaultVis, 0,
       "Emit a compilation database fragment to the specified directory",
       nullptr, nullptr)
OPTION(prefix_1, "-gen-reproducer=", gen_reproducer_eq, Joined, INVALID,
       INVALID, nullptr, NoArgumentUnused, DefaultVis, 0,
       "Emit reproducer on (option: off, crash (default), error, always)",
       nullptr, nullptr)
OPTION(prefix_1, "-gen-reproducer", gen_reproducer, Flag, internal_debug_Group,
       gen_reproducer_eq, "always\0", NoXarchOption | HelpHidden, DefaultVis, 0,
       "Auto-generates preprocessed source files and a reproduction script",
       nullptr, nullptr)
OPTION(prefix_1, "-gfull", gfull, Flag, g_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-ggdb0", ggdb0, Flag, ggdbN_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-ggdb1", ggdb1, Flag, ggdbN_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-ggdb2", ggdb2, Flag, ggdbN_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-ggdb3", ggdb3, Flag, ggdbN_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-ggdb", ggdb, Flag, gTune_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-ggnu-pubnames", ggnu_pubnames, Flag, g_flags_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-ginline-line-tables", ginline_line_tables, Flag, g_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-gline-directives-only", gline_directives_only, Flag,
       gN_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Emit debug line info directives only", nullptr, nullptr)
OPTION(prefix_1, "-gline-tables-only", gline_tables_only, Flag, gN_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "Emit debug line number tables only",
       nullptr, nullptr)
OPTION(prefix_1, "-glldb", glldb, Flag, gTune_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-gmlt", gmlt, Flag, INVALID, gline_tables_only, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-gno-column-info", gno_column_info, Flag, g_flags_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-gno-embed-source", gno_embed_source, Flag, g_flags_Group,
       INVALID, nullptr, NoXarchOption, DefaultVis, 0,
       "Restore the default behavior of not embedding source text in DWARF "
       "debug sections",
       nullptr, nullptr)
OPTION(prefix_1, "-gno-gnu-pubnames", gno_gnu_pubnames, Flag, g_flags_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-gno-inline-line-tables", gno_inline_line_tables, Flag,
       g_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Don't emit inline line tables.", nullptr, nullptr)
OPTION(prefix_1, "-gno-pubnames", gno_pubnames, Flag, g_flags_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-gno-record-command-line", gno_record_command_line, Flag,
       g_flags_Group, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-gno-record-gcc-switches", anonymous_596, Flag, INVALID,
       gno_record_command_line, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-gno-split-dwarf", gno_split_dwarf, Flag, g_flags_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-gno-strict-dwarf", gno_strict_dwarf, Flag, g_flags_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-gpubnames", gpubnames, Flag, g_flags_Group, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-grecord-command-line", grecord_command_line, Flag,
       g_flags_Group, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-grecord-gcc-switches", anonymous_595, Flag, INVALID,
       grecord_command_line, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-gsplit-dwarf=", gsplit_dwarf_EQ, Joined, g_flags_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "Set DWARF fission mode", nullptr,
       "split,single")
OPTION(prefix_1, "-gsplit-dwarf", gsplit_dwarf, Flag, g_flags_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-gsrc-hash=", gsrc_hash_EQ, Joined, g_flags_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, "md5,sha1,sha256")
OPTION(prefix_1, "-gstrict-dwarf", gstrict_dwarf, Flag, g_flags_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "Restrict DWARF features to those defined in the specified version, "
       "avoiding features from later versions.",
       nullptr, nullptr)
OPTION(prefix_1, "-gused", gused, Flag, g_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-gz=", gz_EQ, Joined, g_flags_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "DWARF debug sections compression type", nullptr, nullptr)
OPTION(prefix_1, "-gz", gz, Flag, g_flags_Group, gz_EQ, "zlib\0", 0, DefaultVis,
       0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-g", g_Flag, Flag, g_Group, INVALID, nullptr, 0, DefaultVis,
       0, "Generate source-level debug information", nullptr, nullptr)
OPTION(prefix_1, "-header-include-file", header_include_file, Separate, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Filename (or -) to write header include output to", nullptr, nullptr)
OPTION(prefix_1, "-header-include-filtering=", header_include_filtering_EQ,
       Joined, INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "set the flag that enables filtering header information", nullptr,
       "none,only-direct-system")
OPTION(prefix_1, "-header-include-format=", header_include_format_EQ, Joined,
       INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "set format in which header info is emitted", nullptr, "textual,json")
OPTION(prefix_1, "-headerpad_max_install_names", headerpad__max__install__names,
       Joined, INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_3, "--help-hidden", _help_hidden, Flag, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, "Display help for hidden options", nullptr, nullptr)
OPTION(prefix_2, "-help", help, Flag, INVALID, INVALID, nullptr, 0, DefaultVis,
       0, "Display available options", nullptr, nullptr)
OPTION(prefix_1, "-H", H, Flag, Preprocessor_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Show header includes and nesting depth", nullptr,
       nullptr)
OPTION(prefix_1, "-I-", I_, Flag, I_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Restrict all prior -I flags to double-quoted inclusion and remove "
       "current directory from include path",
       nullptr, nullptr)
OPTION(
    prefix_1, "-ibuiltininc", ibuiltininc, Flag, neverc_i_Group, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Enable builtin #include directories even when -nostdinc is used before or "
    "after -ibuiltininc. Using -nobuiltininc after the option disables it",
    nullptr, nullptr)
OPTION(prefix_1, "-idirafter", idirafter, JoinedOrSeparate, neverc_i_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Add directory to AFTER include search path", nullptr, nullptr)
OPTION(prefix_1, "-iframeworkwithsysroot", iframeworkwithsysroot,
       JoinedOrSeparate, neverc_i_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Add directory to SYSTEM framework search path, absolute paths are "
       "relative to -isysroot",
       "<directory>", nullptr)
OPTION(prefix_1, "-iframework", iframework, JoinedOrSeparate, neverc_i_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Add directory to SYSTEM framework search path", nullptr, nullptr)
OPTION(prefix_2, "-imacros", imacros, JoinedOrSeparate, neverc_i_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Include macros from file before parsing",
       "<file>", nullptr)
OPTION(prefix_1, "-image_base", image__base, Separate, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "-imsvc", imsvc, JoinedOrSeparate, msvc_Group, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Add <dir> to MSVC system include search path", "<dir>", nullptr)
OPTION(prefix_2, "-include", include, JoinedOrSeparate, neverc_i_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Include file before parsing", "<file>",
       nullptr)
OPTION(prefix_1, "-index-header-map", index_header_map, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Make the next included directory (-I or -F) an indexer header map",
       nullptr, nullptr)
OPTION(prefix_1, "-init", init, Separate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-inline-asm=", inline_asm_EQ, Joined, m_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0, nullptr, nullptr, "att,intel")
OPTION(prefix_1, "-install_name", install__name, Separate, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-integrated-as", anonymous_678, Flag, INVALID, fintegrated_as,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-internal-externc-isystem", internal_externc_isystem,
       Separate, INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Add directory to the internal system include search path with implicit "
       "extern \"C\" semantics; these are assumed to not be user-provided and "
       "are used to model system and standard headers' paths.",
       "<directory>", nullptr)
OPTION(prefix_1, "-internal-isystem", internal_isystem, Separate, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Add directory to the internal system include search path; these are "
       "assumed to not be user-provided and are used to model system and "
       "standard headers' paths.",
       "<directory>", nullptr)
OPTION(prefix_1, "-iprefix", iprefix, JoinedOrSeparate, neverc_i_Group, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Set the -iwithprefix/-iwithprefixbefore prefix", "<dir>", nullptr)
OPTION(prefix_1, "-iquote", iquote, JoinedOrSeparate, neverc_i_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Add directory to QUOTE include search path",
       "<directory>", nullptr)
OPTION(prefix_1, "-isysroot", isysroot, JoinedOrSeparate, neverc_i_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Set the system root directory (usually /)", "<dir>", nullptr)
OPTION(prefix_1, "-isystem-after", isystem_after, JoinedOrSeparate,
       neverc_i_Group, INVALID, nullptr, NoXarchOption, DefaultVis, 0,
       "Add directory to end of the SYSTEM include search path", "<directory>",
       nullptr)
OPTION(prefix_1, "-isystem", isystem, JoinedOrSeparate, neverc_i_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Add directory to SYSTEM include search path",
       "<directory>", nullptr)
OPTION(prefix_1, "-ivfsoverlay", ivfsoverlay, JoinedOrSeparate, neverc_i_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Overlay the virtual filesystem described by file over the real file "
       "system",
       nullptr, nullptr)
OPTION(prefix_1, "-iwithprefixbefore", iwithprefixbefore, JoinedOrSeparate,
       neverc_i_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Set directory to include search path with prefix", "<dir>", nullptr)
OPTION(prefix_1, "-iwithprefix", iwithprefix, JoinedOrSeparate, neverc_i_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Set directory to SYSTEM include search path with prefix", "<dir>",
       nullptr)
OPTION(prefix_1, "-iwithsysroot", iwithsysroot, JoinedOrSeparate,
       neverc_i_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Add directory to SYSTEM include search path, absolute paths are "
       "relative to -isysroot",
       "<directory>", nullptr)
OPTION(prefix_1, "-I", I, JoinedOrSeparate, I_Group, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Add directory to the end of the list of include search paths", "<dir>",
       nullptr)
OPTION(prefix_1, "-K", K, Flag, INVALID, INVALID, nullptr, LinkerInput,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_3, "--linker-option=", linker_option, Joined, INVALID, INVALID,
       nullptr, HelpHidden, DefaultVis, 0, "Add linker option", nullptr,
       nullptr)
OPTION(prefix_1, "-llvm-verify-each", llvm_verify_each, Flag, INVALID, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Run the LLVM verifier after every LLVM pass", nullptr, nullptr)
OPTION(prefix_1, "-L", L, JoinedOrSeparate, Link_Group, INVALID, nullptr,
       RenderJoined, DefaultVis, 0, "Add directory to library search path",
       "<dir>", nullptr)
OPTION(prefix_1, "-l", l, JoinedOrSeparate, Link_Group, INVALID, nullptr,
       LinkerInput | RenderJoined, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-m3dnowa", m3dnowa, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-m3dnow", m3dnow, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-m64", m64, Flag, m_Group, INVALID, nullptr, NoXarchOption,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-m80387", m80387, Flag, INVALID, mx87, nullptr,
       TargetSpecific, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mabi=", mabi_EQ, Joined, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-Mach", Mach, Flag, Link_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-madx", madx, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-maes", maes, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-main-file-name", main_file_name, Separate, INVALID, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Main file name to use for debug info and source if missing", nullptr,
       nullptr)
OPTION(prefix_1, "-malign-branch-boundary=", malign_branch_boundary_EQ, Joined,
       m_Group, INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0,
       "Specify the boundary's size to align branches", nullptr, nullptr)
OPTION(prefix_1, "-malign-branch=", malign_branch_EQ, CommaJoined, m_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0,
       "Specify types of branches to align", nullptr, nullptr)
OPTION(prefix_1, "-malign-double", malign_double, Flag, m_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "Align doubles to two words in structs (x86 only)", nullptr, nullptr)
OPTION(prefix_1, "-mamx-bf16", mamx_bf16, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mamx-complex", mamx_complex, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mamx-fp16", mamx_fp16, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mamx-int8", mamx_int8, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mamx-tile", mamx_tile, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mapx-features=", mapx_features_EQ, CommaJoined,
       m_x86_Features_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0, "Enable features of APX", nullptr,
       "egpr,push2pop2,ppx,ndd,ccmp,cf")
OPTION(prefix_1, "-mapxf", mapxf, Flag, INVALID, mapx_features_EQ,
       "egpr\0push2pop2\0ppx\0", TargetSpecific, DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-march=", march_EQ, Joined, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "For a list of available architectures for the target use '-mcpu=help'",
       nullptr, nullptr)
OPTION(prefix_1, "-masm=", masm_EQ, Joined, m_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-massembler-fatal-warnings", massembler_fatal_warnings, Flag,
       INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Make assembler warnings fatal", nullptr, nullptr)
OPTION(prefix_1, "-massembler-no-warn", massembler_no_warn, Flag, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Make assembler not emit warnings", nullptr, nullptr)
OPTION(prefix_1, "-mavx10.1-256", mavx10_1_256, Flag,
       m_x86_AVX10_Features_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mavx10.1-512", mavx10_1_512, Flag,
       m_x86_AVX10_Features_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mavx10.1", mavx10_1, Flag, INVALID, mavx10_1_256, nullptr,
       TargetSpecific, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mavx2", mavx2, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mavx512bf16", mavx512bf16, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mavx512bitalg", mavx512bitalg, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mavx512bw", mavx512bw, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mavx512cd", mavx512cd, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mavx512dq", mavx512dq, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mavx512er", mavx512er, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mavx512fp16", mavx512fp16, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mavx512f", mavx512f, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mavx512ifma", mavx512ifma, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mavx512pf", mavx512pf, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mavx512vbmi2", mavx512vbmi2, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mavx512vbmi", mavx512vbmi, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mavx512vl", mavx512vl, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mavx512vnni", mavx512vnni, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mavx512vp2intersect", mavx512vp2intersect, Flag,
       m_x86_Features_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mavx512vpopcntdq", mavx512vpopcntdq, Flag,
       m_x86_Features_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mavxifma", mavxifma, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mavxneconvert", mavxneconvert, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mavxvnniint16", mavxvnniint16, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mavxvnniint8", mavxvnniint8, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mavxvnni", mavxvnni, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mavx", mavx, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mbmi2", mbmi2, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mbmi", mbmi, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mbranch-protection-pauth-lr", mbranch_protection_pauth_lr,
       Flag, INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mbranch-protection=", mbranch_protection_EQ, Joined, m_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0,
       "Enforce targets of indirect branches and function returns", nullptr,
       nullptr)
OPTION(prefix_1, "-mbranch-target-enforce", mbranch_target_enforce, Flag,
       INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mbranches-within-32B-boundaries",
       mbranches_within_32B_boundaries, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Align selected branches (fused, jcc, jmp) within 32-byte boundary",
       nullptr, nullptr)
OPTION(prefix_1, "-mcldemote", mcldemote, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mclflushopt", mclflushopt, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mclwb", mclwb, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mclzero", mclzero, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mcmodel=", mcmodel_EQ, Joined, m_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mcmpccxadd", mcmpccxadd, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mcpu=help", anonymous_662, Flag, INVALID,
       print_supported_cpus, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mcpu=", mcpu_EQ, Joined, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "For a list of available CPUs for the target use '-mcpu=help'", nullptr,
       nullptr)
OPTION(prefix_1, "-mcrc32", mcrc32, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mcrc", mcrc, Flag, m_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0,
       "Allow use of CRC instructions (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-mcx16", mcx16, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mdebug-pass", mdebug_pass, Separate, INVALID, INVALID,
       nullptr, HelpHidden, DefaultVis, 0, "Enable additional debug output",
       nullptr, nullptr)
OPTION(prefix_1, "-mdefault-visibility-export-mapping=",
       mdefault_visibility_export_mapping_EQ, Joined, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Mapping between default visibility and export", nullptr,
       "none,explicit,all")
OPTION(prefix_1, "-mdouble=", mdouble_EQ, Joined, m_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0, "Force double to be <n> bits", "<n", "32,64")
OPTION(prefix_1, "-MD", MD, Flag, M_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Write a depfile containing user and system headers", nullptr, nullptr)
OPTION(prefix_1, "-menable-no-infs", menable_no_infinities, Flag, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Allow optimization to assume there are no infinities.", nullptr,
       nullptr)
OPTION(prefix_1, "-menable-no-nans", menable_no_nans, Flag, INVALID, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Allow optimization to assume there are no NaNs.", nullptr, nullptr)
OPTION(prefix_1, "-menqcmd", menqcmd, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mevex512", mevex512, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mf16c", mf16c, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mfix-cortex-a53-835769", mfix_cortex_a53_835769, Flag,
       m_aarch64_Features_Group, INVALID, nullptr, TargetSpecific, DefaultVis,
       0, "Workaround Cortex-A53 erratum 835769 (AArch64 only)", nullptr,
       nullptr)
OPTION(prefix_1, "-mfloat-abi", mfloat_abi, Separate, INVALID, INVALID, nullptr,
       HelpHidden, DefaultVis, 0, "The float ABI to use", nullptr, nullptr)
OPTION(prefix_1, "-mfma4", mfma4, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mfma", mfma, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mfpmath=", mfpmath_EQ, Joined, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mfpmath", mfpmath, Separate, INVALID, INVALID, nullptr,
       HelpHidden, DefaultVis, 0, "Which unit to use for fp math", nullptr,
       nullptr)
OPTION(prefix_1, "-mframe-pointer=", mframe_pointer_EQ, Joined, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Specify which frame pointers to retain.", nullptr, "all,non-leaf,none")
OPTION(prefix_1, "-mfsgsbase", mfsgsbase, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mfunction-return=", mfunction_return_EQ, Joined, m_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Replace returns with jumps to ``__x86_return_thunk`` (x86 only, error "
       "otherwise)",
       nullptr, "keep,thunk-extern")
OPTION(prefix_1, "-mfxsr", mfxsr, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-MF", MF, JoinedOrSeparate, M_Group, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Write depfile output from -MMD, -MD, -MM, or -M to <file>", "<file>",
       nullptr)
OPTION(prefix_1, "-mgeneral-regs-only", mgeneral_regs_only, Flag, m_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0,
       "Generate code which only uses the general purpose registers "
       "(AArch64/x86 only)",
       nullptr, nullptr)
OPTION(prefix_1, "-mgfni", mgfni, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mglobal-merge", mglobal_merge, Flag, m_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0, "Enable merging of globals",
       nullptr, nullptr)
OPTION(prefix_1, "-MG", MG, Flag, M_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Add missing headers to depfile", nullptr, nullptr)
OPTION(prefix_1, "-mhard-float", mhard_float, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mharden-sls=", mharden_sls_EQ, Joined, m_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0,
       "Select straight-line speculation hardening scope (AArch64/X86 only). "
       "<arg> must be: all, none, retbr(AArch64), blr(AArch64), "
       "comdat(AArch64), nocomdat(AArch64), return(X86), indirect-jmp(X86)",
       nullptr, nullptr)
OPTION(prefix_1, "-mhreset", mhreset, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mimplicit-float", mimplicit_float, Flag, m_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mimplicit-it=", mimplicit_it_EQ, Joined, m_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mincremental-linker-compatible",
       mincremental_linker_compatible, Flag, m_Group, INVALID, nullptr,
       HelpHidden, DefaultVis | DefaultVis, 0,
       "Emit an object file which can be used with an incremental linker",
       nullptr, nullptr)
OPTION(prefix_1, "-mindirect-branch-cs-prefix", mindirect_branch_cs_prefix,
       Flag, m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Add cs prefix to call and jmp to indirect thunk", nullptr, nullptr)
OPTION(prefix_1, "-minvpcid", minvpcid, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mios-simulator-version-min=", mios_simulator_version_min_EQ,
       Joined, m_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mios-version-min=", mios_version_min_EQ, Joined, m_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0,
       "Set iOS deployment target", nullptr, nullptr)
OPTION(prefix_1, "-miphoneos-version-min=", anonymous_619, Joined, m_Group,
       mios_version_min_EQ, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0,
       nullptr, nullptr, nullptr)
OPTION(prefix_1, "-miphonesimulator-version-min=", anonymous_620, Joined,
       INVALID, mios_simulator_version_min_EQ, nullptr, TargetSpecific,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-MJ", MJ, JoinedOrSeparate, M_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Write a compilation database entry per input", nullptr,
       nullptr)
OPTION(prefix_1, "-mkernel", mkernel, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mkl", mkl, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mlarge-data-threshold=", mlarge_data_threshold_EQ, Joined,
       m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mlimit-float-precision", mlimit_float_precision, Separate,
       INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Limit float precision to the given value", nullptr, nullptr)
OPTION(prefix_1, "-mlink-bitcode-file", mlink_bitcode_file, Separate, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Link the given bitcode file before performing optimizations.", nullptr,
       nullptr)
OPTION(prefix_1, "-mlink-builtin-bitcode", mlink_builtin_bitcode, Separate,
       INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Link and internalize needed symbols from the given bitcode file before "
       "performing optimizations.",
       nullptr, nullptr)
OPTION(prefix_1, "-mlinker-version=", mlinker_version_EQ, Joined, m_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mlittle-endian", mlittle_endian, Flag,
       m_aarch64_Features_Group, INVALID, nullptr, TargetSpecific, DefaultVis,
       0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mllvm=", anonymous_622, Joined, INVALID, mllvm, nullptr,
       HelpHidden, DefaultVis, 0, "Alias for -mllvm", "<arg>", nullptr)
OPTION(prefix_1, "-mllvm", mllvm, Separate, INVALID, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "Additional arguments to forward to LLVM's option processing", nullptr,
       nullptr)
OPTION(prefix_1, "-mlong-double-128", mlong_double_128, Flag, LongDouble_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "Force long double to be 128 bits",
       nullptr, nullptr)
OPTION(prefix_1, "-mlong-double-64", mlong_double_64, Flag, LongDouble_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "Force long double to be 64 bits",
       nullptr, nullptr)
OPTION(prefix_1, "-mlong-double-80", mlong_double_80, Flag, LongDouble_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Force long double to be 80 bits, padded to 128 bits for storage",
       nullptr, nullptr)
OPTION(prefix_1, "-mlvi-cfi", mlvi_cfi, Flag, m_Group, INVALID, nullptr,
       NoXarchOption, DefaultVis | DefaultVis, 0,
       "Enable only control-flow mitigations for Load Value Injection (LVI)",
       nullptr, nullptr)
OPTION(prefix_1, "-mlvi-hardening", mlvi_hardening, Flag, m_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "Enable all mitigations for Load Value Injection (LVI)", nullptr,
       nullptr)
OPTION(prefix_1, "-mlwp", mlwp, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mlzcnt", mlzcnt, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mmacos-version-min=", mmacos_version_min_EQ, Joined, m_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Set macOS deployment target", nullptr, nullptr)
OPTION(prefix_1, "-mmacosx-version-min=", anonymous_623, Joined, m_Group,
       mmacos_version_min_EQ, nullptr, 0, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mmark-bti-property", mmark_bti_property, Flag,
       m_aarch64_Features_Group, INVALID, nullptr, TargetSpecific, DefaultVis,
       0, "Add .note.gnu.property with BTI to assembly files (AArch64 only)",
       nullptr, nullptr)
OPTION(prefix_1, "-MMD", MMD, Flag, M_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Write a depfile containing user headers", nullptr, nullptr)
OPTION(prefix_1, "-mmmx", mmmx, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mmovbe", mmovbe, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mmovdir64b", mmovdir64b, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mmovdiri", mmovdiri, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mms-bitfields", mms_bitfields, Flag, m_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "Set the default structure layout to be compatible with the Microsoft "
       "compiler standard",
       nullptr, nullptr)
OPTION(prefix_1, "-mmwaitx", mmwaitx, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-MM", MM, Flag, M_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Like -MMD, but also implies -E and writes to stdout by default",
       nullptr, nullptr)
OPTION(prefix_1, "-mno-3dnowa", mno_3dnowa, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-3dnow", mno_3dnow, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-80387", mno_80387, Flag, INVALID, mno_x87, nullptr,
       TargetSpecific, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mno-adx", mno_adx, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-aes", mno_aes, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-amx-bf16", mno_amx_bf16, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-amx-complex", mno_amx_complex, Flag,
       m_x86_Features_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mno-amx-fp16", mno_amx_fp16, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-amx-int8", mno_amx_int8, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-amx-tile", mno_amx_tile, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-apx-features=", mno_apx_features_EQ, CommaJoined,
       m_x86_Features_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0, "Disable features of APX", nullptr,
       "egpr,push2pop2,ppx,ndd,ccmp,cf")
OPTION(prefix_1, "-mno-apxf", mno_apxf, Flag, INVALID, mno_apx_features_EQ,
       "egpr\0push2pop2\0ppx\0", TargetSpecific, DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-avx10.1-256", mno_avx10_1_256, Flag,
       m_x86_AVX10_Features_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mno-avx10.1-512", mno_avx10_1_512, Flag,
       m_x86_AVX10_Features_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mno-avx10.1", mno_avx10_1, Flag, INVALID, mno_avx10_1_256,
       nullptr, TargetSpecific, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mno-avx2", mno_avx2, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-avx512bf16", mno_avx512bf16, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-avx512bitalg", mno_avx512bitalg, Flag,
       m_x86_Features_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mno-avx512bw", mno_avx512bw, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-avx512cd", mno_avx512cd, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-avx512dq", mno_avx512dq, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-avx512er", mno_avx512er, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-avx512fp16", mno_avx512fp16, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-avx512f", mno_avx512f, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-avx512ifma", mno_avx512ifma, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-avx512pf", mno_avx512pf, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-avx512vbmi2", mno_avx512vbmi2, Flag,
       m_x86_Features_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mno-avx512vbmi", mno_avx512vbmi, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-avx512vl", mno_avx512vl, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-avx512vnni", mno_avx512vnni, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-avx512vp2intersect", mno_avx512vp2intersect, Flag,
       m_x86_Features_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mno-avx512vpopcntdq", mno_avx512vpopcntdq, Flag,
       m_x86_Features_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mno-avxifma", mno_avxifma, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-avxneconvert", mno_avxneconvert, Flag,
       m_x86_Features_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mno-avxvnniint16", mno_avxvnniint16, Flag,
       m_x86_Features_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mno-avxvnniint8", mno_avxvnniint8, Flag,
       m_x86_Features_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mno-avxvnni", mno_avxvnni, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-avx", mno_avx, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-bmi2", mno_bmi2, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-bmi", mno_bmi, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-bti-at-return-twice", mno_bti_at_return_twice, Flag,
       m_aarch64_Features_Group, INVALID, nullptr, TargetSpecific, DefaultVis,
       0,
       "Do not add a BTI instruction after a setjmp or other return-twice "
       "construct (AArch64 only)",
       nullptr, nullptr)
OPTION(prefix_1, "-mno-cldemote", mno_cldemote, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-clflushopt", mno_clflushopt, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-clwb", mno_clwb, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-clzero", mno_clzero, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-cmpccxadd", mno_cmpccxadd, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-crc32", mno_crc32, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-cx16", mno_cx16, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-enqcmd", mno_enqcmd, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-evex512", mno_evex512, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-f16c", mno_f16c, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-fix-cortex-a53-835769", mno_fix_cortex_a53_835769, Flag,
       m_aarch64_Features_Group, INVALID, nullptr, TargetSpecific, DefaultVis,
       0, "Don't workaround Cortex-A53 erratum 835769 (AArch64 only)", nullptr,
       nullptr)
OPTION(prefix_1, "-mno-fma4", mno_fma4, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-fma", mno_fma, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-fmv", mno_fmv, Flag, f_neverc_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Disable function multiversioning", nullptr, nullptr)
OPTION(prefix_1, "-mno-fp-ret-in-387", mno_fp_ret_in_387, Flag, INVALID,
       mno_x87, nullptr, TargetSpecific, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-fsgsbase", mno_fsgsbase, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-fxsr", mno_fxsr, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(
    prefix_1, "-mno-gather", mno_gather, Flag, m_Group, INVALID, nullptr,
    TargetSpecific, DefaultVis | DefaultVis, 0,
    "Disable generation of gather instructions in auto-vectorization(x86 only)",
    nullptr, nullptr)
OPTION(prefix_1, "-mno-gfni", mno_gfni, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-global-merge", mno_global_merge, Flag, m_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0, "Disable merging of globals",
       nullptr, nullptr)
OPTION(prefix_1, "-mno-hreset", mno_hreset, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-implicit-float", mno_implicit_float, Flag, m_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Don't generate implicit floating point or vector instructions", nullptr,
       nullptr)
OPTION(prefix_1, "-mno-incremental-linker-compatible",
       mno_incremental_linker_compatible, Flag, m_Group, INVALID, nullptr,
       HelpHidden, DefaultVis | DefaultVis, 0,
       "Emit an object file which cannot be used with an incremental linker",
       nullptr, nullptr)
OPTION(prefix_1, "-mno-invpcid", mno_invpcid, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-kl", mno_kl, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-lvi-cfi", mno_lvi_cfi, Flag, m_Group, INVALID, nullptr,
       0, DefaultVis | DefaultVis, 0,
       "Disable control-flow mitigations for Load Value Injection (LVI)",
       nullptr, nullptr)
OPTION(prefix_1, "-mno-lvi-hardening", mno_lvi_hardening, Flag, m_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Disable mitigations for Load Value Injection (LVI)", nullptr, nullptr)
OPTION(prefix_1, "-mno-lwp", mno_lwp, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-lzcnt", mno_lzcnt, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-mmx", mno_mmx, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-movbe", mno_movbe, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-movdir64b", mno_movdir64b, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-movdiri", mno_movdiri, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-ms-bitfields", mno_ms_bitfields, Flag, m_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "Do not set the default structure layout to be compatible with the "
       "Microsoft compiler standard",
       nullptr, nullptr)
OPTION(prefix_1, "-mno-mwaitx", mno_mwaitx, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-neg-immediates", mno_neg_immediates, Flag,
       m_aarch64_Features_Group, INVALID, nullptr, TargetSpecific, DefaultVis,
       0,
       "Disallow converting instructions with negative immediates to their "
       "negation or inversion.",
       nullptr, nullptr)
OPTION(prefix_1, "-mno-omit-leaf-frame-pointer", mno_omit_leaf_frame_pointer,
       Flag, m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-outline-atomics", mno_outline_atomics, Flag,
       f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Don't generate local calls to out-of-line atomic operations", nullptr,
       nullptr)
OPTION(prefix_1, "-mno-outline", mno_outline, Flag, f_neverc_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Disable function outlining (AArch64 only)",
       nullptr, nullptr)
OPTION(prefix_1, "-mno-pclmul", mno_pclmul, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-pconfig", mno_pconfig, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-pku", mno_pku, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-popcnt", mno_popcnt, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-prefetchi", mno_prefetchi, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-prefetchwt1", mno_prefetchwt1, Flag,
       m_x86_Features_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mno-prfchw", mno_prfchw, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-ptwrite", mno_ptwrite, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-raoint", mno_raoint, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-rdpid", mno_rdpid, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-rdpru", mno_rdpru, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-rdrnd", mno_rdrnd, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-rdseed", mno_rdseed, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-red-zone", mno_red_zone, Flag, m_Group, INVALID, nullptr,
       0, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mno-relax-all", mno_relax_all, Flag, m_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mno-retpoline-external-thunk", mno_retpoline_external_thunk,
       Flag, m_x86_Features_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mno-retpoline", mno_retpoline, Flag, m_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mno-rtd", mno_rtd, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mno-rtm", mno_rtm, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-sahf", mno_sahf, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-scatter", mno_scatter, Flag, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0,
       "Disable generation of scatter instructions in auto-vectorization(x86 "
       "only)",
       nullptr, nullptr)
OPTION(prefix_1, "-mno-serialize", mno_serialize, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-seses", mno_seses, Flag, m_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0,
       "Disable speculative execution side effect suppression (SESES)", nullptr,
       nullptr)
OPTION(prefix_1, "-mno-sgx", mno_sgx, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-sha512", mno_sha512, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-sha", mno_sha, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-shstk", mno_shstk, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-skip-rax-setup", mno_skip_rax_setup, Flag, m_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-sm3", mno_sm3, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-sm4", mno_sm4, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-soft-float", mno_soft_float, Flag, m_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-speculative-load-hardening",
       mno_speculative_load_hardening, Flag, m_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-mno-sse2", mno_sse2, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-sse3", mno_sse3, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-sse4.1", mno_sse4_1, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-sse4.2", mno_sse4_2, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-sse4a", mno_sse4a, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-sse4", mno_sse4, Flag, INVALID, mno_sse4_1, nullptr,
       TargetSpecific, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mno-sse", mno_sse, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-ssse3", mno_ssse3, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-stack-arg-probe", mno_stack_arg_probe, Flag, m_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Disable stack probes which are enabled by default", nullptr, nullptr)
OPTION(prefix_1, "-mno-stackrealign", mno_stackrealign, Flag, m_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-strict-align", mno_strict_align, Flag, INVALID,
       munaligned_access, nullptr, HelpHidden, DefaultVis, 0,
       "Allow memory accesses to be unaligned (same as munaligned-access)",
       nullptr, nullptr)
OPTION(prefix_1, "-mno-tbm", mno_tbm, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-tls-direct-seg-refs", mno_tls_direct_seg_refs, Flag,
       m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Disable direct TLS access through segment registers", nullptr, nullptr)
OPTION(prefix_1, "-mno-tsxldtrk", mno_tsxldtrk, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-uintr", mno_uintr, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-unaligned-access", mno_unaligned_access, Flag, m_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0,
       "Force all memory accesses to be aligned (AArch64 only)", nullptr,
       nullptr)
OPTION(prefix_1, "-mno-usermsr", mno_usermsr, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-vaes", mno_vaes, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-vpclmulqdq", mno_vpclmulqdq, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-vzeroupper", mno_vzeroupper, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-waitpkg", mno_waitpkg, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-wbnoinvd", mno_wbnoinvd, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-widekl", mno_widekl, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-x87", mno_x87, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-xop", mno_xop, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-xsavec", mno_xsavec, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-xsaveopt", mno_xsaveopt, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mno-xsaves", mno_xsaves, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mno-xsave", mno_xsave, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mnoexecstack", mno_exec_stack, Flag, INVALID, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Mark the file as not needing an executable stack", nullptr, nullptr)
OPTION(prefix_1, "-momit-leaf-frame-pointer", momit_leaf_frame_pointer, Flag,
       m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Omit frame pointer setup for leaf functions", nullptr, nullptr)
OPTION(prefix_1, "-moutline-atomics", moutline_atomics, Flag, f_neverc_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Generate local calls to out-of-line atomic operations", nullptr,
       nullptr)
OPTION(prefix_1, "-moutline", moutline, Flag, f_neverc_Group, INVALID, nullptr,
       0, DefaultVis, 0, "Enable function outlining (AArch64 only)", nullptr,
       nullptr)
OPTION(prefix_1, "-mpad-max-prefix-size=", mpad_max_prefix_size_EQ, Joined,
       m_Group, INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0,
       "Specify maximum number of prefixes to use for padding", nullptr,
       nullptr)
OPTION(prefix_1, "-mpclmul", mpclmul, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mpconfig", mpconfig, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mpku", mpku, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mpopcnt", mpopcnt, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mprefer-vector-width=", mprefer_vector_width_EQ, Joined,
       m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Specifies preferred vector width for auto-vectorization. Defaults to "
       "'none' which allows target specific decisions.",
       nullptr, nullptr)
OPTION(prefix_1, "-mprefetchi", mprefetchi, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mprefetchwt1", mprefetchwt1, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mprfchw", mprfchw, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mptwrite", mptwrite, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-MP", MP, Flag, M_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Create phony target for each dependency (other than main file)",
       nullptr, nullptr)
OPTION(prefix_1, "-MQ", MQ, JoinedOrSeparate, M_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Specify name of main file output to quote in depfile",
       nullptr, nullptr)
OPTION(prefix_1, "-mraoint", mraoint, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mrdpid", mrdpid, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mrdpru", mrdpru, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mrdrnd", mrdrnd, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mrdseed", mrdseed, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mreassociate", mreassociate, Flag, INVALID, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "Allow reassociation transformations for floating-point instructions",
       nullptr, nullptr)
OPTION(prefix_1, "-mrecip=", mrecip_EQ, CommaJoined, m_Group, INVALID, nullptr,
       0, DefaultVis | DefaultVis, 0,
       "Control use of approximate reciprocal and reciprocal square root "
       "instructions followed by <n> iterations of Newton-Raphson refinement. "
       "<value> = ( ['!'] ['vec-'] ('rcp'|'sqrt') [('h'|'s'|'d')] [':'<n>] ) | "
       "'all' | 'default' | 'none'",
       nullptr, nullptr)
OPTION(prefix_1, "-mrecip", mrecip, Flag, m_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0, "Equivalent to '-mrecip=all'", nullptr,
       nullptr)
OPTION(prefix_1, "-mred-zone", mred_zone, Flag, m_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mrelax-all", mrelax_all, Flag, m_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0, "Relax all machine instructions", nullptr,
       nullptr)
OPTION(prefix_1, "-mrelax-relocations=no", mrelax_relocations_no, Flag, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Disable x86 relax relocations", nullptr, nullptr)
OPTION(prefix_1, "-mretpoline-external-thunk", mretpoline_external_thunk, Flag,
       m_x86_Features_Group, INVALID, nullptr, TargetSpecific,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mretpoline", mretpoline, Flag, m_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mrtd", mrtd, Flag, m_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0,
       "Make StdCall calling convention the default", nullptr, nullptr)
OPTION(prefix_1, "-mrtm", mrtm, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-msahf", msahf, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-msave-temp-labels", msave_temp_labels, Flag, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Save temporary labels in the symbol table. Note this may change .s "
       "semantics and shouldn't generally be used on compiler-generated code.",
       nullptr, nullptr)
OPTION(prefix_1, "-mserialize", mserialize, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mseses", m_seses, Flag, m_Group, INVALID, nullptr,
       NoXarchOption, DefaultVis | DefaultVis, 0,
       "Enable speculative execution side effect suppression (SESES). Includes "
       "LVI control flow integrity mitigations",
       nullptr, nullptr)
OPTION(prefix_1, "-msgx", msgx, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-msha512", msha512, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-msha", msha, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mshellcode-context=", mshellcode_context_EQ, Joined, m_Group,
       INVALID, nullptr, NoXarchOption, DefaultVis | DefaultVis, 0,
       "Privilege level the emitted shellcode will run at: 'user' (default, "
       "ring-3 payload) or 'kernel' (ring-0 driver / kext / kernel module). "
       "Kernel mode disables PEB walk / syscall stub lowering, injects "
       "target-specific driver flags (e.g. -mno-red-zone / -mcmodel=kernel on "
       "Unix x86_64, /kernel on Windows, -mgeneral-regs-only on AArch64), and "
       "routes OS helper resolution through a loader-provided "
       "__neverc_kern_resolve shim.",
       nullptr, nullptr)
OPTION(prefix_1, "-mshellcode-libsystem", mshellcode_libsystem, Flag, m_Group,
       INVALID, nullptr, NoXarchOption, DefaultVis | DefaultVis, 0,
       "Legacy Darwin-centric alias of -mshellcode-syscall; kept for backwards "
       "compatibility",
       nullptr, nullptr)
OPTION(prefix_1, "-mshellcode-syscall", mshellcode_syscall, Flag, m_Group,
       INVALID, nullptr, NoXarchOption, DefaultVis | DefaultVis, 0,
       "Replace libc/libSystem externs (write, exit, read, ...) with inline "
       "syscall wrappers native to the target OS (svc #0x80 on Darwin, svc #0 "
       "on Linux/Android arm64, syscall on Linux x86_64)",
       nullptr, nullptr)
OPTION(prefix_1, "-mshellcode-win-peb-import", mshellcode_win_peb_import, Flag,
       m_Group, INVALID, nullptr, NoXarchOption, DefaultVis | DefaultVis, 0,
       "Windows shellcode only: resolve extern Win32 imports at runtime via a "
       "PEB walk + GetProcAddress thunk instead of relying on the loader's "
       "import table",
       nullptr, nullptr)
OPTION(prefix_1, "-mshstk", mshstk, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-msign-return-address-key=", msign_return_address_key_EQ,
       Joined, INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0, nullptr,
       nullptr, "a_key,b_key")
OPTION(prefix_1, "-msign-return-address=", msign_return_address_EQ, Joined,
       m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Select return address signing scope", nullptr, "none,all,non-leaf")
OPTION(
    prefix_1, "-mskip-rax-setup", mskip_rax_setup, Flag, m_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0,
    "Skip setting up RAX register when passing variable arguments (x86 only)",
    nullptr, nullptr)
OPTION(prefix_1, "-msm3", msm3, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-msm4", msm4, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-msoft-float", msoft_float, Flag, m_Group, INVALID, nullptr,
       0, DefaultVis | DefaultVis, 0, "Use software floating point", nullptr,
       nullptr)
OPTION(prefix_1, "-mspeculative-load-hardening", mspeculative_load_hardening,
       Flag, m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis | DefaultVis,
       0, "", nullptr, nullptr)
OPTION(prefix_1, "-msse2", msse2, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-msse3", msse3, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-msse4.1", msse4_1, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-msse4.2", msse4_2, Flag, m_x86_Features_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-msse4a", msse4a, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-msse4", msse4, Flag, INVALID, msse4_2, nullptr,
       TargetSpecific, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-msse", msse, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mssse3", mssse3, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mstack-alignment=", mstack_alignment, Joined, m_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Set the stack alignment", nullptr, nullptr)
OPTION(prefix_1, "-mstack-arg-probe", mstack_arg_probe, Flag, m_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0, "Enable stack probes", nullptr,
       nullptr)
OPTION(prefix_1, "-mstack-probe-size=", mstack_probe_size, Joined, m_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Set the stack probe size", nullptr, nullptr)
OPTION(prefix_1,
       "-mstack-protector-guard-offset=", mstack_protector_guard_offset_EQ,
       Joined, m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Use the given offset for addressing the stack-protector guard", nullptr,
       nullptr)
OPTION(prefix_1, "-mstack-protector-guard-reg=", mstack_protector_guard_reg_EQ,
       Joined, m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Use the given reg for addressing the stack-protector guard", nullptr,
       nullptr)
OPTION(prefix_1,
       "-mstack-protector-guard-symbol=", mstack_protector_guard_symbol_EQ,
       Joined, m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Use the given symbol for addressing the stack-protector guard", nullptr,
       nullptr)
OPTION(prefix_1, "-mstack-protector-guard=", mstack_protector_guard_EQ, Joined,
       m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Use the given guard (global, tls) for addressing the stack-protector "
       "guard",
       nullptr, nullptr)
OPTION(prefix_1, "-mstackrealign", mstackrealign, Flag, m_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "Force realign the stack at entry to every function", nullptr, nullptr)
OPTION(prefix_1, "-mstrict-align", mstrict_align, Flag, INVALID,
       mno_unaligned_access, nullptr, HelpHidden, DefaultVis, 0,
       "Force all memory accesses to be aligned (same as mno-unaligned-access)",
       nullptr, nullptr)
OPTION(prefix_1, "-msvc-arch=", msvc_arch, Joined, msvc_Group, INVALID, nullptr,
       HelpHidden, DefaultVis, 0, "Set MSVC architecture for code generation",
       nullptr, nullptr)
OPTION(prefix_1, "-msvc-asm-listing", msvc_asm_listing, Joined, msvc_Group,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Output assembly code file during compilation", nullptr, nullptr)
OPTION(prefix_1, "-msvc-asm-output=", msvc_asm_output, Joined, msvc_Group,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Set assembly output file name", "<file or dir/>", nullptr)
OPTION(prefix_1, "-msvc-exe-output=", msvc_exe_output, Joined, msvc_Group,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Set output executable file name", "<file or dir/>", nullptr)
OPTION(prefix_1, "-msvc-obj-output=", msvc_obj_output, Joined, msvc_Group,
       INVALID, nullptr, HelpHidden, DefaultVis, 0, "Set output object file",
       "<file or dir/>", nullptr)
OPTION(prefix_1, "-msvc-optimize=", msvc_optimize, Joined, msvc_Group, INVALID,
       nullptr, HelpHidden, DefaultVis, 0, "Set MSVC optimization flags",
       "<flags>", nullptr)
OPTION(prefix_1, "-msvc-pp-output=", msvc_pp_output, Joined, msvc_Group,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Set preprocess output file name", "<file>", nullptr)
OPTION(prefix_1, "-msvc-preprocess-no-linemarkers",
       msvc_preprocess_no_linemarkers, Flag, msvc_Group, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "Disable linemarker output and preprocess to stdout", nullptr, nullptr)
OPTION(prefix_1, "-msvc-preprocess-to-file", msvc_preprocess_to_file, Flag,
       msvc_Group, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Preprocess to file", nullptr, nullptr)
OPTION(prefix_1, "-msvc-runtime-mdd", msvc_runtime_MDd, Flag,
       msvc_runtime_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Use MSVC DLL debug runtime (msvcrtd)", nullptr, nullptr)
OPTION(prefix_1, "-msvc-runtime-md", msvc_runtime_MD, Flag, msvc_runtime_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "Use MSVC DLL runtime (msvcrt)",
       nullptr, nullptr)
OPTION(prefix_1, "-msvc-runtime-mtd", msvc_runtime_MTd, Flag,
       msvc_runtime_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Use MSVC static debug runtime (libcmtd)", nullptr, nullptr)
OPTION(prefix_1, "-msvc-runtime-mt", msvc_runtime_MT, Flag, msvc_runtime_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "Use MSVC static runtime (libcmt)",
       nullptr, nullptr)
OPTION(prefix_1, "-msvc-std=", msvc_std, Joined, msvc_Group, INVALID, nullptr,
       HelpHidden, DefaultVis, 0, "Set MSVC language version", nullptr, nullptr)
OPTION(prefix_1, "-msvc-wd", msvc_wd, Joined, msvc_Group, INVALID, nullptr,
       HelpHidden, DefaultVis, 0, "Disable MSVC warning by number", nullptr,
       nullptr)
OPTION(prefix_1, "-msve-vector-bits=", msve_vector_bits_EQ, Joined,
       m_aarch64_Features_Group, INVALID, nullptr, TargetSpecific, DefaultVis,
       0,
       "Specify the size in bits of an SVE vector register. Defaults to the "
       "vector length agnostic value of \"scalable\". (AArch64 only)",
       nullptr, nullptr)
OPTION(prefix_1, "-mtargetos=", mtargetos_EQ, Joined, m_Group, INVALID, nullptr,
       0, DefaultVis | DefaultVis, 0,
       "Set the deployment target to be the specified OS and OS version",
       nullptr, nullptr)
OPTION(prefix_1, "-mtbm", mtbm, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mthread-model", mthread_model, Separate, m_Group, INVALID,
       nullptr, 0, DefaultVis | DefaultVis, 0,
       "The thread model to use. Defaults to 'posix')", nullptr, "posix,single")
OPTION(prefix_1, "-mtls-direct-seg-refs", mtls_direct_seg_refs, Flag, m_Group,
       INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
       "Enable direct TLS access through segment registers (default)", nullptr,
       nullptr)
OPTION(prefix_1, "-mtls-size=", mtls_size_EQ, Joined, m_Group, INVALID, nullptr,
       0, DefaultVis | DefaultVis, 0,
       "Specify bit size of immediate TLS offsets (AArch64 ELF only): 12 (for "
       "4KB) | 24 (for 16MB, default) | 32 (for 4GB) | 48 (for 256TB, needs "
       "-mcmodel=large)",
       nullptr, nullptr)
OPTION(prefix_1, "-mtp=", mtp_mode_EQ, Joined, m_aarch64_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis, 0,
       "Thread pointer access method (AArch64 only).", nullptr,
       "el0,el1,el2,el3,tpidr_el0,tpidr_el1,tpidr_el2,tpidr_el3,tpidrro_el0")
OPTION(prefix_1, "-mtp", mtp, Separate, INVALID, INVALID, nullptr, HelpHidden,
       DefaultVis, 0, "Mode for reading thread pointer", nullptr, nullptr)
OPTION(prefix_1, "-mtsxldtrk", mtsxldtrk, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mtune=help", anonymous_663, Flag, INVALID,
       print_supported_cpus, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mtune=", mtune_EQ, Joined, m_Group, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0, "Only supported on AArch64 and X86", nullptr,
       nullptr)
OPTION(prefix_1, "-MT", MT, JoinedOrSeparate, M_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Specify name of main file output in depfile", nullptr,
       nullptr)
OPTION(prefix_1, "-muintr", muintr, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-multi_module", multi__module, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-multiply_defined_unused", multiply__defined__unused,
       Separate, INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-multiply_defined", multiply__defined, Separate, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-munaligned-access", munaligned_access, Flag, m_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0,
       "Allow memory accesses to be unaligned (AArch64 only)", nullptr, nullptr)
OPTION(prefix_1, "-municode", municode, Joined, m_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-musermsr", musermsr, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mvaes", mvaes, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mvpclmulqdq", mvpclmulqdq, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-mvscale-max=", mvscale_max_EQ, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Specify the vscale maximum. Defaults to the vector length agnostic "
       "value of \"0\". (AArch64 only)",
       nullptr, nullptr)
OPTION(prefix_1, "-mvscale-min=", mvscale_min_EQ, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Specify the vscale minimum. Defaults to \"1\". (AArch64 only)", nullptr,
       nullptr)
OPTION(prefix_1, "-mvzeroupper", mvzeroupper, Flag, m_x86_Features_Group,
       INVALID, nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr,
       nullptr, nullptr)
OPTION(prefix_1, "-MV", MV, Flag, M_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Use NMake/Jom format for the depfile", nullptr, nullptr)
OPTION(prefix_1, "-mwaitpkg", mwaitpkg, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mwbnoinvd", mwbnoinvd, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mwidekl", mwidekl, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mx87", mx87, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mxop", mxop, Flag, m_x86_Features_Group, INVALID, nullptr,
       TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-mxsavec", mxsavec, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mxsaveopt", mxsaveopt, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mxsaves", mxsaves, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-mxsave", mxsave, Flag, m_x86_Features_Group, INVALID,
       nullptr, TargetSpecific, DefaultVis | DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-M", M, Flag, M_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Like -MD, but also implies -E and writes to stdout by default", nullptr,
       nullptr)
OPTION(prefix_1, "-new-struct-path-tbaa", new_struct_path_tbaa, Flag, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Enable enhanced struct-path aware Type Based Alias Analysis", nullptr,
       nullptr)
OPTION(prefix_1, "-no-canonical-prefixes", no_canonical_prefixes, Flag, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Use relative paths for invoking subcommands", nullptr, nullptr)
OPTION(prefix_1, "-no-clear-ast-before-backend", no_clear_ast_before_backend,
       Flag, INVALID, INVALID, nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
       "Don't clear the NeverC AST before running backend code generation",
       nullptr, nullptr)
OPTION(prefix_3, "--no-default-config", no_default_config, Flag, INVALID,
       INVALID, nullptr, NoXarchOption, DefaultVis, 0,
       "Disable loading default configuration files", nullptr, nullptr)
OPTION(prefix_1, "-no-emit-llvm-uselists", no_emit_llvm_uselists, Flag, INVALID,
       INVALID, nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
       "Don't preserve order of LLVM use-lists when serializing", nullptr,
       nullptr)
OPTION(prefix_1, "-no-enable-noundef-analysis", no_enable_noundef_analysis,
       Flag, INVALID, INVALID, nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
       "Disable analyzing function argument and return types for mandatory "
       "definedness",
       nullptr, nullptr)
OPTION(prefix_1, "-no-implicit-float", no_implicit_float, Flag, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Don't generate implicit floating point or vector instructions", nullptr,
       nullptr)
OPTION(prefix_1, "-no-integrated-as", anonymous_679, Flag, INVALID,
       fno_integrated_as, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "-no-integrated-cpp", no_integrated_cpp, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "-no-pedantic", no_pedantic, Flag, pedantic_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-no-pie", no_pie, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-no-pthread", no_pthread, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "-no-struct-path-tbaa", no_struct_path_tbaa, Flag, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Turn off struct-path aware Type Based Alias Analysis", nullptr, nullptr)
OPTION(prefix_3, "--no-system-header-prefix=", no_system_header_prefix, Joined,
       neverc_i_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Treat all #include paths starting with <prefix> as not including a "
       "system header.",
       "<prefix>", nullptr)
OPTION(prefix_3, "--no-system-header-prefix", anonymous_660, Separate, INVALID,
       no_system_header_prefix, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_3, "--no-undefined", _no_undefined, Flag, INVALID, INVALID,
       nullptr, LinkerInput, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-no_dead_strip_inits_and_terms",
       no__dead__strip__inits__and__terms, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-nobuiltininc", nobuiltininc, Flag, IncludePath_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Disable builtin #include directories", nullptr, nullptr)
OPTION(prefix_1, "-nodefaultlibs", nodefaultlibs, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-nofixprebinding", nofixprebinding, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-nolibc", nolibc, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-nomultidefs", nomultidefs, Flag, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-noprebind", noprebind, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-noseglinkedit", noseglinkedit, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-nostartfiles", nostartfiles, Flag, Link_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-nostdinc", nostdinc, Flag, IncludePath_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-nostdlibinc", nostdlibinc, Flag, IncludePath_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-nostdlib", nostdlib, Flag, Link_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-nostdsysteminc", nostdsysteminc, Flag, INVALID, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Disable standard system #include directories", nullptr, nullptr)
OPTION(prefix_1, "-n", n, Flag, INVALID, INVALID, nullptr, HelpHidden,
       DefaultVis, 0,
       "Don't automatically start assembly file with a text section", nullptr,
       nullptr)
OPTION(prefix_1, "-O0", O0, Flag, O_Group, INVALID, nullptr, HelpHidden,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-O4", O4, Flag, O_Group, INVALID, nullptr, HelpHidden,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-object-file-name=", object_file_name_EQ, Joined, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Set the output <file> for debug infos", "<file>", nullptr)
OPTION(prefix_1, "-object-file-name", object_file_name, Separate, INVALID,
       object_file_name_EQ, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-object", object, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-Ofast", Ofast, Joined, O_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-opt-record-file", opt_record_file, Separate, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "File name to use for YAML optimization record output", nullptr, nullptr)
OPTION(prefix_1, "-opt-record-format", opt_record_format, Separate, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "The format used for serializing remarks (default: YAML)", nullptr,
       nullptr)
OPTION(prefix_1, "-opt-record-passes", opt_record_passes, Separate, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Only record remark information for passes whose names match the given "
       "regular expression",
       nullptr, nullptr)
OPTION(prefix_1, "-O", O_flag, Flag, INVALID, O, "1\0", 0, DefaultVis, 0,
       nullptr, nullptr, nullptr)
OPTION(prefix_1, "-O", O, Joined, O_Group, INVALID, nullptr, 0, DefaultVis, 0,
       nullptr, nullptr, nullptr)
OPTION(prefix_1, "-o", o, JoinedOrSeparate, INVALID, INVALID, nullptr,
       NoXarchOption, DefaultVis, 0, "Write output to <file>", "<file>",
       nullptr)
OPTION(prefix_1, "-pagezero_size", pagezero__size, JoinedOrSeparate, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_3, "--param=", _param_EQ, Joined, INVALID, _param, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_3, "--param", _param, Separate, CompileOnly_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "-pedantic-errors", pedantic_errors, Flag, pedantic_Group,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "-pedantic", pedantic, Flag, pedantic_Group, INVALID, nullptr,
       0, DefaultVis, 0, "Warn on language extensions", nullptr, nullptr)
OPTION(prefix_1, "-pie", pie, Flag, Link_Group, INVALID, nullptr, 0, DefaultVis,
       0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "-pipe", pipe, Flag, INVALID, INVALID, nullptr, HelpHidden,
       DefaultVis, 0,
       "Accepted for compatibility (no-op: NeverC compiles in-process)",
       nullptr, nullptr)
OPTION(prefix_1, "-prebind_all_twolevel_modules",
       prebind__all__twolevel__modules, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-prebind", prebind, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-preload", preload, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_3, "--print-diagnostic-categories", _print_diagnostic_categories,
       Flag, INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_2, "-print-diagnostic-options", print_diagnostic_options, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Print all of NeverC's warning options", nullptr, nullptr)
OPTION(prefix_2, "-print-effective-triple", print_effective_triple, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Print the effective target triple", nullptr, nullptr)
OPTION(prefix_2, "-print-file-name=", print_file_name_EQ, Joined, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Print the full library path of <file>", "<file>", nullptr)
OPTION(prefix_2, "-print-libgcc-file-name", print_libgcc_file_name, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Print the library path for the currently used compiler runtime library "
       "(\"libgcc.a\" or \"libneverc_rt.builtins.*.a\")",
       nullptr, nullptr)
OPTION(prefix_2, "-print-multi-directory", print_multi_directory, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "-print-multi-flags-experimental", print_multi_flags, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Print the flags used for selecting multilibs (experimental)", nullptr,
       nullptr)
OPTION(prefix_2, "-print-multi-lib", print_multi_lib, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "-print-prog-name=", print_prog_name_EQ, Joined, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Print the full program path of <name>", "<name>", nullptr)
OPTION(prefix_2, "-print-resource-dir", print_resource_dir, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Print the resource directory pathname", nullptr, nullptr)
OPTION(prefix_2, "-print-runtime-dir", print_runtime_dir, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Print the directory pathname containing the compiler runtime libraries",
       nullptr, nullptr)
OPTION(prefix_2, "-print-search-dirs", print_search_dirs, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Print the paths used for finding libraries and programs", nullptr,
       nullptr)
OPTION(prefix_1, "-print-stats", print_stats, Flag, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, "Print performance metrics and statistics", nullptr,
       nullptr)
OPTION(prefix_2, "-print-supported-cpus", print_supported_cpus, Flag,
       CompileOnly_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Print supported cpu models for the given target (if target is not "
       "specified, it will print the supported cpus for the default target)",
       nullptr, nullptr)
OPTION(prefix_2, "-print-target-triple", print_target_triple, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, "Print the normalized target triple",
       nullptr, nullptr)
OPTION(prefix_2, "-print-targets", print_targets, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Print the registered targets", nullptr,
       nullptr)
OPTION(prefix_1, "-private_bundle", private__bundle, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-pthreads", pthreads, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-pthread", pthread, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis | DefaultVis, 0, "Support POSIX threads in generated code",
       nullptr, nullptr)
OPTION(prefix_1, "-P", P, Flag, Preprocessor_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Disable linemarker output in -E mode", nullptr, nullptr)
OPTION(prefix_1, "-Qn", Qn, Flag, INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not emit metadata containing compiler name and version", nullptr,
       nullptr)
OPTION(prefix_1, "-Qunused-arguments", Qunused_arguments, Flag, INVALID,
       INVALID, nullptr, NoXarchOption, DefaultVis, 0,
       "Don't emit warning for unused driver arguments", nullptr, nullptr)
OPTION(prefix_1, "-Qy", Qy, Flag, INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Emit metadata containing compiler name and version", nullptr, nullptr)
OPTION(prefix_1, "-rdynamic", rdynamic, Flag, Link_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-read_only_relocs", read__only__relocs, Separate, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-record-command-line", record_command_line, Separate, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "The string to embed in the .LLVM.command.line section.", nullptr,
       nullptr)
OPTION(prefix_1, "-relaxed-aliasing", relaxed_aliasing, Flag, INVALID, INVALID,
       nullptr, HelpHidden, DefaultVis, 0, "Turn off Type Based Alias Analysis",
       nullptr, nullptr)
OPTION(prefix_1, "-resource-dir=", resource_dir_EQ, Joined, INVALID,
       resource_dir, nullptr, NoXarchOption, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-resource-dir", resource_dir, Separate, INVALID, INVALID,
       nullptr, NoXarchOption | HelpHidden, DefaultVis, 0,
       "The directory which holds the compiler resource files", nullptr,
       nullptr)
OPTION(prefix_1, "-Rpass-analysis=", Rpass_analysis_EQ, Joined, R_value_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Report transformation analysis from optimization passes whose name "
       "matches the given POSIX regular expression",
       nullptr, nullptr)
OPTION(prefix_1, "-Rpass-missed=", Rpass_missed_EQ, Joined, R_value_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Report missed transformations by optimization passes whose name "
       "matches the given POSIX regular expression",
       nullptr, nullptr)
OPTION(prefix_1, "-Rpass=", Rpass_EQ, Joined, R_value_Group, INVALID, nullptr,
       0, DefaultVis, 0,
       "Report transformations performed by optimization passes whose name "
       "matches the given POSIX regular expression",
       nullptr, nullptr)
OPTION(prefix_1, "-rpath", rpath, Separate, Link_Group, INVALID, nullptr,
       LinkerInput, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_3, "--rsp-quoting=", rsp_quoting, Joined, internal_driver_Group,
       INVALID, nullptr, NoXarchOption | HelpHidden, DefaultVis, 0,
       "Set the rsp quoting to either 'posix', or 'windows'", nullptr, nullptr)
OPTION(prefix_2, "-rtlib=", rtlib_EQ, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Compiler runtime library to use", nullptr, nullptr)
OPTION(prefix_1, "-R", R_Joined, Joined, R_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Enable the specified remark", "<remark>", nullptr)
OPTION(prefix_1, "-r", r, Flag, Link_Group, INVALID, nullptr, NoArgumentUnused,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "-save-stats=", save_stats_EQ, Joined, INVALID, INVALID,
       nullptr, NoXarchOption, DefaultVis, 0, "Save llvm statistics.", nullptr,
       nullptr)
OPTION(prefix_2, "-save-stats", save_stats, Flag, INVALID, save_stats_EQ,
       "cwd\0", NoXarchOption, DefaultVis, 0, "Save llvm statistics.", nullptr,
       nullptr)
OPTION(prefix_2, "-save-temps=", save_temps_EQ, Joined, INVALID, INVALID,
       nullptr, NoXarchOption, DefaultVis, 0,
       "Save intermediate compilation results.", nullptr, nullptr)
OPTION(prefix_2, "-save-temps", save_temps, Flag, INVALID, save_temps_EQ,
       "cwd\0", NoXarchOption, DefaultVis, 0,
       "Save intermediate compilation results", nullptr, nullptr)
OPTION(prefix_1, "-sectalign", sectalign, MultiArg, INVALID, INVALID, nullptr,
       0, DefaultVis, 3, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-sectcreate", sectcreate, MultiArg, INVALID, INVALID, nullptr,
       0, DefaultVis, 3, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-sectobjectsymbols", sectobjectsymbols, MultiArg, INVALID,
       INVALID, nullptr, 0, DefaultVis, 2, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-sectorder", sectorder, MultiArg, INVALID, INVALID, nullptr,
       0, DefaultVis, 3, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-seg1addr", seg1addr, JoinedOrSeparate, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-seg_addr_table_filename", seg__addr__table__filename,
       Separate, INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-seg_addr_table", seg__addr__table, Separate, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-segaddr", segaddr, MultiArg, INVALID, INVALID, nullptr, 0,
       DefaultVis, 2, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-segcreate", segcreate, MultiArg, INVALID, INVALID, nullptr,
       0, DefaultVis, 3, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-seglinkedit", seglinkedit, Flag, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-segprot", segprot, MultiArg, INVALID, INVALID, nullptr, 0,
       DefaultVis, 3, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-segs_read_only_addr", segs__read__only__addr, Separate,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-segs_read_write_addr", segs__read__write__addr, Separate,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-segs_read_", segs__read__, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-shared-libgcc", shared_libgcc, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "-shared", shared, Flag, Link_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_3, "--show-includes", show_includes, Flag, INVALID, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Print cl.exe style /showIncludes to stdout", nullptr, nullptr)
OPTION(prefix_1, "-single_module", single__module, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-source-date-epoch", source_date_epoch, Separate, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Time to be used in __DATE__, __TIME__, and __TIMESTAMP__ macros",
       "<time since Epoch in seconds>", nullptr)
OPTION(prefix_1, "-split-dwarf-file", split_dwarf_file, Separate, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Name of the split dwarf debug info file to encode in the object file",
       nullptr, nullptr)
OPTION(prefix_1, "-split-dwarf-output", split_dwarf_output, Separate, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "File name to use for split dwarf debug info output", nullptr, nullptr)
OPTION(prefix_1, "-stack-protector-buffer-size", stack_protector_buffer_size,
       Separate, INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Lower bound for a buffer to be considered for stack protection",
       nullptr, nullptr)
OPTION(prefix_1, "-stack-protector", stack_protector, Separate, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0, "Enable stack protectors",
       nullptr, "0,1,2,3")
OPTION(prefix_1, "-stack-usage-file", stack_usage_file, Separate, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Filename (or -) to write stack usage output to", nullptr, nullptr)
OPTION(prefix_3, "--start-no-unused-arguments", start_no_unused_arguments, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Don't emit warnings about unused arguments for the following arguments",
       nullptr, nullptr)
OPTION(prefix_1, "-static-define", static_define, Flag, INVALID, INVALID,
       nullptr, HelpHidden, DefaultVis, 0, "Should __STATIC__ be defined",
       nullptr, nullptr)
OPTION(prefix_1, "-static-libgcc", static_libgcc, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-static-pie", static_pie, Flag, Link_Group, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "-static", static, Flag, Link_Group, INVALID, nullptr,
       NoArgumentUnused, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-stats-file-append", stats_file_append, Flag, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "If stats should be appended to stats-file instead of overwriting it",
       nullptr, nullptr)
OPTION(prefix_1, "-stats-file=", stats_file, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, "Filename to write statistics to", nullptr, nullptr)
OPTION(prefix_1, "-std-default=", std_default_EQ, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "-std=", std_EQ, Joined, CompileOnly_Group, INVALID, nullptr,
       0, DefaultVis, 0, "Language standard to compile for", nullptr,
       std_EQ_Values)
OPTION(prefix_1, "-sub_library", sub__library, JoinedOrSeparate, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-sub_umbrella", sub__umbrella, JoinedOrSeparate, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-sys-header-deps", sys_header_deps, Flag, INVALID, INVALID,
       nullptr, HelpHidden, DefaultVis, 0,
       "Include system headers in dependency output", nullptr, nullptr)
OPTION(prefix_3, "--sysroot=", _sysroot_EQ, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_3, "--sysroot", _sysroot, Separate, INVALID, _sysroot_EQ, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_3, "--system-header-prefix=", system_header_prefix, Joined,
       neverc_i_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "Treat all #include paths starting with <prefix> as including a system "
       "header.",
       "<prefix>", nullptr)
OPTION(prefix_3, "--system-header-prefix", anonymous_659, Separate, INVALID,
       system_header_prefix, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-S", S, Flag, Action_Group, INVALID, nullptr, NoXarchOption,
       DefaultVis, 0, "Only run preprocess and compilation steps", nullptr,
       nullptr)
OPTION(prefix_1, "-s", s, Flag, Link_Group, INVALID, nullptr, 0, DefaultVis, 0,
       nullptr, nullptr, nullptr)
OPTION(prefix_1, "-target-abi", target_abi, Separate, INVALID, INVALID, nullptr,
       HelpHidden, DefaultVis, 0, "Target a particular ABI type", nullptr,
       nullptr)
OPTION(prefix_1, "-target-cpu", target_cpu, Separate, INVALID, INVALID, nullptr,
       HelpHidden, DefaultVis, 0, "Target a specific cpu type", nullptr,
       nullptr)
OPTION(prefix_1, "-target-feature", target_feature, Separate, INVALID, INVALID,
       nullptr, HelpHidden, DefaultVis, 0, "Target specific attributes",
       nullptr, nullptr)
OPTION(prefix_3, "--target-help", _target_help, Flag, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-target-linker-version", target_linker_version, Separate,
       INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Target linker version", nullptr, nullptr)
OPTION(prefix_1, "-target-sdk-version=", target_sdk_version_EQ, Joined, INVALID,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "The version of target SDK used for compilation", nullptr, nullptr)
OPTION(prefix_3, "--target=", target, Joined, INVALID, INVALID, nullptr,
       NoXarchOption, DefaultVis, 0, "Generate code for the given target",
       nullptr, nullptr)
OPTION(prefix_1, "-target", target_legacy_spelling, Separate, INVALID, target,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-time", time, Flag, INVALID, INVALID, nullptr, 0, DefaultVis,
       0, "Time individual commands", nullptr, nullptr)
OPTION(prefix_2, "-traditional-cpp", traditional_cpp, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Enable some traditional CPP emulation",
       nullptr, nullptr)
OPTION(prefix_2, "-traditional", traditional, Flag, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "-trigraphs", trigraphs, Flag, INVALID, ftrigraphs, nullptr, 0,
       DefaultVis, 0, "Process trigraph sequences", nullptr, nullptr)
OPTION(prefix_1, "-triple=", triple_EQ, Joined, INVALID, triple, nullptr,
       HelpHidden, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-triple", triple, Separate, INVALID, INVALID, nullptr,
       HelpHidden, DefaultVis, 0,
       "Specify target triple (e.g. x86_64-apple-darwin)", nullptr, nullptr)
OPTION(prefix_1, "-tune-cpu", tune_cpu, Separate, INVALID, INVALID, nullptr,
       HelpHidden, DefaultVis, 0, "Tune for a specific cpu type", nullptr,
       nullptr)
OPTION(prefix_1, "-twolevel_namespace_hints", twolevel__namespace__hints, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-twolevel_namespace", twolevel__namespace, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-T", T, JoinedOrSeparate, T_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Specify <script> as linker script", "<script>", nullptr)
OPTION(prefix_1, "-t", t, Flag, Link_Group, INVALID, nullptr, 0, DefaultVis, 0,
       nullptr, nullptr, nullptr)
OPTION(prefix_1, "-umbrella", umbrella, Separate, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-undefined", undefined, JoinedOrSeparate, u_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-undef", undef, Flag, u_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "undef all system defines", nullptr, nullptr)
OPTION(prefix_1, "-unexported_symbols_list", unexported__symbols__list,
       Separate, INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_2, "-unwindlib=", unwindlib_EQ, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, "Unwind library to use", nullptr,
       "libgcc,unwindlib,platform")
OPTION(prefix_1, "-U", U, JoinedOrSeparate, Preprocessor_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Undefine macro <macro>", "<macro>", nullptr)
OPTION(prefix_1, "-u", u, JoinedOrSeparate, u_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "-vctoolsdir", vctoolsdir, JoinedOrSeparate, msvc_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "Path to the VCToolChain", "<dir>",
       nullptr)
OPTION(prefix_2, "-vctoolsversion", vctoolsversion, JoinedOrSeparate,
       msvc_Group, INVALID, nullptr, 0, DefaultVis, 0,
       "MSVC toolchain version, defaults to newest found", nullptr, nullptr)
OPTION(prefix_1, "-vectorize-loops", vectorize_loops, Flag, INVALID, INVALID,
       nullptr, HelpHidden, DefaultVis, 0, "Run the Loop vectorization passes",
       nullptr, nullptr)
OPTION(prefix_1, "-vectorize-slp", vectorize_slp, Flag, INVALID, INVALID,
       nullptr, HelpHidden, DefaultVis, 0, "Run the SLP vectorization passes",
       nullptr, nullptr)
OPTION(prefix_3, "--version", _version, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Print version information", nullptr, nullptr)
OPTION(prefix_1, "-version", version, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Print the compiler version", nullptr, nullptr)
OPTION(prefix_2, "-vfsoverlay", vfsoverlay, JoinedOrSeparate, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Overlay the virtual filesystem described by file over the real file "
       "system. Additionally, pass this overlay file to the linker if it "
       "supports it",
       nullptr, nullptr)
OPTION(prefix_2, "-via-file-asm", via_file_asm, Flag, internal_debug_Group,
       INVALID, nullptr, NoXarchOption | HelpHidden, DefaultVis, 0,
       "Write assembly to file for input to assemble jobs", nullptr, nullptr)
OPTION(prefix_1, "-v", v, Flag, INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Show commands to run and use verbose output", nullptr, nullptr)
OPTION(prefix_1, "-Wa,", Wa_COMMA, CommaJoined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Pass the comma separated arguments in <arg> to the assembler", "<arg>",
       nullptr)
OPTION(prefix_1, "-Wall", Wall, Flag, W_Group, INVALID, nullptr, HelpHidden,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-Wdeprecated", Wdeprecated, Flag, W_Group, INVALID, nullptr,
       0, DefaultVis, 0,
       "Enable warnings for deprecated constructs and define __DEPRECATED",
       nullptr, nullptr)
OPTION(prefix_1, "-weak-l", weak_l, Joined, INVALID, INVALID, nullptr,
       LinkerInput, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-weak_framework", weak__framework, Separate, INVALID, INVALID,
       nullptr, LinkerInput, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-weak_library", weak__library, Separate, INVALID, INVALID,
       nullptr, LinkerInput, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-weak_reference_mismatches", weak__reference__mismatches,
       Separate, INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-Wframe-larger-than=", Wframe_larger_than_EQ, Joined,
       W_value_Group, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-Wframe-larger-than", Wframe_larger_than, Flag, INVALID,
       Wframe_larger_than_EQ, nullptr, 0, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-whatsloaded", whatsloaded, Flag, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-why_load", why_load, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-whyload", whyload, Flag, INVALID, why_load, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_2, "-winsdkdir", winsdkdir, JoinedOrSeparate, msvc_Group, INVALID,
       nullptr, 0, DefaultVis, 0, "Path to the Windows SDK", "<dir>", nullptr)
OPTION(prefix_2, "-winsdkversion", winsdkversion, JoinedOrSeparate, msvc_Group,
       INVALID, nullptr, 0, DefaultVis, 0, "Full version of the Windows SDK",
       nullptr, nullptr)
OPTION(prefix_2, "-winsysroot", winsysroot, JoinedOrSeparate, msvc_Group,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Root of the Windows SDK and MSVC toolchain", "<dir>", nullptr)
OPTION(prefix_1, "-Wl,", Wl_COMMA, CommaJoined, Link_Group, INVALID, nullptr,
       LinkerInput | RenderAsInput, DefaultVis, 0,
       "Pass the comma separated arguments in <arg> to the linker", "<arg>",
       nullptr)
OPTION(prefix_1, "-Wlarge-by-value-copy=", Wlarge_by_value_copy_EQ, Joined,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-Wlarge-by-value-copy", Wlarge_by_value_copy_def, Flag,
       INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Warn if a function definition returns or accepts an object larger in "
       "bytes than a given value",
       nullptr, nullptr)
OPTION(prefix_1, "-Wno-deprecated", Wno_deprecated, Flag, W_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-Wno-system-headers", Wno_system_headers, Flag, W_Group,
       INVALID, nullptr, HelpHidden, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-Wno-write-strings", Wno_write_strings, Flag, W_Group,
       INVALID, nullptr, HelpHidden, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-working-directory=", working_directory_EQ, Joined, INVALID,
       working_directory, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-working-directory", working_directory, Separate, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Resolve file paths relative to the specified directory", nullptr,
       nullptr)
OPTION(prefix_1, "-Wp,", Wp_COMMA, CommaJoined, Preprocessor_Group, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Pass the comma separated arguments in <arg> to the preprocessor",
       "<arg>", nullptr)
OPTION(prefix_1, "-Wsystem-headers", Wsystem_headers, Flag, W_Group, INVALID,
       nullptr, HelpHidden, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-Wundef-prefix=", Wundef_prefix_EQ, CommaJoined,
       W_value_Group, INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Enable warnings for undefined macros with a prefix in the comma "
       "separated list <arg>",
       "<arg>", nullptr)
OPTION(prefix_1, "-Wunused-function", funused_function, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-Wunused-value", funused_value, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-Wunused-variable", funused_variable, Flag, f_Group, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-Wwrite-strings", Wwrite_strings, Flag, W_Group, INVALID,
       nullptr, HelpHidden, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-W", W_Joined, Joined, W_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Enable the specified warning", "<warning>", nullptr)
OPTION(prefix_1, "-w", w, Flag, INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Suppress all warnings", nullptr, nullptr)
OPTION(prefix_1, "-Xarch_host", Xarch_host, Separate, INVALID, INVALID, nullptr,
       NoXarchOption | HelpHidden, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-Xarch_", Xarch__, JoinedAndSeparate, INVALID, INVALID,
       nullptr, NoXarchOption, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-Xassembler", Xassembler, Separate, CompileOnly_Group,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Pass <arg> to the integrated assembler (NeverC always uses the "
       "integrated assembler; prefer -Wa, instead).",
       "<arg>", nullptr)
OPTION(prefix_1, "-Xlinker", Xlinker, Separate, Link_Group, INVALID, nullptr,
       LinkerInput | RenderAsInput, DefaultVis, 0, "Pass <arg> to the linker",
       "<arg>", nullptr)
OPTION(prefix_1, "-Xmslink", Xmslink, Separate, msvc_Group, INVALID, nullptr, 0,
       DefaultVis, 0, "Forward options to the MSVC linker", "<options>",
       nullptr)
OPTION(prefix_1, "-Xpreprocessor", Xpreprocessor, Separate, Preprocessor_Group,
       INVALID, nullptr, HelpHidden, DefaultVis, 0,
       "Forward one argument into the integrated preprocessor/lexer path (no "
       "separate cpp subprocess). Prefer normal -D/-U/-include.",
       "<arg>", nullptr)
OPTION(prefix_1, "-X", X_Flag, Flag, Link_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-x", x, JoinedOrSeparate, INVALID, INVALID, nullptr,
       NoXarchOption, DefaultVis, 0,
       "Treat subsequent input files as having type <language>", "<language>",
       nullptr)
OPTION(prefix_1, "-y", y, Joined, INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       nullptr, nullptr, nullptr)
OPTION(prefix_1, "-Zlinker-input", Zlinker_input, Separate, INVALID, INVALID,
       nullptr, Unsupported | NoArgumentUnused, DefaultVis, 0, nullptr, nullptr,
       nullptr)
OPTION(prefix_1, "-Z", Z_Flag, Flag, Link_Group, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "-z", z, Separate, Link_Group, INVALID, nullptr, LinkerInput,
       DefaultVis, 0, "Pass -z <arg> to the linker", "<arg>", nullptr)
OPTION(prefix_3, "--", _DASH_DASH, RemainingArgs, INVALID, INVALID, nullptr,
       NoXarchOption, DefaultVis, 0, nullptr, nullptr, nullptr)
#endif // OPTION
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-ftreat-warnings-as-errors",
                                ftreat_warnings_as_errors, Flag, f_Group,
                                INVALID, nullptr, 0, DefaultVis, 0,
                                "Treat warnings as errors", nullptr, nullptr,
                                true, 0, CodeGenOpts.TreatWarningsAsErrors,
                                false, false, false, normalizeSimpleFlag,
                                denormalizeSimpleFlag, mergeForwardValue,
                                extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fauto-generate-bitcode", fauto_generate_bitcode, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis, 0, "Automatically generate bitcode",
    nullptr, nullptr, true, 0, CodeGenOpts.AutoGenerateBitcode, false, false,
    false, normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-fauto-generate-ir",
                                fauto_generate_ir, Flag, f_Group, INVALID,
                                nullptr, 0, DefaultVis, 0,
                                "Automatically generate ir", nullptr, nullptr,
                                true, 0, CodeGenOpts.AutoGenerateIR, false,
                                false, false, normalizeSimpleFlag,
                                denormalizeSimpleFlag, mergeForwardValue,
                                extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING
PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-CC", CC, Flag, Preprocessor_Group, INVALID, nullptr, 0,
    DefaultVis, 0, "Include comments from within macros in preprocessed output",
    nullptr, nullptr, true, 0, PrepOutputOpts.ShowMacroComments, false, false,
    false, normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING
PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-C", C, Flag, Preprocessor_Group, INVALID, nullptr, 0,
    DefaultVis, 0, "Include comments in preprocessed output", nullptr, nullptr,
    true, 0, PrepOutputOpts.ShowComments, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-H", H, Flag, Preprocessor_Group, INVALID, nullptr, 0,
    DefaultVis, 0, "Show header includes and nesting depth", nullptr, nullptr,
    true, 0, DependencyOutputOpts.ShowHeaderIncludes, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-fshow-skipped-includes", fshow_skipped_includes, Flag, INVALID,
    INVALID, nullptr, 0, DefaultVis, 0, "Show skipped includes in -H output.",
    nullptr, nullptr, true, 0, DependencyOutputOpts.ShowSkippedHeaderIncludes,
    false, false, false, normalizeSimpleFlag, denormalizeSimpleFlag,
    mergeForwardValue, extractForwardValue, -1)
#endif // DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-MG", MG, Flag, M_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Add missing headers to depfile", nullptr, nullptr, true, 0,
    DependencyOutputOpts.AddMissingHeaderDeps, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-MP", MP, Flag, M_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Create phony target for each dependency (other than main file)", nullptr,
    nullptr, true, 0, DependencyOutputOpts.UsePhonyTargets, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-MT", MT, JoinedOrSeparate, M_Group, INVALID, nullptr, 0,
    DefaultVis, 0, "Specify name of main file output in depfile", nullptr,
    nullptr, true, 0, DependencyOutputOpts.Targets,
    std::vector<std::string>({}), false, std::vector<std::string>({}),
    normalizeStringVector, denormalizeStringVector, mergeForwardValue,
    extractForwardValue, -1)
#endif // DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-MV", MV, Flag, M_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Use NMake/Jom format for the depfile", nullptr, nullptr, true, 0,
    DependencyOutputOpts.OutputFormat, DependencyOutputFormat::Make, false,
    DependencyOutputFormat::Make,
    makeFlagToValueNormalizer(DependencyOutputFormat::NMake),
    denormalizeSimpleFlag, mergeForwardValue, extractForwardValue, -1)
#endif // DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING
PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-P", P, Flag, Preprocessor_Group, INVALID, nullptr, 0,
    DefaultVis, 0, "Disable linemarker output in -E mode", nullptr, nullptr,
    true, 0, PrepOutputOpts.ShowLineMarkers, true, false, true,
    normalizeSimpleNegativeFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(prefix_1, "-Wundef-prefix=", Wundef_prefix_EQ,
                             CommaJoined, W_value_Group, INVALID, nullptr,
                             HelpHidden, DefaultVis, 0,
                             "Enable warnings for undefined macros with a "
                             "prefix in the comma separated list <arg>",
                             "<arg>", nullptr, true, 0,
                             DiagnosticOpts->UndefPrefixes,
                             std::vector<std::string>({}), false,
                             std::vector<std::string>({}),
                             normalizeStringVector, denormalizeStringVector,
                             mergeForwardValue, extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-complex-range=", complex_range_EQ,
                             Joined, f_Group, INVALID, nullptr, 0, DefaultVis,
                             0, nullptr, nullptr, "full,limited,improved", true,
                             0, LangOpts->ComplexRange, LangOptions::CX_Full,
                             false, LangOptions::CX_Full, normalizeSimpleEnum,
                             denormalizeSimpleEnum, mergeForwardValue,
                             extractForwardValue, 0)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING
PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-dI", dI, Flag, d_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Print include directives in -E mode in addition to normal output", nullptr,
    nullptr, true, 0, PrepOutputOpts.ShowIncludeDirectives, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-dependency-file", dependency_file, Separate, INVALID, INVALID,
    nullptr, 0, DefaultVis, 0, "Filename (or -) to write dependency output to",
    nullptr, nullptr, true, 0, DependencyOutputOpts.OutputFile, std::string(),
    false, std::string(), normalizeString, denormalizeString, mergeForwardValue,
    extractForwardValue, -1)
#endif // DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fmax-tokens=", fmax_tokens_EQ, Joined, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Max total number of preprocessed tokens for -Wmax-tokens.", nullptr,
    nullptr, true, 0, LangOpts->MaxTokens, 0, false, 0,
    normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-falign-loops=", falign_loops_EQ, Joined, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0,
    "N must be a power of two. Align loops to the boundary", "<N>", nullptr,
    true, 0, CodeGenOpts.LoopAlignment, 0, false, 0,
    normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fstrict-flex-arrays=", fstrict_flex_arrays_EQ, Joined, f_Group,
    INVALID, nullptr, 0, DefaultVis, 0,
    "Enable optimizations based on the strict definition of flexible arrays",
    "<n>", "0,1,2,3", true, 0, LangOpts->StrictFlexArraysLevel,
    LangOptions::StrictFlexArraysLevelKind::Default, false,
    LangOptions::StrictFlexArraysLevelKind::Default, normalizeSimpleEnum,
    denormalizeSimpleEnum, mergeForwardValue, extractForwardValue, 1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-autolink", fno_autolink, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0,
    "Disable generation of linker directives for automatic library linking",
    nullptr, nullptr, true, 0, CodeGenOpts.Autolink, true, false, true,
    makeBooleanOptionNormalizer(false, true, OPT_fautolink),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fautolink", fautolink, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis, 0, "", nullptr, nullptr, true, 0, CodeGenOpts.Autolink, true,
    false, true, makeBooleanOptionNormalizer(true, false, OPT_fno_autolink),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-auto-import", fno_auto_import, Flag, f_Group, INVALID,
    nullptr, TargetSpecific, DefaultVis | DefaultVis, 0,
    "Disable support for automatic dllimport in code generation and linking",
    nullptr, nullptr, true, 0, CodeGenOpts.AutoImport, true, false, true,
    makeBooleanOptionNormalizer(false, true, OPT_fauto_import),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fauto-import", fauto_import, Flag, f_Group, INVALID, nullptr,
    TargetSpecific, DefaultVis, 0,
    "Enable code generation support for automatic dllimport (default)", nullptr,
    nullptr, true, 0, CodeGenOpts.AutoImport, true, false, true,
    makeBooleanOptionNormalizer(true, false, OPT_fno_auto_import),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-gnu-inline-asm", fno_gnu_inline_asm, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0, "Disable GNU style inline asm",
    nullptr, nullptr, true, 0, LangOpts->GNUAsm, true, false, true,
    makeBooleanOptionNormalizer(false, true, OPT_fgnu_inline_asm),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fgnu-inline-asm", fgnu_inline_asm, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0, LangOpts->GNUAsm,
    true, false, true,
    makeBooleanOptionNormalizer(true, false, OPT_fno_gnu_inline_asm),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fdebug-compilation-dir=", fdebug_compilation_dir_EQ, Joined,
    f_Group, INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "The compilation directory to embed in the debug info", nullptr, nullptr,
    true, 0, CodeGenOpts.DebugCompilationDir, std::string(), false,
    std::string(), normalizeString, denormalizeString, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-faddrsig", faddrsig, Flag, f_Group, INVALID, nullptr,
    HelpHidden | HelpHidden, DefaultVis | DefaultVis, 0,
    "Emit an address-significance table", nullptr, nullptr, true, 0,
    CodeGenOpts.Addrsig, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_addrsig),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-addrsig", fno_addrsig, Flag, f_Group, INVALID, nullptr,
    HelpHidden | HelpHidden, DefaultVis | DefaultVis, 0,
    "Don't emit an address-significance table", nullptr, nullptr, true, 0,
    CodeGenOpts.Addrsig, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_faddrsig),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-caret-diagnostics", fno_caret_diagnostics, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis | DefaultVis, 0, "", nullptr, nullptr, true,
    0, DiagnosticOpts->ShowCarets, true, false, true,
    makeBooleanOptionNormalizer(false, true, OPT_fcaret_diagnostics),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fcaret-diagnostics", fcaret_diagnostics, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    DiagnosticOpts->ShowCarets, true, false, true,
    makeBooleanOptionNormalizer(true, false, OPT_fno_caret_diagnostics),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(prefix_1, "-fansi-escape-codes",
                             fansi_escape_codes, Flag, f_Group, INVALID,
                             nullptr, 0, DefaultVis, 0,
                             "Use ANSI escape codes for diagnostics", nullptr,
                             nullptr, true, 0,
                             DiagnosticOpts->UseANSIEscapeCodes, false, false,
                             false, normalizeSimpleFlag, denormalizeSimpleFlag,
                             mergeForwardValue, extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fcommon", fcommon, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis, 0, "Place uninitialized global variables in a common block",
    nullptr, nullptr, true, 0, CodeGenOpts.NoCommon, true, false, true,
    normalizeSimpleNegativeFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fconstexpr-depth=", fconstexpr_depth_EQ, Joined, f_Group,
    INVALID, nullptr, 0, DefaultVis, 0,
    "Set the maximum depth of recursive constexpr function calls", nullptr,
    nullptr, true, 0, LangOpts->ConstexprCallDepth, 512, false, 512,
    normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fconstexpr-steps=", fconstexpr_steps_EQ, Joined, f_Group,
    INVALID, nullptr, 0, DefaultVis, 0,
    "Set the maximum number of steps in constexpr function evaluation", nullptr,
    nullptr, true, 0, LangOpts->ConstexprStepLimit, 1048576, false, 1048576,
    normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fconstexpr-backtrace-limit=", fconstexpr_backtrace_limit_EQ,
    Joined, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Set the maximum number of entries to print in a constexpr evaluation "
    "backtrace (0 = no limit)",
    nullptr, nullptr, true, 0, DiagnosticOpts->ConstexprBacktraceLimit,
    DiagnosticOptions::DefaultConstexprBacktraceLimit, false,
    DiagnosticOptions::DefaultConstexprBacktraceLimit,
    normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fasync-exceptions", fasync_exceptions, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0, "Enable EH Asynchronous exceptions",
    nullptr, nullptr, true, 0, LangOpts->EHAsynch, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_async_exceptions),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fno-async-exceptions",
                             fno_async_exceptions, Flag, f_Group, INVALID,
                             nullptr, 0, DefaultVis, 0, "", nullptr, nullptr,
                             true, 0, LangOpts->EHAsynch, false, false, false,
                             makeBooleanOptionNormalizer(false, true,
                                                         OPT_fasync_exceptions),
                             makeBooleanOptionDenormalizer(false),
                             mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(prefix_1, "-fno-diagnostics-fixit-info",
                             fno_diagnostics_fixit_info, Flag, f_Group, INVALID,
                             nullptr, 0, DefaultVis, 0,
                             "Do not include fixit information in diagnostics",
                             nullptr, nullptr, true, 0,
                             DiagnosticOpts->ShowFixits, true, false, true,
                             normalizeSimpleNegativeFlag, denormalizeSimpleFlag,
                             mergeForwardValue, extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(prefix_1, "-fdiagnostics-parseable-fixits",
                             fdiagnostics_parseable_fixits, Flag,
                             f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0,
                             "Print fix-its in machine parseable form", nullptr,
                             nullptr, true, 0,
                             DiagnosticOpts->ShowParseableFixits, false, false,
                             false, normalizeSimpleFlag, denormalizeSimpleFlag,
                             mergeForwardValue, extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(prefix_1, "-fdiagnostics-print-source-range-info",
                             fdiagnostics_print_source_range_info, Flag,
                             f_neverc_Group, INVALID, nullptr, 0, DefaultVis, 0,
                             "Print source range spans in numeric form",
                             nullptr, nullptr, true, 0,
                             DiagnosticOpts->ShowSourceRanges, false, false,
                             false, normalizeSimpleFlag, denormalizeSimpleFlag,
                             mergeForwardValue, extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fdiagnostics-show-hotness", fdiagnostics_show_hotness, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Enable profile hotness information in diagnostic line", nullptr, nullptr,
    true, 0, CodeGenOpts.DiagnosticsWithHotness, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_diagnostics_show_hotness),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-diagnostics-show-hotness", fno_diagnostics_show_hotness,
    Flag, f_Group, INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr,
    true, 0, CodeGenOpts.DiagnosticsWithHotness, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fdiagnostics_show_hotness),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-diagnostics-show-option", fno_diagnostics_show_option, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0, "", nullptr,
    nullptr, true, 0, DiagnosticOpts->ShowOptionNames, true, false, true,
    makeBooleanOptionNormalizer(false, true, OPT_fdiagnostics_show_option),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fdiagnostics-show-option", fdiagnostics_show_option, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Print option name with mappable diagnostics", nullptr, nullptr, true, 0,
    DiagnosticOpts->ShowOptionNames, true, false, true,
    makeBooleanOptionNormalizer(true, false, OPT_fno_diagnostics_show_option),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fdiagnostics-show-note-include-stack",
    fdiagnostics_show_note_include_stack, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0, "Display include stacks for diagnostic notes",
    nullptr, nullptr, true, 0, DiagnosticOpts->ShowNoteIncludeStack, false,
    false, false,
    makeBooleanOptionNormalizer(true, false,
                                OPT_fno_diagnostics_show_note_include_stack),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-diagnostics-show-note-include-stack",
    fno_diagnostics_show_note_include_stack, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis, 0, "", nullptr, nullptr, true, 0,
    DiagnosticOpts->ShowNoteIncludeStack, false, false, false,
    makeBooleanOptionNormalizer(false, true,
                                OPT_fdiagnostics_show_note_include_stack),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fdollars-in-identifiers", fdollars_in_identifiers, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Allow '$' in identifiers", nullptr, nullptr, true, 0,
    LangOpts->DollarIdents, !LangOpts->AsmPreprocessor, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_dollars_in_identifiers),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-dollars-in-identifiers", fno_dollars_in_identifiers, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Disallow '$' in identifiers", nullptr, nullptr, true, 0,
    LangOpts->DollarIdents, !LangOpts->AsmPreprocessor, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fdollars_in_identifiers),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-dwarf-directory-asm", fno_dwarf_directory_asm, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0, "", nullptr,
    nullptr, true, 0, CodeGenOpts.NoDwarfDirectoryAsm, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fdwarf_directory_asm),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fdwarf-directory-asm", fdwarf_directory_asm, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    CodeGenOpts.NoDwarfDirectoryAsm, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fno_dwarf_directory_asm),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-femit-all-decls", femit_all_decls,
                             Flag, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
                             "Emit all declarations, even if unused", nullptr,
                             nullptr, true, 0, LangOpts->EmitAllDecls, false,
                             false, false, normalizeSimpleFlag,
                             denormalizeSimpleFlag, mergeForwardValue,
                             extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-femulated-tls", femulated_tls, Flag, f_Group, INVALID, nullptr,
    0, DefaultVis | DefaultVis, 0,
    "Use emutls functions to access thread_local variables", nullptr, nullptr,
    true, 0, CodeGenOpts.EmulatedTLS, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_emulated_tls),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-fno-emulated-tls", fno_emulated_tls,
                                Flag, f_Group, INVALID, nullptr, 0, DefaultVis,
                                0, "", nullptr, nullptr, true, 0,
                                CodeGenOpts.EmulatedTLS, false, false, false,
                                makeBooleanOptionNormalizer(false, true,
                                                            OPT_femulated_tls),
                                makeBooleanOptionDenormalizer(false),
                                mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fexceptions", fexceptions, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0, "Enable support for exception handling",
    nullptr, nullptr, true, 0, LangOpts->Exceptions, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_exceptions),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-exceptions", fno_exceptions, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0,
    "Disable support for exception handling", nullptr, nullptr, true, 0,
    LangOpts->Exceptions, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fexceptions),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fignore-exceptions", fignore_exceptions, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0,
    "Disable all exception mechanisms including SEH and async EH", nullptr,
    nullptr, true, 0, LangOpts->IgnoreExceptions, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_ignore_exceptions),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-ignore-exceptions", fno_ignore_exceptions, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    LangOpts->IgnoreExceptions, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fignore_exceptions),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-exception-model", exception_model,
                             Separate, INVALID, INVALID, nullptr, 0, DefaultVis,
                             0, "The exception model", nullptr, "dwarf,seh",
                             true, 0, LangOpts->ExceptionHandling,
                             LangOptions::ExceptionHandlingKind::None, false,
                             LangOptions::ExceptionHandlingKind::None,
                             normalizeSimpleEnum, denormalizeSimpleEnum,
                             mergeForwardValue, extractForwardValue, 2)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-ffloat16-excess-precision=", ffloat16_excess_precision_EQ,
    Joined, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Allows control over excess precision on targets where native support for "
    "Float16 precision types is not available. By default, excess precision is "
    "used to calculate intermediate results following the rules specified in "
    "ISO C99.",
    nullptr, "standard,fast,none", true, 0, LangOpts->Float16ExcessPrecision,
    LangOptions::FPP_Standard, false, LangOptions::FPP_Standard,
    normalizeSimpleEnum, denormalizeSimpleEnum, mergeForwardValue,
    extractForwardValue, 3)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fbfloat16-excess-precision=", fbfloat16_excess_precision_EQ,
    Joined, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Allows control over excess precision on targets where native support for "
    "BFloat16 precision types is not available. By default, excess precision "
    "is used to calculate intermediate results following the rules specified "
    "in ISO C99.",
    nullptr, "standard,fast,none", true, 0, LangOpts->BFloat16ExcessPrecision,
    LangOptions::FPP_Standard, false, LangOptions::FPP_Standard,
    normalizeSimpleEnum, denormalizeSimpleEnum, mergeForwardValue,
    extractForwardValue, 4)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-ffp-eval-method=", ffp_eval_method_EQ, Joined, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Specifies the evaluation method to use for floating-point arithmetic.",
    nullptr, "source,double,extended", true, 0, LangOpts->FPEvalMethod,
    LangOptions::FEM_UnsetOnCommandLine, false,
    LangOptions::FEM_UnsetOnCommandLine, normalizeSimpleEnum,
    denormalizeSimpleEnum, mergeForwardValue, extractForwardValue, 5)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-ffp-exception-behavior=", ffp_exception_behavior_EQ, Joined,
    f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Specifies the exception behavior of floating-point operations.", nullptr,
    "ignore,maytrap,strict", true, 0, LangOpts->FPExceptionMode,
    LangOptions::FPE_Default, false, LangOptions::FPE_Default,
    normalizeSimpleEnum, denormalizeSimpleEnum, mergeForwardValue,
    extractForwardValue, 6)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-ffast-math", ffast_math, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0,
    "Allow aggressive, lossy floating-point optimizations", nullptr, nullptr,
    true, 0, LangOpts->FastMath, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_fast_math),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fno-fast-math", fno_fast_math, Flag,
                             f_Group, INVALID, nullptr, 0,
                             DefaultVis | DefaultVis, 0, "", nullptr, nullptr,
                             true, 0, LangOpts->FastMath, false, false, false,
                             makeBooleanOptionNormalizer(false, true,
                                                         OPT_ffast_math),
                             makeBooleanOptionDenormalizer(false),
                             mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fmath-errno", fmath_errno, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0,
    "Require math functions to indicate errors by setting errno", nullptr,
    nullptr, true, 0, LangOpts->MathErrno, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_math_errno),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fno-math-errno", fno_math_errno, Flag,
                             f_Group, INVALID, nullptr, 0, DefaultVis, 0, "",
                             nullptr, nullptr, true, 0, LangOpts->MathErrno,
                             false, false, false,
                             makeBooleanOptionNormalizer(false, true,
                                                         OPT_fmath_errno),
                             makeBooleanOptionDenormalizer(false),
                             mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fextend-arguments=", fextend_args_EQ, Joined, f_Group, INVALID,
    nullptr, NoArgumentUnused, DefaultVis, 0,
    "Controls how scalar integer arguments are extended in calls to "
    "unprototyped and varargs functions",
    nullptr, "32,64", true, 0, LangOpts->ExtendIntArgs,
    LangOptions::ExtendArgsKind::ExtendTo32, false,
    LangOptions::ExtendArgsKind::ExtendTo32, normalizeSimpleEnum,
    denormalizeSimpleEnum, mergeForwardValue, extractForwardValue, 7)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-jump-tables", fno_jump_tables, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0,
    "Do not use jump tables for lowering switches", nullptr, nullptr, true, 0,
    CodeGenOpts.NoUseJumpTables, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fjump_tables),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fjump-tables", fjump_tables, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0, "Use jump tables for lowering switches",
    nullptr, nullptr, true, 0, CodeGenOpts.NoUseJumpTables, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fno_jump_tables),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef TARGET_OPTION_WITH_MARSHALLING
TARGET_OPTION_WITH_MARSHALLING(
    prefix_1, "-fforce-enable-int128", fforce_enable_int128, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Enable support for int128_t type", nullptr, nullptr, true, 0,
    TargetOpts->ForceEnableInt128, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_force_enable_int128),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // TARGET_OPTION_WITH_MARSHALLING
#ifdef TARGET_OPTION_WITH_MARSHALLING
TARGET_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-force-enable-int128", fno_force_enable_int128, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Disable support for int128_t type", nullptr, nullptr, true, 0,
    TargetOpts->ForceEnableInt128, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fforce_enable_int128),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // TARGET_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fkeep-static-consts", fkeep_static_consts, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis, 0,
    "Keep static const variables even if unused", nullptr, nullptr, true, 0,
    CodeGenOpts.KeepStaticConsts, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_keep_static_consts),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-keep-static-consts", fno_keep_static_consts, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis, 0,
    "Don't keep static const variables even if unused", nullptr, nullptr, true,
    0, CodeGenOpts.KeepStaticConsts, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fkeep_static_consts),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fkeep-persistent-storage-variables",
    fkeep_persistent_storage_variables, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis, 0,
    "Enable keeping all variables that have a persistent storage duration, "
    "including global, static and thread-local variables, to guarantee that "
    "they can be directly addressed",
    nullptr, nullptr, true, 0, CodeGenOpts.KeepPersistentStorageVariables,
    false, false, false,
    makeBooleanOptionNormalizer(true, false,
                                OPT_fno_keep_persistent_storage_variables),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-keep-persistent-storage-variables",
    fno_keep_persistent_storage_variables, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis, 0,
    "Disable keeping all variables that have a persistent storage duration, "
    "including global, static and thread-local variables, to guarantee that "
    "they can be directly addressed",
    nullptr, nullptr, true, 0, CodeGenOpts.KeepPersistentStorageVariables,
    false, false, false,
    makeBooleanOptionNormalizer(false, true,
                                OPT_fkeep_persistent_storage_variables),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-ffixed-point", ffixed_point, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0, "Enable fixed point types", nullptr, nullptr,
    true, 0, LangOpts->FixedPoint, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_fixed_point),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-fixed-point", fno_fixed_point, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0, "Disable fixed point types",
    nullptr, nullptr, true, 0, LangOpts->FixedPoint, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_ffixed_point),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1,
                                "-fsymbol-partition=", fsymbol_partition_EQ,
                                Joined, f_Group, INVALID, nullptr, 0,
                                DefaultVis, 0, nullptr, nullptr, nullptr, true,
                                0, CodeGenOpts.SymbolPartition, std::string(),
                                false, std::string(), normalizeString,
                                denormalizeString, mergeForwardValue,
                                extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-fjumptable-rdata", fjumptable_rdata,
                                Flag, f_neverc_Group, INVALID, nullptr, 0,
                                DefaultVis, 0,
                                "Put switch case jump tables in .rdata",
                                nullptr, nullptr, true, 0,
                                CodeGenOpts.Jumptablerdata, false, false, false,
                                normalizeSimpleFlag, denormalizeSimpleFlag,
                                mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fdisable-inline-opt", fdisable_inline_opt, Flag, f_neverc_Group,
    INVALID, nullptr, 0, DefaultVis, 0,
    "Disables the optimization of inline functions", nullptr, nullptr, true, 0,
    CodeGenOpts.DisableInlineOpt, false, false, false, normalizeSimpleFlag,
    denormalizeSimpleFlag, mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-fdisable-try-stmt",
                                fdisable_try_stmt, Flag, f_neverc_Group,
                                INVALID, nullptr, 0, DefaultVis, 0,
                                "Disables the try statements", nullptr, nullptr,
                                true, 0, CodeGenOpts.DisableTryStmt, false,
                                false, false, normalizeSimpleFlag,
                                denormalizeSimpleFlag, mergeForwardValue,
                                extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-fdisable-cfi-check",
                                fdisable_cfi_check, Flag, f_neverc_Group,
                                INVALID, nullptr, 0, DefaultVis, 0,
                                "Disables the checks in CFI", nullptr, nullptr,
                                true, 0, CodeGenOpts.DisableCFICheck, false,
                                false, false, normalizeSimpleFlag,
                                denormalizeSimpleFlag, mergeForwardValue,
                                extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-funsafe-math-optimizations",
                             funsafe_math_optimizations, Flag, f_Group, INVALID,
                             nullptr, 0, DefaultVis, 0,
                             "Allow unsafe floating-point math optimizations "
                             "which may decrease precision",
                             nullptr, nullptr, true, 0, LangOpts->UnsafeFPMath,
                             false, false || LangOpts->FastMath, true,
                             normalizeSimpleFlag, denormalizeSimpleFlag,
                             mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-freciprocal-math", freciprocal_math, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0,
    "Allow division operations to be reassociated", nullptr, nullptr, true, 0,
    LangOpts->AllowRecip, false, false || LangOpts->UnsafeFPMath, true,
    makeBooleanOptionNormalizer(true, false, OPT_fno_reciprocal_math),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-reciprocal-math", fno_reciprocal_math, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis | DefaultVis, 0, "", nullptr, nullptr, true,
    0, LangOpts->AllowRecip, false, false || LangOpts->UnsafeFPMath, true,
    makeBooleanOptionNormalizer(false, true, OPT_freciprocal_math),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fapprox-func", fapprox_func, Flag,
                             f_Group, INVALID, nullptr, 0,
                             DefaultVis | DefaultVis, 0,
                             "Allow certain math function calls to be replaced "
                             "with an approximately equivalent calculation",
                             nullptr, nullptr, true, 0, LangOpts->ApproxFunc,
                             false, false || LangOpts->UnsafeFPMath, true,
                             makeBooleanOptionNormalizer(true, false,
                                                         OPT_fno_approx_func),
                             makeBooleanOptionDenormalizer(true),
                             mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-approx-func", fno_approx_func, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0, "", nullptr, nullptr, true, 0,
    LangOpts->ApproxFunc, false, false || LangOpts->UnsafeFPMath, true,
    makeBooleanOptionNormalizer(false, true, OPT_fapprox_func),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-ffinite-math-only", ffinite_math_only, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0,
    "Allow floating-point optimizations that assume arguments and results are "
    "not NaNs or +-inf. This defines the \\_\\_FINITE\\_MATH\\_ONLY\\_\\_ "
    "preprocessor macro.",
    nullptr, nullptr, true, 0, LangOpts->FiniteMathOnly, false,
    false || LangOpts->FastMath, true,
    makeBooleanOptionNormalizer(true, false, OPT_fno_finite_math_only),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-finite-math-only", fno_finite_math_only, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    LangOpts->FiniteMathOnly, false, false || LangOpts->FastMath, true,
    makeBooleanOptionNormalizer(false, true, OPT_ffinite_math_only),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-signed-zeros", fno_signed_zeros, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0,
    "Allow optimizations that ignore the sign of floating point zeros", nullptr,
    nullptr, true, 0, LangOpts->NoSignedZero, false,
    false || LangOpts->UnsafeFPMath, true,
    makeBooleanOptionNormalizer(true, false, OPT_fsigned_zeros),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fsigned-zeros", fsigned_zeros, Flag, f_Group, INVALID, nullptr,
    0, DefaultVis | DefaultVis, 0, "", nullptr, nullptr, true, 0,
    LangOpts->NoSignedZero, false, false || LangOpts->UnsafeFPMath, true,
    makeBooleanOptionNormalizer(false, true, OPT_fno_signed_zeros),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-frounding-math", frounding_math, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr, true, 0,
    LangOpts->RoundingMath, false, false, false,
    makeFlagToValueNormalizer(llvm::RoundingMode::Dynamic),
    denormalizeSimpleFlag, mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-strict-float-cast-overflow", fno_strict_float_cast_overflow,
    Flag, f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Relax language rules and try to match the behavior of the target's native "
    "float-to-int conversion instructions",
    nullptr, nullptr, true, 0, CodeGenOpts.StrictFloatCastOverflow, true, false,
    true,
    makeBooleanOptionNormalizer(false, true, OPT_fstrict_float_cast_overflow),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fstrict-float-cast-overflow", fstrict_float_cast_overflow, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Assume that overflowing float-to-int casts are undefined (default)",
    nullptr, nullptr, true, 0, CodeGenOpts.StrictFloatCastOverflow, true, false,
    true,
    makeBooleanOptionNormalizer(true, false,
                                OPT_fno_strict_float_cast_overflow),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fprotect-parens", fprotect_parens, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0,
    "Determines whether the optimizer honors parentheses when floating-point "
    "expressions are evaluated",
    nullptr, nullptr, true, 0, LangOpts->ProtectParens, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_protect_parens),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-protect-parens", fno_protect_parens, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    LangOpts->ProtectParens, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fprotect_parens),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-delete-null-pointer-checks", fno_delete_null_pointer_checks,
    Flag, f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Do not treat usage of null pointers as undefined behavior", nullptr,
    nullptr, true, 0, CodeGenOpts.NullPointerIsValid, true, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fdelete_null_pointer_checks),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fdelete-null-pointer-checks", fdelete_null_pointer_checks, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Treat usage of null pointers as undefined behavior (default)", nullptr,
    nullptr, true, 0, CodeGenOpts.NullPointerIsValid, true, false, false,
    makeBooleanOptionNormalizer(false, true,
                                OPT_fno_delete_null_pointer_checks),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING
PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-fuse-line-directives", fuse_line_directives, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Use #line in preprocessed output", nullptr, nullptr, true, 0,
    PrepOutputOpts.UseLineDirectives, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_use_line_directives),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING
PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-use-line-directives", fno_use_line_directives, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    PrepOutputOpts.UseLineDirectives, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fuse_line_directives),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING
PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-fminimize-whitespace", fminimize_whitespace, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Ignore the whitespace from the input file when emitting preprocessor "
    "output. It will only contain whitespace when necessary, e.g. to keep two "
    "minus signs from merging into to an increment operator. Useful with the "
    "-P option to normalize whitespace such that two files with only "
    "formatting changes are equal.\n\nOnly valid with -E on C-like inputs and "
    "incompatible with -traditional-cpp.",
    nullptr, nullptr, true, 0, PrepOutputOpts.MinimizeWhitespace, false, false,
    false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_minimize_whitespace),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING
PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-minimize-whitespace", fno_minimize_whitespace, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    PrepOutputOpts.MinimizeWhitespace, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fminimize_whitespace),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING
PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-fkeep-system-includes", fkeep_system_includes, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Instead of expanding system headers when emitting preprocessor output, "
    "preserve the #include directive. Useful when producing preprocessed "
    "output for test case reduction. May produce incorrect output if "
    "preprocessor symbols that control the included content (e.g. "
    "_XOPEN_SOURCE) are defined in the including source file. The portability "
    "of the resulting source to other compilation environments is not "
    "guaranteed.\n\nOnly valid with -E.",
    nullptr, nullptr, true, 0, PrepOutputOpts.KeepSystemIncludes, false, false,
    false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_keep_system_includes),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING
PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-keep-system-includes", fno_keep_system_includes, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    PrepOutputOpts.KeepSystemIncludes, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fkeep_system_includes),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-ffreestanding", ffreestanding, Flag, f_Group, INVALID, nullptr,
    0, DefaultVis, 0,
    "Assert that the compilation takes place in a freestanding environment",
    nullptr, nullptr, true, 0, LangOpts->Freestanding, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fgnu-keywords", fgnu_keywords, Flag, f_Group, INVALID, nullptr,
    0, DefaultVis | DefaultVis, 0,
    "Allow GNU-extension keywords regardless of language standard", nullptr,
    nullptr, true, 0, LangOpts->GNUKeywords, LangOpts->GNUMode, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_gnu_keywords),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-gnu-keywords", fno_gnu_keywords, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    LangOpts->GNUKeywords, LangOpts->GNUMode, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fgnu_keywords),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fgnu89-inline", fgnu89_inline, Flag, f_Group, INVALID, nullptr,
    0, DefaultVis | DefaultVis, 0, "Use the gnu89 inline semantics", nullptr,
    nullptr, true, 0, LangOpts->GNUInline, !LangOpts->C99, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_gnu89_inline),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fno-gnu89-inline", fno_gnu89_inline,
                             Flag, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
                             "", nullptr, nullptr, true, 0, LangOpts->GNUInline,
                             !LangOpts->C99, false, false,
                             makeBooleanOptionNormalizer(false, true,
                                                         OPT_fgnu89_inline),
                             makeBooleanOptionDenormalizer(false),
                             mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-finline-max-stacksize=", finline_max_stacksize_EQ, Joined,
    f_Group, INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "Suppress inlining of functions whose stack size exceeds the given value",
    nullptr, nullptr, true, 0, CodeGenOpts.InlineMaxStackSize, UINT_MAX, false,
    UINT_MAX, normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fjmc", fjmc, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0, "Enable just-my-code debugging", nullptr,
    nullptr, true, 0, CodeGenOpts.JMCInstrument, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_jmc),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-jmc", fno_jmc, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis, 0, "", nullptr, nullptr, true, 0, CodeGenOpts.JMCInstrument,
    false, false, false, makeBooleanOptionNormalizer(false, true, OPT_fjmc),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fexperimental-strict-floating-point",
    fexperimental_strict_floating_point, Flag, f_neverc_Group, INVALID, nullptr,
    0, DefaultVis, 0,
    "Enables the use of non-default rounding modes and non-default exception "
    "handling on targets that are not currently ready.",
    nullptr, nullptr, true, 0, LangOpts->ExpStrictFP, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-mfunction-return=", mfunction_return_EQ, Joined, m_Group,
    INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Replace returns with jumps to ``__x86_return_thunk`` (x86 only, error "
    "otherwise)",
    nullptr, "keep,thunk-extern", true, 0, CodeGenOpts.FunctionReturnThunks,
    llvm::FunctionReturnThunksKind::Keep, false,
    llvm::FunctionReturnThunksKind::Keep, normalizeSimpleEnum,
    denormalizeSimpleEnum, mergeForwardValue, extractForwardValue, 8)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-mindirect-branch-cs-prefix", mindirect_branch_cs_prefix, Flag,
    m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Add cs prefix to call and jmp to indirect thunk", nullptr, nullptr, true,
    0, CodeGenOpts.IndirectBranchCSPrefix, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-ffine-grained-bitfield-accesses",
    ffine_grained_bitfield_accesses, Flag, f_neverc_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0,
    "Use separate accesses for consecutive bitfield runs with legal widths and "
    "alignments.",
    nullptr, nullptr, true, 0, CodeGenOpts.FineGrainedBitfieldAccesses, false,
    false, false,
    makeBooleanOptionNormalizer(true, false,
                                OPT_fno_fine_grained_bitfield_accesses),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-fine-grained-bitfield-accesses",
    fno_fine_grained_bitfield_accesses, Flag, f_neverc_Group, INVALID, nullptr,
    0, DefaultVis | DefaultVis, 0,
    "Use large-integer access for consecutive bitfield runs.", nullptr, nullptr,
    true, 0, CodeGenOpts.FineGrainedBitfieldAccesses, false, false, false,
    makeBooleanOptionNormalizer(false, true,
                                OPT_ffine_grained_bitfield_accesses),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-flax-vector-conversions=", flax_vector_conversions_EQ, Joined,
    f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Enable implicit vector bit-casts", nullptr, "none,integer,all", true, 0,
    LangOpts->LaxVectorConversions, LangOptions::LaxVectorConversionKind::All,
    false, LangOptions::LaxVectorConversionKind::All, normalizeSimpleEnum,
    denormalizeSimpleEnum, mergeForwardValue, extractForwardValue, 9)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fmacro-backtrace-limit=", fmacro_backtrace_limit_EQ, Joined,
    f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Set the maximum number of entries to print in a macro expansion backtrace "
    "(0 = no limit)",
    nullptr, nullptr, true, 0, DiagnosticOpts->MacroBacktraceLimit,
    DiagnosticOptions::DefaultMacroBacktraceLimit, false,
    DiagnosticOptions::DefaultMacroBacktraceLimit,
    normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fcaret-diagnostics-max-lines=", fcaret_diagnostics_max_lines_EQ,
    Joined, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Set the maximum number of source lines to show in a caret diagnostic (0 = "
    "no limit).",
    nullptr, nullptr, true, 0, DiagnosticOpts->SnippetLineLimit,
    DiagnosticOptions::DefaultSnippetLineLimit, false,
    DiagnosticOptions::DefaultSnippetLineLimit,
    normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fmerge-all-constants", fmerge_all_constants, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Allow merging of constants", nullptr, nullptr, true, 0,
    CodeGenOpts.MergeAllConstants, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_merge_all_constants),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-merge-all-constants", fno_merge_all_constants, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Disallow merging of constants", nullptr, nullptr, true, 0,
    CodeGenOpts.MergeAllConstants, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fmerge_all_constants),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fmessage-length=", fmessage_length_EQ, Joined, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Format message diagnostics so that they fit within N columns", nullptr,
    nullptr, true, 0, DiagnosticOpts->MessageLength, 0, false, 0,
    normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fms-compatibility", fms_compatibility,
                             Flag, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
                             "Enable full Microsoft Visual C compatibility",
                             nullptr, nullptr, true, 0, LangOpts->MSVCCompat,
                             false, false, false, normalizeSimpleFlag,
                             denormalizeSimpleFlag, mergeForwardValue,
                             extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fms-extensions", fms_extensions, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Accept some non-standard constructs supported by the Microsoft compiler",
    nullptr, nullptr, true, 0, LangOpts->MicrosoftExt, false,
    false || LangOpts->MSVCCompat, true, normalizeSimpleFlag,
    denormalizeSimpleFlag, mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fasm-blocks", fasm_blocks, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0, "", nullptr, nullptr, true, 0,
    LangOpts->AsmBlocks, LangOpts->MicrosoftExt, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_asm_blocks),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fno-asm-blocks", fno_asm_blocks, Flag,
                             f_Group, INVALID, nullptr, 0, DefaultVis, 0, "",
                             nullptr, nullptr, true, 0, LangOpts->AsmBlocks,
                             LangOpts->MicrosoftExt, false, false,
                             makeBooleanOptionNormalizer(false, true,
                                                         OPT_fasm_blocks),
                             makeBooleanOptionDenormalizer(false),
                             mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fms-volatile", fms_volatile, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0,
    "Volatile loads and stores have acquire and release semantics", nullptr,
    nullptr, true, 0, LangOpts->MSVolatile, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_ms_volatile),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fno-ms-volatile", fno_ms_volatile,
                             Flag, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
                             "", nullptr, nullptr, true, 0,
                             LangOpts->MSVolatile, false, false, false,
                             makeBooleanOptionNormalizer(false, true,
                                                         OPT_fms_volatile),
                             makeBooleanOptionDenormalizer(false),
                             mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fms-kernel", fms_kernel, Flag, f_Group,
                             INVALID, nullptr, 0, DefaultVis, 0, nullptr,
                             nullptr, nullptr, true, 0, LangOpts->Kernel, false,
                             false, false, normalizeSimpleFlag,
                             denormalizeSimpleFlag, mergeForwardValue,
                             extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fdeclspec", fdeclspec, Flag, f_neverc_Group, INVALID, nullptr,
    0, DefaultVis | DefaultVis, 0, "Allow __declspec as a keyword", nullptr,
    nullptr, true, 0, LangOpts->DeclSpecKeyword, true,
    false || LangOpts->MicrosoftExt, true,
    makeBooleanOptionNormalizer(true, false, OPT_fno_declspec),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-declspec", fno_declspec, Flag, f_neverc_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0, "Disallow __declspec as a keyword",
    nullptr, nullptr, true, 0, LangOpts->DeclSpecKeyword, true,
    false || LangOpts->MicrosoftExt, true,
    makeBooleanOptionNormalizer(false, true, OPT_fdeclspec),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fno-knr-functions", fno_knr_functions,
                             Flag, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
                             "Disable support for K&R C function declarations",
                             nullptr, nullptr, true, 0,
                             LangOpts->DisableKNRFunctions, false, false, false,
                             normalizeSimpleFlag, denormalizeSimpleFlag,
                             mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fneverc-types", fneverc_types, Flag, f_Group, INVALID, nullptr,
    0, DefaultVis | DefaultVis, 0,
    "Enable NeverC Rust-style integer type keywords (u8/u16/u32/u64/u128, "
    "i8/i16/i32/i64/i128, isize/usize)",
    nullptr, nullptr, true, 0, LangOpts->NeverCTypes, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_neverc_types),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-neverc-types", fno_neverc_types, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0,
    "Disable NeverC Rust-style integer type keywords (u8/u16/u32/u64/u128, "
    "i8/i16/i32/i64/i128, isize/usize)",
    nullptr, nullptr, true, 0, LangOpts->NeverCTypes, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fneverc_types),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fbuiltin-mimalloc", fbuiltin_mimalloc, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0,
    "Enable mimalloc allocator override injection via IR bitcode embedding",
    nullptr, nullptr, true, 1, LangOpts->BuiltinMimalloc, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_builtin_mimalloc),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-builtin-mimalloc", fno_builtin_mimalloc, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Disable mimalloc allocator override injection",
    nullptr, nullptr, true, 1, LangOpts->BuiltinMimalloc, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fbuiltin_mimalloc),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fbuiltin-string", fbuiltin_string, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0,
    "Enable NeverC builtin string runtime prelude (value-typed `string`, "
    "NEVERC_STRING_* macros, s.format()/s.length()/s.find()/... inline "
    "helpers)",
    nullptr, nullptr, true, 1, LangOpts->BuiltinString, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_builtin_string),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-builtin-string", fno_builtin_string, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0,
    "Disable NeverC builtin string runtime prelude (value-typed `string`, "
    "NEVERC_STRING_* macros, s.format()/s.length()/s.find()/... inline "
    "helpers)",
    nullptr, nullptr, true, 1, LangOpts->BuiltinString, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fbuiltin_string),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fshellcode-mode", fshellcode_mode,
                             Flag, INVALID, INVALID, nullptr, HelpHidden,
                             DefaultVis, 0, nullptr, nullptr, nullptr, true, 0,
                             LangOpts->ShellcodeMode, false, false, false,
                             normalizeSimpleFlag, denormalizeSimpleFlag,
                             mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fdigraphs", fdigraphs, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0,
    "Enable alternative token representations '<:', ':>', '<%', '%>', '%:', "
    "'%:%:' (default)",
    nullptr, nullptr, true, 0, LangOpts->Digraphs,
    LangStandard::getLangStandardForKind(LangOpts->LangStd).hasDigraphs(),
    false, false, makeBooleanOptionNormalizer(true, false, OPT_fno_digraphs),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-digraphs", fno_digraphs, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0,
    "Disallow alternative token representations '<:', ':>', '<%', '%>', '%:', "
    "'%:%:'",
    nullptr, nullptr, true, 0, LangOpts->Digraphs,
    LangStandard::getLangStandardForKind(LangOpts->LangStd).hasDigraphs(),
    false, false, makeBooleanOptionNormalizer(false, true, OPT_fdigraphs),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fveclib=", fveclib, Joined, f_Group, INVALID, nullptr, 0,
    DefaultVis, 0, "Use the given vector functions library", nullptr,
    "Accelerate,libmvec,MASSV,SVML,SLEEF,Darwin_libsystem_m,ArmPL,none", true,
    0, CodeGenOpts.VecLib, llvm::driver::VectorLibrary::NoLibrary, false,
    llvm::driver::VectorLibrary::NoLibrary, normalizeSimpleEnum,
    denormalizeSimpleEnum, mergeForwardValue, extractForwardValue, 10)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(prefix_1, "-fdiagnostics-absolute-paths",
                             fdiagnostics_absolute_paths, Flag, f_Group,
                             INVALID, nullptr, 0, DefaultVis, 0,
                             "Print absolute paths in diagnostics", nullptr,
                             nullptr, true, 0, DiagnosticOpts->AbsolutePath,
                             false, false, false, normalizeSimpleFlag,
                             denormalizeSimpleFlag, mergeForwardValue,
                             extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-diagnostics-show-line-numbers",
    fno_diagnostics_show_line_numbers, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0, "Show line numbers in diagnostic code snippets",
    nullptr, nullptr, true, 0, DiagnosticOpts->ShowLineNumbers, true, false,
    true,
    makeBooleanOptionNormalizer(false, true,
                                OPT_fdiagnostics_show_line_numbers),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fdiagnostics-show-line-numbers", fdiagnostics_show_line_numbers,
    Flag, f_Group, INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr,
    true, 0, DiagnosticOpts->ShowLineNumbers, true, false, true,
    makeBooleanOptionNormalizer(true, false,
                                OPT_fno_diagnostics_show_line_numbers),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef FRONTEND_OPTION_WITH_MARSHALLING
FRONTEND_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-temp-file", fno_temp_file, Flag, f_Group, INVALID, nullptr,
    0, DefaultVis, 0,
    "Directly create compilation output files. This may lead to incorrect "
    "incremental builds if the compiler crashes",
    nullptr, nullptr, true, 0, FrontendOpts.UseTemporary, true, false, true,
    normalizeSimpleNegativeFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // FRONTEND_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-use-cxa-atexit", fno_use_cxa_atexit, Flag, f_Group, INVALID,
    nullptr, HelpHidden, DefaultVis | DefaultVis, 0,
    "Don't use __cxa_atexit for registering cleanup functions", nullptr,
    nullptr, true, 0, CodeGenOpts.CXAAtExit, true, false, true,
    makeBooleanOptionNormalizer(false, true, OPT_fuse_cxa_atexit),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fuse-cxa-atexit", fuse_cxa_atexit, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    CodeGenOpts.CXAAtExit, true, false, true,
    makeBooleanOptionNormalizer(true, false, OPT_fno_use_cxa_atexit),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-fno-verbose-asm", fno_verbose_asm,
                                Flag, f_Group, INVALID, nullptr, 0, DefaultVis,
                                0, nullptr, nullptr, nullptr, true, 0,
                                CodeGenOpts.AsmVerbose, true, false, true,
                                normalizeSimpleNegativeFlag,
                                denormalizeSimpleFlag, mergeForwardValue,
                                extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-optimize-sibling-calls", fno_optimize_sibling_calls, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Disable tail call optimization, keeping the call stack accurate", nullptr,
    nullptr, true, 0, CodeGenOpts.DisableTailCalls, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fpack-struct=", fpack_struct_EQ, Joined, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Specify the default maximum struct packing alignment", nullptr, nullptr,
    true, 0, LangOpts->PackStruct, 0, false, 0,
    normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fmax-type-align=", fmax_type_align_EQ,
                             Joined, f_Group, INVALID, nullptr, 0, DefaultVis,
                             0,
                             "Specify the maximum alignment to enforce on "
                             "pointers lacking an explicit alignment",
                             nullptr, nullptr, true, 0, LangOpts->MaxTypeAlign,
                             0, false, 0, normalizeStringIntegral<unsigned>,
                             denormalizeString<unsigned>, mergeForwardValue,
                             extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fpatchable-function-entry=", fpatchable_function_entry_EQ,
    Joined, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Generate M NOPs before function entry and N-M NOPs after function entry",
    "<N,M>", nullptr, true, 0, CodeGenOpts.PatchableFunctionEntryCount, 0,
    false, 0, normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fms-hotpatch", fms_hotpatch, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis, 0, "Ensure that all functions can be hotpatched at runtime",
    nullptr, nullptr, true, 0, CodeGenOpts.HotPatch, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-fno-plt", fno_plt, Flag, f_Group,
                                INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
                                "Use GOT indirection instead of PLT to make "
                                "external function calls (x86 only)",
                                nullptr, nullptr, true, 0, CodeGenOpts.NoPLT,
                                false, false, false,
                                makeBooleanOptionNormalizer(true, false,
                                                            OPT_fplt),
                                makeBooleanOptionDenormalizer(true),
                                mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-fplt", fplt, Flag, f_Group, INVALID,
                                nullptr, 0, DefaultVis, 0, "", nullptr, nullptr,
                                true, 0, CodeGenOpts.NoPLT, false, false, false,
                                makeBooleanOptionNormalizer(false, true,
                                                            OPT_fno_plt),
                                makeBooleanOptionDenormalizer(false),
                                mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-fpass-plugin=", fpass_plugin_EQ,
                                Joined, f_Group, INVALID, nullptr, 0,
                                DefaultVis, 0,
                                "Load pass plugin from a dynamic shared object "
                                "file (only with new pass manager).",
                                "<dsopath>", nullptr, true, 0,
                                CodeGenOpts.PassPlugins,
                                std::vector<std::string>({}), false,
                                std::vector<std::string>({}),
                                normalizeStringVector, denormalizeStringVector,
                                mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-preserve-as-comments", fno_preserve_as_comments, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Do not preserve comments in inline assembly", nullptr, nullptr, true, 0,
    CodeGenOpts.PreserveAsmComments, true, false, true,
    makeBooleanOptionNormalizer(false, true, OPT_fpreserve_as_comments),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fpreserve-as-comments", fpreserve_as_comments, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    CodeGenOpts.PreserveAsmComments, true, false, true,
    makeBooleanOptionNormalizer(true, false, OPT_fno_preserve_as_comments),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fshort-enums", fshort_enums, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0,
    "Allocate to an enum type only as many bytes as it needs for the declared "
    "range of possible values",
    nullptr, nullptr, true, 0, LangOpts->ShortEnums, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_short_enums),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fno-short-enums", fno_short_enums,
                             Flag, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
                             "", nullptr, nullptr, true, 0,
                             LangOpts->ShortEnums, false, false, false,
                             makeBooleanOptionNormalizer(false, true,
                                                         OPT_fshort_enums),
                             makeBooleanOptionDenormalizer(false),
                             mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-show-column", fno_show_column, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0,
    "Do not include column number on diagnostics", nullptr, nullptr, true, 0,
    DiagnosticOpts->ShowColumn, true, false, true,
    makeBooleanOptionNormalizer(false, true, OPT_fshow_column),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(prefix_1, "-fshow-column", fshow_column, Flag,
                             f_Group, INVALID, nullptr, 0, DefaultVis, 0, "",
                             nullptr, nullptr, true, 0,
                             DiagnosticOpts->ShowColumn, true, false, true,
                             makeBooleanOptionNormalizer(true, false,
                                                         OPT_fno_show_column),
                             makeBooleanOptionDenormalizer(true),
                             mergeForwardValue, extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-show-source-location", fno_show_source_location, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Do not include source location information with diagnostics", nullptr,
    nullptr, true, 0, DiagnosticOpts->ShowLocation, true, false, true,
    makeBooleanOptionNormalizer(false, true, OPT_fshow_source_location),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fshow-source-location", fshow_source_location, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    DiagnosticOpts->ShowLocation, true, false, true,
    makeBooleanOptionNormalizer(true, false, OPT_fno_show_source_location),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-spell-checking", fno_spell_checking, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0, "Disable spell-checking", nullptr,
    nullptr, true, 0, LangOpts->SpellChecking, true, false, true,
    makeBooleanOptionNormalizer(false, true, OPT_fspell_checking),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fspell-checking", fspell_checking, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    LangOpts->SpellChecking, true, false, true,
    makeBooleanOptionNormalizer(true, false, OPT_fno_spell_checking),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fspell-checking-limit=", fspell_checking_limit_EQ, Joined,
    f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Set the maximum number of times to perform spell checking on unrecognized "
    "identifiers (0 = no limit)",
    nullptr, nullptr, true, 0, DiagnosticOpts->SpellCheckingLimit,
    DiagnosticOptions::DefaultSpellCheckingLimit, false,
    DiagnosticOptions::DefaultSpellCheckingLimit,
    normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-signed-char", fno_signed_char, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0, "char is unsigned", nullptr,
    nullptr, true, 0, LangOpts->CharIsSigned, true, false, true,
    makeBooleanOptionNormalizer(false, true, OPT_fsigned_char),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fsigned-char", fsigned_char, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0, "char is signed", nullptr, nullptr, true, 0,
    LangOpts->CharIsSigned, true, false, true,
    makeBooleanOptionNormalizer(true, false, OPT_fno_signed_char),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-split-stack", fno_split_stack, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0, "Wouldn't use segmented stack",
    nullptr, nullptr, true, 0, CodeGenOpts.EnableSegmentedStacks, false, false,
    true, makeBooleanOptionNormalizer(false, true, OPT_fsplit_stack),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fsplit-stack", fsplit_stack, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0, "Use segmented stack", nullptr, nullptr, true,
    0, CodeGenOpts.EnableSegmentedStacks, false, false, true,
    makeBooleanOptionNormalizer(true, false, OPT_fno_split_stack),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fstack-clash-protection", fstack_clash_protection, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Enable stack clash protection", nullptr, nullptr, true, 0,
    CodeGenOpts.StackClashProtector, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_stack_clash_protection),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-stack-clash-protection", fno_stack_clash_protection, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Disable stack clash protection", nullptr, nullptr, true, 0,
    CodeGenOpts.StackClashProtector, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fstack_clash_protection),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-ftrivial-auto-var-init=", ftrivial_auto_var_init, Joined,
    f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Initialize trivial automatic stack variables. Defaults to 'uninitialized'",
    nullptr, "uninitialized,zero,pattern", true, 0,
    LangOpts->TrivialAutoVarInit,
    LangOptions::TrivialAutoVarInitKind::Uninitialized, false,
    LangOptions::TrivialAutoVarInitKind::Uninitialized, normalizeSimpleEnum,
    denormalizeSimpleEnum, mergeForwardValue, extractForwardValue, 11)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1,
    "-ftrivial-auto-var-init-stop-after=", ftrivial_auto_var_init_stop_after,
    Joined, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Stop initializing trivial automatic stack variables after the specified "
    "number of instances",
    nullptr, nullptr, true, 0, LangOpts->TrivialAutoVarInitStopAfter, 0, false,
    0, normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1,
                             "-Wlarge-by-value-copy=", Wlarge_by_value_copy_EQ,
                             Joined, INVALID, INVALID, nullptr, 0, DefaultVis,
                             0, nullptr, nullptr, nullptr, true, 0,
                             LangOpts->NumLargeByValueCopy, 0, false, 0,
                             normalizeStringIntegral<unsigned>,
                             denormalizeString<unsigned>, mergeForwardValue,
                             extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-ftime-report", ftime_report, Flag,
                                f_Group, INVALID, nullptr, 0, DefaultVis, 0,
                                nullptr, nullptr, nullptr, true, 0,
                                CodeGenOpts.TimePasses, false, false, false,
                                normalizeSimpleFlag, denormalizeSimpleFlag,
                                mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-ftime-report=", ftime_report_EQ, Joined, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0,
    "(For new pass manager) 'per-pass': one report for each pass; "
    "'per-pass-run': one report for each pass invocation",
    nullptr, "per-pass,per-pass-run", true, 0, CodeGenOpts.TimePassesPerRun,
    false, false, false, normalizeSimpleFlag, denormalizeSimpleFlag,
    mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef FRONTEND_OPTION_WITH_MARSHALLING
FRONTEND_OPTION_WITH_MARSHALLING(
    prefix_1, "-ftime-trace-granularity=", ftime_trace_granularity_EQ, Joined,
    f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Minimum time granularity (in microseconds) traced by time profiler",
    nullptr, nullptr, true, 0, FrontendOpts.TimeTraceGranularity, 500u, false,
    500u, normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // FRONTEND_OPTION_WITH_MARSHALLING
#ifdef FRONTEND_OPTION_WITH_MARSHALLING
FRONTEND_OPTION_WITH_MARSHALLING(
    prefix_1, "-ftime-trace=", ftime_trace_EQ, Joined, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Similar to -ftime-trace. Specify the JSON file or a directory which will "
    "contain the JSON file",
    nullptr, nullptr, true, 0, FrontendOpts.TimeTracePath, std::string(), false,
    std::string(), normalizeString, denormalizeString, mergeForwardValue,
    extractForwardValue, -1)
#endif // FRONTEND_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-ftls-model=", ftlsmodel_EQ, Joined, f_Group, INVALID, nullptr,
    0, DefaultVis, 0, nullptr, nullptr,
    "global-dynamic,local-dynamic,initial-exec,local-exec", true, 0,
    CodeGenOpts.DefaultTLSModel, CodeGenOptions::GeneralDynamicTLSModel, false,
    CodeGenOptions::GeneralDynamicTLSModel, normalizeSimpleEnum,
    denormalizeSimpleEnum, mergeForwardValue, extractForwardValue, 12)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-ftrap-function=", ftrap_function_EQ, Joined, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Issue call to specified function rather than a trap instruction", nullptr,
    nullptr, true, 0, CodeGenOpts.TrapFuncName, std::string(), false,
    std::string(), normalizeString, denormalizeString, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-use-init-array", fno_use_init_array, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0,
    "Use .ctors/.dtors instead of .init_array/.fini_array", nullptr, nullptr,
    true, 0, CodeGenOpts.UseInitArray, true, false, true,
    makeBooleanOptionNormalizer(false, true, OPT_fuse_init_array),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fuse-init-array", fuse_init_array, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    CodeGenOpts.UseInitArray, true, false, true,
    makeBooleanOptionNormalizer(true, false, OPT_fno_use_init_array),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fvisibility-from-dllstorageclass",
    fvisibility_from_dllstorageclass, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0,
    "Set the visibility of symbols in the generated code from their DLL "
    "storage class",
    nullptr, nullptr, true, 0, LangOpts->VisibilityFromDLLStorageClass, false,
    false, false,
    makeBooleanOptionNormalizer(true, false,
                                OPT_fno_visibility_from_dllstorageclass),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-visibility-from-dllstorageclass",
    fno_visibility_from_dllstorageclass, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis, 0, "", nullptr, nullptr, true, 0,
    LangOpts->VisibilityFromDLLStorageClass, false, false, false,
    makeBooleanOptionNormalizer(false, true,
                                OPT_fvisibility_from_dllstorageclass),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fvisibility-dllexport=", fvisibility_dllexport_EQ, Joined,
    f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "The visibility for dllexport definitions "
    "[-fvisibility-from-dllstorageclass]",
    nullptr, "default,hidden,internal,protected",
    LangOpts->VisibilityFromDLLStorageClass, 0, LangOpts->DLLExportVisibility,
    DefaultVisibility, false, DefaultVisibility, normalizeSimpleEnum,
    denormalizeSimpleEnum, mergeForwardValue, extractForwardValue, 13)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1,
    "-fvisibility-nodllstorageclass=", fvisibility_nodllstorageclass_EQ, Joined,
    f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "The visibility for definitions without an explicit DLL export class "
    "[-fvisibility-from-dllstorageclass]",
    nullptr, "default,hidden,internal,protected",
    LangOpts->VisibilityFromDLLStorageClass, 0,
    LangOpts->NoDLLStorageClassVisibility, HiddenVisibility, false,
    HiddenVisibility, normalizeSimpleEnum, denormalizeSimpleEnum,
    mergeForwardValue, extractForwardValue, 14)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fvisibility-externs-dllimport=",
                             fvisibility_externs_dllimport_EQ, Joined, f_Group,
                             INVALID, nullptr, 0, DefaultVis, 0,
                             "The visibility for dllimport external "
                             "declarations [-fvisibility-from-dllstorageclass]",
                             nullptr, "default,hidden,internal,protected",
                             LangOpts->VisibilityFromDLLStorageClass, 0,
                             LangOpts->ExternDeclDLLImportVisibility,
                             DefaultVisibility, false, DefaultVisibility,
                             normalizeSimpleEnum, denormalizeSimpleEnum,
                             mergeForwardValue, extractForwardValue, 15)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fvisibility-externs-nodllstorageclass=",
    fvisibility_externs_nodllstorageclass_EQ, Joined, f_Group, INVALID, nullptr,
    0, DefaultVis, 0,
    "The visibility for external declarations without an explicit DLL "
    "dllstorageclass [-fvisibility-from-dllstorageclass]",
    nullptr, "default,hidden,internal,protected",
    LangOpts->VisibilityFromDLLStorageClass, 0,
    LangOpts->ExternDeclNoDLLStorageClassVisibility, HiddenVisibility, false,
    HiddenVisibility, normalizeSimpleEnum, denormalizeSimpleEnum,
    mergeForwardValue, extractForwardValue, 16)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fvisibility=", fvisibility_EQ, Joined, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Set the default symbol visibility for all global definitions", nullptr,
    "default,hidden,internal,protected", true, 0, LangOpts->ValueVisibilityMode,
    DefaultVisibility, false, DefaultVisibility, normalizeSimpleEnum,
    denormalizeSimpleEnum, mergeForwardValue, extractForwardValue, 17)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-mdefault-visibility-export-mapping=",
    mdefault_visibility_export_mapping_EQ, Joined, m_Group, INVALID, nullptr,
    TargetSpecific, DefaultVis | DefaultVis, 0,
    "Mapping between default visibility and export", nullptr,
    "none,explicit,all", true, 0, LangOpts->DefaultVisibilityExportMapping,
    LangOptions::DefaultVisiblityExportMapping::None, false,
    LangOptions::DefaultVisiblityExportMapping::None, normalizeSimpleEnum,
    denormalizeSimpleEnum, mergeForwardValue, extractForwardValue, 18)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fwritable-strings", fwritable_strings,
                             Flag, f_Group, INVALID, nullptr, 0, DefaultVis, 0,
                             "Store string literals as writable data", nullptr,
                             nullptr, true, 0, LangOpts->WritableStrings, false,
                             false, false, normalizeSimpleFlag,
                             denormalizeSimpleFlag, mergeForwardValue,
                             extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-zero-initialized-in-bss", fno_zero_initialized_in_bss, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Don't place zero initialized data in BSS", nullptr, nullptr, true, 0,
    CodeGenOpts.NoZeroInitializedInBSS, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fzero_initialized_in_bss),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fzero-initialized-in-bss", fzero_initialized_in_bss, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    CodeGenOpts.NoZeroInitializedInBSS, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fno_zero_initialized_in_bss),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-ffunction-sections", ffunction_sections, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0,
    "Place each function in its own section", nullptr, nullptr, true, 0,
    CodeGenOpts.FunctionSections, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_function_sections),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-function-sections", fno_function_sections, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    CodeGenOpts.FunctionSections, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_ffunction_sections),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fbasic-block-sections=", fbasic_block_sections_EQ, Joined,
    f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Place each function's basic blocks in unique sections (ELF Only)", nullptr,
    "all,labels,none,list=", true, 0, CodeGenOpts.BBSections, "none", false,
    "none", normalizeString, denormalizeString, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fdata-sections", fdata_sections, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0,
    "Place each data in its own section", nullptr, nullptr, true, 0,
    CodeGenOpts.DataSections, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_data_sections),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-data-sections", fno_data_sections, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    CodeGenOpts.DataSections, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fdata_sections),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fstack-size-section", fstack_size_section, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Emit section containing metadata on function stack sizes", nullptr,
    nullptr, true, 0, CodeGenOpts.StackSizeSection, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_stack_size_section),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-stack-size-section", fno_stack_size_section, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    CodeGenOpts.StackSizeSection, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fstack_size_section),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-stack-usage-file", stack_usage_file, Separate, INVALID, INVALID,
    nullptr, 0, DefaultVis, 0, "Filename (or -) to write stack usage output to",
    nullptr, nullptr, true, 0, CodeGenOpts.StackUsageOutput, std::string(),
    false, std::string(), normalizeString, denormalizeString, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-funique-basic-block-section-names",
    funique_basic_block_section_names, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0,
    "Use unique names for basic block sections (ELF Only)", nullptr, nullptr,
    true, 0, CodeGenOpts.UniqueBasicBlockSectionNames, false, false, false,
    makeBooleanOptionNormalizer(true, false,
                                OPT_fno_unique_basic_block_section_names),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-unique-basic-block-section-names",
    fno_unique_basic_block_section_names, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis, 0, "", nullptr, nullptr, true, 0,
    CodeGenOpts.UniqueBasicBlockSectionNames, false, false, false,
    makeBooleanOptionNormalizer(false, true,
                                OPT_funique_basic_block_section_names),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-funique-internal-linkage-names", funique_internal_linkage_names,
    Flag, f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Uniqueify Internal Linkage Symbol Names by appending the MD5 hash of the "
    "module path",
    nullptr, nullptr, true, 0, CodeGenOpts.UniqueInternalLinkageNames, false,
    false, false,
    makeBooleanOptionNormalizer(true, false,
                                OPT_fno_unique_internal_linkage_names),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-unique-internal-linkage-names",
    fno_unique_internal_linkage_names, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis, 0, "", nullptr, nullptr, true, 0,
    CodeGenOpts.UniqueInternalLinkageNames, false, false, false,
    makeBooleanOptionNormalizer(false, true,
                                OPT_funique_internal_linkage_names),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-unique-section-names", fno_unique_section_names, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Don't use unique names for text and data sections", nullptr, nullptr, true,
    0, CodeGenOpts.UniqueSectionNames, true, false, true,
    makeBooleanOptionNormalizer(false, true, OPT_funique_section_names),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-funique-section-names", funique_section_names, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    CodeGenOpts.UniqueSectionNames, true, false, true,
    makeBooleanOptionNormalizer(true, false, OPT_fno_unique_section_names),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fsplit-machine-functions", fsplit_machine_functions, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Enable late function splitting using profile information (x86 ELF)",
    nullptr, nullptr, true, 0, CodeGenOpts.SplitMachineFunctions, false, false,
    false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_split_machine_functions),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-split-machine-functions", fno_split_machine_functions, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Disable late function splitting using profile information (x86 ELF)",
    nullptr, nullptr, true, 0, CodeGenOpts.SplitMachineFunctions, false, false,
    false,
    makeBooleanOptionNormalizer(false, true, OPT_fsplit_machine_functions),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fzero-call-used-regs=", fzero_call_used_regs_EQ, Joined,
    f_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Clear call-used registers upon function return (AArch64/x86 only)",
    nullptr,
    "skip,used-gpr-arg,used-gpr,used-arg,used,all-gpr-arg,all-gpr,all-arg,all",
    true, 0, CodeGenOpts.ZeroCallUsedRegs,
    llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::Skip, false,
    llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::Skip, normalizeSimpleEnum,
    denormalizeSimpleEnum, mergeForwardValue, extractForwardValue, 19)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fdebug-ranges-base-address", fdebug_ranges_base_address, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Use DWARF base address selection entries in .debug_ranges", nullptr,
    nullptr, true, 0, CodeGenOpts.DebugRangesBaseAddress, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_debug_ranges_base_address),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-debug-ranges-base-address", fno_debug_ranges_base_address,
    Flag, f_Group, INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr,
    true, 0, CodeGenOpts.DebugRangesBaseAddress, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fdebug_ranges_base_address),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-split-dwarf-inlining", fno_split_dwarf_inlining, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0, "", nullptr,
    nullptr, true, 0, CodeGenOpts.SplitDwarfInlining, false, false, true,
    makeBooleanOptionNormalizer(false, true, OPT_fsplit_dwarf_inlining),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fsplit-dwarf-inlining", fsplit_dwarf_inlining, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Provide minimal debug info in the object/executable to facilitate online "
    "symbolication/stack traces in the absence of .dwo/.dwp files when using "
    "Split DWARF",
    nullptr, nullptr, true, 0, CodeGenOpts.SplitDwarfInlining, false, false,
    true,
    makeBooleanOptionNormalizer(true, false, OPT_fno_split_dwarf_inlining),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fforce-dwarf-frame", fforce_dwarf_frame, Flag, f_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0, "Always emit a debug frame section",
    nullptr, nullptr, true, 0, CodeGenOpts.ForceDwarfFrameSection, false, false,
    false, makeBooleanOptionNormalizer(true, false, OPT_fno_force_dwarf_frame),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-force-dwarf-frame", fno_force_dwarf_frame, Flag, f_Group,
    INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    CodeGenOpts.ForceDwarfFrameSection, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fforce_dwarf_frame),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-femit-dwarf-unwind=", femit_dwarf_unwind_EQ, Joined, f_Group,
    INVALID, nullptr, 0, DefaultVis, 0,
    "When to emit DWARF unwind (EH frame) info", nullptr,
    "always,no-compact-unwind,default", true, 0, CodeGenOpts.EmitDwarfUnwind,
    llvm::EmitDwarfUnwindType::Default, false,
    llvm::EmitDwarfUnwindType::Default, normalizeSimpleEnum,
    denormalizeSimpleEnum, mergeForwardValue, extractForwardValue, 20)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-femit-compact-unwind-non-canonical",
    femit_compact_unwind_non_canonical, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0,
    "Try emitting Compact-Unwind for non-canonical entries. Maybe overriden by "
    "other constraints",
    nullptr, nullptr, true, 0, CodeGenOpts.EmitCompactUnwindNonCanonical, false,
    false, false,
    makeBooleanOptionNormalizer(true, false,
                                OPT_fno_emit_compact_unwind_non_canonical),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-emit-compact-unwind-non-canonical",
    fno_emit_compact_unwind_non_canonical, Flag, f_Group, INVALID, nullptr, 0,
    DefaultVis, 0, "", nullptr, nullptr, true, 0,
    CodeGenOpts.EmitCompactUnwindNonCanonical, false, false, false,
    makeBooleanOptionNormalizer(false, true,
                                OPT_femit_compact_unwind_non_canonical),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-gdwarf64", gdwarf64, Flag, g_Group,
                                INVALID, nullptr, 0, DefaultVis, 0,
                                "Enables DWARF64 format for ELF binaries, if "
                                "debug information emission is enabled.",
                                nullptr, nullptr, true, 0, CodeGenOpts.Dwarf64,
                                false, false, false, normalizeSimpleFlag,
                                denormalizeSimpleFlag, mergeForwardValue,
                                extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-gno-inline-line-tables", gno_inline_line_tables, Flag, g_Group,
    INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Don't emit inline line tables.", nullptr, nullptr, true, 0,
    CodeGenOpts.NoInlineLineTables, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_ginline_line_tables),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-ginline-line-tables", ginline_line_tables, Flag, g_Group,
    INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    CodeGenOpts.NoInlineLineTables, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_gno_inline_line_tables),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-gstrict-dwarf", gstrict_dwarf, Flag, g_flags_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0,
    "Restrict DWARF features to those defined in the specified version, "
    "avoiding features from later versions.",
    nullptr, nullptr, true, 0, CodeGenOpts.DebugStrictDwarf, false, false,
    false, makeBooleanOptionNormalizer(true, false, OPT_gno_strict_dwarf),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-gno-strict-dwarf", gno_strict_dwarf, Flag, g_flags_Group,
    INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    CodeGenOpts.DebugStrictDwarf, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_gstrict_dwarf),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-gno-column-info", gno_column_info, Flag, g_flags_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0, "", nullptr, nullptr, true, 0,
    CodeGenOpts.DebugColumnInfo, true, false, true,
    makeBooleanOptionNormalizer(false, true, OPT_gcolumn_info),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-gcolumn-info", gcolumn_info, Flag, g_flags_Group, INVALID,
    nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    CodeGenOpts.DebugColumnInfo, true, false, true,
    makeBooleanOptionNormalizer(true, false, OPT_gno_column_info),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-gsrc-hash=", gsrc_hash_EQ, Joined,
                                g_flags_Group, INVALID, nullptr, 0, DefaultVis,
                                0, nullptr, nullptr, "md5,sha1,sha256", true, 0,
                                CodeGenOpts.DebugSrcHash,
                                CodeGenOptions::DSH_MD5, false,
                                CodeGenOptions::DSH_MD5, normalizeSimpleEnum,
                                denormalizeSimpleEnum, mergeForwardValue,
                                extractForwardValue, 21)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-gembed-source", gembed_source, Flag,
                                g_flags_Group, INVALID, nullptr, 0, DefaultVis,
                                0, "Embed source text in DWARF debug sections",
                                nullptr, nullptr, true, 0,
                                CodeGenOpts.EmbedSource, false, false, false,
                                normalizeSimpleFlag, denormalizeSimpleFlag,
                                mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef FRONTEND_OPTION_WITH_MARSHALLING
FRONTEND_OPTION_WITH_MARSHALLING(prefix_2, "-help", help, Flag, INVALID,
                                 INVALID, nullptr, 0, DefaultVis, 0,
                                 "Display available options", nullptr, nullptr,
                                 true, 0, FrontendOpts.ShowHelp, false, false,
                                 false, normalizeSimpleFlag,
                                 denormalizeSimpleFlag, mergeForwardValue,
                                 extractForwardValue, -1)
#endif // FRONTEND_OPTION_WITH_MARSHALLING
#ifdef PREPROCESSOR_OPTION_WITH_MARSHALLING
PREPROCESSOR_OPTION_WITH_MARSHALLING(
    prefix_2, "-imacros", imacros, JoinedOrSeparate, neverc_i_Group, INVALID,
    nullptr, 0, DefaultVis, 0, "Include macros from file before parsing",
    "<file>", nullptr, true, 0, PPO->MacroIncludes,
    std::vector<std::string>({}), false, std::vector<std::string>({}),
    normalizeStringVector, denormalizeStringVector, mergeForwardValue,
    extractForwardValue, -1)
#endif // PREPROCESSOR_OPTION_WITH_MARSHALLING
#ifdef HEADER_SEARCH_OPTION_WITH_MARSHALLING
HEADER_SEARCH_OPTION_WITH_MARSHALLING(
    prefix_1, "-isysroot", isysroot, JoinedOrSeparate, neverc_i_Group, INVALID,
    nullptr, 0, DefaultVis, 0, "Set the system root directory (usually /)",
    "<dir>", nullptr, true, 0, HeaderIdxOpts->Sysroot, "/", false, "/",
    normalizeString, denormalizeString, mergeForwardValue, extractForwardValue,
    -1)
#endif // HEADER_SEARCH_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-mdouble=", mdouble_EQ, Joined, m_Group,
                             INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
                             "Force double to be <n> bits", "<n", "32,64", true,
                             0, LangOpts->DoubleSize, 0, false, 0,
                             normalizeStringIntegral<unsigned>,
                             denormalizeString<unsigned>, mergeForwardValue,
                             extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-inline-asm=", inline_asm_EQ, Joined, m_Group, INVALID, nullptr,
    0, DefaultVis | DefaultVis, 0, nullptr, nullptr, "att,intel", true, 0,
    CodeGenOpts.InlineAsmDialect, CodeGenOptions::IAD_ATT, false,
    CodeGenOptions::IAD_ATT, normalizeSimpleEnum, denormalizeSimpleEnum,
    mergeForwardValue, extractForwardValue, 22)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef TARGET_OPTION_WITH_MARSHALLING
TARGET_OPTION_WITH_MARSHALLING(prefix_1, "-mcmodel=", mcmodel_EQ, Joined,
                               m_Group, INVALID, nullptr, 0,
                               DefaultVis | DefaultVis, 0, nullptr, nullptr,
                               nullptr, true, 0, TargetOpts->CodeModel,
                               "default", false, "default", normalizeString,
                               denormalizeString, mergeForwardValue,
                               extractForwardValue, -1)
#endif // TARGET_OPTION_WITH_MARSHALLING
#ifdef TARGET_OPTION_WITH_MARSHALLING
TARGET_OPTION_WITH_MARSHALLING(
    prefix_1, "-mlarge-data-threshold=", mlarge_data_threshold_EQ, Joined,
    m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0, nullptr, nullptr,
    nullptr, true, 0, TargetOpts->LargeDataThreshold, 65535, false, 65535,
    normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // TARGET_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-mtls-size=", mtls_size_EQ, Joined, m_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0,
    "Specify bit size of immediate TLS offsets (AArch64 ELF only): 12 (for "
    "4KB) | 24 (for 16MB, default) | 32 (for 4GB) | 48 (for 256TB, needs "
    "-mcmodel=large)",
    nullptr, nullptr, true, 0, CodeGenOpts.TLSSize, 0, false, 0,
    normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-malign-double", malign_double, Flag,
                             m_Group, INVALID, nullptr, 0,
                             DefaultVis | DefaultVis, 0,
                             "Align doubles to two words in structs (x86 only)",
                             nullptr, nullptr, true, 0, LangOpts->AlignDouble,
                             false, false, false, normalizeSimpleFlag,
                             denormalizeSimpleFlag, mergeForwardValue,
                             extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef FRONTEND_OPTION_WITH_MARSHALLING
FRONTEND_OPTION_WITH_MARSHALLING(
    prefix_1, "-mllvm", mllvm, Separate, INVALID, INVALID, nullptr, HelpHidden,
    DefaultVis, 0,
    "Additional arguments to forward to LLVM's option processing", nullptr,
    nullptr, true, 0, FrontendOpts.LLVMArgs, std::vector<std::string>({}),
    false, std::vector<std::string>({}), normalizeStringVector,
    denormalizeStringVector, mergeForwardValue, extractForwardValue, -1)
#endif // FRONTEND_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-mms-bitfields", mms_bitfields, Flag,
                             m_Group, INVALID, nullptr, 0,
                             DefaultVis | DefaultVis, 0,
                             "Set the default structure layout to be "
                             "compatible with the Microsoft compiler standard",
                             nullptr, nullptr, true, 0, LangOpts->MSBitfields,
                             false, false, false, normalizeSimpleFlag,
                             denormalizeSimpleFlag, mergeForwardValue,
                             extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-mskip-rax-setup", mskip_rax_setup, Flag, m_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0,
    "Skip setting up RAX register when passing variable arguments (x86 only)",
    nullptr, nullptr, true, 0, CodeGenOpts.SkipRaxSetup, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-mstackrealign", mstackrealign, Flag, m_Group, INVALID, nullptr,
    0, DefaultVis | DefaultVis, 0,
    "Force realign the stack at entry to every function", nullptr, nullptr,
    true, 0, CodeGenOpts.StackRealignment, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-mstack-alignment=", mstack_alignment, Joined, m_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0, "Set the stack alignment", nullptr,
    nullptr, true, 0, CodeGenOpts.StackAlignment, 0, false, 0,
    normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-mstack-probe-size=", mstack_probe_size, Joined, m_Group,
    INVALID, nullptr, 0, DefaultVis | DefaultVis, 0, "Set the stack probe size",
    nullptr, nullptr, true, 0, CodeGenOpts.StackProbeSize, 4096, false, 4096,
    normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-mno-stack-arg-probe", mno_stack_arg_probe, Flag, m_Group,
    INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Disable stack probes which are enabled by default", nullptr, nullptr, true,
    0, CodeGenOpts.NoStackArgProbe, false, false, false, normalizeSimpleFlag,
    denormalizeSimpleFlag, mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-mthread-model", mthread_model, Separate, m_Group, INVALID,
    nullptr, 0, DefaultVis | DefaultVis, 0,
    "The thread model to use. Defaults to 'posix')", nullptr, "posix,single",
    true, 0, LangOpts->ThreadModel, LangOptions::ThreadModelKind::POSIX, false,
    LangOptions::ThreadModelKind::POSIX, normalizeSimpleEnum,
    denormalizeSimpleEnum, mergeForwardValue, extractForwardValue, 23)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-mno-tls-direct-seg-refs", mno_tls_direct_seg_refs, Flag,
    m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Disable direct TLS access through segment registers", nullptr, nullptr,
    true, 0, CodeGenOpts.IndirectTlsSegRefs, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-mspeculative-load-hardening", mspeculative_load_hardening, Flag,
    m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis | DefaultVis, 0, "",
    nullptr, nullptr, true, 0, CodeGenOpts.SpeculativeLoadHardening, false,
    false, false,
    makeBooleanOptionNormalizer(true, false,
                                OPT_mno_speculative_load_hardening),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-mno-speculative-load-hardening", mno_speculative_load_hardening,
    Flag, m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0, "", nullptr,
    nullptr, true, 0, CodeGenOpts.SpeculativeLoadHardening, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_mspeculative_load_hardening),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-mvscale-min=", mvscale_min_EQ, Joined, INVALID, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Specify the vscale minimum. Defaults to \"1\". (AArch64 only)", nullptr,
    nullptr, true, 0, LangOpts->VScaleMin, 0, false, 0,
    normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-mvscale-max=", mvscale_max_EQ, Joined, INVALID, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Specify the vscale maximum. Defaults to the vector length agnostic value "
    "of \"0\". (AArch64 only)",
    nullptr, nullptr, true, 0, LangOpts->VScaleMax, 0, false, 0,
    normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-mrelax-all", mrelax_all, Flag,
                                m_Group, INVALID, nullptr, 0,
                                DefaultVis | DefaultVis, 0,
                                "Relax all machine instructions", nullptr,
                                nullptr, true, 0, CodeGenOpts.RelaxAll, false,
                                false, false, normalizeSimpleFlag,
                                denormalizeSimpleFlag, mergeForwardValue,
                                extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-mincremental-linker-compatible", mincremental_linker_compatible,
    Flag, m_Group, INVALID, nullptr, HelpHidden, DefaultVis | DefaultVis, 0,
    "Emit an object file which can be used with an incremental linker", nullptr,
    nullptr, true, 0, CodeGenOpts.IncrementalLinkerCompatible, false, false,
    false, normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-msoft-float", msoft_float, Flag, m_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0, "Use software floating point", nullptr, nullptr,
    true, 0, CodeGenOpts.SoftFloat, false, false, false, normalizeSimpleFlag,
    denormalizeSimpleFlag, mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-mrecip=", mrecip_EQ, CommaJoined, m_Group, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0,
    "Control use of approximate reciprocal and reciprocal square root "
    "instructions followed by <n> iterations of Newton-Raphson refinement. "
    "<value> = ( ['!'] ['vec-'] ('rcp'|'sqrt') [('h'|'s'|'d')] [':'<n>] ) | "
    "'all' | 'default' | 'none'",
    nullptr, nullptr, true, 0, CodeGenOpts.Reciprocals,
    std::vector<std::string>({}), false, std::vector<std::string>({}),
    normalizeStringVector, denormalizeStringVector, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-mprefer-vector-width=", mprefer_vector_width_EQ, Joined,
    m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Specifies preferred vector width for auto-vectorization. Defaults to "
    "'none' which allows target specific decisions.",
    nullptr, nullptr, true, 0, CodeGenOpts.PreferVectorWidth, std::string(),
    false, std::string(), normalizeString, denormalizeString, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-mstack-protector-guard=", mstack_protector_guard_EQ, Joined,
    m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Use the given guard (global, tls) for addressing the stack-protector "
    "guard",
    nullptr, nullptr, true, 0, CodeGenOpts.StackProtectorGuard, std::string(),
    false, std::string(), normalizeString, denormalizeString, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1,
    "-mstack-protector-guard-offset=", mstack_protector_guard_offset_EQ, Joined,
    m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Use the given offset for addressing the stack-protector guard", nullptr,
    nullptr, true, 0, CodeGenOpts.StackProtectorGuardOffset, INT_MAX, false,
    INT_MAX, normalizeStringIntegral<int>, denormalizeString<int>,
    mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1,
    "-mstack-protector-guard-symbol=", mstack_protector_guard_symbol_EQ, Joined,
    m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Use the given symbol for addressing the stack-protector guard", nullptr,
    nullptr, true, 0, CodeGenOpts.StackProtectorGuardSymbol, std::string(),
    false, std::string(), normalizeString, denormalizeString, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-mstack-protector-guard-reg=", mstack_protector_guard_reg_EQ,
    Joined, m_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0,
    "Use the given reg for addressing the stack-protector guard", nullptr,
    nullptr, true, 0, CodeGenOpts.StackProtectorGuardReg, std::string(), false,
    std::string(), normalizeString, denormalizeString, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef HEADER_SEARCH_OPTION_WITH_MARSHALLING
HEADER_SEARCH_OPTION_WITH_MARSHALLING(
    prefix_1, "-nobuiltininc", nobuiltininc, Flag, IncludePath_Group, INVALID,
    nullptr, 0, DefaultVis, 0, "Disable builtin #include directories", nullptr,
    nullptr, true, 0, HeaderIdxOpts->UseBuiltinIncludes, true, false, true,
    normalizeSimpleNegativeFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // HEADER_SEARCH_OPTION_WITH_MARSHALLING
#ifdef FRONTEND_OPTION_WITH_MARSHALLING
FRONTEND_OPTION_WITH_MARSHALLING(prefix_1, "-o", o, JoinedOrSeparate, INVALID,
                                 INVALID, nullptr, NoXarchOption, DefaultVis, 0,
                                 "Write output to <file>", "<file>", nullptr,
                                 true, 0, FrontendOpts.OutputFile,
                                 std::string(), false, std::string(),
                                 normalizeString, denormalizeString,
                                 mergeForwardValue, extractForwardValue, -1)
#endif // FRONTEND_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-object-file-name=", object_file_name_EQ, Joined, INVALID,
    INVALID, nullptr, 0, DefaultVis, 0, "Set the output <file> for debug infos",
    "<file>", nullptr, true, 0, CodeGenOpts.ObjectFilenameForDebug,
    std::string(), false, std::string(), normalizeString, denormalizeString,
    mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(prefix_2, "-pedantic-errors", pedantic_errors,
                             Flag, pedantic_Group, INVALID, nullptr, 0,
                             DefaultVis, 0, nullptr, nullptr, nullptr, true, 0,
                             DiagnosticOpts->PedanticErrors, false, false,
                             false, normalizeSimpleFlag, denormalizeSimpleFlag,
                             mergeForwardValue, extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(prefix_2, "-pedantic", pedantic, Flag,
                             pedantic_Group, INVALID, nullptr, 0, DefaultVis, 0,
                             "Warn on language extensions", nullptr, nullptr,
                             true, 0, DiagnosticOpts->Pedantic, false, false,
                             false, normalizeSimpleFlag, denormalizeSimpleFlag,
                             mergeForwardValue, extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-pthread", pthread, Flag, INVALID, INVALID, nullptr, 0,
    DefaultVis | DefaultVis, 0, "Support POSIX threads in generated code",
    nullptr, nullptr, true, 0, LangOpts->POSIXThreads, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_no_pthread),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-no-pthread", no_pthread, Flag, INVALID, INVALID, nullptr, 0,
    DefaultVis, 0, "", nullptr, nullptr, true, 0, LangOpts->POSIXThreads, false,
    false, false, makeBooleanOptionNormalizer(false, true, OPT_pthread),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef HEADER_SEARCH_OPTION_WITH_MARSHALLING
HEADER_SEARCH_OPTION_WITH_MARSHALLING(
    prefix_1, "-resource-dir", resource_dir, Separate, INVALID, INVALID,
    nullptr, NoXarchOption | HelpHidden, DefaultVis, 0,
    "The directory which holds the compiler resource files", nullptr, nullptr,
    true, 0, HeaderIdxOpts->ResourceDir, std::string(), false, std::string(),
    normalizeString, denormalizeString, mergeForwardValue, extractForwardValue,
    -1)
#endif // HEADER_SEARCH_OPTION_WITH_MARSHALLING
#ifdef FRONTEND_OPTION_WITH_MARSHALLING
FRONTEND_OPTION_WITH_MARSHALLING(
    prefix_2, "-print-supported-cpus", print_supported_cpus, Flag,
    CompileOnly_Group, INVALID, nullptr, 0, DefaultVis, 0,
    "Print supported cpu models for the given target (if target is not "
    "specified, it will print the supported cpus for the default target)",
    nullptr, nullptr, true, 0, FrontendOpts.PrintSupportedCPUs, false, false,
    false, normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // FRONTEND_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_2, "-traditional-cpp", traditional_cpp,
                             Flag, INVALID, INVALID, nullptr, 0, DefaultVis, 0,
                             "Enable some traditional CPP emulation", nullptr,
                             nullptr, true, 0, LangOpts->TraditionalCPP, false,
                             false, false, normalizeSimpleFlag,
                             denormalizeSimpleFlag, mergeForwardValue,
                             extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef PREPROCESSOR_OPTION_WITH_MARSHALLING
PREPROCESSOR_OPTION_WITH_MARSHALLING(prefix_1, "-undef", undef, Flag, u_Group,
                                     INVALID, nullptr, 0, DefaultVis, 0,
                                     "undef all system defines", nullptr,
                                     nullptr, true, 0, PPO->UsePredefines, true,
                                     false, true, normalizeSimpleNegativeFlag,
                                     denormalizeSimpleFlag, mergeForwardValue,
                                     extractForwardValue, -1)
#endif // PREPROCESSOR_OPTION_WITH_MARSHALLING
#ifdef HEADER_SEARCH_OPTION_WITH_MARSHALLING
HEADER_SEARCH_OPTION_WITH_MARSHALLING(
    prefix_1, "-v", v, Flag, INVALID, INVALID, nullptr, 0, DefaultVis, 0,
    "Show commands to run and use verbose output", nullptr, nullptr, true, 0,
    HeaderIdxOpts->Verbose, false, false, false, normalizeSimpleFlag,
    denormalizeSimpleFlag, mergeForwardValue, extractForwardValue, -1)
#endif // HEADER_SEARCH_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(prefix_1, "-w", w, Flag, INVALID, INVALID, nullptr,
                             0, DefaultVis, 0, "Suppress all warnings", nullptr,
                             nullptr, true, 0, DiagnosticOpts->IgnoreWarnings,
                             false, false, false, normalizeSimpleFlag,
                             denormalizeSimpleFlag, mergeForwardValue,
                             extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-integrated-as", fno_integrated_as, Flag, f_Group, INVALID,
    nullptr, HelpHidden, DefaultVis | DefaultVis, 0,
    "Disable the integrated assembler", nullptr, nullptr, true, 0,
    CodeGenOpts.DisableIntegratedAS, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fintegrated_as),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fintegrated-as", fintegrated_as, Flag, f_Group, INVALID,
    nullptr, HelpHidden, DefaultVis | DefaultVis, 0,
    "Enable the integrated assembler", nullptr, nullptr, true, 0,
    CodeGenOpts.DisableIntegratedAS, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fno_integrated_as),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef FILE_SYSTEM_OPTION_WITH_MARSHALLING
FILE_SYSTEM_OPTION_WITH_MARSHALLING(
    prefix_1, "-working-directory", working_directory, Separate, INVALID,
    INVALID, nullptr, 0, DefaultVis, 0,
    "Resolve file paths relative to the specified directory", nullptr, nullptr,
    true, 0, FileSystemOpts.WorkingDir, std::string(), false, std::string(),
    normalizeString, denormalizeString, mergeForwardValue, extractForwardValue,
    -1)
#endif // FILE_SYSTEM_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fsemantic-interposition", fsemantic_interposition, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis | DefaultVis, 0, "", nullptr,
    nullptr, true, 0, LangOpts->SemanticInterposition, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_semantic_interposition),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-semantic-interposition", fno_semantic_interposition, Flag,
    f_Group, INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    LangOpts->SemanticInterposition, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fsemantic_interposition),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef TARGET_OPTION_WITH_MARSHALLING
TARGET_OPTION_WITH_MARSHALLING(prefix_1, "-tune-cpu", tune_cpu, Separate,
                               INVALID, INVALID, nullptr, HelpHidden,
                               DefaultVis, 0, "Tune for a specific cpu type",
                               nullptr, nullptr, true, 0, TargetOpts->TuneCPU,
                               std::string(), false, std::string(),
                               normalizeString, denormalizeString,
                               mergeForwardValue, extractForwardValue, -1)
#endif // TARGET_OPTION_WITH_MARSHALLING
#ifdef TARGET_OPTION_WITH_MARSHALLING
TARGET_OPTION_WITH_MARSHALLING(prefix_1, "-target-abi", target_abi, Separate,
                               INVALID, INVALID, nullptr, HelpHidden,
                               DefaultVis, 0, "Target a particular ABI type",
                               nullptr, nullptr, true, 0, TargetOpts->ABI,
                               std::string(), false, std::string(),
                               normalizeString, denormalizeString,
                               mergeForwardValue, extractForwardValue, -1)
#endif // TARGET_OPTION_WITH_MARSHALLING
#ifdef TARGET_OPTION_WITH_MARSHALLING
TARGET_OPTION_WITH_MARSHALLING(prefix_1, "-target-cpu", target_cpu, Separate,
                               INVALID, INVALID, nullptr, HelpHidden,
                               DefaultVis, 0, "Target a specific cpu type",
                               nullptr, nullptr, true, 0, TargetOpts->CPU,
                               std::string(), false, std::string(),
                               normalizeString, denormalizeString,
                               mergeForwardValue, extractForwardValue, -1)
#endif // TARGET_OPTION_WITH_MARSHALLING
#ifdef TARGET_OPTION_WITH_MARSHALLING
TARGET_OPTION_WITH_MARSHALLING(prefix_1, "-target-feature", target_feature,
                               Separate, INVALID, INVALID, nullptr, HelpHidden,
                               DefaultVis, 0, "Target specific attributes",
                               nullptr, nullptr, true, 0,
                               TargetOpts->FeaturesAsWritten,
                               std::vector<std::string>({}), false,
                               std::vector<std::string>({}),
                               normalizeStringVector, denormalizeStringVector,
                               mergeForwardValue, extractForwardValue, -1)
#endif // TARGET_OPTION_WITH_MARSHALLING
#ifdef TARGET_OPTION_WITH_MARSHALLING
TARGET_OPTION_WITH_MARSHALLING(
    prefix_1, "-triple", triple, Separate, INVALID, INVALID, nullptr,
    HelpHidden, DefaultVis, 0,
    "Specify target triple (e.g. x86_64-apple-darwin)", nullptr, nullptr, true,
    1, TargetOpts->Triple,
    llvm::Triple::normalize(llvm::sys::getDefaultTargetTriple()), false,
    llvm::Triple::normalize(llvm::sys::getDefaultTargetTriple()),
    normalizeTriple, denormalizeString, mergeForwardValue, extractForwardValue,
    -1)
#endif // TARGET_OPTION_WITH_MARSHALLING
#ifdef TARGET_OPTION_WITH_MARSHALLING
TARGET_OPTION_WITH_MARSHALLING(prefix_1, "-target-linker-version",
                               target_linker_version, Separate, INVALID,
                               INVALID, nullptr, HelpHidden, DefaultVis, 0,
                               "Target linker version", nullptr, nullptr, true,
                               0, TargetOpts->LinkerVersion, std::string(),
                               false, std::string(), normalizeString,
                               denormalizeString, mergeForwardValue,
                               extractForwardValue, -1)
#endif // TARGET_OPTION_WITH_MARSHALLING
#ifdef TARGET_OPTION_WITH_MARSHALLING
TARGET_OPTION_WITH_MARSHALLING(prefix_1, "-mfpmath", mfpmath, Separate, INVALID,
                               INVALID, nullptr, HelpHidden, DefaultVis, 0,
                               "Which unit to use for fp math", nullptr,
                               nullptr, true, 0, TargetOpts->FPMath,
                               std::string(), false, std::string(),
                               normalizeString, denormalizeString,
                               mergeForwardValue, extractForwardValue, -1)
#endif // TARGET_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fpadding-on-unsigned-fixed-point",
    fpadding_on_unsigned_fixed_point, Flag, INVALID, INVALID, nullptr,
    HelpHidden, DefaultVis, 0,
    "Force each unsigned fixed point type to have an extra bit of padding to "
    "align their scales with those of signed fixed point types",
    nullptr, nullptr, LangOpts->FixedPoint, 0,
    LangOpts->PaddingOnUnsignedFixedPoint, false, false, false,
    makeBooleanOptionNormalizer(true, false,
                                OPT_fno_padding_on_unsigned_fixed_point),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-padding-on-unsigned-fixed-point",
    fno_padding_on_unsigned_fixed_point, Flag, INVALID, INVALID, nullptr,
    HelpHidden, DefaultVis, 0, "", nullptr, nullptr, LangOpts->FixedPoint, 0,
    LangOpts->PaddingOnUnsignedFixedPoint, false, false, false,
    makeBooleanOptionNormalizer(false, true,
                                OPT_fpadding_on_unsigned_fixed_point),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-default-function-attr", default_function_attr, Separate,
    INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "Apply given attribute to all functions", nullptr, nullptr, true, 0,
    CodeGenOpts.DefaultFunctionAttrs, std::vector<std::string>({}), false,
    std::vector<std::string>({}), normalizeStringVector,
    denormalizeStringVector, mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-dwarf-version=", dwarf_version_EQ,
                                Joined, INVALID, INVALID, nullptr, HelpHidden,
                                DefaultVis, 0, nullptr, nullptr, nullptr, true,
                                0, CodeGenOpts.DwarfVersion, 0, false, 0,
                                normalizeStringIntegral<unsigned>,
                                denormalizeString<unsigned>, mergeForwardValue,
                                extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-debugger-tuning=", debugger_tuning_EQ, Joined, INVALID, INVALID,
    nullptr, HelpHidden, DefaultVis, 0, nullptr, nullptr, "gdb,lldb", true, 0,
    CodeGenOpts.DebuggerTuning, llvm::DebuggerKind::Default, false,
    llvm::DebuggerKind::Default, normalizeSimpleEnum, denormalizeSimpleEnum,
    mergeForwardValue, extractForwardValue, 24)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-dwarf-debug-flags", dwarf_debug_flags, Separate, INVALID,
    INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "The string to embed in the Dwarf debug flags record.", nullptr, nullptr,
    true, 0, CodeGenOpts.DwarfDebugFlags, std::string(), false, std::string(),
    normalizeString, denormalizeString, mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-record-command-line", record_command_line, Separate, INVALID,
    INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "The string to embed in the .LLVM.command.line section.", nullptr, nullptr,
    true, 0, CodeGenOpts.RecordCommandLine, std::string(), false, std::string(),
    normalizeString, denormalizeString, mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_2, "-compress-debug-sections=", compress_debug_sections_EQ, Joined,
    INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "DWARF debug sections compression type", nullptr, "none,zlib,zstd", true, 0,
    CodeGenOpts.CompressDebugSections, llvm::DebugCompressionType::None, false,
    llvm::DebugCompressionType::None, normalizeSimpleEnum,
    denormalizeSimpleEnum, mergeForwardValue, extractForwardValue, 25)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-mnoexecstack", mno_exec_stack, Flag, INVALID, INVALID, nullptr,
    HelpHidden, DefaultVis, 0,
    "Mark the file as not needing an executable stack", nullptr, nullptr, true,
    0, CodeGenOpts.NoExecStack, false, false, false, normalizeSimpleFlag,
    denormalizeSimpleFlag, mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-massembler-no-warn",
                                massembler_no_warn, Flag, INVALID, INVALID,
                                nullptr, HelpHidden, DefaultVis, 0,
                                "Make assembler not emit warnings", nullptr,
                                nullptr, true, 0, CodeGenOpts.NoWarn, false,
                                false, false, normalizeSimpleFlag,
                                denormalizeSimpleFlag, mergeForwardValue,
                                extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-massembler-fatal-warnings",
                                massembler_fatal_warnings, Flag, INVALID,
                                INVALID, nullptr, HelpHidden, DefaultVis, 0,
                                "Make assembler warnings fatal", nullptr,
                                nullptr, true, 0, CodeGenOpts.FatalWarnings,
                                false, false, false, normalizeSimpleFlag,
                                denormalizeSimpleFlag, mergeForwardValue,
                                extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-mrelax-relocations=no",
                                mrelax_relocations_no, Flag, INVALID, INVALID,
                                nullptr, HelpHidden, DefaultVis, 0,
                                "Disable x86 relax relocations", nullptr,
                                nullptr, true, 0,
                                CodeGenOpts.RelaxELFRelocations, true, false,
                                true, normalizeSimpleNegativeFlag,
                                denormalizeSimpleFlag, mergeForwardValue,
                                extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-msave-temp-labels", msave_temp_labels, Flag, INVALID, INVALID,
    nullptr, HelpHidden, DefaultVis, 0,
    "Save temporary labels in the symbol table. Note this may change .s "
    "semantics and shouldn't generally be used on compiler-generated code.",
    nullptr, nullptr, true, 0, CodeGenOpts.SaveTempLabels, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-math-builtin", fno_math_builtin, Flag, INVALID, INVALID,
    nullptr, HelpHidden, DefaultVis, 0,
    "Disable implicit builtin knowledge of math functions", nullptr, nullptr,
    true, 0, LangOpts->NoMathBuiltin, false, false, false, normalizeSimpleFlag,
    denormalizeSimpleFlag, mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-as-secure-log-file", as_secure_log_file, Separate, INVALID,
    INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "Emit .secure_log_unique directives to this filename.", nullptr, nullptr,
    true, 0, CodeGenOpts.AsSecureLogFile, std::string(), false, std::string(),
    normalizeString, denormalizeString, mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-llvm-verify-each", llvm_verify_each,
                                Flag, INVALID, INVALID, nullptr, HelpHidden,
                                DefaultVis, 0,
                                "Run the LLVM verifier after every LLVM pass",
                                nullptr, nullptr, true, 0,
                                CodeGenOpts.VerifyEach, false, false, false,
                                normalizeSimpleFlag, denormalizeSimpleFlag,
                                mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-disable-llvm-verifier",
                                disable_llvm_verifier, Flag, INVALID, INVALID,
                                nullptr, HelpHidden, DefaultVis, 0,
                                "Don't run the LLVM IR verifier pass", nullptr,
                                nullptr, true, 0, CodeGenOpts.VerifyModule,
                                true, false, true, normalizeSimpleNegativeFlag,
                                denormalizeSimpleFlag, mergeForwardValue,
                                extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-disable-llvm-passes", disable_llvm_passes, Flag, INVALID,
    INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "Use together with -emit-llvm to get pristine LLVM IR from the frontend by "
    "not running any LLVM passes at all",
    nullptr, nullptr, true, 0, CodeGenOpts.DisableLLVMPasses, false, false,
    false, normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-disable-lifetime-markers", disable_lifetimemarkers, Flag,
    INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "Disable lifetime-markers emission even when optimizations are enabled",
    nullptr, nullptr, true, 0, CodeGenOpts.DisableLifetimeMarkers, false, false,
    false, normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-disable-O0-optnone", disable_O0_optnone, Flag, INVALID, INVALID,
    nullptr, HelpHidden, DefaultVis, 0,
    "Disable adding the optnone attribute to functions at O0", nullptr, nullptr,
    true, 0, CodeGenOpts.DisableO0ImplyOptNone, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-disable-red-zone", disable_red_zone,
                                Flag, INVALID, INVALID, nullptr, HelpHidden,
                                DefaultVis, 0,
                                "Do not emit code that uses the red zone.",
                                nullptr, nullptr, true, 0,
                                CodeGenOpts.DisableRedZone, false, false, false,
                                normalizeSimpleFlag, denormalizeSimpleFlag,
                                mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-no-implicit-float", no_implicit_float, Flag, INVALID, INVALID,
    nullptr, HelpHidden, DefaultVis, 0,
    "Don't generate implicit floating point or vector instructions", nullptr,
    nullptr, true, 0, CodeGenOpts.NoImplicitFloat, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-relaxed-aliasing", relaxed_aliasing,
                                Flag, INVALID, INVALID, nullptr, HelpHidden,
                                DefaultVis, 0,
                                "Turn off Type Based Alias Analysis", nullptr,
                                nullptr, true, 0, CodeGenOpts.RelaxedAliasing,
                                false, false, false, normalizeSimpleFlag,
                                denormalizeSimpleFlag, mergeForwardValue,
                                extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-no-struct-path-tbaa", no_struct_path_tbaa, Flag, INVALID,
    INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "Turn off struct-path aware Type Based Alias Analysis", nullptr, nullptr,
    true, 0, CodeGenOpts.StructPathTBAA, true, false, true,
    normalizeSimpleNegativeFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-mdebug-pass", mdebug_pass, Separate,
                                INVALID, INVALID, nullptr, HelpHidden,
                                DefaultVis, 0, "Enable additional debug output",
                                nullptr, nullptr, true, 0,
                                CodeGenOpts.DebugPass, std::string(), false,
                                std::string(), normalizeString,
                                denormalizeString, mergeForwardValue,
                                extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-mfloat-abi", mfloat_abi, Separate,
                                INVALID, INVALID, nullptr, HelpHidden,
                                DefaultVis, 0, "The float ABI to use", nullptr,
                                nullptr, true, 0, CodeGenOpts.FloatABI,
                                std::string(), false, std::string(),
                                normalizeString, denormalizeString,
                                mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-mlimit-float-precision",
                                mlimit_float_precision, Separate, INVALID,
                                INVALID, nullptr, HelpHidden, DefaultVis, 0,
                                "Limit float precision to the given value",
                                nullptr, nullptr, true, 0,
                                CodeGenOpts.LimitFloatPrecision, std::string(),
                                false, std::string(), normalizeString,
                                denormalizeString, mergeForwardValue,
                                extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-funwind-tables=", funwind_tables_EQ, Joined, INVALID, INVALID,
    nullptr, HelpHidden, DefaultVis, 0,
    "Generate unwinding tables for all functions", nullptr, nullptr, true, 0,
    CodeGenOpts.UnwindTables, 0, false, 0, normalizeStringIntegral<unsigned>,
    denormalizeString<unsigned>, mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-vectorize-loops", vectorize_loops,
                                Flag, INVALID, INVALID, nullptr, HelpHidden,
                                DefaultVis, 0,
                                "Run the Loop vectorization passes", nullptr,
                                nullptr, true, 0, CodeGenOpts.VectorizeLoop,
                                false, false, false, normalizeSimpleFlag,
                                denormalizeSimpleFlag, mergeForwardValue,
                                extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-vectorize-slp", vectorize_slp, Flag,
                                INVALID, INVALID, nullptr, HelpHidden,
                                DefaultVis, 0,
                                "Run the SLP vectorization passes", nullptr,
                                nullptr, true, 0, CodeGenOpts.VectorizeSLP,
                                false, false, false, normalizeSimpleFlag,
                                denormalizeSimpleFlag, mergeForwardValue,
                                extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_3, "--linker-option=", linker_option,
                                Joined, INVALID, INVALID, nullptr, HelpHidden,
                                DefaultVis, 0, "Add linker option", nullptr,
                                nullptr, true, 0, CodeGenOpts.LinkerOptions,
                                std::vector<std::string>({}), false,
                                std::vector<std::string>({}),
                                normalizeStringVector, denormalizeStringVector,
                                mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1,
    "-fpatchable-function-entry-offset=", fpatchable_function_entry_offset_EQ,
    Joined, INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "Generate M NOPs before function entry", "<M>", nullptr, true, 0,
    CodeGenOpts.PatchableFunctionEntryOffset, 0, false, 0,
    normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fverify-debuginfo-preserve", fverify_debuginfo_preserve, Flag,
    INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "Enable Debug Info Metadata preservation testing in optimizations.",
    nullptr, nullptr, true, 0, CodeGenOpts.EnableDIPreservationVerify, false,
    false, false, normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1,
    "-fverify-debuginfo-preserve-export=", fverify_debuginfo_preserve_export,
    Joined, INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "Export debug info (by testing original Debug Info) failures into "
    "specified (JSON) file (should be abs path as we use append mode to insert "
    "new JSON objects).",
    "<file>", nullptr, true, 0, CodeGenOpts.DIBugsReportFilePath, std::string(),
    false, std::string(), normalizeString, denormalizeString, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1,
                                "-fwarn-stack-size=", fwarn_stack_size_EQ,
                                Joined, INVALID, INVALID, nullptr, HelpHidden,
                                DefaultVis, 0, nullptr, nullptr, nullptr, true,
                                0, CodeGenOpts.WarnStackSize, UINT_MAX, false,
                                UINT_MAX, normalizeStringIntegral<unsigned>,
                                denormalizeString<unsigned>, mergeForwardValue,
                                extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-mbranch-target-enforce",
                             mbranch_target_enforce, Flag, INVALID, INVALID,
                             nullptr, HelpHidden, DefaultVis, 0, nullptr,
                             nullptr, nullptr, true, 0,
                             LangOpts->BranchTargetEnforcement, false, false,
                             false, normalizeSimpleFlag, denormalizeSimpleFlag,
                             mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-mbranch-protection-pauth-lr",
                             mbranch_protection_pauth_lr, Flag, INVALID,
                             INVALID, nullptr, HelpHidden, DefaultVis, 0,
                             nullptr, nullptr, nullptr, true, 0,
                             LangOpts->BranchProtectionPAuthLR, false, false,
                             false, normalizeSimpleFlag, denormalizeSimpleFlag,
                             mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-cfguard-no-checks", cfguard_no_checks, Flag, INVALID, INVALID,
    nullptr, HelpHidden, DefaultVis, 0,
    "Emit Windows Control Flow Guard tables only (no checks)", nullptr, nullptr,
    true, 0, CodeGenOpts.ControlFlowGuardNoChecks, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-cfguard", cfguard, Flag, INVALID, INVALID, nullptr, HelpHidden,
    DefaultVis, 0, "Emit Windows Control Flow Guard tables and checks", nullptr,
    nullptr, true, 0, CodeGenOpts.ControlFlowGuard, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-ehcontguard", ehcontguard, Flag,
                                INVALID, INVALID, nullptr, HelpHidden,
                                DefaultVis, 0,
                                "Emit Windows EH Continuation Guard tables",
                                nullptr, nullptr, true, 0,
                                CodeGenOpts.EHContGuard, false, false, false,
                                normalizeSimpleFlag, denormalizeSimpleFlag,
                                mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1,
    "-fexperimental-assignment-tracking=", fexperimental_assignment_tracking_EQ,
    Joined, f_Group, INVALID, nullptr, HelpHidden, DefaultVis, 0, nullptr,
    nullptr, "disabled,enabled,forced", true, 0,
    CodeGenOpts.AssignmentTrackingMode,
    CodeGenOptions::AssignmentTrackingOpts::Enabled, false,
    CodeGenOptions::AssignmentTrackingOpts::Enabled, normalizeSimpleEnum,
    denormalizeSimpleEnum, mergeForwardValue, extractForwardValue, 26)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-sys-header-deps", sys_header_deps, Flag, INVALID, INVALID,
    nullptr, HelpHidden, DefaultVis, 0,
    "Include system headers in dependency output", nullptr, nullptr, true, 0,
    DependencyOutputOpts.IncludeSystemHeaders, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-header-include-file", header_include_file, Separate, INVALID,
    INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "Filename (or -) to write header include output to", nullptr, nullptr, true,
    0, DependencyOutputOpts.HeaderIncludeOutputFile, std::string(), false,
    std::string(), normalizeString, denormalizeString, mergeForwardValue,
    extractForwardValue, -1)
#endif // DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-header-include-format=", header_include_format_EQ, Joined,
    INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "set format in which header info is emitted", nullptr, "textual,json", true,
    0, DependencyOutputOpts.HeaderIncludeFormat, HIFMT_Textual, false,
    HIFMT_Textual, normalizeSimpleEnum, denormalizeSimpleEnum,
    mergeForwardValue, extractForwardValue, 27)
#endif // DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING(
    prefix_1, "-header-include-filtering=", header_include_filtering_EQ, Joined,
    INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "set the flag that enables filtering header information", nullptr,
    "none,only-direct-system", true, 0,
    DependencyOutputOpts.HeaderIncludeFiltering, HIFIL_None, false, HIFIL_None,
    normalizeSimpleEnum, denormalizeSimpleEnum, mergeForwardValue,
    extractForwardValue, 28)
#endif // DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fdiagnostics-format", fdiagnostics_format, Separate, INVALID,
    INVALID, nullptr, 0, DefaultVis, 0,
    "Change diagnostic formatting to match IDE and command line tools", nullptr,
    "neverc,msvc,vi", true, 0, DiagnosticOpts->Format,
    DiagnosticOptions::NeverC, false, DiagnosticOptions::NeverC,
    normalizeSimpleEnum, denormalizeSimpleEnum, mergeForwardValue,
    extractForwardValue, 29)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(prefix_1, "-fdiagnostics-show-category",
                             fdiagnostics_show_category, Separate, INVALID,
                             INVALID, nullptr, 0, DefaultVis, 0,
                             "Print diagnostic category", nullptr,
                             "none,id,name", true, 0,
                             DiagnosticOpts->ShowCategories, 0, false, 0,
                             normalizeSimpleEnum, denormalizeSimpleEnum,
                             mergeForwardValue, extractForwardValue, 30)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-diagnostics-use-presumed-location",
    fno_diagnostics_use_presumed_location, Flag, INVALID, INVALID, nullptr, 0,
    DefaultVis, 0,
    "Ignore #line directives when displaying diagnostic locations", nullptr,
    nullptr, true, 0, DiagnosticOpts->ShowPresumedLoc, true, false, true,
    normalizeSimpleNegativeFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(
    prefix_1, "-ftabstop", ftabstop, Separate, INVALID, INVALID, nullptr, 0,
    DefaultVis, 0, "Set the tab stop distance.", "<N>", nullptr, true, 0,
    DiagnosticOpts->TabStop, DiagnosticOptions::DefaultTabStop, false,
    DiagnosticOptions::DefaultTabStop, normalizeStringIntegral<unsigned>,
    denormalizeString<unsigned>, mergeForwardValue, extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef DIAG_OPTION_WITH_MARSHALLING
DIAG_OPTION_WITH_MARSHALLING(
    prefix_1, "-ferror-limit", ferror_limit, Separate, INVALID, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Set the maximum number of errors to emit before stopping (0 = no limit).",
    "<N>", nullptr, true, 0, DiagnosticOpts->ErrorLimit, 0, false, 0,
    normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // DIAG_OPTION_WITH_MARSHALLING
#ifdef FRONTEND_OPTION_WITH_MARSHALLING
FRONTEND_OPTION_WITH_MARSHALLING(prefix_1, "-disable-free", disable_free, Flag,
                                 INVALID, INVALID, nullptr, HelpHidden,
                                 DefaultVis, 0,
                                 "Disable freeing of memory on exit", nullptr,
                                 nullptr, true, 0, FrontendOpts.DisableFree,
                                 false, false, false, normalizeSimpleFlag,
                                 denormalizeSimpleFlag, mergeForwardValue,
                                 extractForwardValue, -1)
#endif // FRONTEND_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-clear-ast-before-backend", clear_ast_before_backend, Flag,
    INVALID, INVALID, nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
    "Clear the NeverC AST before running backend code generation", nullptr,
    nullptr, true, 0, CodeGenOpts.ClearASTBeforeBackend, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_no_clear_ast_before_backend),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-no-clear-ast-before-backend", no_clear_ast_before_backend, Flag,
    INVALID, INVALID, nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
    "Don't clear the NeverC AST before running backend code generation",
    nullptr, nullptr, true, 0, CodeGenOpts.ClearASTBeforeBackend, false, false,
    false,
    makeBooleanOptionNormalizer(false, true, OPT_clear_ast_before_backend),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-enable-noundef-analysis", enable_noundef_analysis, Flag,
    INVALID, INVALID, nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
    "Enable analyzing function argument and return types for mandatory "
    "definedness",
    nullptr, nullptr, true, 0, CodeGenOpts.EnableNoundefAttrs, true, false,
    false,
    makeBooleanOptionNormalizer(true, false, OPT_no_enable_noundef_analysis),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-no-enable-noundef-analysis", no_enable_noundef_analysis, Flag,
    INVALID, INVALID, nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
    "Disable analyzing function argument and return types for mandatory "
    "definedness",
    nullptr, nullptr, true, 0, CodeGenOpts.EnableNoundefAttrs, true, false,
    false,
    makeBooleanOptionNormalizer(false, true, OPT_enable_noundef_analysis),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(prefix_1, "-discard-value-names",
                                discard_value_names, Flag, INVALID, INVALID,
                                nullptr, HelpHidden, DefaultVis, 0,
                                "Discard value names in LLVM IR", nullptr,
                                nullptr, true, 0, CodeGenOpts.DiscardValueNames,
                                false, false, false, normalizeSimpleFlag,
                                denormalizeSimpleFlag, mergeForwardValue,
                                extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fno-recovery-ast", fno_recovery_ast,
                             Flag, INVALID, INVALID, nullptr, 0, DefaultVis, 0,
                             "", nullptr, nullptr, true, 0,
                             LangOpts->RecoveryAST, true, false, true,
                             makeBooleanOptionNormalizer(false, true,
                                                         OPT_frecovery_ast),
                             makeBooleanOptionDenormalizer(false),
                             mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-frecovery-ast", frecovery_ast, Flag,
                             INVALID, INVALID, nullptr, 0, DefaultVis, 0,
                             "Preserve expressions in AST rather than dropping "
                             "them when encountering semantic errors",
                             nullptr, nullptr, true, 0, LangOpts->RecoveryAST,
                             true, false, true,
                             makeBooleanOptionNormalizer(true, false,
                                                         OPT_fno_recovery_ast),
                             makeBooleanOptionDenormalizer(true),
                             mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-recovery-ast-type", fno_recovery_ast_type, Flag, INVALID,
    INVALID, nullptr, 0, DefaultVis, 0, "", nullptr, nullptr, true, 0,
    LangOpts->RecoveryASTType, true, false, true,
    makeBooleanOptionNormalizer(false, true, OPT_frecovery_ast_type),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-frecovery-ast-type", frecovery_ast_type, Flag, INVALID, INVALID,
    nullptr, 0, DefaultVis, 0,
    "Preserve the type for recovery expressions when possible", nullptr,
    nullptr, true, 0, LangOpts->RecoveryASTType, true, false, true,
    makeBooleanOptionNormalizer(true, false, OPT_fno_recovery_ast_type),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-emit-llvm-uselists", emit_llvm_uselists, Flag, INVALID, INVALID,
    nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
    "Preserve order of LLVM use-lists when serializing", nullptr, nullptr, true,
    0, CodeGenOpts.EmitLLVMUseLists, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_no_emit_llvm_uselists),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-no-emit-llvm-uselists", no_emit_llvm_uselists, Flag, INVALID,
    INVALID, nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
    "Don't preserve order of LLVM use-lists when serializing", nullptr, nullptr,
    true, 0, CodeGenOpts.EmitLLVMUseLists, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_emit_llvm_uselists),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef FRONTEND_OPTION_WITH_MARSHALLING
FRONTEND_OPTION_WITH_MARSHALLING(prefix_1, "-print-stats", print_stats, Flag,
                                 INVALID, INVALID, nullptr, 0, DefaultVis, 0,
                                 "Print performance metrics and statistics",
                                 nullptr, nullptr, true, 0,
                                 FrontendOpts.ShowStats, false, false, false,
                                 normalizeSimpleFlag, denormalizeSimpleFlag,
                                 mergeForwardValue, extractForwardValue, -1)
#endif // FRONTEND_OPTION_WITH_MARSHALLING
#ifdef FRONTEND_OPTION_WITH_MARSHALLING
FRONTEND_OPTION_WITH_MARSHALLING(prefix_1, "-stats-file=", stats_file, Joined,
                                 INVALID, INVALID, nullptr, 0, DefaultVis, 0,
                                 "Filename to write statistics to", nullptr,
                                 nullptr, true, 0, FrontendOpts.StatsFile,
                                 std::string(), false, std::string(),
                                 normalizeString, denormalizeString,
                                 mergeForwardValue, extractForwardValue, -1)
#endif // FRONTEND_OPTION_WITH_MARSHALLING
#ifdef FRONTEND_OPTION_WITH_MARSHALLING
FRONTEND_OPTION_WITH_MARSHALLING(
    prefix_1, "-stats-file-append", stats_file_append, Flag, INVALID, INVALID,
    nullptr, HelpHidden, DefaultVis, 0,
    "If stats should be appended to stats-file instead of overwriting it",
    nullptr, nullptr, true, 0, FrontendOpts.AppendStats, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // FRONTEND_OPTION_WITH_MARSHALLING
#ifdef FRONTEND_OPTION_WITH_MARSHALLING
FRONTEND_OPTION_WITH_MARSHALLING(prefix_1, "-version", version, Flag, INVALID,
                                 INVALID, nullptr, 0, DefaultVis, 0,
                                 "Print the compiler version", nullptr, nullptr,
                                 true, 0, FrontendOpts.ShowVersion, false,
                                 false, false, normalizeSimpleFlag,
                                 denormalizeSimpleFlag, mergeForwardValue,
                                 extractForwardValue, -1)
#endif // FRONTEND_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-main-file-name", main_file_name, Separate, INVALID, INVALID,
    nullptr, HelpHidden, DefaultVis, 0,
    "Main file name to use for debug info and source if missing", nullptr,
    nullptr, true, 0, CodeGenOpts.MainFileName, std::string(), false,
    std::string(), normalizeString, denormalizeString, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-split-dwarf-output", split_dwarf_output, Separate, INVALID,
    INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "File name to use for split dwarf debug info output", nullptr, nullptr,
    true, 0, CodeGenOpts.SplitDwarfOutput, std::string(), false, std::string(),
    normalizeString, denormalizeString, mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-mreassociate", mreassociate, Flag, INVALID, INVALID, nullptr,
    HelpHidden, DefaultVis, 0,
    "Allow reassociation transformations for floating-point instructions",
    nullptr, nullptr, true, 0, LangOpts->AllowFPReassoc, false,
    false || LangOpts->UnsafeFPMath, true, normalizeSimpleFlag,
    denormalizeSimpleFlag, mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-menable-no-nans", menable_no_nans,
                             Flag, INVALID, INVALID, nullptr, HelpHidden,
                             DefaultVis, 0,
                             "Allow optimization to assume there are no NaNs.",
                             nullptr, nullptr, true, 0, LangOpts->NoHonorNaNs,
                             false, false || LangOpts->FiniteMathOnly, true,
                             normalizeSimpleFlag, denormalizeSimpleFlag,
                             mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-menable-no-infs", menable_no_infinities, Flag, INVALID, INVALID,
    nullptr, HelpHidden, DefaultVis, 0,
    "Allow optimization to assume there are no infinities.", nullptr, nullptr,
    true, 0, LangOpts->NoHonorInfs, false, false || LangOpts->FiniteMathOnly,
    true, normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-mframe-pointer=", mframe_pointer_EQ, Joined, INVALID, INVALID,
    nullptr, 0, DefaultVis, 0, "Specify which frame pointers to retain.",
    nullptr, "all,non-leaf,none", true, 0, CodeGenOpts.FramePointer,
    CodeGenOptions::FramePointerKind::None, false,
    CodeGenOptions::FramePointerKind::None, normalizeSimpleEnum,
    denormalizeSimpleEnum, mergeForwardValue, extractForwardValue, 31)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_3, "--dependent-lib=", dependent_lib, Joined, INVALID, INVALID,
    nullptr, 0, DefaultVis, 0, "Add dependent library", nullptr, nullptr, true,
    0, CodeGenOpts.DependentLibraries, std::vector<std::string>({}), false,
    std::vector<std::string>({}), normalizeStringVector,
    denormalizeStringVector, mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-split-dwarf-file", split_dwarf_file, Separate, INVALID, INVALID,
    nullptr, HelpHidden, DefaultVis, 0,
    "Name of the split dwarf debug info file to encode in the object file",
    nullptr, nullptr, true, 0, CodeGenOpts.SplitDwarfFile, std::string(), false,
    std::string(), normalizeString, denormalizeString, mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-function-alignment",
                             function_alignment, Separate, INVALID, INVALID,
                             nullptr, HelpHidden, DefaultVis, 0,
                             "default alignment for functions", nullptr,
                             nullptr, true, 0, LangOpts->FunctionAlignment, 0,
                             false, 0, normalizeStringIntegral<unsigned>,
                             denormalizeString<unsigned>, mergeForwardValue,
                             extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fhalf-no-semantic-interposition",
    fhalf_no_semantic_interposition, Flag, INVALID, INVALID, nullptr,
    HelpHidden, DefaultVis, 0,
    "Like -fno-semantic-interposition but don't use local aliases", nullptr,
    nullptr, true, 0, LangOpts->HalfNoSemanticInterposition, false, false,
    false, normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-static-define", static_define, Flag,
                             INVALID, INVALID, nullptr, HelpHidden, DefaultVis,
                             0, "Should __STATIC__ be defined", nullptr,
                             nullptr, true, 0, LangOpts->Static, false, false,
                             false, normalizeSimpleFlag, denormalizeSimpleFlag,
                             mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-stack-protector", stack_protector,
                             Separate, INVALID, INVALID, nullptr, HelpHidden,
                             DefaultVis, 0, "Enable stack protectors", nullptr,
                             "0,1,2,3", true, 0, LangOpts->StackProtector,
                             LangOptions::SSPOff, false, LangOptions::SSPOff,
                             normalizeSimpleEnum, denormalizeSimpleEnum,
                             mergeForwardValue, extractForwardValue, 32)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-stack-protector-buffer-size", stack_protector_buffer_size,
    Separate, INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "Lower bound for a buffer to be considered for stack protection", nullptr,
    nullptr, true, 0, CodeGenOpts.SSPBufferSize, 8, false, 8,
    normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-ftype-visibility=", ftype_visibility,
                             Joined, INVALID, INVALID, nullptr, HelpHidden,
                             DefaultVis, 0, "Default type visibility", nullptr,
                             "default,hidden,internal,protected", true, 0,
                             LangOpts->TypeVisibilityMode,
                             LangOpts->ValueVisibilityMode, false,
                             LangOpts->ValueVisibilityMode, normalizeSimpleEnum,
                             denormalizeSimpleEnum, mergeForwardValue,
                             extractForwardValue, 33)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fapply-global-visibility-to-externs",
                             fapply_global_visibility_to_externs, Flag, INVALID,
                             INVALID, nullptr, HelpHidden, DefaultVis, 0,
                             "Apply global symbol visibility to external "
                             "declarations without an explicit visibility",
                             nullptr, nullptr, true, 0,
                             LangOpts->SetVisibilityForExternDecls, false,
                             false, false, normalizeSimpleFlag,
                             denormalizeSimpleFlag, mergeForwardValue,
                             extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fbracket-depth", fbracket_depth, Separate, INVALID, INVALID,
    nullptr, HelpHidden, DefaultVis, 0,
    "Maximum nesting level for parentheses, brackets, and braces", nullptr,
    nullptr, true, 0, LangOpts->BracketDepth, 256, false, 256,
    normalizeStringIntegral<unsigned>, denormalizeString<unsigned>,
    mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fconst-strings", fconst_strings, Flag, INVALID, INVALID,
    nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
    "Use a const qualified type for string literals in C", nullptr, nullptr,
    true, 0, LangOpts->ConstStrings, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_const_strings),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-const-strings", fno_const_strings, Flag, INVALID, INVALID,
    nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
    "Don't use a const qualified type for string literals in C", nullptr,
    nullptr, true, 0, LangOpts->ConstStrings, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fconst_strings),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fno-bitfield-type-align",
                             fno_bitfield_type_align, Flag, INVALID, INVALID,
                             nullptr, HelpHidden, DefaultVis, 0,
                             "Ignore bit-field types when aligning structures",
                             nullptr, nullptr, true, 0,
                             LangOpts->NoBitFieldTypeAlign, false, false, false,
                             normalizeSimpleFlag, denormalizeSimpleFlag,
                             mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fdeprecated-macro", fdeprecated_macro, Flag, INVALID, INVALID,
    nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
    "Defines the __DEPRECATED macro", nullptr, nullptr, true, 0,
    LangOpts->Deprecated, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_deprecated_macro),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-deprecated-macro", fno_deprecated_macro, Flag, INVALID,
    INVALID, nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
    "Undefines the __DEPRECATED macro", nullptr, nullptr, true, 0,
    LangOpts->Deprecated, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fdeprecated_macro),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fnative-half-type", fnative_half_type, Flag, INVALID, INVALID,
    nullptr, HelpHidden, DefaultVis, 0,
    "Use the native half type for __fp16 instead of promoting to float",
    nullptr, nullptr, true, 0, LangOpts->NativeHalfType, false, false, false,
    normalizeSimpleFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fnative-half-arguments-and-returns",
                             fnative_half_arguments_and_returns, Flag, INVALID,
                             INVALID, nullptr, HelpHidden, DefaultVis, 0,
                             "Use the native __fp16 type for arguments and "
                             "returns (and skip ABI-specific lowering)",
                             nullptr, nullptr, true, 0,
                             LangOpts->NativeHalfArgsAndReturns, false, false,
                             false, normalizeSimpleFlag, denormalizeSimpleFlag,
                             mergeForwardValue, extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fdefault-calling-conv=", fdefault_calling_conv_EQ, Joined,
    INVALID, INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "Set default calling convention", nullptr,
    "cdecl,fastcall,stdcall,vectorcall,regcall", true, 0,
    LangOpts->DefaultCallingConv, LangOptions::DCC_None, false,
    LangOptions::DCC_None, normalizeSimpleEnum, denormalizeSimpleEnum,
    mergeForwardValue, extractForwardValue, 34)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fwchar-type=", fwchar_type_EQ, Joined,
                             INVALID, INVALID, nullptr, HelpHidden, DefaultVis,
                             0, "Select underlying type for wchar_t", nullptr,
                             "char,short,int", true, 0, LangOpts->WCharSize, 0,
                             false, 0, normalizeSimpleEnum,
                             denormalizeSimpleEnum, mergeForwardValue,
                             extractForwardValue, 35)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-signed-wchar", fno_signed_wchar, Flag, INVALID, INVALID,
    nullptr, HelpHidden | HelpHidden, DefaultVis, 0,
    "Use an unsigned type for wchar_t", nullptr, nullptr, true, 0,
    LangOpts->WCharIsSigned, true, false, true,
    makeBooleanOptionNormalizer(false, true, OPT_fsigned_wchar),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(
    prefix_1, "-fsigned-wchar", fsigned_wchar, Flag, INVALID, INVALID, nullptr,
    HelpHidden | HelpHidden, DefaultVis, 0, "Use a signed type for wchar_t",
    nullptr, nullptr, true, 0, LangOpts->WCharIsSigned, true, false, true,
    makeBooleanOptionNormalizer(true, false, OPT_fno_signed_wchar),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef LANG_OPTION_WITH_MARSHALLING
LANG_OPTION_WITH_MARSHALLING(prefix_1, "-fexperimental-max-bitint-width=",
                             fexperimental_max_bitint_width_EQ, Joined, f_Group,
                             INVALID, nullptr, HelpHidden, DefaultVis, 0,
                             "Set the maximum bitwidth for _BitInt (this "
                             "option is expected to be removed in the future)",
                             "<N>", nullptr, true, 0, LangOpts->MaxBitIntWidth,
                             0, false, 0, normalizeStringIntegral<unsigned>,
                             denormalizeString<unsigned>, mergeForwardValue,
                             extractForwardValue, -1)
#endif // LANG_OPTION_WITH_MARSHALLING
#ifdef HEADER_SEARCH_OPTION_WITH_MARSHALLING
HEADER_SEARCH_OPTION_WITH_MARSHALLING(
    prefix_1, "-nostdsysteminc", nostdsysteminc, Flag, INVALID, INVALID,
    nullptr, HelpHidden, DefaultVis, 0,
    "Disable standard system #include directories", nullptr, nullptr, true, 0,
    HeaderIdxOpts->UseStandardSystemIncludes, true, false, true,
    normalizeSimpleNegativeFlag, denormalizeSimpleFlag, mergeForwardValue,
    extractForwardValue, -1)
#endif // HEADER_SEARCH_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fdebug-pass-manager", fdebug_pass_manager, Flag, INVALID,
    INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "Prints debug information for the new pass manager", nullptr, nullptr, true,
    0, CodeGenOpts.DebugPassManager, false, false, false,
    makeBooleanOptionNormalizer(true, false, OPT_fno_debug_pass_manager),
    makeBooleanOptionDenormalizer(true), mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-fno-debug-pass-manager", fno_debug_pass_manager, Flag, INVALID,
    INVALID, nullptr, HelpHidden, DefaultVis, 0,
    "Disables debug printing for the new pass manager", nullptr, nullptr, true,
    0, CodeGenOpts.DebugPassManager, false, false, false,
    makeBooleanOptionNormalizer(false, true, OPT_fdebug_pass_manager),
    makeBooleanOptionDenormalizer(false), mergeForwardValue,
    extractForwardValue, -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING
#ifdef CODEGEN_OPTION_WITH_MARSHALLING
CODEGEN_OPTION_WITH_MARSHALLING(
    prefix_1, "-opt-record-file", opt_record_file, Separate, INVALID, INVALID,
    nullptr, HelpHidden, DefaultVis, 0,
    "File name to use for YAML optimization record output", nullptr, nullptr,
    true, 0, CodeGenOpts.OptRecordFile, std::string(), false, std::string(),
    normalizeString, denormalizeString, mergeForwardValue, extractForwardValue,
    -1)
#endif // CODEGEN_OPTION_WITH_MARSHALLING

#ifdef SIMPLE_ENUM_VALUE_TABLE

struct SimpleEnumValue {
  const char *Name;
  unsigned Value;
};

struct SimpleEnumValueTable {
  const SimpleEnumValue *Table;
  unsigned Size;
};
static const SimpleEnumValue complex_range_EQValueTable[] = {
    {"full", static_cast<unsigned>(LangOptions::CX_Full)},
    {"limited", static_cast<unsigned>(LangOptions::CX_Limited)},
    {"improved", static_cast<unsigned>(LangOptions::CX_Improved)},
};
static const SimpleEnumValue fstrict_flex_arrays_EQValueTable[] = {
    {"0",
     static_cast<unsigned>(LangOptions::StrictFlexArraysLevelKind::Default)},
    {"1", static_cast<unsigned>(
              LangOptions::StrictFlexArraysLevelKind::OneZeroOrIncomplete)},
    {"2", static_cast<unsigned>(
              LangOptions::StrictFlexArraysLevelKind::ZeroOrIncomplete)},
    {"3", static_cast<unsigned>(
              LangOptions::StrictFlexArraysLevelKind::IncompleteOnly)},
};
static const SimpleEnumValue exception_modelValueTable[] = {
    {"dwarf",
     static_cast<unsigned>(LangOptions::ExceptionHandlingKind::DwarfCFI)},
    {"seh", static_cast<unsigned>(LangOptions::ExceptionHandlingKind::WinEH)},
};
static const SimpleEnumValue ffloat16_excess_precision_EQValueTable[] = {
    {"standard", static_cast<unsigned>(LangOptions::FPP_Standard)},
    {"fast", static_cast<unsigned>(LangOptions::FPP_Fast)},
    {"none", static_cast<unsigned>(LangOptions::FPP_None)},
};
static const SimpleEnumValue fbfloat16_excess_precision_EQValueTable[] = {
    {"standard", static_cast<unsigned>(LangOptions::FPP_Standard)},
    {"fast", static_cast<unsigned>(LangOptions::FPP_Fast)},
    {"none", static_cast<unsigned>(LangOptions::FPP_None)},
};
static const SimpleEnumValue ffp_eval_method_EQValueTable[] = {
    {"source", static_cast<unsigned>(LangOptions::FEM_Source)},
    {"double", static_cast<unsigned>(LangOptions::FEM_Double)},
    {"extended", static_cast<unsigned>(LangOptions::FEM_Extended)},
};
static const SimpleEnumValue ffp_exception_behavior_EQValueTable[] = {
    {"ignore", static_cast<unsigned>(LangOptions::FPE_Ignore)},
    {"maytrap", static_cast<unsigned>(LangOptions::FPE_MayTrap)},
    {"strict", static_cast<unsigned>(LangOptions::FPE_Strict)},
};
static const SimpleEnumValue fextend_args_EQValueTable[] = {
    {"32", static_cast<unsigned>(LangOptions::ExtendArgsKind::ExtendTo32)},
    {"64", static_cast<unsigned>(LangOptions::ExtendArgsKind::ExtendTo64)},
};
static const SimpleEnumValue mfunction_return_EQValueTable[] = {
    {"keep", static_cast<unsigned>(llvm::FunctionReturnThunksKind::Keep)},
    {"thunk-extern",
     static_cast<unsigned>(llvm::FunctionReturnThunksKind::Extern)},
};
static const SimpleEnumValue flax_vector_conversions_EQValueTable[] = {
    {"none", static_cast<unsigned>(LangOptions::LaxVectorConversionKind::None)},
    {"integer",
     static_cast<unsigned>(LangOptions::LaxVectorConversionKind::Integer)},
    {"all", static_cast<unsigned>(LangOptions::LaxVectorConversionKind::All)},
};
static const SimpleEnumValue fveclibValueTable[] = {
    {"Accelerate",
     static_cast<unsigned>(llvm::driver::VectorLibrary::Accelerate)},
    {"libmvec", static_cast<unsigned>(llvm::driver::VectorLibrary::LIBMVEC)},
    {"MASSV", static_cast<unsigned>(llvm::driver::VectorLibrary::MASSV)},
    {"SVML", static_cast<unsigned>(llvm::driver::VectorLibrary::SVML)},
    {"SLEEF", static_cast<unsigned>(llvm::driver::VectorLibrary::SLEEF)},
    {"Darwin_libsystem_m",
     static_cast<unsigned>(llvm::driver::VectorLibrary::Darwin_libsystem_m)},
    {"ArmPL", static_cast<unsigned>(llvm::driver::VectorLibrary::ArmPL)},
    {"none", static_cast<unsigned>(llvm::driver::VectorLibrary::NoLibrary)},
};
static const SimpleEnumValue ftrivial_auto_var_initValueTable[] = {
    {"uninitialized",
     static_cast<unsigned>(LangOptions::TrivialAutoVarInitKind::Uninitialized)},
    {"zero", static_cast<unsigned>(LangOptions::TrivialAutoVarInitKind::Zero)},
    {"pattern",
     static_cast<unsigned>(LangOptions::TrivialAutoVarInitKind::Pattern)},
};
static const SimpleEnumValue ftlsmodel_EQValueTable[] = {
    {"global-dynamic",
     static_cast<unsigned>(CodeGenOptions::GeneralDynamicTLSModel)},
    {"local-dynamic",
     static_cast<unsigned>(CodeGenOptions::LocalDynamicTLSModel)},
    {"initial-exec",
     static_cast<unsigned>(CodeGenOptions::InitialExecTLSModel)},
    {"local-exec", static_cast<unsigned>(CodeGenOptions::LocalExecTLSModel)},
};
static const SimpleEnumValue fvisibility_dllexport_EQValueTable[] = {
    {"default", static_cast<unsigned>(DefaultVisibility)},
    {"hidden", static_cast<unsigned>(HiddenVisibility)},
    {"internal", static_cast<unsigned>(HiddenVisibility)},
    {"protected", static_cast<unsigned>(ProtectedVisibility)},
};
static const SimpleEnumValue fvisibility_nodllstorageclass_EQValueTable[] = {
    {"default", static_cast<unsigned>(DefaultVisibility)},
    {"hidden", static_cast<unsigned>(HiddenVisibility)},
    {"internal", static_cast<unsigned>(HiddenVisibility)},
    {"protected", static_cast<unsigned>(ProtectedVisibility)},
};
static const SimpleEnumValue fvisibility_externs_dllimport_EQValueTable[] = {
    {"default", static_cast<unsigned>(DefaultVisibility)},
    {"hidden", static_cast<unsigned>(HiddenVisibility)},
    {"internal", static_cast<unsigned>(HiddenVisibility)},
    {"protected", static_cast<unsigned>(ProtectedVisibility)},
};
static const SimpleEnumValue
    fvisibility_externs_nodllstorageclass_EQValueTable[] = {
        {"default", static_cast<unsigned>(DefaultVisibility)},
        {"hidden", static_cast<unsigned>(HiddenVisibility)},
        {"internal", static_cast<unsigned>(HiddenVisibility)},
        {"protected", static_cast<unsigned>(ProtectedVisibility)},
};
static const SimpleEnumValue fvisibility_EQValueTable[] = {
    {"default", static_cast<unsigned>(DefaultVisibility)},
    {"hidden", static_cast<unsigned>(HiddenVisibility)},
    {"internal", static_cast<unsigned>(HiddenVisibility)},
    {"protected", static_cast<unsigned>(ProtectedVisibility)},
};
static const SimpleEnumValue mdefault_visibility_export_mapping_EQValueTable[] =
    {
        {"none", static_cast<unsigned>(
                     LangOptions::DefaultVisiblityExportMapping::None)},
        {"explicit", static_cast<unsigned>(
                         LangOptions::DefaultVisiblityExportMapping::Explicit)},
        {"all", static_cast<unsigned>(
                    LangOptions::DefaultVisiblityExportMapping::All)},
};
static const SimpleEnumValue fzero_call_used_regs_EQValueTable[] = {
    {"skip",
     static_cast<unsigned>(llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::Skip)},
    {"used-gpr-arg",
     static_cast<unsigned>(
         llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::UsedGPRArg)},
    {"used-gpr", static_cast<unsigned>(
                     llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::UsedGPR)},
    {"used-arg", static_cast<unsigned>(
                     llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::UsedArg)},
    {"used",
     static_cast<unsigned>(llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::Used)},
    {"all-gpr-arg",
     static_cast<unsigned>(
         llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::AllGPRArg)},
    {"all-gpr", static_cast<unsigned>(
                    llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::AllGPR)},
    {"all-arg", static_cast<unsigned>(
                    llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::AllArg)},
    {"all",
     static_cast<unsigned>(llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::All)},
};
static const SimpleEnumValue femit_dwarf_unwind_EQValueTable[] = {
    {"always", static_cast<unsigned>(llvm::EmitDwarfUnwindType::Always)},
    {"no-compact-unwind",
     static_cast<unsigned>(llvm::EmitDwarfUnwindType::NoCompactUnwind)},
    {"default", static_cast<unsigned>(llvm::EmitDwarfUnwindType::Default)},
};
static const SimpleEnumValue gsrc_hash_EQValueTable[] = {
    {"md5", static_cast<unsigned>(CodeGenOptions::DSH_MD5)},
    {"sha1", static_cast<unsigned>(CodeGenOptions::DSH_SHA1)},
    {"sha256", static_cast<unsigned>(CodeGenOptions::DSH_SHA256)},
};
static const SimpleEnumValue inline_asm_EQValueTable[] = {
    {"att", static_cast<unsigned>(CodeGenOptions::IAD_ATT)},
    {"intel", static_cast<unsigned>(CodeGenOptions::IAD_Intel)},
};
static const SimpleEnumValue mthread_modelValueTable[] = {
    {"posix", static_cast<unsigned>(LangOptions::ThreadModelKind::POSIX)},
    {"single", static_cast<unsigned>(LangOptions::ThreadModelKind::Single)},
};
static const SimpleEnumValue debugger_tuning_EQValueTable[] = {
    {"gdb", static_cast<unsigned>(llvm::DebuggerKind::GDB)},
    {"lldb", static_cast<unsigned>(llvm::DebuggerKind::LLDB)},
};
static const SimpleEnumValue compress_debug_sections_EQValueTable[] = {
    {"none", static_cast<unsigned>(llvm::DebugCompressionType::None)},
    {"zlib", static_cast<unsigned>(llvm::DebugCompressionType::Zlib)},
    {"zstd", static_cast<unsigned>(llvm::DebugCompressionType::Zstd)},
};
static const SimpleEnumValue fexperimental_assignment_tracking_EQValueTable[] =
    {
        {"disabled", static_cast<unsigned>(
                         CodeGenOptions::AssignmentTrackingOpts::Disabled)},
        {"enabled", static_cast<unsigned>(
                        CodeGenOptions::AssignmentTrackingOpts::Enabled)},
        {"forced",
         static_cast<unsigned>(CodeGenOptions::AssignmentTrackingOpts::Forced)},
};
static const SimpleEnumValue header_include_format_EQValueTable[] = {
    {"textual", static_cast<unsigned>(HIFMT_Textual)},
    {"json", static_cast<unsigned>(HIFMT_JSON)},
};
static const SimpleEnumValue header_include_filtering_EQValueTable[] = {
    {"none", static_cast<unsigned>(HIFIL_None)},
    {"only-direct-system", static_cast<unsigned>(HIFIL_Only_Direct_System)},
};
static const SimpleEnumValue fdiagnostics_formatValueTable[] = {
    {"neverc", static_cast<unsigned>(DiagnosticOptions::NeverC)},
    {"msvc", static_cast<unsigned>(DiagnosticOptions::MSVC)},
    {"vi", static_cast<unsigned>(DiagnosticOptions::Vi)},
};
static const SimpleEnumValue fdiagnostics_show_categoryValueTable[] = {
    {"none", static_cast<unsigned>(0)},
    {"id", static_cast<unsigned>(1)},
    {"name", static_cast<unsigned>(2)},
};
static const SimpleEnumValue mframe_pointer_EQValueTable[] = {
    {"all", static_cast<unsigned>(CodeGenOptions::FramePointerKind::All)},
    {"non-leaf",
     static_cast<unsigned>(CodeGenOptions::FramePointerKind::NonLeaf)},
    {"none", static_cast<unsigned>(CodeGenOptions::FramePointerKind::None)},
};
static const SimpleEnumValue stack_protectorValueTable[] = {
    {"0", static_cast<unsigned>(LangOptions::SSPOff)},
    {"1", static_cast<unsigned>(LangOptions::SSPOn)},
    {"2", static_cast<unsigned>(LangOptions::SSPStrong)},
    {"3", static_cast<unsigned>(LangOptions::SSPReq)},
};
static const SimpleEnumValue ftype_visibilityValueTable[] = {
    {"default", static_cast<unsigned>(DefaultVisibility)},
    {"hidden", static_cast<unsigned>(HiddenVisibility)},
    {"internal", static_cast<unsigned>(HiddenVisibility)},
    {"protected", static_cast<unsigned>(ProtectedVisibility)},
};
static const SimpleEnumValue fdefault_calling_conv_EQValueTable[] = {
    {"cdecl", static_cast<unsigned>(LangOptions::DCC_CDecl)},
    {"fastcall", static_cast<unsigned>(LangOptions::DCC_FastCall)},
    {"stdcall", static_cast<unsigned>(LangOptions::DCC_StdCall)},
    {"vectorcall", static_cast<unsigned>(LangOptions::DCC_VectorCall)},
    {"regcall", static_cast<unsigned>(LangOptions::DCC_RegCall)},
};
static const SimpleEnumValue fwchar_type_EQValueTable[] = {
    {"char", static_cast<unsigned>(1)},
    {"short", static_cast<unsigned>(2)},
    {"int", static_cast<unsigned>(4)},
};
static const SimpleEnumValueTable SimpleEnumValueTables[] = {
    {complex_range_EQValueTable, std::size(complex_range_EQValueTable)},
    {fstrict_flex_arrays_EQValueTable,
     std::size(fstrict_flex_arrays_EQValueTable)},
    {exception_modelValueTable, std::size(exception_modelValueTable)},
    {ffloat16_excess_precision_EQValueTable,
     std::size(ffloat16_excess_precision_EQValueTable)},
    {fbfloat16_excess_precision_EQValueTable,
     std::size(fbfloat16_excess_precision_EQValueTable)},
    {ffp_eval_method_EQValueTable, std::size(ffp_eval_method_EQValueTable)},
    {ffp_exception_behavior_EQValueTable,
     std::size(ffp_exception_behavior_EQValueTable)},
    {fextend_args_EQValueTable, std::size(fextend_args_EQValueTable)},
    {mfunction_return_EQValueTable, std::size(mfunction_return_EQValueTable)},
    {flax_vector_conversions_EQValueTable,
     std::size(flax_vector_conversions_EQValueTable)},
    {fveclibValueTable, std::size(fveclibValueTable)},
    {ftrivial_auto_var_initValueTable,
     std::size(ftrivial_auto_var_initValueTable)},
    {ftlsmodel_EQValueTable, std::size(ftlsmodel_EQValueTable)},
    {fvisibility_dllexport_EQValueTable,
     std::size(fvisibility_dllexport_EQValueTable)},
    {fvisibility_nodllstorageclass_EQValueTable,
     std::size(fvisibility_nodllstorageclass_EQValueTable)},
    {fvisibility_externs_dllimport_EQValueTable,
     std::size(fvisibility_externs_dllimport_EQValueTable)},
    {fvisibility_externs_nodllstorageclass_EQValueTable,
     std::size(fvisibility_externs_nodllstorageclass_EQValueTable)},
    {fvisibility_EQValueTable, std::size(fvisibility_EQValueTable)},
    {mdefault_visibility_export_mapping_EQValueTable,
     std::size(mdefault_visibility_export_mapping_EQValueTable)},
    {fzero_call_used_regs_EQValueTable,
     std::size(fzero_call_used_regs_EQValueTable)},
    {femit_dwarf_unwind_EQValueTable,
     std::size(femit_dwarf_unwind_EQValueTable)},
    {gsrc_hash_EQValueTable, std::size(gsrc_hash_EQValueTable)},
    {inline_asm_EQValueTable, std::size(inline_asm_EQValueTable)},
    {mthread_modelValueTable, std::size(mthread_modelValueTable)},
    {debugger_tuning_EQValueTable, std::size(debugger_tuning_EQValueTable)},
    {compress_debug_sections_EQValueTable,
     std::size(compress_debug_sections_EQValueTable)},
    {fexperimental_assignment_tracking_EQValueTable,
     std::size(fexperimental_assignment_tracking_EQValueTable)},
    {header_include_format_EQValueTable,
     std::size(header_include_format_EQValueTable)},
    {header_include_filtering_EQValueTable,
     std::size(header_include_filtering_EQValueTable)},
    {fdiagnostics_formatValueTable, std::size(fdiagnostics_formatValueTable)},
    {fdiagnostics_show_categoryValueTable,
     std::size(fdiagnostics_show_categoryValueTable)},
    {mframe_pointer_EQValueTable, std::size(mframe_pointer_EQValueTable)},
    {stack_protectorValueTable, std::size(stack_protectorValueTable)},
    {ftype_visibilityValueTable, std::size(ftype_visibilityValueTable)},
    {fdefault_calling_conv_EQValueTable,
     std::size(fdefault_calling_conv_EQValueTable)},
    {fwchar_type_EQValueTable, std::size(fwchar_type_EQValueTable)},
};
static const unsigned SimpleEnumValueTablesSize =
    std::size(SimpleEnumValueTables);
#endif // SIMPLE_ENUM_VALUE_TABLE
