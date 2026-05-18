#include "neverc/Scan/PrepObserver.h"
#include "llvm/Support/Compiler.h"

using namespace neverc;

PrepObserver::~PrepObserver() = default;

void PrepObserver::HasInclude(SourceLocation Loc, llvm::StringRef FileName,
                              bool IsAngled, OptionalFileEntryRef File,
                              SrcMgr::CharacteristicKind FileType) {}

ChainedPrepObserver::~ChainedPrepObserver() = default;

void ChainedPrepObserver::HasInclude(SourceLocation Loc,
                                     llvm::StringRef FileName, bool IsAngled,
                                     OptionalFileEntryRef File,
                                     SrcMgr::CharacteristicKind FileType) {
  First->HasInclude(Loc, FileName, IsAngled, File, FileType);
  if (LLVM_LIKELY(Second != nullptr))
    Second->HasInclude(Loc, FileName, IsAngled, File, FileType);
}
