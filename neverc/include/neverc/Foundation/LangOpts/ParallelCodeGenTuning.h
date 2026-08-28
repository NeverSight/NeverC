#ifndef NEVERC_FOUNDATION_LANGOPTS_PARALLELCODEGENTUNING_H
#define NEVERC_FOUNDATION_LANGOPTS_PARALLELCODEGENTUNING_H

#include <cstdint>

namespace neverc {

namespace ParallelCodeGenTuningDefaults {
#define NEVERC_PARALLEL_CODEGEN_TUNING_OPTION(Field, Variable, Spelling,       \
                                              Default)                         \
  inline constexpr std::uint32_t Field = Default;
#include "neverc/Foundation/LangOpts/ParallelCodeGenTuning.def"
#undef NEVERC_PARALLEL_CODEGEN_TUNING_OPTION
} // namespace ParallelCodeGenTuningDefaults

namespace ParallelCodeGenTuningOptionSpelling {
#define NEVERC_PARALLEL_CODEGEN_TUNING_OPTION(Field, Variable, Spelling,       \
                                              Default)                         \
  inline constexpr char Field[] = Spelling;
#include "neverc/Foundation/LangOpts/ParallelCodeGenTuning.def"
#undef NEVERC_PARALLEL_CODEGEN_TUNING_OPTION
} // namespace ParallelCodeGenTuningOptionSpelling

/// Immutable-at-use policy snapshot for one parallel-codegen request.
///
/// The type intentionally contains data only so frontend code, linker
/// configuration, cache-key construction, and Emit can share the exact same
/// low-level representation without depending on LLVM pass or linker types.
struct ParallelCodeGenTuning {
#define NEVERC_PARALLEL_CODEGEN_TUNING_OPTION(Field, Variable, Spelling,       \
                                              Default)                         \
  std::uint32_t Field = ParallelCodeGenTuningDefaults::Field;
#include "neverc/Foundation/LangOpts/ParallelCodeGenTuning.def"
#undef NEVERC_PARALLEL_CODEGEN_TUNING_OPTION
};

} // namespace neverc

#endif // NEVERC_FOUNDATION_LANGOPTS_PARALLELCODEGENTUNING_H
