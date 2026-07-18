#include "PluginTreeConsumer.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/Diagnostic/DiagnosticDriver.h"
#include "neverc/Plugin/Host/FrontendPluginBridge.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Decl/DeclGroup.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"
#include <string>

using namespace llvm;

namespace neverc::plugin {

PluginTreeConsumer::PluginTreeConsumer(PluginASTBridge &ASTValue)
    : AST(ASTValue) {}

void PluginTreeConsumer::reportFailure(NevercStatus Status) {
  if (Failed)
    return;
  Failed = true;
  if (!Context)
    return;
  std::string Message =
      "plugin AST lifecycle observer failed with status " +
      std::to_string(Status.Code);
  Context->getDiagnostics().Report(diag::err_drv_plugin_phase) << Message;
}

bool PluginTreeConsumer::emit(uint32_t Kind, Decl *Declaration,
                              bool CleanupEvent) {
  if ((Failed && !CleanupEvent) || !Context)
    return !Failed;

  auto TranslationUnit = AST.publishDecl(Context->getTranslationUnitDecl());
  if (!TranslationUnit) {
    consumeError(TranslationUnit.takeError());
    reportFailure({NEVERC_STATUS_RESOURCE_EXHAUSTED, 0, 0});
    return false;
  }

  NevercASTLifecycleEvent Event{};
  Event.Header = {sizeof(Event), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
  Event.Kind = Kind;
  Event.TranslationUnit = *TranslationUnit;
  if (Declaration) {
    auto Published = AST.publishDecl(Declaration);
    if (!Published) {
      consumeError(Published.takeError());
      reportFailure({NEVERC_STATUS_RESOURCE_EXHAUSTED, 0, 0});
      return false;
    }
    Event.Declaration = *Published;
    Event.Declarations = &Event.Declaration;
    Event.DeclarationCount = 1;
  }

  NevercStatus Status = AST.dispatchLifecycleEvent(Event);
  if (Status.Code != NEVERC_STATUS_OK) {
    reportFailure(Status);
    return false;
  }
  return !Failed;
}

bool PluginTreeConsumer::emitGroup(uint32_t Kind,
                                   DeclGroupRef Declarations) {
  if (Failed || !Context)
    return false;

  SmallVector<NevercDeclHandle, 4> Published;
  for (Decl *Declaration : Declarations) {
    auto Handle = AST.publishDecl(Declaration);
    if (!Handle) {
      consumeError(Handle.takeError());
      reportFailure({NEVERC_STATUS_RESOURCE_EXHAUSTED, 0, 0});
      return false;
    }
    Published.push_back(*Handle);
  }

  auto TranslationUnit = AST.publishDecl(Context->getTranslationUnitDecl());
  if (!TranslationUnit) {
    consumeError(TranslationUnit.takeError());
    reportFailure({NEVERC_STATUS_RESOURCE_EXHAUSTED, 0, 0});
    return false;
  }

  NevercASTLifecycleEvent Event{};
  Event.Header = {sizeof(Event), NEVERC_AST_API_MAJOR, NEVERC_AST_API_MINOR, 0};
  Event.Kind = Kind;
  Event.TranslationUnit = *TranslationUnit;
  Event.Declarations = Published.data();
  Event.DeclarationCount = Published.size();
  if (!Published.empty())
    Event.Declaration = Published.front();

  NevercStatus Status = AST.dispatchLifecycleEvent(Event);
  if (Status.Code != NEVERC_STATUS_OK) {
    reportFailure(Status);
    return false;
  }
  return true;
}

void PluginTreeConsumer::Initialize(TreeContext &ContextValue) {
  Context = &ContextValue;
  (void)emit(NEVERC_AST_LIFECYCLE_TREE_INITIALIZE);
}

bool PluginTreeConsumer::ProcessTopLevelDecl(DeclGroupRef Declarations) {
  return emitGroup(NEVERC_AST_LIFECYCLE_TOP_LEVEL_DECL, Declarations);
}

void PluginTreeConsumer::ProcessInlineFunctionDefinition(
    FunctionDecl *Declaration) {
  (void)emit(NEVERC_AST_LIFECYCLE_INLINE_FUNCTION_DEFINITION, Declaration);
}

void PluginTreeConsumer::ProcessInterestingDecl(DeclGroupRef Declarations) {
  (void)emitGroup(NEVERC_AST_LIFECYCLE_INTERESTING_DECL, Declarations);
}

void PluginTreeConsumer::ProcessTranslationUnit(TreeContext &ContextValue) {
  Context = &ContextValue;
  (void)emit(NEVERC_AST_LIFECYCLE_TRANSLATION_UNIT);
}

void PluginTreeConsumer::ProcessTagDeclDefinition(TagDecl *Declaration) {
  (void)emit(NEVERC_AST_LIFECYCLE_TAG_DEFINITION, Declaration);
}

void PluginTreeConsumer::ProcessTagDeclRequiredDefinition(
    const TagDecl *Declaration) {
  (void)emit(NEVERC_AST_LIFECYCLE_TAG_REQUIRED_DEFINITION,
             const_cast<TagDecl *>(Declaration));
}

void PluginTreeConsumer::FinalizeTentativeDefinition(VarDecl *Declaration) {
  (void)emit(NEVERC_AST_LIFECYCLE_TENTATIVE_DEFINITION, Declaration);
}

void PluginTreeConsumer::FinalizeExternalDeclaration(VarDecl *Declaration) {
  (void)emit(NEVERC_AST_LIFECYCLE_EXTERNAL_DECLARATION, Declaration);
}

void PluginTreeConsumer::InitializeSema(Sema &SemanticAnalyzer) {
  (void)SemanticAnalyzer;
  if (SemaActive)
    return;
  SemaActive = true;
  (void)emit(NEVERC_AST_LIFECYCLE_SEMA_BEGIN);
}

void PluginTreeConsumer::ForgetSema() {
  if (!SemaActive)
    return;
  SemaActive = false;
  (void)emit(NEVERC_AST_LIFECYCLE_SEMA_END, nullptr,
             /*CleanupEvent=*/true);
}

} // namespace neverc::plugin
