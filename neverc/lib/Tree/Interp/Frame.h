#ifndef NEVERC_AST_INTERP_FRAME_H
#define NEVERC_AST_INTERP_FRAME_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "llvm/Support/raw_ostream.h"

namespace neverc {
class FunctionDecl;

namespace interp {

class Frame {
public:
  virtual ~Frame();

  virtual void describe(llvm::raw_ostream &OS) const = 0;

  virtual Frame *getCaller() const = 0;

  virtual SourceRange getCallRange() const = 0;

  virtual const FunctionDecl *getCallee() const = 0;
};

} // namespace interp
} // namespace neverc

#endif
