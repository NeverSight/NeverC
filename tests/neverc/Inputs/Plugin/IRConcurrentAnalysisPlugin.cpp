#include "neverc/Plugin/PluginIR.h"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <thread>

#define STRING_VIEW(Value)                                                     \
  NevercStringView { (Value), static_cast<uint64_t>(sizeof(Value) - 1) }

namespace {

const NevercIRPassAPI *PassAPI;
const NevercIRAnalysisAPI *AnalysisAPI;
int ProcessState;
std::atomic<uint64_t> ComputeCount{0};
uint64_t ResultValue = 0;

constexpr NevercInterfaceID AnalysisID{
    UINT64_C(0x4e43505445535441), UINT64_C(0x0000000000000010)};

NevercStatus status(NevercStatusCode Code) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  return Result;
}

NevercStatus NEVERC_CALL computeAnalysis(
    const NevercIRPassInvocation *Invocation, void **OutResult, void *) {
  if (!Invocation || !OutResult || Invocation->Builder ||
      Invocation->Level != NEVERC_IR_PASS_LEVEL_MODULE)
    return status(NEVERC_STATUS_POLICY_VIOLATION);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  ResultValue = ComputeCount.fetch_add(1, std::memory_order_acq_rel) + 1;
  *OutResult = &ResultValue;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL queryAnalysis(const void *Result,
                                       NevercByteView *OutData, void *) {
  if (!Result || !OutData)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  OutData->Data = static_cast<const uint8_t *>(Result);
  OutData->Length = sizeof(uint64_t);
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL invalidateAnalysis(
    void *, NevercIRAnalysisInvalidationReason, void *) {
  return neverc_status_ok();
}

void NEVERC_CALL destroyAnalysis(void *, void *) {}

NevercStatus NEVERC_CALL runPass(const NevercIRPassInvocation *Invocation,
                                 NevercIRPreservedAnalyses *OutPreserved,
                                 void *) {
  if (!Invocation || !Invocation->Analyses || !OutPreserved)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);

  constexpr size_t ThreadCount = 8;
  NevercStatus Results[ThreadCount]{};
  std::thread Threads[ThreadCount];
  std::atomic<uint32_t> Ready{0};
  std::atomic<bool> Start{false};
  for (size_t I = 0; I != ThreadCount; ++I) {
    Threads[I] = std::thread([&, I] {
      Ready.fetch_add(1, std::memory_order_acq_rel);
      while (!Start.load(std::memory_order_acquire))
        std::this_thread::yield();
      NevercIRAnalysisResultHandle Handle{};
      Results[I] = Invocation->Analyses->QueryCustom(
          Invocation->Analyses->Context, Invocation->Task, AnalysisID,
          &Handle);
      if (Results[I].Code != NEVERC_STATUS_OK)
        return;
      NevercByteView Data{};
      Results[I] = Invocation->Analyses->GetCustomResultData(
          Invocation->Analyses->Context, Invocation->Task, Handle, &Data);
      if (Results[I].Code == NEVERC_STATUS_OK &&
          (Data.Length != sizeof(uint64_t) || !Data.Data ||
           *reinterpret_cast<const uint64_t *>(Data.Data) != UINT64_C(1)))
        Results[I] = status(NEVERC_STATUS_VERIFICATION_FAILED);
    });
  }
  while (Ready.load(std::memory_order_acquire) != ThreadCount)
    std::this_thread::yield();
  Start.store(true, std::memory_order_release);
  for (std::thread &Thread : Threads)
    Thread.join();
  for (NevercStatus Result : Results)
    if (Result.Code != NEVERC_STATUS_OK)
      return Result;
  if (ComputeCount.load(std::memory_order_acquire) != 1)
    return status(NEVERC_STATUS_VERIFICATION_FAILED);

  std::memset(OutPreserved, 0, sizeof(*OutPreserved));
  OutPreserved->Header = {sizeof(*OutPreserved), NEVERC_IR_PASS_API_MAJOR,
                          NEVERC_IR_PASS_API_MINOR, 0};
  OutPreserved->CustomAnalyses = &AnalysisID;
  OutPreserved->CustomAnalysisCount = 1;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL processBegin(const NevercCoreAPI *,
                                      void **OutProcessState) {
  if (!OutProcessState)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = &ProcessState;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL registerPlugin(const NevercCoreAPI *,
                                        const NevercRegistrarAPI *,
                                        void *RegistrarContext, void *) {
  NevercIRAnalysisDescriptor Analysis{};
  Analysis.Header = {sizeof(Analysis), NEVERC_IR_ANALYSIS_API_MAJOR,
                     NEVERC_IR_ANALYSIS_API_MINOR, 0};
  Analysis.AnalysisID = AnalysisID;
  Analysis.Name = STRING_VIEW("neverc.test.concurrent_analysis");
  Analysis.Level = NEVERC_IR_PASS_LEVEL_MODULE;
  Analysis.Compute = computeAnalysis;
  Analysis.Query = queryAnalysis;
  Analysis.Invalidate = invalidateAnalysis;
  Analysis.Destroy = destroyAnalysis;
  NevercStatus Status = AnalysisAPI->RegisterAnalysis(
      AnalysisAPI->Context, RegistrarContext, &Analysis);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  NevercIRPassDescriptor Pass{};
  Pass.Header = {sizeof(Pass), NEVERC_IR_PASS_API_MAJOR,
                 NEVERC_IR_PASS_API_MINOR, 0};
  Pass.PassID = STRING_VIEW("neverc.test.concurrent_analysis_pass");
  Pass.Phase = {NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH,
                NEVERC_PHASE_IR_PASS_PRE_OPT_LOW};
  Pass.Level = NEVERC_IR_PASS_LEVEL_MODULE;
  Pass.Deterministic = NEVERC_TRUE;
  Pass.Run = runPass;
  return PassAPI->RegisterPass(PassAPI->Context, RegistrarContext, &Pass);
}

NevercStatus NEVERC_CALL destroyPlugin(const NevercCoreAPI *, void *) {
  return neverc_status_ok();
}

} // namespace

extern "C" NEVERC_EXPORT NevercStatus NEVERC_CALL neverc_plugin_entry(
    const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin) {
  if (!Bootstrap || !Bootstrap->QueryInterface || !OutPlugin ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return status(NEVERC_STATUS_INVALID_ARGUMENT);

  const void *Table = nullptr;
  uint16_t Minor = 0;
  uint64_t StructSize = 0;
  NevercStatus Status = Bootstrap->QueryInterface(
      Bootstrap->Context,
      {NEVERC_INTERFACE_IR_PASS_HIGH, NEVERC_INTERFACE_IR_PASS_LOW},
      NEVERC_IR_PASS_API_MAJOR, NEVERC_IR_PASS_API_MINOR, &Table, &Minor,
      &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Table || StructSize < sizeof(NevercIRPassAPI))
    return status(NEVERC_STATUS_ABI_MISMATCH);
  PassAPI = static_cast<const NevercIRPassAPI *>(Table);

  Table = nullptr;
  Status = Bootstrap->QueryInterface(
      Bootstrap->Context,
      {NEVERC_INTERFACE_IR_ANALYSIS_HIGH, NEVERC_INTERFACE_IR_ANALYSIS_LOW},
      NEVERC_IR_ANALYSIS_API_MAJOR, NEVERC_IR_ANALYSIS_API_MINOR, &Table,
      &Minor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Table || StructSize < sizeof(NevercIRAnalysisAPI))
    return status(NEVERC_STATUS_ABI_MISMATCH);
  AnalysisAPI = static_cast<const NevercIRAnalysisAPI *>(Table);

  const uint32_t Capacity = OutPlugin->Header.StructSize;
  NevercPluginDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = STRING_VIEW("org.neverc.test.ir-concurrent-analysis");
  Descriptor.DisplayName = STRING_VIEW("NeverC concurrent IR analysis test");
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_THREAD_SAFE;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_ALLOWED;
  Descriptor.ProcessBegin = processBegin;
  Descriptor.Register = registerPlugin;
  Descriptor.Destroy = destroyPlugin;
  const size_t Writable =
      Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  std::memcpy(OutPlugin, &Descriptor, Writable);
  OutPlugin->Header.StructSize = sizeof(Descriptor);
  return neverc_status_ok();
}
