#include "neverc/Plugin/Host/MCPluginBridge.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Error.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus mcStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

template <typename Storage, typename Value>
auto findValue(Storage &Values, Value *Pointer) {
  return llvm::find_if(Values, [Pointer](const auto &Entry) {
    return Entry.get() == Pointer;
  });
}

std::string copyString(NevercStringView Value) {
  return Value.Length == 0
             ? std::string()
             : std::string(Value.Data, static_cast<size_t>(Value.Length));
}

std::vector<uint8_t> copyBytes(NevercByteView Value) {
  return Value.Length == 0
             ? std::vector<uint8_t>()
             : std::vector<uint8_t>(
                   Value.Data, Value.Data + Value.Length);
}

Error invalidMutation() {
  return createStringError(inconvertibleErrorCode(),
                           "invalid MC mutation input");
}

} // namespace

Expected<NevercMCSectionHandle> MCPluginBridge::createSection(
    NevercMCMutationHandle Mutation,
    const NevercMCSectionDescriptor &Descriptor) {
  if (checkMutation(Mutation).Code != NEVERC_STATUS_OK)
    return invalidMutation();
  auto Section = std::make_unique<PluginMCSection>();
  Section->Name = copyString(Descriptor.Name);
  Section->Alignment = Descriptor.Alignment;
  Section->Flags = Descriptor.Flags;
  PluginMCSection *Raw = Section.get();
  Unit.sections().push_back(std::move(Section));
  CreatedSections.push_back(Raw);
  UndoActions.push_back([this, Raw] {
    auto It = findValue(Unit.sections(), Raw);
    if (It != Unit.sections().end())
      Unit.sections().erase(It);
  });
  return wrapSection(*Raw);
}

NevercStatus MCPluginBridge::moveSectionBefore(
    NevercMCMutationHandle Mutation, NevercMCSectionHandle Section,
    NevercMCSectionHandle Position) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginMCSection *Value = nullptr;
  PluginMCSection *PositionValue = nullptr;
  Status = resolveSection(Section, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!neverc_handle_is_null(Position)) {
    Status = resolveSection(Position, &PositionValue);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
  if (Value == PositionValue)
    return neverc_status_ok();
  auto It = findValue(Unit.sections(), Value);
  auto PositionIt = PositionValue
                        ? findValue(Unit.sections(), PositionValue)
                        : Unit.sections().end();
  if (It == Unit.sections().end() ||
      (PositionValue && PositionIt == Unit.sections().end()))
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto OldNext = std::next(It);
  PluginMCSection *OldNextValue =
      OldNext == Unit.sections().end() ? nullptr : OldNext->get();
  Unit.sections().splice(PositionIt, Unit.sections(), It);
  UndoActions.push_back([this, Value, OldNextValue] {
    auto Current = findValue(Unit.sections(), Value);
    if (Current == Unit.sections().end())
      return;
    auto OldPosition = OldNextValue
                           ? findValue(Unit.sections(), OldNextValue)
                           : Unit.sections().end();
    Unit.sections().splice(OldPosition, Unit.sections(), Current);
  });
  return neverc_status_ok();
}

NevercStatus MCPluginBridge::eraseSection(
    NevercMCMutationHandle Mutation, NevercMCSectionHandle Section) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginMCSection *Value = nullptr;
  Status = resolveSection(Section, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Value->Fragments.empty() ||
      llvm::any_of(Unit.symbols(), [Value](const auto &Symbol) {
        return Symbol->Section == Value;
      }))
    return mcStatus(NEVERC_STATUS_INVALID_STATE);
  auto It = findValue(Unit.sections(), Value);
  if (It == Unit.sections().end())
    return mcStatus(NEVERC_STATUS_STALE_HANDLE);
  auto Next = std::next(It);
  PluginMCSection *NextValue =
      Next == Unit.sections().end() ? nullptr : Next->get();
  RemovedSections.splice(RemovedSections.end(), Unit.sections(), It);
  UndoActions.push_back([this, Value, NextValue] {
    auto RemovedIt = findValue(RemovedSections, Value);
    if (RemovedIt == RemovedSections.end())
      return;
    auto Position = NextValue ? findValue(Unit.sections(), NextValue)
                              : Unit.sections().end();
    Unit.sections().splice(Position, RemovedSections, RemovedIt);
  });
  invalidateSection(Value);
  return neverc_status_ok();
}

Expected<NevercMCSymbolHandle> MCPluginBridge::createSymbol(
    NevercMCMutationHandle Mutation,
    const NevercMCSymbolDescriptor &Descriptor) {
  if (checkMutation(Mutation).Code != NEVERC_STATUS_OK)
    return invalidMutation();
  PluginMCSection *Section = nullptr;
  if (!neverc_handle_is_null(Descriptor.Section) &&
      resolveSection(Descriptor.Section, &Section).Code !=
          NEVERC_STATUS_OK)
    return invalidMutation();
  auto Symbol = std::make_unique<PluginMCSymbol>();
  Symbol->Name = copyString(Descriptor.Name);
  Symbol->Binding = Descriptor.Binding;
  Symbol->Visibility = Descriptor.Visibility;
  Symbol->Type = Descriptor.Type;
  Symbol->Definition = Descriptor.Definition;
  Symbol->Section = Section;
  Symbol->Value = Descriptor.Value;
  Symbol->Size = Descriptor.Size;
  Symbol->Alignment = Descriptor.Alignment;
  Symbol->Flags = Descriptor.Flags;
  PluginMCSymbol *Raw = Symbol.get();
  Unit.symbols().push_back(std::move(Symbol));
  CreatedSymbols.push_back(Raw);
  UndoActions.push_back([this, Raw] {
    auto It = findValue(Unit.symbols(), Raw);
    if (It != Unit.symbols().end())
      Unit.symbols().erase(It);
  });
  return wrapSymbol(*Raw);
}

NevercStatus MCPluginBridge::moveSymbolBefore(
    NevercMCMutationHandle Mutation, NevercMCSymbolHandle Symbol,
    NevercMCSymbolHandle Position) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginMCSymbol *Value = nullptr;
  PluginMCSymbol *PositionValue = nullptr;
  Status = resolveSymbol(Symbol, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!neverc_handle_is_null(Position)) {
    Status = resolveSymbol(Position, &PositionValue);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
  if (Value == PositionValue)
    return neverc_status_ok();
  auto It = findValue(Unit.symbols(), Value);
  auto PositionIt = PositionValue ? findValue(Unit.symbols(), PositionValue)
                                  : Unit.symbols().end();
  if (It == Unit.symbols().end() ||
      (PositionValue && PositionIt == Unit.symbols().end()))
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto OldNext = std::next(It);
  PluginMCSymbol *OldNextValue =
      OldNext == Unit.symbols().end() ? nullptr : OldNext->get();
  Unit.symbols().splice(PositionIt, Unit.symbols(), It);
  UndoActions.push_back([this, Value, OldNextValue] {
    auto Current = findValue(Unit.symbols(), Value);
    if (Current == Unit.symbols().end())
      return;
    auto OldPosition = OldNextValue
                           ? findValue(Unit.symbols(), OldNextValue)
                           : Unit.symbols().end();
    Unit.symbols().splice(OldPosition, Unit.symbols(), Current);
  });
  return neverc_status_ok();
}

NevercStatus MCPluginBridge::eraseSymbol(
    NevercMCMutationHandle Mutation, NevercMCSymbolHandle Symbol) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginMCSymbol *Value = nullptr;
  Status = resolveSymbol(Symbol, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (llvm::any_of(Unit.expressions(), [Value](const auto &Expression) {
        return Expression->Symbol == Value;
      }))
    return mcStatus(NEVERC_STATUS_INVALID_STATE);
  auto It = findValue(Unit.symbols(), Value);
  if (It == Unit.symbols().end())
    return mcStatus(NEVERC_STATUS_STALE_HANDLE);
  auto Next = std::next(It);
  PluginMCSymbol *NextValue =
      Next == Unit.symbols().end() ? nullptr : Next->get();
  RemovedSymbols.splice(RemovedSymbols.end(), Unit.symbols(), It);
  UndoActions.push_back([this, Value, NextValue] {
    auto RemovedIt = findValue(RemovedSymbols, Value);
    if (RemovedIt == RemovedSymbols.end())
      return;
    auto Position = NextValue ? findValue(Unit.symbols(), NextValue)
                              : Unit.symbols().end();
    Unit.symbols().splice(Position, RemovedSymbols, RemovedIt);
  });
  invalidateSymbol(Value);
  return neverc_status_ok();
}

Expected<NevercMCExprHandle> MCPluginBridge::createExpression(
    NevercMCMutationHandle Mutation,
    const NevercMCExpressionDescriptor &Descriptor) {
  if (checkMutation(Mutation).Code != NEVERC_STATUS_OK ||
      Descriptor.Reserved != 0)
    return invalidMutation();
  if (Descriptor.Kind == NEVERC_MC_EXPRESSION_TARGET_VARIANT) {
    if (checkSchemaToken(Descriptor.SchemaToken).Code !=
        NEVERC_STATUS_OK)
      return invalidMutation();
  } else if (!neverc_handle_is_null(Descriptor.SchemaToken)) {
    return invalidMutation();
  }
  PluginMCSymbol *Symbol = nullptr;
  PluginMCExpression *Left = nullptr;
  PluginMCExpression *Right = nullptr;
  if (!neverc_handle_is_null(Descriptor.Symbol) &&
      resolveSymbol(Descriptor.Symbol, &Symbol).Code !=
          NEVERC_STATUS_OK)
    return invalidMutation();
  if (!neverc_handle_is_null(Descriptor.Left) &&
      resolveExpression(Descriptor.Left, &Left).Code !=
          NEVERC_STATUS_OK)
    return invalidMutation();
  if (!neverc_handle_is_null(Descriptor.Right) &&
      resolveExpression(Descriptor.Right, &Right).Code !=
          NEVERC_STATUS_OK)
    return invalidMutation();
  auto Expression = std::make_unique<PluginMCExpression>();
  Expression->Kind = Descriptor.Kind;
  Expression->Operator = Descriptor.Operator;
  Expression->Constant = Descriptor.Constant;
  Expression->Symbol = Symbol;
  Expression->Left = Left;
  Expression->Right = Right;
  Expression->TargetVariant = Descriptor.TargetVariant;
  Expression->Extension.Owner = Descriptor.ExtensionOwner;
  Expression->Extension.Bytes = copyBytes(Descriptor.Extension);
  PluginMCExpression *Raw = Expression.get();
  Unit.expressions().push_back(std::move(Expression));
  CreatedExpressions.push_back(Raw);
  UndoActions.push_back([this, Raw] {
    auto It = findValue(Unit.expressions(), Raw);
    if (It != Unit.expressions().end())
      Unit.expressions().erase(It);
  });
  return wrapExpression(*Raw);
}

NevercStatus MCPluginBridge::setExpressionOperands(
    NevercMCMutationHandle Mutation, NevercMCExprHandle Expression,
    NevercMCExprHandle Left, NevercMCExprHandle Right) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginMCExpression *Value = nullptr;
  PluginMCExpression *LeftValue = nullptr;
  PluginMCExpression *RightValue = nullptr;
  Status = resolveExpression(Expression, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!neverc_handle_is_null(Left)) {
    Status = resolveExpression(Left, &LeftValue);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
  if (!neverc_handle_is_null(Right)) {
    Status = resolveExpression(Right, &RightValue);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
  PluginMCExpression *OldLeft = Value->Left;
  PluginMCExpression *OldRight = Value->Right;
  Value->Left = LeftValue;
  Value->Right = RightValue;
  UndoActions.push_back([Value, OldLeft, OldRight] {
    Value->Left = OldLeft;
    Value->Right = OldRight;
  });
  return neverc_status_ok();
}

NevercStatus MCPluginBridge::eraseExpression(
    NevercMCMutationHandle Mutation, NevercMCExprHandle Expression) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginMCExpression *Value = nullptr;
  Status = resolveExpression(Expression, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  bool Referenced =
      llvm::any_of(Unit.expressions(), [Value](const auto &Entry) {
        return Entry.get() != Value &&
               (Entry->Left == Value || Entry->Right == Value);
      });
  if (!Referenced)
    for (const auto &Section : Unit.sections())
      for (const auto &Fragment : Section->Fragments)
        if (llvm::any_of(Fragment->Fixups,
                         [Value](const auto &Fixup) {
                           return Fixup->Expression == Value;
                         }))
          Referenced = true;
  if (Referenced)
    return mcStatus(NEVERC_STATUS_INVALID_STATE);
  auto It = findValue(Unit.expressions(), Value);
  if (It == Unit.expressions().end())
    return mcStatus(NEVERC_STATUS_STALE_HANDLE);
  auto Next = std::next(It);
  PluginMCExpression *NextValue =
      Next == Unit.expressions().end() ? nullptr : Next->get();
  RemovedExpressions.splice(RemovedExpressions.end(),
                            Unit.expressions(), It);
  UndoActions.push_back([this, Value, NextValue] {
    auto RemovedIt = findValue(RemovedExpressions, Value);
    if (RemovedIt == RemovedExpressions.end())
      return;
    auto Position =
        NextValue ? findValue(Unit.expressions(), NextValue)
                  : Unit.expressions().end();
    Unit.expressions().splice(Position, RemovedExpressions, RemovedIt);
  });
  invalidateExpression(Value);
  return neverc_status_ok();
}

Expected<NevercMCFragmentHandle> MCPluginBridge::createFragment(
    NevercMCMutationHandle Mutation, NevercMCSectionHandle Section,
    const NevercMCFragmentDescriptor &Descriptor) {
  if (checkMutation(Mutation).Code != NEVERC_STATUS_OK)
    return invalidMutation();
  PluginMCSection *SectionValue = nullptr;
  if (resolveSection(Section, &SectionValue).Code != NEVERC_STATUS_OK)
    return invalidMutation();
  if (Descriptor.Kind == NEVERC_MC_FRAGMENT_FORMAT_EXTENSION) {
    if (checkSchemaToken(Descriptor.SchemaToken).Code !=
        NEVERC_STATUS_OK)
      return invalidMutation();
  } else if (!neverc_handle_is_null(Descriptor.SchemaToken)) {
    return invalidMutation();
  }
  auto Fragment = std::make_unique<PluginMCFragment>();
  Fragment->Parent = SectionValue;
  Fragment->Kind = Descriptor.Kind;
  Fragment->FillValue = Descriptor.FillValue;
  Fragment->ExplicitOffset = Descriptor.ExplicitOffset;
  Fragment->Alignment = Descriptor.Alignment;
  Fragment->Contents = copyBytes(Descriptor.Contents);
  Fragment->Extension.Owner = Descriptor.ExtensionOwner;
  Fragment->Extension.Bytes = copyBytes(Descriptor.Extension);
  PluginMCFragment *Raw = Fragment.get();
  SectionValue->Fragments.push_back(std::move(Fragment));
  CreatedFragments.push_back(Raw);
  UndoActions.push_back([SectionValue, Raw] {
    auto It = findValue(SectionValue->Fragments, Raw);
    if (It != SectionValue->Fragments.end())
      SectionValue->Fragments.erase(It);
  });
  return wrapFragment(*Raw);
}

NevercStatus MCPluginBridge::moveFragmentBefore(
    NevercMCMutationHandle Mutation, NevercMCFragmentHandle Fragment,
    NevercMCFragmentHandle Position) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginMCFragment *Value = nullptr;
  PluginMCFragment *PositionValue = nullptr;
  Status = resolveFragment(Fragment, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!neverc_handle_is_null(Position)) {
    Status = resolveFragment(Position, &PositionValue);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
  if (Value == PositionValue)
    return neverc_status_ok();
  PluginMCSection *OldParent = Value->Parent;
  PluginMCSection *NewParent =
      PositionValue ? PositionValue->Parent : OldParent;
  auto It = findValue(OldParent->Fragments, Value);
  auto PositionIt =
      PositionValue ? findValue(NewParent->Fragments, PositionValue)
                    : NewParent->Fragments.end();
  if (It == OldParent->Fragments.end() ||
      (PositionValue && PositionIt == NewParent->Fragments.end()))
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto OldNext = std::next(It);
  PluginMCFragment *OldNextValue =
      OldNext == OldParent->Fragments.end() ? nullptr : OldNext->get();
  NewParent->Fragments.splice(PositionIt, OldParent->Fragments, It);
  Value->Parent = NewParent;
  UndoActions.push_back(
      [Value, OldParent, NewParent, OldNextValue] {
        auto Current = findValue(NewParent->Fragments, Value);
        if (Current == NewParent->Fragments.end())
          return;
        auto OldPosition =
            OldNextValue
                ? findValue(OldParent->Fragments, OldNextValue)
                : OldParent->Fragments.end();
        OldParent->Fragments.splice(OldPosition, NewParent->Fragments,
                                    Current);
        Value->Parent = OldParent;
      });
  return neverc_status_ok();
}

NevercStatus MCPluginBridge::eraseFragment(
    NevercMCMutationHandle Mutation, NevercMCFragmentHandle Fragment) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginMCFragment *Value = nullptr;
  Status = resolveFragment(Fragment, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Value->Instructions.empty() || !Value->Fixups.empty())
    return mcStatus(NEVERC_STATUS_INVALID_STATE);
  PluginMCSection *Parent = Value->Parent;
  auto It = findValue(Parent->Fragments, Value);
  if (It == Parent->Fragments.end())
    return mcStatus(NEVERC_STATUS_STALE_HANDLE);
  auto Next = std::next(It);
  PluginMCFragment *NextValue =
      Next == Parent->Fragments.end() ? nullptr : Next->get();
  RemovedFragments.splice(RemovedFragments.end(), Parent->Fragments, It);
  UndoActions.push_back([this, Parent, Value, NextValue] {
    auto RemovedIt = findValue(RemovedFragments, Value);
    if (RemovedIt == RemovedFragments.end())
      return;
    auto Position = NextValue
                        ? findValue(Parent->Fragments, NextValue)
                        : Parent->Fragments.end();
    Parent->Fragments.splice(Position, RemovedFragments, RemovedIt);
  });
  invalidateFragment(Value);
  return neverc_status_ok();
}

Expected<NevercMCFixupHandle> MCPluginBridge::createFixup(
    NevercMCMutationHandle Mutation, NevercMCFragmentHandle Fragment,
    const NevercMCFixupDescriptor &Descriptor) {
  if (checkMutation(Mutation).Code != NEVERC_STATUS_OK ||
      Descriptor.Reserved != 0 || Descriptor.Reserved8 != 0)
    return invalidMutation();
  PluginMCFragment *FragmentValue = nullptr;
  PluginMCExpression *Expression = nullptr;
  if (resolveFragment(Fragment, &FragmentValue).Code !=
          NEVERC_STATUS_OK ||
      resolveExpression(Descriptor.Expression, &Expression).Code !=
          NEVERC_STATUS_OK)
    return invalidMutation();
  if (Descriptor.Kind == NEVERC_MC_FIXUP_TARGET_EXTENSION) {
    if (checkSchemaToken(Descriptor.SchemaToken).Code !=
        NEVERC_STATUS_OK)
      return invalidMutation();
  } else if (!neverc_handle_is_null(Descriptor.SchemaToken)) {
    return invalidMutation();
  }
  auto Fixup = std::make_unique<PluginMCFixup>();
  Fixup->Parent = FragmentValue;
  Fixup->Expression = Expression;
  Fixup->Offset = Descriptor.Offset;
  Fixup->Width = Descriptor.Width;
  Fixup->IsPCRelative = Descriptor.IsPCRelative != 0;
  Fixup->IsSigned = Descriptor.IsSigned != 0;
  Fixup->MayRelax = Descriptor.MayRelax != 0;
  Fixup->Kind = Descriptor.Kind;
  Fixup->TargetKind = Descriptor.TargetKind;
  PluginMCFixup *Raw = Fixup.get();
  FragmentValue->Fixups.push_back(std::move(Fixup));
  CreatedFixups.push_back(Raw);
  UndoActions.push_back([FragmentValue, Raw] {
    auto It = findValue(FragmentValue->Fixups, Raw);
    if (It != FragmentValue->Fixups.end())
      FragmentValue->Fixups.erase(It);
  });
  return wrapFixup(*Raw);
}

NevercStatus MCPluginBridge::moveFixupBefore(
    NevercMCMutationHandle Mutation, NevercMCFixupHandle Fixup,
    NevercMCFixupHandle Position) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginMCFixup *Value = nullptr;
  PluginMCFixup *PositionValue = nullptr;
  Status = resolveFixup(Fixup, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!neverc_handle_is_null(Position)) {
    Status = resolveFixup(Position, &PositionValue);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
  if (Value == PositionValue)
    return neverc_status_ok();
  PluginMCFragment *OldParent = Value->Parent;
  PluginMCFragment *NewParent =
      PositionValue ? PositionValue->Parent : OldParent;
  auto It = findValue(OldParent->Fixups, Value);
  auto PositionIt =
      PositionValue ? findValue(NewParent->Fixups, PositionValue)
                    : NewParent->Fixups.end();
  if (It == OldParent->Fixups.end() ||
      (PositionValue && PositionIt == NewParent->Fixups.end()))
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto OldNext = std::next(It);
  PluginMCFixup *OldNextValue =
      OldNext == OldParent->Fixups.end() ? nullptr : OldNext->get();
  NewParent->Fixups.splice(PositionIt, OldParent->Fixups, It);
  Value->Parent = NewParent;
  UndoActions.push_back([Value, OldParent, NewParent, OldNextValue] {
    auto Current = findValue(NewParent->Fixups, Value);
    if (Current == NewParent->Fixups.end())
      return;
    auto OldPosition =
        OldNextValue ? findValue(OldParent->Fixups, OldNextValue)
                     : OldParent->Fixups.end();
    OldParent->Fixups.splice(OldPosition, NewParent->Fixups, Current);
    Value->Parent = OldParent;
  });
  return neverc_status_ok();
}

NevercStatus MCPluginBridge::eraseFixup(
    NevercMCMutationHandle Mutation, NevercMCFixupHandle Fixup) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginMCFixup *Value = nullptr;
  Status = resolveFixup(Fixup, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginMCFragment *Parent = Value->Parent;
  auto It = findValue(Parent->Fixups, Value);
  if (It == Parent->Fixups.end())
    return mcStatus(NEVERC_STATUS_STALE_HANDLE);
  auto Next = std::next(It);
  PluginMCFixup *NextValue =
      Next == Parent->Fixups.end() ? nullptr : Next->get();
  RemovedFixups.splice(RemovedFixups.end(), Parent->Fixups, It);
  UndoActions.push_back([this, Parent, Value, NextValue] {
    auto RemovedIt = findValue(RemovedFixups, Value);
    if (RemovedIt == RemovedFixups.end())
      return;
    auto Position = NextValue ? findValue(Parent->Fixups, NextValue)
                              : Parent->Fixups.end();
    Parent->Fixups.splice(Position, RemovedFixups, RemovedIt);
  });
  invalidateFixup(Value);
  return neverc_status_ok();
}

} // namespace neverc::plugin
