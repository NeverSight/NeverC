#include "neverc/Plugin/Host/MIRPassPlugin.h"
#include "neverc/Plugin/Host/MIRPluginBridge.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/RegisterPressure.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/TimeProfiler.h"
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus mirPassStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

bool validAnalysis(NevercMIRBuiltinAnalysis Analysis) {
  return Analysis >= NEVERC_MIR_ANALYSIS_LIVE_INTERVALS &&
         Analysis <= NEVERC_MIR_ANALYSIS_REGISTER_PRESSURE;
}

bool contains(ArrayRef<NevercMIRBuiltinAnalysis> Values,
              NevercMIRBuiltinAnalysis Value) {
  return llvm::is_contained(Values, Value);
}

NevercStatus NEVERC_CALL
registerPass(void *, void *RegistrarContext,
             const NevercMIRPassDescriptor *Descriptor) {
  return registerPluginMIRPass(RegistrarContext, Descriptor);
}

const NevercMIRPassAPI PassAPI = {
    {sizeof(NevercMIRPassAPI), NEVERC_MIR_PASS_API_MAJOR,
     NEVERC_MIR_PASS_API_MINOR, 0},
    nullptr,
    registerPass,
};

struct AnalysisAccess {
  LiveIntervals *Intervals = nullptr;
  SlotIndexes *Indexes = nullptr;
  MachineDominatorTree *DominatorTree = nullptr;
  MachineLoopInfo *LoopInfo = nullptr;
};

struct PressureData {
  DenseMap<const MachineBasicBlock *, std::vector<unsigned>> Maximums;
  std::vector<unsigned> Limits;
};

struct LiveVariableData {
  DenseMap<const MachineBasicBlock *, DenseSet<unsigned>> LiveIns;
};

struct AnalysisResultRecord {
  NevercMIRBuiltinAnalysis Kind = 0;
  void *Payload = nullptr;
  uint64_t Generation = 0;
};

class AnalysisInvocationBridge {
public:
  AnalysisInvocationBridge(PluginTaskContext &TaskValue,
                           MIRPluginBridge &CoreValue,
                           MachineFunction &FunctionValue,
                           AnalysisAccess AccessValue)
      : Task(TaskValue), Core(CoreValue), Function(FunctionValue),
        Access(AccessValue), Generation(CoreValue.mutationGeneration()) {
    API.Header = {sizeof(API), NEVERC_MIR_ANALYSIS_API_MAJOR,
                  NEVERC_MIR_ANALYSIS_API_MINOR, 0};
    API.Context = this;
    API.QueryBuiltin = queryBuiltin;
    API.DominatorTreeDominates = dominatorTreeDominates;
    API.GetLoopCount = getLoopCount;
    API.GetLoopHeader = getLoopHeader;
    API.GetLoopForBlock = getLoopForBlock;
    API.GetSlotIndex = getSlotIndex;
    API.GetLiveIntervalSegmentCount = getLiveIntervalSegmentCount;
    API.GetLiveIntervalSegment = getLiveIntervalSegment;
    API.IsRegisterLiveInBlock = isRegisterLiveInBlock;
    API.GetRegisterPressureSetCount = getRegisterPressureSetCount;
    API.GetRegisterPressure = getRegisterPressure;
  }

  ~AnalysisInvocationBridge() {
    for (const auto &Entry : Results)
      (void)Task.handles().release(Entry.second.Handle,
                                   PluginMIRAnalysisResultHandleKind);
  }

  const NevercMIRAnalysisAPI &api() const { return API; }

private:
  struct ResultEntry {
    NevercHandle Handle{};
    std::unique_ptr<AnalysisResultRecord> Record;
  };

  static AnalysisInvocationBridge *bridge(void *Context,
                                          NevercTaskHandle Task,
                                          NevercStatus *OutStatus) {
    if (OutStatus)
      *OutStatus = mirPassStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Bridge = static_cast<AnalysisInvocationBridge *>(Context);
    if (!Bridge || !sameHandle(Bridge->Task.handle(), Task))
      return nullptr;
    if (OutStatus)
      *OutStatus = neverc_status_ok();
    return Bridge;
  }

  Expected<NevercMIRAnalysisResultHandle>
  wrapResult(NevercMIRBuiltinAnalysis Kind, void *Payload) {
    auto Existing = Results.find(Kind);
    if (Existing != Results.end())
      return Existing->second.Handle;
    auto Record = std::make_unique<AnalysisResultRecord>();
    Record->Kind = Kind;
    Record->Payload = Payload;
    Record->Generation = Generation;
    auto Handle = Task.handles().create(PluginMIRAnalysisResultHandleKind,
                                        Record.get());
    if (!Handle)
      return Handle.takeError();
    ResultEntry Entry;
    Entry.Handle = *Handle;
    Entry.Record = std::move(Record);
    Results.insert({Kind, std::move(Entry)});
    return *Handle;
  }

  NevercStatus resolveResult(NevercMIRAnalysisResultHandle Handle,
                             NevercMIRBuiltinAnalysis Expected,
                             AnalysisResultRecord **OutRecord) {
    if (!OutRecord)
      return mirPassStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutRecord = nullptr;
    void *Payload = nullptr;
    NevercStatus Status = Task.handles().resolve(
        Handle, PluginMIRAnalysisResultHandleKind, &Payload);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto *Record = static_cast<AnalysisResultRecord *>(Payload);
    auto Existing = Results.find(Expected);
    if (!Record || Record->Kind != Expected || !Record->Payload ||
        Existing == Results.end() || Existing->second.Record.get() != Record ||
        !sameHandle(Existing->second.Handle, Handle))
      return mirPassStatus(NEVERC_STATUS_WRONG_SCOPE);
    if (Record->Generation != Core.mutationGeneration())
      return mirPassStatus(NEVERC_STATUS_STALE_HANDLE);
    *OutRecord = Record;
    return neverc_status_ok();
  }

  MachineDominatorTree *getOrCreateDominatorTree() {
    if (Access.DominatorTree)
      return Access.DominatorTree;
    if (!OwnedDominatorTree)
      OwnedDominatorTree =
          std::make_unique<MachineDominatorTree>(Function);
    return OwnedDominatorTree.get();
  }

  MachineLoopInfo *getOrCreateLoopInfo() {
    if (Access.LoopInfo)
      return Access.LoopInfo;
    if (!OwnedLoopInfo)
      OwnedLoopInfo =
          std::make_unique<MachineLoopInfo>(*getOrCreateDominatorTree());
    return OwnedLoopInfo.get();
  }

  PressureData *getOrCreatePressure() {
    if (Pressure)
      return Pressure.get();
    const TargetRegisterInfo *TRI =
        Function.getSubtarget().getRegisterInfo();
    if (!TRI)
      return nullptr;
    Pressure = std::make_unique<PressureData>();
    RegisterClassInfo RCI;
    RCI.runOnMachineFunction(Function);
    unsigned SetCount = TRI->getNumRegPressureSets();
    Pressure->Limits.reserve(SetCount);
    for (unsigned Set = 0; Set != SetCount; ++Set)
      Pressure->Limits.push_back(RCI.getRegPressureSetLimit(Set));
    for (MachineBasicBlock &Block : Function) {
      RegionPressure Region;
      Region.reset();
      RegPressureTracker Tracker(Region);
      Tracker.init(&Function, &RCI, nullptr, &Block, Block.begin(),
                   false, false);
      while (Tracker.getPos() != Block.end())
        Tracker.advance();
      Tracker.closeRegion();
      Pressure->Maximums[&Block] = Region.MaxSetPressure;
    }
    return Pressure.get();
  }

  LiveVariableData *getOrCreateLiveVariables() {
    if (LiveVariablesSidecar)
      return LiveVariablesSidecar.get();
    LiveVariablesSidecar = std::make_unique<LiveVariableData>();
    using RegisterSet = DenseSet<unsigned>;
    DenseMap<const MachineBasicBlock *, RegisterSet> Uses;
    DenseMap<const MachineBasicBlock *, RegisterSet> Definitions;
    DenseMap<std::pair<const MachineBasicBlock *, const MachineBasicBlock *>,
             RegisterSet>
        PhiUses;

    for (const MachineBasicBlock &Block : Function) {
      RegisterSet &BlockUses = Uses[&Block];
      RegisterSet &BlockDefinitions = Definitions[&Block];
      for (const MachineInstr &Instruction : Block) {
        if (Instruction.isPHI()) {
          for (const MachineOperand &Operand : Instruction.operands())
            if (Operand.isReg() && Operand.isDef() &&
                Operand.getReg().isVirtual())
              BlockDefinitions.insert(Operand.getReg().id());
          for (unsigned Index = 1; Index + 1 < Instruction.getNumOperands();
               Index += 2) {
            const MachineOperand &RegisterOperand =
                Instruction.getOperand(Index);
            const MachineOperand &BlockOperand =
                Instruction.getOperand(Index + 1);
            if (RegisterOperand.isReg() &&
                RegisterOperand.getReg().isVirtual() &&
                BlockOperand.isMBB())
              PhiUses[{BlockOperand.getMBB(), &Block}].insert(
                  RegisterOperand.getReg().id());
          }
          continue;
        }
        for (const MachineOperand &Operand : Instruction.operands()) {
          if (!Operand.isReg() || !Operand.getReg().isVirtual())
            continue;
          unsigned Register = Operand.getReg().id();
          if (Operand.readsReg() && !BlockDefinitions.contains(Register))
            BlockUses.insert(Register);
          if (Operand.isDef())
            BlockDefinitions.insert(Register);
        }
      }
      LiveVariablesSidecar->LiveIns[&Block];
    }

    DenseMap<const MachineBasicBlock *, RegisterSet> LiveOuts;
    bool Changed;
    do {
      Changed = false;
      for (const MachineBasicBlock &Block :
           llvm::reverse(Function)) {
        RegisterSet NewOut;
        for (const MachineBasicBlock *Successor : Block.successors()) {
          for (unsigned Register :
               LiveVariablesSidecar->LiveIns[Successor])
            if (!Definitions[Successor].contains(Register))
              NewOut.insert(Register);
          auto Phi = PhiUses.find({&Block, Successor});
          if (Phi != PhiUses.end())
            NewOut.insert(Phi->second.begin(), Phi->second.end());
        }
        RegisterSet NewIn = Uses[&Block];
        for (unsigned Register : NewOut)
          if (!Definitions[&Block].contains(Register))
            NewIn.insert(Register);
        auto Equal = [](const RegisterSet &Left, const RegisterSet &Right) {
          return Left.size() == Right.size() &&
                 llvm::all_of(Left, [&](unsigned Value) {
                   return Right.contains(Value);
                 });
        };
        if (!Equal(LiveOuts[&Block], NewOut)) {
          LiveOuts[&Block] = std::move(NewOut);
          Changed = true;
        }
        if (!Equal(LiveVariablesSidecar->LiveIns[&Block], NewIn)) {
          LiveVariablesSidecar->LiveIns[&Block] = std::move(NewIn);
          Changed = true;
        }
      }
    } while (Changed);
    return LiveVariablesSidecar.get();
  }

  static NevercStatus NEVERC_CALL queryBuiltin(
      void *Context, NevercTaskHandle Task,
      NevercMIRBuiltinAnalysis Analysis,
      NevercMachineFunctionHandle FunctionHandle,
      NevercMIRAnalysisResultHandle *OutResult) {
    NevercStatus Status;
    AnalysisInvocationBridge *Self = bridge(Context, Task, &Status);
    if (!Self)
      return Status;
    if (!OutResult || !validAnalysis(Analysis))
      return mirPassStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutResult = {};
    MachineFunction *Resolved = nullptr;
    Status = Self->Core.resolveMachineFunction(FunctionHandle, &Resolved);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Resolved != &Self->Function)
      return mirPassStatus(NEVERC_STATUS_WRONG_SCOPE);

    void *Payload = nullptr;
    switch (Analysis) {
    case NEVERC_MIR_ANALYSIS_LIVE_INTERVALS:
      Payload = Self->Access.Intervals;
      break;
    case NEVERC_MIR_ANALYSIS_LIVE_VARIABLES:
      Payload = Self->getOrCreateLiveVariables();
      break;
    case NEVERC_MIR_ANALYSIS_SLOT_INDEXES:
      Payload = Self->Access.Indexes;
      break;
    case NEVERC_MIR_ANALYSIS_DOMINATOR_TREE:
      Payload = Self->getOrCreateDominatorTree();
      break;
    case NEVERC_MIR_ANALYSIS_LOOP_INFO:
      Payload = Self->getOrCreateLoopInfo();
      break;
    case NEVERC_MIR_ANALYSIS_REGISTER_PRESSURE:
      Payload = Self->getOrCreatePressure();
      break;
    default:
      break;
    }
    if (!Payload)
      return mirPassStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    auto Handle = Self->wrapResult(Analysis, Payload);
    if (!Handle) {
      consumeError(Handle.takeError());
      return mirPassStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutResult = *Handle;
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL dominatorTreeDominates(
      void *Context, NevercTaskHandle Task,
      NevercMIRAnalysisResultHandle Result,
      NevercMachineBasicBlockHandle Dominator,
      NevercMachineBasicBlockHandle Dominated, NevercBool *OutDominates) {
    NevercStatus Status;
    AnalysisInvocationBridge *Self = bridge(Context, Task, &Status);
    if (!Self)
      return Status;
    if (!OutDominates)
      return mirPassStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    AnalysisResultRecord *Record = nullptr;
    Status = Self->resolveResult(Result,
                                 NEVERC_MIR_ANALYSIS_DOMINATOR_TREE, &Record);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    MachineBasicBlock *Left = nullptr;
    MachineBasicBlock *Right = nullptr;
    if ((Status = Self->Core.resolveBasicBlock(Dominator, &Left)).Code !=
            NEVERC_STATUS_OK ||
        (Status = Self->Core.resolveBasicBlock(Dominated, &Right)).Code !=
            NEVERC_STATUS_OK)
      return Status;
    auto *Tree = static_cast<MachineDominatorTree *>(Record->Payload);
    *OutDominates = Tree->dominates(Left, Right) ? NEVERC_TRUE : NEVERC_FALSE;
    return neverc_status_ok();
  }

  static SmallVector<MachineLoop *, 8>
  loopsInPreorder(MachineLoopInfo &Info) {
    SmallVector<MachineLoop *, 8> Result;
    for (MachineLoop *Loop : Info.getBase().getLoopsInPreorder())
      Result.push_back(Loop);
    return Result;
  }

  static NevercStatus NEVERC_CALL
  getLoopCount(void *Context, NevercTaskHandle Task,
               NevercMIRAnalysisResultHandle Result, uint64_t *OutCount) {
    NevercStatus Status;
    AnalysisInvocationBridge *Self = bridge(Context, Task, &Status);
    if (!Self)
      return Status;
    if (!OutCount)
      return mirPassStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    AnalysisResultRecord *Record = nullptr;
    Status =
        Self->resolveResult(Result, NEVERC_MIR_ANALYSIS_LOOP_INFO, &Record);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    *OutCount =
        loopsInPreorder(*static_cast<MachineLoopInfo *>(Record->Payload)).size();
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL
  getLoopHeader(void *Context, NevercTaskHandle Task,
                NevercMIRAnalysisResultHandle Result, uint64_t Index,
                NevercMachineBasicBlockHandle *OutHeader) {
    NevercStatus Status;
    AnalysisInvocationBridge *Self = bridge(Context, Task, &Status);
    if (!Self)
      return Status;
    if (!OutHeader)
      return mirPassStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    AnalysisResultRecord *Record = nullptr;
    Status =
        Self->resolveResult(Result, NEVERC_MIR_ANALYSIS_LOOP_INFO, &Record);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto Loops =
        loopsInPreorder(*static_cast<MachineLoopInfo *>(Record->Payload));
    if (Index >= Loops.size())
      return mirPassStatus(NEVERC_STATUS_NOT_FOUND);
    auto Handle = Self->Core.wrapBasicBlock(*Loops[Index]->getHeader());
    if (!Handle) {
      consumeError(Handle.takeError());
      return mirPassStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutHeader = *Handle;
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL getLoopForBlock(
      void *Context, NevercTaskHandle Task,
      NevercMIRAnalysisResultHandle Result,
      NevercMachineBasicBlockHandle BlockHandle,
      NevercMachineBasicBlockHandle *OutHeader, uint32_t *OutDepth) {
    NevercStatus Status;
    AnalysisInvocationBridge *Self = bridge(Context, Task, &Status);
    if (!Self)
      return Status;
    if (!OutHeader || !OutDepth)
      return mirPassStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutHeader = {};
    *OutDepth = 0;
    AnalysisResultRecord *Record = nullptr;
    Status =
        Self->resolveResult(Result, NEVERC_MIR_ANALYSIS_LOOP_INFO, &Record);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    MachineBasicBlock *Block = nullptr;
    Status = Self->Core.resolveBasicBlock(BlockHandle, &Block);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto *Info = static_cast<MachineLoopInfo *>(Record->Payload);
    MachineLoop *Loop = Info->getLoopFor(Block);
    if (!Loop)
      return mirPassStatus(NEVERC_STATUS_NOT_FOUND);
    auto Handle = Self->Core.wrapBasicBlock(*Loop->getHeader());
    if (!Handle) {
      consumeError(Handle.takeError());
      return mirPassStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutHeader = *Handle;
    *OutDepth = Loop->getLoopDepth();
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL
  getSlotIndex(void *Context, NevercTaskHandle Task,
               NevercMIRAnalysisResultHandle Result,
               NevercMachineInstrHandle InstructionHandle,
               uint64_t *OutIndex) {
    NevercStatus Status;
    AnalysisInvocationBridge *Self = bridge(Context, Task, &Status);
    if (!Self)
      return Status;
    if (!OutIndex)
      return mirPassStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    AnalysisResultRecord *Record = nullptr;
    Status =
        Self->resolveResult(Result, NEVERC_MIR_ANALYSIS_SLOT_INDEXES, &Record);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    MachineInstr *Instruction = nullptr;
    Status =
        Self->Core.resolveInstruction(InstructionHandle, &Instruction);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    SlotIndex Index =
        static_cast<SlotIndexes *>(Record->Payload)
            ->getInstructionIndex(*Instruction);
    if (!Index.isValid())
      return mirPassStatus(NEVERC_STATUS_NOT_FOUND);
    *OutIndex = Index.getRawIndex();
    return neverc_status_ok();
  }

  static NevercStatus resolveLiveInterval(
      AnalysisInvocationBridge &Self, NevercMIRAnalysisResultHandle Result,
      uint32_t RegisterNumber, const LiveInterval **OutInterval) {
    if (!OutInterval)
      return mirPassStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutInterval = nullptr;
    AnalysisResultRecord *Record = nullptr;
    NevercStatus Status = Self.resolveResult(
        Result, NEVERC_MIR_ANALYSIS_LIVE_INTERVALS, &Record);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Register Reg(RegisterNumber);
    auto *Intervals = static_cast<LiveIntervals *>(Record->Payload);
    if (!Reg.isVirtual() || !Intervals->hasInterval(Reg))
      return mirPassStatus(NEVERC_STATUS_NOT_FOUND);
    *OutInterval = &Intervals->getInterval(Reg);
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL getLiveIntervalSegmentCount(
      void *Context, NevercTaskHandle Task,
      NevercMIRAnalysisResultHandle Result, uint32_t Register,
      uint64_t *OutCount) {
    NevercStatus Status;
    AnalysisInvocationBridge *Self = bridge(Context, Task, &Status);
    if (!Self)
      return Status;
    if (!OutCount)
      return mirPassStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    const LiveInterval *Interval = nullptr;
    Status = resolveLiveInterval(*Self, Result, Register, &Interval);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    *OutCount = Interval->size();
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL getLiveIntervalSegment(
      void *Context, NevercTaskHandle Task,
      NevercMIRAnalysisResultHandle Result, uint32_t Register,
      uint64_t Index, NevercMIRLiveRangeSegment *OutSegment) {
    NevercStatus Status;
    AnalysisInvocationBridge *Self = bridge(Context, Task, &Status);
    if (!Self)
      return Status;
    if (!OutSegment)
      return mirPassStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    const LiveInterval *Interval = nullptr;
    Status = resolveLiveInterval(*Self, Result, Register, &Interval);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (Index >= Interval->size())
      return mirPassStatus(NEVERC_STATUS_NOT_FOUND);
    auto It = Interval->begin();
    std::advance(It, static_cast<ptrdiff_t>(Index));
    OutSegment->Start = It->start.getRawIndex();
    OutSegment->End = It->end.getRawIndex();
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL isRegisterLiveInBlock(
      void *Context, NevercTaskHandle Task,
      NevercMIRAnalysisResultHandle Result, uint32_t RegisterNumber,
      NevercMachineBasicBlockHandle BlockHandle, NevercBool *OutLive) {
    NevercStatus Status;
    AnalysisInvocationBridge *Self = bridge(Context, Task, &Status);
    if (!Self)
      return Status;
    if (!OutLive)
      return mirPassStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    AnalysisResultRecord *Record = nullptr;
    Status = Self->resolveResult(Result,
                                 NEVERC_MIR_ANALYSIS_LIVE_VARIABLES, &Record);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    MachineBasicBlock *Block = nullptr;
    Status = Self->Core.resolveBasicBlock(BlockHandle, &Block);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Register Reg(RegisterNumber);
    if (!Reg.isVirtual())
      return mirPassStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Variables = static_cast<LiveVariableData *>(Record->Payload);
    auto It = Variables->LiveIns.find(Block);
    *OutLive =
        It != Variables->LiveIns.end() && It->second.contains(Reg.id())
            ? NEVERC_TRUE
            : NEVERC_FALSE;
    return neverc_status_ok();
  }

  static NevercStatus resolvePressure(
      AnalysisInvocationBridge &Self, NevercMIRAnalysisResultHandle Result,
      NevercMachineBasicBlockHandle BlockHandle, PressureData **OutPressure,
      MachineBasicBlock **OutBlock) {
    if (!OutPressure || !OutBlock)
      return mirPassStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    AnalysisResultRecord *Record = nullptr;
    NevercStatus Status = Self.resolveResult(
        Result, NEVERC_MIR_ANALYSIS_REGISTER_PRESSURE, &Record);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Status = Self.Core.resolveBasicBlock(BlockHandle, OutBlock);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    *OutPressure = static_cast<PressureData *>(Record->Payload);
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL getRegisterPressureSetCount(
      void *Context, NevercTaskHandle Task,
      NevercMIRAnalysisResultHandle Result,
      NevercMachineBasicBlockHandle Block, uint64_t *OutCount) {
    NevercStatus Status;
    AnalysisInvocationBridge *Self = bridge(Context, Task, &Status);
    if (!Self)
      return Status;
    if (!OutCount)
      return mirPassStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    PressureData *Pressure = nullptr;
    MachineBasicBlock *Resolved = nullptr;
    Status = resolvePressure(*Self, Result, Block, &Pressure, &Resolved);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto It = Pressure->Maximums.find(Resolved);
    if (It == Pressure->Maximums.end())
      return mirPassStatus(NEVERC_STATUS_NOT_FOUND);
    *OutCount = It->second.size();
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL getRegisterPressure(
      void *Context, NevercTaskHandle Task,
      NevercMIRAnalysisResultHandle Result,
      NevercMachineBasicBlockHandle Block, uint32_t PressureSet,
      NevercMIRRegisterPressureInfo *OutInfo) {
    NevercStatus Status;
    AnalysisInvocationBridge *Self = bridge(Context, Task, &Status);
    if (!Self)
      return Status;
    if (!OutInfo || OutInfo->Header.StructSize < sizeof(*OutInfo) ||
        OutInfo->Header.Major != NEVERC_MIR_ANALYSIS_API_MAJOR)
      return mirPassStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    PressureData *Pressure = nullptr;
    MachineBasicBlock *Resolved = nullptr;
    Status = resolvePressure(*Self, Result, Block, &Pressure, &Resolved);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto It = Pressure->Maximums.find(Resolved);
    if (It == Pressure->Maximums.end() || PressureSet >= It->second.size() ||
        PressureSet >= Pressure->Limits.size())
      return mirPassStatus(NEVERC_STATUS_NOT_FOUND);
    NevercMIRRegisterPressureInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_MIR_ANALYSIS_API_MAJOR,
                   NEVERC_MIR_ANALYSIS_API_MINOR, 0};
    Info.PressureSet = PressureSet;
    Info.Maximum = It->second[PressureSet];
    Info.Limit = Pressure->Limits[PressureSet];
    *OutInfo = Info;
    return neverc_status_ok();
  }

  PluginTaskContext &Task;
  MIRPluginBridge &Core;
  MachineFunction &Function;
  AnalysisAccess Access;
  uint64_t Generation;
  NevercMIRAnalysisAPI API{};
  DenseMap<NevercMIRBuiltinAnalysis, ResultEntry> Results;
  std::unique_ptr<MachineDominatorTree> OwnedDominatorTree;
  std::unique_ptr<MachineLoopInfo> OwnedLoopInfo;
  std::unique_ptr<PressureData> Pressure;
  std::unique_ptr<LiveVariableData> LiveVariablesSidecar;
};

const NevercMIRAnalysisAPI ProcessAnalysisAPI = {
    {sizeof(NevercMIRAnalysisAPI), NEVERC_MIR_ANALYSIS_API_MAJOR,
     NEVERC_MIR_ANALYSIS_API_MINOR, 0},
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

struct PassBinding {
  std::string PluginID;
  std::string PassID;
  std::string RequiredTargetSchemaDigest;
  NevercMIRPassDescriptor Descriptor{};
  std::vector<NevercMIRBuiltinAnalysis> RequiredAnalyses;
  std::vector<NevercMIRBuiltinAnalysis> PreservedAnalyses;
};

NevercInterfaceID phaseForHook(MachinePipelineHookPoint Point) {
  switch (Point) {
  case MachinePipelineHookPoint::PostLegalize:
    return {NEVERC_PHASE_MIR_PASS_POST_LEGALIZE_HIGH,
            NEVERC_PHASE_MIR_PASS_POST_LEGALIZE_LOW};
  case MachinePipelineHookPoint::PostISel:
    return {NEVERC_PHASE_MIR_PASS_POST_ISEL_HIGH,
            NEVERC_PHASE_MIR_PASS_POST_ISEL_LOW};
  case MachinePipelineHookPoint::PreScheduler:
    return {NEVERC_PHASE_MIR_PASS_PRE_SCHEDULER_HIGH,
            NEVERC_PHASE_MIR_PASS_PRE_SCHEDULER_LOW};
  case MachinePipelineHookPoint::PostScheduler:
    return {NEVERC_PHASE_MIR_PASS_POST_SCHEDULER_HIGH,
            NEVERC_PHASE_MIR_PASS_POST_SCHEDULER_LOW};
  case MachinePipelineHookPoint::PreRegAlloc:
    return {NEVERC_PHASE_MIR_PASS_PRE_REGALLOC_HIGH,
            NEVERC_PHASE_MIR_PASS_PRE_REGALLOC_LOW};
  case MachinePipelineHookPoint::PostRegAlloc:
    return {NEVERC_PHASE_MIR_PASS_POST_REGALLOC_HIGH,
            NEVERC_PHASE_MIR_PASS_POST_REGALLOC_LOW};
  case MachinePipelineHookPoint::PostPrologEpilog:
    return {NEVERC_PHASE_MIR_PASS_POST_PROLOG_EPILOG_HIGH,
            NEVERC_PHASE_MIR_PASS_POST_PROLOG_EPILOG_LOW};
  case MachinePipelineHookPoint::PreEmit:
    return {NEVERC_PHASE_MIR_PASS_PREEMIT_HIGH,
            NEVERC_PHASE_MIR_PASS_PREEMIT_LOW};
  case MachinePipelineHookPoint::Final:
    return {NEVERC_PHASE_MIR_PASS_FINAL_HIGH,
            NEVERC_PHASE_MIR_PASS_FINAL_LOW};
  }
  llvm_unreachable("unknown machine pipeline hook");
}

bool sameInterface(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

bool validPreserved(const NevercMIRPreservedAnalyses &Preserved) {
  constexpr NevercMIRPreservedAnalysisFlags Known =
      NEVERC_MIR_PRESERVE_CFG | NEVERC_MIR_PRESERVE_ALL;
  if (Preserved.Header.StructSize < sizeof(Preserved) ||
      Preserved.Header.Major != NEVERC_MIR_PASS_API_MAJOR ||
      Preserved.Header.Minor > NEVERC_MIR_PASS_API_MINOR ||
      Preserved.Header.Flags != 0 || (Preserved.Flags & ~Known) != 0 ||
      Preserved.AnalysisCount > 64 ||
      (Preserved.AnalysisCount != 0 && !Preserved.Analyses))
    return false;
  for (uint64_t Index = 0; Index != Preserved.AnalysisCount; ++Index)
    if (!validAnalysis(Preserved.Analyses[Index]))
      return false;
  return true;
}

void emitPassError(MachineFunction &Function, const PassBinding &Binding,
                   const Twine &Message) {
  Function.getFunction().getContext().emitError(
      "NeverC MIR plugin pass '" + Binding.PassID + "' " + Message);
}

} // namespace

struct MIRPassPlan::Impl {
  explicit Impl(PluginTaskContext &TaskValue) : Task(TaskValue) {
    std::shared_ptr<const PluginTargetSnapshot> Snapshot =
        findPluginTargetSnapshot(Task.processServices(),
                                 Task.session().handle());
    if (Snapshot && Snapshot->selectedTarget())
      ActiveTargetSchemaDigest =
          Snapshot->selectedTarget()->Machine.SchemaDigest;
  }

  bool runOne(const PassBinding &Binding, MachineFunction &Function,
              MachineBasicBlock *Block, AnalysisAccess Access) {
    if (!Binding.RequiredTargetSchemaDigest.empty() &&
        Binding.RequiredTargetSchemaDigest != ActiveTargetSchemaDigest) {
      emitPassError(Function, Binding,
                    "requires target schema digest '" +
                        Binding.RequiredTargetSchemaDigest +
                        "', but the active route provides '" +
                        ActiveTargetSchemaDigest + "'");
      return false;
    }
    uint64_t FunctionGeneration = 1;
    {
      std::lock_guard<std::mutex> Lock(GenerationMutex);
      FunctionGeneration = Generations.lookup(&Function);
      if (FunctionGeneration == 0)
        FunctionGeneration = 1;
    }
    MIRPluginBridge Core(Task, Function, FunctionGeneration,
                         ActiveTargetSchemaDigest,
                         Binding.RequiredTargetSchemaDigest,
                         Binding.PluginID);
    AnalysisInvocationBridge Analyses(Task, Core, Function, Access);
    auto FunctionHandle = Core.machineFunction();
    if (!FunctionHandle) {
      emitPassError(Function, Binding,
                    "could not create a machine-function handle");
      consumeError(FunctionHandle.takeError());
      return false;
    }
    NevercMachineBasicBlockHandle BlockHandle{};
    if (Block) {
      auto Wrapped = Core.wrapBasicBlock(*Block);
      if (!Wrapped) {
        emitPassError(Function, Binding,
                      "could not create a basic-block handle");
        consumeError(Wrapped.takeError());
        return false;
      }
      BlockHandle = *Wrapped;
    }

    NevercMIRPassInvocation Invocation{};
    Invocation.Header = {sizeof(Invocation), NEVERC_MIR_PASS_API_MAJOR,
                         NEVERC_MIR_PASS_API_MINOR, 0};
    Invocation.Task = Task.handle();
    Invocation.Phase = Binding.Descriptor.Phase;
    Invocation.PassID = {Binding.PassID.data(), Binding.PassID.size()};
    Invocation.Level = Binding.Descriptor.Level;
    Invocation.Function = *FunctionHandle;
    Invocation.BasicBlock = BlockHandle;
    Invocation.Core = &Core.api();
    Invocation.Analyses = &Analyses.api();
    Invocation.TargetSchemaDigest = {
        ActiveTargetSchemaDigest.data(), ActiveTargetSchemaDigest.size()};

    NevercMIRPreservedAnalyses Preserved{};
    Preserved.Header = {sizeof(Preserved), NEVERC_MIR_PASS_API_MAJOR,
                        NEVERC_MIR_PASS_API_MINOR, 0};
    PrettyStackTraceString CrashInfo(
        ("NeverC MIR plugin pass '" + Binding.PassID + "'").c_str());
    TimeTraceScope TimeScope("PluginMIRPass", Binding.PassID);
    auto CallbackStatus = Task.invokeCallback(
        Binding.PluginID, "mir-pass/" + Binding.PassID,
        [&] {
          return Binding.Descriptor.Run(&Invocation, &Preserved,
                                        Binding.Descriptor.UserData);
        });
    if (!CallbackStatus) {
      emitPassError(Function, Binding,
                    "failed: " + toString(CallbackStatus.takeError()));
      return false;
    }
    if (CallbackStatus->Code != NEVERC_STATUS_OK) {
      emitPassError(Function, Binding,
                    "returned status " +
                        Twine(static_cast<unsigned>(CallbackStatus->Code)));
      return false;
    }
    if (!validPreserved(Preserved)) {
      emitPassError(Function, Binding,
                    "returned an invalid preserved-analysis descriptor");
      return false;
    }
    ArrayRef<NevercMIRBuiltinAnalysis> RuntimePreserved(
        Preserved.Analyses,
        static_cast<size_t>(Preserved.AnalysisCount));
    if (llvm::any_of(RuntimePreserved, [&](NevercMIRBuiltinAnalysis Analysis) {
          return !contains(Binding.PreservedAnalyses, Analysis);
        })) {
      emitPassError(Function, Binding,
                    "preserved an analysis not declared by its descriptor");
      return false;
    }

    bool Changed = Core.functionGeneration() != FunctionGeneration;
    if (Changed && (Preserved.Flags & NEVERC_MIR_PRESERVE_ALL) != 0) {
      emitPassError(Function, Binding,
                    "mutated MIR while preserving all analyses");
      return false;
    }
    if (Changed) {
      std::lock_guard<std::mutex> Lock(GenerationMutex);
      Generations[&Function] = Core.functionGeneration();
    }
    return Changed;
  }

  bool run(const PassBinding &Binding, MachineFunction &Function,
           AnalysisAccess Access) {
    if (Binding.Descriptor.Level != NEVERC_MIR_PASS_LEVEL_BASIC_BLOCK)
      return runOne(Binding, Function, nullptr, Access);

    SmallVector<MachineBasicBlock *, 16> InitialBlocks;
    for (MachineBasicBlock &Block : Function)
      InitialBlocks.push_back(&Block);
    bool Changed = false;
    for (MachineBasicBlock *Initial : InitialBlocks) {
      MachineBasicBlock *Current = nullptr;
      for (MachineBasicBlock &Candidate : Function)
        if (&Candidate == Initial) {
          Current = &Candidate;
          break;
        }
      if (Current)
        Changed |= runOne(Binding, Function, Current, Access);
    }
    return Changed;
  }

  PluginTaskContext &Task;
  std::string ActiveTargetSchemaDigest;
  std::vector<PassBinding> Bindings;
  std::mutex GenerationMutex;
  DenseMap<MachineFunction *, uint64_t> Generations;
};

namespace {

class PluginMachinePass final : public MachineFunctionPass {
public:
  static char ID;

  PluginMachinePass(MIRPassPlan::Impl &PlanValue,
                    const PassBinding &BindingValue)
      : MachineFunctionPass(ID), Plan(PlanValue), Binding(BindingValue) {}

  StringRef getPassName() const override { return Binding.PassID; }

  void getAnalysisUsage(AnalysisUsage &Usage) const override {
    for (NevercMIRBuiltinAnalysis Analysis : Binding.RequiredAnalyses) {
      switch (Analysis) {
      case NEVERC_MIR_ANALYSIS_LIVE_INTERVALS:
        Usage.addRequired<LiveIntervals>();
        break;
      case NEVERC_MIR_ANALYSIS_LIVE_VARIABLES:
        // LLVM's legacy LiveVariables pass cannot be safely reconstructed
        // after SSA destruction. The plugin bridge supplies a read-only
        // sidecar dataflow result instead.
        break;
      case NEVERC_MIR_ANALYSIS_SLOT_INDEXES:
        Usage.addRequired<SlotIndexes>();
        break;
      case NEVERC_MIR_ANALYSIS_DOMINATOR_TREE:
      case NEVERC_MIR_ANALYSIS_LOOP_INFO:
        // Read-only sidecars avoid perturbing target-owned legacy analyses.
        break;
      case NEVERC_MIR_ANALYSIS_REGISTER_PRESSURE:
        break;
      }
    }
    for (NevercMIRBuiltinAnalysis Analysis : Binding.PreservedAnalyses) {
      switch (Analysis) {
      case NEVERC_MIR_ANALYSIS_LIVE_INTERVALS:
        Usage.addPreserved<LiveIntervals>();
        Usage.addPreserved<MachineDominatorTree>();
        Usage.addPreserved<MachineLoopInfo>();
        break;
      case NEVERC_MIR_ANALYSIS_LIVE_VARIABLES:
        // Sidecar results are scoped to one invocation and generation.
        break;
      case NEVERC_MIR_ANALYSIS_SLOT_INDEXES:
        Usage.addPreserved<SlotIndexes>();
        break;
      case NEVERC_MIR_ANALYSIS_DOMINATOR_TREE:
      case NEVERC_MIR_ANALYSIS_LOOP_INFO:
        // Sidecar results are scoped to one invocation and generation.
        break;
      case NEVERC_MIR_ANALYSIS_REGISTER_PRESSURE:
        break;
      }
    }
    MachineFunctionPass::getAnalysisUsage(Usage);
  }

  bool runOnMachineFunction(MachineFunction &Function) override {
    AnalysisAccess Access;
    if (contains(Binding.RequiredAnalyses,
                 NEVERC_MIR_ANALYSIS_LIVE_INTERVALS))
      Access.Intervals = &getAnalysis<LiveIntervals>();
    if (contains(Binding.RequiredAnalyses,
                 NEVERC_MIR_ANALYSIS_SLOT_INDEXES))
      Access.Indexes = &getAnalysis<SlotIndexes>();
    return Plan.run(Binding, Function, Access);
  }

private:
  MIRPassPlan::Impl &Plan;
  const PassBinding &Binding;
};

char PluginMachinePass::ID = 0;

class PluginMachineModulePass final : public ModulePass {
public:
  static char ID;

  PluginMachineModulePass(MIRPassPlan::Impl &PlanValue,
                          const PassBinding &BindingValue)
      : ModulePass(ID), Plan(PlanValue), Binding(BindingValue) {}

  StringRef getPassName() const override { return Binding.PassID; }

  void getAnalysisUsage(AnalysisUsage &Usage) const override {
    Usage.addRequired<MachineModuleInfoWrapperPass>();
    Usage.addPreserved<MachineModuleInfoWrapperPass>();
    Usage.setPreservesAll();
  }

  bool runOnModule(Module &ModuleValue) override {
    MachineModuleInfo &MMI =
        getAnalysis<MachineModuleInfoWrapperPass>().getMMI();
    for (Function &FunctionValue : ModuleValue)
      if (MachineFunction *Function = MMI.getMachineFunction(FunctionValue))
        Plan.run(Binding, *Function, {});
    // Plugin callbacks mutate MachineFunctions, not the LLVM IR Module.
    return false;
  }

private:
  MIRPassPlan::Impl &Plan;
  const PassBinding &Binding;
};

char PluginMachineModulePass::ID = 0;

} // namespace

Expected<std::shared_ptr<MIRPassPlan>>
MIRPassPlan::create(PluginTaskContext &Task) {
  auto State = std::make_unique<Impl>(Task);
  for (const auto &Module : Task.session().plugins()) {
    const PluginPublishedRegistration *Registration = Module->registration();
    if (!Registration)
      continue;
    for (const PluginRegistrationRecord &Record : Registration->records()) {
      if (Record.Kind != PluginRegistrationKind::MIRPass)
        continue;
      PassBinding Binding;
      Binding.PluginID = Module->descriptor().PluginID;
      Binding.PassID = Record.PassID;
      Binding.RequiredTargetSchemaDigest = Record.SchemaDigest;
      Binding.Descriptor = Record.MIRPass;
      Binding.Descriptor.PassID = {};
      Binding.RequiredAnalyses = Record.MIRRequiredAnalyses;
      Binding.PreservedAnalyses = Record.MIRPreservedAnalyses;
      State->Bindings.push_back(std::move(Binding));
    }
  }
  return std::shared_ptr<MIRPassPlan>(
      new MIRPassPlan(std::move(State)));
}

MIRPassPlan::MIRPassPlan(std::unique_ptr<Impl> StateValue)
    : State(std::move(StateValue)) {}

MIRPassPlan::~MIRPassPlan() = default;

bool MIRPassPlan::empty() const { return State->Bindings.empty(); }

bool MIRPassPlan::requiresSerialCodeGen() const {
  return llvm::any_of(State->Bindings, [](const PassBinding &Binding) {
    return Binding.Descriptor.Level == NEVERC_MIR_PASS_LEVEL_MODULE;
  });
}

void MIRPassPlan::addPasses(TargetPassConfig &TPC,
                            MachinePipelineHookPoint Point) {
  NevercInterfaceID Phase = phaseForHook(Point);
  for (const PassBinding &Binding : State->Bindings)
    if (sameInterface(Binding.Descriptor.Phase, Phase)) {
      if (Binding.Descriptor.Level == NEVERC_MIR_PASS_LEVEL_MODULE)
        TPC.addExternalPass(new PluginMachineModulePass(*State, Binding));
      else
        TPC.addExternalPass(new PluginMachinePass(*State, Binding));
    }
}

Error registerPluginMIRPassInterface(PluginProcessServices &Services) {
  NevercInterfaceID AnalysisInterface{NEVERC_INTERFACE_MIR_ANALYSIS_HIGH,
                                      NEVERC_INTERFACE_MIR_ANALYSIS_LOW};
  if (Error E = Services.interfaces().registerInterface(
          AnalysisInterface, NEVERC_MIR_ANALYSIS_INTERFACE_STABILITY,
          &ProcessAnalysisAPI, {}))
    return E;
  NevercInterfaceID PassInterface{NEVERC_INTERFACE_MIR_PASS_HIGH,
                                  NEVERC_INTERFACE_MIR_PASS_LOW};
  return Services.interfaces().registerInterface(
      PassInterface, NEVERC_MIR_PASS_INTERFACE_STABILITY, &PassAPI, {});
}

} // namespace neverc::plugin
