#include "neverc/Compiler/CompilerInvocation.h"
#include <fstream>
#include "neverc/Compiler/DependencyOutputOptions.h"
#include "neverc/Compiler/FrontendDiag.h"
#include "neverc/Compiler/FrontendOptions.h"
#include "neverc/Compiler/PrepOutputOptions.h"
#include "neverc/Compiler/Utils.h"
#include "neverc/Config/config.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/Version.h"
#include "neverc/Foundation/Core/Visibility.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/Diagnostic/DiagnosticDriver.h"
#include "neverc/Foundation/Target/TargetOptions.h"
#include "neverc/Invoke/Driver.h"
#include "neverc/Invoke/DriverDiagnostic.h"
#include "neverc/Invoke/Options.h"
#include "neverc/Scan/HeaderIndexOptions.h"
#include "neverc/Scan/PrepOptions.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Frontend/Debug/Options.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Linker/Linker.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptSpecifier.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Remarks/HotnessThresholdParser.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/HashBuilder.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace neverc;
using namespace driver;
using namespace options;
using namespace llvm::opt;

// ===----------------------------------------------------------------------===
// Argument normalization helpers
// ===----------------------------------------------------------------------===

namespace {
template <class T> std::shared_ptr<T> make_shared_copy(const T &X) {
  return std::make_shared<T>(X);
}

template <class T>
llvm::IntrusiveRefCntPtr<T> makeIntrusiveRefCntCopy(const T &X) {
  return llvm::makeIntrusiveRefCnt<T>(X);
}
} // namespace

CompilerInvocationBase::CompilerInvocationBase()
    : LangOpts(std::make_shared<LangOptions>()),
      TargetOpts(std::make_shared<TargetOptions>()),
      DiagnosticOpts(llvm::makeIntrusiveRefCnt<DiagnosticOptions>()),
      HSOpts(std::make_shared<HeaderIndexOptions>()),
      PPOpts(std::make_shared<PrepOptions>()),
      CodeGenOpts(std::make_shared<CodeGenOptions>()),
      FSOpts(std::make_shared<FileSystemOptions>()),
      FrontendOpts(std::make_shared<FrontendOptions>()),
      DependencyOutputOpts(std::make_shared<DependencyOutputOptions>()),
      PrepOutputOpts(std::make_shared<PrepOutputOptions>()) {}

CompilerInvocationBase &
CompilerInvocationBase::deep_copy_assign(const CompilerInvocationBase &X) {
  if (this != &X) {
    LangOpts = make_shared_copy(X.getLangOpts());
    TargetOpts = make_shared_copy(X.getTargetOpts());
    DiagnosticOpts = makeIntrusiveRefCntCopy(X.getDiagnosticOpts());
    HSOpts = make_shared_copy(X.getHeaderIdxOpts());
    PPOpts = make_shared_copy(X.getPrepOpts());
    CodeGenOpts = make_shared_copy(X.getCodeGenOpts());
    FSOpts = make_shared_copy(X.getFileSystemOpts());
    FrontendOpts = make_shared_copy(X.getFrontendOpts());
    DependencyOutputOpts = make_shared_copy(X.getDependencyOutputOpts());
    PrepOutputOpts = make_shared_copy(X.getPrepOutputOpts());
  }
  return *this;
}

CompilerInvocationBase &
CompilerInvocationBase::shallow_copy_assign(const CompilerInvocationBase &X) {
  if (this != &X) {
    LangOpts = X.LangOpts;
    TargetOpts = X.TargetOpts;
    DiagnosticOpts = X.DiagnosticOpts;
    HSOpts = X.HSOpts;
    PPOpts = X.PPOpts;
    CodeGenOpts = X.CodeGenOpts;
    FSOpts = X.FSOpts;
    FrontendOpts = X.FrontendOpts;
    DependencyOutputOpts = X.DependencyOutputOpts;
    PrepOutputOpts = X.PrepOutputOpts;
  }
  return *this;
}

namespace {
template <typename T> T &ensureOwned(std::shared_ptr<T> &Storage) {
  if (Storage.use_count() > 1)
    Storage = std::make_shared<T>(*Storage);
  return *Storage;
}

template <typename T> T &ensureOwned(llvm::IntrusiveRefCntPtr<T> &Storage) {
  if (Storage.useCount() > 1)
    Storage = llvm::makeIntrusiveRefCnt<T>(*Storage);
  return *Storage;
}
} // namespace

LangOptions &CowCompilerInvocation::getMutLangOpts() {
  return ensureOwned(LangOpts);
}

TargetOptions &CowCompilerInvocation::getMutTargetOpts() {
  return ensureOwned(TargetOpts);
}

DiagnosticOptions &CowCompilerInvocation::getMutDiagnosticOpts() {
  return ensureOwned(DiagnosticOpts);
}

HeaderIndexOptions &CowCompilerInvocation::getMutHeaderIdxOpts() {
  return ensureOwned(HSOpts);
}

PrepOptions &CowCompilerInvocation::getMutPrepOpts() {
  return ensureOwned(PPOpts);
}

CodeGenOptions &CowCompilerInvocation::getMutCodeGenOpts() {
  return ensureOwned(CodeGenOpts);
}

FileSystemOptions &CowCompilerInvocation::getMutFileSystemOpts() {
  return ensureOwned(FSOpts);
}

FrontendOptions &CowCompilerInvocation::getMutFrontendOpts() {
  return ensureOwned(FrontendOpts);
}

DependencyOutputOptions &CowCompilerInvocation::getMutDependencyOutputOpts() {
  return ensureOwned(DependencyOutputOpts);
}

PrepOutputOptions &CowCompilerInvocation::getMutPrepOutputOpts() {
  return ensureOwned(PrepOutputOpts);
}

using ArgumentConsumer = CompilerInvocation::ArgumentConsumer;

#define SIMPLE_ENUM_VALUE_TABLE
#include "neverc/Invoke/Options.td.h"
#undef SIMPLE_ENUM_VALUE_TABLE

namespace {
std::optional<bool> normalizeSimpleFlag(OptSpecifier Opt, unsigned TableIndex,
                                        const ArgList &Args,
                                        DiagnosticsEngine &Diags) {
  if (Args.hasArg(Opt))
    return true;
  return std::nullopt;
}

std::optional<bool> normalizeSimpleNegativeFlag(OptSpecifier Opt, unsigned,
                                                const ArgList &Args,
                                                DiagnosticsEngine &) {
  if (Args.hasArg(Opt))
    return false;
  return std::nullopt;
}

void denormalizeSimpleFlag(ArgumentConsumer Consumer,
                           const llvm::Twine &Spelling, Option::OptionClass,
                           unsigned, /*T*/...) {
  Consumer(Spelling);
}

template <typename T> constexpr bool is_uint64_t_convertible() {
  return !std::is_same_v<T, uint64_t> && llvm::is_integral_or_enum<T>::value;
}

template <typename T,
          std::enable_if_t<!is_uint64_t_convertible<T>(), bool> = false>
auto makeFlagToValueNormalizer(T Value) {
  return [Value](OptSpecifier Opt, unsigned, const ArgList &Args,
                 DiagnosticsEngine &) -> std::optional<T> {
    if (Args.hasArg(Opt))
      return Value;
    return std::nullopt;
  };
}

template <typename T,
          std::enable_if_t<is_uint64_t_convertible<T>(), bool> = false>
auto makeFlagToValueNormalizer(T Value) {
  return makeFlagToValueNormalizer(uint64_t(Value));
}

auto makeBooleanOptionNormalizer(bool Value, bool OtherValue,
                                 OptSpecifier OtherOpt) {
  return [Value, OtherValue,
          OtherOpt](OptSpecifier Opt, unsigned, const ArgList &Args,
                    DiagnosticsEngine &) -> std::optional<bool> {
    if (const Arg *A = Args.getLastArg(Opt, OtherOpt)) {
      return A->getOption().matches(Opt) ? Value : OtherValue;
    }
    return std::nullopt;
  };
}

auto makeBooleanOptionDenormalizer(bool Value) {
  return [Value](ArgumentConsumer Consumer, const llvm::Twine &Spelling,
                 Option::OptionClass, unsigned, bool KeyPath) {
    if (KeyPath == Value)
      Consumer(Spelling);
  };
}

void denormalizeString(ArgumentConsumer Consumer, const llvm::Twine &Spelling,
                       Option::OptionClass OptClass, unsigned,
                       const llvm::Twine &Value) {
  switch (OptClass) {
  case Option::SeparateClass:
  case Option::JoinedOrSeparateClass:
  case Option::JoinedAndSeparateClass:
    Consumer(Spelling);
    Consumer(Value);
    break;
  case Option::JoinedClass:
  case Option::CommaJoinedClass:
    Consumer(Spelling + Value);
    break;
  default:
    llvm_unreachable("Cannot denormalize an option with option class "
                     "incompatible with string denormalization.");
  }
}

template <typename T>
void denormalizeString(ArgumentConsumer Consumer, const llvm::Twine &Spelling,
                       Option::OptionClass OptClass, unsigned TableIndex,
                       T Value) {
  denormalizeString(Consumer, Spelling, OptClass, TableIndex,
                    llvm::Twine(Value));
}

std::optional<SimpleEnumValue>
findValueTableByName(const SimpleEnumValueTable &Table, llvm::StringRef Name) {
  for (int I = 0, E = Table.Size; I != E; ++I)
    if (Name == Table.Table[I].Name)
      return Table.Table[I];

  return std::nullopt;
}

std::optional<SimpleEnumValue>
findValueTableByValue(const SimpleEnumValueTable &Table, unsigned Value) {
  for (int I = 0, E = Table.Size; I != E; ++I)
    if (Value == Table.Table[I].Value)
      return Table.Table[I];

  return std::nullopt;
}

std::optional<unsigned> normalizeSimpleEnum(OptSpecifier Opt,
                                            unsigned TableIndex,
                                            const ArgList &Args,
                                            DiagnosticsEngine &Diags) {
  assert(TableIndex < SimpleEnumValueTablesSize);
  const SimpleEnumValueTable &Table = SimpleEnumValueTables[TableIndex];

  auto *Arg = Args.getLastArg(Opt);
  if (!Arg)
    return std::nullopt;

  llvm::StringRef ArgValue = Arg->getValue();
  if (auto MaybeEnumVal = findValueTableByName(Table, ArgValue))
    return MaybeEnumVal->Value;

  Diags.Report(diag::err_drv_invalid_value)
      << Arg->getAsString(Args) << ArgValue;
  return std::nullopt;
}

void denormalizeSimpleEnumImpl(ArgumentConsumer Consumer,
                               const llvm::Twine &Spelling,
                               Option::OptionClass OptClass,
                               unsigned TableIndex, unsigned Value) {
  assert(TableIndex < SimpleEnumValueTablesSize);
  const SimpleEnumValueTable &Table = SimpleEnumValueTables[TableIndex];
  if (auto MaybeEnumVal = findValueTableByValue(Table, Value)) {
    denormalizeString(Consumer, Spelling, OptClass, TableIndex,
                      MaybeEnumVal->Name);
  } else {
    llvm_unreachable("The simple enum value was not correctly defined in "
                     "the tablegen option description");
  }
}

template <typename T>
void denormalizeSimpleEnum(ArgumentConsumer Consumer,
                           const llvm::Twine &Spelling,
                           Option::OptionClass OptClass, unsigned TableIndex,
                           T Value) {
  return denormalizeSimpleEnumImpl(Consumer, Spelling, OptClass, TableIndex,
                                   static_cast<unsigned>(Value));
}

std::optional<std::string> normalizeString(OptSpecifier Opt, int TableIndex,
                                           const ArgList &Args,
                                           DiagnosticsEngine &Diags) {
  auto *Arg = Args.getLastArg(Opt);
  if (!Arg)
    return std::nullopt;
  return std::string(Arg->getValue());
}

template <typename IntTy>
std::optional<IntTy> normalizeStringIntegral(OptSpecifier Opt, int,
                                             const ArgList &Args,
                                             DiagnosticsEngine &Diags) {
  auto *Arg = Args.getLastArg(Opt);
  if (!Arg)
    return std::nullopt;
  IntTy Res;
  if (llvm::StringRef(Arg->getValue()).getAsInteger(0, Res)) {
    Diags.Report(diag::err_drv_invalid_int_value)
        << Arg->getAsString(Args) << Arg->getValue();
    return std::nullopt;
  }
  return Res;
}

std::optional<std::vector<std::string>>
normalizeStringVector(OptSpecifier Opt, int, const ArgList &Args,
                      DiagnosticsEngine &) {
  return Args.getAllArgValues(Opt);
}

void denormalizeStringVector(ArgumentConsumer Consumer,
                             const llvm::Twine &Spelling,
                             Option::OptionClass OptClass, unsigned TableIndex,
                             const std::vector<std::string> &Values) {
  switch (OptClass) {
  case Option::CommaJoinedClass: {
    std::string CommaJoinedValue;
    if (!Values.empty()) {
      CommaJoinedValue.append(Values.front());
      for (const std::string &Value : llvm::drop_begin(Values, 1)) {
        CommaJoinedValue.append(",");
        CommaJoinedValue.append(Value);
      }
    }
    denormalizeString(Consumer, Spelling, Option::OptionClass::JoinedClass,
                      TableIndex, CommaJoinedValue);
    break;
  }
  case Option::JoinedClass:
  case Option::SeparateClass:
  case Option::JoinedOrSeparateClass:
    for (const std::string &Value : Values)
      denormalizeString(Consumer, Spelling, OptClass, TableIndex, Value);
    break;
  default:
    llvm_unreachable("Cannot denormalize an option with option class "
                     "incompatible with string vector denormalization.");
  }
}

std::optional<std::string> normalizeTriple(OptSpecifier Opt, int TableIndex,
                                           const ArgList &Args,
                                           DiagnosticsEngine &Diags) {
  auto *Arg = Args.getLastArg(Opt);
  if (!Arg)
    return std::nullopt;
  return llvm::Triple::normalize(Arg->getValue());
}

template <typename T, typename U> T mergeForwardValue(T KeyPath, U Value) {
  return static_cast<T>(Value);
}

template <typename T, typename U> T mergeMaskValue(T KeyPath, U Value) {
  return KeyPath | Value;
}

template <typename T> T extractForwardValue(T KeyPath) { return KeyPath; }

template <typename T, typename U, U Value> T extractMaskValue(T KeyPath) {
  return ((KeyPath & Value) == Value) ? static_cast<T>(Value) : T();
}
} // namespace

#define PARSE_OPTION_WITH_MARSHALLING(                                         \
    ARGS, DIAGS, PREFIX_TYPE, SPELLING, ID, KIND, GROUP, ALIAS, ALIASARGS,     \
    FLAGS, VISIBILITY, PARAM, HELPTEXT, METAVAR, VALUES, SHOULD_PARSE,         \
    ALWAYS_EMIT, KEYPATH, DEFAULT_VALUE, IMPLIED_CHECK, IMPLIED_VALUE,         \
    NORMALIZER, DENORMALIZER, MERGER, EXTRACTOR, TABLE_INDEX)                  \
  if ((VISIBILITY) & options::NeverCOption) {                                  \
    KEYPATH = MERGER(KEYPATH, DEFAULT_VALUE);                                  \
    if (IMPLIED_CHECK)                                                         \
      KEYPATH = MERGER(KEYPATH, IMPLIED_VALUE);                                \
    if (SHOULD_PARSE)                                                          \
      if (auto MaybeValue = NORMALIZER(OPT_##ID, TABLE_INDEX, ARGS, DIAGS))    \
        KEYPATH =                                                              \
            MERGER(KEYPATH, static_cast<decltype(KEYPATH)>(*MaybeValue));      \
  }

// Capture the extracted value as a lambda argument to avoid potential issues
// with lifetime extension of the reference.
#define GENERATE_OPTION_WITH_MARSHALLING(                                      \
    CONSUMER, PREFIX_TYPE, SPELLING, ID, KIND, GROUP, ALIAS, ALIASARGS, FLAGS, \
    VISIBILITY, PARAM, HELPTEXT, METAVAR, VALUES, SHOULD_PARSE, ALWAYS_EMIT,   \
    KEYPATH, DEFAULT_VALUE, IMPLIED_CHECK, IMPLIED_VALUE, NORMALIZER,          \
    DENORMALIZER, MERGER, EXTRACTOR, TABLE_INDEX)                              \
  if ((VISIBILITY) & options::NeverCOption) {                                  \
    [&](const auto &Extracted) {                                               \
      if (ALWAYS_EMIT ||                                                       \
          (Extracted !=                                                        \
           static_cast<decltype(KEYPATH)>((IMPLIED_CHECK) ? (IMPLIED_VALUE)    \
                                                          : (DEFAULT_VALUE)))) \
        DENORMALIZER(CONSUMER, SPELLING, Option::KIND##Class, TABLE_INDEX,     \
                     Extracted);                                               \
    }(EXTRACTOR(KEYPATH));                                                     \
  }

// ===----------------------------------------------------------------------===
// Validation & optimization level
// ===----------------------------------------------------------------------===

namespace {
bool validateInvocation(CompilerInvocation &Invocation,
                        DiagnosticsEngine &Diags, const ArgList &Args,
                        InputKind IK) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  LangOptions &LangOpts = Invocation.getLangOpts();
  CodeGenOptions &CodeGenOpts = Invocation.getCodeGenOpts();
  TargetOptions &TargetOpts = Invocation.getTargetOpts();
  FrontendOptions &FrontendOpts = Invocation.getFrontendOpts();
  CodeGenOpts.DisableFree = FrontendOpts.DisableFree;
  if (FrontendOpts.ShowStats)
    CodeGenOpts.ClearASTBeforeBackend = false;
  LangOpts.SpeculativeLoadHardening = CodeGenOpts.SpeculativeLoadHardening;

  llvm::Triple T(TargetOpts.Triple);

  CodeGenOpts.CodeModel = TargetOpts.CodeModel;
  CodeGenOpts.LargeDataThreshold = TargetOpts.LargeDataThreshold;

  if (LangOpts.IgnoreExceptions) {
    LangOpts.Exceptions = 0;
    LangOpts.EHAsynch = 0;
    LangOpts.setExceptionHandling(LangOptions::ExceptionHandlingKind::None);
  }

  if (LangOpts.getExceptionHandling() !=
          LangOptions::ExceptionHandlingKind::None &&
      T.isWindowsMSVCEnvironment())
    Diags.Report(diag::err_fe_invalid_exception_model)
        << static_cast<unsigned>(LangOpts.getExceptionHandling()) << T.str();

  // When these options are used, the compiler is allowed to apply
  // optimizations that may affect the final result. For example
  // (x+y)+z is transformed to x+(y+z) but may not give the same
  // final result; it's not value safe.
  // Another example can be to simplify x/x to 1.0 but x could be 0.0, INF
  // or NaN. Final result may then differ. An error is issued when the eval
  // method is set with one of these options.
  if (Args.hasArg(OPT_ffp_eval_method_EQ)) {
    if (LangOpts.ApproxFunc)
      Diags.Report(diag::err_incompatible_fp_eval_method_options) << 0;
    if (LangOpts.AllowFPReassoc)
      Diags.Report(diag::err_incompatible_fp_eval_method_options) << 1;
    if (LangOpts.AllowRecip)
      Diags.Report(diag::err_incompatible_fp_eval_method_options) << 2;
  }

  if (Arg *A = Args.getLastArg(OPT_fdefault_calling_conv_EQ)) {
    auto DefaultCC = LangOpts.getDefaultCallingConv();

    bool emitError = (DefaultCC == LangOptions::DCC_FastCall ||
                      DefaultCC == LangOptions::DCC_StdCall);
    emitError |= (DefaultCC == LangOptions::DCC_VectorCall ||
                  DefaultCC == LangOptions::DCC_RegCall) &&
                 !T.isX86();
    if (emitError)
      Diags.Report(diag::err_drv_argument_not_allowed_with)
          << A->getSpelling() << T.getTriple();
  }

  return Diags.getNumErrors() == NumErrorsBefore;
}

unsigned getOptimizationLevel(ArgList &Args, InputKind IK,
                              DiagnosticsEngine &Diags) {
  unsigned DefaultOpt = 0;

  if (Arg *A = Args.getLastArg(options::OPT_O_Group)) {
    if (A->getOption().matches(options::OPT_O0))
      return 0;

    if (A->getOption().matches(options::OPT_Ofast))
      return 3;

    assert(A->getOption().matches(options::OPT_O));

    llvm::StringRef S(A->getValue());
    if (S == "s" || S == "z")
      return 2;

    if (S == "g")
      return 1;

    return getLastArgIntValue(Args, OPT_O, DefaultOpt, Diags);
  }

  return DefaultOpt;
}

unsigned getOptimizationLevelSize(ArgList &Args) {
  if (Arg *A = Args.getLastArg(options::OPT_O_Group)) {
    if (A->getOption().matches(options::OPT_O)) {
      switch (A->getValue()[0]) {
      default:
        return 0;
      case 's':
        return 1;
      case 'z':
        return 2;
      }
    }
  }
  return 0;
}

void emitArg(ArgumentConsumer Consumer, llvm::opt::OptSpecifier OptSpecifier) {
  Option Opt = getDriverOptTable().getOption(OptSpecifier);
  denormalizeSimpleFlag(Consumer, Opt.getPrefixedName(),
                        Option::OptionClass::FlagClass, 0);
}

void emitArg(ArgumentConsumer Consumer, llvm::opt::OptSpecifier OptSpecifier,
             const llvm::Twine &Value) {
  Option Opt = getDriverOptTable().getOption(OptSpecifier);
  denormalizeString(Consumer, Opt.getPrefixedName(), Opt.getKind(), 0, Value);
}

void addDiagnosticArgs(ArgList &Args, OptSpecifier Group,
                       OptSpecifier GroupWithValue,
                       std::vector<std::string> &Diagnostics) {
  for (auto *A : Args.filtered(Group)) {
    if (A->getOption().getKind() == Option::FlagClass) {
      // The argument is a pure flag (such as OPT_Wall or OPT_Wdeprecated). Add
      // its name (minus the "W" or "R" at the beginning) to the diagnostics.
      Diagnostics.push_back(
          std::string(A->getOption().getName().drop_front(1)));
    } else if (A->getOption().matches(GroupWithValue)) {
      // This is -Wfoo= or -Rfoo=, where foo is the name of the diagnostic
      // group. Add only the group name to the diagnostics.
      Diagnostics.push_back(
          std::string(A->getOption().getName().drop_front(1).rtrim("=-")));
    } else {
      // Otherwise, add its value (for OPT_W_Joined and similar).
      Diagnostics.push_back(A->getValue());
    }
  }
}

void collectNoBuiltinFuncs(ArgList &Args, std::vector<std::string> &Funcs) {
  std::vector<std::string> Values = Args.getAllArgValues(OPT_fno_builtin_);
  auto BuiltinEnd = llvm::partition(Values, Builtin::Context::isBuiltinFunc);
  Funcs.insert(Funcs.end(), Values.begin(), BuiltinEnd);
}

void generateOptimizationRemark(ArgumentConsumer Consumer, OptSpecifier OptEQ,
                                llvm::StringRef Name,
                                const CodeGenOptions::OptRemark &Remark) {
  if (Remark.hasValidPattern()) {
    emitArg(Consumer, OptEQ, Remark.Pattern);
  } else if (Remark.Kind == CodeGenOptions::RK_Enabled) {
    emitArg(Consumer, OPT_R_Joined, Name);
  } else if (Remark.Kind == CodeGenOptions::RK_Disabled) {
    emitArg(Consumer, OPT_R_Joined, llvm::StringRef("no-") + Name);
  }
}

CodeGenOptions::OptRemark parseOptimizationRemark(DiagnosticsEngine &Diags,
                                                  ArgList &Args,
                                                  OptSpecifier OptEQ,
                                                  llvm::StringRef Name) {
  CodeGenOptions::OptRemark Result;

  auto InitializeResultPattern =
      [&Diags, &Args, &Result](const Arg *A, llvm::StringRef Pattern) {
        Result.Pattern = Pattern.str();

        llvm::SmallString<256> RegexError;
        Result.Regex = std::make_shared<llvm::Regex>(Result.Pattern);
        if (!Result.Regex->isValid(RegexError)) {
          Diags.Report(diag::err_drv_optimization_remark_pattern)
              << RegexError << A->getAsString(Args);
          return false;
        }

        return true;
      };

  for (Arg *A : Args) {
    if (A->getOption().matches(OPT_R_Joined)) {
      llvm::StringRef Value = A->getValue();

      if (Value == Name)
        Result.Kind = CodeGenOptions::RK_Enabled;
      else if (Value == "everything")
        Result.Kind = CodeGenOptions::RK_EnabledEverything;
      else if (Value.split('-') == std::make_pair(llvm::StringRef("no"), Name))
        Result.Kind = CodeGenOptions::RK_Disabled;
      else if (Value == "no-everything")
        Result.Kind = CodeGenOptions::RK_DisabledEverything;
      else
        continue;

      if (Result.Kind == CodeGenOptions::RK_Disabled ||
          Result.Kind == CodeGenOptions::RK_DisabledEverything) {
        Result.Pattern = "";
        Result.Regex = nullptr;
      } else {
        InitializeResultPattern(A, ".*");
      }
    } else if (A->getOption().matches(OptEQ)) {
      Result.Kind = CodeGenOptions::RK_WithPattern;
      if (!InitializeResultPattern(A, A->getValue()))
        return CodeGenOptions::OptRemark();
    }
  }

  return Result;
}
} // namespace

// ===----------------------------------------------------------------------===
// CodeGen options
// ===----------------------------------------------------------------------===

void CompilerInvocationBase::GenerateCodeGenArgs(const CodeGenOptions &Opts,
                                                 ArgumentConsumer Consumer,
                                                 const llvm::Triple &T,
                                                 const std::string &OutputFile,
                                                 const LangOptions *LangOpts) {
  const CodeGenOptions &CodeGenOpts = Opts;

  if (Opts.OptimizationLevel == 0)
    emitArg(Consumer, OPT_O0);
  else
    emitArg(Consumer, OPT_O, llvm::Twine(Opts.OptimizationLevel));

#define CODEGEN_OPTION_WITH_MARSHALLING(...)                                   \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef CODEGEN_OPTION_WITH_MARSHALLING

  if (Opts.OptimizationLevel > 0) {
    if (Opts.Inlining == CodeGenOptions::NormalInlining)
      emitArg(Consumer, OPT_finline_functions);
    else if (Opts.Inlining == CodeGenOptions::OnlyHintInlining)
      emitArg(Consumer, OPT_finline_hint_functions);
    else if (Opts.Inlining == CodeGenOptions::OnlyAlwaysInlining)
      emitArg(Consumer, OPT_fno_inline);
  }

  if (Opts.DirectAccessExternalData)
    emitArg(Consumer, OPT_fdirect_access_external_data);

  std::optional<llvm::StringRef> DebugInfoVal;
  switch (Opts.DebugInfo) {
  case llvm::codegenoptions::DebugLineTablesOnly:
    DebugInfoVal = "line-tables-only";
    break;
  case llvm::codegenoptions::DebugDirectivesOnly:
    DebugInfoVal = "line-directives-only";
    break;
  case llvm::codegenoptions::DebugInfoConstructor:
    DebugInfoVal = "constructor";
    break;
  case llvm::codegenoptions::LimitedDebugInfo:
    DebugInfoVal = "limited";
    break;
  case llvm::codegenoptions::FullDebugInfo:
    DebugInfoVal = "standalone";
    break;
  case llvm::codegenoptions::UnusedTypeInfo:
    DebugInfoVal = "unused-types";
    break;
  case llvm::codegenoptions::NoDebugInfo: // default value
    DebugInfoVal = std::nullopt;
    break;
  case llvm::codegenoptions::LocTrackingOnly: // implied value
    DebugInfoVal = std::nullopt;
    break;
  }
  if (DebugInfoVal)
    emitArg(Consumer, OPT_debug_info_kind_EQ, *DebugInfoVal);

  for (const auto &Prefix : Opts.DebugPrefixMap)
    emitArg(Consumer, OPT_fdebug_prefix_map_EQ,
            Prefix.first + "=" + Prefix.second);

  if (Opts.NewStructPathTBAA)
    emitArg(Consumer, OPT_new_struct_path_tbaa);

  if (Opts.OptimizeSize == 1)
    emitArg(Consumer, OPT_O, "s");
  else if (Opts.OptimizeSize == 2)
    emitArg(Consumer, OPT_O, "z");

  // SimplifyLibCalls is set only in the absence of -fno-builtin and
  // -ffreestanding. We'll consider that when generating them.

  // NoBuiltinFuncs are generated by LangOptions.

  if (Opts.UnrollLoops && Opts.OptimizationLevel <= 1)
    emitArg(Consumer, OPT_funroll_loops);
  else if (!Opts.UnrollLoops && Opts.OptimizationLevel > 1)
    emitArg(Consumer, OPT_fno_unroll_loops);

  if (!Opts.BinutilsVersion.empty())
    emitArg(Consumer, OPT_fbinutils_version_EQ, Opts.BinutilsVersion);

  if (Opts.DebugNameTable ==
      static_cast<unsigned>(llvm::DICompileUnit::DebugNameTableKind::GNU))
    emitArg(Consumer, OPT_ggnu_pubnames);
  else if (Opts.DebugNameTable ==
           static_cast<unsigned>(
               llvm::DICompileUnit::DebugNameTableKind::Default))
    emitArg(Consumer, OPT_gpubnames);

  if (Opts.TimePasses) {
    if (Opts.TimePassesPerRun)
      emitArg(Consumer, OPT_ftime_report_EQ, "per-pass-run");
    else
      emitArg(Consumer, OPT_ftime_report);
  }

  if (Opts.PrepareForLTO)
    emitArg(Consumer, OPT_flto_EQ, "full");

  if (Opts.SaveTempsFilePrefix == OutputFile)
    emitArg(Consumer, OPT_save_temps_EQ, "obj");

  if (Opts.CFProtectionReturn && Opts.CFProtectionBranch)
    emitArg(Consumer, OPT_fcf_protection_EQ, "full");
  else if (Opts.CFProtectionReturn)
    emitArg(Consumer, OPT_fcf_protection_EQ, "return");
  else if (Opts.CFProtectionBranch)
    emitArg(Consumer, OPT_fcf_protection_EQ, "branch");

  if (Opts.FunctionReturnThunks)
    emitArg(Consumer, OPT_mfunction_return_EQ, "thunk-extern");

  for (const auto &F : Opts.LinkBitcodeFiles) {
    bool Builtint = F.LinkFlags == llvm::Linker::Flags::LinkOnlyNeeded &&
                    F.PropagateAttrs && F.Internalize;
    emitArg(Consumer,
            Builtint ? OPT_mlink_builtin_bitcode : OPT_mlink_bitcode_file,
            F.Filename);
  }

  if (Opts.EmulatedTLS)
    emitArg(Consumer, OPT_femulated_tls);

  if (Opts.FPDenormalMode != llvm::DenormalMode::getIEEE())
    emitArg(Consumer, OPT_fdenormal_fp_math_EQ, Opts.FPDenormalMode.str());

  if ((Opts.FPDenormalMode != Opts.FP32DenormalMode) ||
      (Opts.FP32DenormalMode != llvm::DenormalMode::getIEEE()))
    emitArg(Consumer, OPT_fdenormal_fp_math_f32_EQ,
            Opts.FP32DenormalMode.str());

  if (!Opts.OptRecordPasses.empty())
    emitArg(Consumer, OPT_opt_record_passes, Opts.OptRecordPasses);

  if (!Opts.OptRecordFormat.empty())
    emitArg(Consumer, OPT_opt_record_format, Opts.OptRecordFormat);

  generateOptimizationRemark(Consumer, OPT_Rpass_EQ, "pass",
                             Opts.OptimizationRemark);

  generateOptimizationRemark(Consumer, OPT_Rpass_missed_EQ, "pass-missed",
                             Opts.OptimizationRemarkMissed);

  generateOptimizationRemark(Consumer, OPT_Rpass_analysis_EQ, "pass-analysis",
                             Opts.OptimizationRemarkAnalysis);

  emitArg(Consumer, OPT_fdiagnostics_hotness_threshold_EQ,
          Opts.DiagnosticsHotnessThreshold
              ? llvm::Twine(*Opts.DiagnosticsHotnessThreshold)
              : "auto");

  if (!Opts.EmitVersionIdentMetadata)
    emitArg(Consumer, OPT_Qn);

  switch (Opts.FiniteLoops) {
  case CodeGenOptions::FiniteLoopsKind::Language:
    break;
  case CodeGenOptions::FiniteLoopsKind::Always:
    emitArg(Consumer, OPT_ffinite_loops);
    break;
  case CodeGenOptions::FiniteLoopsKind::Never:
    emitArg(Consumer, OPT_fno_finite_loops);
    break;
  }
}

bool CompilerInvocation::ParseCodeGenArgs(CodeGenOptions &Opts, ArgList &Args,
                                          InputKind IK,
                                          DiagnosticsEngine &Diags,
                                          const llvm::Triple &T,
                                          const std::string &OutputFile,
                                          const LangOptions &LangOptsRef) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  unsigned OptimizationLevel = getOptimizationLevel(Args, IK, Diags);
  unsigned MaxOptLevel = 3;
  if (OptimizationLevel > MaxOptLevel) {
    // If the optimization level is not supported, fall back on the default
    // optimization
    Diags.Report(diag::warn_drv_optimization_value)
        << Args.getLastArg(OPT_O)->getAsString(Args) << "-O" << MaxOptLevel;
    OptimizationLevel = MaxOptLevel;
  }
  Opts.OptimizationLevel = OptimizationLevel;

  // The key paths of codegen options defined in Options.td start with
  // "CodeGenOpts.". Let's provide the expected variable name and type.
  CodeGenOptions &CodeGenOpts = Opts;
  // Some codegen options depend on language options. Let's provide the expected
  // variable name and type.
  const LangOptions *LangOpts = &LangOptsRef;

#define CODEGEN_OPTION_WITH_MARSHALLING(...)                                   \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef CODEGEN_OPTION_WITH_MARSHALLING

  // At O0 we want to fully disable inlining outside of cases marked with
  // 'alwaysinline' that are required for correctness.
  if (Opts.OptimizationLevel == 0) {
    Opts.setInlining(CodeGenOptions::OnlyAlwaysInlining);
  } else if (const Arg *A = Args.getLastArg(options::OPT_finline_functions,
                                            options::OPT_finline_hint_functions,
                                            options::OPT_fno_inline_functions,
                                            options::OPT_fno_inline)) {
    // Explicit inlining flags can disable some or all inlining even at
    // optimization levels above zero.
    if (A->getOption().matches(options::OPT_finline_functions))
      Opts.setInlining(CodeGenOptions::NormalInlining);
    else if (A->getOption().matches(options::OPT_finline_hint_functions))
      Opts.setInlining(CodeGenOptions::OnlyHintInlining);
    else
      Opts.setInlining(CodeGenOptions::OnlyAlwaysInlining);
  } else {
    Opts.setInlining(CodeGenOptions::NormalInlining);
  }

  Opts.DirectAccessExternalData =
      Args.hasArg(OPT_fdirect_access_external_data) &&
      !Args.hasArg(OPT_fno_direct_access_external_data);

  if (Arg *A = Args.getLastArg(OPT_debug_info_kind_EQ)) {
    unsigned Val =
        llvm::StringSwitch<unsigned>(A->getValue())
            .Case("line-tables-only", llvm::codegenoptions::DebugLineTablesOnly)
            .Case("line-directives-only",
                  llvm::codegenoptions::DebugDirectivesOnly)
            .Case("constructor", llvm::codegenoptions::DebugInfoConstructor)
            .Case("limited", llvm::codegenoptions::LimitedDebugInfo)
            .Case("standalone", llvm::codegenoptions::FullDebugInfo)
            .Case("unused-types", llvm::codegenoptions::UnusedTypeInfo)
            .Default(~0U);
    if (Val == ~0U)
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
    else
      Opts.setDebugInfo(static_cast<llvm::codegenoptions::DebugInfoKind>(Val));
  }

  for (const auto &Arg : Args.getAllArgValues(OPT_fdebug_prefix_map_EQ)) {
    auto Split = llvm::StringRef(Arg).split('=');
    Opts.DebugPrefixMap.emplace_back(Split.first, Split.second);
  }

  const llvm::Triple::ArchType DebugEntryValueArchs[] = {llvm::Triple::x86_64,
                                                         llvm::Triple::aarch64};

  if (Opts.OptimizationLevel > 0 && Opts.hasReducedDebugInfo() &&
      llvm::is_contained(DebugEntryValueArchs, T.getArch()))
    Opts.EmitCallSiteInfo = true;

  if (!Opts.EnableDIPreservationVerify && Opts.DIBugsReportFilePath.size()) {
    Diags.Report(diag::warn_ignoring_verify_debuginfo_preserve_export)
        << Opts.DIBugsReportFilePath;
    Opts.DIBugsReportFilePath = "";
  }

  Opts.NewStructPathTBAA = !Args.hasArg(OPT_no_struct_path_tbaa) &&
                           Args.hasArg(OPT_new_struct_path_tbaa);
  Opts.OptimizeSize = getOptimizationLevelSize(Args);
  Opts.SimplifyLibCalls = !LangOpts->NoBuiltin;
  if (Opts.SimplifyLibCalls)
    Opts.NoBuiltinFuncs = LangOpts->NoBuiltinFuncs;
  Opts.UnrollLoops = Args.hasFlag(OPT_funroll_loops, OPT_fno_unroll_loops,
                                  (Opts.OptimizationLevel > 1));
  Opts.BinutilsVersion =
      std::string(Args.getLastArgValue(OPT_fbinutils_version_EQ));

  Opts.DebugNameTable = static_cast<unsigned>(
      Args.hasArg(OPT_ggnu_pubnames)
          ? llvm::DICompileUnit::DebugNameTableKind::GNU
      : Args.hasArg(OPT_gpubnames)
          ? llvm::DICompileUnit::DebugNameTableKind::Default
          : llvm::DICompileUnit::DebugNameTableKind::None);

  if (const Arg *A = Args.getLastArg(OPT_ftime_report, OPT_ftime_report_EQ)) {
    Opts.TimePasses = true;

    // -ftime-report= is only for new pass manager.
    if (A->getOption().getID() == OPT_ftime_report_EQ) {
      llvm::StringRef Val = A->getValue();
      if (Val == "per-pass")
        Opts.TimePassesPerRun = false;
      else if (Val == "per-pass-run")
        Opts.TimePassesPerRun = true;
      else
        Diags.Report(diag::err_drv_invalid_value)
            << A->getAsString(Args) << A->getValue();
    }
  }

  Opts.PrepareForLTO = false;
  Opts.InMemoryLTOOutput = false;
  if (Arg *A = Args.getLastArg(OPT_flto_EQ)) {
    Opts.PrepareForLTO = true;
    llvm::StringRef S = A->getValue();
    if (S != "full")
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << S;
  }
  if (Arg *A = Args.getLastArg(OPT_save_temps_EQ))
    Opts.SaveTempsFilePrefix =
        llvm::StringSwitch<std::string>(A->getValue())
            .Case("obj", OutputFile)
            .Default(llvm::sys::path::filename(OutputFile).str());

  if (const Arg *A = Args.getLastArg(OPT_fcf_protection_EQ)) {
    llvm::StringRef Name = A->getValue();
    if (Name == "full") {
      Opts.CFProtectionReturn = 1;
      Opts.CFProtectionBranch = 1;
    } else if (Name == "return")
      Opts.CFProtectionReturn = 1;
    else if (Name == "branch")
      Opts.CFProtectionBranch = 1;
    else if (Name != "none")
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Name;
  }

  if (const Arg *A = Args.getLastArg(OPT_mfunction_return_EQ)) {
    auto Val = llvm::StringSwitch<llvm::FunctionReturnThunksKind>(A->getValue())
                   .Case("keep", llvm::FunctionReturnThunksKind::Keep)
                   .Case("thunk-extern", llvm::FunctionReturnThunksKind::Extern)
                   .Default(llvm::FunctionReturnThunksKind::Invalid);
    if (!T.isX86())
      Diags.Report(diag::err_drv_argument_not_allowed_with)
          << A->getSpelling() << T.getTriple();
    else if (Val == llvm::FunctionReturnThunksKind::Invalid)
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
    else if (Val == llvm::FunctionReturnThunksKind::Extern &&
             Args.getLastArgValue(OPT_mcmodel_EQ).equals("large"))
      Diags.Report(diag::err_drv_argument_not_allowed_with)
          << A->getAsString(Args)
          << Args.getLastArg(OPT_mcmodel_EQ)->getAsString(Args);
    else
      Opts.FunctionReturnThunks = static_cast<unsigned>(Val);
  }

  for (auto *A :
       Args.filtered(OPT_mlink_bitcode_file, OPT_mlink_builtin_bitcode)) {
    CodeGenOptions::BitcodeFileToLink F;
    F.Filename = A->getValue();
    if (A->getOption().matches(OPT_mlink_builtin_bitcode)) {
      F.LinkFlags = llvm::Linker::Flags::LinkOnlyNeeded;
      // When linking builtin bitcode, propagate function attributes.
      F.PropagateAttrs = true;
      F.Internalize = true;
    }
    Opts.LinkBitcodeFiles.push_back(F);
  }

  if (Arg *A = Args.getLastArg(OPT_fdenormal_fp_math_EQ)) {
    llvm::StringRef Val = A->getValue();
    Opts.FPDenormalMode = llvm::parseDenormalFPAttribute(Val);
    Opts.FP32DenormalMode = Opts.FPDenormalMode;
    if (!Opts.FPDenormalMode.isValid())
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Val;
  }

  if (Arg *A = Args.getLastArg(OPT_fdenormal_fp_math_f32_EQ)) {
    llvm::StringRef Val = A->getValue();
    Opts.FP32DenormalMode = llvm::parseDenormalFPAttribute(Val);
    if (!Opts.FP32DenormalMode.isValid())
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Val;
  }

  bool NeedLocTracking = false;

  if (!Opts.OptRecordFile.empty())
    NeedLocTracking = true;

  if (Arg *A = Args.getLastArg(OPT_opt_record_passes)) {
    Opts.OptRecordPasses = A->getValue();
    NeedLocTracking = true;
  }

  if (Arg *A = Args.getLastArg(OPT_opt_record_format)) {
    Opts.OptRecordFormat = A->getValue();
    NeedLocTracking = true;
  }

  Opts.OptimizationRemark =
      parseOptimizationRemark(Diags, Args, OPT_Rpass_EQ, "pass");

  Opts.OptimizationRemarkMissed =
      parseOptimizationRemark(Diags, Args, OPT_Rpass_missed_EQ, "pass-missed");

  Opts.OptimizationRemarkAnalysis = parseOptimizationRemark(
      Diags, Args, OPT_Rpass_analysis_EQ, "pass-analysis");

  NeedLocTracking |= Opts.OptimizationRemark.hasValidPattern() ||
                     Opts.OptimizationRemarkMissed.hasValidPattern() ||
                     Opts.OptimizationRemarkAnalysis.hasValidPattern();

  // Parse remarks hotness threshold. Valid value is either integer or 'auto'.
  if (auto *arg =
          Args.getLastArg(options::OPT_fdiagnostics_hotness_threshold_EQ)) {
    auto ResultOrErr =
        llvm::remarks::parseHotnessThresholdOption(arg->getValue());

    if (!ResultOrErr)
      Diags.Report(diag::err_drv_invalid_diagnotics_hotness_threshold)
          << "-fdiagnostics-hotness-threshold=";
    else
      Opts.DiagnosticsHotnessThreshold = *ResultOrErr;
  }

  if (!Opts.StackUsageOutput.empty())
    NeedLocTracking = true;

  // If the user requested a flag that requires source locations available in
  // the backend, make sure that the backend tracks source location information.
  if (NeedLocTracking &&
      Opts.getDebugInfo() == llvm::codegenoptions::NoDebugInfo)
    Opts.setDebugInfo(llvm::codegenoptions::LocTrackingOnly);

  Opts.EmitVersionIdentMetadata = Args.hasFlag(OPT_Qy, OPT_Qn, true);

  if (Args.hasArg(options::OPT_ffinite_loops))
    Opts.FiniteLoops = CodeGenOptions::FiniteLoopsKind::Always;
  else if (Args.hasArg(options::OPT_fno_finite_loops))
    Opts.FiniteLoops = CodeGenOptions::FiniteLoopsKind::Never;

  if (Args.hasArg(OPT_fno_parallel_codegen)) {
    Opts.ParallelCodeGen = 1;
  } else if (const Arg *A = Args.getLastArg(OPT_fparallel_codegen_EQ)) {
    unsigned Val;
    if (llvm::StringRef(A->getValue()).getAsInteger(10, Val))
      Diags.Report(diag::err_drv_invalid_int_value)
          << A->getAsString(Args) << A->getValue();
    else
      Opts.ParallelCodeGen = Val;
  }

  return Diags.getNumErrors() == NumErrorsBefore;
}

// ===----------------------------------------------------------------------===
// Dependency, filesystem & diagnostic options
// ===----------------------------------------------------------------------===

namespace {
void generateDependencyOutputArgs(const DependencyOutputOptions &Opts,
                                  ArgumentConsumer Consumer) {
  const DependencyOutputOptions &DependencyOutputOpts = Opts;
#define DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING(...)                         \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING

  if (Opts.ShowIncludesDest != ShowIncludesDestination::None)
    emitArg(Consumer, OPT_show_includes);

  for (const auto &Dep : Opts.ExtraDeps) {
    if (Dep.second == EDK_DepFileEntry)
      emitArg(Consumer, OPT_fdepfile_entry, Dep.first);
  }
}

bool parseDependencyOutputArgs(DependencyOutputOptions &Opts, ArgList &Args,
                               DiagnosticsEngine &Diags,
                               frontend::ActionKind Action,
                               bool ShowLineMarkers) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  DependencyOutputOptions &DependencyOutputOpts = Opts;
#define DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING(...)                         \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING

  if (Args.hasArg(OPT_show_includes)) {
    // Writing both /showIncludes and preprocessor output to stdout
    // would produce interleaved output, so use stderr for /showIncludes.
    // This behaves the same as cl.exe, when /E, /EP or /P are passed.
    if (Action == frontend::PrintPreprocessedInput || !ShowLineMarkers)
      Opts.ShowIncludesDest = ShowIncludesDestination::Stderr;
    else
      Opts.ShowIncludesDest = ShowIncludesDestination::Stdout;
  } else {
    Opts.ShowIncludesDest = ShowIncludesDestination::None;
  }

  // Propagate the extra dependencies.
  for (const auto *A : Args.filtered(OPT_fdepfile_entry))
    Opts.ExtraDeps.emplace_back(A->getValue(), EDK_DepFileEntry);

  // Check for invalid combinations of header-include-format
  // and header-include-filtering.
  if ((Opts.HeaderIncludeFormat == HIFMT_Textual &&
       Opts.HeaderIncludeFiltering != HIFIL_None) ||
      (Opts.HeaderIncludeFormat == HIFMT_JSON &&
       Opts.HeaderIncludeFiltering != HIFIL_Only_Direct_System))
    Diags.Report(diag::err_drv_print_header_env_var_combination)
        << Args.getLastArg(OPT_header_include_format_EQ)->getValue()
        << Args.getLastArg(OPT_header_include_filtering_EQ)->getValue();

  return Diags.getNumErrors() == NumErrorsBefore;
}

bool resolveColorOption(const ArgList &Args, bool DefaultColor) {
  // Color diagnostics default to auto ("on" if terminal supports) in the driver
  // but default to off in the frontend, needing an explicit
  // OPT_fdiagnostics_color. Support both NeverC's -f[no-]color-diagnostics and
  // gcc's -f[no-]diagnostics-colors[=never|always|auto].
  enum {
    Colors_On,
    Colors_Off,
    Colors_Auto
  } ShowColors = DefaultColor ? Colors_Auto : Colors_Off;
  for (auto *A : Args) {
    const Option &O = A->getOption();
    if (O.matches(options::OPT_fcolor_diagnostics)) {
      ShowColors = Colors_On;
    } else if (O.matches(options::OPT_fno_color_diagnostics)) {
      ShowColors = Colors_Off;
    } else if (O.matches(options::OPT_fdiagnostics_color_EQ)) {
      llvm::StringRef Value(A->getValue());
      if (Value == "always")
        ShowColors = Colors_On;
      else if (Value == "never")
        ShowColors = Colors_Off;
      else if (Value == "auto")
        ShowColors = Colors_Auto;
    }
  }
  return ShowColors == Colors_On ||
         (ShowColors == Colors_Auto &&
          llvm::sys::Process::StandardErrHasColors());
}

void generateFileSystemArgs(const FileSystemOptions &Opts,
                            ArgumentConsumer Consumer) {
  const FileSystemOptions &FileSystemOpts = Opts;

#define FILE_SYSTEM_OPTION_WITH_MARSHALLING(...)                               \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef FILE_SYSTEM_OPTION_WITH_MARSHALLING
}

bool parseFileSystemArgs(FileSystemOptions &Opts, const ArgList &Args,
                         DiagnosticsEngine &Diags) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  FileSystemOptions &FileSystemOpts = Opts;

#define FILE_SYSTEM_OPTION_WITH_MARSHALLING(...)                               \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef FILE_SYSTEM_OPTION_WITH_MARSHALLING

  return Diags.getNumErrors() == NumErrorsBefore;
}
} // namespace

void CompilerInvocationBase::GenerateDiagnosticArgs(
    const DiagnosticOptions &Opts, ArgumentConsumer Consumer,
    bool DefaultDiagColor) {
  const DiagnosticOptions *DiagnosticOpts = &Opts;
#define DIAG_OPTION_WITH_MARSHALLING(...)                                      \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef DIAG_OPTION_WITH_MARSHALLING

  if (Opts.ShowColors)
    emitArg(Consumer, OPT_fcolor_diagnostics);

  for (const auto &Warning : Opts.Warnings) {
    // This option is automatically generated from UndefPrefixes.
    if (Warning == "undef-prefix")
      continue;
    Consumer(llvm::StringRef("-W") + Warning);
  }

  for (const auto &Remark : Opts.Remarks) {
    // These arguments are generated from OptimizationRemark fields of
    // CodeGenOptions.
    llvm::StringRef IgnoredRemarks[] = {"pass",          "no-pass",
                                        "pass-analysis", "no-pass-analysis",
                                        "pass-missed",   "no-pass-missed"};
    if (llvm::is_contained(IgnoredRemarks, Remark))
      continue;

    Consumer(llvm::StringRef("-R") + Remark);
  }
}

std::unique_ptr<DiagnosticOptions>
neverc::CreateAndPopulateDiagOpts(llvm::ArrayRef<const char *> Argv) {
  auto DiagOpts = std::make_unique<DiagnosticOptions>();
  unsigned MissingArgIndex, MissingArgCount;
  InputArgList Args = getDriverOptTable().ParseArgs(
      Argv.slice(1), MissingArgIndex, MissingArgCount);

  bool ShowColors = true;
  if (auto NoColor = llvm::sys::Process::GetEnv("NO_COLOR");
      NoColor && !NoColor->empty()) {
    // If the user set the NO_COLOR environment variable, we'll honor that
    // unless the command line overrides it.
    ShowColors = false;
  }

  // We ignore MissingArgCount and the return value of ParseDiagnosticArgs.
  // Any errors that would be diagnosed here will also be diagnosed later,
  // when the DiagnosticsEngine actually exists.
  (void)ParseDiagnosticArgs(*DiagOpts, Args, /*Diags=*/nullptr, ShowColors);
  return DiagOpts;
}

bool neverc::ParseDiagnosticArgs(DiagnosticOptions &Opts, ArgList &Args,
                                 DiagnosticsEngine *Diags,
                                 bool DefaultDiagColor) {
  std::optional<DiagnosticsEngine> IgnoringDiags;
  if (!Diags) {
    IgnoringDiags.emplace(new DiagnosticIDs(), new DiagnosticOptions(),
                          new IgnoringDiagConsumer());
    Diags = &*IgnoringDiags;
  }

  unsigned NumErrorsBefore = Diags->getNumErrors();

  // The key paths of diagnostic options defined in Options.td start with
  // "DiagnosticOpts->". Let's provide the expected variable name and type.
  DiagnosticOptions *DiagnosticOpts = &Opts;

#define DIAG_OPTION_WITH_MARSHALLING(...)                                      \
  PARSE_OPTION_WITH_MARSHALLING(Args, *Diags, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef DIAG_OPTION_WITH_MARSHALLING

  llvm::sys::Process::UseANSIEscapeCodes(Opts.UseANSIEscapeCodes);

  Opts.ShowColors = resolveColorOption(Args, DefaultDiagColor);

  if (Opts.TabStop == 0 || Opts.TabStop > DiagnosticOptions::MaxTabStop) {
    Diags->Report(diag::warn_ignoring_ftabstop_value)
        << Opts.TabStop << DiagnosticOptions::DefaultTabStop;
    Opts.TabStop = DiagnosticOptions::DefaultTabStop;
  }

  addDiagnosticArgs(Args, OPT_W_Group, OPT_W_value_Group, Opts.Warnings);
  addDiagnosticArgs(Args, OPT_R_Group, OPT_R_value_Group, Opts.Remarks);

  return Diags->getNumErrors() == NumErrorsBefore;
}

namespace {
const auto &getFrontendActionTable() {
  static const std::pair<frontend::ActionKind, unsigned> Table[] = {
      {frontend::GenAssembly, OPT_S},
      {frontend::GenBC, OPT_emit_llvm_bc},
      {frontend::GenLLVM, OPT_emit_llvm},
      {frontend::GenObj, OPT_emit_obj},
      {frontend::ParseSyntaxOnly, OPT_fsyntax_only},
      {frontend::PrintPreprocessedInput, OPT_E},
      {frontend::RunPreprocessorOnly, OPT_Eonly},
  };

  return Table;
}

std::optional<frontend::ActionKind> getFrontendAction(OptSpecifier &Opt) {
  for (const auto &ActionOpt : getFrontendActionTable())
    if (ActionOpt.second == Opt.getID())
      return ActionOpt.first;

  return std::nullopt;
}

std::optional<OptSpecifier>
getProgramActionOpt(frontend::ActionKind ProgramAction) {
  for (const auto &ActionOpt : getFrontendActionTable())
    if (ActionOpt.first == ProgramAction)
      return OptSpecifier(ActionOpt.second);

  return std::nullopt;
}

// ===----------------------------------------------------------------------===
// Frontend options
// ===----------------------------------------------------------------------===

void generateFrontendArgs(const FrontendOptions &Opts,
                          ArgumentConsumer Consumer, bool IsHeader) {
  const FrontendOptions &FrontendOpts = Opts;
#define FRONTEND_OPTION_WITH_MARSHALLING(...)                                  \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef FRONTEND_OPTION_WITH_MARSHALLING

  std::optional<OptSpecifier> ProgramActionOpt =
      getProgramActionOpt(Opts.ProgramAction);
  assert(ProgramActionOpt && "Frontend action without option.");
  emitArg(Consumer, *ProgramActionOpt);

  {
    llvm::StringRef Preprocessed =
        Opts.DashX.isPreprocessed() ? "-cpp-output" : "";
    llvm::StringRef Header = IsHeader ? "-header" : "";

    llvm::StringRef Lang;
    switch (Opts.DashX.getLanguage()) {
    case Language::C:
      Lang = "c";
      break;
    case Language::Asm:
      Lang = "assembler-with-cpp";
      break;
    case Language::Unknown:
      Lang = "unknown";
      break;
    case Language::LLVM_IR:
      Lang = "ir";
      break;
    }

    emitArg(Consumer, OPT_x, Lang + Header + Preprocessed);
  }

  // OPT_INPUT has a unique class, generate it directly.
  for (const auto &Input : Opts.Inputs)
    Consumer(Input.getFile());
}

bool parseFrontendArgs(FrontendOptions &Opts, ArgList &Args,
                       DiagnosticsEngine &Diags, bool &IsHeaderFile) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  FrontendOptions &FrontendOpts = Opts;

#define FRONTEND_OPTION_WITH_MARSHALLING(...)                                  \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef FRONTEND_OPTION_WITH_MARSHALLING

  Opts.ProgramAction = frontend::ParseSyntaxOnly;
  if (const Arg *A = Args.getLastArg(OPT_Action_Group)) {
    OptSpecifier Opt = OptSpecifier(A->getOption().getID());
    std::optional<frontend::ActionKind> ProgramAction = getFrontendAction(Opt);
    if (!ProgramAction) {
      Diags.Report(diag::err_drv_unsupported) << A->getAsString(Args);
    } else {
      Opts.ProgramAction = *ProgramAction;
    }
  }

  InputKind DashX(Language::Unknown);
  if (const Arg *A = Args.getLastArg(OPT_x)) {
    llvm::StringRef XValue = A->getValue();

    // Parse suffixes: '<lang>(-header)(-cpp-output)'.
    bool Preprocessed = XValue.consume_back("-cpp-output");
    bool IsHeader = XValue.consume_back("-header");

    IsHeaderFile = IsHeader && !Preprocessed;

    if (XValue != "c" && XValue != "c-header" && XValue != "cpp-output" &&
        XValue != "assembler-with-cpp" && XValue != "ir" && XValue != "ast") {
      Diags.Report(diag::err_drv_unsupported) << XValue;
      return false;
    }
    DashX = llvm::StringSwitch<InputKind>(XValue)
                .Case("c", Language::C)
                .Default(Language::Unknown);

    if (DashX.isUnknown() && !Preprocessed && !IsHeaderFile)
      DashX = llvm::StringSwitch<InputKind>(XValue)
                  .Case("cpp-output", InputKind(Language::C).getPreprocessed())
                  .Case("assembler-with-cpp", Language::Asm)
                  .Case("ir", Language::LLVM_IR)
                  .Default(Language::Unknown);

    if (DashX.isUnknown())
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();

    if (Preprocessed)
      DashX = DashX.getPreprocessed();
    if (IsHeaderFile)
      DashX = DashX.getHeader();
  }

  // '-' is the default input if none is given.
  std::vector<std::string> Inputs = Args.getAllArgValues(OPT_INPUT);
  Opts.Inputs.clear();
  if (Inputs.empty())
    Inputs.push_back("-");

  for (unsigned i = 0, e = Inputs.size(); i != e; ++i) {
    InputKind IK = DashX;
    if (IK.isUnknown()) {
      IK = FrontendOptions::getInputKindForExtension(
          llvm::StringRef(Inputs[i]).rsplit('.').second);
      if (IK.isUnknown())
        IK = Language::C;
    }

    if (IK.getLanguage() != Language::C && IK.getLanguage() != Language::Asm &&
        IK.getLanguage() != Language::LLVM_IR &&
        IK.getLanguage() != Language::Unknown) {
      Diags.Report(diag::err_drv_unsupported) << Inputs[i];
      continue;
    }

    if (DashX.isUnknown() && i == 0)
      DashX = IK;

    Opts.Inputs.emplace_back(std::move(Inputs[i]), IK);
  }

  Opts.DashX = DashX;

  return Diags.getNumErrors() == NumErrorsBefore;
}
} // namespace

// ===----------------------------------------------------------------------===
// Header search & language options
// ===----------------------------------------------------------------------===

std::string CompilerInvocation::GetResourcesPath(const char *Argv0,
                                                 void *MainAddr) {
  std::string Executable = llvm::sys::fs::getMainExecutable(Argv0, MainAddr);
  return Driver::GetResourcesPath(Executable, NEVERC_RESOURCE_DIR);
}

namespace {
void generateHeaderSearchArgs(const HeaderIndexOptions &Opts,
                              ArgumentConsumer Consumer) {
  const HeaderIndexOptions *HeaderIdxOpts = &Opts;
#define HEADER_SEARCH_OPTION_WITH_MARSHALLING(...)                             \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef HEADER_SEARCH_OPTION_WITH_MARSHALLING

  auto Matches = [](const HeaderIndexOptions::Entry &Entry,
                    llvm::ArrayRef<frontend::IncludeDirGroup> Groups,
                    std::optional<bool> IsFramework,
                    std::optional<bool> IgnoreSysRoot) {
    return llvm::is_contained(Groups, Entry.Group) &&
           (!IsFramework || (Entry.IsFramework == *IsFramework)) &&
           (!IgnoreSysRoot || (Entry.IgnoreSysRoot == *IgnoreSysRoot));
  };

  auto It = Opts.UserEntries.begin();
  auto End = Opts.UserEntries.end();

  // Add -I..., -F..., and -index-header-map options in order.
  for (; It < End && Matches(*It, {frontend::IndexHeaderMap, frontend::Angled},
                             std::nullopt, true);
       ++It) {
    OptSpecifier Opt = [It, Matches]() {
      if (Matches(*It, frontend::IndexHeaderMap, true, true))
        return OPT_F;
      if (Matches(*It, frontend::IndexHeaderMap, false, true))
        return OPT_I;
      if (Matches(*It, frontend::Angled, true, true))
        return OPT_F;
      if (Matches(*It, frontend::Angled, false, true))
        return OPT_I;
      llvm_unreachable("Unexpected HeaderIndexOptions::Entry.");
    }();

    if (It->Group == frontend::IndexHeaderMap)
      emitArg(Consumer, OPT_index_header_map);
    emitArg(Consumer, Opt, It->Path);
  };

  // Note: some paths that came from "[-iprefix=xx] -iwithprefixbefore=yy" may
  // have already been generated as "-I[xx]yy". If that's the case, their
  // position on command line was such that this has no semantic impact on
  // include paths.
  for (; It < End &&
         Matches(*It, {frontend::After, frontend::Angled}, false, true);
       ++It) {
    OptSpecifier Opt =
        It->Group == frontend::After ? OPT_iwithprefix : OPT_iwithprefixbefore;
    emitArg(Consumer, Opt, It->Path);
  }

  // Note: Some paths that came from "-idirafter=xxyy" may have already been
  // generated as "-iwithprefix=xxyy". If that's the case, their position on
  // command line was such that this has no semantic impact on include paths.
  for (; It < End && Matches(*It, {frontend::After}, false, true); ++It)
    emitArg(Consumer, OPT_idirafter, It->Path);
  for (; It < End && Matches(*It, {frontend::Quoted}, false, true); ++It)
    emitArg(Consumer, OPT_iquote, It->Path);
  for (; It < End && Matches(*It, {frontend::System}, false, std::nullopt);
       ++It)
    emitArg(Consumer, It->IgnoreSysRoot ? OPT_isystem : OPT_iwithsysroot,
            It->Path);
  for (; It < End && Matches(*It, {frontend::System}, true, true); ++It)
    emitArg(Consumer, OPT_iframework, It->Path);
  for (; It < End && Matches(*It, {frontend::System}, true, false); ++It)
    emitArg(Consumer, OPT_iframeworkwithsysroot, It->Path);

  // Add the paths for the various language specific isystem flags.
  for (; It < End && Matches(*It, {frontend::CSystem}, false, true); ++It)
    emitArg(Consumer, OPT_c_isystem, It->Path);

  // Add the internal paths from a driver that detects standard include paths.
  // Note: Some paths that came from "-internal-isystem" arguments may have
  // already been generated as "-isystem". If that's the case, their position on
  // command line was such that this has no semantic impact on include paths.
  for (; It < End &&
         Matches(*It, {frontend::System, frontend::ExternCSystem}, false, true);
       ++It) {
    OptSpecifier Opt = It->Group == frontend::System
                           ? OPT_internal_isystem
                           : OPT_internal_externc_isystem;
    emitArg(Consumer, Opt, It->Path);
  }

  assert(It == End && "Unhandled HeaderSearchOption::Entry.");

  // Add the path prefixes which are implicitly treated as being system headers.
  for (const auto &P : Opts.SystemHeaderPrefixes) {
    OptSpecifier Opt = P.IsSystemHeader ? OPT_system_header_prefix
                                        : OPT_no_system_header_prefix;
    emitArg(Consumer, Opt, P.Prefix);
  }

  for (const std::string &F : Opts.VFSOverlayFiles)
    emitArg(Consumer, OPT_ivfsoverlay, F);
}

bool parseHeaderSearchArgs(HeaderIndexOptions &Opts, ArgList &Args,
                           DiagnosticsEngine &Diags,
                           const std::string &WorkingDir) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  HeaderIndexOptions *HeaderIdxOpts = &Opts;

#define HEADER_SEARCH_OPTION_WITH_MARSHALLING(...)                             \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef HEADER_SEARCH_OPTION_WITH_MARSHALLING

  // Add -I..., -F..., and -index-header-map options in order.
  bool IsIndexHeaderMap = false;
  bool IsSysrootSpecified =
      Args.hasArg(OPT__sysroot_EQ) || Args.hasArg(OPT_isysroot);
  for (const auto *A : Args.filtered(OPT_I, OPT_F, OPT_index_header_map)) {
    if (A->getOption().matches(OPT_index_header_map)) {
      // -index-header-map applies to the next -I or -F.
      IsIndexHeaderMap = true;
      continue;
    }

    frontend::IncludeDirGroup Group =
        IsIndexHeaderMap ? frontend::IndexHeaderMap : frontend::Angled;

    bool IsFramework = A->getOption().matches(OPT_F);
    std::string Path = A->getValue();

    if (IsSysrootSpecified && !IsFramework && A->getValue()[0] == '=') {
      llvm::SmallString<32> Buffer;
      llvm::sys::path::append(Buffer, Opts.Sysroot,
                              llvm::StringRef(A->getValue()).substr(1));
      Path = std::string(Buffer.str());
    }

    Opts.AddPath(Path, Group, IsFramework,
                 /*IgnoreSysroot*/ true);
    IsIndexHeaderMap = false;
  }

  // Add -iprefix/-iwithprefix/-iwithprefixbefore options.
  llvm::StringRef Prefix = "";
  for (const auto *A :
       Args.filtered(OPT_iprefix, OPT_iwithprefix, OPT_iwithprefixbefore)) {
    if (A->getOption().matches(OPT_iprefix))
      Prefix = A->getValue();
    else if (A->getOption().matches(OPT_iwithprefix))
      Opts.AddPath(Prefix.str() + A->getValue(), frontend::After, false, true);
    else
      Opts.AddPath(Prefix.str() + A->getValue(), frontend::Angled, false, true);
  }

  for (const auto *A : Args.filtered(OPT_idirafter))
    Opts.AddPath(A->getValue(), frontend::After, false, true);
  for (const auto *A : Args.filtered(OPT_iquote))
    Opts.AddPath(A->getValue(), frontend::Quoted, false, true);
  for (const auto *A : Args.filtered(OPT_isystem, OPT_iwithsysroot))
    Opts.AddPath(A->getValue(), frontend::System, false,
                 !A->getOption().matches(OPT_iwithsysroot));
  for (const auto *A : Args.filtered(OPT_iframework))
    Opts.AddPath(A->getValue(), frontend::System, true, true);
  for (const auto *A : Args.filtered(OPT_iframeworkwithsysroot))
    Opts.AddPath(A->getValue(), frontend::System, /*IsFramework=*/true,
                 /*IgnoreSysRoot=*/false);

  // Add the paths for the various language specific isystem flags.
  for (const auto *A : Args.filtered(OPT_c_isystem))
    Opts.AddPath(A->getValue(), frontend::CSystem, false, true);

  // Add the internal paths from a driver that detects standard include paths.
  for (const auto *A :
       Args.filtered(OPT_internal_isystem, OPT_internal_externc_isystem)) {
    frontend::IncludeDirGroup Group = frontend::System;
    if (A->getOption().matches(OPT_internal_externc_isystem))
      Group = frontend::ExternCSystem;
    Opts.AddPath(A->getValue(), Group, false, true);
  }

  // Add the path prefixes which are implicitly treated as being system headers.
  for (const auto *A :
       Args.filtered(OPT_system_header_prefix, OPT_no_system_header_prefix))
    Opts.AddSystemHeaderPrefix(
        A->getValue(), A->getOption().matches(OPT_system_header_prefix));

  for (const auto *A : Args.filtered(OPT_ivfsoverlay, OPT_vfsoverlay))
    Opts.AddVFSOverlayFile(A->getValue());

  return Diags.getNumErrors() == NumErrorsBefore;
}

bool isInputCompatibleWithStandard(InputKind IK, const LangStandard &S) {
  switch (IK.getLanguage()) {
  case Language::Unknown:
  case Language::LLVM_IR:
    llvm_unreachable("should not parse language flags for this input");

  case Language::C:
    return S.getLanguage() == Language::C;

  case Language::Asm:
    return true;
  }
}

llvm::StringRef getInputKindName(InputKind IK) {
  switch (IK.getLanguage()) {
  case Language::C:
    return "C";
  case Language::Asm:
    return "Asm";
  case Language::LLVM_IR:
    return "LLVM IR";
  case Language::Unknown:
    break;
  }
  llvm_unreachable("unknown input language");
}
} // namespace

void CompilerInvocationBase::GenerateLangArgs(const LangOptions &Opts,
                                              ArgumentConsumer Consumer,
                                              const llvm::Triple &T,
                                              InputKind IK) {
  if (IK.getLanguage() == Language::LLVM_IR)
    return;

  auto LangStandard = LangStandard::getLangStandardForKind(Opts.LangStd);
  emitArg(Consumer, OPT_std_EQ, LangStandard.getName());

  const LangOptions *LangOpts = &Opts;

#define LANG_OPTION_WITH_MARSHALLING(...)                                      \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef LANG_OPTION_WITH_MARSHALLING

  // The '-fcf-protection=' option is generated by CodeGenOpts generator.

  if (Opts.GNUCVersion != 0) {
    unsigned Major = Opts.GNUCVersion / 100 / 100;
    unsigned Minor = (Opts.GNUCVersion / 100) % 100;
    unsigned Patch = Opts.GNUCVersion % 100;
    emitArg(Consumer, OPT_fgnuc_version_EQ,
            llvm::Twine(Major) + "." + llvm::Twine(Minor) + "." +
                llvm::Twine(Patch));
  }

  if (Opts.SignedOverflowBehavior == LangOptions::SOB_Trapping) {
    emitArg(Consumer, OPT_ftrapv);
    emitArg(Consumer, OPT_ftrapv_handler, Opts.OverflowHandler);
  } else if (Opts.SignedOverflowBehavior == LangOptions::SOB_Defined) {
    emitArg(Consumer, OPT_fwrapv);
  }

  if (Opts.MSCompatibilityVersion != 0) {
    unsigned Major = Opts.MSCompatibilityVersion / 10000000;
    unsigned Minor = (Opts.MSCompatibilityVersion / 100000) % 100;
    unsigned Subminor = Opts.MSCompatibilityVersion % 100000;
    emitArg(Consumer, OPT_fms_compatibility_version,
            llvm::Twine(Major) + "." + llvm::Twine(Minor) + "." +
                llvm::Twine(Subminor));
  }

  if (!Opts.GNUMode && !Opts.MSVCCompat) {
    if (!Opts.Trigraphs)
      emitArg(Consumer, OPT_fno_trigraphs);
  } else {
    if (Opts.Trigraphs)
      emitArg(Consumer, OPT_ftrigraphs);
  }

  if (Opts.NoBuiltin && !Opts.Freestanding)
    emitArg(Consumer, OPT_fno_builtin);

  if (!Opts.NoBuiltin)
    for (const auto &Func : Opts.NoBuiltinFuncs)
      emitArg(Consumer, OPT_fno_builtin_, Func);

  if (Opts.LongDoubleSize == 128)
    emitArg(Consumer, OPT_mlong_double_128);
  else if (Opts.LongDoubleSize == 64)
    emitArg(Consumer, OPT_mlong_double_64);
  else if (Opts.LongDoubleSize == 80)
    emitArg(Consumer, OPT_mlong_double_80);

  // The arguments used to set Optimize, OptimizeSize and NoInlineDefine are
  // generated from CodeGenOptions.

  if (Opts.DefaultFPContractMode == LangOptions::FPM_Fast)
    emitArg(Consumer, OPT_ffp_contract, "fast");
  else if (Opts.DefaultFPContractMode == LangOptions::FPM_On)
    emitArg(Consumer, OPT_ffp_contract, "on");
  else if (Opts.DefaultFPContractMode == LangOptions::FPM_Off)
    emitArg(Consumer, OPT_ffp_contract, "off");
  else if (Opts.DefaultFPContractMode == LangOptions::FPM_FastHonorPragmas)
    emitArg(Consumer, OPT_ffp_contract, "fast-honor-pragmas");

  if (Opts.getSignReturnAddressScope() ==
      LangOptions::SignReturnAddressScopeKind::All)
    emitArg(Consumer, OPT_msign_return_address_EQ, "all");
  else if (Opts.getSignReturnAddressScope() ==
           LangOptions::SignReturnAddressScopeKind::NonLeaf)
    emitArg(Consumer, OPT_msign_return_address_EQ, "non-leaf");

  if (Opts.getSignReturnAddressKey() ==
      LangOptions::SignReturnAddressKeyKind::BKey)
    emitArg(Consumer, OPT_msign_return_address_key_EQ, "b_key");

  if (Opts.UseTargetPathSeparator)
    emitArg(Consumer, OPT_ffile_reproducible);
  else
    emitArg(Consumer, OPT_fno_file_reproducible);

  for (const auto &MP : Opts.MacroPrefixMap)
    emitArg(Consumer, OPT_fmacro_prefix_map_EQ, MP.first + "=" + MP.second);

  if (!Opts.RandstructSeed.empty())
    emitArg(Consumer, OPT_frandomize_layout_seed_EQ, Opts.RandstructSeed);
}

bool CompilerInvocation::ParseLangArgs(LangOptions &Opts, ArgList &Args,
                                       InputKind IK, const llvm::Triple &T,
                                       std::vector<std::string> &Includes,
                                       DiagnosticsEngine &Diags) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  Opts.PIE = T.isOSLinux();

  if (IK.getLanguage() == Language::LLVM_IR)
    return Diags.getNumErrors() == NumErrorsBefore;

  // Other LangOpts are only initialized when the input is not AST or LLVM IR.

  LangStandard::Kind LangStd = LangStandard::lang_unspecified;
  if (const Arg *A = Args.getLastArg(OPT_std_EQ)) {
    LangStd = LangStandard::getLangKind(A->getValue());
    if (LangStd == LangStandard::lang_unspecified) {
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
      // Report supported standards with short description.
      for (unsigned KindValue = 0; KindValue != LangStandard::lang_unspecified;
           ++KindValue) {
        const LangStandard &Std = LangStandard::getLangStandardForKind(
            static_cast<LangStandard::Kind>(KindValue));
        if (isInputCompatibleWithStandard(IK, Std)) {
          auto Diag = Diags.Report(diag::note_drv_use_standard);
          Diag << Std.getName() << Std.getDescription();
          unsigned NumAliases = 0;
#define LANGSTANDARD(id, name, lang, desc, features)
#define LANGSTANDARD_ALIAS(id, alias)                                          \
  if (KindValue == LangStandard::lang_##id)                                    \
    ++NumAliases;
#define LANGSTANDARD_ALIAS_DEPR(id, alias)
#include "neverc/Foundation/LangOpts/LangStandards.def"
          Diag << NumAliases;
#define LANGSTANDARD(id, name, lang, desc, features)
#define LANGSTANDARD_ALIAS(id, alias)                                          \
  if (KindValue == LangStandard::lang_##id)                                    \
    Diag << alias;
#define LANGSTANDARD_ALIAS_DEPR(id, alias)
#include "neverc/Foundation/LangOpts/LangStandards.def"
        }
      }
    } else {
      // Valid standard, check to make sure language and standard are
      // compatible.
      const LangStandard &Std = LangStandard::getLangStandardForKind(LangStd);
      if (!isInputCompatibleWithStandard(IK, Std)) {
        Diags.Report(diag::err_drv_argument_not_allowed_with)
            << A->getAsString(Args) << getInputKindName(IK);
      }
    }
  }

  LangOptions::setLangDefaults(Opts, IK.getLanguage(), T, Includes, LangStd);

  // The key paths of codegen options defined in Options.td start with
  // "LangOpts->". Let's provide the expected variable name and type.
  LangOptions *LangOpts = &Opts;

#define LANG_OPTION_WITH_MARSHALLING(...)                                      \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef LANG_OPTION_WITH_MARSHALLING

  if (const Arg *A = Args.getLastArg(OPT_fcf_protection_EQ)) {
    llvm::StringRef Name = A->getValue();
    if (Name == "full" || Name == "branch") {
      Opts.CFProtectionBranch = 1;
    }
  }

  if (Arg *A = Args.getLastArg(options::OPT_fgnuc_version_EQ)) {
    llvm::VersionTuple GNUCVer;
    bool Invalid = GNUCVer.tryParse(A->getValue());
    unsigned Major = GNUCVer.getMajor();
    unsigned Minor = GNUCVer.getMinor();
    unsigned Patch = GNUCVer.getSubminor();
    if (Invalid || GNUCVer.getBuild() || Minor >= 100 || Patch >= 100) {
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
    }
    Opts.GNUCVersion = Major * 100 * 100 + Minor * 100 + Patch;
  }

  if (Args.hasArg(OPT_ftrapv)) {
    Opts.setSignedOverflowBehavior(LangOptions::SOB_Trapping);
    Opts.OverflowHandler =
        std::string(Args.getLastArgValue(OPT_ftrapv_handler));
  } else if (Args.hasArg(OPT_fwrapv))
    Opts.setSignedOverflowBehavior(LangOptions::SOB_Defined);

  Opts.MSCompatibilityVersion = 0;
  if (const Arg *A = Args.getLastArg(OPT_fms_compatibility_version)) {
    llvm::VersionTuple VT;
    if (VT.tryParse(A->getValue()))
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
    Opts.MSCompatibilityVersion =
        VT.getMajor() * 10000000 + VT.getMinor() * 100000 + VT.getSubminor();
  }

  Opts.Trigraphs = !Opts.GNUMode && !Opts.MSVCCompat;
  Opts.Trigraphs =
      Args.hasFlag(OPT_ftrigraphs, OPT_fno_trigraphs, Opts.Trigraphs);

  Opts.NoBuiltin = Args.hasArg(OPT_fno_builtin) || Opts.Freestanding;
  if (!Opts.NoBuiltin)
    collectNoBuiltinFuncs(Args, Opts.NoBuiltinFuncs);
  if (Arg *A = Args.getLastArg(options::OPT_LongDouble_Group)) {
    if (A->getOption().matches(options::OPT_mlong_double_64))
      Opts.LongDoubleSize = 64;
    else if (A->getOption().matches(options::OPT_mlong_double_80))
      Opts.LongDoubleSize = 80;
    else if (A->getOption().matches(options::OPT_mlong_double_128))
      Opts.LongDoubleSize = 128;
    else
      Opts.LongDoubleSize = 0;
  }
  // -mrtd option
  if (Arg *A = Args.getLastArg(OPT_mrtd)) {
    if (Opts.getDefaultCallingConv() != LangOptions::DCC_None)
      Diags.Report(diag::err_drv_argument_not_allowed_with)
          << A->getSpelling() << "-fdefault-calling-conv";
    else {
      Diags.Report(diag::err_drv_argument_not_allowed_with)
          << A->getSpelling() << T.getTriple();
    }
  }

  unsigned Opt = getOptimizationLevel(Args, IK, Diags),
           OptSize = getOptimizationLevelSize(Args);
  Opts.Optimize = Opt != 0;
  Opts.OptimizeSize = OptSize != 0;

  // This is the __NO_INLINE__ define, which just depends on things like the
  // optimization level and -fno-inline, not actually whether the backend has
  // inlining enabled.
  Opts.NoInlineDefine = !Opts.Optimize;
  if (Arg *InlineArg = Args.getLastArg(
          options::OPT_finline_functions, options::OPT_finline_hint_functions,
          options::OPT_fno_inline_functions, options::OPT_fno_inline))
    if (InlineArg->getOption().matches(options::OPT_fno_inline))
      Opts.NoInlineDefine = true;

  if (Arg *A = Args.getLastArg(OPT_ffp_contract)) {
    llvm::StringRef Val = A->getValue();
    if (Val == "fast")
      Opts.setDefaultFPContractMode(LangOptions::FPM_Fast);
    else if (Val == "on")
      Opts.setDefaultFPContractMode(LangOptions::FPM_On);
    else if (Val == "off")
      Opts.setDefaultFPContractMode(LangOptions::FPM_Off);
    else if (Val == "fast-honor-pragmas")
      Opts.setDefaultFPContractMode(LangOptions::FPM_FastHonorPragmas);
    else
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Val;
  }

  // Reject any -fsanitize-* arguments at the frontend level (driver already
  // rejects them, but a direct frontend invocation could still bypass that).
  if (Args.hasArg(OPT_fsanitize_EQ) || Args.hasArg(OPT_fsanitize_recover_EQ) ||
      Args.hasArg(OPT_fsanitize_trap_EQ) ||
      Args.hasArg(OPT_fsanitize_ignorelist_EQ) ||
      Args.hasArg(OPT_fsanitize_system_ignorelist_EQ)) {
    Diags.Report(diag::err_drv_unsupported) << "sanitizers (-fsanitize=...)";
  }

  if (Arg *A = Args.getLastArg(OPT_msign_return_address_EQ)) {
    llvm::StringRef SignScope = A->getValue();

    if (SignScope.equals_insensitive("none"))
      Opts.setSignReturnAddressScope(
          LangOptions::SignReturnAddressScopeKind::None);
    else if (SignScope.equals_insensitive("all"))
      Opts.setSignReturnAddressScope(
          LangOptions::SignReturnAddressScopeKind::All);
    else if (SignScope.equals_insensitive("non-leaf"))
      Opts.setSignReturnAddressScope(
          LangOptions::SignReturnAddressScopeKind::NonLeaf);
    else
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << SignScope;

    if (Arg *A = Args.getLastArg(OPT_msign_return_address_key_EQ)) {
      llvm::StringRef SignKey = A->getValue();
      if (!SignScope.empty() && !SignKey.empty()) {
        if (SignKey == "a_key")
          Opts.setSignReturnAddressKey(
              LangOptions::SignReturnAddressKeyKind::AKey);
        else if (SignKey == "b_key")
          Opts.setSignReturnAddressKey(
              LangOptions::SignReturnAddressKeyKind::BKey);
        else
          Diags.Report(diag::err_drv_invalid_value)
              << A->getAsString(Args) << SignKey;
      }
    }
  }

  for (const auto &A : Args.getAllArgValues(OPT_fmacro_prefix_map_EQ)) {
    auto Split = llvm::StringRef(A).split('=');
    Opts.MacroPrefixMap.insert(
        {std::string(Split.first), std::string(Split.second)});
  }

  Opts.UseTargetPathSeparator =
      !Args.getLastArg(OPT_fno_file_reproducible) &&
      (Args.getLastArg(OPT_ffile_compilation_dir_EQ) ||
       Args.getLastArg(OPT_fmacro_prefix_map_EQ) ||
       Args.getLastArg(OPT_ffile_reproducible));

  // Error if -mvscale-min is unbounded.
  if (Arg *A = Args.getLastArg(options::OPT_mvscale_min_EQ)) {
    unsigned VScaleMin;
    if (llvm::StringRef(A->getValue()).getAsInteger(10, VScaleMin) ||
        VScaleMin == 0)
      Diags.Report(diag::err_frontend_unbounded_vscale_min);
  }

  if (const Arg *A = Args.getLastArg(OPT_frandomize_layout_seed_file_EQ)) {
    std::ifstream SeedFile(A->getValue(0));

    if (!SeedFile.is_open())
      Diags.Report(diag::err_drv_cannot_open_randomize_layout_seed_file)
          << A->getValue(0);

    std::getline(SeedFile, Opts.RandstructSeed);
  }

  if (const Arg *A = Args.getLastArg(OPT_frandomize_layout_seed_EQ))
    Opts.RandstructSeed = A->getValue(0);

  return Diags.getNumErrors() == NumErrorsBefore;
}

// ===----------------------------------------------------------------------===
// Preprocessor & target options
// ===----------------------------------------------------------------------===

namespace {
bool isStrictlyPreprocessorAction(frontend::ActionKind Action) {
  switch (Action) {
  case frontend::GenAssembly:
  case frontend::GenBC:
  case frontend::GenLLVM:
  case frontend::GenObj:
  case frontend::ParseSyntaxOnly:
    return false;

  case frontend::PrintPreprocessedInput:
  case frontend::RunPreprocessorOnly:
    return true;
  }
  llvm_unreachable("invalid frontend action");
}

void generatePreprocessorArgs(const PrepOptions &Opts,
                              ArgumentConsumer Consumer,
                              const LangOptions &LangOpts,
                              const FrontendOptions &FrontendOpts,
                              const CodeGenOptions &CodeGenOpts) {
  const PrepOptions *PPO = &Opts;

#define PREPROCESSOR_OPTION_WITH_MARSHALLING(...)                              \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef PREPROCESSOR_OPTION_WITH_MARSHALLING

  for (const auto &M : Opts.Macros) {
    // Don't generate __CET__ macro definitions. They are implied by the
    // -fcf-protection option that is generated elsewhere.
    if (M.first == "__CET__=1" && !M.second &&
        !CodeGenOpts.CFProtectionReturn && CodeGenOpts.CFProtectionBranch)
      continue;
    if (M.first == "__CET__=2" && !M.second && CodeGenOpts.CFProtectionReturn &&
        !CodeGenOpts.CFProtectionBranch)
      continue;
    if (M.first == "__CET__=3" && !M.second && CodeGenOpts.CFProtectionReturn &&
        CodeGenOpts.CFProtectionBranch)
      continue;

    emitArg(Consumer, M.second ? OPT_U : OPT_D, M.first);
  }

  for (const auto &I : Opts.Includes)
    emitArg(Consumer, OPT_include, I);

  if (Opts.SourceDateEpoch)
    emitArg(Consumer, OPT_source_date_epoch,
            llvm::Twine(*Opts.SourceDateEpoch));

  if (Opts.DefineTargetOSMacros)
    emitArg(Consumer, OPT_fdefine_target_os_macros);
}

bool parsePreprocessorArgs(PrepOptions &Opts, ArgList &Args,
                           DiagnosticsEngine &Diags,
                           frontend::ActionKind Action,
                           const FrontendOptions &FrontendOpts) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  PrepOptions *PPO = &Opts;

#define PREPROCESSOR_OPTION_WITH_MARSHALLING(...)                              \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef PREPROCESSOR_OPTION_WITH_MARSHALLING

  // Add the __CET__ macro if a CFProtection option is set.
  if (const Arg *A = Args.getLastArg(OPT_fcf_protection_EQ)) {
    llvm::StringRef Name = A->getValue();
    if (Name == "branch")
      Opts.addMacroDef("__CET__=1");
    else if (Name == "return")
      Opts.addMacroDef("__CET__=2");
    else if (Name == "full")
      Opts.addMacroDef("__CET__=3");
  }

  for (const auto *A : Args.filtered(OPT_D, OPT_U)) {
    if (A->getOption().matches(OPT_D))
      Opts.addMacroDef(A->getValue());
    else
      Opts.addMacroUndef(A->getValue());
  }

  for (const auto *A : Args.filtered(OPT_include))
    Opts.Includes.emplace_back(A->getValue());

  if (const Arg *A = Args.getLastArg(OPT_source_date_epoch)) {
    llvm::StringRef Epoch = A->getValue();
    // SOURCE_DATE_EPOCH, if specified, must be a non-negative decimal integer.
    // On time64 systems, pick 253402300799 (the UNIX timestamp of
    // 9999-12-31T23:59:59Z) as the upper bound.
    const uint64_t MaxTimestamp =
        std::min<uint64_t>(std::numeric_limits<time_t>::max(), 253402300799);
    uint64_t V;
    if (Epoch.getAsInteger(10, V) || V > MaxTimestamp) {
      Diags.Report(diag::err_fe_invalid_source_date_epoch)
          << Epoch << MaxTimestamp;
    } else {
      Opts.SourceDateEpoch = V;
    }
  }

  Opts.DefineTargetOSMacros =
      Args.hasFlag(OPT_fdefine_target_os_macros,
                   OPT_fno_define_target_os_macros, Opts.DefineTargetOSMacros);

  return Diags.getNumErrors() == NumErrorsBefore;
}

void generatePrepOutputArgs(const PrepOutputOptions &Opts,
                            ArgumentConsumer Consumer,
                            frontend::ActionKind Action) {
  const PrepOutputOptions &PrepOutputOpts = Opts;

#define PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING(...)                       \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING

  bool Generate_dM = isStrictlyPreprocessorAction(Action) && !Opts.ShowCPP;
  if (Generate_dM)
    emitArg(Consumer, OPT_dM);
  if (!Generate_dM && Opts.ShowMacros)
    emitArg(Consumer, OPT_dD);
  if (Opts.DirectivesOnly)
    emitArg(Consumer, OPT_fdirectives_only);
}

bool parsePrepOutputArgs(PrepOutputOptions &Opts, ArgList &Args,
                         DiagnosticsEngine &Diags,
                         frontend::ActionKind Action) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  PrepOutputOptions &PrepOutputOpts = Opts;

#define PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING(...)                       \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING

  Opts.ShowCPP = isStrictlyPreprocessorAction(Action) && !Args.hasArg(OPT_dM);
  Opts.ShowMacros = Args.hasArg(OPT_dM) || Args.hasArg(OPT_dD);
  Opts.DirectivesOnly = Args.hasArg(OPT_fdirectives_only);

  return Diags.getNumErrors() == NumErrorsBefore;
}

void generateTargetArgs(const TargetOptions &Opts, ArgumentConsumer Consumer) {
  const TargetOptions *TargetOpts = &Opts;
#define TARGET_OPTION_WITH_MARSHALLING(...)                                    \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef TARGET_OPTION_WITH_MARSHALLING

  if (!Opts.SDKVersion.empty())
    emitArg(Consumer, OPT_target_sdk_version_EQ, Opts.SDKVersion.getAsString());
}

bool parseTargetArgs(TargetOptions &Opts, ArgList &Args,
                     DiagnosticsEngine &Diags) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  TargetOptions *TargetOpts = &Opts;

#define TARGET_OPTION_WITH_MARSHALLING(...)                                    \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "neverc/Invoke/Options.td.h"
#undef TARGET_OPTION_WITH_MARSHALLING

  if (Arg *A = Args.getLastArg(options::OPT_target_sdk_version_EQ)) {
    llvm::VersionTuple Version;
    if (Version.tryParse(A->getValue()))
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
    else
      Opts.SDKVersion = Version;
  }

  return Diags.getNumErrors() == NumErrorsBefore;
}
} // namespace

bool CompilerInvocation::CreateFromArgsImpl(
    CompilerInvocation &Res, llvm::ArrayRef<const char *> CommandLineArgs,
    DiagnosticsEngine &Diags, const char *Argv0) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  const OptTable &Opts = getDriverOptTable();
  llvm::opt::Visibility VisibilityMask(options::NeverCOption);
  unsigned MissingArgIndex, MissingArgCount;
  InputArgList Args = Opts.ParseArgs(CommandLineArgs, MissingArgIndex,
                                     MissingArgCount, VisibilityMask);
  LangOptions &LangOpts = Res.getLangOpts();

  if (MissingArgCount)
    Diags.Report(diag::err_drv_missing_argument)
        << Args.getArgString(MissingArgIndex) << MissingArgCount;

  // Issue errors on unknown arguments.
  for (const auto *A : Args.filtered(OPT_UNKNOWN)) {
    auto ArgString = A->getAsString(Args);
    std::string Nearest;
    if (Opts.findNearest(ArgString, Nearest, VisibilityMask) > 1)
      Diags.Report(diag::err_drv_unknown_argument) << ArgString;
    else
      Diags.Report(diag::err_drv_unknown_argument_with_suggestion)
          << ArgString << Nearest;
  }

  parseFileSystemArgs(Res.getFileSystemOpts(), Args, Diags);
  ParseDiagnosticArgs(Res.getDiagnosticOpts(), Args, &Diags,
                      /*DefaultDiagColor=*/false);
  parseFrontendArgs(Res.getFrontendOpts(), Args, Diags, LangOpts.IsHeaderFile);
  InputKind DashX = Res.getFrontendOpts().DashX;
  parseTargetArgs(Res.getTargetOpts(), Args, Diags);
  llvm::Triple T(Res.getTargetOpts().Triple);
  parseHeaderSearchArgs(Res.getHeaderIdxOpts(), Args, Diags,
                        Res.getFileSystemOpts().WorkingDir);

  ParseLangArgs(LangOpts, Args, DashX, T, Res.getPrepOpts().Includes, Diags);
  ParseCodeGenArgs(Res.getCodeGenOpts(), Args, DashX, Diags, T,
                   Res.getFrontendOpts().OutputFile, LangOpts);

  parsePreprocessorArgs(Res.getPrepOpts(), Args, Diags,
                        Res.getFrontendOpts().ProgramAction,
                        Res.getFrontendOpts());
  parsePrepOutputArgs(Res.getPrepOutputOpts(), Args, Diags,
                      Res.getFrontendOpts().ProgramAction);

  parseDependencyOutputArgs(Res.getDependencyOutputOpts(), Args, Diags,
                            Res.getFrontendOpts().ProgramAction,
                            Res.getPrepOutputOpts().ShowLineMarkers);
  if (!Res.getDependencyOutputOpts().OutputFile.empty() &&
      Res.getDependencyOutputOpts().Targets.empty())
    Diags.Report(diag::err_fe_dependency_file_requires_MT);

  validateInvocation(Res, Diags, Args, DashX);

  return Diags.getNumErrors() == NumErrorsBefore;
}

bool CompilerInvocation::CreateFromArgs(
    CompilerInvocation &Invocation,
    llvm::ArrayRef<const char *> CommandLineArgs, DiagnosticsEngine &Diags,
    const char *Argv0) {
  return CreateFromArgsImpl(Invocation, CommandLineArgs, Diags, Argv0);
}

void CompilerInvocationBase::generateFrontendCommandLine(
    ArgumentConsumer Consumer) const {
  llvm::Triple T(getTargetOpts().Triple);

  generateFileSystemArgs(getFileSystemOpts(), Consumer);
  GenerateDiagnosticArgs(getDiagnosticOpts(), Consumer,
                         /*DefaultDiagColor=*/false);
  generateFrontendArgs(getFrontendOpts(), Consumer, getLangOpts().IsHeaderFile);
  generateTargetArgs(getTargetOpts(), Consumer);
  generateHeaderSearchArgs(getHeaderIdxOpts(), Consumer);
  GenerateLangArgs(getLangOpts(), Consumer, T, getFrontendOpts().DashX);
  GenerateCodeGenArgs(getCodeGenOpts(), Consumer, T,
                      getFrontendOpts().OutputFile, &getLangOpts());
  generatePreprocessorArgs(getPrepOpts(), Consumer, getLangOpts(),
                           getFrontendOpts(), getCodeGenOpts());
  generatePrepOutputArgs(getPrepOutputOpts(), Consumer,
                         getFrontendOpts().ProgramAction);
  generateDependencyOutputArgs(getDependencyOutputOpts(), Consumer);
}

std::vector<std::string>
CompilerInvocationBase::getFrontendCommandLine() const {
  std::vector<std::string> Args;
  generateFrontendCommandLine(
      [&Args](const llvm::Twine &Arg) { Args.push_back(Arg.str()); });
  return Args;
}

llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem>
neverc::createVFSFromCompilerInvocation(const CompilerInvocation &CI,
                                        DiagnosticsEngine &Diags) {
  return createVFSFromCompilerInvocation(CI, Diags,
                                         llvm::vfs::getRealFileSystem());
}

llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem>
neverc::createVFSFromCompilerInvocation(
    const CompilerInvocation &CI, DiagnosticsEngine &Diags,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFS) {
  return createVFSFromOverlayFiles(CI.getHeaderIdxOpts().VFSOverlayFiles, Diags,
                                   std::move(BaseFS));
}

llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem>
neverc::createVFSFromOverlayFiles(
    llvm::ArrayRef<std::string> VFSOverlayFiles, DiagnosticsEngine &Diags,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFS) {
  if (VFSOverlayFiles.empty())
    return BaseFS;

  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> Result = BaseFS;
  // earlier vfs files are on the bottom
  for (const auto &File : VFSOverlayFiles) {
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Buffer =
        Result->getBufferForFile(File);
    if (!Buffer) {
      Diags.Report(diag::err_missing_vfs_overlay_file) << File;
      continue;
    }

    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS =
        llvm::vfs::getVFSFromYAML(std::move(Buffer.get()),
                                  /*DiagHandler*/ nullptr, File,
                                  /*DiagContext*/ nullptr, Result);
    if (!FS) {
      Diags.Report(diag::err_invalid_vfs_overlay) << File;
      continue;
    }

    Result = FS;
  }
  return Result;
}
