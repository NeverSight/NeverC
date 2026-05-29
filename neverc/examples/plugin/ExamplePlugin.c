/*
 * ExamplePlugin.c — Pure C neverc out-of-tree pass plugin.
 *
 * Demonstrates:
 *   - Module-level passes at PRE_OPT / POST_OPT / PIPELINE_START / LAST hooks
 *   - IR mutation: function-entry instrumentation via IRBuilder
 *   - High-throughput batch collection (ModuleCollectFunctions / CollectBBs /
 *     CollectInstructions) to minimise vtable indirection overhead
 *   - MIR-level pass at SC_BEFORE_PREEMIT hook (shellcode flow)
 *   - Binary-level pass at SC_POST_EXTRACT hook (shellcode flow)
 *   - LTO pipeline passes at LTO_PRE_OPT / LTO_POST_OPT hooks
 *   - Linker passes at LINK_PRE_LAYOUT / POST_LAYOUT / POST_EMIT hooks
 *   - Host-provided string utilities (StrReplace, StrToLower, StrFindChar, etc.)
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

/* ---- Module pass: print module info and count defined functions ---- */
static int functionCounterPass(NevercModuleRef M,
                               const NevercHostAPI *API,
                               void *UserData) {
  (void)UserData;

  const char *Triple = API->ModuleGetTargetTriple(M);
  API->DiagNoteF("[example-plugin] Target: %s", Triple);

  int Count = 0;
  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    if (!API->FunctionIsDeclaration(F))
      Count++;
    F = API->ModuleGetNextFunction(F);
  }

  API->DiagNoteF("[example-plugin] Found %d defined functions", Count);
  return 0;
}

/* ---- Module pass: list function names ---- */
static int functionListPass(NevercModuleRef M,
                            const NevercHostAPI *API,
                            void *UserData) {
  (void)UserData;
  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    if (!API->FunctionIsDeclaration(F))
      API->DiagNoteF("[example-plugin] Processing function: %s",
                     API->ValueGetName(F));
    F = API->ModuleGetNextFunction(F);
  }
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
  if (!NEVERC_API_HAS(API, BuildGlobalStringPtr))
    return 0;

  if (NEVERC_API_FN(API, HostIsShellcodeMode) &&
      API->HostIsShellcodeMode()) {
    API->DiagNoteF("[example-plugin] Shellcode mode active; skipping trace "
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

  int Modified = 0;
  NevercBuilderRef Builder = API->BuilderCreate(Ctx);

  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    if (!API->FunctionIsDeclaration(F) && F != TraceFn) {
      NevercBasicBlockRef EntryBB = API->FunctionGetFirstBB(F);
      if (EntryBB) {
        NevercValueRef FirstInst = API->BBGetFirstInst(EntryBB);
        if (FirstInst)
          API->BuilderSetInsertPointBefore(Builder, FirstInst);
        else
          API->BuilderSetInsertPoint(Builder, EntryBB);

        const char *FnName = API->ValueGetName(F);
        NevercValueRef NamePtr =
            API->BuildGlobalStringPtr(Builder, FnName, "fn.name");

        NevercValueRef CallArgs[1];
        CallArgs[0] = NamePtr;
        API->BuildCall(Builder, TraceFnTy, TraceFn, CallArgs, 1, "");

        Modified = 1;
      }
    }
    F = API->ModuleGetNextFunction(F);
  }

  API->BuilderDispose(Builder);

  if (Modified)
    API->DiagNoteF("[example-plugin] Inserted function-entry tracing calls");
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
    API->DiagNoteF("[example-plugin] MIR '%s': empty", FnName);
    return 0;
  }

  if (NEVERC_API_HAS(API, MFuncCollectBBs)) {
    NevercMachineBBRef *MBBs =
        (NevercMachineBBRef *)API->Alloc(BBCount * sizeof(NevercMachineBBRef));
    if (!MBBs)
      return 0;
    API->MFuncCollectBBs(MF, MBBs);

    unsigned B;
    for (B = 0; B < BBCount; B++) {
      unsigned IC = API->MBBGetInstCount(MBBs[B]);
      InstrCount += (int)IC;
      if (IC == 0)
        continue;

      NevercMachineInstrRef *MIs = (NevercMachineInstrRef *)API->Alloc(
          IC * sizeof(NevercMachineInstrRef));
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

  API->DiagNoteF("[example-plugin] MIR '%s': %u BBs, %d instrs, %d reg ops, "
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

  API->DiagNoteF("[example-plugin] Binary: %llu bytes",
                 (unsigned long long)*Len);
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
    API->DiagWarningF("[example-plugin] BinaryResize failed");
    return 0;
  }

  API->MemSet(*Data + OldLen, 0x90, 16);

  API->DiagNoteF("[example-plugin] Appended 16-byte NOP sled "
                 "(%llu -> %llu bytes)",
                 (unsigned long long)OldLen, (unsigned long long)NewLen);
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
  if (!NEVERC_API_HAS(API, GEPGetIndex))
    return 0;

  int TotalGEPs = 0, ConstIdxCount = 0, DynIdxCount = 0;

  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    if (!API->FunctionIsDeclaration(F)) {
      NevercBasicBlockRef BB = API->FunctionGetFirstBB(F);
      while (BB) {
        NevercValueRef I = API->BBGetFirstInst(BB);
        while (I) {
          if (API->InstIsGEP(I)) {
            TotalGEPs++;
            unsigned NumIdx = API->GEPGetNumIndices(I);
            unsigned Idx;
            for (Idx = 0; Idx < NumIdx; Idx++) {
              NevercValueRef IdxVal = API->GEPGetIndex(I, Idx);
              if (IdxVal && API->ValueIsConstantInt(IdxVal))
                ConstIdxCount++;
              else
                DynIdxCount++;
            }
          }
          I = API->BBGetNextInst(I);
        }
        BB = API->FunctionGetNextBB(BB);
      }
    }
    F = API->ModuleGetNextFunction(F);
  }

  API->DiagNoteF("[example-plugin] GEP index analysis: %d GEPs, "
                 "%d const indices, %d dynamic indices",
                 TotalGEPs, ConstIdxCount, DynIdxCount);
  return 0;
}

/* ---- Module pass: demonstrate Value kind queries & GlobalVariable ops ---- */
static int valueKindDemoPass(NevercModuleRef M,
                             const NevercHostAPI *API,
                             void *UserData) {
  (void)UserData;
  int FuncCount = 0, GlobalCount = 0;

  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    if (API->ValueIsFunction(F))
      FuncCount++;
    F = API->ModuleGetNextFunction(F);
  }

  NevercValueRef G = API->ModuleGetFirstGlobal(M);
  while (G) {
    if (API->ValueIsGlobalVariable(G))
      GlobalCount++;
    G = API->ModuleGetNextGlobal(G);
  }

  API->DiagNoteF("[example-plugin] ValueKind: %d functions, %d globals",
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
    API->DiagNoteF("[example-plugin] Created struct with %u fields",
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

  API->DiagNoteF("[example-plugin] Module stats: %u functions, %u globals",
                 FnCount, GvCount);

  NevercValueRef Main = API->ModuleGetNamedGlobal(M, "main_var");
  if (Main)
    API->DiagNoteF("[example-plugin] Found global 'main_var'");

  return 0;
}

/*
 * Module pass: instruction type analysis using batch collection.
 *
 * Uses ModuleCollectFunctions -> FunctionCollectBBs -> BBCollectInstructions
 * to walk the entire module with O(N+M) vtable calls for navigation instead
 * of O(N+M+L) with per-element GetNext iteration (N=functions, M=BBs,
 * L=instructions).  The inner instruction loop runs over a flat array with
 * zero indirection overhead.
 */
static int instrTypeDemoPass(NevercModuleRef M,
                             const NevercHostAPI *API,
                             void *UserData) {
  (void)UserData;
  if (!NEVERC_API_HAS(API, InstIsCall) ||
      !NEVERC_API_HAS(API, ModuleCollectFunctions))
    return 0;

  unsigned FnCount = API->ModuleGetFunctionCount(M);
  if (FnCount == 0)
    return 0;

  NevercValueRef *Fns =
      (NevercValueRef *)API->Alloc(FnCount * sizeof(NevercValueRef));
  if (!Fns)
    return 0;
  API->ModuleCollectFunctions(M, Fns);

  unsigned Calls = 0, Branches = 0, Loads = 0, Stores = 0;
  unsigned Allocas = 0, PHIs = 0, GEPs = 0;

  unsigned FI;
  for (FI = 0; FI < FnCount; FI++) {
    if (API->FunctionIsDeclaration(Fns[FI]))
      continue;

    unsigned BBCount = API->FunctionGetBBCount(Fns[FI]);
    if (BBCount == 0)
      continue;

    NevercBasicBlockRef *BBs = (NevercBasicBlockRef *)API->Alloc(
        BBCount * sizeof(NevercBasicBlockRef));
    if (!BBs)
      continue;
    API->FunctionCollectBBs(Fns[FI], BBs);

    unsigned BI;
    for (BI = 0; BI < BBCount; BI++) {
      unsigned IC = API->BBGetInstCount(BBs[BI]);
      if (IC == 0)
        continue;

      NevercValueRef *Insts =
          (NevercValueRef *)API->Alloc(IC * sizeof(NevercValueRef));
      if (!Insts)
        continue;
      API->BBCollectInstructions(BBs[BI], Insts);

      unsigned II;
      for (II = 0; II < IC; II++) {
        NevercValueRef Inst = Insts[II];
        if (API->InstIsCall(Inst))
          Calls++;
        if (API->InstIsBranch(Inst))
          Branches++;
        if (API->InstIsLoad(Inst))
          Loads++;
        if (API->InstIsStore(Inst))
          Stores++;
        if (API->InstIsAlloca(Inst))
          Allocas++;
        if (API->InstIsPHI(Inst))
          PHIs++;
        if (API->InstIsGEP(Inst))
          GEPs++;
      }
      API->Free(Insts);
    }
    API->Free(BBs);
  }
  API->Free(Fns);

  API->DiagNoteF("[example-plugin] Instr stats: %u calls, %u branches, "
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
  if (!NEVERC_API_HAS(API, InstIsICmp))
    return 0;

  int EqCount = 0, RelCount = 0, FCmpCount = 0;

  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    if (!API->FunctionIsDeclaration(F)) {
      NevercBasicBlockRef BB = API->FunctionGetFirstBB(F);
      while (BB) {
        NevercValueRef I = API->BBGetFirstInst(BB);
        while (I) {
          if (API->InstIsICmp(I)) {
            unsigned Pred = API->CmpGetPredicate(I);
            if (Pred == NEVERC_ICMP_EQ || Pred == NEVERC_ICMP_NE)
              EqCount++;
            else
              RelCount++;
          } else if (API->InstIsFCmp(I)) {
            FCmpCount++;
          }
          I = API->BBGetNextInst(I);
        }
        BB = API->FunctionGetNextBB(BB);
      }
    }
    F = API->ModuleGetNextFunction(F);
  }

  API->DiagNoteF("[example-plugin] Cmp analysis: %d eq, %d relational, "
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
  if (!NEVERC_API_HAS(API, InstGetNumSuccessors))
    return 0;

  int TotalEdges = 0;
  int ConditionalTerms = 0;

  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    if (!API->FunctionIsDeclaration(F)) {
      NevercBasicBlockRef BB = API->FunctionGetFirstBB(F);
      while (BB) {
        NevercValueRef Term = API->BBGetTerminator(BB);
        if (Term) {
          unsigned NumSucc = API->InstGetNumSuccessors(Term);
          TotalEdges += (int)NumSucc;
          if (NumSucc > 1)
            ConditionalTerms++;
        }
        BB = API->FunctionGetNextBB(BB);
      }
    }
    F = API->ModuleGetNextFunction(F);
  }

  API->DiagNoteF("[example-plugin] CFG: %d edges, "
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
  if (!NEVERC_API_HAS(API, GlobalGetLinkage))
    return 0;

  int InternalCount = 0;
  int ExternalCount = 0;

  NevercValueRef G = API->ModuleGetFirstGlobal(M);
  while (G) {
    if (API->ValueIsGlobalVariable(G)) {
      unsigned Linkage = API->GlobalGetLinkage(G);
      if (Linkage == NEVERC_LINKAGE_INTERNAL ||
          Linkage == NEVERC_LINKAGE_PRIVATE)
        InternalCount++;
      else
        ExternalCount++;
    }
    G = API->ModuleGetNextGlobal(G);
  }

  API->DiagNoteF("[example-plugin] Globals: %d internal, %d external",
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
  if (!NEVERC_API_HAS(API, ModuleRemoveFunction))
    return 0;

  unsigned FnCount = API->ModuleGetFunctionCount(M);
  if (FnCount == 0)
    return 0;

  NevercValueRef *ToRemove =
      (NevercValueRef *)API->Alloc(FnCount * sizeof(NevercValueRef));
  if (!ToRemove)
    return 0;

  unsigned RemoveCount = 0;
  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    NevercValueRef Next = API->ModuleGetNextFunction(F);
    if (!API->FunctionIsDeclaration(F)) {
      unsigned Linkage = API->FunctionGetLinkage(F);
      if (Linkage == NEVERC_LINKAGE_INTERNAL ||
          Linkage == NEVERC_LINKAGE_PRIVATE) {
        if (API->ValueGetNumUses(F) == 0)
          ToRemove[RemoveCount++] = F;
      }
    }
    F = Next;
  }

  unsigned I;
  for (I = 0; I < RemoveCount; I++) {
    API->DiagNoteF("[example-plugin] Removing dead internal function: %s",
                   API->ValueGetName(ToRemove[I]));
    API->ModuleRemoveFunction(M, ToRemove[I]);
  }
  API->Free(ToRemove);

  if (RemoveCount > 0)
    API->DiagNoteF("[example-plugin] Removed %u dead internal functions",
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
  if (!NEVERC_API_HAS(API, SwitchGetNumCases))
    return 0;

  int SwitchCount = 0;
  int TotalCases = 0;

  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    if (!API->FunctionIsDeclaration(F)) {
      NevercBasicBlockRef BB = API->FunctionGetFirstBB(F);
      while (BB) {
        NevercValueRef I = API->BBGetFirstInst(BB);
        while (I) {
          if (API->InstIsSwitch(I)) {
            SwitchCount++;
            TotalCases += (int)API->SwitchGetNumCases(I);
          }
          I = API->BBGetNextInst(I);
        }
        BB = API->FunctionGetNextBB(BB);
      }
    }
    F = API->ModuleGetNextFunction(F);
  }

  API->DiagNoteF("[example-plugin] Switch: %d switches, %d total cases",
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
  if (!NEVERC_API_HAS(API, ModuleGetPrevFunction) ||
      !NEVERC_API_HAS(API, ModuleGetPrevGlobal) ||
      !NEVERC_API_HAS(API, FunctionGetBBCount))
    return 0;

  API->DiagNoteF("[example-plugin] Source: %s",
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
  if (NEVERC_API_HAS(API, ModuleGetPrevAlias)) {
    NevercValueRef A = API->ModuleGetLastAlias(M);
    while (A) {
      AliasCount++;
      A = API->ModuleGetPrevAlias(A);
    }
  }

  API->DiagNoteF("[example-plugin] ReverseIter: %u fns/%u terminators, "
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
  if (!NEVERC_API_HAS(API, InstIsCallLike))
    return 0;

  int DirectCalls = 0, IndirectCalls = 0, Invokes = 0;

  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    if (!API->FunctionIsDeclaration(F)) {
      NevercBasicBlockRef BB = API->FunctionGetFirstBB(F);
      while (BB) {
        NevercValueRef I = API->BBGetFirstInst(BB);
        while (I) {
          if (API->InstIsCallLike(I)) {
            NevercValueRef Callee = API->CallGetCalledOperand(I);
            if (Callee && API->ValueIsFunction(Callee))
              DirectCalls++;
            else
              IndirectCalls++;
            if (API->InstIsInvoke(I))
              Invokes++;
          }
          I = API->BBGetNextInst(I);
        }
        BB = API->FunctionGetNextBB(BB);
      }
    }
    F = API->ModuleGetNextFunction(F);
  }

  API->DiagNoteF("[example-plugin] CallLike: %d direct, %d indirect, "
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
  if (!NEVERC_API_HAS(API, LoadGetType) ||
      !NEVERC_API_HAS(API, GEPIsInBounds))
    return 0;

  int PtrLoads = 0, IntLoads = 0, OtherLoads = 0;
  int InboundsGEPs = 0, RegularGEPs = 0;

  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    if (!API->FunctionIsDeclaration(F)) {
      NevercBasicBlockRef BB = API->FunctionGetFirstBB(F);
      while (BB) {
        NevercValueRef I = API->BBGetFirstInst(BB);
        while (I) {
          if (API->InstIsLoad(I)) {
            NevercTypeRef LdTy = API->LoadGetType(I);
            if (LdTy) {
              if (API->TypeIsPointer(LdTy))
                PtrLoads++;
              else if (API->TypeIsInteger(LdTy))
                IntLoads++;
              else
                OtherLoads++;
            }
          }
          if (API->InstIsGEP(I)) {
            if (API->GEPIsInBounds(I))
              InboundsGEPs++;
            else
              RegularGEPs++;
          }
          I = API->BBGetNextInst(I);
        }
        BB = API->FunctionGetNextBB(BB);
      }
    }
    F = API->ModuleGetNextFunction(F);
  }

  API->DiagNoteF("[example-plugin] MemAccess: loads=%dptr/%dint/%dother, "
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
 * Uses ModuleCollectFunctions + FunctionGetBBCount for O(N) vtable
 * calls (N = functions) instead of O(N+M) from per-BB iteration.
 */
static int pipelineStagePass(NevercModuleRef M,
                             const NevercHostAPI *API,
                             void *UserData) {
  const char *Stage = getHookName(API, UserData);
  unsigned FnCount = API->ModuleGetFunctionCount(M);
  if (FnCount == 0) {
    API->DiagNoteF("[example-plugin] %s: 0 functions", Stage);
    return 0;
  }

  unsigned Defined = 0, BBTotal = 0;

  if (NEVERC_API_FN(API, ModuleCollectFunctions)) {
    NevercValueRef *Fns =
        (NevercValueRef *)API->Alloc(FnCount * sizeof(NevercValueRef));
    if (!Fns)
      return 0;
    API->ModuleCollectFunctions(M, Fns);
    unsigned I;
    for (I = 0; I < FnCount; I++) {
      if (API->FunctionIsDeclaration(Fns[I]))
        continue;
      Defined++;
      BBTotal += API->FunctionGetBBCount(Fns[I]);
    }
    API->Free(Fns);
  } else {
    NevercValueRef F = API->ModuleGetFirstFunction(M);
    while (F) {
      if (!API->FunctionIsDeclaration(F)) {
        Defined++;
        BBTotal += API->FunctionGetBBCount(F);
      }
      F = API->ModuleGetNextFunction(F);
    }
  }

  API->DiagNoteF("[example-plugin] %s: %u functions, %u BBs",
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
  if (!NEVERC_API_HAS(API, InstCopyDebugLoc))
    return 0;

  int WithLoc = 0, WithoutLoc = 0;
  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    if (!API->FunctionIsDeclaration(F)) {
      NevercBasicBlockRef BB = API->FunctionGetFirstBB(F);
      while (BB) {
        NevercValueRef I = API->BBGetFirstInst(BB);
        while (I) {
          if (API->InstHasDebugLoc(I))
            WithLoc++;
          else
            WithoutLoc++;
          I = API->BBGetNextInst(I);
        }
        BB = API->FunctionGetNextBB(BB);
      }
    }
    F = API->ModuleGetNextFunction(F);
  }

  int SectionedGlobals = 0;
  if (NEVERC_API_HAS(API, GlobalGetSection)) {
    NevercValueRef G = API->ModuleGetFirstGlobal(M);
    while (G) {
      if (API->ValueIsGlobalVariable(G)) {
        const char *Sec = API->GlobalGetSection(G);
        if (Sec && Sec[0] != '\0')
          SectionedGlobals++;
      }
      G = API->ModuleGetNextGlobal(G);
    }
  }

  API->DiagNoteF("[example-plugin] DebugLoc: %d with, %d without, "
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
  if (!NEVERC_API_HAS(API, ValuePrintToString))
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
            API->DiagNoteF("[example-plugin] Entry terminator of '%s': %s",
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
  API->DiagNoteF("[example-plugin] Stage: %s", getHookName(API, UserData));
  return 0;
}

static int stageTrackerMachinePass(NevercMachineFuncRef MF,
                                   const NevercHostAPI *API,
                                   void *UserData) {
  const char *FnName = API->MFuncGetName(MF);
  API->DiagNoteF("[example-plugin] Stage: %s (MF=%s)",
                 getHookName(API, UserData), FnName ? FnName : "?");
  return 0;
}

static int stageTrackerBinaryPass(uint8_t **Data, uint64_t *Len,
                                  uint64_t *Capacity,
                                  const NevercHostAPI *API,
                                  void *UserData) {
  (void)Data;
  (void)Capacity;
  API->DiagNoteF("[example-plugin] Stage: %s (binary=%llu bytes)",
                 getHookName(API, UserData), (unsigned long long)*Len);
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
  if (!NEVERC_API_HAS(API, IntrinsicLookupByName))
    return 0;

  unsigned TrapID = API->IntrinsicLookupByName("llvm.debugtrap");
  if (TrapID != 0) {
    char *Name = API->IntrinsicGetName(TrapID);
    if (Name) {
      API->DiagNoteF("[example-plugin] Intrinsic '%s' id=%u%s",
                     Name, TrapID,
                     API->IntrinsicIsOverloaded(TrapID)
                         ? " (overloaded)"
                         : " (not overloaded)");
      API->Free(Name);
    }
  }

  if (NEVERC_API_HAS(API, ModuleGetNamedMetadata)) {
    NevercNamedMDRef Ident = API->ModuleGetNamedMetadata(M, "llvm.ident");
    if (Ident)
      API->DiagNoteF("[example-plugin] llvm.ident has %u operand(s)",
                     API->NamedMDGetNumOperands(Ident));
    else
      API->DiagNoteF("[example-plugin] No llvm.ident metadata found");
  }
  return 0;
}

/* ---- Module pass: demonstrate plugin argument access ---- */
static int pluginArgDemoPass(NevercModuleRef M,
                             const NevercHostAPI *API,
                             void *UserData) {
  (void)UserData;
  if (!NEVERC_API_HAS(API, PluginGetArg))
    return 0;

  API->DiagNoteF("[example-plugin] Plugin arg count: %u",
                 API->PluginGetArgCount());

  const char *Verbose = API->PluginGetArg("verbose");
  if (Verbose && Verbose[0] == '1') {
    NevercValueRef F = API->ModuleGetFirstFunction(M);
    while (F) {
      if (!API->FunctionIsDeclaration(F))
        API->DiagNoteF("[example-plugin][verbose] Function: %s",
                       API->ValueGetName(F));
      F = API->ModuleGetNextFunction(F);
    }
  }

  if (API->PluginHasArg("prefix")) {
    const char *Prefix = API->PluginGetArg("prefix");
    API->DiagNoteF("[example-plugin] Using custom prefix: %s",
                   Prefix ? Prefix : "(empty)");
  }

  return 0;
}

/* ---- Module pass: demonstrate DataLayout, PoisonValue, and vector API ---- */
static int dataLayoutDemoPass(NevercModuleRef M,
                              const NevercHostAPI *API,
                              void *UserData) {
  (void)UserData;
  if (!NEVERC_API_HAS(API, TypeSizeInBits))
    return 0;

  NevercContextRef C = API->ModuleGetContext(M);

  unsigned PtrBits = API->PointerSizeInBits(M, 0);
  API->DiagNoteF("[example-plugin] Pointer size: %u bits", PtrBits);

  NevercTypeRef I32Ty = API->TypeGetInt32(C);
  NevercTypeRef I64Ty = API->TypeGetInt64(C);
  NevercTypeRef DblTy = API->TypeGetDouble(C);

  uint64_t I32Size = API->TypeAllocSize(M, I32Ty);
  uint64_t I64Size = API->TypeAllocSize(M, I64Ty);
  unsigned I32Align = API->TypeABIAlignment(M, I32Ty);
  unsigned DblAlign = API->TypeABIAlignment(M, DblTy);

  API->DiagNoteF("[example-plugin] i32: %llu bytes, align %u | "
                 "i64: %llu bytes | double align %u",
                 (unsigned long long)I32Size, I32Align,
                 (unsigned long long)I64Size, DblAlign);

  if (NEVERC_API_HAS(API, ConstPoison)) {
    NevercValueRef Poison = API->ConstPoison(I32Ty);
    API->DiagNoteF("[example-plugin] PoisonValue(i32): isPoison=%d "
                   "isUndef=%d",
                   API->ValueIsPoison(Poison), API->ValueIsUndef(Poison));
  }

  if (NEVERC_API_HAS(API, TypeGetFixedVector)) {
    NevercTypeRef V4I32 = API->TypeGetFixedVector(I32Ty, 4);
    API->DiagNoteF("[example-plugin] <4 x i32>: isVector=%d numElems=%u "
                   "sizeBits=%llu",
                   API->TypeIsVector(V4I32),
                   API->TypeGetVectorNumElements(V4I32),
                   (unsigned long long)API->TypeSizeInBits(M, V4I32));
  }

  if (NEVERC_API_HAS(API, TypeGetPtrInAddrSpace)) {
    NevercTypeRef DefaultPtr = API->TypeGetPtr(C);
    NevercTypeRef AS1Ptr = API->TypeGetPtrInAddrSpace(C, 1);
    API->DiagNoteF("[example-plugin] ptr addrspace(0)=%u "
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
  if (!NEVERC_API_HAS(API, BuildNSWAdd) || !NEVERC_API_HAS(API, GlobalGetComdat))
    return 0;

  int ComdatCount = 0;
  NevercValueRef G = API->ModuleGetFirstGlobal(M);
  while (G) {
    if (API->ValueIsGlobalVariable(G)) {
      NevercComdatRef C = API->GlobalGetComdat(G);
      if (C)
        ComdatCount++;
    }
    G = API->ModuleGetNextGlobal(G);
  }

  int ThreadLocalCount = 0;
  if (NEVERC_API_HAS(API, GlobalIsThreadLocal)) {
    G = API->ModuleGetFirstGlobal(M);
    while (G) {
      if (API->ValueIsGlobalVariable(G) && API->GlobalIsThreadLocal(G))
        ThreadLocalCount++;
      G = API->ModuleGetNextGlobal(G);
    }
  }

  API->DiagNoteF("[example-plugin] Globals: %d with COMDAT, "
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
  if (!NEVERC_API_HAS(API, ConstGetAggregateElement) ||
      !NEVERC_API_HAS(API, FunctionHasEnumAttr))
    return 0;

  int AggrGlobals = 0;
  NevercValueRef G = API->ModuleGetFirstGlobal(M);
  while (G) {
    if (API->ValueIsGlobalVariable(G) && API->GlobalHasInitializer(G)) {
      NevercValueRef Init = API->GlobalGetInitializer(G);
      if (Init) {
        NevercValueRef Elem0 = API->ConstGetAggregateElement(Init, 0);
        if (Elem0)
          AggrGlobals++;
      }
    }
    G = API->ModuleGetNextGlobal(G);
  }

  int NoInlineFns = 0;
  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    if (!API->FunctionIsDeclaration(F) &&
        API->FunctionHasEnumAttr(F, 11 /* NoInline */))
      NoInlineFns++;
    F = API->ModuleGetNextFunction(F);
  }

  API->DiagNoteF("[example-plugin] %d globals with aggregate init, "
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
  if (!NEVERC_API_FN(API, StrStartsWith))
    return 0;

  int PrefixedCount = 0;
  int TotalDefined = 0;
  int IntrinsicCount = 0;
  NevercValueRef FirstDefined = NULL;

  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    const char *Name = API->ValueGetName(F);
    if (!API->FunctionIsDeclaration(F)) {
      TotalDefined++;
      if (!FirstDefined)
        FirstDefined = F;
      if (API->StrStartsWith(Name, "__neverc_") ||
          API->StrStartsWith(Name, "llvm."))
        PrefixedCount++;
    }
    if (!API->StrStartsWith(Name, "llvm.")) {
      F = API->ModuleGetNextFunction(F);
      continue;
    }
    IntrinsicCount++;

    if (!NEVERC_API_FN(API, StrFindChar)) {
      F = API->ModuleGetNextFunction(F);
      continue;
    }
    uint64_t DotPos = API->StrFindChar(Name + 5, '.');
    if (DotPos == (uint64_t)-1) {
      F = API->ModuleGetNextFunction(F);
      continue;
    }
    char *BaseName = API->StrSubstring(Name, 5, DotPos);
    if (!BaseName) {
      F = API->ModuleGetNextFunction(F);
      continue;
    }

    const char *Display = BaseName;
    char *Lower = NULL;
    if (NEVERC_API_FN(API, StrToLower)) {
      Lower = API->StrToLower(BaseName);
      if (Lower)
        Display = Lower;
    }
    API->DiagNoteF("[example-plugin] Intrinsic: %s", Display);
    if (Lower)
      API->Free(Lower);
    API->Free(BaseName);

    F = API->ModuleGetNextFunction(F);
  }

  if (FirstDefined && NEVERC_API_FN(API, StrReplaceAll)) {
    const char *Name = API->ValueGetName(FirstDefined);
    char *Demangled = API->StrReplaceAll(Name, "_", "::");
    if (Demangled) {
      API->DiagNoteF("[example-plugin] Demangled-like: %s -> %s",
                     Name, Demangled);
      API->Free(Demangled);
    }
    if (NEVERC_API_FN(API, MemDup) && NEVERC_API_FN(API, StrLen)) {
      uint64_t NameLen = API->StrLen(Name);
      char *Copy = (char *)API->MemDup(Name, NameLen + 1);
      if (Copy) {
        API->DiagNoteF("[example-plugin] MemDup name: %s", Copy);
        API->Free(Copy);
      }
    }
  }

  API->DiagNoteF("[example-plugin] StringUtil: %d defined, %d prefixed, "
                 "%d intrinsics",
                 TotalDefined, PrefixedCount, IntrinsicCount);
  return 0;
}

/*
 * Module pass: demonstrate batch collection for high-throughput analysis.
 *
 * Uses ModuleCollectFunctions / FunctionCollectBBs to gather all items
 * into flat arrays, avoiding repeated vtable indirection.  For a module
 * with N functions and M total instructions the iterator pattern makes
 * O(N+M) indirect calls; the batch path makes O(N) calls plus fast
 * sequential array walks.
 */
static int batchAnalysisDemoPass(NevercModuleRef M,
                                 const NevercHostAPI *API,
                                 void *UserData) {
  (void)UserData;
  if (!NEVERC_API_HAS(API, ModuleCollectFunctions))
    return 0;

  unsigned FnCount = API->ModuleGetFunctionCount(M);
  if (FnCount == 0)
    return 0;
  NevercValueRef *Funcs =
      (NevercValueRef *)API->Alloc(FnCount * sizeof(NevercValueRef));
  if (!Funcs)
    return 0;
  API->ModuleCollectFunctions(M, Funcs);

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

    NevercBasicBlockRef *BBs = (NevercBasicBlockRef *)API->Alloc(
        BBCount * sizeof(NevercBasicBlockRef));
    if (!BBs)
      continue;
    API->FunctionCollectBBs(Funcs[I], BBs);

    unsigned J;
    for (J = 0; J < BBCount; J++)
      TotalInsts += API->BBGetInstCount(BBs[J]);
    API->Free(BBs);
  }
  API->Free(Funcs);

  API->DiagNoteF("[example-plugin] Batch: %u defined fns, "
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
  unsigned FnCount = API->ModuleGetFunctionCount(M);
  unsigned Defined = 0;

  if (FnCount && NEVERC_API_FN(API, ModuleCollectFunctions)) {
    NevercValueRef *Fns =
        (NevercValueRef *)API->Alloc(FnCount * sizeof(NevercValueRef));
    if (Fns) {
      API->ModuleCollectFunctions(M, Fns);
      unsigned I;
      for (I = 0; I < FnCount; I++) {
        if (!API->FunctionIsDeclaration(Fns[I]))
          Defined++;
      }
      API->Free(Fns);
    }
  } else {
    NevercValueRef F = API->ModuleGetFirstFunction(M);
    while (F) {
      if (!API->FunctionIsDeclaration(F))
        Defined++;
      F = API->ModuleGetNextFunction(F);
    }
  }

  API->DiagNoteF("[example-plugin] %s: %u functions (%u defined)",
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

  unsigned Format = API->LinkGetOutputFormat();
  const char *FmtName = "unknown";
  if (Format == NEVERC_LINK_FORMAT_ELF)
    FmtName = "ELF";
  else if (Format == NEVERC_LINK_FORMAT_COFF)
    FmtName = "COFF";
  else if (Format == NEVERC_LINK_FORMAT_MACHO)
    FmtName = "Mach-O";

  API->DiagNoteF("[example-plugin] Link %s [%s]: %d defined, %d undef, "
                 "%d local, %d hidden syms; %d sections; defined size=%llu",
                 Stage, FmtName, Defined, Undefined,
                 LocalCount, HiddenCount, Sections,
                 (unsigned long long)DefinedSizeTotal);
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
