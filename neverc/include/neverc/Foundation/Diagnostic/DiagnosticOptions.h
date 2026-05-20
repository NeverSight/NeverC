#ifndef NEVERC_FOUNDATION_DIAGNOSTICOPTIONS_H
#define NEVERC_FOUNDATION_DIAGNOSTICOPTIONS_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <type_traits>
#include <vector>

namespace llvm {
namespace opt {
class ArgList;
} // namespace opt
} // namespace llvm

namespace neverc {
class DiagnosticsEngine;

class DiagnosticOptions : public llvm::RefCountedBase<DiagnosticOptions> {
  friend bool ParseDiagnosticArgs(DiagnosticOptions &, llvm::opt::ArgList &,
                                  neverc::DiagnosticsEngine *, bool);

  friend class CompilerInvocation;
  friend class CompilerInvocationBase;

public:
  enum TextDiagnosticFormat { NeverC, MSVC, Vi };

  // Default values.
  enum {
    DefaultTabStop = 8,
    MaxTabStop = 100,
    DefaultMacroBacktraceLimit = 6,
    DefaultConstexprBacktraceLimit = 10,
    DefaultSpellCheckingLimit = 50,
    DefaultSnippetLineLimit = 16,
    DefaultShowLineNumbers = 1,
  };

  // Define simple diagnostic options (with no accessors).
#define DIAGOPT(Name, Bits, Default) unsigned Name : Bits;
#define ENUM_DIAGOPT(Name, Type, Bits, Default)
#include "neverc/Foundation/Diagnostic/DiagnosticOptions.def"

protected:
  // Define diagnostic options of enumeration type. These are private, and will
  // have accessors (below).
#define DIAGOPT(Name, Bits, Default)
#define ENUM_DIAGOPT(Name, Type, Bits, Default) unsigned Name : Bits;
#include "neverc/Foundation/Diagnostic/DiagnosticOptions.def"

public:
  std::vector<std::string> Warnings;

  std::vector<std::string> UndefPrefixes;

  std::vector<std::string> Remarks;

public:
  // Define accessors/mutators for diagnostic options of enumeration type.
#define DIAGOPT(Name, Bits, Default)
#define ENUM_DIAGOPT(Name, Type, Bits, Default)                                \
  Type get##Name() const { return static_cast<Type>(Name); }                   \
  void set##Name(Type Value) { Name = static_cast<unsigned>(Value); }
#include "neverc/Foundation/Diagnostic/DiagnosticOptions.def"

  DiagnosticOptions() {
#define DIAGOPT(Name, Bits, Default) Name = Default;
#define ENUM_DIAGOPT(Name, Type, Bits, Default) set##Name(Default);
#include "neverc/Foundation/Diagnostic/DiagnosticOptions.def"
  }
};

using TextDiagnosticFormat = DiagnosticOptions::TextDiagnosticFormat;

} // namespace neverc

#endif // NEVERC_FOUNDATION_DIAGNOSTICOPTIONS_H
