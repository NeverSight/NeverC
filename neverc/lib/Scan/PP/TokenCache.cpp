#include "neverc/Scan/PrepEngine.h"
#include "llvm/Support/Compiler.h"
using namespace neverc;

void PrepEngine::SaveLexState() {
  assert(LexLevel == 0 && "cannot use lookahead while lexing");
  BacktrackPositions.push_back(CachedLexPos);
  EnableCaching();
}

void PrepEngine::DropSavedState() {
  assert(!BacktrackPositions.empty() && "SaveLexState was not called!");
  BacktrackPositions.pop_back();
}

void PrepEngine::RestoreLexState() {
  assert(!BacktrackPositions.empty() && "SaveLexState was not called!");
  CachedLexPos = BacktrackPositions.back();
  BacktrackPositions.pop_back();
  refreshDispatch();
}

__attribute__((hot)) void PrepEngine::FetchCachedToken(Token &Result) {
  if (LLVM_UNLIKELY(!IsCaching()))
    return;

  assert(LexLevel == 1 &&
         "should not use token caching within the preprocessor");

  const size_t Pos = CachedLexPos;
  const size_t Sz = CachedTokens.size();
  if (LLVM_LIKELY(Pos < Sz)) {
    if (LLVM_LIKELY(Pos + 1 < Sz))
      __builtin_prefetch(&CachedTokens[Pos + 1], 0, 3);
    Result = CachedTokens[Pos];
    CachedLexPos = Pos + 1;
    Result.setFlag(Token::IsReinjected);
    return;
  }

  DisableCaching();
  Lex(Result);

  if (LLVM_UNLIKELY(isBacktrackEnabled())) {
    EnableCachingFast();
    CachedTokens.push_back(Result);
    ++CachedLexPos;
    return;
  }

  if (Pos < CachedTokens.size()) {
    EnableCachingFast();
  } else {
    CachedTokens.clear();
    CachedLexPos = 0;
  }
}

void PrepEngine::EnableCaching() {
  assert(LexLevel == 0 &&
         "entered caching lex mode while lexing something else");

  if (IsCaching()) {
    assert(CurLexerCallback == DispatchCache && "Unexpected lexer kind");
    return;
  }

  EnableCachingFast();
}

void PrepEngine::EnableCachingFast() {
  assert(CurLexerCallback != DispatchCache && "already in caching lex mode");
  SaveLexerFrame();
  CurLexerCallback = DispatchCache;
}

const Token &PrepEngine::PeekAhead(unsigned N) {
  assert(CachedLexPos + N > CachedTokens.size() && "Confused caching.");
  DisableCaching();
  size_t Target = CachedLexPos + N;
  if (Target > CachedTokens.capacity())
    CachedTokens.reserve(llvm::NextPowerOf2(Target));
  while (CachedTokens.size() < Target) {
    CachedTokens.emplace_back();
    Lex(CachedTokens.back());
  }
  EnableCaching();
  return CachedTokens[Target - 1];
}

void PrepEngine::SpliceCachedAnnotation(const Token &Tok) {
  assert(Tok.isAnnotation() && "Expected annotation token");
  assert(CachedLexPos != 0 && "Expected to have some cached tokens");
  assert(CachedTokens[CachedLexPos - 1].getLastLoc() ==
             Tok.getAnnotationEndLoc() &&
         "annotation should cover up to the most recent cached token");

  const SourceLocation TokLoc = Tok.getLocation();
  const Token *Base = CachedTokens.data();
  size_t Lo = 0;
  size_t Hi = CachedLexPos;

  while (Lo < Hi) {
    size_t Idx = Hi - 1;
    if (Base[Idx].getLocation() == TokLoc) {
      size_t i = Idx + 1;
      assert((BacktrackPositions.empty() || BacktrackPositions.back() <= i) &&
             "backtrack pos inside annotated tokens");
      if (i < CachedLexPos)
        CachedTokens.erase(CachedTokens.begin() + i,
                           CachedTokens.begin() + CachedLexPos);
      CachedTokens[Idx] = Tok;
      CachedLexPos = i;
      return;
    }
    Hi = Idx;
  }
}

bool PrepEngine::IsLastCachedToken(const Token &Tok) const {
  if (!CachedLexPos)
    return false;

  const Token LastCachedTok = CachedTokens[CachedLexPos - 1];
  if (LastCachedTok.getKind() != Tok.getKind())
    return false;

  SourceLocation::IntTy RelOffset = 0;
  if ((!getSourceManager().isInSameSLocAddrSpace(
          Tok.getLocation(), getLastCachedTokenLocation(), &RelOffset)) ||
      RelOffset)
    return false;

  return true;
}

void PrepEngine::ReplaceLastCachedToken(llvm::ArrayRef<Token> NewToks) {
  assert(CachedLexPos != 0 && "Expected to have some cached tokens");
  const size_t ReplaceIdx = CachedLexPos - 1;

  if (LLVM_LIKELY(NewToks.size() == 1)) {
    CachedTokens[ReplaceIdx] = NewToks[0];
    return;
  }

  CachedTokens[ReplaceIdx] = NewToks[0];
  const size_t Extra = NewToks.size() - 1;
  CachedTokens.insert(CachedTokens.begin() + ReplaceIdx + 1,
                      NewToks.begin() + 1, NewToks.end());
  CachedLexPos += Extra;
}
