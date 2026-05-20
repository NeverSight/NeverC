#ifndef NEVERC_INVOKE_INPUTINFO_H
#define NEVERC_INVOKE_INPUTINFO_H

#include "neverc/Invoke/Action.h"
#include "neverc/Invoke/Types.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Option/Arg.h"
#include <cassert>
#include <string>

namespace neverc {
namespace driver {

class InputInfo {
  enum Class { Nothing, Filename, InputArg, Pipe };

  union {
    const char *Filename;
    const llvm::opt::Arg *InputArg;
  } Data;
  Class Kind;
  const Action *Act;
  types::ID Type;
  const char *BaseInput;

  static types::ID GetActionType(const Action *A) {
    return A != nullptr ? A->getType() : types::TY_Nothing;
  }

public:
  InputInfo() : InputInfo(nullptr, nullptr) {}
  InputInfo(const Action *A, const char *_BaseInput)
      : Kind(Nothing), Act(A), Type(GetActionType(A)), BaseInput(_BaseInput) {}

  InputInfo(types::ID _Type, const char *_Filename, const char *_BaseInput)
      : Kind(Filename), Act(nullptr), Type(_Type), BaseInput(_BaseInput) {
    Data.Filename = _Filename;
  }
  InputInfo(const Action *A, const char *_Filename, const char *_BaseInput)
      : Kind(Filename), Act(A), Type(GetActionType(A)), BaseInput(_BaseInput) {
    Data.Filename = _Filename;
  }

  InputInfo(types::ID _Type, const llvm::opt::Arg *_InputArg,
            const char *_BaseInput)
      : Kind(InputArg), Act(nullptr), Type(_Type), BaseInput(_BaseInput) {
    Data.InputArg = _InputArg;
  }
  InputInfo(const Action *A, const llvm::opt::Arg *_InputArg,
            const char *_BaseInput)
      : Kind(InputArg), Act(A), Type(GetActionType(A)), BaseInput(_BaseInput) {
    Data.InputArg = _InputArg;
  }

  bool isNothing() const { return Kind == Nothing; }
  bool isFilename() const { return Kind == Filename; }
  bool isInputArg() const { return Kind == InputArg; }
  types::ID getType() const { return Type; }
  const char *getBaseInput() const { return BaseInput; }
  const Action *getAction() const { return Act; }
  void setAction(const Action *A) { Act = A; }

  const char *getFilename() const {
    assert(isFilename() && "Invalid accessor.");
    return Data.Filename;
  }
  const llvm::opt::Arg &getInputArg() const {
    assert(isInputArg() && "Invalid accessor.");
    return *Data.InputArg;
  }

  std::string getAsString() const {
    if (isFilename())
      return std::string("\"") + getFilename() + '"';
    else if (isInputArg())
      return "(input arg)";
    else
      return "(nothing)";
  }
};

typedef llvm::SmallVector<InputInfo, 4> InputInfoList;

} // end namespace driver
} // end namespace neverc

#endif
