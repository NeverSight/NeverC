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

MCPluginBridge *bridge(void *Context, NevercTaskHandle Task,
                       NevercStatus &Status) {
  if (!Context) {
    Status = mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return nullptr;
  }
  auto *Bridge = static_cast<MCPluginBridge *>(Context);
  NevercTaskHandle Expected = Bridge->taskHandle();
  if (Expected.Owner != Task.Owner || Expected.Value != Task.Value) {
    Status = mcStatus(NEVERC_STATUS_WRONG_SCOPE);
    return nullptr;
  }
  Status = neverc_status_ok();
  return Bridge;
}

template <typename T> bool validRecord(const T *Value) {
  return Value && Value->Header.StructSize >= sizeof(T) &&
         Value->Header.Major == NEVERC_MC_API_MAJOR &&
         Value->Header.Minor <= NEVERC_MC_API_MINOR;
}

bool validString(NevercStringView Value) {
  return Value.Length == 0 || Value.Data != nullptr;
}

bool validBytes(NevercByteView Value) {
  return Value.Length == 0 || Value.Data != nullptr;
}

template <typename T>
NevercStatus writeHandle(Expected<T> Handle, T *Output) {
  if (!Output) {
    if (!Handle)
      consumeError(Handle.takeError());
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  *Output = {};
  if (!Handle) {
    consumeError(Handle.takeError());
    return mcStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *Output = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetUnitInfo(
    void *Context, NevercTaskHandle Task, NevercMCUnitHandle Unit,
    NevercMCUnitInfo *OutInfo) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(OutInfo))
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  PluginMCUnit *Resolved = nullptr;
  Status = Bridge->resolveUnit(Unit, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  OutInfo->TargetID = Resolved->targetID();
  OutInfo->TargetSchemaDigest = {
      Resolved->targetSchemaDigest().data(),
      Resolved->targetSchemaDigest().size()};
  OutInfo->Generation = Bridge->unitGeneration();
  OutInfo->SectionCount = Resolved->sectionCount();
  OutInfo->SymbolCount = Resolved->symbolCount();
  OutInfo->ExpressionCount = Resolved->expressionCount();
  OutInfo->FragmentCount = Resolved->fragmentCount();
  OutInfo->InstructionCount = Resolved->instructionCount();
  OutInfo->FixupCount = Resolved->fixupCount();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetFirstSection(
    void *Context, NevercTaskHandle Task, NevercMCUnitHandle Unit,
    NevercMCSectionHandle *OutSection) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutSection)
    return Bridge ? mcStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  *OutSection = {};
  PluginMCUnit *Resolved = nullptr;
  Status = Bridge->resolveUnit(Unit, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Resolved->sections().empty())
    return mcStatus(NEVERC_STATUS_NOT_FOUND);
  return writeHandle(Bridge->wrapSection(*Resolved->sections().front()),
                     OutSection);
}

NevercStatus NEVERC_CALL GetNextSection(
    void *Context, NevercTaskHandle Task, NevercMCSectionHandle Section,
    NevercMCSectionHandle *OutSection) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutSection)
    return Bridge ? mcStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  *OutSection = {};
  PluginMCSection *Resolved = nullptr;
  Status = Bridge->resolveSection(Section, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginMCUnit *ResolvedUnit = nullptr;
  auto Unit = Bridge->unit();
  if (!Unit) {
    consumeError(Unit.takeError());
    return mcStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Status = Bridge->resolveUnit(*Unit, &ResolvedUnit);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto It = llvm::find_if(
      ResolvedUnit->sections(),
      [Resolved](const auto &Entry) { return Entry.get() == Resolved; });
  if (It == ResolvedUnit->sections().end() ||
      ++It == ResolvedUnit->sections().end())
    return mcStatus(NEVERC_STATUS_NOT_FOUND);
  return writeHandle(Bridge->wrapSection(**It), OutSection);
}

NevercStatus NEVERC_CALL GetSectionInfo(
    void *Context, NevercTaskHandle Task, NevercMCSectionHandle Section,
    NevercMCSectionInfo *OutInfo) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(OutInfo))
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  PluginMCSection *Resolved = nullptr;
  Status = Bridge->resolveSection(Section, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  OutInfo->Name = {Resolved->Name.data(), Resolved->Name.size()};
  OutInfo->Alignment = Resolved->Alignment;
  OutInfo->Flags = Resolved->Flags;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL CreateSection(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation,
    const NevercMCSectionDescriptor *Descriptor,
    NevercMCSectionHandle *OutSection) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(Descriptor) || !validString(Descriptor->Name))
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeHandle(Bridge->createSection(Mutation, *Descriptor),
                     OutSection);
}

NevercStatus NEVERC_CALL MoveSectionBefore(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation, NevercMCSectionHandle Section,
    NevercMCSectionHandle Position) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->moveSectionBefore(Mutation, Section, Position)
                : Status;
}

NevercStatus NEVERC_CALL EraseSection(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation, NevercMCSectionHandle Section) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->eraseSection(Mutation, Section) : Status;
}

NevercStatus NEVERC_CALL GetFirstSymbol(
    void *Context, NevercTaskHandle Task, NevercMCUnitHandle Unit,
    NevercMCSymbolHandle *OutSymbol) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutSymbol)
    return Bridge ? mcStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  *OutSymbol = {};
  PluginMCUnit *Resolved = nullptr;
  Status = Bridge->resolveUnit(Unit, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Resolved->symbols().empty())
    return mcStatus(NEVERC_STATUS_NOT_FOUND);
  return writeHandle(Bridge->wrapSymbol(*Resolved->symbols().front()),
                     OutSymbol);
}

NevercStatus NEVERC_CALL GetNextSymbol(
    void *Context, NevercTaskHandle Task, NevercMCSymbolHandle Symbol,
    NevercMCSymbolHandle *OutSymbol) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutSymbol)
    return Bridge ? mcStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  *OutSymbol = {};
  PluginMCSymbol *Resolved = nullptr;
  Status = Bridge->resolveSymbol(Symbol, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginMCUnit *ResolvedUnit = nullptr;
  auto Unit = Bridge->unit();
  if (!Unit) {
    consumeError(Unit.takeError());
    return mcStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Status = Bridge->resolveUnit(*Unit, &ResolvedUnit);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto It = llvm::find_if(
      ResolvedUnit->symbols(),
      [Resolved](const auto &Entry) { return Entry.get() == Resolved; });
  if (It == ResolvedUnit->symbols().end() ||
      ++It == ResolvedUnit->symbols().end())
    return mcStatus(NEVERC_STATUS_NOT_FOUND);
  return writeHandle(Bridge->wrapSymbol(**It), OutSymbol);
}

NevercStatus NEVERC_CALL GetSymbolInfo(
    void *Context, NevercTaskHandle Task, NevercMCSymbolHandle Symbol,
    NevercMCSymbolInfo *OutInfo) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(OutInfo))
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  PluginMCSymbol *Resolved = nullptr;
  Status = Bridge->resolveSymbol(Symbol, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  OutInfo->Name = {Resolved->Name.data(), Resolved->Name.size()};
  OutInfo->Binding = Resolved->Binding;
  OutInfo->Visibility = Resolved->Visibility;
  OutInfo->Type = Resolved->Type;
  OutInfo->Definition = Resolved->Definition;
  OutInfo->Value = Resolved->Value;
  OutInfo->Size = Resolved->Size;
  OutInfo->Alignment = Resolved->Alignment;
  OutInfo->Flags = Resolved->Flags;
  OutInfo->Section = {};
  if (Resolved->Section != nullptr) {
    auto Section = Bridge->wrapSection(*Resolved->Section);
    if (!Section) {
      consumeError(Section.takeError());
      return mcStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    OutInfo->Section = *Section;
  }
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL CreateSymbol(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation,
    const NevercMCSymbolDescriptor *Descriptor,
    NevercMCSymbolHandle *OutSymbol) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(Descriptor) || !validString(Descriptor->Name))
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeHandle(Bridge->createSymbol(Mutation, *Descriptor),
                     OutSymbol);
}

NevercStatus NEVERC_CALL MoveSymbolBefore(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation, NevercMCSymbolHandle Symbol,
    NevercMCSymbolHandle Position) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->moveSymbolBefore(Mutation, Symbol, Position)
                : Status;
}

NevercStatus NEVERC_CALL EraseSymbol(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation, NevercMCSymbolHandle Symbol) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->eraseSymbol(Mutation, Symbol) : Status;
}

NevercStatus NEVERC_CALL GetExpressionInfo(
    void *Context, NevercTaskHandle Task, NevercMCExprHandle Expression,
    NevercMCExpressionInfo *OutInfo) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(OutInfo))
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  PluginMCExpression *Resolved = nullptr;
  Status = Bridge->resolveExpression(Expression, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  OutInfo->Kind = Resolved->Kind;
  OutInfo->Operator = Resolved->Operator;
  OutInfo->Constant = Resolved->Constant;
  OutInfo->TargetVariant = Resolved->TargetVariant;
  OutInfo->ExtensionOwner = Resolved->Extension.Owner;
  OutInfo->SchemaToken = {};
  if (Resolved->Kind == NEVERC_MC_EXPRESSION_TARGET_VARIANT) {
    auto SchemaToken = Bridge->schemaToken();
    if (!SchemaToken) {
      consumeError(SchemaToken.takeError());
      return mcStatus(NEVERC_STATUS_NOT_FOUND);
    }
    OutInfo->SchemaToken = *SchemaToken;
  }
  OutInfo->Extension = {Resolved->Extension.Bytes.data(),
                        Resolved->Extension.Bytes.size()};
  OutInfo->Reserved = 0;
  OutInfo->Symbol = {};
  OutInfo->Left = {};
  OutInfo->Right = {};
  if (Resolved->Symbol != nullptr) {
    auto Symbol = Bridge->wrapSymbol(*Resolved->Symbol);
    if (!Symbol) {
      consumeError(Symbol.takeError());
      return mcStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    OutInfo->Symbol = *Symbol;
  }
  if (Resolved->Left != nullptr) {
    auto Left = Bridge->wrapExpression(*Resolved->Left);
    if (!Left) {
      consumeError(Left.takeError());
      return mcStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    OutInfo->Left = *Left;
  }
  if (Resolved->Right != nullptr) {
    auto Right = Bridge->wrapExpression(*Resolved->Right);
    if (!Right) {
      consumeError(Right.takeError());
      return mcStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    OutInfo->Right = *Right;
  }
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL CreateExpression(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation,
    const NevercMCExpressionDescriptor *Descriptor,
    NevercMCExprHandle *OutExpression) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(Descriptor) || !validBytes(Descriptor->Extension))
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeHandle(Bridge->createExpression(Mutation, *Descriptor),
                     OutExpression);
}

NevercStatus NEVERC_CALL SetExpressionOperands(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation, NevercMCExprHandle Expression,
    NevercMCExprHandle Left, NevercMCExprHandle Right) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->setExpressionOperands(Mutation, Expression,
                                                 Left, Right)
                : Status;
}

NevercStatus NEVERC_CALL EraseExpression(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation, NevercMCExprHandle Expression) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->eraseExpression(Mutation, Expression) : Status;
}

NevercStatus NEVERC_CALL GetFirstFragment(
    void *Context, NevercTaskHandle Task, NevercMCSectionHandle Section,
    NevercMCFragmentHandle *OutFragment) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutFragment)
    return Bridge ? mcStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  *OutFragment = {};
  PluginMCSection *Resolved = nullptr;
  Status = Bridge->resolveSection(Section, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Resolved->Fragments.empty())
    return mcStatus(NEVERC_STATUS_NOT_FOUND);
  return writeHandle(Bridge->wrapFragment(*Resolved->Fragments.front()),
                     OutFragment);
}

NevercStatus NEVERC_CALL GetNextFragment(
    void *Context, NevercTaskHandle Task, NevercMCFragmentHandle Fragment,
    NevercMCFragmentHandle *OutFragment) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutFragment)
    return Bridge ? mcStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  *OutFragment = {};
  PluginMCFragment *Resolved = nullptr;
  Status = Bridge->resolveFragment(Fragment, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto It = llvm::find_if(
      Resolved->Parent->Fragments,
      [Resolved](const auto &Entry) { return Entry.get() == Resolved; });
  if (It == Resolved->Parent->Fragments.end() ||
      ++It == Resolved->Parent->Fragments.end())
    return mcStatus(NEVERC_STATUS_NOT_FOUND);
  return writeHandle(Bridge->wrapFragment(**It), OutFragment);
}

NevercStatus NEVERC_CALL GetFragmentInfo(
    void *Context, NevercTaskHandle Task, NevercMCFragmentHandle Fragment,
    NevercMCFragmentInfo *OutInfo) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(OutInfo))
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  PluginMCFragment *Resolved = nullptr;
  Status = Bridge->resolveFragment(Fragment, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto Section = Bridge->wrapSection(*Resolved->Parent);
  if (!Section) {
    consumeError(Section.takeError());
    return mcStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  OutInfo->Section = *Section;
  OutInfo->Kind = Resolved->Kind;
  OutInfo->FillValue = Resolved->FillValue;
  OutInfo->SchemaToken = {};
  if (Resolved->Kind == NEVERC_MC_FRAGMENT_FORMAT_EXTENSION) {
    auto SchemaToken = Bridge->schemaToken();
    if (!SchemaToken) {
      consumeError(SchemaToken.takeError());
      return mcStatus(NEVERC_STATUS_NOT_FOUND);
    }
    OutInfo->SchemaToken = *SchemaToken;
  }
  OutInfo->ExplicitOffset = Resolved->ExplicitOffset;
  OutInfo->Alignment = Resolved->Alignment;
  OutInfo->Contents = {Resolved->Contents.data(),
                       Resolved->Contents.size()};
  OutInfo->ExtensionOwner = Resolved->Extension.Owner;
  OutInfo->Extension = {Resolved->Extension.Bytes.data(),
                        Resolved->Extension.Bytes.size()};
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL CreateFragment(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation, NevercMCSectionHandle Section,
    const NevercMCFragmentDescriptor *Descriptor,
    NevercMCFragmentHandle *OutFragment) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(Descriptor) || !validBytes(Descriptor->Contents) ||
      !validBytes(Descriptor->Extension))
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeHandle(
      Bridge->createFragment(Mutation, Section, *Descriptor),
      OutFragment);
}

NevercStatus NEVERC_CALL MoveFragmentBefore(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation, NevercMCFragmentHandle Fragment,
    NevercMCFragmentHandle Position) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->moveFragmentBefore(Mutation, Fragment,
                                              Position)
                : Status;
}

NevercStatus NEVERC_CALL EraseFragment(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation, NevercMCFragmentHandle Fragment) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->eraseFragment(Mutation, Fragment) : Status;
}

NevercStatus NEVERC_CALL GetFirstFragmentInstruction(
    void *Context, NevercTaskHandle Task, NevercMCFragmentHandle Fragment,
    NevercMCInstHandle *OutInstruction) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutInstruction)
    return Bridge ? mcStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  *OutInstruction = {};
  PluginMCFragment *Resolved = nullptr;
  Status = Bridge->resolveFragment(Fragment, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Resolved->Instructions.empty())
    return mcStatus(NEVERC_STATUS_NOT_FOUND);
  return writeHandle(
      Bridge->wrapInstruction(*Resolved->Instructions.front()),
      OutInstruction);
}

NevercStatus NEVERC_CALL GetFirstFixup(
    void *Context, NevercTaskHandle Task, NevercMCFragmentHandle Fragment,
    NevercMCFixupHandle *OutFixup) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutFixup)
    return Bridge ? mcStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  *OutFixup = {};
  PluginMCFragment *Resolved = nullptr;
  Status = Bridge->resolveFragment(Fragment, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Resolved->Fixups.empty())
    return mcStatus(NEVERC_STATUS_NOT_FOUND);
  return writeHandle(Bridge->wrapFixup(*Resolved->Fixups.front()),
                     OutFixup);
}

NevercStatus NEVERC_CALL GetNextFixup(
    void *Context, NevercTaskHandle Task, NevercMCFixupHandle Fixup,
    NevercMCFixupHandle *OutFixup) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutFixup)
    return Bridge ? mcStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  *OutFixup = {};
  PluginMCFixup *Resolved = nullptr;
  Status = Bridge->resolveFixup(Fixup, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto It = llvm::find_if(
      Resolved->Parent->Fixups,
      [Resolved](const auto &Entry) { return Entry.get() == Resolved; });
  if (It == Resolved->Parent->Fixups.end() ||
      ++It == Resolved->Parent->Fixups.end())
    return mcStatus(NEVERC_STATUS_NOT_FOUND);
  return writeHandle(Bridge->wrapFixup(**It), OutFixup);
}

NevercStatus NEVERC_CALL GetFixupInfo(
    void *Context, NevercTaskHandle Task, NevercMCFixupHandle Fixup,
    NevercMCFixupInfo *OutInfo) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(OutInfo))
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  PluginMCFixup *Resolved = nullptr;
  Status = Bridge->resolveFixup(Fixup, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto Fragment = Bridge->wrapFragment(*Resolved->Parent);
  auto Expression = Bridge->wrapExpression(*Resolved->Expression);
  if (!Fragment || !Expression) {
    if (!Fragment)
      consumeError(Fragment.takeError());
    if (!Expression)
      consumeError(Expression.takeError());
    return mcStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  OutInfo->Fragment = *Fragment;
  OutInfo->Expression = *Expression;
  OutInfo->Offset = Resolved->Offset;
  OutInfo->Width = Resolved->Width;
  OutInfo->IsPCRelative = Resolved->IsPCRelative;
  OutInfo->IsSigned = Resolved->IsSigned;
  OutInfo->MayRelax = Resolved->MayRelax;
  OutInfo->Kind = Resolved->Kind;
  OutInfo->TargetKind = Resolved->TargetKind;
  OutInfo->Reserved8 = 0;
  OutInfo->Reserved = 0;
  OutInfo->SchemaToken = {};
  if (Resolved->Kind == NEVERC_MC_FIXUP_TARGET_EXTENSION) {
    auto SchemaToken = Bridge->schemaToken();
    if (!SchemaToken) {
      consumeError(SchemaToken.takeError());
      return mcStatus(NEVERC_STATUS_NOT_FOUND);
    }
    OutInfo->SchemaToken = *SchemaToken;
  }
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL CreateFixup(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation, NevercMCFragmentHandle Fragment,
    const NevercMCFixupDescriptor *Descriptor,
    NevercMCFixupHandle *OutFixup) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(Descriptor))
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeHandle(
      Bridge->createFixup(Mutation, Fragment, *Descriptor), OutFixup);
}

NevercStatus NEVERC_CALL MoveFixupBefore(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation, NevercMCFixupHandle Fixup,
    NevercMCFixupHandle Position) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->moveFixupBefore(Mutation, Fixup, Position)
                : Status;
}

NevercStatus NEVERC_CALL EraseFixup(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation, NevercMCFixupHandle Fixup) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->eraseFixup(Mutation, Fixup) : Status;
}

NevercStatus NEVERC_CALL InsertOperand(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation, NevercMCInstHandle Instruction,
    uint64_t Index, const NevercMCOperandValue *Value,
    NevercMCOperandHandle *OutOperand) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(Value))
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  if (OutOperand)
    *OutOperand = {};
  return Bridge->insertOperand(Mutation, Instruction, Index, *Value,
                               OutOperand);
}

NevercStatus NEVERC_CALL EraseOperand(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation, NevercMCInstHandle Instruction,
    uint64_t Index) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->eraseOperand(Mutation, Instruction, Index)
                : Status;
}

NevercStatus NEVERC_CALL AppendInstructionToFragment(
    void *Context, NevercTaskHandle Task,
    NevercMCMutationHandle Mutation, NevercMCFragmentHandle Fragment,
    NevercMCInstHandle Instruction) {
  NevercStatus Status;
  MCPluginBridge *Bridge = bridge(Context, Task, Status);
  return Bridge ? Bridge->appendInstructionToFragment(
                      Mutation, Fragment, Instruction)
                : Status;
}

} // namespace

void initializeMCBuilderAPI(NevercMCAPI &API, MCPluginBridge &Bridge) {
  API.Context = &Bridge;
  API.GetUnitInfo = GetUnitInfo;
  API.GetFirstSection = GetFirstSection;
  API.GetNextSection = GetNextSection;
  API.GetSectionInfo = GetSectionInfo;
  API.CreateSection = CreateSection;
  API.MoveSectionBefore = MoveSectionBefore;
  API.EraseSection = EraseSection;
  API.GetFirstSymbol = GetFirstSymbol;
  API.GetNextSymbol = GetNextSymbol;
  API.GetSymbolInfo = GetSymbolInfo;
  API.CreateSymbol = CreateSymbol;
  API.MoveSymbolBefore = MoveSymbolBefore;
  API.EraseSymbol = EraseSymbol;
  API.GetFirstFragment = GetFirstFragment;
  API.GetNextFragment = GetNextFragment;
  API.GetFragmentInfo = GetFragmentInfo;
  API.CreateFragment = CreateFragment;
  API.MoveFragmentBefore = MoveFragmentBefore;
  API.EraseFragment = EraseFragment;
  API.GetFirstFragmentInstruction = GetFirstFragmentInstruction;
  API.GetFirstFixup = GetFirstFixup;
  API.GetNextFixup = GetNextFixup;
  API.GetFixupInfo = GetFixupInfo;
  API.CreateFixup = CreateFixup;
  API.MoveFixupBefore = MoveFixupBefore;
  API.EraseFixup = EraseFixup;
  API.InsertOperand = InsertOperand;
  API.EraseOperand = EraseOperand;
  API.AppendInstructionToFragment = AppendInstructionToFragment;
}

void initializeMCExpressionAPI(NevercMCAPI &API,
                               MCPluginBridge &Bridge) {
  API.Context = &Bridge;
  API.GetExpressionInfo = GetExpressionInfo;
  API.CreateExpression = CreateExpression;
  API.SetExpressionOperands = SetExpressionOperands;
  API.EraseExpression = EraseExpression;
}

} // namespace neverc::plugin
