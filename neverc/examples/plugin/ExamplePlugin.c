/*
 * ExamplePlugin.c — Pure C neverc out-of-tree pass plugin.
 *
 * Demonstrates:
 *   - Module-level passes at PRE_OPT / POST_OPT / PIPELINE_START / LAST hooks
 *   - IR mutation: function-entry instrumentation via IRBuilder
 *   - ModuleCollectAllInstructions — host-side batch collection of every
 *     instruction into a contiguous flat array, zero vtable navigation
 *     overhead (single call replaces count→alloc→fill dance)
 *   - ModuleCollectAllFunctions / ModuleCollectAllGlobals — same pattern
 *   - MIR-level pass at SC_BEFORE_PREEMIT hook (shellcode flow)
 *   - Binary-level pass at SC_POST_EXTRACT hook (shellcode flow)
 *   - LTO pipeline passes at LTO_PRE_OPT / LTO_POST_OPT hooks
 *   - Linker passes at LINK_PRE_LAYOUT / POST_LAYOUT / POST_EMIT hooks
 *   - Host-provided string utilities (StrReplace, StrToLower, StrFindChar, etc.)
 *   - StrJoin / StrSplit / StrHash for batch string operations
 *   - NEVERC_ALLOC_ARRAY macro for typed, overflow-safe array allocation
 *   - HookPointGetName for runtime hook-name resolution (no static strings)
 *   - MemDup for single-call allocate-and-copy
 *   - Formatted diagnostics via DiagNoteF — no manual StrFormat/Free dance
 *   - Zero LLVM C++ or CRT dependencies — everything goes through the vtable
 *
 * Build:
 *   cc -shared -o ExamplePlugin.dll ExamplePlugin.c -I<neverc>/include
 *
 * Usage:
 *   neverc -fplugin-pass=./ExamplePlugin.dll input.c -o output.obj
 */

#include "neverc/Plugin/NevercPluginAPI.h"

#define PLUGIN_TAG "[example-plugin] "

/* ---- Module pass: print module info and count defined functions ---- */
static int functionCounterPass(NevercModuleRef M,
                               const NevercHostAPI *API,
                               void *UserData) {
  (void)UserData;

  const char *Triple = API->ModuleGetTargetTriple(M);
  API->DiagNoteF(PLUGIN_TAG "Target: %s", Triple);

  unsigned FnCount;
  NevercValueRef *Fns = NEVERC_API_FN(API, ModuleCollectAllFunctions)
                            ? API->ModuleCollectAllFunctions(M, &FnCount)
                            : NULL;
  if (!Fns) {
    API->DiagNoteF(PLUGIN_TAG "Found 0 defined functions");
    return 0;
  }

  unsigned Defined = 0;
  unsigned I;
  for (I = 0; I < FnCount; I++) {
    if (!API->FunctionIsDeclaration(Fns[I]))
      Defined++;
  }
  API->Free(Fns);

  API->DiagNoteF(PLUGIN_TAG "Found %u defined functions", Defined);
  return 0;
}

/* ---- Module pass: list function names ---- */
static int functionListPass(NevercModuleRef M,
                            const NevercHostAPI *API,
                            void *UserData) {
  (void)UserData;
  unsigned FnCount;
  NevercValueRef *Fns = NEVERC_API_FN(API, ModuleCollectAllFunctions)
                            ? API->ModuleCollectAllFunctions(M, &FnCount)
                            : NULL;
  if (!Fns)
    return 0;

  unsigned I;
  for (I = 0; I < FnCount; I++) {
    if (!API->FunctionIsDeclaration(Fns[I]))
      API->DiagNoteF(PLUGIN_TAG "Processing function: %s",
                     API->ValueGetName(Fns[I]));
  }
  API->Free(Fns);
  return 0;
}

/*
 * Module pass: function-entry instrumentation (IR mutation demo).
 *
 * Inserts a call to an external tracing function at the entry of every
 * defined function:
 *
 *   declare void @__neverc_plugin_trace(i8* %fn_name)
 *
 * Shows how to use the C API to:
 *   1. Create types and function declarations
 *   2. Build instructions with IRBuilder
 *   3. Create global string constants via BuildGlobalStringPtr
 *   4. Insert code at function entry points
 *
 * NOTE: This pass creates a call to an EXTERNAL symbol that must be
 * provided by the linker.  When compiling pure shellcode
 * (-fshellcode), the extractor rejects unresolved relocations, so
 * we ask the host whether shellcode mode is active and skip
 * instrumentation in that case.  Real plugins targeting shellcode
 * should never introduce external symbols.
 */
static int functionEntryInstrPass(NevercModuleRef M,
                                  const NevercHostAPI *API,
                                  void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, BuildGlobalStringPtr))
    return 0;

  if (NEVERC_API_FN(API, HostIsShellcodeMode) &&
      API->HostIsShellcodeMode()) {
    API->DiagNoteF(PLUGIN_TAG "Shellcode mode active; skipping trace "
                   "instrumentation (would break shellcode extraction)");
    return 0;
  }

  NevercContextRef Ctx = API->ModuleGetContext(M);
  if (!Ctx)
    return 0;

  NevercTypeRef VoidTy = API->TypeGetVoid(Ctx);
  NevercTypeRef PtrTy = API->TypeGetPtr(Ctx);

  NevercTypeRef ParamTys[1];
  ParamTys[0] = PtrTy;
  NevercTypeRef TraceFnTy = API->TypeGetFunction(VoidTy, ParamTys, 1, 0);

  NevercValueRef TraceFn =
      API->ModuleGetNamedFunction(M, "__neverc_plugin_trace");
  if (!TraceFn)
    TraceFn = API->ModuleAddFunction(M, "__neverc_plugin_trace", TraceFnTy);

  unsigned FnCount;
  NevercValueRef *Fns = NEVERC_API_FN(API, ModuleCollectAllFunctions)
                            ? API->ModuleCollectAllFunctions(M, &FnCount)
                            : NULL;
  if (!Fns)
    return 0;

  int Modified = 0;
  NevercBuilderRef Builder = API->BuilderCreate(Ctx);

  unsigned I;
  for (I = 0; I < FnCount; I++) {
    if (API->FunctionIsDeclaration(Fns[I]) || Fns[I] == TraceFn)
      continue;
    NevercBasicBlockRef EntryBB = API->FunctionGetFirstBB(Fns[I]);
    if (!EntryBB)
      continue;
    NevercValueRef FirstInst = API->BBGetFirstInst(EntryBB);
    if (FirstInst)
      API->BuilderSetInsertPointBefore(Builder, FirstInst);
    else
      API->BuilderSetInsertPoint(Builder, EntryBB);

    NevercValueRef NamePtr = API->BuildGlobalStringPtr(
        Builder, API->ValueGetName(Fns[I]), "fn.name");
    NevercValueRef CallArgs[1];
    CallArgs[0] = NamePtr;
    API->BuildCall(Builder, TraceFnTy, TraceFn, CallArgs, 1, "");
    Modified = 1;
  }

  API->BuilderDispose(Builder);
  API->Free(Fns);

  if (Modified)
    API->DiagNoteF(PLUGIN_TAG "Inserted function-entry tracing calls");
  return Modified;
}

/*
 * MIR pass: detailed MachineFunction analysis.
 *
 * Uses batch collection (MFuncCollectBBs / MBBCollectInstructions) when
 * available, falling back to per-element iteration otherwise.  The batch
 * path makes 2 vtable calls per BB (collect + count) instead of O(instrs)
 * GetNextInst calls, cutting indirect-call overhead on large functions.
 */
static int mirInstrCountPass(NevercMachineFuncRef MF,
                             const NevercHostAPI *API,
                             void *UserData) {
  (void)UserData;
  const char *FnName = API->MFuncGetName(MF);
  int InstrCount = 0;
  int RegOpCount = 0;
  int ImmOpCount = 0;

  unsigned BBCount = API->MFuncGetBBCount(MF);
  if (BBCount == 0) {
    API->DiagNoteF(PLUGIN_TAG "MIR '%s': empty", FnName);
    return 0;
  }

  if (NEVERC_API_FN(API, MFuncCollectBBs)) {
    NevercMachineBBRef *MBBs =
        NEVERC_ALLOC_ARRAY(API, NevercMachineBBRef, BBCount);
    if (!MBBs)
      return 0;
    API->MFuncCollectBBs(MF, MBBs);

    unsigned B;
    for (B = 0; B < BBCount; B++) {
      unsigned IC = API->MBBGetInstCount(MBBs[B]);
      InstrCount += (int)IC;
      if (IC == 0)
        continue;

      NevercMachineInstrRef *MIs =
          NEVERC_ALLOC_ARRAY(API, NevercMachineInstrRef, IC);
      if (!MIs)
        continue;
      API->MBBCollectInstructions(MBBs[B], MIs);

      unsigned J;
      for (J = 0; J < IC; J++) {
        unsigned NumOps = API->MInstGetNumOperands(MIs[J]);
        unsigned K;
        for (K = 0; K < NumOps; K++) {
          if (API->MInstGetOperandIsReg(MIs[J], K))
            RegOpCount++;
          if (API->MInstGetOperandIsImm(MIs[J], K))
            ImmOpCount++;
        }
      }
      API->Free(MIs);
    }
    API->Free(MBBs);
  } else {
    NevercMachineBBRef MBB = API->MFuncGetFirstBB(MF);
    while (MBB) {
      NevercMachineInstrRef MI = API->MBBGetFirstInst(MBB);
      while (MI) {
        InstrCount++;
        unsigned NumOps = API->MInstGetNumOperands(MI);
        unsigned I;
        for (I = 0; I < NumOps; I++) {
          if (API->MInstGetOperandIsReg(MI, I))
            RegOpCount++;
          if (API->MInstGetOperandIsImm(MI, I))
            ImmOpCount++;
        }
        MI = API->MBBGetNextInst(MI);
      }
      MBB = API->MFuncGetNextBB(MBB);
    }
  }

  API->DiagNoteF(PLUGIN_TAG "MIR '%s': %u BBs, %d instrs, %d reg ops, "
                 "%d imm ops",
                 FnName, BBCount, InstrCount, RegOpCount, ImmOpCount);
  return 0;
}

/* ---- Binary pass: log extracted shellcode size ---- */
static int binarySizePass(uint8_t **Data, uint64_t *Len,
                          uint64_t *Capacity,
                          const NevercHostAPI *API,
                          void *UserData) {
  (void)Data;
  (void)Capacity;
  (void)UserData;

  API->DiagNoteF(PLUGIN_TAG "Binary: %" PRIu64 " bytes", *Len);
  return 0;
}

/*
 * Binary pass: demonstrate BinaryResize — append a 16-byte NOP sled.
 *
 * Uses BinaryResize to grow the buffer, then fills the new tail with
 * 0x90 (x86 NOP).  This is a common pattern for binary patching passes
 * that need to add code-cave space or alignment padding.
 */
static int binaryNopSledPass(uint8_t **Data, uint64_t *Len,
                             uint64_t *Capacity,
                             const NevercHostAPI *API,
                             void *UserData) {
  (void)UserData;
  uint64_t OldLen = *Len;
  uint64_t NewLen = OldLen + 16;

  if (!API->BinaryResize(Data, Len, Capacity, NewLen)) {
    API->DiagWarningF(PLUGIN_TAG "BinaryResize failed");
    return 0;
  }

  API->MemSet(*Data + OldLen, 0x90, 16);

  API->DiagNoteF(PLUGIN_TAG "Appended 16-byte NOP sled "
                 "(%" PRIu64 " -> %" PRIu64 " bytes)",
                 OldLen, NewLen);
  return 1;
}

/*
 * Module pass: demonstrate GEPGetIndex for detailed GEP analysis.
 *
 * Walks all GEP instructions, reads each index via the new GEPGetIndex API,
 * and reports how many use constant-int indices vs dynamic indices.
 */
static int gepIndexDemoPass(NevercModuleRef M,
                            const NevercHostAPI *API,
                            void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, GEPGetIndex) ||
      !NEVERC_API_FN(API, ModuleCollectAllInstructions))
    return 0;

  unsigned InstCount;
  NevercValueRef *Insts = API->ModuleCollectAllInstructions(M, &InstCount);
  if (!Insts)
    return 0;

  int TotalGEPs = 0, ConstIdxCount = 0, DynIdxCount = 0;
  unsigned I;
  for (I = 0; I < InstCount; I++) {
    if (!API->InstIsGEP(Insts[I]))
      continue;
    TotalGEPs++;
    unsigned NumIdx = API->GEPGetNumIndices(Insts[I]);
    unsigned Idx;
    for (Idx = 0; Idx < NumIdx; Idx++) {
      NevercValueRef IdxVal = API->GEPGetIndex(Insts[I], Idx);
      if (IdxVal && API->ValueIsConstantInt(IdxVal))
        ConstIdxCount++;
      else
        DynIdxCount++;
    }
  }
  API->Free(Insts);

  API->DiagNoteF(PLUGIN_TAG "GEP index analysis: %d GEPs, "
                 "%d const indices, %d dynamic indices",
                 TotalGEPs, ConstIdxCount, DynIdxCount);
  return 0;
}

/* ---- Module pass: demonstrate Value kind queries & GlobalVariable ops ---- */
static int valueKindDemoPass(NevercModuleRef M,
                             const NevercHostAPI *API,
                             void *UserData) {
  (void)UserData;
  unsigned FuncCount = API->ModuleGetFunctionCount(M);
  unsigned GlobalCount = API->ModuleGetGlobalCount(M);

  API->DiagNoteF(PLUGIN_TAG "ValueKind: %u functions, %u globals",
                 FuncCount, GlobalCount);
  return 0;
}

/* ---- Module pass: demonstrate struct type creation ---- */
static int structTypeDemoPass(NevercModuleRef M,
                              const NevercHostAPI *API,
                              void *UserData) {
  (void)UserData;
  NevercContextRef Ctx = API->ModuleGetContext(M);
  if (!Ctx)
    return 0;

  NevercTypeRef I32Ty = API->TypeGetInt32(Ctx);
  NevercTypeRef PtrTy = API->TypeGetPtr(Ctx);
  NevercTypeRef Fields[2];
  Fields[0] = I32Ty;
  Fields[1] = PtrTy;

  NevercTypeRef ST = API->TypeGetStruct(Ctx, Fields, 2, 0);
  if (ST && API->TypeIsStruct(ST))
    API->DiagNoteF(PLUGIN_TAG "Created struct with %u fields",
                   API->StructGetNumElements(ST));
  return 0;
}

/* ---- Module pass: demonstrate module stats & named global lookup ---- */
static int moduleStatsPass(NevercModuleRef M,
                           const NevercHostAPI *API,
                           void *UserData) {
  (void)UserData;
  unsigned FnCount = API->ModuleGetFunctionCount(M);
  unsigned GvCount = API->ModuleGetGlobalCount(M);

  API->DiagNoteF(PLUGIN_TAG "Module stats: %u functions, %u globals",
                 FnCount, GvCount);

  NevercValueRef Main = API->ModuleGetNamedGlobal(M, "main_var");
  if (Main)
    API->DiagNoteF(PLUGIN_TAG "Found global 'main_var'");

  return 0;
}

/*
 * Module pass: instruction type analysis.
 *
 * Uses ModuleCollectAllInstructions to gather every instruction into a
 * contiguous flat array in a single vtable call, then classifies each
 * one with a linear scan.
 */
static int instrTypeDemoPass(NevercModuleRef M,
                             const NevercHostAPI *API,
                             void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, InstIsCall) ||
      !NEVERC_API_FN(API, ModuleCollectAllInstructions))
    return 0;

  unsigned InstCount;
  NevercValueRef *Insts = API->ModuleCollectAllInstructions(M, &InstCount);
  if (!Insts)
    return 0;

  unsigned Calls = 0, Branches = 0, Loads = 0, Stores = 0;
  unsigned Allocas = 0, PHIs = 0, GEPs = 0;

  unsigned I;
  for (I = 0; I < InstCount; I++) {
    if (API->InstIsCall(Insts[I]))
      Calls++;
    if (API->InstIsBranch(Insts[I]))
      Branches++;
    if (API->InstIsLoad(Insts[I]))
      Loads++;
    if (API->InstIsStore(Insts[I]))
      Stores++;
    if (API->InstIsAlloca(Insts[I]))
      Allocas++;
    if (API->InstIsPHI(Insts[I]))
      PHIs++;
    if (API->InstIsGEP(Insts[I]))
      GEPs++;
  }
  API->Free(Insts);

  API->DiagNoteF(PLUGIN_TAG "Instr stats: %u calls, %u branches, "
                 "%u loads, %u stores, %u allocas, %u phis, %u geps",
                 Calls, Branches, Loads, Stores, Allocas, PHIs, GEPs);
  return 0;
}

/*
 * Module pass: demonstrate CmpGetPredicate and ICmp/FCmp predicate analysis.
 *
 * Uses InstIsICmp / InstIsFCmp kind queries to identify comparison
 * instructions, then reads their predicates via CmpGetPredicate:
 *   - Equality: ICMP_EQ / ICMP_NE
 *   - Relational: all other ICmp predicates (SGT, SLT, UGE, ...)
 *   - FCmp: all floating-point comparisons
 *
 * IMPORTANT: Always call InstIsICmp / InstIsFCmp BEFORE CmpGetPredicate,
 * because CmpGetPredicate returns 0 for non-CmpInst values, which
 * collides with NEVERC_FCMP_FALSE.
 */
static int icmpAnalysisDemoPass(NevercModuleRef M,
                                const NevercHostAPI *API,
                                void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, InstIsICmp) ||
      !NEVERC_API_FN(API, ModuleCollectAllInstructions))
    return 0;

  unsigned InstCount;
  NevercValueRef *Insts = API->ModuleCollectAllInstructions(M, &InstCount);
  if (!Insts)
    return 0;

  int EqCount = 0, RelCount = 0, FCmpCount = 0;
  unsigned I;
  for (I = 0; I < InstCount; I++) {
    if (API->InstIsICmp(Insts[I])) {
      unsigned Pred = API->CmpGetPredicate(Insts[I]);
      if (Pred == NEVERC_ICMP_EQ || Pred == NEVERC_ICMP_NE)
        EqCount++;
      else
        RelCount++;
    } else if (API->InstIsFCmp(Insts[I])) {
      FCmpCount++;
    }
  }
  API->Free(Insts);

  API->DiagNoteF(PLUGIN_TAG "Cmp analysis: %d eq, %d relational, "
                 "%d fcmp",
                 EqCount, RelCount, FCmpCount);
  return 0;
}

/*
 * Module pass: demonstrate control flow analysis using successor navigation.
 *
 * Uses the new InstGetNumSuccessors/InstGetSuccessor API to walk the CFG
 * from terminators, counting branch targets.
 */
static int cfgAnalysisDemoPass(NevercModuleRef M,
                               const NevercHostAPI *API,
                               void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, InstGetNumSuccessors) ||
      !NEVERC_API_FN(API, ModuleCollectAllFunctions))
    return 0;

  unsigned FnCount;
  NevercValueRef *Fns = API->ModuleCollectAllFunctions(M, &FnCount);
  if (!Fns)
    return 0;

  int TotalEdges = 0;
  int ConditionalTerms = 0;
  unsigned FI, BI;
  for (FI = 0; FI < FnCount; FI++) {
    if (API->FunctionIsDeclaration(Fns[FI]))
      continue;
    unsigned BBCount = API->FunctionGetBBCount(Fns[FI]);
    if (BBCount == 0)
      continue;
    NevercBasicBlockRef *BBs =
        NEVERC_ALLOC_ARRAY(API, NevercBasicBlockRef, BBCount);
    if (!BBs)
      continue;
    API->FunctionCollectBBs(Fns[FI], BBs);
    for (BI = 0; BI < BBCount; BI++) {
      NevercValueRef Term = API->BBGetTerminator(BBs[BI]);
      if (Term) {
        unsigned NumSucc = API->InstGetNumSuccessors(Term);
        TotalEdges += (int)NumSucc;
        if (NumSucc > 1)
          ConditionalTerms++;
      }
    }
    API->Free(BBs);
  }
  API->Free(Fns);

  API->DiagNoteF(PLUGIN_TAG "CFG: %d edges, "
                 "%d conditional terminators",
                 TotalEdges, ConditionalTerms);
  return 0;
}

/*
 * Module pass: demonstrate global variable linkage and alignment ops.
 *
 * Uses GlobalGetLinkage, GlobalGetAlignment, and ValueSetName API.
 */
static int globalInfoDemoPass(NevercModuleRef M,
                              const NevercHostAPI *API,
                              void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, GlobalGetLinkage) ||
      !NEVERC_API_FN(API, ModuleCollectAllGlobals))
    return 0;

  unsigned GvCount;
  NevercValueRef *Gvs = API->ModuleCollectAllGlobals(M, &GvCount);
  if (!Gvs)
    return 0;

  int InternalCount = 0;
  int ExternalCount = 0;
  unsigned I;
  for (I = 0; I < GvCount; I++) {
    if (!API->ValueIsGlobalVariable(Gvs[I]))
      continue;
    unsigned Linkage = API->GlobalGetLinkage(Gvs[I]);
    if (Linkage == NEVERC_LINKAGE_INTERNAL ||
        Linkage == NEVERC_LINKAGE_PRIVATE)
      InternalCount++;
    else
      ExternalCount++;
  }
  API->Free(Gvs);

  API->DiagNoteF(PLUGIN_TAG "Globals: %d internal, %d external",
                 InternalCount, ExternalCount);
  return 0;
}

/*
 * Module pass: remove dead internal functions (IR mutation demo).
 *
 * Demonstrates a complete analyze-then-modify workflow:
 *   1. Iterate all functions and check linkage via FunctionGetLinkage
 *   2. For internal/private functions, walk the use chain via
 *      ValueGetFirstUse / UseGetNext to detect zero-use functions
 *   3. Remove dead functions with ModuleRemoveFunction
 *   4. Report removals via diagnostics
 *
 * Only removes functions with NEVERC_LINKAGE_INTERNAL or _PRIVATE
 * linkage and zero uses, so it's safe even if the module has
 * external entry points.
 */
static int deadInternalRemovalPass(NevercModuleRef M,
                                   const NevercHostAPI *API,
                                   void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, ModuleRemoveFunction) ||
      !NEVERC_API_FN(API, ModuleCollectAllFunctions))
    return 0;

  unsigned FnCount;
  NevercValueRef *Fns = API->ModuleCollectAllFunctions(M, &FnCount);
  if (!Fns)
    return 0;

  NevercValueRef *ToRemove =
      NEVERC_ALLOC_ARRAY(API, NevercValueRef, FnCount);
  if (!ToRemove) {
    API->Free(Fns);
    return 0;
  }

  unsigned RemoveCount = 0;
  unsigned FI;
  for (FI = 0; FI < FnCount; FI++) {
    if (API->FunctionIsDeclaration(Fns[FI]))
      continue;
    unsigned Linkage = API->FunctionGetLinkage(Fns[FI]);
    if ((Linkage == NEVERC_LINKAGE_INTERNAL ||
         Linkage == NEVERC_LINKAGE_PRIVATE) &&
        API->ValueGetNumUses(Fns[FI]) == 0)
      ToRemove[RemoveCount++] = Fns[FI];
  }
  API->Free(Fns);

  unsigned I;
  for (I = 0; I < RemoveCount; I++) {
    API->DiagNoteF(PLUGIN_TAG "Removing dead internal function: %s",
                   API->ValueGetName(ToRemove[I]));
    API->ModuleRemoveFunction(M, ToRemove[I]);
  }
  API->Free(ToRemove);

  if (RemoveCount > 0)
    API->DiagNoteF(PLUGIN_TAG "Removed %u dead internal functions",
                   RemoveCount);
  return RemoveCount > 0 ? 1 : 0;
}

/*
 * Module pass: demonstrate switch instruction inspection.
 *
 * Uses the new SwitchGetNumCases / SwitchGetCaseValue / SwitchGetCaseSuccessor
 * API to analyze switch-based control flow.
 */
static int switchAnalysisDemoPass(NevercModuleRef M,
                                  const NevercHostAPI *API,
                                  void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, SwitchGetNumCases) ||
      !NEVERC_API_FN(API, ModuleCollectAllInstructions))
    return 0;

  unsigned InstCount;
  NevercValueRef *Insts = API->ModuleCollectAllInstructions(M, &InstCount);
  if (!Insts)
    return 0;

  int SwitchCount = 0;
  int TotalCases = 0;
  unsigned I;
  for (I = 0; I < InstCount; I++) {
    if (!API->InstIsSwitch(Insts[I]))
      continue;
    SwitchCount++;
    TotalCases += (int)API->SwitchGetNumCases(Insts[I]);
  }
  API->Free(Insts);

  API->DiagNoteF(PLUGIN_TAG "Switch: %d switches, %d total cases",
                 SwitchCount, TotalCases);
  return 0;
}

/*
 * Module pass: comprehensive reverse iteration demo.
 *
 * Consolidates reverse walks over functions, globals, and aliases into a
 * single pass, exercising:
 *   - ModuleGetLastFunction / ModuleGetPrevFunction
 *   - FunctionGetLastBB / FunctionGetPrevBB
 *   - BBGetLastInst / InstGetPrevInst
 *   - ModuleGetLastGlobal / ModuleGetPrevGlobal
 *   - ModuleGetLastAlias / ModuleGetPrevAlias
 *   - ModuleGetSourceFileName, FunctionGetBBCount
 */
static int reverseIterationDemoPass(NevercModuleRef M,
                                    const NevercHostAPI *API,
                                    void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, ModuleGetPrevFunction) ||
      !NEVERC_API_FN(API, ModuleGetPrevGlobal) ||
      !NEVERC_API_FN(API, FunctionGetBBCount))
    return 0;

  API->DiagNoteF(PLUGIN_TAG "Source: %s",
                 API->ModuleGetSourceFileName(M));

  unsigned RevFnCount = 0;
  unsigned TotalTerms = 0;
  NevercValueRef F = API->ModuleGetLastFunction(M);
  while (F) {
    if (!API->FunctionIsDeclaration(F)) {
      RevFnCount++;
      NevercBasicBlockRef BB = API->FunctionGetLastBB(F);
      while (BB) {
        NevercValueRef I = API->BBGetLastInst(BB);
        while (I) {
          if (API->InstIsTerminator(I))
            TotalTerms++;
          I = API->InstGetPrevInst(I);
        }
        BB = API->FunctionGetPrevBB(BB);
      }
    }
    F = API->ModuleGetPrevFunction(F);
  }

  unsigned TotalGlobals = 0, WithInit = 0;
  NevercValueRef G = API->ModuleGetLastGlobal(M);
  while (G) {
    TotalGlobals++;
    if (API->GlobalHasInitializer(G))
      WithInit++;
    G = API->ModuleGetPrevGlobal(G);
  }

  unsigned AliasCount = 0;
  if (NEVERC_API_FN(API, ModuleGetAliasCount)) {
    AliasCount = API->ModuleGetAliasCount(M);
  } else if (NEVERC_API_FN(API, ModuleGetPrevAlias)) {
    NevercValueRef A = API->ModuleGetLastAlias(M);
    while (A) {
      AliasCount++;
      A = API->ModuleGetPrevAlias(A);
    }
  }

  API->DiagNoteF(PLUGIN_TAG "ReverseIter: %u fns/%u terminators, "
                 "%u globals (%u init), %u aliases",
                 RevFnCount, TotalTerms, TotalGlobals, WithInit, AliasCount);
  return 0;
}

/*
 * Module pass: demonstrate call-like instruction handling.
 *
 * Uses the fixed CallBase-aware API (CallGetCalledOperand, CallGetNumArgs,
 * CallGetFunctionType) and the new InstIsCallLike convenience to handle
 * both call and invoke instructions uniformly.
 */
static int callLikeDemoPass(NevercModuleRef M,
                            const NevercHostAPI *API,
                            void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, InstIsCallLike) ||
      !NEVERC_API_FN(API, ModuleCollectAllInstructions))
    return 0;

  unsigned InstCount;
  NevercValueRef *Insts = API->ModuleCollectAllInstructions(M, &InstCount);
  if (!Insts)
    return 0;

  int DirectCalls = 0, IndirectCalls = 0, Invokes = 0;
  unsigned I;
  for (I = 0; I < InstCount; I++) {
    if (!API->InstIsCallLike(Insts[I]))
      continue;
    NevercValueRef Callee = API->CallGetCalledOperand(Insts[I]);
    if (Callee && API->ValueIsFunction(Callee))
      DirectCalls++;
    else
      IndirectCalls++;
    if (API->InstIsInvoke(Insts[I]))
      Invokes++;
  }
  API->Free(Insts);

  API->DiagNoteF(PLUGIN_TAG "CallLike: %d direct, %d indirect, "
                 "%d invokes",
                 DirectCalls, IndirectCalls, Invokes);
  return 0;
}

/*
 * Module pass: demonstrate LoadGetType and GEPIsInBounds.
 *
 * Walks instructions, counting loads by pointer vs integer type,
 * and distinguishing inbounds GEPs from regular GEPs.
 */
static int memAccessDemoPass(NevercModuleRef M,
                             const NevercHostAPI *API,
                             void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, LoadGetType) ||
      !NEVERC_API_FN(API, GEPIsInBounds) ||
      !NEVERC_API_FN(API, ModuleCollectAllInstructions))
    return 0;

  unsigned InstCount;
  NevercValueRef *Insts = API->ModuleCollectAllInstructions(M, &InstCount);
  if (!Insts)
    return 0;

  int PtrLoads = 0, IntLoads = 0, OtherLoads = 0;
  int InboundsGEPs = 0, RegularGEPs = 0;
  unsigned I;
  for (I = 0; I < InstCount; I++) {
    if (API->InstIsLoad(Insts[I])) {
      NevercTypeRef LdTy = API->LoadGetType(Insts[I]);
      if (LdTy) {
        if (API->TypeIsPointer(LdTy))
          PtrLoads++;
        else if (API->TypeIsInteger(LdTy))
          IntLoads++;
        else
          OtherLoads++;
      }
    }
    if (API->InstIsGEP(Insts[I])) {
      if (API->GEPIsInBounds(Insts[I]))
        InboundsGEPs++;
      else
        RegularGEPs++;
    }
  }
  API->Free(Insts);

  API->DiagNoteF(PLUGIN_TAG "MemAccess: loads=%dptr/%dint/%dother, "
                 "GEPs=%dinbounds/%dregular",
                 PtrLoads, IntLoads, OtherLoads, InboundsGEPs, RegularGEPs);
  return 0;
}

/* Resolve a hook-point name from UserData carrying a NevercHookPoint value.
 * Falls back to "<unknown>" on hosts that predate the HookPointGetName API. */
static const char *getHookName(const NevercHostAPI *API, void *UserData) {
  unsigned Hook = (unsigned)(uintptr_t)UserData;
  if (NEVERC_API_FN(API, HookPointGetName))
    return API->HookPointGetName(Hook);
  return "<unknown>";
}

/*
 * Module pass: count functions and BBs at a pipeline stage.
 *
 * Registered at both PIPELINE_START and PIPELINE_LAST so the plugin
 * user can see the optimizer's impact on function/BB counts.  UserData
 * carries the NevercHookPoint enum value; the name is resolved via
 * HookPointGetName.
 *
 * Uses ModuleCollectAllFunctions + FunctionGetBBCount for a single
 * vtable call to collect functions, then O(N) queries for BB counts.
 */
static int pipelineStagePass(NevercModuleRef M,
                             const NevercHostAPI *API,
                             void *UserData) {
  const char *Stage = getHookName(API, UserData);
  unsigned FnCount;
  NevercValueRef *Fns = NEVERC_API_FN(API, ModuleCollectAllFunctions)
                            ? API->ModuleCollectAllFunctions(M, &FnCount)
                            : NULL;
  if (!Fns) {
    API->DiagNoteF(PLUGIN_TAG "%s: 0 functions", Stage);
    return 0;
  }

  unsigned Defined = 0, BBTotal = 0;
  unsigned I;
  for (I = 0; I < FnCount; I++) {
    if (API->FunctionIsDeclaration(Fns[I]))
      continue;
    Defined++;
    BBTotal += API->FunctionGetBBCount(Fns[I]);
  }
  API->Free(Fns);

  API->DiagNoteF(PLUGIN_TAG "%s: %u functions, %u BBs",
                 Stage, Defined, BBTotal);
  return 0;
}

/*
 * Module pass: demonstrate debug location and section API.
 *
 * Uses the new InstHasDebugLoc / InstGetDebugLocLine / InstCopyDebugLoc
 * API to count instructions with debug info, and GlobalGetSection to
 * report section placement of globals.
 */
static int debugLocDemoPass(NevercModuleRef M,
                            const NevercHostAPI *API,
                            void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, InstCopyDebugLoc) ||
      !NEVERC_API_FN(API, ModuleCollectAllInstructions))
    return 0;

  int WithLoc = 0, WithoutLoc = 0;
  unsigned InstCount;
  NevercValueRef *Insts = API->ModuleCollectAllInstructions(M, &InstCount);
  if (Insts) {
    unsigned I;
    for (I = 0; I < InstCount; I++) {
      if (API->InstHasDebugLoc(Insts[I]))
        WithLoc++;
      else
        WithoutLoc++;
    }
    API->Free(Insts);
  }

  int SectionedGlobals = 0;
  if (NEVERC_API_FN(API, GlobalGetSection) &&
      NEVERC_API_FN(API, ModuleCollectAllGlobals)) {
    unsigned GvCount;
    NevercValueRef *Gvs = API->ModuleCollectAllGlobals(M, &GvCount);
    if (Gvs) {
      unsigned I;
      for (I = 0; I < GvCount; I++) {
        if (API->ValueIsGlobalVariable(Gvs[I])) {
          const char *Sec = API->GlobalGetSection(Gvs[I]);
          if (Sec && Sec[0] != '\0')
            SectionedGlobals++;
        }
      }
      API->Free(Gvs);
    }
  }

  API->DiagNoteF(PLUGIN_TAG "DebugLoc: %d with, %d without, "
                 "%d sectioned globals",
                 WithLoc, WithoutLoc, SectionedGlobals);
  return 0;
}

/*
 * Module pass: demonstrate ValuePrintToString for structured logging.
 *
 * Prints the textual IR representation of the first defined function's
 * entry terminator using the host-allocated string API.
 * Caller is responsible for freeing the returned string via API->Free.
 */
static int valuePrintDemoPass(NevercModuleRef M,
                              const NevercHostAPI *API,
                              void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, ValuePrintToString))
    return 0;

  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    if (!API->FunctionIsDeclaration(F)) {
      NevercBasicBlockRef BB = API->FunctionGetFirstBB(F);
      if (BB) {
        NevercValueRef Term = API->BBGetTerminator(BB);
        if (Term) {
          char *Str = API->ValuePrintToString(Term);
          if (Str) {
            API->DiagNoteF(PLUGIN_TAG "Entry terminator of '%s': %s",
                           API->ValueGetName(F), Str);
            API->Free(Str);
          }
        }
      }
      break;
    }
    F = API->ModuleGetNextFunction(F);
  }
  return 0;
}

/*
 * Stage tracker — a single pass that logs which hook point it runs at.
 * Registered at every hook we don't otherwise demonstrate, so the user
 * can verify all pipeline insertion points are wired correctly.
 *
 * UserData carries the NevercHookPoint enum value (cast to void*).
 * The hook name is resolved at runtime via getHookName (defined above
 * pipelineStagePass).
 */
static int stageTrackerModulePass(NevercModuleRef M,
                                  const NevercHostAPI *API,
                                  void *UserData) {
  (void)M;
  API->DiagNoteF(PLUGIN_TAG "Stage: %s", getHookName(API, UserData));
  return 0;
}

static int stageTrackerMachinePass(NevercMachineFuncRef MF,
                                   const NevercHostAPI *API,
                                   void *UserData) {
  const char *FnName = API->MFuncGetName(MF);
  API->DiagNoteF(PLUGIN_TAG "Stage: %s (MF=%s)",
                 getHookName(API, UserData), FnName ? FnName : "?");
  return 0;
}

static int stageTrackerBinaryPass(uint8_t **Data, uint64_t *Len,
                                  uint64_t *Capacity,
                                  const NevercHostAPI *API,
                                  void *UserData) {
  (void)Data;
  (void)Capacity;
  API->DiagNoteF(PLUGIN_TAG "Stage: %s (binary=%" PRIu64 " bytes)",
                 getHookName(API, UserData), *Len);
  return 0;
}

/*
 * Module pass: demonstrate Intrinsic lookup and NamedMetadata access.
 *
 * Uses IntrinsicLookupByName to find an intrinsic ID, IntrinsicGetName
 * to print it, and ModuleGetNamedMetadata to read llvm.ident.
 */
static int intrinsicAndMDDemoPass(NevercModuleRef M,
                                  const NevercHostAPI *API,
                                  void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, IntrinsicLookupByName))
    return 0;

  unsigned TrapID = API->IntrinsicLookupByName("llvm.debugtrap");
  if (TrapID != 0) {
    char *Name = API->IntrinsicGetName(TrapID);
    if (Name) {
      API->DiagNoteF(PLUGIN_TAG "Intrinsic '%s' id=%u%s",
                     Name, TrapID,
                     API->IntrinsicIsOverloaded(TrapID)
                         ? " (overloaded)"
                         : " (not overloaded)");
      API->Free(Name);
    }
  }

  if (NEVERC_API_FN(API, ModuleGetNamedMetadata)) {
    NevercNamedMDRef Ident = API->ModuleGetNamedMetadata(M, "llvm.ident");
    if (Ident)
      API->DiagNoteF(PLUGIN_TAG "llvm.ident has %u operand(s)",
                     API->NamedMDGetNumOperands(Ident));
    else
      API->DiagNoteF(PLUGIN_TAG "No llvm.ident metadata found");
  }
  return 0;
}

/* ---- Module pass: demonstrate plugin argument access ---- */
static int pluginArgDemoPass(NevercModuleRef M,
                             const NevercHostAPI *API,
                             void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, PluginGetArg))
    return 0;

  API->DiagNoteF(PLUGIN_TAG "Plugin arg count: %u",
                 API->PluginGetArgCount());

  const char *Verbose = API->PluginGetArg("verbose");
  if (Verbose && Verbose[0] == '1' &&
      NEVERC_API_FN(API, ModuleCollectAllFunctions)) {
    unsigned FnCount;
    NevercValueRef *Fns = API->ModuleCollectAllFunctions(M, &FnCount);
    if (Fns) {
      unsigned I;
      for (I = 0; I < FnCount; I++) {
        if (!API->FunctionIsDeclaration(Fns[I]))
          API->DiagNoteF(PLUGIN_TAG "[verbose] Function: %s",
                         API->ValueGetName(Fns[I]));
      }
      API->Free(Fns);
    }
  }

  if (API->PluginHasArg("prefix")) {
    const char *Prefix = API->PluginGetArg("prefix");
    API->DiagNoteF(PLUGIN_TAG "Using custom prefix: %s",
                   Prefix ? Prefix : "(empty)");
  }

  return 0;
}

/* ---- Module pass: demonstrate DataLayout, PoisonValue, and vector API ---- */
static int dataLayoutDemoPass(NevercModuleRef M,
                              const NevercHostAPI *API,
                              void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, TypeSizeInBits))
    return 0;

  NevercContextRef C = API->ModuleGetContext(M);

  unsigned PtrBits = API->PointerSizeInBits(M, 0);
  API->DiagNoteF(PLUGIN_TAG "Pointer size: %u bits", PtrBits);

  NevercTypeRef I32Ty = API->TypeGetInt32(C);
  NevercTypeRef I64Ty = API->TypeGetInt64(C);
  NevercTypeRef DblTy = API->TypeGetDouble(C);

  uint64_t I32Size = API->TypeAllocSize(M, I32Ty);
  uint64_t I64Size = API->TypeAllocSize(M, I64Ty);
  unsigned I32Align = API->TypeABIAlignment(M, I32Ty);
  unsigned DblAlign = API->TypeABIAlignment(M, DblTy);

  API->DiagNoteF(PLUGIN_TAG "i32: %" PRIu64 " bytes, align %u | "
                 "i64: %" PRIu64 " bytes | double align %u",
                 I32Size, I32Align, I64Size, DblAlign);

  if (NEVERC_API_FN(API, ConstPoison)) {
    NevercValueRef Poison = API->ConstPoison(I32Ty);
    API->DiagNoteF(PLUGIN_TAG "PoisonValue(i32): isPoison=%d "
                   "isUndef=%d",
                   API->ValueIsPoison(Poison), API->ValueIsUndef(Poison));
  }

  if (NEVERC_API_FN(API, TypeGetFixedVector)) {
    NevercTypeRef V4I32 = API->TypeGetFixedVector(I32Ty, 4);
    API->DiagNoteF(PLUGIN_TAG "<4 x i32>: isVector=%d numElems=%u "
                   "sizeBits=%" PRIu64,
                   API->TypeIsVector(V4I32),
                   API->TypeGetVectorNumElements(V4I32),
                   API->TypeSizeInBits(M, V4I32));
  }

  if (NEVERC_API_FN(API, TypeGetPtrInAddrSpace)) {
    NevercTypeRef DefaultPtr = API->TypeGetPtr(C);
    NevercTypeRef AS1Ptr = API->TypeGetPtrInAddrSpace(C, 1);
    API->DiagNoteF(PLUGIN_TAG "ptr addrspace(0)=%u "
                   "ptr addrspace(1)=%u",
                   API->TypeGetPointerAddrSpace(DefaultPtr),
                   API->TypeGetPointerAddrSpace(AS1Ptr));
  }

  return 0;
}

/*
 * Module pass: demonstrate NSW/NUW arithmetic and COMDAT API.
 *
 * Creates an NSWAdd instruction to verify the overflow-flag builder works,
 * and queries COMDAT state on globals to demonstrate the Windows linkage API.
 */
static int nswAndComdatDemoPass(NevercModuleRef M,
                                const NevercHostAPI *API,
                                void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, BuildNSWAdd) ||
      !NEVERC_API_FN(API, GlobalGetComdat) ||
      !NEVERC_API_FN(API, ModuleCollectAllGlobals))
    return 0;

  unsigned GvCount;
  NevercValueRef *Gvs = API->ModuleCollectAllGlobals(M, &GvCount);
  if (!Gvs)
    return 0;

  int ComdatCount = 0;
  int ThreadLocalCount = 0;
  unsigned I;
  for (I = 0; I < GvCount; I++) {
    if (!API->ValueIsGlobalVariable(Gvs[I]))
      continue;
    if (API->GlobalGetComdat(Gvs[I]))
      ComdatCount++;
    if (NEVERC_API_FN(API, GlobalIsThreadLocal) &&
        API->GlobalIsThreadLocal(Gvs[I]))
      ThreadLocalCount++;
  }
  API->Free(Gvs);

  API->DiagNoteF(PLUGIN_TAG "Globals: %d with COMDAT, "
                 "%d thread-local",
                 ComdatCount, ThreadLocalCount);
  return 0;
}

/*
 * Module pass: demonstrate ConstGetAggregateElement and enum attributes.
 *
 * Walks globals with constant initializers and extracts aggregate elements.
 * Also queries and reports function enum attributes.
 */
static int constAggrAndAttrDemoPass(NevercModuleRef M,
                                    const NevercHostAPI *API,
                                    void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, ConstGetAggregateElement) ||
      !NEVERC_API_FN(API, FunctionHasEnumAttr) ||
      !NEVERC_API_FN(API, ModuleCollectAllGlobals) ||
      !NEVERC_API_FN(API, ModuleCollectAllFunctions))
    return 0;

  int AggrGlobals = 0;
  unsigned GvCount;
  NevercValueRef *Gvs = API->ModuleCollectAllGlobals(M, &GvCount);
  if (Gvs) {
    unsigned I;
    for (I = 0; I < GvCount; I++) {
      if (!API->ValueIsGlobalVariable(Gvs[I]) ||
          !API->GlobalHasInitializer(Gvs[I]))
        continue;
      NevercValueRef Init = API->GlobalGetInitializer(Gvs[I]);
      if (Init && API->ConstGetAggregateElement(Init, 0))
        AggrGlobals++;
    }
    API->Free(Gvs);
  }

  int NoInlineFns = 0;
  unsigned FnCount;
  NevercValueRef *Fns = API->ModuleCollectAllFunctions(M, &FnCount);
  if (Fns) {
    unsigned I;
    for (I = 0; I < FnCount; I++) {
      if (!API->FunctionIsDeclaration(Fns[I]) &&
          API->FunctionHasEnumAttr(Fns[I], 11 /* NoInline */))
        NoInlineFns++;
    }
    API->Free(Fns);
  }

  API->DiagNoteF(PLUGIN_TAG "%d globals with aggregate init, "
                 "%d noinline functions",
                 AggrGlobals, NoInlineFns);
  return 0;
}

/*
 * Module pass: demonstrate the string / memory utility API.
 *
 * Uses StrStartsWith, StrFindChar, StrSubstring, StrReplaceAll, StrToLower,
 * MemDup to classify function names by prefix, extract base names from mangled
 * intrinsics (e.g. "llvm.memcpy.p0.p0.i64" -> "memcpy"), and show name
 * transformation via StrReplaceAll and StrToLower.
 *
 * Tracks the first defined function during the walk to avoid a second
 * traversal for the StrReplaceAll/MemDup demo.
 */
static int stringUtilDemoPass(NevercModuleRef M,
                              const NevercHostAPI *API,
                              void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, StrStartsWith) ||
      !NEVERC_API_FN(API, ModuleCollectAllFunctions))
    return 0;

  unsigned FnCount;
  NevercValueRef *Fns = API->ModuleCollectAllFunctions(M, &FnCount);
  if (!Fns)
    return 0;

  int PrefixedCount = 0;
  int TotalDefined = 0;
  int IntrinsicCount = 0;
  NevercValueRef FirstDefined = NULL;

  unsigned FI;
  for (FI = 0; FI < FnCount; FI++) {
    const char *Name = API->ValueGetName(Fns[FI]);
    if (!API->FunctionIsDeclaration(Fns[FI])) {
      TotalDefined++;
      if (!FirstDefined)
        FirstDefined = Fns[FI];
      if (API->StrStartsWith(Name, "__neverc_") ||
          API->StrStartsWith(Name, "llvm."))
        PrefixedCount++;
    }
    if (!API->StrStartsWith(Name, "llvm."))
      continue;
    IntrinsicCount++;

    if (!NEVERC_API_FN(API, StrFindChar))
      continue;
    uint64_t DotPos = API->StrFindChar(Name + 5, '.');
    if (DotPos == (uint64_t)-1)
      continue;
    char *BaseName = API->StrSubstring(Name, 5, DotPos);
    if (!BaseName)
      continue;

    const char *Display = BaseName;
    char *Lower = NULL;
    if (NEVERC_API_FN(API, StrToLower)) {
      Lower = API->StrToLower(BaseName);
      if (Lower)
        Display = Lower;
    }
    API->DiagNoteF(PLUGIN_TAG "Intrinsic: %s", Display);
    if (Lower)
      API->Free(Lower);
    API->Free(BaseName);
  }

  if (FirstDefined && NEVERC_API_FN(API, StrReplaceAll)) {
    const char *Name = API->ValueGetName(FirstDefined);
    char *Demangled = API->StrReplaceAll(Name, "_", "::");
    if (Demangled) {
      API->DiagNoteF(PLUGIN_TAG "Demangled-like: %s -> %s",
                     Name, Demangled);
      API->Free(Demangled);
    }
    if (NEVERC_API_FN(API, MemDup) && NEVERC_API_FN(API, StrLen)) {
      uint64_t NameLen = API->StrLen(Name);
      char *Copy = (char *)API->MemDup(Name, NameLen + 1);
      if (Copy) {
        API->DiagNoteF(PLUGIN_TAG "MemDup name: %s", Copy);
        API->Free(Copy);
      }
    }
  }

  API->Free(Fns);

  API->DiagNoteF(PLUGIN_TAG "StringUtil: %d defined, %d prefixed, "
                 "%d intrinsics",
                 TotalDefined, PrefixedCount, IntrinsicCount);
  return 0;
}

/*
 * Module pass: demonstrate StrJoin/StrSplit/StrHash utility API.
 *
 * Collects defined function names, joins them with " | " via StrJoin,
 * then splits the result back apart with StrSplit to verify round-trip.
 * Hashes each name with StrHash for a collision-free identity check.
 */
static int strJoinSplitDemoPass(NevercModuleRef M,
                                const NevercHostAPI *API,
                                void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, StrJoin) ||
      !NEVERC_API_FN(API, StrSplit) ||
      !NEVERC_API_FN(API, StrHash) ||
      !NEVERC_API_FN(API, ModuleCollectAllFunctions))
    return 0;

  unsigned FnCount;
  NevercValueRef *Fns = API->ModuleCollectAllFunctions(M, &FnCount);
  if (!Fns)
    return 0;

  /* Collect defined function names. */
  const char **Names = NEVERC_ALLOC_ARRAY(API, const char *, FnCount);
  if (!Names) {
    API->Free(Fns);
    return 0;
  }
  unsigned Defined = 0;
  unsigned I;
  for (I = 0; I < FnCount; I++) {
    if (!API->FunctionIsDeclaration(Fns[I]))
      Names[Defined++] = API->ValueGetName(Fns[I]);
  }
  API->Free(Fns);

  if (Defined == 0) {
    API->Free(Names);
    return 0;
  }

  /* Join all names with " | " separator. */
  char *Joined = API->StrJoin(Names, Defined, " | ");

  /* Hash each name. */
  uint64_t XorHash = 0;
  for (I = 0; I < Defined; I++)
    XorHash ^= API->StrHash(Names[I]);

  API->Free(Names);

  if (!Joined)
    return 0;

  /* Split back apart and verify count matches. */
  unsigned SplitCount = 0;
  char **Parts = API->StrSplit(Joined, " | ", &SplitCount);
  API->Free(Joined);

  API->DiagNoteF(PLUGIN_TAG "StrJoin/Split: %u names, split back to %u, "
                 "xor-hash=%" PRIx64,
                 Defined, SplitCount, XorHash);

  if (Parts) {
    for (I = 0; I < SplitCount; I++)
      API->Free(Parts[I]);
    API->Free(Parts);
  }
  return 0;
}

/*
 * Module pass: hierarchical batch analysis without full instruction collect.
 *
 * Unlike passes that use ModuleCollectAllInstructions (which materialises
 * every instruction pointer into one flat array), this pass counts
 * instructions per BB via BBGetInstCount — no per-instruction pointer
 * is ever allocated.  This is the right pattern when you only need
 * aggregate counts at each level (functions / BBs / instructions) and
 * do not need to inspect individual instructions.
 */
static int batchAnalysisDemoPass(NevercModuleRef M,
                                 const NevercHostAPI *API,
                                 void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, ModuleCollectAllFunctions))
    return 0;

  unsigned FnCount;
  NevercValueRef *Funcs = API->ModuleCollectAllFunctions(M, &FnCount);
  if (!Funcs)
    return 0;

  unsigned TotalBBs = 0;
  unsigned TotalInsts = 0;
  unsigned DefinedFns = 0;

  unsigned I;
  for (I = 0; I < FnCount; I++) {
    if (API->FunctionIsDeclaration(Funcs[I]))
      continue;
    DefinedFns++;

    unsigned BBCount = API->FunctionGetBBCount(Funcs[I]);
    TotalBBs += BBCount;

    NevercBasicBlockRef *BBs =
        NEVERC_ALLOC_ARRAY(API, NevercBasicBlockRef, BBCount);
    if (!BBs)
      continue;
    API->FunctionCollectBBs(Funcs[I], BBs);

    unsigned J;
    for (J = 0; J < BBCount; J++)
      TotalInsts += API->BBGetInstCount(BBs[J]);
    API->Free(BBs);
  }
  API->Free(Funcs);

  API->DiagNoteF(PLUGIN_TAG "Batch: %u defined fns, "
                 "%u BBs, %u instructions",
                 DefinedFns, TotalBBs, TotalInsts);
  return 0;
}

/*
 * Module pass: LTO pipeline stage tracker.
 *
 * Registered at LTO_PRE_OPT and LTO_POST_OPT.  UserData carries the
 * NevercHookPoint enum value.
 */
static int ltoStagePass(NevercModuleRef M,
                        const NevercHostAPI *API,
                        void *UserData) {
  const char *Stage = getHookName(API, UserData);
  unsigned FnCount;
  NevercValueRef *Fns = NEVERC_API_FN(API, ModuleCollectAllFunctions)
                            ? API->ModuleCollectAllFunctions(M, &FnCount)
                            : NULL;
  unsigned Defined = 0;
  if (Fns) {
    unsigned I;
    for (I = 0; I < FnCount; I++) {
      if (!API->FunctionIsDeclaration(Fns[I]))
        Defined++;
    }
    API->Free(Fns);
  } else {
    FnCount = 0;
  }

  API->DiagNoteF(PLUGIN_TAG "%s: %u functions (%u defined)",
                 Stage, FnCount, Defined);
  return 0;
}

/*
 * Linker pass: symbol and section census.
 *
 * Walks the linker's symbol table and section list to demonstrate the
 * LINK_* hook API.  Reports counts of defined/undefined/local symbols
 * and the output format.  UserData carries the NevercHookPoint enum value.
 */
static int linkerCensusPass(const NevercHostAPI *API,
                            void *UserData) {
  const char *Stage = getHookName(API, UserData);
  int Defined = 0, Undefined = 0, LocalCount = 0, HiddenCount = 0;

  uint64_t DefinedSizeTotal = 0;
  NevercLinkerSymbolRef Sym = API->LinkGetFirstSymbol();
  while (Sym) {
    if (API->LinkSymbolIsDefined(Sym)) {
      Defined++;
      if (NEVERC_API_FN(API, LinkSymbolGetSize))
        DefinedSizeTotal += API->LinkSymbolGetSize(Sym);
    } else {
      Undefined++;
    }
    if (API->LinkSymbolIsLocal(Sym))
      LocalCount++;
    if (API->LinkSymbolIsHidden(Sym))
      HiddenCount++;
    Sym = API->LinkGetNextSymbol(Sym);
  }

  int Sections = 0;
  NevercLinkerSectionRef Sec = API->LinkGetFirstSection();
  while (Sec) {
    Sections++;
    Sec = API->LinkGetNextSection(Sec);
  }

  const char *FmtName = NEVERC_API_FN(API, LinkGetOutputFormatName)
                            ? API->LinkGetOutputFormatName()
                            : "unknown";

  API->DiagNoteF(PLUGIN_TAG "Link %s [%s]: %d defined, %d undef, "
                 "%d local, %d hidden syms; %d sections; defined size=%" PRIu64,
                 Stage, FmtName, Defined, Undefined,
                 LocalCount, HiddenCount, Sections,
                 DefinedSizeTotal);
  return 0;
}

/* ---- Registration ---- */
static void registerPasses(const NevercHostAPI *API, void *Registrar) {
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          pluginArgDemoPass, NULL, "example-plugin-args");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          functionCounterPass, NULL, "example-counter");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          functionEntryInstrPass, NULL, "example-instrument");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_POST_OPT,
                          functionListPass, NULL, "example-list");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_POST_OPT,
                          valueKindDemoPass, NULL, "example-value-kind");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_POST_OPT,
                          structTypeDemoPass, NULL, "example-struct-type");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          moduleStatsPass, NULL, "example-stats");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_POST_OPT,
                          instrTypeDemoPass, NULL, "example-instr-types");
  API->RegisterMachinePass(Registrar, NEVERC_HOOK_SC_BEFORE_PREEMIT,
                           mirInstrCountPass, NULL, "example-mir-count");
  API->RegisterBinaryPass(Registrar, NEVERC_HOOK_SC_POST_EXTRACT,
                          binarySizePass, NULL, "example-binary-size");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_POST_OPT,
                          icmpAnalysisDemoPass, NULL, "example-icmp-analysis");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_POST_OPT,
                          cfgAnalysisDemoPass, NULL, "example-cfg-analysis");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          globalInfoDemoPass, NULL, "example-global-info");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_POST_OPT,
                          deadInternalRemovalPass, NULL,
                          "example-dead-internal-removal");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_POST_OPT,
                          switchAnalysisDemoPass, NULL,
                          "example-switch-analysis");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_POST_OPT,
                          callLikeDemoPass, NULL,
                          "example-call-like");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_POST_OPT,
                          memAccessDemoPass, NULL,
                          "example-mem-access");

#define HOOK_UD(h) ((void *)(uintptr_t)(h))

  API->RegisterModulePass(Registrar, NEVERC_HOOK_PIPELINE_START,
                          pipelineStagePass,
                          HOOK_UD(NEVERC_HOOK_PIPELINE_START),
                          "example-pipeline-start");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PIPELINE_LAST,
                          pipelineStagePass,
                          HOOK_UD(NEVERC_HOOK_PIPELINE_LAST),
                          "example-pipeline-last");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_POST_OPT,
                          debugLocDemoPass, NULL,
                          "example-debugloc");
  API->RegisterBinaryPass(Registrar, NEVERC_HOOK_SC_POST_EXTRACT,
                          binaryNopSledPass, NULL,
                          "example-nop-sled");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_POST_OPT,
                          gepIndexDemoPass, NULL,
                          "example-gep-index");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_POST_OPT,
                          valuePrintDemoPass, NULL,
                          "example-value-print");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          intrinsicAndMDDemoPass, NULL,
                          "example-intrinsic-md");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          dataLayoutDemoPass, NULL,
                          "example-datalayout");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          reverseIterationDemoPass, NULL,
                          "example-reverse-iteration");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          nswAndComdatDemoPass, NULL,
                          "example-nsw-comdat");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          constAggrAndAttrDemoPass, NULL,
                          "example-const-aggr-attr");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          stringUtilDemoPass, NULL,
                          "example-string-util");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          strJoinSplitDemoPass, NULL,
                          "example-str-join-split");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          batchAnalysisDemoPass, NULL,
                          "example-batch-analysis");

  /* Stage trackers — verify every hook point is wired correctly.
     UserData carries the NevercHookPoint enum value; stage names are
     resolved at runtime via HookPointGetName. */

  /* Normal flow — MIR hooks */
  API->RegisterMachinePass(Registrar, NEVERC_HOOK_BEFORE_CODEGEN_PREEMIT,
                           stageTrackerMachinePass,
                           HOOK_UD(NEVERC_HOOK_BEFORE_CODEGEN_PREEMIT),
                           "example-stage-before-codegen-preemit");
  API->RegisterMachinePass(Registrar, NEVERC_HOOK_AFTER_CODEGEN_FINAL_MIR,
                           stageTrackerMachinePass,
                           HOOK_UD(NEVERC_HOOK_AFTER_CODEGEN_FINAL_MIR),
                           "example-stage-after-codegen-final-mir");

  /* Shellcode flow — IR hooks */
  API->RegisterModulePass(Registrar, NEVERC_HOOK_SC_BEFORE_PREP,
                          stageTrackerModulePass,
                          HOOK_UD(NEVERC_HOOK_SC_BEFORE_PREP),
                          "example-stage-sc-before-prep");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_SC_AFTER_PREP,
                          stageTrackerModulePass,
                          HOOK_UD(NEVERC_HOOK_SC_AFTER_PREP),
                          "example-stage-sc-after-prep");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_SC_BEFORE_INLINING,
                          stageTrackerModulePass,
                          HOOK_UD(NEVERC_HOOK_SC_BEFORE_INLINING),
                          "example-stage-sc-before-inlining");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_SC_AFTER_INLINING,
                          stageTrackerModulePass,
                          HOOK_UD(NEVERC_HOOK_SC_AFTER_INLINING),
                          "example-stage-sc-after-inlining");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_SC_AFTER_STACKIFY,
                          stageTrackerModulePass,
                          HOOK_UD(NEVERC_HOOK_SC_AFTER_STACKIFY),
                          "example-stage-sc-after-stackify");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_SC_AFTER_FINAL_IR,
                          stageTrackerModulePass,
                          HOOK_UD(NEVERC_HOOK_SC_AFTER_FINAL_IR),
                          "example-stage-sc-after-final-ir");

  /* Shellcode flow — MIR hooks */
  API->RegisterMachinePass(Registrar, NEVERC_HOOK_SC_BEFORE_PREEMIT,
                           stageTrackerMachinePass,
                           HOOK_UD(NEVERC_HOOK_SC_BEFORE_PREEMIT),
                           "example-stage-sc-before-preemit");
  API->RegisterMachinePass(Registrar, NEVERC_HOOK_SC_AFTER_PREEMIT,
                           stageTrackerMachinePass,
                           HOOK_UD(NEVERC_HOOK_SC_AFTER_PREEMIT),
                           "example-stage-sc-after-preemit");
  API->RegisterMachinePass(Registrar, NEVERC_HOOK_SC_AFTER_FINAL_MIR,
                           stageTrackerMachinePass,
                           HOOK_UD(NEVERC_HOOK_SC_AFTER_FINAL_MIR),
                           "example-stage-sc-after-final-mir");

  /* Shellcode flow — binary hooks */
  API->RegisterBinaryPass(Registrar, NEVERC_HOOK_SC_POST_EXTRACT,
                          stageTrackerBinaryPass,
                          HOOK_UD(NEVERC_HOOK_SC_POST_EXTRACT),
                          "example-stage-sc-post-extract");
  API->RegisterBinaryPass(Registrar, NEVERC_HOOK_SC_POST_FINALIZE,
                          stageTrackerBinaryPass,
                          HOOK_UD(NEVERC_HOOK_SC_POST_FINALIZE),
                          "example-stage-sc-post-finalize");

  /* LTO flow — IR hooks (module passes; independent of linker API). */
  API->RegisterModulePass(Registrar, NEVERC_HOOK_LTO_PRE_OPT,
                          ltoStagePass,
                          HOOK_UD(NEVERC_HOOK_LTO_PRE_OPT),
                          "example-lto-pre-opt");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_LTO_POST_OPT,
                          ltoStagePass,
                          HOOK_UD(NEVERC_HOOK_LTO_POST_OPT),
                          "example-lto-post-opt");

  /* Linker flow — object-level hooks */
  if (NEVERC_API_FN(API, RegisterLinkerPass)) {
    API->RegisterLinkerPass(Registrar, NEVERC_HOOK_LINK_PRE_LAYOUT,
                            linkerCensusPass,
                            HOOK_UD(NEVERC_HOOK_LINK_PRE_LAYOUT),
                            "example-linker-pre-layout");
    API->RegisterLinkerPass(Registrar, NEVERC_HOOK_LINK_POST_LAYOUT,
                            linkerCensusPass,
                            HOOK_UD(NEVERC_HOOK_LINK_POST_LAYOUT),
                            "example-linker-post-layout");
    API->RegisterLinkerPass(Registrar, NEVERC_HOOK_LINK_POST_EMIT,
                            linkerCensusPass,
                            HOOK_UD(NEVERC_HOOK_LINK_POST_EMIT),
                            "example-linker-post-emit");
  }

#undef HOOK_UD
}

NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void) {
  NevercPluginInfo Info;
  Info.APIVersion = NEVERC_PLUGIN_API_VERSION;
  Info.PluginName = "example-plugin";
  Info.PluginVersion = "1.0.0";
  Info.RegisterPasses = registerPasses;
  Info.Destroy = NULL;
  return Info;
}
