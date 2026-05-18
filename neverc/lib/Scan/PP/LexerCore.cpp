#include "neverc/Scan/LexerCore.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Scan/LexDiag.h"
#include "neverc/Scan/PrepEngine.h"
#include <cassert>

using namespace neverc;

void LexerCore::anchor() {}

LexerCore::LexerCore(PrepEngine *pp, FileID fid) : PP(pp), FID(fid) {
  if (LLVM_LIKELY(pp != nullptr))
    InitialNumSLocEntries = pp->getSourceManager().local_sloc_entry_size();
}

void LexerCore::LexIncludePath(Token &FilenameTok) {
  assert(ParsingFilename == false && "reentered LexIncludePath");
  ParsingFilename = true;

  if (LLVM_UNLIKELY(LexingRawMode))
    IndirectLex(FilenameTok);
  else
    PP->Lex(FilenameTok);

  ParsingFilename = false;
}

OptionalFileEntryRef LexerCore::getFileEntry() const {
  return PP->getSourceManager().getFileEntryRefForID(getFileID());
}
