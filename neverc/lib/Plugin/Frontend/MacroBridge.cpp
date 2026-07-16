#include "PrepBridgeInternal.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Scan/MacroArgStorage.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/SourceScanner.h"
#include <limits>
#include <new>

using namespace llvm;

namespace neverc::plugin {
using namespace prep_bridge_detail;

namespace {

template <typename PayloadT>
NevercStatus
resolvePrepPayload(PluginTaskContext &Task, PrepEngine &Prep,
                   NevercTaskHandle TaskHandle, NevercHandle Handle,
                   PluginHandleKind HandleKind, PayloadT **OutPayload) {
  if (!OutPayload)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutPayload = nullptr;
  if (!sameHandle(TaskHandle, Task.handle()))
    return status(NEVERC_STATUS_WRONG_SCOPE);
  if (Task.isEnded())
    return status(NEVERC_STATUS_INVALID_STATE);
  void *RawPayload = nullptr;
  NevercStatus Status = Task.handles().resolve(Handle, HandleKind, &RawPayload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Payload = static_cast<PayloadT *>(RawPayload);
  if (Payload->Engine != &Prep)
    return status(NEVERC_STATUS_WRONG_SCOPE);
  *OutPayload = Payload;
  return neverc_status_ok();
}

NevercStatus locationStatus(FrontendPluginBridge &Locations,
                            SourceLocation Native,
                            NevercSourceLocation *OutLocation) {
  if (!OutLocation)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutLocation = {};
  if (Native.isInvalid())
    return neverc_status_ok();
  auto Public = Locations.createLocation(Native);
  if (!Public) {
    consumeError(Public.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutLocation = *Public;
  return neverc_status_ok();
}

bool hasVAOpt(const MacroRecord &Macro) {
  for (const Token &Value : Macro.tokens()) {
    if (Value.is(tok::raw_identifier) || Value.isAnnotation() ||
        Value.isLiteral() || Value.is(tok::eof))
      continue;
    const IdentifierInfo *Identifier = Value.getIdentifierInfo();
    if (Identifier && Identifier->getName() == "__VA_OPT__")
      return true;
  }
  return false;
}

bool validIdentifierName(PrepEngine &Prep, StringRef Name) {
  if (Name.empty() || Name.contains('\0'))
    return false;
  Token Scratch;
  Scratch.startToken();
  Scratch.setKind(tok::raw_identifier);
  Prep.WriteScratch(Name, Scratch);
  Token Scanned;
  if (SourceScanner::scanRawToken(Scratch.getLocation(), Scanned,
                                  Prep.getSourceManager(), Prep.getLangOpts(),
                                  /*IgnoreWhiteSpace=*/true))
    return false;
  return Scanned.is(tok::raw_identifier) && Scanned.getLength() == Name.size();
}

} // namespace

Expected<NevercIdentifierHandle>
PluginPrepBridge::createIdentifier(IdentifierInfo *Identifier) {
  if (!Identifier)
    return createStringError(inconvertibleErrorCode(),
                             "cannot publish a null identifier");
  auto *Payload = new (std::nothrow) IdentifierPayload{&Prep, Identifier};
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "unable to allocate identifier payload");
  auto Handle = Task.handles().create(
      PluginIdentifierHandleKind, Payload,
      [](void *Value) { delete static_cast<IdentifierPayload *>(Value); });
  if (!Handle) {
    delete Payload;
    return Handle.takeError();
  }
  return *Handle;
}

Expected<NevercMacroDefinitionHandle>
PluginPrepBridge::createMacroDefinition(IdentifierInfo *Name,
                                        MacroDirective *Directive,
                                        SourceLocation UndefinitionLocation) {
  auto *Definition = dyn_cast_or_null<DefMacroDirective>(Directive);
  if (!Name || !Definition)
    return createStringError(inconvertibleErrorCode(),
                             "cannot publish an invalid macro definition");
  auto *Payload = new (std::nothrow) MacroDefinitionPayload{
      &Prep, Name, Definition->getInfo(), Definition, UndefinitionLocation};
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "unable to allocate macro definition payload");
  auto Handle = Task.handles().create(
      PluginMacroDefinitionHandleKind, Payload,
      [](void *Value) { delete static_cast<MacroDefinitionPayload *>(Value); });
  if (!Handle) {
    delete Payload;
    return Handle.takeError();
  }
  return *Handle;
}

Expected<NevercMacroDefinitionHandle>
PluginPrepBridge::createMacroDefinition(IdentifierInfo *Name,
                                        MacroRecord *Record) {
  if (!Name || !Record)
    return createStringError(inconvertibleErrorCode(),
                             "cannot publish an invalid macro definition");
  auto *Payload = new (std::nothrow)
      MacroDefinitionPayload{&Prep, Name, Record, nullptr, {}};
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "unable to allocate macro definition payload");
  auto Handle = Task.handles().create(
      PluginMacroDefinitionHandleKind, Payload,
      [](void *Value) { delete static_cast<MacroDefinitionPayload *>(Value); });
  if (!Handle) {
    delete Payload;
    return Handle.takeError();
  }
  return *Handle;
}

Expected<NevercMacroDirectiveHandle>
PluginPrepBridge::createMacroDirective(IdentifierInfo *Name,
                                       MacroDirective *Directive) {
  if (!Name || !Directive)
    return createStringError(inconvertibleErrorCode(),
                             "cannot publish an invalid macro directive");
  auto *Payload =
      new (std::nothrow) MacroDirectivePayload{&Prep, Name, Directive};
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "unable to allocate macro directive payload");
  auto Handle = Task.handles().create(
      PluginMacroDirectiveHandleKind, Payload,
      [](void *Value) { delete static_cast<MacroDirectivePayload *>(Value); });
  if (!Handle) {
    delete Payload;
    return Handle.takeError();
  }
  return *Handle;
}

Expected<NevercMacroArgumentHandle>
PluginPrepBridge::createMacroArguments(const MacroArgStorage *Arguments) {
  if (!Arguments)
    return createStringError(inconvertibleErrorCode(),
                             "cannot publish null macro arguments");
  auto *Payload = new (std::nothrow) MacroArgumentPayload();
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "unable to allocate macro argument payload");
  Payload->Engine = &Prep;
  Payload->VarargsElided = Arguments->isVarargsElidedUse();
  const unsigned Count = Arguments->getNumMacroArguments();
  Payload->Offsets.reserve(Count);
  Payload->Lengths.reserve(Count);
  for (unsigned Index = 0; Index != Count; ++Index) {
    const Token *Argument = Arguments->getUnexpArgument(Index);
    const unsigned Length = MacroArgStorage::getArgLength(Argument);
    Payload->Offsets.push_back(Payload->Tokens.size());
    Payload->Lengths.push_back(Length);
    Payload->Tokens.insert(Payload->Tokens.end(), Argument, Argument + Length);
  }
  auto Handle = Task.handles().create(
      PluginMacroArgumentHandleKind, Payload,
      [](void *Value) { delete static_cast<MacroArgumentPayload *>(Value); });
  if (!Handle) {
    delete Payload;
    return Handle.takeError();
  }
  return *Handle;
}

NevercStatus
PluginPrepBridge::resolveIdentifier(NevercTaskHandle TaskHandle,
                                    NevercIdentifierHandle Identifier,
                                    IdentifierInfo **OutIdentifier) {
  if (!OutIdentifier)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutIdentifier = nullptr;
  IdentifierPayload *Payload = nullptr;
  NevercStatus Status = resolvePrepPayload(
      Task, Prep, TaskHandle, Identifier, PluginIdentifierHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutIdentifier = Payload->Identifier;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginPrepBridge::getIdentifierInfo(
    void *Context, NevercTaskHandle TaskHandle,
    NevercIdentifierHandle IdentifierHandle, NevercIdentifierInfo *OutInfo) {
  if (!Context || !OutInfo)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  IdentifierInfo *Identifier = nullptr;
  NevercStatus Status =
      Bridge.resolveIdentifier(TaskHandle, IdentifierHandle, &Identifier);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  NevercIdentifierInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, 0};
  Info.Name = stringView(Identifier->getName());
  Info.TokenKind = stableTokenKind(Identifier->getTokenID());
  const tok::PPKeywordKind PPKeyword = Identifier->getPPKeywordID();
  Info.PPKeywordKind = stablePPKeywordKind(PPKeyword);
  Info.BuiltinID = Identifier->getBuiltinID();
  if (Identifier->isKeyword(Bridge.Prep.getLangOpts()))
    Info.Flags |= NEVERC_IDENTIFIER_KEYWORD;
  if (PPKeyword != tok::pp_not_keyword)
    Info.Flags |= NEVERC_IDENTIFIER_PP_KEYWORD;
  if (Info.BuiltinID != 0)
    Info.Flags |= NEVERC_IDENTIFIER_BUILTIN;
  if (Identifier->hasMacroDefinition())
    Info.Flags |= NEVERC_IDENTIFIER_HAS_MACRO;
  if (Identifier->isPoisoned())
    Info.Flags |= NEVERC_IDENTIFIER_POISONED;
  if (Identifier->isExtensionToken())
    Info.Flags |= NEVERC_IDENTIFIER_EXTENSION_TOKEN;
  if (Identifier->isFutureCompatKeyword())
    Info.Flags |= NEVERC_IDENTIFIER_FUTURE_COMPAT_KEYWORD;
  if (Identifier->isReserved(Bridge.Prep.getLangOpts()) !=
      ReservedIdentifierStatus::NotReserved)
    Info.Flags |= NEVERC_IDENTIFIER_RESERVED;
  return writeCallerBuffer(OutInfo, Info);
}

NevercStatus NEVERC_CALL PluginPrepBridge::getOrCreateIdentifier(
    void *Context, NevercTaskHandle TaskHandle, NevercStringView Name,
    NevercIdentifierHandle *OutIdentifier) {
  if (!Context || !OutIdentifier)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutIdentifier = {};
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  if (!sameHandle(TaskHandle, Bridge.Task.handle()))
    return status(NEVERC_STATUS_WRONG_SCOPE);
  if (Bridge.Task.isEnded())
    return status(NEVERC_STATUS_INVALID_STATE);
  if ((!Name.Data && Name.Length != 0) ||
      Name.Length > std::numeric_limits<size_t>::max())
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  StringRef NativeName(Name.Data ? Name.Data : "",
                       static_cast<size_t>(Name.Length));
  if (!validIdentifierName(Bridge.Prep, NativeName))
    return status(NEVERC_STATUS_VERIFICATION_FAILED);

  auto PublicIdentifier =
      Bridge.createIdentifier(Bridge.Prep.getIdentifierInfo(NativeName));
  if (!PublicIdentifier) {
    consumeError(PublicIdentifier.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutIdentifier = *PublicIdentifier;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginPrepBridge::getMacroDefinitionForIdentifier(
    void *Context, NevercTaskHandle TaskHandle,
    NevercIdentifierHandle IdentifierHandle,
    NevercMacroDefinitionHandle *OutDefinition) {
  if (!Context || !OutDefinition)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutDefinition = {};
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  IdentifierInfo *Identifier = nullptr;
  NevercStatus Status =
      Bridge.resolveIdentifier(TaskHandle, IdentifierHandle, &Identifier);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  MacroDefinition Definition = Bridge.Prep.getMacroDefinition(Identifier);
  if (!Definition)
    return neverc_status_ok();
  auto PublicDefinition =
      Bridge.createMacroDefinition(Identifier, Definition.getLocalDirective());
  if (!PublicDefinition) {
    consumeError(PublicDefinition.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutDefinition = *PublicDefinition;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginPrepBridge::getMacroDefinitionInfo(
    void *Context, NevercTaskHandle TaskHandle,
    NevercMacroDefinitionHandle DefinitionHandle,
    NevercMacroDefinitionInfo *OutInfo) {
  if (!Context || !OutInfo)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  MacroDefinitionPayload *Payload = nullptr;
  NevercStatus Status =
      resolvePrepPayload(Bridge.Task, Bridge.Prep, TaskHandle, DefinitionHandle,
                         PluginMacroDefinitionHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MacroRecord *Macro = Payload->Record;

  NevercMacroDefinitionInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, 0};
  auto Name = Bridge.createIdentifier(Payload->Name);
  if (!Name) {
    consumeError(Name.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Info.Name = *Name;
  if (Payload->Directive) {
    auto Directive =
        Bridge.createMacroDirective(Payload->Name, Payload->Directive);
    if (!Directive) {
      consumeError(Directive.takeError());
      return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Info.Directive = *Directive;
  }
  Status = locationStatus(Bridge.Locations, Macro->getDefinitionLoc(),
                          &Info.DefinitionLocation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = locationStatus(Bridge.Locations, Macro->getDefinitionEndLoc(),
                          &Info.DefinitionEndLocation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = locationStatus(Bridge.Locations, Payload->UndefinitionLocation,
                          &Info.UndefinitionLocation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Info.ParameterCount = Macro->getNumParams();
  Info.ReplacementTokenCount = Macro->getNumTokens();
  if (Macro->isFunctionLike())
    Info.Flags |= NEVERC_MACRO_FUNCTION_LIKE;
  if (Macro->isVariadic())
    Info.Flags |= NEVERC_MACRO_VARIADIC;
  if (Macro->isC99Varargs())
    Info.Flags |= NEVERC_MACRO_C99_VARIADIC;
  if (Macro->isGNUVarargs())
    Info.Flags |= NEVERC_MACRO_GNU_VARIADIC;
  if (hasVAOpt(*Macro))
    Info.Flags |= NEVERC_MACRO_HAS_VA_OPT;
  if (Macro->isBuiltinMacro())
    Info.Flags |= NEVERC_MACRO_BUILTIN;
  if (Macro->hasCommaPasting())
    Info.Flags |= NEVERC_MACRO_COMMA_PASTING;
  return writeCallerBuffer(OutInfo, Info);
}

NevercStatus NEVERC_CALL PluginPrepBridge::getMacroParameter(
    void *Context, NevercTaskHandle TaskHandle,
    NevercMacroDefinitionHandle DefinitionHandle, uint32_t Index,
    NevercIdentifierHandle *OutParameter) {
  if (!Context || !OutParameter)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutParameter = {};
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  MacroDefinitionPayload *Payload = nullptr;
  NevercStatus Status =
      resolvePrepPayload(Bridge.Task, Bridge.Prep, TaskHandle, DefinitionHandle,
                         PluginMacroDefinitionHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MacroRecord *Macro = Payload->Record;
  if (Index >= Macro->getNumParams())
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  IdentifierInfo *Parameter = *(Macro->param_begin() + Index);
  auto PublicParameter = Bridge.createIdentifier(Parameter);
  if (!PublicParameter) {
    consumeError(PublicParameter.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutParameter = *PublicParameter;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginPrepBridge::getMacroReplacementToken(
    void *Context, NevercTaskHandle TaskHandle,
    NevercMacroDefinitionHandle DefinitionHandle, uint32_t Index,
    NevercTokenHandle *OutToken) {
  if (!Context || !OutToken)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutToken = {};
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  MacroDefinitionPayload *Payload = nullptr;
  NevercStatus Status =
      resolvePrepPayload(Bridge.Task, Bridge.Prep, TaskHandle, DefinitionHandle,
                         PluginMacroDefinitionHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MacroRecord *Macro = Payload->Record;
  if (Index >= Macro->getNumTokens())
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto PublicToken = Bridge.createToken(Macro->getReplacementToken(Index));
  if (!PublicToken) {
    consumeError(PublicToken.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutToken = *PublicToken;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginPrepBridge::getMacroDirectiveInfo(
    void *Context, NevercTaskHandle TaskHandle,
    NevercMacroDirectiveHandle DirectiveHandle,
    NevercMacroDirectiveInfo *OutInfo) {
  if (!Context || !OutInfo)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  MacroDirectivePayload *Payload = nullptr;
  NevercStatus Status =
      resolvePrepPayload(Bridge.Task, Bridge.Prep, TaskHandle, DirectiveHandle,
                         PluginMacroDirectiveHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  NevercMacroDirectiveInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, 0};
  Info.Kind = Payload->Directive->getKind() == MacroDirective::MD_Define
                  ? NEVERC_MACRO_DIRECTIVE_DEFINE
                  : NEVERC_MACRO_DIRECTIVE_UNDEFINE;
  Status = locationStatus(Bridge.Locations, Payload->Directive->getLocation(),
                          &Info.Location);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (MacroDirective *Previous = Payload->Directive->getPrevious()) {
    auto PublicPrevious = Bridge.createMacroDirective(Payload->Name, Previous);
    if (!PublicPrevious) {
      consumeError(PublicPrevious.takeError());
      return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Info.Previous = *PublicPrevious;
  }

  MacroDirective::DefInfo Definition = Payload->Directive->getDefinition();
  if (Definition) {
    auto PublicDefinition =
        Bridge.createMacroDefinition(Payload->Name, Definition.getDirective(),
                                     Definition.getUndefLocation());
    if (!PublicDefinition) {
      consumeError(PublicDefinition.takeError());
      return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Info.Definition = *PublicDefinition;
  }
  return writeCallerBuffer(OutInfo, Info);
}

NevercStatus NEVERC_CALL PluginPrepBridge::getMacroArgumentInfo(
    void *Context, NevercTaskHandle TaskHandle,
    NevercMacroArgumentHandle Arguments, NevercMacroArgumentInfo *OutInfo) {
  if (!Context || !OutInfo)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  MacroArgumentPayload *Payload = nullptr;
  NevercStatus Status =
      resolvePrepPayload(Bridge.Task, Bridge.Prep, TaskHandle, Arguments,
                         PluginMacroArgumentHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  NevercMacroArgumentInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, 0};
  Info.ArgumentCount = Payload->Offsets.size();
  Info.VarargsElided = Payload->VarargsElided ? NEVERC_TRUE : NEVERC_FALSE;
  return writeCallerBuffer(OutInfo, Info);
}

NevercStatus NEVERC_CALL PluginPrepBridge::getMacroArgumentTokenStream(
    void *Context, NevercTaskHandle TaskHandle,
    NevercMacroArgumentHandle Arguments, uint32_t Index,
    NevercTokenStreamHandle *OutStream) {
  if (!Context || !OutStream)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutStream = {};
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  MacroArgumentPayload *Payload = nullptr;
  NevercStatus Status =
      resolvePrepPayload(Bridge.Task, Bridge.Prep, TaskHandle, Arguments,
                         PluginMacroArgumentHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Index >= Payload->Offsets.size())
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  ArrayRef<Token> Tokens(Payload->Tokens);
  Tokens = Tokens.slice(static_cast<size_t>(Payload->Offsets[Index]),
                        static_cast<size_t>(Payload->Lengths[Index]));
  auto PublicStream = Bridge.createTokenStream(Tokens);
  if (!PublicStream) {
    consumeError(PublicStream.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutStream = *PublicStream;
  return neverc_status_ok();
}

} // namespace neverc::plugin
