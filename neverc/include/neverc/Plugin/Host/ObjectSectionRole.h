#ifndef NEVERC_PLUGIN_HOST_OBJECTSECTIONROLE_H
#define NEVERC_PLUGIN_HOST_OBJECTSECTIONROLE_H

#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "llvm/ADT/StringRef.h"

namespace neverc::plugin {

// What role a section plays, as far as its name can tell.
//
// Four places need this answer -- the built-in object reader and the ELF, COFF
// and Mach-O link adapters -- and each sees a different section type, so the
// name is the only thing they can share. Left to themselves they drifted apart:
// one matched ".eh_frame" exactly while another matched any name *containing*
// "unwind", so ".text._Unwind_Resume" was unwind data on one path and ordinary
// code on the other. Meanwhile ".pdata$foo", which is what a COMDAT .pdata is
// actually called, was recognised by neither, and no spelling of COFF's
// ".tls$..." matched the thread-local test at all.
//
// Prefer a native flag where the format has one: ELF states SHF_TLS and Mach-O
// has dedicated section types, and both are authoritative in a way a name never
// is. These are for the cases with nothing better -- COFF, which records
// neither, and .eh_frame, which is ordinary SHT_PROGBITS on most targets.

// COFF appends "$<key>" to a COMDAT section name; the linker groups by the key
// and keeps the part before it as the real section name.
inline llvm::StringRef coffSectionStem(llvm::StringRef Name) {
  return Name.split('$').first;
}

inline bool isUnwindSectionName(BuiltinObjectFormat Format,
                                llvm::StringRef Name) {
  switch (Format) {
  case BuiltinObjectFormat::ELF:
    // ".eh_frame.<func>" is the per-function spelling; the bare name and the
    // dotted prefix are matched separately so that ".eh_frame_hdr" is reached
    // by its own test rather than by a prefix that would also swallow it.
    return Name == ".eh_frame" || Name.starts_with(".eh_frame.") ||
           Name == ".eh_frame_hdr" ||
           Name.starts_with(".gcc_except_table") ||
           Name.starts_with(".ARM.exidx") || Name.starts_with(".ARM.extab");
  case BuiltinObjectFormat::COFF: {
    // The GNU toolchains targeting COFF emit DWARF unwind data into
    // ".eh_frame" rather than into the ".pdata"/".xdata" pair MSVC uses.
    const llvm::StringRef Stem = coffSectionStem(Name);
    return Stem == ".pdata" || Stem == ".xdata" || Stem == ".eh_frame";
  }
  case BuiltinObjectFormat::MachO:
    return Name == "__eh_frame" || Name == "__compact_unwind" ||
           Name == "__unwind_info" || Name == "__gcc_except_tab";
  }
  return false;
}

inline bool isDebugSectionName(BuiltinObjectFormat Format,
                               llvm::StringRef Name) {
  switch (Format) {
  case BuiltinObjectFormat::ELF:
    return Name.starts_with(".debug") || Name.starts_with(".zdebug");
  case BuiltinObjectFormat::COFF:
    return coffSectionStem(Name).starts_with(".debug");
  case BuiltinObjectFormat::MachO:
    return Name.starts_with("__debug");
  }
  return false;
}

inline bool isThreadLocalSectionName(BuiltinObjectFormat Format,
                                     llvm::StringRef Name) {
  switch (Format) {
  case BuiltinObjectFormat::ELF:
    return Name == ".tdata" || Name == ".tbss" ||
           Name.starts_with(".tdata.") || Name.starts_with(".tbss.");
  case BuiltinObjectFormat::COFF:
    // MSVC and LLVM emit thread-local data into ".tls$<key>", which the linker
    // merges into ".tls".
    return coffSectionStem(Name) == ".tls";
  case BuiltinObjectFormat::MachO:
    return Name == "__thread_data" || Name == "__thread_bss" ||
           Name == "__thread_vars" || Name == "__thread_ptrs" ||
           Name == "__thread_init";
  }
  return false;
}

} // namespace neverc::plugin

#endif
