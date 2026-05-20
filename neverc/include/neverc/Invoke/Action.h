#ifndef NEVERC_INVOKE_ACTION_H
#define NEVERC_INVOKE_ACTION_H

#include "neverc/Invoke/Types.h"
#include "neverc/Invoke/Util.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include <string>

namespace llvm {
namespace opt {

class Arg;

} // namespace opt
} // namespace llvm

namespace neverc {
namespace driver {

class Action {
public:
  using size_type = ActionList::size_type;
  using input_iterator = ActionList::iterator;
  using input_const_iterator = ActionList::const_iterator;
  using input_range = llvm::iterator_range<input_iterator>;
  using input_const_range = llvm::iterator_range<input_const_iterator>;

  enum ActionClass {
    InputClass = 0,
    BindArchClass,
    PreprocessJobClass,
    CompileJobClass,
    BackendJobClass,
    AssembleJobClass,
    LinkJobClass,
    LipoJobClass,
    DsymutilJobClass,
    StaticLibJobClass,

    JobClassFirst = PreprocessJobClass,
    JobClassLast = StaticLibJobClass
  };

  static const char *getClassName(ActionClass AC);

private:
  ActionClass Kind;

  types::ID Type;

  ActionList Inputs;

  bool CanBeCollapsedWithNextDependentAction = true;

protected:
  Action(ActionClass Kind, types::ID Type) : Action(Kind, ActionList(), Type) {}
  Action(ActionClass Kind, Action *Input, types::ID Type)
      : Action(Kind, ActionList({Input}), Type) {}
  Action(ActionClass Kind, Action *Input)
      : Action(Kind, ActionList({Input}), Input->getType()) {}
  Action(ActionClass Kind, const ActionList &Inputs, types::ID Type)
      : Kind(Kind), Type(Type), Inputs(Inputs) {}

public:
  virtual ~Action();

  const char *getClassName() const { return Action::getClassName(getKind()); }

  ActionClass getKind() const { return Kind; }
  types::ID getType() const { return Type; }

  ActionList &getInputs() { return Inputs; }
  const ActionList &getInputs() const { return Inputs; }

  size_type size() const { return Inputs.size(); }

  input_iterator input_begin() { return Inputs.begin(); }
  input_iterator input_end() { return Inputs.end(); }
  input_range inputs() { return input_range(input_begin(), input_end()); }
  input_const_iterator input_begin() const { return Inputs.begin(); }
  input_const_iterator input_end() const { return Inputs.end(); }
  input_const_range inputs() const {
    return input_const_range(input_begin(), input_end());
  }

  void setCannotBeCollapsedWithNextDependentAction() {
    CanBeCollapsedWithNextDependentAction = false;
  }

  bool isCollapsingWithNextDependentActionLegal() const {
    return CanBeCollapsedWithNextDependentAction;
  }
};

class InputAction : public Action {
  const llvm::opt::Arg &Input;
  std::string Id;
  virtual void anchor();

public:
  InputAction(const llvm::opt::Arg &Input, types::ID Type,
              llvm::StringRef Id = llvm::StringRef());

  const llvm::opt::Arg &getInputArg() const { return Input; }

  void setId(llvm::StringRef _Id) { Id = _Id.str(); }
  llvm::StringRef getId() const { return Id; }

  static bool classof(const Action *A) { return A->getKind() == InputClass; }
};

class BindArchAction : public Action {
  virtual void anchor();

  llvm::StringRef ArchName;

public:
  BindArchAction(Action *Input, llvm::StringRef ArchName);

  llvm::StringRef getArchName() const { return ArchName; }

  static bool classof(const Action *A) { return A->getKind() == BindArchClass; }
};

class JobAction : public Action {
  virtual void anchor();

protected:
  JobAction(ActionClass Kind, Action *Input, types::ID Type);
  JobAction(ActionClass Kind, const ActionList &Inputs, types::ID Type);

public:
  static bool classof(const Action *A) {
    return (A->getKind() >= JobClassFirst && A->getKind() <= JobClassLast);
  }
};

class PreprocessJobAction : public JobAction {
  void anchor() override;

public:
  PreprocessJobAction(Action *Input, types::ID OutputType);

  static bool classof(const Action *A) {
    return A->getKind() == PreprocessJobClass;
  }
};

class CompileJobAction : public JobAction {
  void anchor() override;

public:
  CompileJobAction(Action *Input, types::ID OutputType);

  static bool classof(const Action *A) {
    return A->getKind() == CompileJobClass;
  }
};

class BackendJobAction : public JobAction {
  void anchor() override;

public:
  BackendJobAction(Action *Input, types::ID OutputType);

  static bool classof(const Action *A) {
    return A->getKind() == BackendJobClass;
  }
};

class AssembleJobAction : public JobAction {
  void anchor() override;

public:
  AssembleJobAction(Action *Input, types::ID OutputType);

  static bool classof(const Action *A) {
    return A->getKind() == AssembleJobClass;
  }
};

class LinkJobAction : public JobAction {
  void anchor() override;

public:
  LinkJobAction(ActionList &Inputs, types::ID Type);

  static bool classof(const Action *A) { return A->getKind() == LinkJobClass; }
};

class LipoJobAction : public JobAction {
  void anchor() override;

public:
  LipoJobAction(ActionList &Inputs, types::ID Type);

  static bool classof(const Action *A) { return A->getKind() == LipoJobClass; }
};

class DsymutilJobAction : public JobAction {
  void anchor() override;

public:
  DsymutilJobAction(ActionList &Inputs, types::ID Type);

  static bool classof(const Action *A) {
    return A->getKind() == DsymutilJobClass;
  }
};

class StaticLibJobAction : public JobAction {
  void anchor() override;

public:
  StaticLibJobAction(ActionList &Inputs, types::ID Type);

  static bool classof(const Action *A) {
    return A->getKind() == StaticLibJobClass;
  }
};

} // namespace driver
} // namespace neverc

#endif // NEVERC_INVOKE_ACTION_H
