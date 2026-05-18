//===----------------------------------------------------------------------===//
//
//  CodegenFlags — thin adapter over `llvm/CodeGen/CommandFlags`.
//
//  The linker (and its LTO backends) need to consult the same codegen
//  command-line state that `NeverC` / `opt` register globally; this
//  translation unit forwards to LLVM's `codegen::*` accessors so the
//  rest of the tree never has to pull `CommandFlags.h` in.  Keeping the
//  forwarders out of a header also avoids dragging LLVM CodeGen symbols
//  into every include chain that only wants the prototypes.
//
//===----------------------------------------------------------------------===//

#include "Linker/Core/Driver/CodegenFlags.h"

#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/TargetParser/Triple.h"

llvm::TargetOptions linker::codegen::initTargetOptions() {
  return llvm::codegen::InitTargetOptionsFromCodeGenFlags(llvm::Triple());
}

std::optional<llvm::CodeModel::Model> linker::codegen::codeModel() {
  return llvm::codegen::getExplicitCodeModel();
}
