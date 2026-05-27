#ifndef NEVERC_LIB_COMPILER_COMPILERINVOCATIONARG_H
#define NEVERC_LIB_COMPILER_COMPILERINVOCATIONARG_H

// Internal header shared by CompilerInvocation*.cpp split files.
// NOT part of the public API.  Only include from
// neverc/lib/Compiler/Compiler/CompilerInvocation*.cpp files.
//
// Intentionally uses 'using namespace' directives for readability—
// safe because all includers are private implementation files.

#include "neverc/Compiler/CompilerInvocation.h"
#include "neverc/Compiler/FrontendDiag.h"
#include "neverc/Compiler/FrontendOptions.h"
#include "neverc/Compiler/Utils.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/Diagnostic/DiagnosticDriver.h"
#include "neverc/Foundation/LangOpts/CodeGenOptions.h"
#include "neverc/Foundation/Target/TargetOptions.h"
#include "neverc/Invoke/DriverDiagnostic.h"
#include "neverc/Invoke/Options.h"
#include "neverc/Scan/HeaderIndexOptions.h"
#include "neverc/Scan/PrepOptions.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptSpecifier.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

using namespace neverc;
using namespace driver;
using namespace options;
using namespace llvm::opt;

using ArgumentConsumer = CompilerInvocation::ArgumentConsumer;

#define SIMPLE_ENUM_VALUE_TABLE
#include "neverc/Invoke/Options.td.h"
#undef SIMPLE_ENUM_VALUE_TABLE

// ===----------------------------------------------------------------------===
// Argument normalization helpers
// ===----------------------------------------------------------------------===

static inline std::optional<bool>
normalizeSimpleFlag(OptSpecifier Opt, unsigned, const ArgList &Args,
                    DiagnosticsEngine &) {
  if (Args.hasArg(Opt))
    return true;
  return std::nullopt;
}

static inline std::optional<bool>
normalizeSimpleNegativeFlag(OptSpecifier Opt, unsigned, const ArgList &Args,
                            DiagnosticsEngine &) {
  if (Args.hasArg(Opt))
    return false;
  return std::nullopt;
}

static inline void denormalizeSimpleFlag(ArgumentConsumer Consumer,
                                         const llvm::Twine &Spelling,
                                         Option::OptionClass, unsigned,
                                         /*T*/...) {
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

static inline auto makeBooleanOptionNormalizer(bool Value, bool OtherValue,
                                               OptSpecifier OtherOpt) {
  return [Value, OtherValue,
          OtherOpt](OptSpecifier Opt, unsigned, const ArgList &Args,
                    DiagnosticsEngine &) -> std::optional<bool> {
    if (const Arg *A = Args.getLastArg(Opt, OtherOpt))
      return A->getOption().matches(Opt) ? Value : OtherValue;
    return std::nullopt;
  };
}

static inline auto makeBooleanOptionDenormalizer(bool Value) {
  return [Value](ArgumentConsumer Consumer, const llvm::Twine &Spelling,
                 Option::OptionClass, unsigned, bool KeyPath) {
    if (KeyPath == Value)
      Consumer(Spelling);
  };
}

static inline void denormalizeString(ArgumentConsumer Consumer,
                                     const llvm::Twine &Spelling,
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

static inline std::optional<SimpleEnumValue>
findValueTableByName(const SimpleEnumValueTable &Table, llvm::StringRef Name) {
  for (int I = 0, E = Table.Size; I != E; ++I)
    if (Name == Table.Table[I].Name)
      return Table.Table[I];
  return std::nullopt;
}

static inline std::optional<SimpleEnumValue>
findValueTableByValue(const SimpleEnumValueTable &Table, unsigned Value) {
  for (int I = 0, E = Table.Size; I != E; ++I)
    if (Value == Table.Table[I].Value)
      return Table.Table[I];
  return std::nullopt;
}

static inline std::optional<unsigned>
normalizeSimpleEnum(OptSpecifier Opt, unsigned TableIndex, const ArgList &Args,
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

static inline void denormalizeSimpleEnumImpl(ArgumentConsumer Consumer,
                                             const llvm::Twine &Spelling,
                                             Option::OptionClass OptClass,
                                             unsigned TableIndex,
                                             unsigned Value) {
  assert(TableIndex < SimpleEnumValueTablesSize);
  const SimpleEnumValueTable &Table = SimpleEnumValueTables[TableIndex];
  if (auto MaybeEnumVal = findValueTableByValue(Table, Value))
    denormalizeString(Consumer, Spelling, OptClass, TableIndex,
                      MaybeEnumVal->Name);
  else
    llvm_unreachable("The simple enum value was not correctly defined in "
                     "the tablegen option description");
}

template <typename T>
void denormalizeSimpleEnum(ArgumentConsumer Consumer,
                           const llvm::Twine &Spelling,
                           Option::OptionClass OptClass, unsigned TableIndex,
                           T Value) {
  denormalizeSimpleEnumImpl(Consumer, Spelling, OptClass, TableIndex,
                            static_cast<unsigned>(Value));
}

static inline std::optional<std::string>
normalizeString(OptSpecifier Opt, int, const ArgList &Args,
                DiagnosticsEngine &) {
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

static inline std::optional<std::vector<std::string>>
normalizeStringVector(OptSpecifier Opt, int, const ArgList &Args,
                      DiagnosticsEngine &) {
  return Args.getAllArgValues(Opt);
}

static inline void
denormalizeStringVector(ArgumentConsumer Consumer, const llvm::Twine &Spelling,
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

static inline std::optional<std::string>
normalizeTriple(OptSpecifier Opt, int, const ArgList &Args,
                DiagnosticsEngine &) {
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

// ===----------------------------------------------------------------------===
// Marshalling macros
// ===----------------------------------------------------------------------===

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
// Utility helpers
// ===----------------------------------------------------------------------===

static inline void emitArg(ArgumentConsumer Consumer,
                            llvm::opt::OptSpecifier OptSpecifier) {
  Option Opt = getDriverOptTable().getOption(OptSpecifier);
  denormalizeSimpleFlag(Consumer, Opt.getPrefixedName(),
                        Option::OptionClass::FlagClass, 0);
}

static inline void emitArg(ArgumentConsumer Consumer,
                            llvm::opt::OptSpecifier OptSpecifier,
                            const llvm::Twine &Value) {
  Option Opt = getDriverOptTable().getOption(OptSpecifier);
  denormalizeString(Consumer, Opt.getPrefixedName(), Opt.getKind(), 0, Value);
}

static inline void addDiagnosticArgs(ArgList &Args, OptSpecifier Group,
                                      OptSpecifier GroupWithValue,
                                      std::vector<std::string> &Diagnostics) {
  for (auto *A : Args.filtered(Group)) {
    if (A->getOption().getKind() == Option::FlagClass) {
      Diagnostics.push_back(
          std::string(A->getOption().getName().drop_front(1)));
    } else if (A->getOption().matches(GroupWithValue)) {
      Diagnostics.push_back(
          std::string(A->getOption().getName().drop_front(1).rtrim("=-")));
    } else {
      Diagnostics.push_back(A->getValue());
    }
  }
}

static inline unsigned getOptimizationLevel(ArgList &Args, InputKind IK,
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

static inline unsigned getOptimizationLevelSize(ArgList &Args) {
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

static inline void collectNoBuiltinFuncs(ArgList &Args,
                                          std::vector<std::string> &Funcs) {
  std::vector<std::string> Values = Args.getAllArgValues(OPT_fno_builtin_);
  auto BuiltinEnd = llvm::partition(Values, Builtin::Context::isBuiltinFunc);
  Funcs.insert(Funcs.end(), Values.begin(), BuiltinEnd);
}

static inline void
generateOptimizationRemark(ArgumentConsumer Consumer, OptSpecifier OptEQ,
                           llvm::StringRef Name,
                           const CodeGenOptions::OptRemark &Remark) {
  if (Remark.hasValidPattern())
    emitArg(Consumer, OptEQ, Remark.Pattern);
  else if (Remark.Kind == CodeGenOptions::RK_Enabled)
    emitArg(Consumer, OPT_R_Joined, Name);
  else if (Remark.Kind == CodeGenOptions::RK_Disabled)
    emitArg(Consumer, OPT_R_Joined, llvm::StringRef("no-") + Name);
}

// ===----------------------------------------------------------------------===
// Cross-file function declarations
// ===----------------------------------------------------------------------===

void generateHeaderSearchArgs(const HeaderIndexOptions &Opts,
                              ArgumentConsumer Consumer);

bool parseHeaderSearchArgs(HeaderIndexOptions &Opts, ArgList &Args,
                           DiagnosticsEngine &Diags,
                           const std::string &WorkingDir);

#endif // NEVERC_LIB_COMPILER_COMPILERINVOCATIONARG_H
