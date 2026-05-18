#ifndef NEVERC_LEX_PRAGMADISPATCH_H
#define NEVERC_LEX_PRAGMADISPATCH_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <string>

namespace neverc {

class PragmaRegistry;
class PrepEngine;
class Token;

enum PragmaIntroducerKind { PIK_HashPragma, PIK__Pragma, PIK___pragma };

struct PragmaIntroducer {
  PragmaIntroducerKind Kind;
  SourceLocation Loc;
};

class PragmaDispatch {
  std::string Name;

public:
  PragmaDispatch() = default;
  explicit PragmaDispatch(llvm::StringRef name) : Name(name) {}
  virtual ~PragmaDispatch();

  llvm::StringRef getName() const { return Name; }
  virtual void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                             Token &FirstToken) = 0;

  virtual PragmaRegistry *getIfNamespace() { return nullptr; }
};

class EmptyPragmaDispatch : public PragmaDispatch {
public:
  explicit EmptyPragmaDispatch(llvm::StringRef Name = llvm::StringRef());

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &FirstToken) override;
};

class PragmaRegistry : public PragmaDispatch {
  llvm::StringMap<std::unique_ptr<PragmaDispatch>> Handlers;

public:
  explicit PragmaRegistry(llvm::StringRef Name) : PragmaDispatch(Name) {}

  PragmaDispatch *FindHandler(llvm::StringRef Name,
                              bool IgnoreNull = true) const;

  void AddPragma(PragmaDispatch *Handler);
  void RemovePragmaDispatch(PragmaDispatch *Handler);

  bool IsEmpty() const { return Handlers.empty(); }

  void ProcessPragma(PrepEngine &PP, PragmaIntroducer Introducer,
                     Token &Tok) override;

  PragmaRegistry *getIfNamespace() override { return this; }
};

void prepare_PragmaString(llvm::SmallVectorImpl<char> &StrVal);

} // namespace neverc

#endif // NEVERC_LEX_PRAGMADISPATCH_H
