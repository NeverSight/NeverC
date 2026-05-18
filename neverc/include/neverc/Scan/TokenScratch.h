#ifndef NEVERC_LEX_TOKENSCRATCH_H
#define NEVERC_LEX_TOKENSCRATCH_H

#include "neverc/Foundation/Core/SourceLocation.h"

namespace neverc {
class SourceManager;

class TokenScratch {
  SourceManager &SrcMgr;
  char *CurBuffer;
  SourceLocation BufferStartLoc;
  unsigned BytesUsed;
  unsigned CurrentChunkCapacity;

public:
  TokenScratch(SourceManager &SM);

  SourceLocation getToken(const char *Buf, unsigned Len, const char *&DestPtr);

private:
  void allocateChunk(unsigned RequestLen);
};

} // end namespace neverc

#endif
