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

// What an ELF assembler reads out of a section's name before it looks at the
// flags written beside it.
//
// It adds these to what the directive states rather than letting the directive
// stand on its own, so a section named like one of these comes out carrying
// them whatever it asked for: anything called ".text" or ".text.<x>" is
// executable, ".data" and ".bss" are writable, ".rodata" is allocated, and
// ".tdata"/".tbss" are thread-local. Nothing reports the difference -- the
// object assembles, and reads back as a section with flags its author did not
// ask for -- so whoever writes the directive has to notice first.
//
// This mirrors the assembler's own list, and has to keep mirroring it: a name
// added there and not here goes back to being changed in silence.
//
// ELF is the only format with such a list. COFF and Mach-O reach a similar
// place by another route -- naming a section the assembler already built for
// itself returns that one, attributes and all -- but only for the exact names
// it presets, and their attributes are the ones a real ".text" or ".data"
// already has, so a section read out of an object and written back is
// unchanged. The ELF rule applies to whole families, ".text.<x>" as much as
// ".text", and that is what makes it worth stating.
struct SectionNameImpliedFlags {
  bool Allocated = false;
  bool Writable = false;
  bool Executable = false;
  bool ThreadLocal = false;
};

// True for the name itself and for the per-entity spelling that appends
// ".<something>", which is how the assembler reads the family too.
inline bool isSectionNameFamily(llvm::StringRef Name, llvm::StringRef Stem) {
  if (!Name.consume_front(Stem))
    return false;
  return Name.empty() || Name.front() == '.';
}

inline SectionNameImpliedFlags elfNameImpliedFlags(llvm::StringRef Name) {
  SectionNameImpliedFlags Result;
  if (isSectionNameFamily(Name, ".rodata") || Name == ".rodata1")
    Result.Allocated = true;
  else if (Name == ".fini" || Name == ".init" ||
           isSectionNameFamily(Name, ".text"))
    Result = {true, false, true, false};
  else if (isSectionNameFamily(Name, ".data") || Name == ".data1" ||
           isSectionNameFamily(Name, ".bss") ||
           isSectionNameFamily(Name, ".init_array") ||
           isSectionNameFamily(Name, ".fini_array") ||
           isSectionNameFamily(Name, ".preinit_array"))
    Result = {true, true, false, false};
  else if (isSectionNameFamily(Name, ".tdata") ||
           isSectionNameFamily(Name, ".tbss"))
    Result = {true, true, false, true};
  return Result;
}

// Whether a section with these flags can be written under this name without
// the assembler adding to them.
inline bool elfNameAgreesWithFlags(llvm::StringRef Name, bool Allocated,
                                   bool Writable, bool Executable,
                                   bool ThreadLocal) {
  const SectionNameImpliedFlags Implied = elfNameImpliedFlags(Name);
  return (!Implied.Allocated || Allocated) && (!Implied.Writable || Writable) &&
         (!Implied.Executable || Executable) &&
         (!Implied.ThreadLocal || ThreadLocal);
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
