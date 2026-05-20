#ifndef NEVERC_SCAN_LEXERCORE_H
#define NEVERC_SCAN_LEXERCORE_H

#include "neverc/Foundation/Core/FileEntry.h"
#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Scan/IncludeGuardOpt.h"
#include "neverc/Scan/Token.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include <cassert>

namespace neverc {

class FileEntry;
class PrepEngine;

class LexerCore {
  virtual void anchor();

protected:
  friend class PrepEngine;

  PrepEngine *PP = nullptr;
  const FileID FID;
  unsigned InitialNumSLocEntries = 0;

  bool ParsingDirective = false;
  bool ParsingFilename = false;

  bool LexingRawMode = false;

  IncludeGuardTracker MIOpt;
  llvm::SmallVector<PPConditionalInfo, 4> ConditionalStack;

  LexerCore() : FID() {}
  LexerCore(PrepEngine *pp, FileID fid);
  virtual ~LexerCore() = default;

  virtual void IndirectLex(Token &Result) = 0;
  virtual SourceLocation getSourceLocation() = 0;

  void pushConditionalLevel(SourceLocation DirectiveStart, bool WasSkipping,
                            bool FoundNonSkip, bool FoundElse) {
    PPConditionalInfo CI;
    CI.IfLoc = DirectiveStart;
    CI.WasSkipping = WasSkipping;
    CI.FoundNonSkip = FoundNonSkip;
    CI.FoundElse = FoundElse;
    ConditionalStack.push_back(CI);
  }
  void pushConditionalLevel(const PPConditionalInfo &CI) {
    ConditionalStack.push_back(CI);
  }

  bool popConditionalLevel(PPConditionalInfo &CI) {
    if (ConditionalStack.empty())
      return true;
    CI = ConditionalStack.pop_back_val();
    return false;
  }

  PPConditionalInfo &peekConditionalLevel() {
    assert(!ConditionalStack.empty() && "No conditionals active!");
    return ConditionalStack.back();
  }

  unsigned getConditionalStackDepth() const { return ConditionalStack.size(); }

public:
  LexerCore(const LexerCore &) = delete;
  LexerCore &operator=(const LexerCore &) = delete;

  void LexIncludePath(Token &FilenameTok);

  void setParsingDirective(bool f) { ParsingDirective = f; }

  bool isLexingRawMode() const { return LexingRawMode; }
  PrepEngine *getPP() const { return PP; }

  FileID getFileID() const {
    assert(PP && "LexerCore::getFileID() requires attached PrepEngine");
    return FID;
  }

  unsigned getInitialNumSLocEntries() const { return InitialNumSLocEntries; }
  OptionalFileEntryRef getFileEntry() const;

  using conditional_iterator =
      llvm::SmallVectorImpl<PPConditionalInfo>::const_iterator;

  conditional_iterator conditional_begin() const {
    return ConditionalStack.begin();
  }

  conditional_iterator conditional_end() const {
    return ConditionalStack.end();
  }

  void setConditionalLevels(llvm::ArrayRef<PPConditionalInfo> CL) {
    ConditionalStack.clear();
    ConditionalStack.append(CL.begin(), CL.end());
  }
};

} // namespace neverc

#endif // NEVERC_SCAN_LEXERCORE_H
