#ifndef NEVERC_LIB_CODEGEN_DECL_PATTERNINIT_H
#define NEVERC_LIB_CODEGEN_DECL_PATTERNINIT_H

namespace llvm {
class Constant;
class Type;
} // namespace llvm

namespace neverc {
namespace Emit {

class ModuleEmitter;

llvm::Constant *initializationPatternFor(ModuleEmitter &, llvm::Type *);

} // end namespace Emit
} // end namespace neverc

#endif
