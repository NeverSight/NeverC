#ifndef NEVERC_SHELLCODE_EXTERNREWRITER_H
#define NEVERC_SHELLCODE_EXTERNREWRITER_H

namespace llvm {
class Function;
class FunctionType;
class Module;
class StringRef;
}

namespace neverc {
namespace shellcode {

bool rewriteExternCalls(llvm::Module &M, llvm::Function &Decl,
                        llvm::Function &Helper);

llvm::Function *getOrCreateScHelper(llvm::Module &M, llvm::StringRef Name,
                                    llvm::FunctionType *FTy);

}
}

#endif
