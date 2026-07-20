#include "neverc/Plugin/Host/MCLayoutEngine.h"
#include "neverc/Plugin/Host/MCUnit.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

constexpr uint32_t HardMaximumLayoutIterations = 64;
constexpr uint64_t MaximumFragmentSize = UINT64_C(16) * 1024 * 1024;

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

bool nonzero(NevercInterfaceID ID) {
  return ID.High != 0 || ID.Low != 0;
}

bool validHeader(const NevercABITableHeader &Header, size_t Required) {
  return Header.StructSize >= Required &&
         Header.Major == NEVERC_MC_API_MAJOR &&
         Header.Minor <= NEVERC_MC_API_MINOR && Header.Flags == 0;
}

bool validBool(NevercBool Value) {
  return Value == NEVERC_FALSE || Value == NEVERC_TRUE;
}

bool powerOfTwo(uint64_t Value) {
  return Value != 0 && (Value & (Value - 1)) == 0;
}

Error layoutError(const Twine &Message) {
  return createStringError(errc::invalid_argument, Message);
}

Error callbackError(StringRef Name, NevercStatus Status) {
  return layoutError(Name + " callback failed with status " +
                     Twine(static_cast<uint32_t>(Status.Code)));
}

Expected<uint64_t> alignOffset(uint64_t Value, uint64_t Alignment) {
  if (!powerOfTwo(Alignment))
    return layoutError("MC layout alignment is not a power of two");
  const uint64_t Mask = Alignment - 1;
  if (Value > std::numeric_limits<uint64_t>::max() - Mask)
    return layoutError("MC layout alignment overflows");
  return (Value + Mask) & ~Mask;
}

struct EvaluatedExpression {
  bool Resolved = true;
  int64_t Value = 0;
  PluginMCSymbol *Symbol = nullptr;
  int64_t Addend = 0;
};

Expected<EvaluatedExpression>
evaluateExpression(PluginMCExpression *Expression,
                   const PluginMCSection *CurrentSection,
                   std::set<PluginMCExpression *> &Visiting) {
  if (!Expression)
    return layoutError("MC fixup has no expression");
  if (!Visiting.insert(Expression).second)
    return layoutError("MC fixup expression contains a cycle");
  auto Leave = make_scope_exit([&] { Visiting.erase(Expression); });

  switch (Expression->Kind) {
  case NEVERC_MC_EXPRESSION_CONSTANT:
    return EvaluatedExpression{true, Expression->Constant, nullptr, 0};
  case NEVERC_MC_EXPRESSION_SYMBOL_REF: {
    PluginMCSymbol *Symbol = Expression->Symbol;
    if (!Symbol)
      return layoutError("MC symbol expression has no symbol");
    if (Symbol->Definition == NEVERC_MC_SYMBOL_DEFINITION_ABSOLUTE ||
        (Symbol->Definition == NEVERC_MC_SYMBOL_DEFINITION_SECTION &&
         Symbol->Section == CurrentSection)) {
      if (Symbol->Value >
          static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        return layoutError("MC symbol value exceeds signed layout range");
      return EvaluatedExpression{
          true, static_cast<int64_t>(Symbol->Value), nullptr, 0};
    }
    return EvaluatedExpression{false, 0, Symbol, 0};
  }
  case NEVERC_MC_EXPRESSION_UNARY: {
    auto Operand =
        evaluateExpression(Expression->Left, CurrentSection, Visiting);
    if (!Operand)
      return Operand.takeError();
    if (!Operand->Resolved &&
        Expression->Operator != NEVERC_MC_UNARY_PLUS)
      return layoutError(
          "unresolved MC expression uses a non-additive unary operator");
    switch (Expression->Operator) {
    case NEVERC_MC_UNARY_PLUS:
      return std::move(*Operand);
    case NEVERC_MC_UNARY_MINUS:
      if (Operand->Value == std::numeric_limits<int64_t>::min())
        return layoutError("MC expression negation overflows");
      Operand->Value = -Operand->Value;
      return std::move(*Operand);
    case NEVERC_MC_UNARY_NOT:
      Operand->Value = ~Operand->Value;
      return std::move(*Operand);
    default:
      return layoutError("MC expression has an unknown unary operator");
    }
  }
  case NEVERC_MC_EXPRESSION_BINARY: {
    auto Left =
        evaluateExpression(Expression->Left, CurrentSection, Visiting);
    if (!Left)
      return Left.takeError();
    auto Right =
        evaluateExpression(Expression->Right, CurrentSection, Visiting);
    if (!Right)
      return Right.takeError();

    if (!Left->Resolved || !Right->Resolved) {
      if (!Left->Resolved && !Right->Resolved)
        return layoutError(
            "MC relocation expression references multiple unresolved symbols");
      EvaluatedExpression Result =
          Left->Resolved ? std::move(*Right) : std::move(*Left);
      const int64_t Constant =
          Left->Resolved ? Left->Value : Right->Value;
      int64_t Addend = 0;
      if (Expression->Operator == NEVERC_MC_BINARY_ADD) {
        if (AddOverflow(Result.Addend, Constant, Addend))
          return layoutError("MC relocation addend overflows");
      } else if (Expression->Operator == NEVERC_MC_BINARY_SUBTRACT &&
                 !Left->Resolved) {
        if (SubOverflow(Result.Addend, Constant, Addend))
          return layoutError("MC relocation addend overflows");
      } else {
        return layoutError(
            "unresolved MC expression uses an unsupported binary operator");
      }
      Result.Addend = Addend;
      return Result;
    }

    int64_t Value = 0;
    switch (Expression->Operator) {
    case NEVERC_MC_BINARY_ADD:
      if (AddOverflow(Left->Value, Right->Value, Value))
        return layoutError("MC expression addition overflows");
      break;
    case NEVERC_MC_BINARY_SUBTRACT:
      if (SubOverflow(Left->Value, Right->Value, Value))
        return layoutError("MC expression subtraction overflows");
      break;
    case NEVERC_MC_BINARY_MULTIPLY:
      if (MulOverflow(Left->Value, Right->Value, Value))
        return layoutError("MC expression multiplication overflows");
      break;
    case NEVERC_MC_BINARY_DIVIDE:
      if (Right->Value == 0 ||
          (Left->Value == std::numeric_limits<int64_t>::min() &&
           Right->Value == -1))
        return layoutError("MC expression division is invalid");
      Value = Left->Value / Right->Value;
      break;
    case NEVERC_MC_BINARY_AND:
      Value = Left->Value & Right->Value;
      break;
    case NEVERC_MC_BINARY_OR:
      Value = Left->Value | Right->Value;
      break;
    case NEVERC_MC_BINARY_XOR:
      Value = Left->Value ^ Right->Value;
      break;
    case NEVERC_MC_BINARY_SHIFT_LEFT:
    case NEVERC_MC_BINARY_SHIFT_RIGHT:
      if (Right->Value < 0 || Right->Value >= 64)
        return layoutError("MC expression shift count is invalid");
      Value = Expression->Operator == NEVERC_MC_BINARY_SHIFT_LEFT
                  ? static_cast<int64_t>(
                        static_cast<uint64_t>(Left->Value)
                        << Right->Value)
                  : Left->Value >> Right->Value;
      break;
    default:
      return layoutError("MC expression has an unknown binary operator");
    }
    return EvaluatedExpression{true, Value, nullptr, 0};
  }
  case NEVERC_MC_EXPRESSION_TARGET_VARIANT:
    if (!Expression->Left)
      return layoutError(
          "target MC expression has no portable base expression");
    return evaluateExpression(Expression->Left, CurrentSection, Visiting);
  default:
    return layoutError("MC expression has an unknown kind");
  }
}

Expected<EvaluatedExpression>
evaluateExpression(PluginMCExpression *Expression,
                   const PluginMCSection *CurrentSection) {
  std::set<PluginMCExpression *> Visiting;
  return evaluateExpression(Expression, CurrentSection, Visiting);
}

bool fitsValue(int64_t Value, uint32_t Width, bool Signed) {
  if (Width == 0 || Width > 64)
    return false;
  if (Signed) {
    if (Width == 64)
      return true;
    const int64_t Minimum = -(INT64_C(1) << (Width - 1));
    const int64_t Maximum = (INT64_C(1) << (Width - 1)) - 1;
    return Value >= Minimum && Value <= Maximum;
  }
  if (Value < 0)
    return false;
  if (Width == 64)
    return true;
  return static_cast<uint64_t>(Value) <
         (UINT64_C(1) << Width);
}

struct LayoutPositions {
  std::unordered_map<const PluginMCFragment *, uint64_t> FragmentOffsets;
  std::unordered_map<const PluginMCSection *, uint64_t> SectionSizes;
};

Expected<LayoutPositions>
computePositions(const PluginMCUnit &Unit,
                 uint32_t MinimumInstructionAlignment) {
  LayoutPositions Positions;
  for (const auto &SectionStorage : Unit.sections()) {
    const PluginMCSection &Section = *SectionStorage;
    if (!powerOfTwo(Section.Alignment))
      return layoutError("MC section alignment is invalid");
    uint64_t Offset = 0;
    for (const auto &FragmentStorage : Section.Fragments) {
      const PluginMCFragment &Fragment = *FragmentStorage;
      if (Fragment.Parent != &Section)
        return layoutError("MC fragment has a foreign parent section");
      if (!powerOfTwo(Fragment.Alignment))
        return layoutError("MC fragment alignment is invalid");
      if (!Fragment.Instructions.empty() &&
          Fragment.Alignment < MinimumInstructionAlignment)
        return layoutError(
            "MC instruction fragment violates minimum alignment");
      auto Aligned = alignOffset(Offset, Fragment.Alignment);
      if (!Aligned)
        return Aligned.takeError();
      uint64_t FragmentOffset = *Aligned;
      if (Fragment.ExplicitOffset != NEVERC_MC_AUTOMATIC_OFFSET) {
        if (Fragment.ExplicitOffset < FragmentOffset)
          return layoutError(
              "MC fragment explicit offset overlaps prior data");
        FragmentOffset = Fragment.ExplicitOffset;
      }
      if (Fragment.Contents.size() > MaximumFragmentSize ||
          FragmentOffset >
              std::numeric_limits<uint64_t>::max() -
                  Fragment.Contents.size())
        return layoutError("MC fragment size overflows layout");
      Positions.FragmentOffsets[&Fragment] = FragmentOffset;
      Offset = FragmentOffset + Fragment.Contents.size();
    }
    Positions.SectionSizes[&Section] = Offset;
  }
  return Positions;
}

struct FixupContext {
  NevercMCFixupKindInfo Info{};
  NevercMCLayoutFixupRequest Request{};
  EvaluatedExpression Evaluation;
};

Expected<NevercMCFixupKindInfo>
queryFixupKindInfo(
    const MCLayoutBackendRegistry::BackendRecord &Backend,
    NevercMCFixupKind Kind, uint32_t TargetKind, uint32_t Width,
    uint64_t FragmentSize) {
  NevercMCFixupKindInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_MC_API_MAJOR,
                 NEVERC_MC_API_MINOR, 0};
  NevercStatus Status;
  try {
    Status = Backend.GetFixupKindInfo(
        Backend.UserData, Kind, TargetKind, &Info);
  } catch (...) {
    return layoutError("MC fixup-info callback raised an exception");
  }
  if (Status.Code != NEVERC_STATUS_OK)
    return callbackError("MC fixup-info", Status);
  constexpr NevercMCFixupInfoFlags KnownFlags =
      NEVERC_MC_FIXUP_INFO_PC_RELATIVE |
      NEVERC_MC_FIXUP_INFO_SIGNED |
      NEVERC_MC_FIXUP_INFO_RELAXABLE |
      NEVERC_MC_FIXUP_INFO_TARGET;
  if (!validHeader(Info.Header, sizeof(Info)) ||
      Info.Reserved != 0 ||
      (Info.Flags & ~KnownFlags) != 0 ||
      Info.TargetSize != Width ||
      Info.TargetOffset + Info.TargetSize > FragmentSize * 8)
    return layoutError("MC backend returned inconsistent fixup info");
  return Info;
}

Expected<FixupContext>
makeFixupContext(
    PluginTaskContext &Task, PluginMCSection &Section,
    PluginMCFragment &Fragment, PluginMCFixup &Fixup,
    uint64_t FragmentOffset,
    const MCLayoutBackendRegistry::BackendRecord &Backend) {
  if (Fixup.Parent != &Fragment)
    return layoutError("MC fixup has a foreign parent fragment");
  if (Fixup.Width == 0 || Fixup.Width > 64 ||
      Fixup.Width % 8 != 0)
    return layoutError("MC fixup width is invalid");

  FixupContext Context;
  auto Info = queryFixupKindInfo(
      Backend, Fixup.Kind, Fixup.TargetKind, Fixup.Width,
      Fragment.Contents.size());
  if (!Info)
    return Info.takeError();
  Context.Info = *Info;
  if ((((Context.Info.Flags & NEVERC_MC_FIXUP_INFO_PC_RELATIVE) != 0) !=
       Fixup.IsPCRelative) ||
      (((Context.Info.Flags & NEVERC_MC_FIXUP_INFO_SIGNED) != 0) !=
       Fixup.IsSigned) ||
      (Fixup.MayRelax &&
       (Context.Info.Flags & NEVERC_MC_FIXUP_INFO_RELAXABLE) == 0))
    return layoutError("MC backend returned inconsistent fixup info");

  auto Evaluation = evaluateExpression(Fixup.Expression, &Section);
  if (!Evaluation)
    return Evaluation.takeError();
  Context.Evaluation = std::move(*Evaluation);

  Context.Request.Header = {
      sizeof(Context.Request), NEVERC_MC_API_MAJOR, NEVERC_MC_API_MINOR, 0};
  Context.Request.Task = Task.handle();
  Context.Request.Kind = Fixup.Kind;
  Context.Request.TargetKind = Fixup.TargetKind;
  Context.Request.FixupOffset = Fixup.Offset;
  Context.Request.FragmentOffset = FragmentOffset;
  Context.Request.FragmentSize = Fragment.Contents.size();
  if (FragmentOffset >
      std::numeric_limits<uint64_t>::max() - Fixup.Offset)
    return layoutError("MC fixup place overflows");
  Context.Request.Place = FragmentOffset + Fixup.Offset;
  Context.Request.Width = Fixup.Width;
  Context.Request.IsPCRelative =
      Fixup.IsPCRelative ? NEVERC_TRUE : NEVERC_FALSE;
  Context.Request.IsSigned = Fixup.IsSigned ? NEVERC_TRUE : NEVERC_FALSE;
  Context.Request.MayRelax = Fixup.MayRelax ? NEVERC_TRUE : NEVERC_FALSE;
  Context.Request.IsResolved =
      Context.Evaluation.Resolved ? NEVERC_TRUE : NEVERC_FALSE;
  Context.Request.Value =
      Context.Evaluation.Resolved ? Context.Evaluation.Value
                                  : Context.Evaluation.Addend;
  if (Context.Evaluation.Resolved && Fixup.IsPCRelative) {
    if (Context.Request.Place >
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
      return layoutError("MC PC-relative fixup place exceeds signed range");
    int64_t Relative = 0;
    if (SubOverflow(Context.Request.Value,
                    static_cast<int64_t>(Context.Request.Place), Relative))
      return layoutError("MC PC-relative fixup value overflows");
    Context.Request.Value = Relative;
  }
  if (Context.Evaluation.Symbol)
    Context.Request.SymbolName = {
        Context.Evaluation.Symbol->Name.data(),
        Context.Evaluation.Symbol->Name.size()};
  return Context;
}

Error verifyFixupRanges(PluginMCFragment &Fragment) {
  struct Range {
    uint64_t Begin;
    uint64_t End;
  };
  std::vector<Range> Ranges;
  Ranges.reserve(Fragment.Fixups.size());
  for (const auto &FixupStorage : Fragment.Fixups) {
    const PluginMCFixup &Fixup = *FixupStorage;
    const uint64_t Bytes = (Fixup.Width + 7) / 8;
    if (Fixup.Offset > Fragment.Contents.size() ||
        Bytes > Fragment.Contents.size() - Fixup.Offset)
      return layoutError("MC fixup is outside fragment bytes");
    Ranges.push_back({Fixup.Offset, Fixup.Offset + Bytes});
  }
  llvm::sort(Ranges, [](const Range &Left, const Range &Right) {
    return std::tie(Left.Begin, Left.End) <
           std::tie(Right.Begin, Right.End);
  });
  for (size_t I = 1; I < Ranges.size(); ++I)
    if (Ranges[I].Begin < Ranges[I - 1].End)
      return layoutError("MC fragment contains overlapping fixups");
  return Error::success();
}

struct NopSinkState {
  uint64_t Expected = 0;
  std::vector<uint8_t> Bytes;
  bool Overflow = false;
};

NevercStatus sinkStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

NevercStatus NEVERC_CALL writeNopBytes(void *Context,
                                       NevercByteView Bytes) {
  auto *State = static_cast<NopSinkState *>(Context);
  if (!State || (!Bytes.Data && Bytes.Length != 0) ||
      Bytes.Length > State->Expected - State->Bytes.size()) {
    if (State)
      State->Overflow = true;
    return sinkStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  if (Bytes.Length == 0)
    return neverc_status_ok();
  try {
    State->Bytes.insert(State->Bytes.end(), Bytes.Data,
                        Bytes.Data + Bytes.Length);
  } catch (...) {
    return sinkStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  return neverc_status_ok();
}

Expected<std::vector<uint8_t>>
generateNops(const MCLayoutBackendRegistry::BackendRecord &Backend,
             uint64_t Count) {
  NopSinkState State;
  State.Expected = Count;
  NevercMCByteSink Sink{};
  Sink.Header = {sizeof(Sink), NEVERC_MC_API_MAJOR,
                 NEVERC_MC_API_MINOR, 0};
  Sink.Context = &State;
  Sink.WriteBytes = writeNopBytes;
  NevercStatus Status;
  try {
    Status = Backend.WriteNops(Backend.UserData, Count, &Sink);
  } catch (...) {
    return layoutError("MC nop callback raised an exception");
  }
  if (Status.Code != NEVERC_STATUS_OK)
    return callbackError("MC nop", Status);
  if (State.Overflow || State.Bytes.size() != Count)
    return layoutError("MC nop callback wrote an invalid byte count");
  return std::move(State.Bytes);
}

std::string digestResult(const MCLayoutResult &Result) {
  SmallString<0> Canonical;
  raw_svector_ostream Stream(Canonical);
  for (const MCLayoutSection &Section : Result.Sections) {
    Stream << Section.Name.size() << ':' << Section.Name << ':'
           << Section.Alignment << ':' << Section.Bytes.size() << ':';
    Stream.write(
        reinterpret_cast<const char *>(Section.Bytes.data()),
        Section.Bytes.size());
    Stream << ';';
  }
  for (const MCLayoutRelocation &Relocation : Result.Relocations)
    Stream << Relocation.SectionName.size() << ':'
           << Relocation.SectionName << ':' << Relocation.Offset << ':'
           << Relocation.Width << ':' << Relocation.RelocationKind << ':'
           << Relocation.SymbolName.size() << ':'
           << Relocation.SymbolName << ':' << Relocation.Addend << ':'
           << Relocation.IsPCRelative << ':' << Relocation.IsSigned << ';';
  const std::array<uint8_t, 32> Hash = SHA256::hash(ArrayRef<uint8_t>(
      reinterpret_cast<const uint8_t *>(Canonical.data()),
      Canonical.size()));
  static constexpr char Digits[] = "0123456789abcdef";
  std::string Hex(64, '0');
  for (size_t I = 0; I != Hash.size(); ++I) {
    Hex[I * 2] = Digits[Hash[I] >> 4];
    Hex[I * 2 + 1] = Digits[Hash[I] & 0xf];
  }
  return Hex;
}

} // namespace

Expected<std::shared_ptr<const MCLayoutBackendRegistry>>
MCLayoutBackendRegistry::freeze(
    ArrayRef<MCLayoutBackendRegistrationView> Registrations,
    const PluginTargetSnapshot &Targets) {
  auto Registry = std::make_shared<MCLayoutBackendRegistry>();
  for (const MCLayoutBackendRegistrationView &Registration :
       Registrations) {
    if (Registration.PluginID.empty())
      return layoutError("MC layout backend registration has no plugin ID");
    for (const NevercMCAsmBackendDescriptor &Descriptor :
         Registration.Backends) {
      constexpr size_t Required =
          offsetof(NevercMCAsmBackendDescriptor, DestroyUserData) +
          sizeof(NevercMCAsmBackendDescriptor::DestroyUserData);
      const bool ValidAlignment =
          powerOfTwo(Descriptor.MinimumInstructionAlignment) &&
          Descriptor.MinimumInstructionAlignment <= 4096;
      if (!validHeader(Descriptor.Header, Required) ||
          !nonzero(Descriptor.ProviderID) ||
          !nonzero(Descriptor.TargetID) ||
          !nonzero(Descriptor.SchemaID) ||
          Descriptor.MaximumLayoutIterations == 0 ||
          Descriptor.MaximumLayoutIterations >
              HardMaximumLayoutIterations ||
          !ValidAlignment || Descriptor.Flags != 0 ||
          !Descriptor.GetFixupKindInfo || !Descriptor.MapRelocation ||
          !Descriptor.ShouldRelaxFixup || !Descriptor.RelaxFragment ||
          !Descriptor.ApplyFixup || !Descriptor.WriteNops)
        return layoutError("plugin '" + Registration.PluginID +
                           "' has an invalid MC layout backend descriptor");
      const auto *Target = Targets.findTarget(Descriptor.TargetID);
      const auto *Schema = Targets.findMCSchema(Descriptor.SchemaID);
      if (!Target)
        return layoutError("MC layout backend references an unknown Target");
      if (!Schema || !sameID(Schema->TargetID, Descriptor.TargetID) ||
          !sameID(Target->MCSchemaID, Descriptor.SchemaID))
        return layoutError(
            "MC layout backend references an unknown Target schema");
      for (const BackendRecord &Existing : Registry->Backends) {
        if (sameID(Existing.ProviderID, Descriptor.ProviderID))
          return layoutError("duplicate MC layout backend provider ID");
        if (sameID(Existing.TargetID, Descriptor.TargetID) &&
            sameID(Existing.SchemaID, Descriptor.SchemaID))
          return layoutError(
              "multiple MC layout backends registered for one Target/schema");
      }
      Registry->Backends.push_back(
          {Registration.PluginID.str(),
           Registration.Owner,
           Descriptor.ProviderID,
           Descriptor.TargetID,
           Descriptor.SchemaID,
           Descriptor.MaximumLayoutIterations,
           Descriptor.MinimumInstructionAlignment,
           Descriptor.GetFixupKindInfo,
           Descriptor.MapRelocation,
           Descriptor.ShouldRelaxFixup,
           Descriptor.RelaxFragment,
           Descriptor.ApplyFixup,
           Descriptor.WriteNops,
           Descriptor.UserData});
    }
  }
  return std::shared_ptr<const MCLayoutBackendRegistry>(
      std::move(Registry));
}

Expected<std::shared_ptr<const MCLayoutBackendRegistry>>
MCLayoutBackendRegistry::freeze(
    ArrayRef<std::shared_ptr<const PluginModule>> Modules,
    const PluginTargetSnapshot &Targets) {
  struct MaterializedRegistration {
    std::string PluginID;
    std::shared_ptr<const PluginModule> Owner;
    std::vector<NevercMCAsmBackendDescriptor> Backends;
  };
  std::vector<MaterializedRegistration> Materialized;
  Materialized.reserve(Modules.size());
  for (const std::shared_ptr<const PluginModule> &Module : Modules) {
    const PluginPublishedRegistration *Published = Module->registration();
    if (!Published)
      continue;
    MaterializedRegistration Registration;
    Registration.PluginID = Module->descriptor().PluginID;
    Registration.Owner = Module;
    for (const PluginRegistrationRecord &Record : Published->records())
      if (Record.Kind == PluginRegistrationKind::MCAsmBackend)
        Registration.Backends.push_back(Record.MCAsmBackend);
    Materialized.push_back(std::move(Registration));
  }
  std::vector<MCLayoutBackendRegistrationView> Views;
  Views.reserve(Materialized.size());
  for (const MaterializedRegistration &Registration : Materialized)
    Views.push_back(
        {Registration.PluginID, Registration.Owner,
         Registration.Backends});
  return freeze(Views, Targets);
}

const MCLayoutBackendRegistry::BackendRecord *
MCLayoutBackendRegistry::findBackend(
    NevercTargetID Target, NevercInterfaceID Schema) const {
  for (const BackendRecord &Backend : Backends)
    if (sameID(Backend.TargetID, Target) &&
        sameID(Backend.SchemaID, Schema))
      return &Backend;
  return nullptr;
}

struct MCLayoutEngine::Impl {
  std::shared_ptr<const MCLayoutBackendRegistry> Registry;
  std::shared_ptr<const PluginTargetSnapshot> Targets;
  MCLayoutBackendRegistry::BackendRecord Backend;
  const PluginTargetSnapshot::TargetRecord *Target = nullptr;
  const PluginTargetSnapshot::NamedRecord *Schema = nullptr;
};

MCLayoutEngine::MCLayoutEngine(std::unique_ptr<Impl> StateValue)
    : State(std::move(StateValue)) {}

MCLayoutEngine::~MCLayoutEngine() = default;

Expected<std::unique_ptr<MCLayoutEngine>>
MCLayoutEngine::create(
    std::shared_ptr<const MCLayoutBackendRegistry> Registry,
    std::shared_ptr<const PluginTargetSnapshot> Targets,
    NevercTargetID TargetID) {
  if (!Registry || !Targets)
    return layoutError(
        "MC layout engine requires frozen backend and Target registries");
  const auto *Target = Targets->findTarget(TargetID);
  if (!Target)
    return layoutError("MC layout Target is not registered");
  const auto *Schema = Targets->findMCSchema(Target->MCSchemaID);
  if (!Schema)
    return layoutError("MC layout Target schema is not registered");
  const auto *Backend =
      Registry->findBackend(TargetID, Target->MCSchemaID);
  if (!Backend)
    return layoutError("MC layout backend is not registered");
  auto State = std::make_unique<Impl>();
  State->Registry = std::move(Registry);
  State->Targets = std::move(Targets);
  State->Backend = *Backend;
  State->Target = Target;
  State->Schema = Schema;
  return std::unique_ptr<MCLayoutEngine>(
      new MCLayoutEngine(std::move(State)));
}

Expected<MCLayoutResult>
MCLayoutEngine::layout(
    PluginTaskContext &Task, PluginMCUnit &Unit,
    const MCLayoutOptions &Options) const {
  if (!sameID(Unit.targetID(), State->Target->ID) ||
      Unit.targetSchemaDigest() != State->Schema->Digest)
    return layoutError("MC layout received a foreign Target/schema unit");
  uint32_t MaximumIterations =
      Options.MaximumIterations == 0
          ? State->Backend.MaximumLayoutIterations
          : Options.MaximumIterations;
  if (MaximumIterations == 0 ||
      MaximumIterations > HardMaximumLayoutIterations)
    return layoutError("MC layout iteration limit is invalid");

  struct FragmentSnapshot {
    PluginMCFragment *Fragment = nullptr;
    std::vector<uint8_t> Contents;
  };
  struct FixupSnapshot {
    PluginMCFixup *Fixup = nullptr;
    uint64_t Offset = 0;
    uint32_t Width = 0;
    bool IsPCRelative = false;
    bool IsSigned = false;
    bool MayRelax = false;
    NevercMCFixupKind Kind = NEVERC_MC_FIXUP_NONE;
    uint32_t TargetKind = 0;
  };
  std::vector<FragmentSnapshot> SavedFragments;
  std::vector<FixupSnapshot> SavedFixups;
  for (auto &Section : Unit.sections())
    for (auto &Fragment : Section->Fragments) {
      SavedFragments.push_back({Fragment.get(), Fragment->Contents});
      for (auto &Fixup : Fragment->Fixups)
        SavedFixups.push_back(
            {Fixup.get(), Fixup->Offset, Fixup->Width,
             Fixup->IsPCRelative, Fixup->IsSigned, Fixup->MayRelax,
             Fixup->Kind, Fixup->TargetKind});
    }
  auto Rollback = make_scope_exit([&] {
    for (FragmentSnapshot &Saved : SavedFragments)
      Saved.Fragment->Contents = std::move(Saved.Contents);
    for (const FixupSnapshot &Saved : SavedFixups) {
      Saved.Fixup->Offset = Saved.Offset;
      Saved.Fixup->Width = Saved.Width;
      Saved.Fixup->IsPCRelative = Saved.IsPCRelative;
      Saved.Fixup->IsSigned = Saved.IsSigned;
      Saved.Fixup->MayRelax = Saved.MayRelax;
      Saved.Fixup->Kind = Saved.Kind;
      Saved.Fixup->TargetKind = Saved.TargetKind;
    }
  });

  LayoutPositions FinalPositions;
  uint32_t CompletedIterations = 0;
  for (uint32_t Iteration = 1; Iteration <= MaximumIterations;
       ++Iteration) {
    auto Positions =
        computePositions(Unit, State->Backend.MinimumInstructionAlignment);
    if (!Positions)
      return Positions.takeError();
    for (auto &Section : Unit.sections())
      for (auto &Fragment : Section->Fragments)
        if (Error E = verifyFixupRanges(*Fragment))
          return std::move(E);

    struct RelaxPlan {
      PluginMCFragment *Fragment = nullptr;
      PluginMCFixup *Fixup = nullptr;
      std::vector<uint8_t> Bytes;
      uint64_t Offset = 0;
      uint32_t Width = 0;
      bool IsPCRelative = false;
      bool IsSigned = false;
      bool MayRelax = false;
      NevercMCFixupKind Kind = NEVERC_MC_FIXUP_NONE;
      uint32_t TargetKind = 0;
    };
    std::vector<RelaxPlan> Plans;
    std::set<PluginMCFragment *> PlannedFragments;

    for (auto &SectionStorage : Unit.sections()) {
      PluginMCSection &Section = *SectionStorage;
      for (auto &FragmentStorage : Section.Fragments) {
        PluginMCFragment &Fragment = *FragmentStorage;
        const uint64_t FragmentOffset =
            Positions->FragmentOffsets.at(&Fragment);
        for (auto &FixupStorage : Fragment.Fixups) {
          PluginMCFixup &Fixup = *FixupStorage;
          auto Context = makeFixupContext(
              Task, Section, Fragment, Fixup, FragmentOffset,
              State->Backend);
          if (!Context)
            return Context.takeError();
          const bool InRange =
              !Context->Evaluation.Resolved ||
              fitsValue(Context->Request.Value, Fixup.Width,
                        Fixup.IsSigned);
          NevercBool Relax = NEVERC_FALSE;
          if (Fixup.MayRelax) {
            NevercStatus Status;
            try {
              Status = State->Backend.ShouldRelaxFixup(
                  State->Backend.UserData, &Context->Request, &Relax);
            } catch (...) {
              return layoutError(
                  "MC relaxation predicate raised an exception");
            }
            if (Status.Code != NEVERC_STATUS_OK)
              return callbackError("MC relaxation predicate", Status);
            if (!validBool(Relax))
              return layoutError(
                  "MC relaxation predicate returned an invalid boolean");
          }
          if (!InRange && Relax != NEVERC_TRUE)
            return layoutError("MC fixup value is out of range");
          if (Relax != NEVERC_TRUE)
            continue;
          if (!PlannedFragments.insert(&Fragment).second)
            return layoutError(
                "multiple MC fixups attempted to relax one fragment");

          NevercMCRelaxationResult Result{};
          Result.Header = {sizeof(Result), NEVERC_MC_API_MAJOR,
                           NEVERC_MC_API_MINOR, 0};
          NevercStatus Status;
          try {
            Status = State->Backend.RelaxFragment(
                State->Backend.UserData, &Context->Request, &Result);
          } catch (...) {
            return layoutError("MC relaxation callback raised an exception");
          }
          if (Status.Code != NEVERC_STATUS_OK)
            return callbackError("MC relaxation", Status);
          if (!validHeader(Result.Header, sizeof(Result)) ||
              Result.Changed != NEVERC_TRUE ||
              llvm::any_of(Result.Reserved8,
                           [](uint8_t Value) { return Value != 0; }) ||
              Result.Reserved != 0 ||
              (!Result.ReplacementBytes.Data &&
               Result.ReplacementBytes.Length != 0) ||
              Result.ReplacementBytes.Length == 0 ||
              Result.ReplacementBytes.Length > MaximumFragmentSize ||
              Result.NewFixupWidth == 0 ||
              Result.NewFixupWidth > 64 ||
              Result.NewFixupWidth % 8 != 0 ||
              Result.NewFixupOffset >
                  Result.ReplacementBytes.Length ||
              Result.NewFixupWidth / 8 >
                  Result.ReplacementBytes.Length -
                      Result.NewFixupOffset)
            return layoutError(
                "MC relaxation callback returned an invalid transaction");
          auto NewInfo = queryFixupKindInfo(
              State->Backend, Result.NewFixupKind,
              Result.NewTargetKind, Result.NewFixupWidth,
              Result.ReplacementBytes.Length);
          if (!NewInfo)
            return NewInfo.takeError();
          RelaxPlan Plan;
          Plan.Fragment = &Fragment;
          Plan.Fixup = &Fixup;
          Plan.Bytes.assign(
              Result.ReplacementBytes.Data,
              Result.ReplacementBytes.Data +
                  Result.ReplacementBytes.Length);
          Plan.Offset = Result.NewFixupOffset;
          Plan.Width = Result.NewFixupWidth;
          Plan.IsPCRelative =
              (NewInfo->Flags &
               NEVERC_MC_FIXUP_INFO_PC_RELATIVE) != 0;
          Plan.IsSigned =
              (NewInfo->Flags & NEVERC_MC_FIXUP_INFO_SIGNED) != 0;
          Plan.MayRelax =
              (NewInfo->Flags &
               NEVERC_MC_FIXUP_INFO_RELAXABLE) != 0;
          Plan.Kind = Result.NewFixupKind;
          Plan.TargetKind = Result.NewTargetKind;
          Plans.push_back(std::move(Plan));
        }
      }
    }

    if (!Plans.empty()) {
      for (RelaxPlan &Plan : Plans) {
        Plan.Fragment->Contents = std::move(Plan.Bytes);
        Plan.Fixup->Offset = Plan.Offset;
        Plan.Fixup->Width = Plan.Width;
        Plan.Fixup->IsPCRelative = Plan.IsPCRelative;
        Plan.Fixup->IsSigned = Plan.IsSigned;
        Plan.Fixup->MayRelax = Plan.MayRelax;
        Plan.Fixup->Kind = Plan.Kind;
        Plan.Fixup->TargetKind = Plan.TargetKind;
      }
      if (Iteration == MaximumIterations)
        return layoutError("MC layout did not converge");
      continue;
    }
    FinalPositions = std::move(*Positions);
    CompletedIterations = Iteration;
    break;
  }
  if (CompletedIterations == 0)
    return layoutError("MC layout did not converge");

  std::unordered_map<PluginMCFragment *, std::vector<uint8_t>>
      WorkingBytes;
  for (auto &Section : Unit.sections())
    for (auto &Fragment : Section->Fragments)
      WorkingBytes.emplace(Fragment.get(), Fragment->Contents);

  MCLayoutResult Result;
  Result.Iterations = CompletedIterations;
  for (auto &SectionStorage : Unit.sections()) {
    PluginMCSection &Section = *SectionStorage;
    for (auto &FragmentStorage : Section.Fragments) {
      PluginMCFragment &Fragment = *FragmentStorage;
      const uint64_t FragmentOffset =
          FinalPositions.FragmentOffsets.at(&Fragment);
      for (auto &FixupStorage : Fragment.Fixups) {
        PluginMCFixup &Fixup = *FixupStorage;
        auto Context = makeFixupContext(
            Task, Section, Fragment, Fixup, FragmentOffset,
            State->Backend);
        if (!Context)
          return Context.takeError();
        if (Context->Evaluation.Resolved) {
          if (!fitsValue(Context->Request.Value, Fixup.Width,
                         Fixup.IsSigned))
            return layoutError("MC fixup value is out of range");
          std::vector<uint8_t> &Bytes = WorkingBytes.at(&Fragment);
          NevercStatus Status;
          try {
            Status = State->Backend.ApplyFixup(
                State->Backend.UserData, &Context->Request,
                {Bytes.data(), Bytes.size()});
          } catch (...) {
            return layoutError("MC apply-fixup callback raised an exception");
          }
          if (Status.Code != NEVERC_STATUS_OK)
            return callbackError("MC apply-fixup", Status);
          continue;
        }

        uint32_t RelocationKind = 0;
        NevercStatus Status;
        try {
          Status = State->Backend.MapRelocation(
              State->Backend.UserData, &Context->Request,
              &RelocationKind);
        } catch (...) {
          return layoutError(
              "MC relocation mapping callback raised an exception");
        }
        if (Status.Code != NEVERC_STATUS_OK)
          return callbackError("MC relocation mapping", Status);
        if (RelocationKind == 0 || !Context->Evaluation.Symbol)
          return layoutError("MC relocation mapping returned no valid target");
        Result.Relocations.push_back(
            {Section.Name,
             FragmentOffset + Fixup.Offset,
             Fixup.Width,
             RelocationKind,
             Context->Evaluation.Symbol->Name,
             Context->Evaluation.Addend,
             Fixup.IsPCRelative,
             Fixup.IsSigned});
      }
    }
  }

  for (auto &Entry : WorkingBytes)
    Entry.first->Contents = std::move(Entry.second);

  for (const auto &SectionStorage : Unit.sections()) {
    const PluginMCSection &Section = *SectionStorage;
    MCLayoutSection Output;
    Output.Name = Section.Name;
    Output.Alignment = Section.Alignment;
    Output.Bytes.reserve(
        static_cast<size_t>(FinalPositions.SectionSizes.at(&Section)));
    for (const auto &FragmentStorage : Section.Fragments) {
      const PluginMCFragment &Fragment = *FragmentStorage;
      const uint64_t Offset =
          FinalPositions.FragmentOffsets.at(&Fragment);
      if (Offset < Output.Bytes.size())
        return layoutError("MC section materialization overlaps fragments");
      const uint64_t Padding = Offset - Output.Bytes.size();
      if (Padding != 0) {
        if ((Section.Flags & NEVERC_MC_SECTION_EXECUTABLE) != 0) {
          auto Nops = generateNops(State->Backend, Padding);
          if (!Nops)
            return Nops.takeError();
          Output.Bytes.insert(Output.Bytes.end(), Nops->begin(), Nops->end());
        } else {
          Output.Bytes.insert(Output.Bytes.end(),
                              static_cast<size_t>(Padding), 0);
        }
      }
      Output.Bytes.insert(Output.Bytes.end(), Fragment.Contents.begin(),
                          Fragment.Contents.end());
    }
    if (Result.TotalSize >
        std::numeric_limits<uint64_t>::max() - Output.Bytes.size())
      return layoutError("MC layout total size overflows");
    Result.TotalSize += Output.Bytes.size();
    Result.Sections.push_back(std::move(Output));
  }
  Result.Digest = digestResult(Result);
  Rollback.release();
  return Result;
}

Expected<NevercMCFixupKindInfo>
BuiltinMCAsmBackendAdapter::fixupKindInfo(
    const MCAsmBackend &Backend, MCFixupKind Kind) {
  const MCFixupKindInfo &Native = Backend.getFixupKindInfo(Kind);
  NevercMCFixupKindInfo Result{};
  Result.Header = {sizeof(Result), NEVERC_MC_API_MAJOR,
                   NEVERC_MC_API_MINOR, 0};
  Result.TargetOffset = Native.TargetOffset;
  Result.TargetSize = Native.TargetSize;
  if ((Native.Flags & MCFixupKindInfo::FKF_IsPCRel) != 0)
    Result.Flags |= NEVERC_MC_FIXUP_INFO_PC_RELATIVE;
  if ((Native.Flags & MCFixupKindInfo::FKF_IsTarget) != 0)
    Result.Flags |= NEVERC_MC_FIXUP_INFO_TARGET;
  return Result;
}

Expected<std::vector<uint8_t>>
BuiltinMCAsmBackendAdapter::writeNops(
    const MCAsmBackend &Backend, uint64_t Count,
    const MCSubtargetInfo *Subtarget) {
  SmallString<64> Bytes;
  raw_svector_ostream Stream(Bytes);
  if (!Backend.writeNopData(Stream, Count, Subtarget))
    return layoutError("built-in MC backend cannot emit requested nops");
  return std::vector<uint8_t>(Bytes.begin(), Bytes.end());
}

} // namespace neverc::plugin
