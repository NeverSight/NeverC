/////////
// Prefixes

#ifdef PREFIX
#define COMMA ,
PREFIX(prefix_0, {llvm::StringLiteral("")})
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

//////////
// Options

OPTION(prefix_0, "<input>", INPUT, Input, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_0, "<unknown>", UNKNOWN, Unknown, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--?", help_q, Flag, INVALID, help, nullptr, 0, DefaultVis, 0,
       "Print option help", nullptr, nullptr)
OPTION(prefix_1, "--align=", align, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Section alignment", nullptr, nullptr)
OPTION(prefix_1, "--aligncomm=", aligncomm, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, "Set common symbol alignment", nullptr, nullptr)
OPTION(prefix_1, "--allowbind", allowbind, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Enable DLL binding (default)", nullptr, nullptr)
OPTION(prefix_1, "--allowisolation", allowisolation, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Enable DLL isolation (default)", nullptr,
       nullptr)
OPTION(prefix_1, "--alternatename=", alternatename, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Define weak alias", nullptr, nullptr)
OPTION(prefix_1, "--appcontainer", appcontainer, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Image can only be run in an app container",
       nullptr, nullptr)
OPTION(prefix_1, "--assemblydebug=", ignored_assemblydebug_eq, Joined, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--assemblydebug", ignored_assemblydebug, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--base=", base, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Base address of the program", nullptr, nullptr)
OPTION(prefix_1, "--brepro", brepro, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Use a hash of the output as its timestamp", nullptr,
       nullptr)
OPTION(prefix_1, "--build-id", build_id, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Generate a build ID", nullptr, nullptr)
OPTION(prefix_1, "--call-graph-ordering-file=", call_graph_ordering_file,
       Joined, INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Layout sections to optimize the given call graph", nullptr, nullptr)
OPTION(prefix_1, "--call-graph-profile-sort", call_graph_profile_sort, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Reorder sections with call graph profile", nullptr, nullptr)
OPTION(prefix_1, "--cetcompat", cetcompat, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Mark executable image as compatible with Control-flow Enforcement "
       "Technology (CET) Shadow Stack",
       nullptr, nullptr)
OPTION(prefix_1, "--color-diagnostics=", color_diagnostics_eq, Joined, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Use colors in diagnostics: always, never or auto", nullptr, nullptr)
OPTION(prefix_1, "--color-diagnostics", color_diagnostics, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, "Use colors in diagnostics", nullptr,
       nullptr)
OPTION(prefix_1, "--debug=", debug_opt, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Embed a symbol table in the image with option", nullptr,
       nullptr)
OPTION(prefix_1, "--debugtype=", debugtype, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, "Debug information types; NeverC emits DWARF", nullptr,
       nullptr)
OPTION(prefix_1, "--debug", debug, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Embed DWARF debug information and a symbol table",
       nullptr, nullptr)
OPTION(prefix_1, "--def=", deffile, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Use module-definition file", nullptr, nullptr)
OPTION(prefix_1, "--defaultlib=", defaultlib, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, "Add the library to the list of input files", nullptr,
       nullptr)
OPTION(prefix_1, "--delay=", ignored_delay, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--delayload=", delayload, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, "Delay loaded DLL name", nullptr, nullptr)
OPTION(prefix_1, "--demangle", demangle, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Demangle symbols in diagnostics", nullptr, nullptr)
OPTION(prefix_1, "--dependentloadflag=", dependentloadflag_opt, Joined, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Sets the default load flags used to resolve the statically linked "
       "imports of a module",
       nullptr, nullptr)
OPTION(prefix_1, "--dependentloadflag", dependentloadflag, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--diasdkdir=", diasdkdir, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--disallowlib=", disallowlib, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Prevent linking a specific library (CRT directive)", nullptr, nullptr)
OPTION(prefix_1, "--dll", dll, Flag, INVALID, INVALID, nullptr, 0, DefaultVis,
       0, "Create a DLL", nullptr, nullptr)
OPTION(prefix_1, "--dont-merge-sections", dont_merge_sections, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Each data directory will have its own section", nullptr, nullptr)
OPTION(prefix_1, "--driver=uponly,wdm", driver_uponly_wdm, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--driver=uponly", driver_uponly, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Set IMAGE_FILE_UP_SYSTEM_ONLY bit in PE header", nullptr, nullptr)
OPTION(prefix_1, "--driver=wdm,uponly", driver_wdm_uponly, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--driver=wdm", driver_wdm, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Set IMAGE_DLL_CHARACTERISTICS_WDM_DRIVER bit in PE header", nullptr,
       nullptr)
OPTION(prefix_1, "--driver", driver, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Generate a Windows NT Kernel Mode Driver", nullptr,
       nullptr)
OPTION(prefix_1, "--dynamicbase", dynamicbase, Flag, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, "Enable ASLR (default unless /fixed)", nullptr,
       nullptr)
OPTION(prefix_1, "--editandcontinue", ignored_editandcontinue, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--emitpogophaseinfo", ignored_emitpogophaseinfo, Flag,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--enclave", enclave, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Create a VBS enclave image", nullptr, nullptr)
OPTION(prefix_1, "--end-lib", end_lib, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "End group of objects treated as if they were in a library", nullptr,
       nullptr)
OPTION(prefix_1, "--entry=", entry, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Name of entry point symbol", nullptr, nullptr)
OPTION(prefix_1, "--errorlimit=", errorlimit, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0,
       "Maximum number of errors to emit before stopping (0 = no limit)",
       nullptr, nullptr)
OPTION(prefix_1, "--errorreport=", ignored_errorreport, Joined, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--export=", export, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Export a function", nullptr, nullptr)
OPTION(prefix_1, "--failifmismatch=", failifmismatch, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "", nullptr, nullptr)
OPTION(prefix_1, "--fastfail", ignored_fastfail, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--filealign=", filealign, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, "Section alignment in the output file", nullptr,
       nullptr)
OPTION(prefix_1, "--fixed", fixed, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Disable base relocations", nullptr, nullptr)
OPTION(prefix_1, "--force=multipleres", force_multipleres, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Allow multiply defined resources when creating executables", nullptr,
       nullptr)
OPTION(prefix_1, "--force=multiple", force_multiple, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Allow multiply defined symbols when creating executables", nullptr,
       nullptr)
OPTION(prefix_1, "--force=unresolved", force_unresolved, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Allow undefined symbols when creating executables", nullptr, nullptr)
OPTION(prefix_1, "--force", force, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Allow undefined and multiply defined symbols", nullptr,
       nullptr)
OPTION(prefix_1, "--fork", fork, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Report the result as soon as the output is complete and release "
       "memory in the background (default)",
       nullptr, nullptr)
OPTION(prefix_1, "--functionpadmin=", functionpadmin_opt, Joined, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, "Prepares an image for hotpatching",
       nullptr, nullptr)
OPTION(prefix_1, "--functionpadmin", functionpadmin, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Prepares an image for hotpatching", nullptr,
       nullptr)
OPTION(prefix_1, "--guard=", guard, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Control flow guard", nullptr, nullptr)
OPTION(prefix_1, "--guardsym=", guardsym, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--heap=", heap, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Size of the heap", nullptr, nullptr)
OPTION(prefix_1, "--help", help, Flag, INVALID, INVALID, nullptr, 0, DefaultVis,
       0, "Print option help", nullptr, nullptr)
OPTION(prefix_1, "--highentropyva", highentropyva, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Enable 64-bit ASLR (default on 64-bit)",
       nullptr, nullptr)
OPTION(prefix_1, "--idlout=", ignored_idlout, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--ignore=", ignore, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Specify warning codes to ignore", nullptr, nullptr)
OPTION(prefix_1, "--ignoreidl", ignored_ignoreidl, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--ilk=", ignored_ilk, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--implib=", implib, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Import library name", nullptr, nullptr)
OPTION(prefix_1, "--include=", incl, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Force symbol to be added to symbol table as undefined one", nullptr,
       nullptr)
OPTION(prefix_1, "--includeglob=", includeglob, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Keep every symbol whose name matches the glob", nullptr, nullptr)
OPTION(prefix_1, "--includeoptional=", include_optional, Joined, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Add symbol as undefined, but allow it to remain undefined", nullptr,
       nullptr)
OPTION(prefix_1, "--incremental", incremental, Flag, INVALID, INVALID, nullptr,
       0, DefaultVis, 0,
       "Keep original import library if contents are unchanged", nullptr,
       nullptr)
OPTION(prefix_1, "--inferasanlibs", inferasanlibs, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Add the AddressSanitizer runtime that instrumented objects need",
       nullptr, nullptr)
OPTION(prefix_1, "--integritycheck", integritycheck, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Set FORCE_INTEGRITY bit in PE header",
       nullptr, nullptr)
OPTION(prefix_1, "--kernel", ignored_kernel, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--kill-at", kill_at, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--largeaddressaware", largeaddressaware, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, "Enable large addresses (default)",
       nullptr, nullptr)
OPTION(prefix_1, "--libpath=", libpath, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Additional library search path", nullptr, nullptr)
OPTION(prefix_1, "--lib", lib, Flag, INVALID, INVALID, nullptr, 0, DefaultVis,
       0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--linkrepro=", linkrepro, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--ltcg=", ignored_ltcg_eq, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--ltcgout=", ignored_ltcgout, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--ltcg", ignored_ltcg, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--machine=", machine, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Specify target platform", nullptr, nullptr)
OPTION(prefix_1, "--manifest=", manifest_colon, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "NO disables manifest output; EMBED[,ID=#] embeds manifest as resource "
       "in the image",
       nullptr, nullptr)
OPTION(
    prefix_1, "--manifestdependency=", manifestdependency, Joined, INVALID,
    INVALID, nullptr, 0, DefaultVis, 0,
    "Attributes for <dependency> element in manifest file; implies /manifest",
    nullptr, nullptr)
OPTION(prefix_1, "--manifestfile=", manifestfile, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Manifest output path, with /manifest",
       nullptr, nullptr)
OPTION(prefix_1, "--manifestinput=", manifestinput, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Additional manifest inputs; only valid with /manifest:embed", nullptr,
       nullptr)
OPTION(prefix_1, "--manifestuac=", manifestuac, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "User access control", nullptr, nullptr)
OPTION(prefix_1, "--manifest", manifest, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Create .manifest file", nullptr, nullptr)
OPTION(prefix_1, "--map=", map_file, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Write a map file to the given path", nullptr, nullptr)
OPTION(prefix_1, "--mapinfo=", map_info, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Include the specified information in a map file",
       nullptr, nullptr)
OPTION(prefix_1, "--map", map, Flag, INVALID, INVALID, nullptr, 0, DefaultVis,
       0, "Write a map file named after the output", nullptr, nullptr)
OPTION(prefix_1, "--maxilksize=", ignored_maxilksize, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--merge=", merge, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Combine sections", nullptr, nullptr)
OPTION(prefix_1, "--mllvm=", mllvm, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Pass an LLVM option to LTO code generation", nullptr,
       nullptr)
OPTION(prefix_1, "--natvis=", natvis, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--no-allowbind", allowbind_no, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Disable DLL binding", nullptr, nullptr)
OPTION(prefix_1, "--no-allowisolation", allowisolation_no, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, "Disable DLL isolation", nullptr,
       nullptr)
OPTION(prefix_1, "--no-appcontainer", appcontainer_no, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Image can run outside an app container (default)", nullptr, nullptr)
OPTION(prefix_1, "--no-build-id", no_build_id, Flag, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, "Do not generate a build ID", nullptr, nullptr)
OPTION(prefix_1, "--no-call-graph-profile-sort", no_call_graph_profile_sort,
       Flag, INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Do not reorder sections with call graph profile", nullptr, nullptr)
OPTION(prefix_1, "--no-cetcompat", cetcompat_no, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Don't mark executable image as compatible with Control-flow "
       "Enforcement Technology (CET) Shadow Stack (default)",
       nullptr, nullptr)
OPTION(prefix_1, "--no-color-diagnostics", no_color_diagnostics, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, "Do not use colors in diagnostics",
       nullptr, nullptr)
OPTION(prefix_1, "--no-demangle", no_demangle, Flag, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, "Do not demangle symbols in diagnostics", nullptr,
       nullptr)
OPTION(prefix_1, "--no-dynamicbase", dynamicbase_no, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Disable ASLR (default when /fixed)", nullptr,
       nullptr)
OPTION(prefix_1, "--no-fixed", fixed_no, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Enable base relocations (default)", nullptr, nullptr)
OPTION(prefix_1, "--no-fork", fork_no, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Exit only after all memory is released", nullptr,
       nullptr)
OPTION(prefix_1, "--no-highentropyva", highentropyva_no, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Disable 64-bit ASLR", nullptr, nullptr)
OPTION(prefix_1, "--no-incremental", incremental_no, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Overwrite import library even if contents are unchanged", nullptr,
       nullptr)
OPTION(prefix_1, "--no-inferasanlibs", inferasanlibs_no, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "No effect (default)", nullptr, nullptr)
OPTION(prefix_1, "--no-integritycheck", integritycheck_no, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, "No effect (default)", nullptr,
       nullptr)
OPTION(prefix_1, "--no-largeaddressaware", largeaddressaware_no, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, "Disable large addresses", nullptr,
       nullptr)
OPTION(prefix_1, "--no-nxcompat", nxcompat_no, Flag, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, "Disable data execution provention", nullptr, nullptr)
OPTION(prefix_1, "--no-safeseh", no_safeseh, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Do not produce a safe exception handler table", nullptr,
       nullptr)
OPTION(prefix_1, "--no-tsaware", tsaware_no, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Create non-Terminal Server aware executable", nullptr,
       nullptr)
OPTION(prefix_1, "--no-wx", no_wx, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Do not treat warnings as errors", nullptr, nullptr)
OPTION(prefix_1, "--nodefaultlib=", nodefaultlib, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Remove a default library", nullptr, nullptr)
OPTION(prefix_1, "--nodefaultlib", nodefaultlib_all, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Remove all default libraries", nullptr,
       nullptr)
OPTION(prefix_1, "--noentry", noentry, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Don't add reference to DllMainCRTStartup; only valid with /dll",
       nullptr, nullptr)
OPTION(prefix_1, "--noimplib", noimplib, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Don't output an import lib", nullptr, nullptr)
OPTION(prefix_1, "--nologo", ignored_nologo, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--noseh", noseh, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "The image has no exception handlers; x86 only", nullptr,
       nullptr)
OPTION(prefix_1, "--nxcompat", nxcompat, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Enable data execution prevention (default)", nullptr,
       nullptr)
OPTION(prefix_1, "--opt=", opt, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Control optimizations", nullptr, nullptr)
OPTION(prefix_1, "--order=", order, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Put functions in order", nullptr, nullptr)
OPTION(prefix_1, "--osversion=", osversion, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--out=", out, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Path to file to write output", nullptr, nullptr)
OPTION(prefix_1, "--override=", override, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Allow symbol to override any other definition without error", nullptr,
       nullptr)
OPTION(prefix_1, "--pdb=", pdb, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--pdbaltpath=", pdbaltpath, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--pdbcompress", pdbcompress, Flag, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--pdbpagesize=", pdbpagesize, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--pdbsourcepath=", pdbsourcepath, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--pdbstream=", pdbstream, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--pdbstripped=", pdbstripped, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--print-search-paths", print_search_paths, Flag, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0, "Print the library search paths",
       nullptr, nullptr)
OPTION(prefix_1, "--print-symbol-order=", print_symbol_order, Joined, INVALID,
       INVALID, nullptr, 0, DefaultVis, 0,
       "Print the symbol order chosen from the call graph to the given file",
       nullptr, nullptr)
OPTION(prefix_1, "--profile", profile, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--release", release, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Set the Checksum in the header of an PE file", nullptr,
       nullptr)
OPTION(prefix_1, "--reproduce=", reproduce, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--rsp-quoting=", rsp_quoting, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0,
       "Quoting style for response files: windows or posix", nullptr, nullptr)
OPTION(prefix_1, "--safeseh", safeseh, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Safe exception handlers; x86 only, without effect on 64-bit targets",
       nullptr, nullptr)
OPTION(prefix_1, "--section=", section, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Specify section attributes", nullptr, nullptr)
OPTION(prefix_1, "--stack=", stack, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Size of the stack", nullptr, nullptr)
OPTION(prefix_1, "--start-lib", start_lib, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Start group of objects treated as if they were in a library", nullptr,
       nullptr)
OPTION(prefix_1, "--stub=", stub, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Replace the MS-DOS stub program with the given file",
       nullptr, nullptr)
OPTION(prefix_1, "--subsystem=", subsystem, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, "Specify subsystem", nullptr, nullptr)
OPTION(prefix_1, "--summary", summary, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--swaprun=", swaprun, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Comma-separated list of 'cd' or 'net'", nullptr, nullptr)
OPTION(prefix_1, "--test-sign", test_sign, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Attach an Authenticode test signature to the output image", nullptr,
       nullptr)
OPTION(prefix_1, "--threads=", threads_eq, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0,
       "Number of worker threads; 1 disables multi-threading (default: "
       "chosen from the input size and available CPUs)",
       "<number>", nullptr)
OPTION(prefix_1, "--throwingnew", ignored_throwingnew, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--time-trace-granularity=", time_trace_granularity, Joined,
       INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Minimum time granularity (in microseconds) traced by the time profiler",
       nullptr, nullptr)
OPTION(prefix_1, "--time-trace=", time_trace_eq, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Record a time trace to the given file",
       nullptr, nullptr)
OPTION(prefix_1, "--time-trace", time_trace, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Record a time trace next to the output", nullptr,
       nullptr)
OPTION(prefix_1, "--timestamp=", timestamp, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, "Specify the PE header timestamp", nullptr, nullptr)
OPTION(prefix_1, "--time", time, Flag, INVALID, INVALID, nullptr, 0, DefaultVis,
       0, "Print timing information", nullptr, nullptr)
OPTION(prefix_1, "--tlbid=", ignored_tlbid, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--tlbout=", ignored_tlbout, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--tsaware", tsaware, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Create Terminal Server aware executable (default)",
       nullptr, nullptr)
OPTION(prefix_1, "--vctoolsdir=", vctoolsdir, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--vctoolsversion=", vctoolsversion, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--verbose=", verbose_eq, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Verbose mode", nullptr, nullptr)
OPTION(prefix_1, "--verbose", verbose, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Verbose mode", nullptr, nullptr)
OPTION(prefix_1, "--version=", version, Joined, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Specify a version number in the PE header", nullptr,
       nullptr)
OPTION(prefix_1, "--version", print_version, Flag, INVALID, INVALID, nullptr, 0,
       DefaultVis, 0, "Print the linker version", nullptr, nullptr)
OPTION(prefix_1, "--vfsoverlay=", vfsoverlay, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0,
       "Path to a vfsoverlay yaml file to optionally look for --defaultlib "
       "libraries in",
       nullptr, nullptr)
OPTION(prefix_1, "--wholearchive=", wholearchive_file, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Include all object files from this library",
       nullptr, nullptr)
OPTION(prefix_1, "--wholearchive", wholearchive_flag, Flag, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, "Include all object files from all libraries",
       nullptr, nullptr)
OPTION(prefix_1, "--winsdkdir=", winsdkdir, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--winsdkversion=", winsdkversion, Joined, INVALID, INVALID,
       nullptr, 0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--winsysroot=", winsysroot, Joined, INVALID, INVALID, nullptr,
       0, DefaultVis, 0, nullptr, nullptr, nullptr)
OPTION(prefix_1, "--wx", wx, Flag, INVALID, INVALID, nullptr, 0, DefaultVis, 0,
       "Treat warnings as errors", nullptr, nullptr)
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
