#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/ObjectPhaseHooks.h"
#include "neverc/Plugin/Host/ObjectWriterProvider.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "gtest/gtest.h"
#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;

namespace {

constexpr NevercTargetID TestTargetID{UINT64_C(0x4e43505752545247),
                                      UINT64_C(1)};
constexpr NevercObjectFormatID TestFormatID{UINT64_C(0x4e43505752464d54),
                                            UINT64_C(1)};

std::string errorText(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

NevercStringView view(const char *Value) {
  return {Value, std::char_traits<char>::length(Value)};
}

class ObjectWriterTaskScope {
public:
  ObjectWriterTaskScope()
      : Services("neverc-plugin-object-writer-tests", LLVM_VERSION_MAJOR) {}

  bool initialize(StringRef PluginPath = NEVERC_TEST_MINIMAL_PLUGIN,
                  StringRef AdditionalPluginPath = {}) {
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
    auto Query = Services.interfaces().query(
        {NEVERC_INTERFACE_OBJECT_PHASE_HIGH, NEVERC_INTERFACE_OBJECT_PHASE_LOW},
        NEVERC_OBJECT_PHASE_API_MAJOR, NEVERC_OBJECT_PHASE_API_MINOR);
    if (!Query) {
      ADD_FAILURE() << errorText(Query.takeError());
      return false;
    }
    PhaseAPI = static_cast<const NevercObjectPhaseAPI *>(Query->Table);
    if (!PhaseAPI) {
      ADD_FAILURE() << "object phase interface returned a null table";
      return false;
    }
    std::vector<StringRef> Selected;
    for (StringRef Path : {PluginPath, AdditionalPluginPath}) {
      if (Path.empty())
        continue;
      auto Loaded = Services.registry().load(Path);
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

  ~ObjectWriterTaskScope() {
    if (Task)
      EXPECT_FALSE(Task->end());
    if (Session)
      EXPECT_FALSE(Session->end());
    Plan.reset();
    EXPECT_FALSE(Services.shutdown());
  }

  PluginTaskContext &task() { return *Task; }
  const NevercObjectPhaseAPI &phaseAPI() const { return *PhaseAPI; }
  const std::shared_ptr<const PluginModule> &plugin(size_t Index) const {
    return Plugins.at(Index);
  }

private:
  PluginProcessServices Services;
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
  const NevercObjectPhaseAPI *PhaseAPI = nullptr;
  std::vector<std::shared_ptr<const PluginModule>> Plugins;
};

Expected<OwnedTargetKey> makeTargetKey() {
  TargetKeyBuilder Builder;
  Builder.setTargetID(TestTargetID)
      .setTriple("x86_64-neverc-none", "x86_64", "neverc", "none", "")
      .setCPU("generic", "generic")
      .setFeatures({})
      .setABI({UINT64_C(0x4e43505752414249), UINT64_C(1)})
      .setCallingConvention({UINT64_C(0x4e43505752434349), UINT64_C(1)})
      .setObjectFormat(TestFormatID)
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest(
          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
  return Builder.build();
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
      .setCallingConvention({UINT64_C(0x4e43505752434349), Route.TargetID.Low})
      .setObjectFormat(Route.ObjectFormatID)
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest(
          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
  return Builder.build();
}

NevercStatus NEVERC_CALL
writeTestObject(void *, const NevercObjectWriteRequest *Request) {
  if (!Request || !Request->Object || !Request->Binary)
    return {NEVERC_STATUS_INVALID_ARGUMENT, 0, 0};

  NevercObjectSectionHandle Section{};
  NevercStatus Status = Request->Object->GetFirstSection(
      Request->Object->Context, Request->Task, Request->Graph, &Section);
  if (!neverc_status_is_ok(Status)) {
    if (Status.Detail == 0)
      Status.Detail = 101;
    return Status;
  }

  NevercObjectSectionInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_OBJECT_API_MAJOR, NEVERC_OBJECT_API_MINOR,
                 0};
  Status = Request->Object->GetSectionInfo(Request->Object->Context,
                                           Request->Task, Section, &Info);
  if (!neverc_status_is_ok(Status)) {
    if (Status.Detail == 0)
      Status.Detail = 102;
    return Status;
  }

  Status = Request->Binary->Reserve(Request->Binary->Context, Request->Task,
                                    Request->Builder, 4);
  if (!neverc_status_is_ok(Status)) {
    if (Status.Detail == 0)
      Status.Detail = 201;
    return Status;
  }
  Status = Request->Binary->Write(Request->Binary->Context, Request->Task,
                                  Request->Builder, Info.Data);
  if (!neverc_status_is_ok(Status)) {
    if (Status.Detail == 0)
      Status.Detail = 202;
    return Status;
  }

  static const std::array<uint8_t, 4> Magic{{'N', 'O', 'B', 'J'}};
  return Request->Binary->WriteAt(Request->Binary->Context, Request->Task,
                                  Request->Builder, 0,
                                  {Magic.data(), Magic.size()});
}

struct NestedWriterMutationState {
  PluginTaskContext *Task = nullptr;
  std::string ObserverPluginID;
  const NevercMutableBinaryAPI *CachedBinary = nullptr;
  NevercTaskHandle CachedTask{};
  NevercMutableBinaryBuilderHandle CachedBuilder{};
  NevercStatus ObserverDispatch{NEVERC_STATUS_INVALID_STATE, 0, 0};
  NevercStatus MutationAttempt{NEVERC_STATUS_INVALID_STATE, 0, 0};
};

NevercStatus NEVERC_CALL writeAndAttemptMutationFromNestedObserver(
    void *UserData, const NevercObjectWriteRequest *Request) {
  if (!UserData || !Request)
    return {NEVERC_STATUS_INVALID_ARGUMENT, 0, 0};
  auto &State = *static_cast<NestedWriterMutationState *>(UserData);
  if (!State.Task || State.ObserverPluginID.empty())
    return {NEVERC_STATUS_INVALID_STATE, 0, 0};

  NevercStatus Status = writeTestObject(nullptr, Request);
  if (!neverc_status_is_ok(Status))
    return Status;
  State.CachedBinary = Request->Binary;
  State.CachedTask = Request->Task;
  State.CachedBuilder = Request->Builder;

  auto Nested = State.Task->invokeCallback(
      State.ObserverPluginID, "object_writer_nested_read_only_observer", [&] {
        static const std::array<uint8_t, 1> Byte{{UINT8_C(0xa5)}};
        State.MutationAttempt = State.CachedBinary->Append(
            State.CachedBinary->Context, State.CachedTask, State.CachedBuilder,
            {Byte.data(), Byte.size()});
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

struct ObjectWriteRequestObservation {
  unsigned Calls = 0;
  uint16_t Minor = UINT16_MAX;
  uint64_t Flags = UINT64_MAX;
  bool RejectPolicies = false;
};

NevercStatus NEVERC_CALL
observeWriteRequest(void *UserData, const NevercObjectWriteRequest *Request) {
  if (!UserData || !Request)
    return {NEVERC_STATUS_INVALID_ARGUMENT, 0, 0};
  auto &Observation = *static_cast<ObjectWriteRequestObservation *>(UserData);
  ++Observation.Calls;
  Observation.Minor = Request->Header.Minor;
  Observation.Flags = Request->Header.Flags;
  if (Observation.RejectPolicies && Request->Header.Flags != 0)
    return {NEVERC_STATUS_CAPABILITY_UNAVAILABLE, 0, 909};
  return writeTestObject(nullptr, Request);
}

NevercStatus NEVERC_CALL
writeEditedBinary(void *, const NevercObjectWriteRequest *Request) {
  if (!Request || !Request->Binary || !Request->Binary->ReadAt ||
      !Request->Binary->Insert || !Request->Binary->Append ||
      !Request->Binary->Resize)
    return {NEVERC_STATUS_MISSING_INTERFACE, 0, 0};

  static const std::array<uint8_t, 4> Initial{{'A', 'B', 'C', 'D'}};
  NevercStatus Status = Request->Binary->Append(
      Request->Binary->Context, Request->Task, Request->Builder,
      {Initial.data(), Initial.size()});
  if (!neverc_status_is_ok(Status))
    return Status;
  static const std::array<uint8_t, 2> Inserted{{'x', 'y'}};
  Status = Request->Binary->Insert(Request->Binary->Context, Request->Task,
                                   Request->Builder, 2,
                                   {Inserted.data(), Inserted.size()});
  if (!neverc_status_is_ok(Status))
    return Status;
  Status = Request->Binary->Resize(Request->Binary->Context, Request->Task,
                                   Request->Builder, 8);
  if (!neverc_status_is_ok(Status))
    return Status;
  static const std::array<uint8_t, 2> Suffix{{'Z', '!'}};
  Status = Request->Binary->WriteAt(Request->Binary->Context, Request->Task,
                                    Request->Builder, 6,
                                    {Suffix.data(), Suffix.size()});
  if (!neverc_status_is_ok(Status))
    return Status;

  std::array<uint8_t, 4> Readback{};
  Status = Request->Binary->ReadAt(Request->Binary->Context, Request->Task,
                                   Request->Builder, 1,
                                   {Readback.data(), Readback.size()});
  if (!neverc_status_is_ok(Status) ||
      Readback != std::array<uint8_t, 4>{{'B', 'x', 'y', 'C'}})
    return {NEVERC_STATUS_PLUGIN_FAILURE, 0, 210};
  Status = Request->Binary->ReadAt(Request->Binary->Context, Request->Task,
                                   Request->Builder, 7, {Readback.data(), 2});
  if (Status.Code != NEVERC_STATUS_INVALID_ARGUMENT)
    return {NEVERC_STATUS_PLUGIN_FAILURE, 0, 211};
  uint64_t Size = 0;
  Status = Request->Binary->Tell(Request->Binary->Context, Request->Task,
                                 Request->Builder, &Size);
  if (!neverc_status_is_ok(Status) || Size != 8)
    return {NEVERC_STATUS_PLUGIN_FAILURE, 0, 212};
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL writePastReservedImage(
    void *UserData, const NevercObjectWriteRequest *Request) {
  if (!UserData || !Request || !Request->Binary)
    return {NEVERC_STATUS_INVALID_ARGUMENT, 0, 0};
  NevercStatus Status = Request->Binary->Reserve(
      Request->Binary->Context, Request->Task, Request->Builder, 4);
  if (!neverc_status_is_ok(Status))
    return Status;
  const uint8_t Byte = UINT8_C(0xff);
  Status = Request->Binary->WriteAt(Request->Binary->Context, Request->Task,
                                    Request->Builder, 4, {&Byte, 1});
  *static_cast<NevercStatusCode *>(UserData) = Status.Code;
  if (Status.Detail == 0)
    Status.Detail = 301;
  return Status;
}

NevercStatus NEVERC_CALL
writeThenFail(void *UserData, const NevercObjectWriteRequest *Request) {
  NevercStatus Status = writeTestObject(UserData, Request);
  if (!neverc_status_is_ok(Status))
    return Status;
  return {NEVERC_STATUS_PLUGIN_FAILURE, 0, 501};
}

struct RejectFinalVerifyData {
  const NevercObjectPhaseAPI *API = nullptr;
  NevercObjectImageState ImageState = 0;
  NevercOutputState OutputState = 0;
  unsigned Calls = 0;
};

struct RelayoutData {
  const NevercObjectPhaseAPI *API = nullptr;
  std::array<uint8_t, 3> Replacement{
      {UINT8_C(0x90), UINT8_C(0x90), UINT8_C(0xc3)}};
  unsigned Calls = 0;
};

struct PostWriteData {
  const NevercObjectPhaseAPI *API = nullptr;
  NevercObjectImageState ImageState = 0;
  NevercOutputState OutputState = 0;
  std::string Provenance;
  NevercObjectLayoutProofInfo LayoutReport{};
  NevercBool HasLayoutReport = NEVERC_FALSE;
  unsigned Calls = 0;
};

struct ReadOnlyObserverData {
  const NevercObjectPhaseAPI *API = nullptr;
  NevercStatusCode MutationStatus = NEVERC_STATUS_OK;
  unsigned Calls = 0;
};

NevercStatus NEVERC_CALL attemptObserverMutation(const NevercPhaseFrame *Frame,
                                                 NevercObserverPoint Point,
                                                 void *UserData) {
  auto *Data = static_cast<ReadOnlyObserverData *>(UserData);
  if (!Frame || !Data || !Data->API || Point != NEVERC_OBSERVER_BEFORE)
    return {NEVERC_STATUS_INVALID_ARGUMENT, 0, 0};
  NevercObjectPhaseGraphInfo GraphInfo{};
  GraphInfo.Header = {sizeof(GraphInfo), NEVERC_OBJECT_PHASE_API_MAJOR,
                      NEVERC_OBJECT_PHASE_API_MINOR, 0};
  NevercStatus Status =
      Data->API->GetGraph(Data->API->Context, Frame, Frame->Input, &GraphInfo);
  if (!neverc_status_is_ok(Status))
    return Status;
  NevercObjectMutationHandle Mutation{};
  Status = GraphInfo.Object->BeginMutation(
      GraphInfo.Object->Context, Frame->Task, GraphInfo.Graph, &Mutation);
  Data->MutationStatus = Status.Code;
  ++Data->Calls;
  return Status.Code == NEVERC_STATUS_POLICY_VIOLATION
             ? neverc_status_ok()
             : NevercStatus{NEVERC_STATUS_PLUGIN_FAILURE, 0, 0};
}

NevercStatus NEVERC_CALL rewritePostWriteByte(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  auto *Data = static_cast<PostWriteData *>(UserData);
  if (!Frame || !Continuation || !OutResult || !Data || !Data->API)
    return {NEVERC_STATUS_INVALID_ARGUMENT, 0, 0};

  NevercObjectImageInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_OBJECT_PHASE_API_MAJOR,
                 NEVERC_OBJECT_PHASE_API_MINOR, 0};
  NevercStatus Status =
      Data->API->GetImage(Data->API->Context, Frame, Frame->Input, &Info);
  if (!neverc_status_is_ok(Status))
    return Status;
  Data->ImageState = Info.State;
  Data->OutputState = Info.OutputState;
  Data->Provenance.assign(Info.Provenance.Data,
                          static_cast<size_t>(Info.Provenance.Length));
  Data->HasLayoutReport = Info.HasLayoutReport;
  Data->LayoutReport = Info.LayoutReport;
  ++Data->Calls;
  if (!Info.Binary || neverc_handle_is_null(Info.Builder))
    return {NEVERC_STATUS_MISSING_INTERFACE, 0, 0};

  const uint8_t Replacement = UINT8_C(0xcc);
  Status = Info.Binary->WriteAt(Info.Binary->Context, Frame->Task, Info.Builder,
                                4, {&Replacement, 1});
  if (!neverc_status_is_ok(Status))
    return Status;

  NevercPhaseResult Downstream{};
  Downstream.Header = {sizeof(Downstream), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  Status = Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (!neverc_status_is_ok(Status))
    return Status;
  *OutResult = {};
  OutResult->Header = {sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL mutatePostLayoutOnce(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  auto *Data = static_cast<RelayoutData *>(UserData);
  if (!Frame || !Continuation || !OutResult || !Data || !Data->API)
    return {NEVERC_STATUS_INVALID_ARGUMENT, 0, 0};

  ++Data->Calls;
  if (Data->Calls == 1) {
    NevercObjectPhaseGraphInfo GraphInfo{};
    GraphInfo.Header = {sizeof(GraphInfo), NEVERC_OBJECT_PHASE_API_MAJOR,
                        NEVERC_OBJECT_PHASE_API_MINOR, 0};
    NevercStatus Status = Data->API->GetGraph(Data->API->Context, Frame,
                                              Frame->Input, &GraphInfo);
    if (!neverc_status_is_ok(Status))
      return Status;

    NevercObjectSectionHandle Section{};
    Status = GraphInfo.Object->GetFirstSection(
        GraphInfo.Object->Context, Frame->Task, GraphInfo.Graph, &Section);
    if (!neverc_status_is_ok(Status))
      return Status;
    NevercObjectSectionDescriptor Descriptor{};
    Descriptor.Header = {sizeof(Descriptor), NEVERC_OBJECT_API_MAJOR,
                         NEVERC_OBJECT_API_MINOR, 0};
    Status = GraphInfo.Object->GetSectionInfo(
        GraphInfo.Object->Context, Frame->Task, Section, &Descriptor);
    if (!neverc_status_is_ok(Status))
      return Status;
    Descriptor.Data = {Data->Replacement.data(), Data->Replacement.size()};

    NevercObjectMutationHandle Mutation{};
    Status = GraphInfo.Object->BeginMutation(
        GraphInfo.Object->Context, Frame->Task, GraphInfo.Graph, &Mutation);
    if (!neverc_status_is_ok(Status))
      return Status;
    Status = GraphInfo.Object->ReplaceSection(
        GraphInfo.Object->Context, Frame->Task, Mutation, Section, &Descriptor);
    if (!neverc_status_is_ok(Status)) {
      (void)GraphInfo.Object->AbandonMutation(GraphInfo.Object->Context,
                                              Frame->Task, Mutation);
      return Status;
    }
    Status = GraphInfo.Object->CommitMutation(GraphInfo.Object->Context,
                                              Frame->Task, Mutation);
    if (!neverc_status_is_ok(Status))
      return Status;
  }

  NevercPhaseResult Downstream{};
  Downstream.Header = {sizeof(Downstream), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  NevercStatus Status =
      Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (!neverc_status_is_ok(Status))
    return Status;
  *OutResult = {};
  OutResult->Header = {sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL rejectFinalObjectImage(const NevercPhaseFrame *Frame,
                                                NevercObserverPoint Point,
                                                void *UserData) {
  auto *Data = static_cast<RejectFinalVerifyData *>(UserData);
  if (!Frame || !Data || !Data->API || Point != NEVERC_OBSERVER_BEFORE)
    return {NEVERC_STATUS_INVALID_ARGUMENT, 0, 0};
  NevercObjectImageInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_OBJECT_PHASE_API_MAJOR,
                 NEVERC_OBJECT_PHASE_API_MINOR, 0};
  NevercStatus Status =
      Data->API->GetImage(Data->API->Context, Frame, Frame->Input, &Info);
  if (!neverc_status_is_ok(Status))
    return Status;
  Data->ImageState = Info.State;
  Data->OutputState = Info.OutputState;
  ++Data->Calls;
  return {NEVERC_STATUS_VERIFICATION_FAILED, 0, 401};
}

TEST(PluginObjectWriterTest,
     NegotiatesWriterPolicyMinorAndFailsClosedBeforeOutput) {
  ObjectWriterTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginObjectGraph Graph(std::move(*Target));
  PluginObjectSection Section;
  Section.ID = Graph.allocateEntityID();
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 1;
  Section.Data = {UINT8_C(0x2a), UINT8_C(0xc3)};
  Graph.sections().push_back(std::move(Section));
  Graph.issueLayoutProof();

  ObjectWriteRequestObservation OldObservation;
  NevercObjectFormatDescriptor OldFormat{};
  OldFormat.Header = {sizeof(OldFormat), NEVERC_OBJECT_FORMAT_API_MAJOR,
                      UINT16_C(0), 0};
  OldFormat.FormatID = TestFormatID;
  OldFormat.CanonicalName = view("nobj");
  OldFormat.DefaultExtension = view(".nobj");
  OldFormat.Flags = NEVERC_OBJECT_FORMAT_CAN_WRITE;
  OldFormat.Writer = observeWriteRequest;
  OldFormat.UserData = &OldObservation;
  PluginTargetRegistrationView OldRegistration;
  OldRegistration.PluginID = "org.neverc.test.object-writer.minor0";
  OldRegistration.ObjectFormats =
      ArrayRef<NevercObjectFormatDescriptor>(OldFormat);
  auto OldSnapshot = PluginTargetRegistry::freeze(ArrayRef(OldRegistration),
                                                  PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(OldSnapshot))
      << errorText(OldSnapshot.takeError());
  ASSERT_EQ((*OldSnapshot)->objectFormats().size(), 1U);
  EXPECT_EQ((*OldSnapshot)->objectFormats().front().APIMinor, 0U);
  auto OldProvider = ObjectWriterProvider::create(*OldSnapshot);
  ASSERT_TRUE(static_cast<bool>(OldProvider))
      << errorText(OldProvider.takeError());

  auto DefaultImage = (*OldProvider)
                          ->beginWrite(Scope.task(), Graph,
                                       ObjectOutputDestination::memory(
                                           "minor0-default.nobj", 1024));
  ASSERT_TRUE(static_cast<bool>(DefaultImage))
      << errorText(DefaultImage.takeError());
  EXPECT_EQ(OldObservation.Calls, 1U);
  EXPECT_EQ(OldObservation.Minor, 0U);
  EXPECT_EQ(OldObservation.Flags, 0U);
  EXPECT_FALSE((*DefaultImage)->abort());

  auto ExpectOldPolicyRejected = [&](StringRef Name, ObjectWritePolicy Policy,
                                     bool DropDebugInfo) {
    ObjectOutputDestination Destination =
        ObjectOutputDestination::memory(Name, 1024);
    Destination.WritePolicy = Policy;
    Destination.DropDebugInfo = DropDebugInfo;
    auto Rejected =
        (*OldProvider)->beginWrite(Scope.task(), Graph, Destination);
    ASSERT_FALSE(static_cast<bool>(Rejected));
    EXPECT_NE(errorText(Rejected.takeError()).find("API minor 1"),
              std::string::npos);
    EXPECT_EQ(OldObservation.Calls, 1U);
    EXPECT_FALSE(findPluginMemoryOutput(Scope.task(), Name).has_value());
  };
  ExpectOldPolicyRejected("minor0-canonical.nobj",
                          ObjectWritePolicy::CanonicalELFTables, false);
  ExpectOldPolicyRejected("minor0-release.nobj",
                          ObjectWritePolicy::AndroidKernelRelease, false);
  ExpectOldPolicyRejected("minor0-debug.nobj",
                          ObjectWritePolicy::CanonicalELFTables, true);

  ObjectWriteRequestObservation NewObservation;
  NewObservation.RejectPolicies = true;
  NevercObjectFormatDescriptor NewFormat = OldFormat;
  NewFormat.Header.Minor = NEVERC_OBJECT_FORMAT_API_MINOR;
  NewFormat.CanonicalName = view("nobj.v1");
  NewFormat.UserData = &NewObservation;
  PluginTargetRegistrationView NewRegistration;
  NewRegistration.PluginID = "org.neverc.test.object-writer.minor1";
  NewRegistration.ObjectFormats =
      ArrayRef<NevercObjectFormatDescriptor>(NewFormat);
  auto NewSnapshot = PluginTargetRegistry::freeze(ArrayRef(NewRegistration),
                                                  PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(NewSnapshot))
      << errorText(NewSnapshot.takeError());
  ASSERT_EQ((*NewSnapshot)->objectFormats().size(), 1U);
  EXPECT_EQ((*NewSnapshot)->objectFormats().front().APIMinor, 1U);
  auto NewProvider = ObjectWriterProvider::create(*NewSnapshot);
  ASSERT_TRUE(static_cast<bool>(NewProvider))
      << errorText(NewProvider.takeError());

  ObjectOutputDestination CanonicalDestination =
      ObjectOutputDestination::memory("minor1-canonical.nobj", 1024);
  CanonicalDestination.WritePolicy = ObjectWritePolicy::CanonicalELFTables;
  CanonicalDestination.DropDebugInfo = true;
  auto CanonicalRejected =
      (*NewProvider)->beginWrite(Scope.task(), Graph, CanonicalDestination);
  ASSERT_FALSE(static_cast<bool>(CanonicalRejected));
  EXPECT_NE(errorText(CanonicalRejected.takeError()).find("detail 909"),
            std::string::npos);
  EXPECT_EQ(NewObservation.Calls, 1U);
  EXPECT_EQ(NewObservation.Minor, 1U);
  EXPECT_EQ(NewObservation.Flags, NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES |
                                      NEVERC_OBJECT_WRITE_DROP_DEBUG_INFO);
  EXPECT_FALSE(findPluginMemoryOutput(Scope.task(), "minor1-canonical.nobj")
                   .has_value());

  ObjectOutputDestination ReleaseDestination =
      ObjectOutputDestination::memory("minor1-release.nobj", 1024);
  ReleaseDestination.WritePolicy = ObjectWritePolicy::AndroidKernelRelease;
  auto ReleaseRejected =
      (*NewProvider)->beginWrite(Scope.task(), Graph, ReleaseDestination);
  ASSERT_FALSE(static_cast<bool>(ReleaseRejected));
  EXPECT_NE(errorText(ReleaseRejected.takeError()).find("detail 909"),
            std::string::npos);
  EXPECT_EQ(NewObservation.Calls, 2U);
  EXPECT_EQ(NewObservation.Minor, 1U);
  EXPECT_EQ(NewObservation.Flags,
            NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES |
                NEVERC_OBJECT_WRITE_ANDROID_KERNEL_RELEASE);
  EXPECT_FALSE(
      findPluginMemoryOutput(Scope.task(), "minor1-release.nobj").has_value());
}

TEST(PluginObjectWriterTest,
     NativeImagePassthroughDoesNotNegotiateGraphWriterPolicy) {
  ObjectWriterTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  NevercObjectFormatDescriptor Format{};
  Format.Header = {sizeof(Format), NEVERC_OBJECT_FORMAT_API_MAJOR, UINT16_C(0),
                   0};
  Format.FormatID = TestFormatID;
  Format.CanonicalName = view("nobj.native");
  Format.DefaultExtension = view(".nobj");
  PluginTargetRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.object-writer.native";
  Registration.ObjectFormats = ArrayRef<NevercObjectFormatDescriptor>(Format);
  auto Snapshot = PluginTargetRegistry::freeze(ArrayRef(Registration),
                                               PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Provider = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Provider)) << errorText(Provider.takeError());

  const std::array<uint8_t, 8> NativeBytes{
      {'N', 'O', 'B', 'J', UINT8_C(1), UINT8_C(2), UINT8_C(3), UINT8_C(4)}};
  ObjectOutputDestination Destination =
      ObjectOutputDestination::memory("native-policy-bypass.nobj", 1024);
  Destination.WritePolicy = ObjectWritePolicy::AndroidKernelRelease;
  Destination.DropDebugInfo = true;
  auto Image = (*Provider)->beginImage(Scope.task(), TestFormatID, TestTargetID,
                                       17, NativeBytes, Destination);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto Pending = (*Image)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(Pending)) << errorText(Pending.takeError());
  EXPECT_EQ(std::vector<uint8_t>(Pending->begin(), Pending->end()),
            std::vector<uint8_t>(NativeBytes.begin(), NativeBytes.end()));
  EXPECT_FALSE((*Image)->abort());
}

TEST(PluginObjectWriterTest,
     MutableBinarySupportsCheckedReadInsertAppendAndResize) {
  ObjectWriterTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  NevercObjectFormatDescriptor Format{};
  Format.Header = {sizeof(Format), NEVERC_OBJECT_FORMAT_API_MAJOR, UINT16_C(0),
                   0};
  Format.FormatID = TestFormatID;
  Format.CanonicalName = view("nobj");
  Format.DefaultExtension = view(".nobj");
  Format.Flags = NEVERC_OBJECT_FORMAT_CAN_WRITE;
  Format.Writer = writeEditedBinary;

  PluginTargetRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.object-writer";
  Registration.ObjectFormats = ArrayRef<NevercObjectFormatDescriptor>(Format);
  auto Snapshot = PluginTargetRegistry::freeze(ArrayRef(Registration),
                                               PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Provider = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Provider)) << errorText(Provider.takeError());

  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginObjectGraph Graph(std::move(*Target));
  Graph.issueLayoutProof();

  auto Image = (*Provider)->write(
      Scope.task(), Graph,
      ObjectOutputDestination::memory("edited.nobj", UINT64_C(1024)));
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  ASSERT_FALSE((*Image)->verify());
  auto Committed = (*Image)->commit();
  ASSERT_TRUE(static_cast<bool>(Committed)) << errorText(Committed.takeError());

  auto Published = findPluginMemoryOutput(Scope.task(), "edited.nobj");
  ASSERT_TRUE(Published.has_value());
  const std::array<uint8_t, 8> Expected{
      {'A', 'B', 'x', 'y', 'C', 'D', 'Z', '!'}};
  EXPECT_EQ(Published->Bytes,
            (std::vector<uint8_t>(Expected.begin(), Expected.end())));
}

TEST(PluginObjectWriterTest,
     NestedReadOnlyCallbackCannotReuseThirdPartyWriterMutationFacade) {
  ObjectWriterTaskScope Scope;
  ASSERT_TRUE(Scope.initialize(NEVERC_TEST_MINIMAL_PLUGIN,
                               NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN));

  NestedWriterMutationState State;
  State.Task = &Scope.task();
  State.ObserverPluginID = Scope.plugin(1)->descriptor().PluginID;

  NevercObjectFormatDescriptor Format{};
  Format.Header = {sizeof(Format), NEVERC_OBJECT_FORMAT_API_MAJOR,
                   NEVERC_OBJECT_FORMAT_API_MINOR, 0};
  Format.FormatID = TestFormatID;
  Format.CanonicalName = view("nested-writer-capability");
  Format.DefaultExtension = view(".nwc");
  Format.Flags = NEVERC_OBJECT_FORMAT_CAN_WRITE;
  Format.Writer = writeAndAttemptMutationFromNestedObserver;
  Format.UserData = &State;

  PluginTargetRegistrationView Registration;
  Registration.PluginID = Scope.plugin(0)->descriptor().PluginID;
  Registration.Owner = Scope.plugin(0);
  Registration.ObjectFormats = ArrayRef<NevercObjectFormatDescriptor>(Format);
  auto Snapshot = PluginTargetRegistry::freeze(ArrayRef(Registration),
                                               PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Provider = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Provider)) << errorText(Provider.takeError());

  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginObjectGraph Graph(std::move(*Target));
  PluginObjectSection Section;
  Section.ID = Graph.allocateEntityID();
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 1;
  Section.Data = {UINT8_C(0x2a), UINT8_C(0xc3)};
  Graph.sections().push_back(std::move(Section));
  Graph.issueLayoutProof();

  auto Image = (*Provider)->beginWrite(
      Scope.task(), Graph,
      ObjectOutputDestination::memory("nested-writer.nwc", UINT64_C(1024)));
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  EXPECT_EQ(State.ObserverDispatch.Code, NEVERC_STATUS_OK);
  EXPECT_EQ(State.MutationAttempt.Code, NEVERC_STATUS_POLICY_VIOLATION);
  EXPECT_FALSE((*Image)->abort());
}

TEST(PluginObjectWriterTest, ProducesVerifiedCandidateBeforeAtomicHostCommit) {
  ObjectWriterTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  NevercObjectFormatDescriptor Format{};
  Format.Header = {sizeof(Format), NEVERC_OBJECT_FORMAT_API_MAJOR, UINT16_C(0),
                   0};
  Format.FormatID = TestFormatID;
  Format.CanonicalName = view("nobj");
  Format.DefaultExtension = view(".nobj");
  Format.Flags = NEVERC_OBJECT_FORMAT_CAN_WRITE;
  Format.Writer = writeTestObject;

  PluginTargetRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.object-writer";
  Registration.ObjectFormats = ArrayRef<NevercObjectFormatDescriptor>(Format);
  auto Snapshot = PluginTargetRegistry::freeze(ArrayRef(Registration),
                                               PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Provider = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Provider)) << errorText(Provider.takeError());

  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginObjectGraph Graph(std::move(*Target));
  PluginObjectSection Section;
  Section.ID = Graph.allocateEntityID();
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 1;
  Section.Data = {UINT8_C(0x2a), UINT8_C(0xc3)};
  Graph.sections().push_back(std::move(Section));
  Graph.issueLayoutProof();

  auto Image = (*Provider)->write(
      Scope.task(), Graph,
      ObjectOutputDestination::memory("answer.nobj", UINT64_C(1024)));
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  EXPECT_EQ((*Image)->state(), PluginObjectImageState::Candidate);
  EXPECT_FALSE(findPluginMemoryOutput(Scope.task(), "answer.nobj").has_value());

  auto PrematureCommit = (*Image)->commit();
  EXPECT_FALSE(static_cast<bool>(PrematureCommit));
  consumeError(PrematureCommit.takeError());
  EXPECT_FALSE(findPluginMemoryOutput(Scope.task(), "answer.nobj").has_value());

  ASSERT_FALSE((*Image)->verify());
  EXPECT_EQ((*Image)->state(), PluginObjectImageState::Verified);
  auto Committed = (*Image)->commit();
  ASSERT_TRUE(static_cast<bool>(Committed)) << errorText(Committed.takeError());
  EXPECT_EQ(Committed->State, NEVERC_OUTPUT_COMMITTED);
  EXPECT_EQ(Committed->PublicationGeneration, 1U);
  EXPECT_EQ((*Image)->state(), PluginObjectImageState::Committed);

  auto Published = findPluginMemoryOutput(Scope.task(), "answer.nobj");
  ASSERT_TRUE(Published.has_value());
  const std::array<uint8_t, 6> Expected{
      {'N', 'O', 'B', 'J', UINT8_C(0x2a), UINT8_C(0xc3)}};
  EXPECT_EQ(Published->Bytes,
            (std::vector<uint8_t>(Expected.begin(), Expected.end())));
}

TEST(PluginObjectWriterTest,
     RejectsRandomWritePastReservedImageAndAbortsCandidate) {
  ObjectWriterTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  NevercStatusCode ObservedStatus = NEVERC_STATUS_OK;
  NevercObjectFormatDescriptor Format{};
  Format.Header = {sizeof(Format), NEVERC_OBJECT_FORMAT_API_MAJOR, UINT16_C(0),
                   0};
  Format.FormatID = TestFormatID;
  Format.CanonicalName = view("nobj");
  Format.DefaultExtension = view(".nobj");
  Format.Flags = NEVERC_OBJECT_FORMAT_CAN_WRITE;
  Format.Writer = writePastReservedImage;
  Format.UserData = &ObservedStatus;

  PluginTargetRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.object-writer";
  Registration.ObjectFormats = ArrayRef<NevercObjectFormatDescriptor>(Format);
  auto Snapshot = PluginTargetRegistry::freeze(ArrayRef(Registration),
                                               PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Provider = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Provider)) << errorText(Provider.takeError());

  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginObjectGraph Graph(std::move(*Target));
  Graph.issueLayoutProof();

  auto Image = (*Provider)->write(
      Scope.task(), Graph,
      ObjectOutputDestination::memory("overflow.nobj", UINT64_C(1024)));
  ASSERT_FALSE(static_cast<bool>(Image));
  EXPECT_NE(errorText(Image.takeError()).find("detail 301"), std::string::npos);
  EXPECT_EQ(ObservedStatus, NEVERC_STATUS_INVALID_ARGUMENT);
  EXPECT_FALSE(
      findPluginMemoryOutput(Scope.task(), "overflow.nobj").has_value());
}

TEST(PluginObjectWriterTest,
     WriterFailureDiscardsStagingAndPreservesExistingFile) {
  ObjectWriterTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  NevercObjectFormatDescriptor Format{};
  Format.Header = {sizeof(Format), NEVERC_OBJECT_FORMAT_API_MAJOR, UINT16_C(0),
                   0};
  Format.FormatID = TestFormatID;
  Format.CanonicalName = view("nobj");
  Format.DefaultExtension = view(".nobj");
  Format.Flags = NEVERC_OBJECT_FORMAT_CAN_WRITE;
  Format.Writer = writeThenFail;

  PluginTargetRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.object-writer";
  Registration.ObjectFormats = ArrayRef<NevercObjectFormatDescriptor>(Format);
  auto Snapshot = PluginTargetRegistry::freeze(ArrayRef(Registration),
                                               PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Provider = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Provider)) << errorText(Provider.takeError());

  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginObjectGraph Graph(std::move(*Target));
  PluginObjectSection Section;
  Section.ID = Graph.allocateEntityID();
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 1;
  Section.Data = {UINT8_C(0x90), UINT8_C(0xc3)};
  Graph.sections().push_back(std::move(Section));
  Graph.issueLayoutProof();

  SmallString<128> Directory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory("neverc-object-writer-failure",
                                              Directory));
  auto RemoveDirectory =
      make_scope_exit([&] { (void)sys::fs::remove_directories(Directory); });
  SmallString<160> OutputPath(Directory);
  sys::path::append(OutputPath, "answer.nobj");
  {
    std::error_code Error;
    raw_fd_ostream Existing(OutputPath, Error);
    ASSERT_FALSE(Error);
    Existing << "existing";
  }

  auto Image = (*Provider)->write(
      Scope.task(), Graph,
      ObjectOutputDestination::file(OutputPath, UINT64_C(1024)));
  ASSERT_FALSE(static_cast<bool>(Image));
  EXPECT_NE(errorText(Image.takeError()).find("detail 501"), std::string::npos);
  auto Contents = MemoryBuffer::getFile(OutputPath);
  ASSERT_TRUE(static_cast<bool>(Contents));
  EXPECT_EQ((*Contents)->getBuffer(), "existing");
}

TEST(PluginObjectWriterTest,
     BuiltinWritersEmitELFCOFFAndMachORelocatableImages) {
  static std::once_flag InitializeTargets;
  std::call_once(InitializeTargets, [] {
    InitializeAllTargetInfos();
    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmParsers();
    InitializeAllAsmPrinters();
  });

  ObjectWriterTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Provider = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Provider)) << errorText(Provider.takeError());

  std::array<bool, 3> Tested{{false, false, false}};
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const size_t FormatIndex =
        Route.ObjectFormat == BuiltinObjectFormat::ELF    ? 0
        : Route.ObjectFormat == BuiltinObjectFormat::COFF ? 1
                                                          : 2;
    if (!Route.SupportsObject || Tested[FormatIndex])
      continue;

    auto Target = makeBuiltinTargetKey(Route);
    ASSERT_TRUE(static_cast<bool>(Target))
        << Route.CanonicalName.str() << ": " << errorText(Target.takeError());
    PluginObjectGraph Graph(std::move(*Target));
    PluginObjectSection Section;
    Section.ID = Graph.allocateEntityID();
    Section.Name =
        Route.ObjectFormat == BuiltinObjectFormat::MachO ? "__text" : ".text";
    Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
    Section.Flags =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
    Section.Alignment = 1;
    Section.Data = {UINT8_C(0xc3)};
    const uint64_t SectionID = Section.ID;
    Graph.sections().push_back(std::move(Section));

    PluginObjectSymbol Symbol;
    Symbol.ID = Graph.allocateEntityID();
    Symbol.Name = Route.ObjectFormat == BuiltinObjectFormat::MachO
                      ? "_builtin_writer"
                      : "builtin_writer";
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
    Symbol.SectionID = SectionID;
    Symbol.Size = 1;
    Symbol.Alignment = 1;
    Graph.symbols().push_back(std::move(Symbol));
    Graph.issueLayoutProof();

    const std::string OutputName =
        "builtin-" + std::to_string(FormatIndex) + ".o";
    auto Image = (*Provider)->write(
        Scope.task(), Graph,
        ObjectOutputDestination::memory(OutputName, UINT64_C(1) << 20));
    ASSERT_TRUE(static_cast<bool>(Image))
        << Route.CanonicalName.str() << ": " << errorText(Image.takeError());
    ASSERT_FALSE((*Image)->verify());
    auto Committed = (*Image)->commit();
    ASSERT_TRUE(static_cast<bool>(Committed))
        << Route.CanonicalName.str() << ": "
        << errorText(Committed.takeError());
    auto Published = findPluginMemoryOutput(Scope.task(), OutputName);
    ASSERT_TRUE(Published.has_value());
    StringRef Bytes(reinterpret_cast<const char *>(Published->Bytes.data()),
                    Published->Bytes.size());
    const file_magic Expected =
        Route.ObjectFormat == BuiltinObjectFormat::ELF
            ? file_magic::elf_relocatable
        : Route.ObjectFormat == BuiltinObjectFormat::COFF
            ? file_magic::coff_object
            : file_magic::macho_object;
    EXPECT_EQ(identify_magic(Bytes), Expected) << Route.CanonicalName.str();
    Tested[FormatIndex] = true;
  }
  EXPECT_TRUE(Tested[0]);
  EXPECT_TRUE(Tested[1]);
  EXPECT_TRUE(Tested[2]);
}

TEST(
    PluginObjectWriterTest,
    BuiltinWritersRejectAnonymousSymbolsBeforeSinkAndAllowSameDestinationRetry) {
  static std::once_flag InitializeTargets;
  std::call_once(InitializeTargets, [] {
    InitializeAllTargetInfos();
    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmParsers();
    InitializeAllAsmPrinters();
  });

  ObjectWriterTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Provider = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Provider)) << errorText(Provider.takeError());

  std::array<bool, 3> Tested{{false, false, false}};
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const size_t FormatIndex =
        Route.ObjectFormat == BuiltinObjectFormat::ELF    ? 0
        : Route.ObjectFormat == BuiltinObjectFormat::COFF ? 1
                                                          : 2;
    if (!Route.SupportsObject || Tested[FormatIndex])
      continue;

    auto Target = makeBuiltinTargetKey(Route);
    ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
    PluginObjectGraph Graph(std::move(*Target));
    PluginObjectSection Section;
    Section.ID = Graph.allocateEntityID();
    Section.Name =
        Route.ObjectFormat == BuiltinObjectFormat::MachO ? "__text" : ".text";
    Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
    Section.Flags =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
    Section.Alignment = 1;
    Section.Data = {UINT8_C(0xc3)};
    const uint64_t SectionID = Section.ID;
    Graph.sections().push_back(std::move(Section));

    PluginObjectSymbol Symbol;
    Symbol.ID = Graph.allocateEntityID();
    Symbol.Name.clear();
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
    Symbol.SectionID = SectionID;
    Symbol.Size = 1;
    Symbol.Alignment = 1;
    Graph.symbols().push_back(std::move(Symbol));
    Graph.issueLayoutProof();
    ASSERT_FALSE(verifyPluginObjectGraph(Graph));

    const std::string OutputName =
        "anonymous-preflight-" + std::to_string(FormatIndex) + ".o";
    const ObjectOutputDestination Destination =
        ObjectOutputDestination::memory(OutputName, UINT64_C(1) << 20);
    auto Rejected = (*Provider)->beginWrite(Scope.task(), Graph, Destination);
    ASSERT_FALSE(static_cast<bool>(Rejected));
    const std::string Message = errorText(Rejected.takeError());
    EXPECT_NE(Message.find("symbol name cannot be represented"),
              std::string::npos)
        << Message;
    EXPECT_FALSE(findPluginMemoryOutput(Scope.task(), OutputName).has_value());

    Graph.symbols().front().Name =
        Route.ObjectFormat == BuiltinObjectFormat::MachO ? "_repaired_symbol"
                                                         : "repaired_symbol";
    Graph.advanceGeneration();
    Graph.issueLayoutProof();
    auto Retried = (*Provider)->beginWrite(Scope.task(), Graph, Destination);
    ASSERT_TRUE(static_cast<bool>(Retried)) << errorText(Retried.takeError());
    EXPECT_FALSE((*Retried)->abort());
    Tested[FormatIndex] = true;
  }
  EXPECT_TRUE(Tested[0]);
  EXPECT_TRUE(Tested[1]);
  EXPECT_TRUE(Tested[2]);
}

TEST(PluginObjectWriterTest,
     ObjectPhasePipelineVerifiesAndCommitsOnlyAtSealedGate) {
  ObjectWriterTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  NevercObjectFormatDescriptor Format{};
  Format.Header = {sizeof(Format), NEVERC_OBJECT_FORMAT_API_MAJOR, UINT16_C(0),
                   0};
  Format.FormatID = TestFormatID;
  Format.CanonicalName = view("nobj");
  Format.DefaultExtension = view(".nobj");
  Format.Flags = NEVERC_OBJECT_FORMAT_CAN_WRITE;
  Format.Writer = writeTestObject;

  PluginTargetRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.object-writer";
  Registration.ObjectFormats = ArrayRef<NevercObjectFormatDescriptor>(Format);
  auto Snapshot = PluginTargetRegistry::freeze(ArrayRef(Registration),
                                               PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Pipeline = ObjectPhasePipeline::create(Scope.task(), *Snapshot);
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());

  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginObjectGraph Graph(std::move(*Target));
  PluginObjectSection Section;
  Section.ID = Graph.allocateEntityID();
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 1;
  Section.Data = {UINT8_C(0x90), UINT8_C(0xc3)};
  Graph.sections().push_back(std::move(Section));

  auto Image = (*Pipeline)->execute(
      Graph, ObjectOutputDestination::memory("pipeline.nobj", UINT64_C(1024)));
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  EXPECT_EQ((*Image)->state(), PluginObjectImageState::Committed);

  auto Published = findPluginMemoryOutput(Scope.task(), "pipeline.nobj");
  ASSERT_TRUE(Published.has_value());
  const std::array<uint8_t, 6> Expected{
      {'N', 'O', 'B', 'J', UINT8_C(0x90), UINT8_C(0xc3)}};
  EXPECT_EQ(Published->Bytes,
            (std::vector<uint8_t>(Expected.begin(), Expected.end())));
}

TEST(PluginObjectWriterTest,
     SessionRegisteredPostWriteInterceptorRunsInPipeline) {
  ObjectWriterTaskScope Scope;
  ASSERT_TRUE(Scope.initialize(NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN));

  NevercObjectFormatDescriptor Format{};
  Format.Header = {sizeof(Format), NEVERC_OBJECT_FORMAT_API_MAJOR, UINT16_C(0),
                   0};
  Format.FormatID = TestFormatID;
  Format.CanonicalName = view("nobj");
  Format.DefaultExtension = view(".nobj");
  Format.Flags = NEVERC_OBJECT_FORMAT_CAN_WRITE;
  Format.Writer = writeTestObject;
  PluginTargetRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.object-writer";
  Registration.ObjectFormats = ArrayRef<NevercObjectFormatDescriptor>(Format);
  auto Snapshot = PluginTargetRegistry::freeze(ArrayRef(Registration),
                                               PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Pipeline = ObjectPhasePipeline::create(Scope.task(), *Snapshot);
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());

  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginObjectGraph Graph(std::move(*Target));
  PluginObjectSection Section;
  Section.ID = Graph.allocateEntityID();
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 1;
  Section.Data = {UINT8_C(0x90), UINT8_C(0xc3)};
  Graph.sections().push_back(std::move(Section));

  auto Image = (*Pipeline)->execute(
      Graph,
      ObjectOutputDestination::memory("registered-hook.nobj", UINT64_C(1024)));
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto Published = findPluginMemoryOutput(Scope.task(), "registered-hook.nobj");
  ASSERT_TRUE(Published.has_value());
  const std::array<uint8_t, 6> Expected{
      {'N', 'O', 'B', 'J', UINT8_C(0x42), UINT8_C(0xc3)}};
  EXPECT_EQ(Published->Bytes,
            (std::vector<uint8_t>(Expected.begin(), Expected.end())));
}

TEST(PluginObjectWriterTest, ObjectObserversCannotMutateGraphs) {
  ObjectWriterTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  NevercObjectFormatDescriptor Format{};
  Format.Header = {sizeof(Format), NEVERC_OBJECT_FORMAT_API_MAJOR, UINT16_C(0),
                   0};
  Format.FormatID = TestFormatID;
  Format.CanonicalName = view("nobj");
  Format.DefaultExtension = view(".nobj");
  Format.Flags = NEVERC_OBJECT_FORMAT_CAN_WRITE;
  Format.Writer = writeTestObject;
  PluginTargetRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.object-writer";
  Registration.ObjectFormats = ArrayRef<NevercObjectFormatDescriptor>(Format);
  auto Snapshot = PluginTargetRegistry::freeze(ArrayRef(Registration),
                                               PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Pipeline = ObjectPhasePipeline::create(Scope.task(), *Snapshot);
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());

  ReadOnlyObserverData ReadOnly{&Scope.phaseAPI()};
  NevercObserverDescriptor Observer{};
  Observer.Header = {sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase = {NEVERC_PHASE_OBJECT_PRE_WRITE_HIGH,
                    NEVERC_PHASE_OBJECT_PRE_WRITE_LOW};
  Observer.Points = NEVERC_OBSERVER_BEFORE;
  Observer.Callback = attemptObserverMutation;
  Observer.UserData = &ReadOnly;
  ASSERT_FALSE((*Pipeline)->addObserver("org.neverc.test.minimal", Observer));

  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginObjectGraph Graph(std::move(*Target));
  PluginObjectSection Section;
  Section.ID = Graph.allocateEntityID();
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 1;
  Section.Data = {UINT8_C(0xc3)};
  Graph.sections().push_back(std::move(Section));

  auto Image = (*Pipeline)->execute(
      Graph, ObjectOutputDestination::memory("read-only.nobj", UINT64_C(1024)));
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  EXPECT_EQ(ReadOnly.Calls, 1U);
  EXPECT_EQ(ReadOnly.MutationStatus, NEVERC_STATUS_POLICY_VIOLATION);
}

TEST(PluginObjectWriterTest, PreWriteInterceptorMutatesGraphBeforeLayout) {
  ObjectWriterTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  NevercObjectFormatDescriptor Format{};
  Format.Header = {sizeof(Format), NEVERC_OBJECT_FORMAT_API_MAJOR, UINT16_C(0),
                   0};
  Format.FormatID = TestFormatID;
  Format.CanonicalName = view("nobj");
  Format.DefaultExtension = view(".nobj");
  Format.Flags = NEVERC_OBJECT_FORMAT_CAN_WRITE;
  Format.Writer = writeTestObject;

  PluginTargetRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.object-writer";
  Registration.ObjectFormats = ArrayRef<NevercObjectFormatDescriptor>(Format);
  auto Snapshot = PluginTargetRegistry::freeze(ArrayRef(Registration),
                                               PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Pipeline = ObjectPhasePipeline::create(Scope.task(), *Snapshot);
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());

  RelayoutData Rewrite{&Scope.phaseAPI()};
  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = {NEVERC_PHASE_OBJECT_PRE_WRITE_HIGH,
                       NEVERC_PHASE_OBJECT_PRE_WRITE_LOW};
  Interceptor.Callback = mutatePostLayoutOnce;
  Interceptor.UserData = &Rewrite;
  ASSERT_FALSE(
      (*Pipeline)->addInterceptor("org.neverc.test.minimal", Interceptor));

  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginObjectGraph Graph(std::move(*Target));
  PluginObjectSection Section;
  Section.ID = Graph.allocateEntityID();
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 1;
  Section.Data = {UINT8_C(0x90), UINT8_C(0xc3)};
  Graph.sections().push_back(std::move(Section));

  auto Image = (*Pipeline)->execute(
      Graph, ObjectOutputDestination::memory("pre-write.nobj", UINT64_C(1024)));
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  EXPECT_EQ(Rewrite.Calls, 1U);

  auto Published = findPluginMemoryOutput(Scope.task(), "pre-write.nobj");
  ASSERT_TRUE(Published.has_value());
  const std::array<uint8_t, 7> Expected{
      {'N', 'O', 'B', 'J', UINT8_C(0x90), UINT8_C(0x90), UINT8_C(0xc3)}};
  EXPECT_EQ(Published->Bytes,
            (std::vector<uint8_t>(Expected.begin(), Expected.end())));
}

TEST(PluginObjectWriterTest,
     PostLayoutMutationInvalidatesProofAndTriggersRelayout) {
  ObjectWriterTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  NevercObjectFormatDescriptor Format{};
  Format.Header = {sizeof(Format), NEVERC_OBJECT_FORMAT_API_MAJOR, UINT16_C(0),
                   0};
  Format.FormatID = TestFormatID;
  Format.CanonicalName = view("nobj");
  Format.DefaultExtension = view(".nobj");
  Format.Flags = NEVERC_OBJECT_FORMAT_CAN_WRITE;
  Format.Writer = writeTestObject;

  PluginTargetRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.object-writer";
  Registration.ObjectFormats = ArrayRef<NevercObjectFormatDescriptor>(Format);
  auto Snapshot = PluginTargetRegistry::freeze(ArrayRef(Registration),
                                               PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Pipeline = ObjectPhasePipeline::create(Scope.task(), *Snapshot);
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());

  RelayoutData Relayout{&Scope.phaseAPI()};
  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = {NEVERC_PHASE_OBJECT_POST_LAYOUT_HIGH,
                       NEVERC_PHASE_OBJECT_POST_LAYOUT_LOW};
  Interceptor.Callback = mutatePostLayoutOnce;
  Interceptor.UserData = &Relayout;
  ASSERT_FALSE(
      (*Pipeline)->addInterceptor("org.neverc.test.minimal", Interceptor));

  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginObjectGraph Graph(std::move(*Target));
  PluginObjectSection Section;
  Section.ID = Graph.allocateEntityID();
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 1;
  Section.Data = {UINT8_C(0x90), UINT8_C(0xc3)};
  Graph.sections().push_back(std::move(Section));

  auto Image = (*Pipeline)->execute(
      Graph, ObjectOutputDestination::memory("relayout.nobj", UINT64_C(1024)));
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  EXPECT_EQ(Relayout.Calls, 2U);

  auto Published = findPluginMemoryOutput(Scope.task(), "relayout.nobj");
  ASSERT_TRUE(Published.has_value());
  const std::array<uint8_t, 7> Expected{
      {'N', 'O', 'B', 'J', UINT8_C(0x90), UINT8_C(0x90), UINT8_C(0xc3)}};
  EXPECT_EQ(Published->Bytes,
            (std::vector<uint8_t>(Expected.begin(), Expected.end())));
}

TEST(PluginObjectWriterTest,
     PostWriteInterceptorMutatesBoundedCandidateBeforeFinish) {
  ObjectWriterTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  NevercObjectFormatDescriptor Format{};
  Format.Header = {sizeof(Format), NEVERC_OBJECT_FORMAT_API_MAJOR, UINT16_C(0),
                   0};
  Format.FormatID = TestFormatID;
  Format.CanonicalName = view("nobj");
  Format.DefaultExtension = view(".nobj");
  Format.Flags = NEVERC_OBJECT_FORMAT_CAN_WRITE;
  Format.Writer = writeTestObject;

  PluginTargetRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.object-writer";
  Registration.ObjectFormats = ArrayRef<NevercObjectFormatDescriptor>(Format);
  auto Snapshot = PluginTargetRegistry::freeze(ArrayRef(Registration),
                                               PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Pipeline = ObjectPhasePipeline::create(Scope.task(), *Snapshot);
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());

  PostWriteData Rewrite{&Scope.phaseAPI()};
  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = {NEVERC_PHASE_OBJECT_POST_WRITE_HIGH,
                       NEVERC_PHASE_OBJECT_POST_WRITE_LOW};
  Interceptor.Callback = rewritePostWriteByte;
  Interceptor.UserData = &Rewrite;
  ASSERT_FALSE(
      (*Pipeline)->addInterceptor("org.neverc.test.minimal", Interceptor));

  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginObjectGraph Graph(std::move(*Target));
  PluginObjectSection Section;
  Section.ID = Graph.allocateEntityID();
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 1;
  Section.Data = {UINT8_C(0x90), UINT8_C(0xc3)};
  Graph.sections().push_back(std::move(Section));

  auto Image = (*Pipeline)->execute(
      Graph,
      ObjectOutputDestination::memory("post-write.nobj", UINT64_C(1024)));
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  EXPECT_EQ(Rewrite.Calls, 1U);
  EXPECT_EQ(Rewrite.ImageState, NEVERC_OBJECT_IMAGE_CANDIDATE);
  EXPECT_EQ(Rewrite.OutputState, NEVERC_OUTPUT_OPEN);
  EXPECT_EQ(Rewrite.Provenance, "writer:org.neverc.test.object-writer:nobj");
  EXPECT_EQ(Rewrite.HasLayoutReport, NEVERC_TRUE);
  EXPECT_EQ(Rewrite.LayoutReport.GraphGeneration, Graph.generation());
  EXPECT_EQ(Rewrite.LayoutReport.TargetID.High, TestTargetID.High);
  EXPECT_EQ(Rewrite.LayoutReport.TargetID.Low, TestTargetID.Low);
  EXPECT_EQ(Rewrite.LayoutReport.FormatID.High, TestFormatID.High);
  EXPECT_EQ(Rewrite.LayoutReport.FormatID.Low, TestFormatID.Low);

  auto Published = findPluginMemoryOutput(Scope.task(), "post-write.nobj");
  ASSERT_TRUE(Published.has_value());
  const std::array<uint8_t, 6> Expected{
      {'N', 'O', 'B', 'J', UINT8_C(0xcc), UINT8_C(0xc3)}};
  EXPECT_EQ(Published->Bytes,
            (std::vector<uint8_t>(Expected.begin(), Expected.end())));
}

TEST(PluginObjectWriterTest,
     FinalVerificationRejectionAbortsFinishedImageBeforePublication) {
  ObjectWriterTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  NevercObjectFormatDescriptor Format{};
  Format.Header = {sizeof(Format), NEVERC_OBJECT_FORMAT_API_MAJOR, UINT16_C(0),
                   0};
  Format.FormatID = TestFormatID;
  Format.CanonicalName = view("nobj");
  Format.DefaultExtension = view(".nobj");
  Format.Flags = NEVERC_OBJECT_FORMAT_CAN_WRITE;
  Format.Writer = writeTestObject;

  PluginTargetRegistrationView Registration;
  Registration.PluginID = "org.neverc.test.object-writer";
  Registration.ObjectFormats = ArrayRef<NevercObjectFormatDescriptor>(Format);
  auto Snapshot = PluginTargetRegistry::freeze(ArrayRef(Registration),
                                               PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Pipeline = ObjectPhasePipeline::create(Scope.task(), *Snapshot);
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());

  RejectFinalVerifyData Rejection{&Scope.phaseAPI()};
  NevercObserverDescriptor Observer{};
  Observer.Header = {sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase = {NEVERC_PHASE_OBJECT_FINAL_VERIFY_HIGH,
                    NEVERC_PHASE_OBJECT_FINAL_VERIFY_LOW};
  Observer.Points = NEVERC_OBSERVER_BEFORE;
  Observer.Callback = rejectFinalObjectImage;
  Observer.UserData = &Rejection;
  ASSERT_FALSE((*Pipeline)->addObserver("org.neverc.test.minimal", Observer));

  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginObjectGraph Graph(std::move(*Target));
  PluginObjectSection Section;
  Section.ID = Graph.allocateEntityID();
  Section.Name = ".text";
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Section.Alignment = 1;
  Section.Data = {UINT8_C(0x90), UINT8_C(0xc3)};
  Graph.sections().push_back(std::move(Section));

  auto Image = (*Pipeline)->execute(
      Graph, ObjectOutputDestination::memory("rejected.nobj", UINT64_C(1024)));
  ASSERT_FALSE(static_cast<bool>(Image));
  const std::string Failure = errorText(Image.takeError());
  EXPECT_EQ(Rejection.Calls, 1U) << Failure;
  EXPECT_EQ(Rejection.ImageState, NEVERC_OBJECT_IMAGE_CANDIDATE);
  EXPECT_EQ(Rejection.OutputState, NEVERC_OUTPUT_FINISHED);
  EXPECT_FALSE(
      findPluginMemoryOutput(Scope.task(), "rejected.nobj").has_value());
}

} // namespace
