#ifndef NEVERC_FOUNDATION_CODEGENOPTIONS_H
#define NEVERC_FOUNDATION_CODEGENOPTIONS_H

#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Frontend/Debug/Options.h"
#include "llvm/Frontend/Driver/CodeGenOptions.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compression.h"
#include "llvm/Target/TargetOptions.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
class PassBuilder;
class Regex;
} // namespace llvm
namespace neverc {

class CodeGenOptionsBase {
  friend class CompilerInvocation;
  friend class CompilerInvocationBase;

public:
#define CODEGENOPT(Name, Bits, Default) unsigned Name : Bits;
#define ENUM_CODEGENOPT(Name, Type, Bits, Default)
#include "neverc/Foundation/LangOpts/CodeGenOptions.def"

protected:
#define CODEGENOPT(Name, Bits, Default)
#define ENUM_CODEGENOPT(Name, Type, Bits, Default) unsigned Name : Bits;
#include "neverc/Foundation/LangOpts/CodeGenOptions.def"
};

class CodeGenOptions : public CodeGenOptionsBase {
public:
  enum InliningMethod {
    NormalInlining,    // Use the standard function inlining pass.
    OnlyHintInlining,  // Inline only (implicitly) hinted functions.
    OnlyAlwaysInlining // Only run the always inlining pass.
  };

  enum TLSModel {
    GeneralDynamicTLSModel,
    LocalDynamicTLSModel,
    InitialExecTLSModel,
    LocalExecTLSModel
  };

  enum InlineAsmDialectKind {
    IAD_ATT,
    IAD_Intel,
  };

  enum DebugSrcHashKind {
    DSH_MD5,
    DSH_SHA1,
    DSH_SHA256,
  };

  // This field stores one of the allowed values for the option
  // -fbasic-block-sections=.  The allowed values with this option are:
  // {"labels", "all", "list=<file>", "none"}.
  //
  // "labels":      Only generate basic block symbols (labels) for all basic
  //                blocks, do not generate unique sections for basic blocks.
  //                Use the machine basic block id in the symbol name to
  //                associate profile info from virtual address to machine
  //                basic block.
  // "all" :        Generate basic block sections for all basic blocks.
  // "list=<file>": Generate basic block sections for a subset of basic blocks.
  //                The functions and the machine basic block ids are specified
  //                in the file.
  // "none":        Disable sections/labels for basic blocks.
  std::string BBSections;

  // If set, override the default value of MCAsmInfo::BinutilsVersion.
  // "none" means that all ELF features can be used, regardless of binutils
  // support.
  std::string BinutilsVersion;

  enum class FramePointerKind {
    None,    // Omit all frame pointers.
    NonLeaf, // Keep non-leaf frame pointers.
    All,     // Keep all frame pointers.
  };

  static llvm::StringRef getFramePointerKindName(FramePointerKind Kind) {
    switch (Kind) {
    case FramePointerKind::None:
      return "none";
    case FramePointerKind::NonLeaf:
      return "non-leaf";
    case FramePointerKind::All:
      return "all";
    }

    llvm_unreachable("invalid FramePointerKind");
  }

  enum FiniteLoopsKind {
    Language, // Not specified, use language standard.
    Always,   // All loops are assumed to be finite.
    Never,    // No loop is assumed to be finite.
  };

  enum AssignmentTrackingOpts {
    Disabled,
    Enabled,
    Forced,
  };

  std::string CodeModel;

  uint64_t LargeDataThreshold;

  std::string DebugPass;

  std::string DebugCompilationDir;

  std::string DwarfDebugFlags;

  std::string RecordCommandLine;

  llvm::SmallVector<std::pair<std::string, std::string>, 0> DebugPrefixMap;

  std::string FloatABI;

  std::string DIBugsReportFilePath;

  llvm::DenormalMode FPDenormalMode = llvm::DenormalMode::getIEEE();

  llvm::DenormalMode FP32DenormalMode = llvm::DenormalMode::getIEEE();

  std::string LimitFloatPrecision;

  struct BitcodeFileToLink {
    /// The filename of the bitcode file to link in.
    std::string Filename;
    /// If true, we set attributes functions in the bitcode library according to
    /// our CodeGenOptions, much as we set attrs on functions that we generate
    /// ourselves.
    bool PropagateAttrs = false;
    /// If true, we use LLVM module internalizer.
    bool Internalize = false;
    /// Bitwise combination of llvm::Linker::Flags, passed to the LLVM linker.
    unsigned LinkFlags = 0;
  };

  std::vector<BitcodeFileToLink> LinkBitcodeFiles;

  std::string MainFileName;

  std::string SplitDwarfFile;

  std::string SplitDwarfOutput;

  std::string ObjectFilenameForDebug;

  std::string TrapFuncName;

  std::vector<std::string> DependentLibraries;

  std::vector<std::string> LinkerOptions;

  std::string SaveTempsFilePrefix;

  std::string OptRecordFile;

  std::string OptRecordPasses;

  std::string OptRecordFormat;

  std::string SymbolPartition;

  enum RemarkKind {
    RK_Missing,            // Remark argument not present on the command line.
    RK_Enabled,            // Remark enabled via '-Rgroup'.
    RK_EnabledEverything,  // Remark enabled via '-Reverything'.
    RK_Disabled,           // Remark disabled via '-Rno-group'.
    RK_DisabledEverything, // Remark disabled via '-Rno-everything'.
    RK_WithPattern,        // Remark pattern specified via '-Rgroup=regexp'.
  };

  struct OptRemark {
    RemarkKind Kind = RK_Missing;
    std::string Pattern;
    std::shared_ptr<llvm::Regex> Regex;

    /// By default, optimization remark is missing.
    OptRemark() = default;

    /// Returns true iff the optimization remark holds a valid regular
    /// expression.
    bool hasValidPattern() const { return Regex != nullptr; }

    /// Matches the given string against the regex, if there is some.
    bool patternMatches(llvm::StringRef String) const;
  };

  OptRemark OptimizationRemark;

  OptRemark OptimizationRemarkMissed;

  OptRemark OptimizationRemarkAnalysis;

  std::vector<std::string> NoBuiltinFuncs;

  std::vector<std::string> Reciprocals;

  std::string PreferVectorWidth;

  std::vector<std::string> DefaultFunctionAttrs;

  std::vector<std::string> PassPlugins;

  std::vector<std::function<void(llvm::PassBuilder &)>> PassBuilderCallbacks;

  std::string StackProtectorGuard;

  std::string StackProtectorGuardReg;

  std::string StackProtectorGuardSymbol;

  std::string StackUsageOutput;

  const char *Argv0 = nullptr;
  std::vector<std::string> CommandLineArgs;

  std::optional<uint64_t> DiagnosticsHotnessThreshold = 0;

  std::string AsSecureLogFile;

public:
  // Define accessors/mutators for code generation options of enumeration type.
#define CODEGENOPT(Name, Bits, Default)
#define ENUM_CODEGENOPT(Name, Type, Bits, Default)                             \
  Type get##Name() const { return static_cast<Type>(Name); }                   \
  void set##Name(Type Value) { Name = static_cast<unsigned>(Value); }
#include "neverc/Foundation/LangOpts/CodeGenOptions.def"

  CodeGenOptions();

  const std::vector<std::string> &getNoBuiltinFuncs() const {
    return NoBuiltinFuncs;
  }

  bool hasReducedDebugInfo() const {
    return getDebugInfo() >= llvm::codegenoptions::DebugInfoConstructor;
  }

  bool hasMaybeUnusedDebugInfo() const {
    return getDebugInfo() >= llvm::codegenoptions::UnusedTypeInfo;
  }
};

} // end namespace neverc

#endif
