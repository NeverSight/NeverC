#include "neverc/Plugin/Host/BuiltinLLVMAsmParser.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/ObjectPhaseHooks.h"
#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "neverc/Plugin/Host/ObjectWriterProvider.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "gtest/gtest.h"
#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;

namespace {

constexpr NevercTargetID TestTargetID{UINT64_C(0x4e43505244525401),
                                      UINT64_C(1)};
constexpr NevercObjectFormatID TestFormatID{UINT64_C(0x4e43505244464d54),
                                            UINT64_C(1)};

std::string errorText(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

NevercStringView view(const char *Value) {
  return {Value, std::char_traits<char>::length(Value)};
}

class ObjectReaderTaskScope {
public:
  ObjectReaderTaskScope()
      : Services("neverc-plugin-object-reader-tests", LLVM_VERSION_MAJOR) {}

  bool initialize(llvm::ArrayRef<llvm::StringRef> PluginPaths = {}) {
    if (Error E = registerPluginIOInterface(Services)) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    if (Error E = registerPluginObjectPhaseInterface(Services)) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    if (Error E = Services.interfaces().freeze()) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    std::vector<StringRef> Selected;
    for (StringRef PluginPath : PluginPaths) {
      auto Loaded = Services.registry().load(PluginPath);
      if (!Loaded) {
        ADD_FAILURE() << errorText(Loaded.takeError());
        return false;
      }
      Plugins.push_back(*Loaded);
      Selected.push_back(Plugins.back()->descriptor().PluginID);
    }
    auto CreatedPlan = makePluginActivationPlan(Services.registry(), Selected);
    if (!CreatedPlan) {
      ADD_FAILURE() << errorText(CreatedPlan.takeError());
      return false;
    }
    Plan.emplace(std::move(*CreatedPlan));
    auto CreatedSession = PluginSession::create(Services, *Plan);
    if (!CreatedSession) {
      ADD_FAILURE() << errorText(CreatedSession.takeError());
      return false;
    }
    Session = std::move(*CreatedSession);
    auto CreatedTask = Session->createTask(NEVERC_TASK_CODEGEN);
    if (!CreatedTask) {
      ADD_FAILURE() << errorText(CreatedTask.takeError());
      return false;
    }
    Task = std::move(*CreatedTask);
    return true;
  }

  ~ObjectReaderTaskScope() {
    if (Task)
      EXPECT_FALSE(Task->end());
    if (Session)
      EXPECT_FALSE(Session->end());
    Plan.reset();
    EXPECT_FALSE(Services.shutdown());
  }

  PluginTaskContext &task() { return *Task; }
  const std::shared_ptr<const PluginModule> &plugin(size_t Index) const {
    return Plugins.at(Index);
  }

private:
  PluginProcessServices Services;
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
  std::vector<std::shared_ptr<const PluginModule>> Plugins;
};

Expected<OwnedTargetKey> makeTargetKey(NevercObjectFormatID FormatID) {
  TargetKeyBuilder Builder;
  Builder.setTargetID(TestTargetID)
      .setTriple("x86_64-neverc-none", "x86_64", "neverc", "none", "")
      .setCPU("generic", "generic")
      .setFeatures({})
      .setABI({UINT64_C(0x4e43505244414249), UINT64_C(1)})
      .setCallingConvention({UINT64_C(0x4e43505244434349), UINT64_C(1)})
      .setObjectFormat(FormatID)
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest(
          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
  return Builder.build();
}

void initializeBuiltinTargets() {
  static std::once_flag Once;
  std::call_once(Once, [] {
    InitializeAllTargetInfos();
    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmParsers();
    InitializeAllAsmPrinters();
  });
}

Expected<std::vector<uint8_t>>
assembleBuiltinObject(const BuiltinTargetRoute &Route, StringRef Assembly) {
  auto LLVMTarget = lookupBuiltinLLVMTarget(Route);
  if (!LLVMTarget)
    return LLVMTarget.takeError();

  SmallVector<char, 0> Bytes;
  raw_svector_ostream Stream(Bytes);
  BuiltinLLVMAsmParserRequest Request;
  Request.Target = *LLVMTarget;
  Request.TargetTriple = Triple(Triple::normalize(Route.CanonicalTriple));
  Request.CPU = Route.DefaultCPU;
  Request.Input = MemoryBufferRef(Assembly, "<object-reader-test>");
  Request.Output = &Stream;
  if (Error E = runBuiltinLLVMAsmParser(Request))
    return std::move(E);
  return std::vector<uint8_t>(Bytes.begin(), Bytes.end());
}

Error convertSingleAArch64RelaToRel(std::vector<uint8_t> &Bytes,
                                    uint64_t ImplicitAddend) {
  StringRef Image(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(Image);
  if (!Parsed)
    return Parsed.takeError();
  auto Sections = Parsed->sections();
  if (!Sections)
    return Sections.takeError();

  bool PatchedSite = false;
  for (const object::ELF64LE::Shdr &Section : *Sections) {
    auto Name = Parsed->getSectionName(Section);
    if (!Name)
      return Name.takeError();
    if (*Name == ".text") {
      if (Section.sh_size < sizeof(uint64_t) ||
          Section.sh_offset > Bytes.size() ||
          sizeof(uint64_t) > Bytes.size() - Section.sh_offset)
        return createStringError(inconvertibleErrorCode(),
                                 "test .text section cannot hold addend");
      for (unsigned I = 0; I != sizeof(uint64_t); ++I)
        Bytes[static_cast<size_t>(Section.sh_offset) + I] =
            static_cast<uint8_t>(ImplicitAddend >> (I * 8));
      PatchedSite = true;
      break;
    }
  }
  if (!PatchedSite)
    return createStringError(inconvertibleErrorCode(),
                             "test object has no .text section");

  for (const object::ELF64LE::Shdr &Section : *Sections) {
    auto Name = Parsed->getSectionName(Section);
    if (!Name)
      return Name.takeError();
    if (Section.sh_type != ELF::SHT_RELA || *Name != ".rela.text")
      continue;
    if (Section.sh_size != sizeof(object::ELF64LE::Rela) ||
        Section.sh_entsize != sizeof(object::ELF64LE::Rela))
      return createStringError(inconvertibleErrorCode(),
                               "test RELA section has unexpected shape");

    object::ELF64LE::Shdr Replacement = Section;
    Replacement.sh_type = ELF::SHT_REL;
    Replacement.sh_size = sizeof(object::ELF64LE::Rel);
    Replacement.sh_entsize = sizeof(object::ELF64LE::Rel);
    const auto *SectionBytes = reinterpret_cast<const uint8_t *>(&Section);
    if (SectionBytes < Bytes.data())
      return createStringError(inconvertibleErrorCode(),
                               "test section header precedes image");
    const size_t Offset = static_cast<size_t>(SectionBytes - Bytes.data());
    if (Offset > Bytes.size() || sizeof(Replacement) > Bytes.size() - Offset)
      return createStringError(inconvertibleErrorCode(),
                               "test section header exceeds image");
    std::memcpy(Bytes.data() + Offset, &Replacement, sizeof(Replacement));
    return Error::success();
  }
  return createStringError(inconvertibleErrorCode(),
                           "test object has no .rela.text section");
}

Error retargetSingleAArch64RelaToSymbol(std::vector<uint8_t> &Bytes,
                                        StringRef SymbolName,
                                        uint64_t ExpectedSymbolValue,
                                        int64_t Addend) {
  StringRef Image(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(Image);
  if (!Parsed)
    return Parsed.takeError();
  auto Sections = Parsed->sections();
  if (!Sections)
    return Sections.takeError();

  std::optional<uint32_t> SymbolIndex;
  for (const object::ELF64LE::Shdr &Section : *Sections) {
    if (Section.sh_type != ELF::SHT_SYMTAB)
      continue;
    auto Symbols = Parsed->symbols(&Section);
    auto Strings = Parsed->getStringTableForSymtab(Section);
    if (!Symbols)
      return Symbols.takeError();
    if (!Strings)
      return Strings.takeError();
    for (uint32_t I = 0; I != Symbols->size(); ++I) {
      auto Name = (*Symbols)[I].getName(*Strings);
      if (!Name)
        return Name.takeError();
      if (*Name != SymbolName)
        continue;
      if ((*Symbols)[I].st_value != ExpectedSymbolValue)
        return createStringError(inconvertibleErrorCode(),
                                 "test mapping symbol has unexpected value");
      SymbolIndex = I;
      break;
    }
  }
  if (!SymbolIndex)
    return createStringError(inconvertibleErrorCode(),
                             "test object has no requested mapping symbol");

  for (const object::ELF64LE::Shdr &Section : *Sections) {
    auto Name = Parsed->getSectionName(Section);
    if (!Name)
      return Name.takeError();
    if (Section.sh_type != ELF::SHT_RELA || *Name != ".rela.text")
      continue;
    auto Relocations = Parsed->relas(Section);
    if (!Relocations)
      return Relocations.takeError();
    if (Relocations->size() != 1)
      return createStringError(inconvertibleErrorCode(),
                               "test RELA section has unexpected shape");

    object::ELF64LE::Rela Replacement = Relocations->front();
    Replacement.setSymbolAndType(*SymbolIndex, ELF::R_AARCH64_ABS64);
    Replacement.r_addend = Addend;
    if (Section.sh_offset > Bytes.size() ||
        sizeof(Replacement) > Bytes.size() - Section.sh_offset)
      return createStringError(inconvertibleErrorCode(),
                               "test relocation exceeds image");
    std::memcpy(Bytes.data() + static_cast<size_t>(Section.sh_offset),
                &Replacement, sizeof(Replacement));
    return Error::success();
  }
  return createStringError(inconvertibleErrorCode(),
                           "test object has no .rela.text section");
}

Error clearELF64SymbolName(std::vector<uint8_t> &Bytes, StringRef SymbolName) {
  StringRef Image(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(Image);
  if (!Parsed)
    return Parsed.takeError();
  auto Sections = Parsed->sections();
  if (!Sections)
    return Sections.takeError();
  for (const object::ELF64LE::Shdr &Section : *Sections) {
    if (Section.sh_type != ELF::SHT_SYMTAB)
      continue;
    auto Symbols = Parsed->symbols(&Section);
    auto Strings = Parsed->getStringTableForSymtab(Section);
    if (!Symbols)
      return Symbols.takeError();
    if (!Strings)
      return Strings.takeError();
    for (const object::ELF64LE::Sym &Symbol : *Symbols) {
      auto Name = Symbol.getName(*Strings);
      if (!Name)
        return Name.takeError();
      if (*Name != SymbolName)
        continue;
      object::ELF64LE::Sym Replacement = Symbol;
      Replacement.st_name = 0;
      const auto *Native = reinterpret_cast<const uint8_t *>(&Symbol);
      if (Native < Bytes.data())
        return createStringError(inconvertibleErrorCode(),
                                 "test symbol precedes ELF image");
      const size_t Offset = static_cast<size_t>(Native - Bytes.data());
      if (Offset > Bytes.size() || sizeof(Replacement) > Bytes.size() - Offset)
        return createStringError(inconvertibleErrorCode(),
                                 "test symbol exceeds ELF image");
      std::memcpy(Bytes.data() + Offset, &Replacement, sizeof(Replacement));
      return Error::success();
    }
  }
  return createStringError(inconvertibleErrorCode(),
                           "test ELF symbol was not found");
}

Error patchELF64SectionAddress(std::vector<uint8_t> &Bytes,
                               StringRef SectionName, uint64_t Address) {
  StringRef Image(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(Image);
  if (!Parsed)
    return Parsed.takeError();
  auto Sections = Parsed->sections();
  if (!Sections)
    return Sections.takeError();
  for (const object::ELF64LE::Shdr &Section : *Sections) {
    auto Name = Parsed->getSectionName(Section);
    if (!Name)
      return Name.takeError();
    if (*Name != SectionName)
      continue;
    object::ELF64LE::Shdr Replacement = Section;
    Replacement.sh_addr = Address;
    const auto *SectionBytes = reinterpret_cast<const uint8_t *>(&Section);
    if (SectionBytes < Bytes.data())
      return createStringError(inconvertibleErrorCode(),
                               "test section precedes ELF image");
    const size_t Offset = static_cast<size_t>(SectionBytes - Bytes.data());
    if (Offset > Bytes.size() || sizeof(Replacement) > Bytes.size() - Offset)
      return createStringError(inconvertibleErrorCode(),
                               "test section header exceeds ELF image");
    std::memcpy(Bytes.data() + Offset, &Replacement, sizeof(Replacement));
    return Error::success();
  }
  return createStringError(inconvertibleErrorCode(),
                           "test ELF section was not found");
}

Error patchELF64Type(std::vector<uint8_t> &Bytes, uint16_t Type) {
  if (Bytes.size() < sizeof(object::ELF64LE::Ehdr))
    return createStringError(inconvertibleErrorCode(),
                             "test ELF header is truncated");
  StringRef Image(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(Image);
  if (!Parsed)
    return Parsed.takeError();
  object::ELF64LE::Ehdr Replacement = Parsed->getHeader();
  Replacement.e_type = Type;
  std::memcpy(Bytes.data(), &Replacement, sizeof(Replacement));
  return Error::success();
}

Expected<std::vector<std::string>>
readELF64RelocationTargetNames(ArrayRef<uint8_t> Bytes,
                               StringRef RelocationSectionName) {
  StringRef Image(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(Image);
  if (!Parsed)
    return Parsed.takeError();
  auto Sections = Parsed->sections();
  if (!Sections)
    return Sections.takeError();

  std::vector<std::string> Names;
  for (const object::ELF64LE::Shdr &Section : *Sections) {
    auto SectionName = Parsed->getSectionName(Section);
    if (!SectionName)
      return SectionName.takeError();
    if (Section.sh_type != ELF::SHT_RELA ||
        *SectionName != RelocationSectionName)
      continue;
    auto SymbolTable = Parsed->getSection(Section.sh_link);
    if (!SymbolTable)
      return SymbolTable.takeError();
    auto Symbols = Parsed->symbols(*SymbolTable);
    auto Strings = Parsed->getStringTableForSymtab(**SymbolTable);
    auto Relocations = Parsed->relas(Section);
    if (!Symbols)
      return Symbols.takeError();
    if (!Strings)
      return Strings.takeError();
    if (!Relocations)
      return Relocations.takeError();
    for (const object::ELF64LE::Rela &Relocation : *Relocations) {
      if (Relocation.getSymbol() >= Symbols->size())
        return createStringError(inconvertibleErrorCode(),
                                 "test relocation symbol is out of range");
      auto Name = (*Symbols)[Relocation.getSymbol()].getName(*Strings);
      if (!Name)
        return Name.takeError();
      Names.push_back(Name->str());
    }
  }
  return Names;
}

Expected<std::vector<uint8_t>>
emitBuiltinObject(const BuiltinTargetRoute &Route) {
  auto LLVMTarget = lookupBuiltinLLVMTarget(Route);
  if (!LLVMTarget)
    return LLVMTarget.takeError();
  llvm::TargetOptions Options;
  std::unique_ptr<TargetMachine> Machine(
      (*LLVMTarget)
          ->createTargetMachine(Route.CanonicalTriple, Route.DefaultCPU, "",
                                Options, std::nullopt, CodeGenOptLevel::None));
  if (!Machine)
    return createStringError(inconvertibleErrorCode(),
                             "failed to create LLVM TargetMachine");

  LLVMContext Context;
  Module ObjectModule("object-reader-test", Context);
  ObjectModule.setTargetTriple(Route.CanonicalTriple);
  ObjectModule.setDataLayout(Machine->createDataLayout());
  Function *FunctionValue = Function::Create(
      FunctionType::get(Type::getInt32Ty(Context), false),
      GlobalValue::ExternalLinkage, "object_reader_probe", ObjectModule);
  BasicBlock *Entry = BasicBlock::Create(Context, "entry", FunctionValue);
  IRBuilder<> Builder(Entry);
  Builder.CreateRet(ConstantInt::get(Type::getInt32Ty(Context), 42));

  SmallVector<char, 0> Bytes;
  raw_svector_ostream Stream(Bytes);
  legacy::PassManager Passes;
  if (Machine->addPassesToEmitFile(Passes, Stream, nullptr,
                                   CodeGenFileType::ObjectFile))
    return createStringError(inconvertibleErrorCode(),
                             "TargetMachine cannot emit object files");
  Passes.run(ObjectModule);
  return std::vector<uint8_t>(Bytes.begin(), Bytes.end());
}

Expected<OwnedTargetKey> makeBuiltinTargetKey(const BuiltinTargetRoute &Route) {
  Triple Parsed(Triple::normalize(Route.CanonicalTriple));
  TargetKeyBuilder Builder;
  Builder.setTargetID(Route.TargetID)
      .setTriple(Route.CanonicalTriple.str(), Parsed.getArchName().str(),
                 Parsed.getVendorName().str(), Parsed.getOSName().str(),
                 Parsed.getEnvironmentName().str())
      .setCPU(Route.DefaultCPU.str(), Route.DefaultCPU.str())
      .setFeatures({})
      .setABI(Route.ABIID)
      .setCallingConvention({UINT64_C(0x4e43505244434349), Route.TargetID.Low})
      .setObjectFormat(Route.ObjectFormatID)
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest(
          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
  return Builder.build();
}

struct ObjectRequestVersionObservation {
  uint16_t ProbeMinor = UINT16_MAX;
  uint16_t ReadMinor = UINT16_MAX;
};

NevercStatus NEVERC_CALL
probeTestObject(void *UserData, const NevercObjectProbeRequest *Request,
                NevercObjectProbeResult *Result) {
  if (!Request || !Result)
    return {NEVERC_STATUS_INVALID_ARGUMENT, 0, 0};
  if (UserData)
    static_cast<ObjectRequestVersionObservation *>(UserData)->ProbeMinor =
        Request->Header.Minor;
  Result->ConsumedMinimum = 4;
  if (Request->Input.Length >= 4 &&
      std::memcmp(Request->Input.Data, "NOBJ", 4) == 0) {
    Result->Confidence = 900;
    Result->ArtifactKind = NEVERC_OBJECT_ARTIFACT_RELOCATABLE;
  }
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL probeTestObjectIgnoringUserData(
    void *, const NevercObjectProbeRequest *Request,
    NevercObjectProbeResult *Result) {
  return probeTestObject(nullptr, Request, Result);
}

NevercStatus NEVERC_CALL
readTestObject(void *UserData, const NevercObjectReadRequest *Request) {
  if (!Request || !Request->Object)
    return {NEVERC_STATUS_INVALID_ARGUMENT, 0, 0};
  if (UserData)
    static_cast<ObjectRequestVersionObservation *>(UserData)->ReadMinor =
        Request->Header.Minor;
  static const std::array<uint8_t, 2> Code{{0x2a, 0xc3}};
  NevercObjectSectionDescriptor Section{};
  Section.Header = {sizeof(Section), NEVERC_OBJECT_API_MAJOR,
                    NEVERC_OBJECT_API_MINOR, 0};
  Section.Name = view(".text");
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 1;
  Section.Data = {Code.data(), Code.size()};
  NevercObjectSectionHandle Handle{};
  return Request->Object->CreateSection(Request->Object->Context, Request->Task,
                                        Request->Mutation, &Section, &Handle);
}

struct NestedReaderMutationState {
  PluginTaskContext *Task = nullptr;
  std::string ObserverPluginID;
  const NevercObjectAPI *CachedObject = nullptr;
  NevercTaskHandle CachedTask{};
  NevercObjectMutationHandle CachedMutation{};
  NevercStatus ObserverDispatch{NEVERC_STATUS_INVALID_STATE, 0, 0};
  NevercStatus MutationAttempt{NEVERC_STATUS_INVALID_STATE, 0, 0};
};

NevercStatus NEVERC_CALL readAndAttemptMutationFromNestedObserver(
    void *UserData, const NevercObjectReadRequest *Request) {
  if (!UserData || !Request)
    return {NEVERC_STATUS_INVALID_ARGUMENT, 0, 0};
  auto &State = *static_cast<NestedReaderMutationState *>(UserData);
  if (!State.Task || State.ObserverPluginID.empty())
    return {NEVERC_STATUS_INVALID_STATE, 0, 0};

  NevercStatus Status = readTestObject(nullptr, Request);
  if (!neverc_status_is_ok(Status))
    return Status;
  State.CachedObject = Request->Object;
  State.CachedTask = Request->Task;
  State.CachedMutation = Request->Mutation;

  auto Nested = State.Task->invokeCallback(
      State.ObserverPluginID, "object_reader_nested_read_only_observer", [&] {
        static const std::array<uint8_t, 1> Byte{{UINT8_C(0x7f)}};
        NevercObjectSectionDescriptor Section{};
        Section.Header = {sizeof(Section), NEVERC_OBJECT_API_MAJOR,
                          NEVERC_OBJECT_API_MINOR, 0};
        Section.Name = view(".nested-observer-write");
        Section.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
        Section.Flags = NEVERC_OBJECT_SECTION_ALLOCATED;
        Section.Alignment = 1;
        Section.Data = {Byte.data(), Byte.size()};
        NevercObjectSectionHandle Created{};
        State.MutationAttempt = State.CachedObject->CreateSection(
            State.CachedObject->Context, State.CachedTask, State.CachedMutation,
            &Section, &Created);
        return neverc_status_ok();
      },
      true, nullptr, false, nullptr);
  if (!Nested) {
    consumeError(Nested.takeError());
    return {NEVERC_STATUS_PLUGIN_FAILURE, 0, 801};
  }
  State.ObserverDispatch = *Nested;
  return *Nested;
}

TEST(PluginObjectReaderTest, CustomReaderBuildsVerifiedGraphTransactionally) {
  ObjectReaderTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  ObjectRequestVersionObservation Versions;
  NevercObjectFormatDescriptor Format{};
  Format.Header = {sizeof(Format), NEVERC_OBJECT_FORMAT_API_MAJOR, UINT16_C(0),
                   0};
  Format.FormatID = TestFormatID;
  Format.CanonicalName = view("nobj");
  Format.DefaultExtension = view(".nobj");
  Format.Flags = NEVERC_OBJECT_FORMAT_CAN_PROBE | NEVERC_OBJECT_FORMAT_CAN_READ;
  Format.Probe = probeTestObject;
  Format.Reader = readTestObject;
  Format.UserData = &Versions;

  PluginTargetRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.object-reader";
  Registration.ObjectFormats = ArrayRef<NevercObjectFormatDescriptor>(Format);
  auto Snapshot = PluginTargetRegistry::freeze(ArrayRef(Registration),
                                               PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Provider = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Provider)) << errorText(Provider.takeError());
  auto Target = makeTargetKey(TestFormatID);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());

  const std::array<uint8_t, 8> Input{{'N', 'O', 'B', 'J', 1, 0, 0, 0}};
  auto Graph = (*Provider)->read(Scope.task(), Input, "answer.nobj", *Target);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());
  EXPECT_EQ((*Graph)->sectionCount(), 1U);
  ASSERT_EQ((*Graph)->sections().size(), 1U);
  const PluginObjectSection &Section = (*Graph)->sections().front();
  EXPECT_EQ(Section.Name, ".text");
  EXPECT_EQ(Section.Kind, NEVERC_OBJECT_SECTION_KIND_TEXT);
  EXPECT_EQ(Section.Data, (std::vector<uint8_t>{UINT8_C(0x2a), UINT8_C(0xc3)}));
  EXPECT_EQ((*Graph)->generation(), 2U);
  EXPECT_FALSE((*Graph)->hasLayoutProof());
  EXPECT_FALSE(verifyPluginObjectGraph(**Graph));
  EXPECT_EQ(Versions.ProbeMinor, 0U);
  EXPECT_EQ(Versions.ReadMinor, 0U);
}

TEST(PluginObjectReaderTest,
     NestedReadOnlyCallbackCannotReuseThirdPartyReaderMutationFacade) {
  ObjectReaderTaskScope Scope;
  const std::array<StringRef, 2> Plugins{
      {NEVERC_TEST_MINIMAL_PLUGIN, NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN}};
  ASSERT_TRUE(Scope.initialize(Plugins));

  NestedReaderMutationState State;
  State.Task = &Scope.task();
  State.ObserverPluginID = Scope.plugin(1)->descriptor().PluginID;

  NevercObjectFormatDescriptor Format{};
  Format.Header = {sizeof(Format), NEVERC_OBJECT_FORMAT_API_MAJOR,
                   NEVERC_OBJECT_FORMAT_API_MINOR, 0};
  Format.FormatID = TestFormatID;
  Format.CanonicalName = view("nested-reader-capability");
  Format.DefaultExtension = view(".nrc");
  Format.Flags = NEVERC_OBJECT_FORMAT_CAN_PROBE | NEVERC_OBJECT_FORMAT_CAN_READ;
  Format.Probe = probeTestObjectIgnoringUserData;
  Format.Reader = readAndAttemptMutationFromNestedObserver;
  Format.UserData = &State;

  PluginTargetRegistrationView Registration;
  Registration.PluginID = Scope.plugin(0)->descriptor().PluginID;
  Registration.Owner = Scope.plugin(0);
  Registration.ObjectFormats = ArrayRef<NevercObjectFormatDescriptor>(Format);
  auto Snapshot = PluginTargetRegistry::freeze(ArrayRef(Registration),
                                               PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Provider = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Provider)) << errorText(Provider.takeError());
  auto Target = makeTargetKey(TestFormatID);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());

  const std::array<uint8_t, 8> Input{{'N', 'O', 'B', 'J', 1, 0, 0, 0}};
  auto Graph = (*Provider)->read(Scope.task(), Input, "nested-reader.nrc",
                                 *Target, TestFormatID);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());
  EXPECT_EQ(State.ObserverDispatch.Code, NEVERC_STATUS_OK);
  EXPECT_EQ(State.MutationAttempt.Code, NEVERC_STATUS_POLICY_VIOLATION);
}

TEST(PluginObjectReaderTest, EqualConfidenceProbeIsRejectedAsAmbiguous) {
  ObjectReaderTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  NevercObjectFormatDescriptor Formats[2]{};
  Formats[0].Header = {sizeof(Formats[0]), NEVERC_OBJECT_FORMAT_API_MAJOR,
                       UINT16_C(0), 0};
  Formats[0].FormatID = TestFormatID;
  Formats[0].CanonicalName = view("nobj.first");
  Formats[0].Flags =
      NEVERC_OBJECT_FORMAT_CAN_PROBE | NEVERC_OBJECT_FORMAT_CAN_READ;
  Formats[0].Probe = probeTestObject;
  Formats[0].Reader = readTestObject;
  Formats[1] = Formats[0];
  Formats[1].FormatID = {TestFormatID.High, UINT64_C(2)};
  Formats[1].CanonicalName = view("nobj.second");

  PluginTargetRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.object-probe-conflict";
  Registration.ObjectFormats = Formats;
  auto Snapshot = PluginTargetRegistry::freeze(ArrayRef(Registration),
                                               PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Provider = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Provider)) << errorText(Provider.takeError());
  auto Target = makeTargetKey(TestFormatID);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());

  const std::array<uint8_t, 4> Input{{'N', 'O', 'B', 'J'}};
  auto Match = (*Provider)->registry().probe(Scope.task(), Input,
                                             "ambiguous.nobj", Target->view());
  ASSERT_FALSE(static_cast<bool>(Match));
  EXPECT_NE(errorText(Match.takeError()).find("ambiguous object format probe"),
            std::string::npos);
}

TEST(PluginObjectReaderTest, TruncatedProbeAndArchiveReturnPreciseErrors) {
  ObjectReaderTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  NevercObjectFormatDescriptor Format{};
  Format.Header = {sizeof(Format), NEVERC_OBJECT_FORMAT_API_MAJOR, UINT16_C(0),
                   0};
  Format.FormatID = TestFormatID;
  Format.CanonicalName = view("nobj");
  Format.Flags = NEVERC_OBJECT_FORMAT_CAN_PROBE | NEVERC_OBJECT_FORMAT_CAN_READ;
  Format.Probe = probeTestObject;
  Format.Reader = readTestObject;
  PluginTargetRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.object-reader-errors";
  Registration.ObjectFormats = ArrayRef<NevercObjectFormatDescriptor>(Format);
  auto Snapshot = PluginTargetRegistry::freeze(ArrayRef(Registration),
                                               PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Provider = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Provider)) << errorText(Provider.takeError());
  auto Target = makeTargetKey(TestFormatID);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());

  const std::array<uint8_t, 2> Truncated{{'N', 'O'}};
  auto TruncatedGraph =
      (*Provider)->read(Scope.task(), Truncated, "short.nobj", *Target);
  ASSERT_FALSE(static_cast<bool>(TruncatedGraph));
  EXPECT_NE(
      errorText(TruncatedGraph.takeError()).find("requires at least 4 bytes"),
      std::string::npos);

  const std::array<uint8_t, 8> Archive{
      {'!', '<', 'a', 'r', 'c', 'h', '>', '\n'}};
  auto ArchiveGraph =
      (*Provider)->read(Scope.task(), Archive, "library.a", *Target);
  ASSERT_FALSE(static_cast<bool>(ArchiveGraph));
  const std::string ArchiveError = errorText(ArchiveGraph.takeError());
  EXPECT_NE(ArchiveError.find("artifact-kind mismatch"), std::string::npos);
  EXPECT_NE(ArchiveError.find("archive"), std::string::npos);
}

TEST(PluginObjectReaderTest, BuiltinLLVMReaderMapsELFCOFFAndMachO) {
  initializeBuiltinTargets();
  ObjectReaderTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Provider = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Provider)) << errorText(Provider.takeError());

  std::set<BuiltinObjectFormat> Seen;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    if (!Seen.insert(Route.ObjectFormat).second)
      continue;
    SCOPED_TRACE(Route.CanonicalTriple.str());
    auto Bytes = emitBuiltinObject(Route);
    ASSERT_TRUE(static_cast<bool>(Bytes)) << errorText(Bytes.takeError());
    auto Target = makeBuiltinTargetKey(Route);
    ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
    auto Graph =
        (*Provider)->read(Scope.task(), *Bytes, "builtin-object.o", *Target);
    ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());
    EXPECT_GT((*Graph)->sectionCount(), 0U);
    EXPECT_GT((*Graph)->symbolCount(), 0U);
    EXPECT_FALSE(verifyPluginObjectGraph(**Graph));
  }
  EXPECT_EQ(Seen.size(), 3U);
}

TEST(PluginObjectReaderTest,
     BuiltinLLVMReaderRejectsAArch64ELFRELWithImplicitAddend) {
  initializeBuiltinTargets();
  ObjectReaderTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  constexpr StringLiteral Assembly = R"(
    .text
    .globl rel_source
    .globl rel_import
rel_source:
    .xword rel_import+3
)";
  auto RelaInput = assembleBuiltinObject(*ELFRoute, Assembly);
  ASSERT_TRUE(static_cast<bool>(RelaInput)) << errorText(RelaInput.takeError());
  auto Target = makeBuiltinTargetKey(*ELFRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());

  auto RelaGraph = (*Reader)->read(Scope.task(), *RelaInput,
                                   "explicit-addend-rela.o", *Target);
  ASSERT_TRUE(static_cast<bool>(RelaGraph)) << errorText(RelaGraph.takeError());
  ASSERT_EQ((*RelaGraph)->relocationCount(), 1U);
  EXPECT_EQ((*RelaGraph)->relocations().front().Addend, 3);

  std::vector<uint8_t> RelInput = *RelaInput;
  Error Converted = convertSingleAArch64RelaToRel(RelInput, 3);
  ASSERT_FALSE(Converted) << errorText(std::move(Converted));
  StringRef RelBytes(reinterpret_cast<const char *>(RelInput.data()),
                     RelInput.size());
  auto ParsedRel = object::ELFFile<object::ELF64LE>::create(RelBytes);
  ASSERT_TRUE(static_cast<bool>(ParsedRel)) << errorText(ParsedRel.takeError());
  auto RelSections = ParsedRel->sections();
  ASSERT_TRUE(static_cast<bool>(RelSections))
      << errorText(RelSections.takeError());
  bool SawREL = false;
  bool SawImplicitAddend = false;
  for (const object::ELF64LE::Shdr &Section : *RelSections) {
    auto Name = ParsedRel->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    SawREL |= Section.sh_type == ELF::SHT_REL;
    if (*Name != ".text")
      continue;
    auto Contents = ParsedRel->getSectionContents(Section);
    ASSERT_TRUE(static_cast<bool>(Contents)) << errorText(Contents.takeError());
    ASSERT_GE(Contents->size(), sizeof(uint64_t));
    uint64_t Site = 0;
    for (unsigned I = 0; I != sizeof(uint64_t); ++I)
      Site |= static_cast<uint64_t>((*Contents)[I]) << (I * 8);
    SawImplicitAddend = Site == 3;
  }
  EXPECT_TRUE(SawREL);
  EXPECT_TRUE(SawImplicitAddend);

  const std::vector<uint8_t> BeforeRead = RelInput;
  auto RelGraph =
      (*Reader)->read(Scope.task(), RelInput, "implicit-addend-rel.o", *Target);
  ASSERT_FALSE(static_cast<bool>(RelGraph));
  const std::string Message = errorText(RelGraph.takeError());
  EXPECT_NE(Message.find("status 18"), std::string::npos);
  EXPECT_EQ(Message.find("detail 0"), std::string::npos);
  EXPECT_EQ(RelInput, BeforeRead);
}

TEST(PluginObjectReaderTest,
     PreservesNonzeroDroppedMappingSymbolRelocationTargetValue) {
  initializeBuiltinTargets();
  ObjectReaderTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  constexpr StringLiteral Assembly = R"(
    .text
    .globl mapping_relocation_source
mapping_relocation_source:
    .xword 0
    .space 8
$x.7:
    nop
    .reloc mapping_relocation_source, R_AARCH64_ABS64, $x.7+3
)";
  auto Input = assembleBuiltinObject(*ELFRoute, Assembly);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  Error Retargeted = retargetSingleAArch64RelaToSymbol(*Input, "$x.7", 16, 3);
  ASSERT_FALSE(Retargeted) << errorText(std::move(Retargeted));

  StringRef RawInput(reinterpret_cast<const char *>(Input->data()),
                     Input->size());
  auto ParsedInput = object::ELFFile<object::ELF64LE>::create(RawInput);
  ASSERT_TRUE(static_cast<bool>(ParsedInput))
      << errorText(ParsedInput.takeError());
  auto InputSections = ParsedInput->sections();
  ASSERT_TRUE(static_cast<bool>(InputSections))
      << errorText(InputSections.takeError());
  bool SawMappingTarget = false;
  for (const object::ELF64LE::Shdr &Section : *InputSections) {
    if (Section.sh_type != ELF::SHT_RELA)
      continue;
    auto SymbolTable = ParsedInput->getSection(Section.sh_link);
    ASSERT_TRUE(static_cast<bool>(SymbolTable))
        << errorText(SymbolTable.takeError());
    auto Symbols = ParsedInput->symbols(*SymbolTable);
    auto Strings = ParsedInput->getStringTableForSymtab(**SymbolTable);
    auto Relocations = ParsedInput->relas(Section);
    ASSERT_TRUE(static_cast<bool>(Symbols)) << errorText(Symbols.takeError());
    ASSERT_TRUE(static_cast<bool>(Strings)) << errorText(Strings.takeError());
    ASSERT_TRUE(static_cast<bool>(Relocations))
        << errorText(Relocations.takeError());
    for (const object::ELF64LE::Rela &Relocation : *Relocations) {
      ASSERT_LT(Relocation.getSymbol(), Symbols->size());
      auto Name = (*Symbols)[Relocation.getSymbol()].getName(*Strings);
      ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
      SawMappingTarget |= *Name == "$x.7" && Relocation.r_addend == 3;
    }
  }
  EXPECT_TRUE(SawMappingTarget);
  auto Target = makeBuiltinTargetKey(*ELFRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Graph = (*Reader)->read(Scope.task(), *Input,
                               "mapping-symbol-relocation.o", *Target);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());
  ASSERT_EQ((*Graph)->relocationCount(), 1U);
  const PluginObjectRelocation &Relocation = (*Graph)->relocations().front();
  EXPECT_EQ(Relocation.TargetKind, NEVERC_OBJECT_RELOCATION_TARGET_SECTION);
  EXPECT_NE((*Graph)->findSection(Relocation.TargetSectionID), nullptr);
  EXPECT_EQ(Relocation.TargetValue, 16U);
  EXPECT_EQ(Relocation.Addend, 3);
  for (const PluginObjectSymbol &Symbol : (*Graph)->symbols())
    EXPECT_FALSE(StringRef(Symbol.Name).starts_with("$x"));

  (*Graph)->issueLayoutProof();
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());
  ObjectOutputDestination Destination = ObjectOutputDestination::memory(
      "mapping-symbol-release.ko", UINT64_C(1) << 20);
  Destination.WritePolicy = ObjectWritePolicy::AndroidKernelRelease;
  auto Image = (*Writer)->beginWrite(Scope.task(), **Graph, Destination);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto Output = (*Image)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(Output)) << errorText(Output.takeError());

  StringRef OutputBytes(reinterpret_cast<const char *>(Output->data()),
                        Output->size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(OutputBytes);
  ASSERT_TRUE(static_cast<bool>(Parsed)) << errorText(Parsed.takeError());
  auto Sections = Parsed->sections();
  ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());
  unsigned RelocationCount = 0;
  for (const object::ELF64LE::Shdr &Section : *Sections) {
    if (Section.sh_type != ELF::SHT_RELA)
      continue;
    auto Relocations = Parsed->relas(Section);
    ASSERT_TRUE(static_cast<bool>(Relocations))
        << errorText(Relocations.takeError());
    for (const object::ELF64LE::Rela &Serialized : *Relocations) {
      ++RelocationCount;
      EXPECT_EQ(Serialized.getType(), ELF::R_AARCH64_ABS64);
      EXPECT_EQ(Serialized.r_addend, 19);
    }
  }
  EXPECT_EQ(RelocationCount, 1U);
  EXPECT_FALSE((*Image)->abort());
}

TEST(PluginObjectReaderTest,
     SameOffsetELFRelocationsKeepNativeOrderAcrossReadAndWrite) {
  initializeBuiltinTargets();
  ObjectReaderTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  constexpr StringLiteral Assembly = R"(
    .text
    .globl same_offset_source
same_offset_source:
    .xword 0
    .reloc same_offset_source, R_AARCH64_ABS64, first_same_offset_target
    .reloc same_offset_source, R_AARCH64_ABS64, second_same_offset_target
)";
  auto Input = assembleBuiltinObject(*ELFRoute, Assembly);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  auto InputOrder = readELF64RelocationTargetNames(*Input, ".rela.text");
  ASSERT_TRUE(static_cast<bool>(InputOrder))
      << errorText(InputOrder.takeError());
  ASSERT_EQ(*InputOrder,
            (std::vector<std::string>{"first_same_offset_target",
                                      "second_same_offset_target"}));

  auto Target = makeBuiltinTargetKey(*ELFRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Graph = (*Reader)->read(Scope.task(), *Input,
                               "same-offset-relocations.o", *Target);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());
  ASSERT_EQ((*Graph)->relocationCount(), 2U);

  std::vector<std::string> GraphOrder;
  for (const PluginObjectRelocation &Relocation : (*Graph)->relocations()) {
    EXPECT_EQ(Relocation.Offset, 0U);
    EXPECT_EQ(Relocation.TargetKind, NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL);
    const PluginObjectSymbol *TargetSymbol =
        (*Graph)->findSymbol(Relocation.TargetSymbolID);
    ASSERT_NE(TargetSymbol, nullptr);
    GraphOrder.push_back(TargetSymbol->Name);
  }
  EXPECT_EQ(GraphOrder, *InputOrder);

  (*Graph)->issueLayoutProof();
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());
  auto Image =
      (*Writer)->beginWrite(Scope.task(), **Graph,
                            ObjectOutputDestination::memory(
                                "same-offset-roundtrip.o", UINT64_C(1) << 20));
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto Output = (*Image)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(Output)) << errorText(Output.takeError());
  auto OutputOrder = readELF64RelocationTargetNames(*Output, ".rela.text");
  ASSERT_TRUE(static_cast<bool>(OutputOrder))
      << errorText(OutputOrder.takeError());
  EXPECT_EQ(*OutputOrder, *InputOrder);
  EXPECT_FALSE((*Image)->abort());
}

TEST(PluginObjectReaderTest,
     PreservesOrdinaryEmptyELFSymbolNameWithExplicitNativeProvenance) {
  initializeBuiltinTargets();
  ObjectReaderTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  constexpr StringLiteral Assembly = R"(
    .text
    .globl ordinary_empty_name
    .type ordinary_empty_name, %function
ordinary_empty_name:
    nop
    .size ordinary_empty_name, .-ordinary_empty_name
)";
  auto Input = assembleBuiltinObject(*ELFRoute, Assembly);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  Error Patched = clearELF64SymbolName(*Input, "ordinary_empty_name");
  ASSERT_FALSE(Patched) << errorText(std::move(Patched));

  auto Target = makeBuiltinTargetKey(*ELFRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Graph =
      (*Reader)->read(Scope.task(), *Input, "ordinary-empty-symbol.o", *Target);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());

  const auto Ordinary =
      llvm::find_if((*Graph)->symbols(), [](const PluginObjectSymbol &Symbol) {
        return Symbol.Type == NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION &&
               Symbol.Binding == NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
      });
  ASSERT_NE(Ordinary, (*Graph)->symbols().end());
  EXPECT_TRUE(Ordinary->Name.empty());
  EXPECT_EQ(Ordinary->Extension.Version, 2U);
  EXPECT_EQ(Ordinary->Extension.Bytes.size(), 48U);
  ASSERT_GE(Ordinary->Extension.Bytes.size(), 48U);
  uint64_t NameState = 0;
  for (unsigned I = 0; I != sizeof(uint64_t); ++I)
    NameState |= static_cast<uint64_t>(Ordinary->Extension.Bytes[40 + I])
                 << (I * 8);
  EXPECT_EQ(NameState, 1U);
  EXPECT_FALSE(verifyPluginObjectGraph(**Graph));
}

TEST(PluginObjectReaderTest,
     ELFRelocatableSymbolValueRemainsSectionRelativeWithNonzeroShAddr) {
  initializeBuiltinTargets();
  ObjectReaderTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  constexpr StringLiteral Assembly = R"(
    .text
    .space 16
    .globl section_relative_value
    .type section_relative_value, %function
section_relative_value:
    nop
    .size section_relative_value, .-section_relative_value
)";
  auto Input = assembleBuiltinObject(*ELFRoute, Assembly);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  Error Patched = patchELF64SectionAddress(*Input, ".text", 8);
  ASSERT_FALSE(Patched) << errorText(std::move(Patched));

  auto Target = makeBuiltinTargetKey(*ELFRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Graph =
      (*Reader)->read(Scope.task(), *Input, "nonzero-sh-addr.o", *Target);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());
  const auto Symbol = llvm::find_if(
      (*Graph)->symbols(), [](const PluginObjectSymbol &Candidate) {
        return Candidate.Name == "section_relative_value";
      });
  ASSERT_NE(Symbol, (*Graph)->symbols().end());
  EXPECT_EQ(Symbol->Value, 16U);
}

TEST(PluginObjectReaderTest,
     ELFExecutableAndSharedArtifactsAreRejectedBeforeGraphRead) {
  initializeBuiltinTargets();
  ObjectReaderTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  constexpr StringLiteral Assembly = R"(
    .text
    .globl image_artifact_symbol
    .type image_artifact_symbol, %function
image_artifact_symbol:
    nop
    .size image_artifact_symbol, .-image_artifact_symbol
)";
  auto Original = assembleBuiltinObject(*ELFRoute, Assembly);
  ASSERT_TRUE(static_cast<bool>(Original)) << errorText(Original.takeError());

  auto Target = makeBuiltinTargetKey(*ELFRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());

  struct Case {
    uint16_t Type;
    StringLiteral Name;
    StringLiteral ExpectedArtifact;
  };
  constexpr Case Cases[] = {
      {ELF::ET_EXEC, "executable", "executable image"},
      {ELF::ET_DYN, "shared", "shared image"},
  };
  for (const Case &TestCase : Cases) {
    SCOPED_TRACE(TestCase.Name.str());
    std::vector<uint8_t> Input = *Original;
    Error TypePatch = patchELF64Type(Input, TestCase.Type);
    ASSERT_FALSE(TypePatch) << errorText(std::move(TypePatch));
    const std::vector<uint8_t> BeforeRead = Input;
    auto Rejected = (*Reader)->read(
        Scope.task(), Input, (Twine(TestCase.Name) + "-artifact.elf").str(),
        *Target);
    ASSERT_FALSE(static_cast<bool>(Rejected));
    const std::string Message = errorText(Rejected.takeError());
    EXPECT_NE(
        Message.find((Twine("artifact-kind mismatch: object Reader cannot "
                            "consume ") +
                      TestCase.ExpectedArtifact)
                         .str()),
        std::string::npos)
        << Message;
    EXPECT_EQ(Input, BeforeRead);
  }
}

TEST(PluginObjectReaderTest,
     RelocationTargetedLocalEmptyELFSymbolRemainsSymbolTargeted) {
  initializeBuiltinTargets();
  ObjectReaderTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  constexpr StringLiteral Assembly = R"(
    .text
    .space 16
    .type local_empty_target, %function
local_empty_target:
    nop
    .balign 8
    .xword external_target
)";
  auto Input = assembleBuiltinObject(*ELFRoute, Assembly);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  Error Retargeted =
      retargetSingleAArch64RelaToSymbol(*Input, "local_empty_target", 16, 0);
  ASSERT_FALSE(Retargeted) << errorText(std::move(Retargeted));
  Error Cleared = clearELF64SymbolName(*Input, "local_empty_target");
  ASSERT_FALSE(Cleared) << errorText(std::move(Cleared));

  auto Target = makeBuiltinTargetKey(*ELFRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Graph =
      (*Reader)->read(Scope.task(), *Input, "local-empty-target.o", *Target);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());

  const auto Empty = llvm::find_if(
      (*Graph)->symbols(), [](const PluginObjectSymbol &Candidate) {
        return Candidate.Name.empty() &&
               Candidate.Binding == NEVERC_OBJECT_SYMBOL_BINDING_LOCAL &&
               Candidate.Type == NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
      });
  ASSERT_NE(Empty, (*Graph)->symbols().end());
  ASSERT_EQ((*Graph)->relocations().size(), 1U);
  const PluginObjectRelocation &Relocation = (*Graph)->relocations().front();
  EXPECT_EQ(Relocation.TargetKind, NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL);
  EXPECT_EQ(Relocation.TargetSymbolID, Empty->ID);
  EXPECT_EQ(Relocation.TargetValue, 0U);
}

TEST(PluginObjectReaderTest, LiteralDollarSymbolNameIsNotAnonymousProvenance) {
  initializeBuiltinTargets();
  ObjectReaderTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  constexpr StringLiteral Assembly = R"(
    .text
    .globl "$symbol.1"
    .type "$symbol.1", %function
"$symbol.1":
    nop
    .size "$symbol.1", .-"$symbol.1"
)";
  auto Input = assembleBuiltinObject(*ELFRoute, Assembly);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  auto Target = makeBuiltinTargetKey(*ELFRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Graph =
      (*Reader)->read(Scope.task(), *Input, "literal-dollar-symbol.o", *Target);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());
  const auto Symbol = llvm::find_if((*Graph)->symbols(),
                                    [](const PluginObjectSymbol &Candidate) {
                                      return Candidate.Name == "$symbol.1";
                                    });
  ASSERT_NE(Symbol, (*Graph)->symbols().end());
  ASSERT_GE(Symbol->Extension.Bytes.size(), 48U);
  uint64_t NameState = 0;
  for (unsigned I = 0; I != sizeof(uint64_t); ++I)
    NameState |= static_cast<uint64_t>(Symbol->Extension.Bytes[40 + I])
                 << (I * 8);
  EXPECT_EQ(NameState, 0U);
}

} // namespace
