#include "neverc/Compiler/MultiplexConsumer.h"
#include "neverc/Tree/Core/TreeMutationListener.h"
#include "neverc/Tree/Decl/DeclGroup.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Casting.h"

using namespace neverc;
using llvm::dyn_cast;

namespace neverc {

class MultiplexASTMutationListener : public TreeMutationListener {
public:
  MultiplexASTMutationListener(llvm::ArrayRef<TreeMutationListener *> L);
  void CompletedTagDefinition(const TagDecl *D) override;
  void AddedVisibleDecl(const DeclContext *DC, const Decl *D) override;
  void DeclarationMarkedUsed(const Decl *D) override;
  void AddedAttributeToRecord(const Attr *Attr,
                              const RecordDecl *Record) override;

private:
  std::vector<TreeMutationListener *> Listeners;
};

MultiplexASTMutationListener::MultiplexASTMutationListener(
    llvm::ArrayRef<TreeMutationListener *> L)
    : Listeners(L.begin(), L.end()) {}

void MultiplexASTMutationListener::CompletedTagDefinition(const TagDecl *D) {
  for (size_t i = 0, e = Listeners.size(); i != e; ++i)
    Listeners[i]->CompletedTagDefinition(D);
}

void MultiplexASTMutationListener::AddedVisibleDecl(const DeclContext *DC,
                                                    const Decl *D) {
  for (size_t i = 0, e = Listeners.size(); i != e; ++i)
    Listeners[i]->AddedVisibleDecl(DC, D);
}

void MultiplexASTMutationListener::DeclarationMarkedUsed(const Decl *D) {
  for (size_t i = 0, e = Listeners.size(); i != e; ++i)
    Listeners[i]->DeclarationMarkedUsed(D);
}

void MultiplexASTMutationListener::AddedAttributeToRecord(
    const Attr *Attr, const RecordDecl *Record) {
  for (auto *L : Listeners)
    L->AddedAttributeToRecord(Attr, Record);
}

} // end namespace neverc

MultiplexConsumer::MultiplexConsumer(
    std::vector<std::unique_ptr<TreeConsumer>> C)
    : Consumers(std::move(C)) {
  std::vector<TreeMutationListener *> mutationListeners;
  for (auto &Consumer : Consumers) {
    if (auto *mutationListener = Consumer->GetTreeMutationListener())
      mutationListeners.push_back(mutationListener);
  }
  if (!mutationListeners.empty()) {
    MutationListener =
        std::make_unique<MultiplexASTMutationListener>(mutationListeners);
  }
}

MultiplexConsumer::~MultiplexConsumer() {}

void MultiplexConsumer::Initialize(TreeContext &Context) {
  for (auto &Consumer : Consumers)
    Consumer->Initialize(Context);
}

bool MultiplexConsumer::ProcessTopLevelDecl(DeclGroupRef D) {
  bool Continue = true;
  for (auto &Consumer : Consumers)
    Continue = Continue && Consumer->ProcessTopLevelDecl(D);
  return Continue;
}

void MultiplexConsumer::ProcessInlineFunctionDefinition(FunctionDecl *D) {
  for (auto &Consumer : Consumers)
    Consumer->ProcessInlineFunctionDefinition(D);
}

void MultiplexConsumer::ProcessInterestingDecl(DeclGroupRef D) {
  for (auto &Consumer : Consumers)
    Consumer->ProcessInterestingDecl(D);
}

void MultiplexConsumer::ProcessTranslationUnit(TreeContext &Ctx) {
  for (auto &Consumer : Consumers)
    Consumer->ProcessTranslationUnit(Ctx);
}

void MultiplexConsumer::ProcessTagDeclDefinition(TagDecl *D) {
  for (auto &Consumer : Consumers)
    Consumer->ProcessTagDeclDefinition(D);
}

void MultiplexConsumer::ProcessTagDeclRequiredDefinition(const TagDecl *D) {
  for (auto &Consumer : Consumers)
    Consumer->ProcessTagDeclRequiredDefinition(D);
}

void MultiplexConsumer::FinalizeTentativeDefinition(VarDecl *D) {
  for (auto &Consumer : Consumers)
    Consumer->FinalizeTentativeDefinition(D);
}

void MultiplexConsumer::FinalizeExternalDeclaration(VarDecl *D) {
  for (auto &Consumer : Consumers)
    Consumer->FinalizeExternalDeclaration(D);
}

TreeMutationListener *MultiplexConsumer::GetTreeMutationListener() {
  return MutationListener.get();
}

void MultiplexConsumer::PrintStats() {
  for (auto &Consumer : Consumers)
    Consumer->PrintStats();
}

void MultiplexConsumer::InitializeSema(Sema &S) {
  for (auto &Consumer : Consumers)
    if (SemaConsumer *SC = dyn_cast<SemaConsumer>(Consumer.get()))
      SC->InitializeSema(S);
}

void MultiplexConsumer::ForgetSema() {
  for (auto &Consumer : Consumers)
    if (SemaConsumer *SC = dyn_cast<SemaConsumer>(Consumer.get()))
      SC->ForgetSema();
}
