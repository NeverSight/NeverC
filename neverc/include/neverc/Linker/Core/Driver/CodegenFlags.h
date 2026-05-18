//===----------------------------------------------------------------------===//
//
//  CodegenFlags — forwarding layer over `llvm::codegen::*` command-line
//  accessors.  The linker (and its LTO backends) need the same codegen
//  flags state that NeverC / opt register globally; routing through this
//  translation unit keeps `llvm/CodeGen/CommandFlags.h` out of every
//  include chain that only wants the prototypes.
//
//  Every forwarder lives in the `linker::codegen::` sub-namespace for
//  symmetry with `linker::args::` and friends.
//
//===----------------------------------------------------------------------===//

#ifndef LINKER_CORE_DRIVER_CODEGENFLAGS_H
#define LINKER_CORE_DRIVER_CODEGENFLAGS_H

#include "llvm/Support/CodeGen.h"
#include "llvm/Target/TargetOptions.h"

#include <optional>

namespace linker {
namespace codegen {

/// Snapshot the `llvm::codegen::*` command-line state into a fresh
/// `TargetOptions`.  Equivalent to
/// `llvm::codegen::InitTargetOptionsFromCodeGenFlags(llvm::Triple())`.
llvm::TargetOptions initTargetOptions();

/// Explicit `--code-model=` value if the user supplied one; otherwise
/// `std::nullopt`.
std::optional<llvm::CodeModel::Model> codeModel();

} // namespace codegen
} // namespace linker

#endif // LINKER_CORE_DRIVER_CODEGENFLAGS_H
