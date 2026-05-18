#include "neverc/Invoke/Action.h"
#include "llvm/Support/ErrorHandling.h"

using namespace neverc;
using namespace driver;
using namespace llvm::opt;
using llvm::StringRef;

Action::~Action() = default;

const char *Action::getClassName(ActionClass AC) {
  switch (AC) {
  case InputClass:
    return "input";
  case BindArchClass:
    return "bind-arch";
  case PreprocessJobClass:
    return "preprocessor";
  case CompileJobClass:
    return "compiler";
  case BackendJobClass:
    return "backend";
  case AssembleJobClass:
    return "assembler";
  case LinkJobClass:
    return "linker";
  case LipoJobClass:
    return "lipo";
  case DsymutilJobClass:
    return "dsymutil";
  case StaticLibJobClass:
    return "static-lib-linker";
  }

  llvm_unreachable("invalid class");
}

void InputAction::anchor() {}

InputAction::InputAction(const Arg &_Input, types::ID _Type,
                         llvm::StringRef _Id)
    : Action(InputClass, _Type), Input(_Input), Id(_Id.str()) {}

void BindArchAction::anchor() {}

BindArchAction::BindArchAction(Action *Input, llvm::StringRef ArchName)
    : Action(BindArchClass, Input), ArchName(ArchName) {}

void JobAction::anchor() {}

JobAction::JobAction(ActionClass Kind, Action *Input, types::ID Type)
    : Action(Kind, Input, Type) {}

JobAction::JobAction(ActionClass Kind, const ActionList &Inputs, types::ID Type)
    : Action(Kind, Inputs, Type) {}

void PreprocessJobAction::anchor() {}

PreprocessJobAction::PreprocessJobAction(Action *Input, types::ID OutputType)
    : JobAction(PreprocessJobClass, Input, OutputType) {}

void CompileJobAction::anchor() {}

CompileJobAction::CompileJobAction(Action *Input, types::ID OutputType)
    : JobAction(CompileJobClass, Input, OutputType) {}

void BackendJobAction::anchor() {}

BackendJobAction::BackendJobAction(Action *Input, types::ID OutputType)
    : JobAction(BackendJobClass, Input, OutputType) {}

void AssembleJobAction::anchor() {}

AssembleJobAction::AssembleJobAction(Action *Input, types::ID OutputType)
    : JobAction(AssembleJobClass, Input, OutputType) {}

void LinkJobAction::anchor() {}

LinkJobAction::LinkJobAction(ActionList &Inputs, types::ID Type)
    : JobAction(LinkJobClass, Inputs, Type) {}

void LipoJobAction::anchor() {}

LipoJobAction::LipoJobAction(ActionList &Inputs, types::ID Type)
    : JobAction(LipoJobClass, Inputs, Type) {}

void DsymutilJobAction::anchor() {}

DsymutilJobAction::DsymutilJobAction(ActionList &Inputs, types::ID Type)
    : JobAction(DsymutilJobClass, Inputs, Type) {}

void StaticLibJobAction::anchor() {}

StaticLibJobAction::StaticLibJobAction(ActionList &Inputs, types::ID Type)
    : JobAction(StaticLibJobClass, Inputs, Type) {}
