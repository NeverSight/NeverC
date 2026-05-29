/*
 * ExamplePlugin.c -- Pure C neverc out-of-tree pass plugin.
 *
 * Demonstrates:
 *   - Module-level IR reading and mutation
 *   - ModuleCollectDefinedFunctions -- host-side filter+collect in one call
 *   - DynArray -- opaque growable array for collect-filter-sort workflows
 *   - StrMap -- opaque string-keyed hash table for histogram / counting
 *   - IntMap -- opaque integer-keyed hash table for opcode counting
 *   - InstOpcodeToName -- numeric opcode -> human-readable name lookup
 *   - StrBuilder -- opaque incremental string construction
 *   - MIR-level analysis with batch collection
 *   - Binary-level inspection and patching (BinaryResize + NOP sled)
 *   - LTO pipeline hooks (LTO_PRE_OPT / LTO_POST_OPT)
 *   - Linker hooks (LINK_PRE_LAYOUT / POST_LAYOUT / POST_EMIT)
 *   - Host-provided string/memory utilities (StrStartsWith, StrFindChar, etc.)
 *   - Sort (host-routed qsort -- no cross-DLL CRT calls)
 *   - Formatted diagnostics via DiagNoteF -- no manual StrFormat/Free dance
 *   - Plugin command-line arguments via PluginGetArg / PluginHasArg
 *   - HookPointGetName for runtime hook-name resolution
 *   - NEVERC_ALLOC_ARRAY / NEVERC_COLLECT_* convenience macros
 *   - NEVERC_HOOK_UD / NEVERC_HOOK_NAME / NEVERC_STRMAP_NEW / NEVERC_INTMAP_NEW
 *   - Zero LLVM C++ or CRT dependencies -- everything goes through the vtable
 *
 * Build:
 *   cc -shared -o ExamplePlugin.dll ExamplePlugin.c -I<neverc>/include
 *
 * Usage:
 *   neverc -fplugin-pass=./ExamplePlugin.dll input.c -o output.obj
 *   neverc -fplugin-pass=./ExamplePlugin.dll \
 *          -fplugin-pass-arg=verbose=1 input.c -o output.obj
 */

#include "neverc/Plugin/NevercPluginAPI.h"

#define PLUGIN_TAG "[example] "

/* ======================================================================== */
/*  IR Passes                                                               */
/* ======================================================================== */

/* Count defined functions and report module info. */
static int functionCounterPass(NevercModuleRef M, const NevercHostAPI *API,
                               void *UserData) {
  (void)UserData;

  const char *Triple = API->ModuleGetTargetTriple(M);
  unsigned Defined = 0;
  NevercValueRef *Fns = NEVERC_COLLECT_DEFINED_FUNCTIONS(API, M, &Defined);
  if (Fns)
    API->Free(Fns);

  API->DiagNoteF(PLUGIN_TAG "Target: %s -- %u defined functions",
                 Triple, Defined);
  return 0;
}

/*
 * Insert a call to an external tracing function at every function entry:
 *
 *   declare void @__neverc_plugin_trace(ptr %fn_name)
 *
 * Shows: type creation, ModuleAddFunction, BuilderCreate, BuildGlobalStringPtr,
 *        BuildCall, HostIsShellcodeMode guard.
 *
 * Skips when shellcode mode is active (external symbols are forbidden).
 */
static int functionEntryInstrPass(NevercModuleRef M, const NevercHostAPI *API,
                                  void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, BuildGlobalStringPtr))
    return 0;

  if (NEVERC_API_FN(API, HostIsShellcodeMode) && API->HostIsShellcodeMode()) {
    API->DiagNoteF(PLUGIN_TAG "Shellcode mode -- skipping trace "
                   "instrumentation");
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

  unsigned FnCount = 0;
  NevercValueRef *Fns = NEVERC_COLLECT_DEFINED_FUNCTIONS(API, M, &FnCount);
  if (!Fns)
    return 0;

  int Modified = 0;
  NevercBuilderRef Builder = API->BuilderCreate(Ctx);

  for (unsigned I = 0; I < FnCount; I++) {
    if (Fns[I] == TraceFn)
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
 * Remove dead internal functions (analyze -> modify workflow).
 *
 * Shows: FunctionGetLinkage, ValueGetNumUses, ModuleRemoveFunction,
 *        DynArray for safe two-phase collect-then-delete pattern.
 */
static int deadFunctionRemovalPass(NevercModuleRef M, const NevercHostAPI *API,
                                   void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, ModuleRemoveFunction) ||
      !NEVERC_API_FN(API, DynArrayCreate))
    return 0;

  unsigned FnCount = 0;
  NevercValueRef *Fns = NEVERC_COLLECT_DEFINED_FUNCTIONS(API, M, &FnCount);
  if (!Fns)
    return 0;

  NevercDynArrayRef Dead = API->DynArrayCreate(sizeof(NevercValueRef));
  if (!Dead) {
    API->Free(Fns);
    return 0;
  }

  for (unsigned I = 0; I < FnCount; I++) {
    unsigned Linkage = API->FunctionGetLinkage(Fns[I]);
    if ((Linkage == NEVERC_LINKAGE_INTERNAL ||
         Linkage == NEVERC_LINKAGE_PRIVATE) &&
        API->ValueGetNumUses(Fns[I]) == 0)
      API->DynArrayPush(Dead, &Fns[I]);
  }
  API->Free(Fns);

  unsigned RemoveCount = API->DynArrayCount(Dead);
  NevercValueRef *ToRemove = (NevercValueRef *)API->DynArrayData(Dead);
  for (unsigned I = 0; I < RemoveCount; I++) {
    API->DiagNoteF(PLUGIN_TAG "Removing dead: %s",
                   API->ValueGetName(ToRemove[I]));
    API->ModuleRemoveFunction(M, ToRemove[I]);
  }
  API->DynArrayDestroy(Dead);

  if (RemoveCount > 0)
    API->DiagNoteF(PLUGIN_TAG "Removed %u dead internal functions",
                   RemoveCount);
  return RemoveCount > 0;
}

/*
 * Pipeline stage tracker -- reports function/BB counts at a specific stage.
 *
 * Registered at PIPELINE_START and PIPELINE_LAST so users can see the
 * optimizer's impact.  UserData carries NevercHookPoint as (void*).
 */
static int pipelineStagePass(NevercModuleRef M, const NevercHostAPI *API,
                             void *UserData) {
  const char *Stage = NEVERC_HOOK_NAME(API, UserData);
  unsigned Defined = 0;
  NevercValueRef *Fns = NEVERC_COLLECT_DEFINED_FUNCTIONS(API, M, &Defined);
  if (!Fns) {
    API->DiagNoteF(PLUGIN_TAG "%s: 0 functions", Stage);
    return 0;
  }

  unsigned BBTotal = 0;
  for (unsigned I = 0; I < Defined; I++)
    BBTotal += API->FunctionGetBBCount(Fns[I]);
  API->Free(Fns);

  API->DiagNoteF(PLUGIN_TAG "%s: %u functions, %u BBs", Stage, Defined,
                 BBTotal);
  return 0;
}

/*
 * Intrinsic category histogram using StrMap + StrBuilder + StrMapForEach.
 *
 * Groups all LLVM intrinsics by category (the segment between "llvm."
 * and the next dot), counts occurrences in a StrMap, then iterates the
 * map via StrMapForEach to build a summary with StrBuilder.
 *
 * Shows: StrMap (fast string-keyed counting), StrMapIncrement (single
 *        hash lookup for counting), StrBuilder (incremental string
 *        construction), StrMapForEach (zero-alloc callback iteration),
 *        StrStartsWith, StrFindChar, StrCopyBuf.
 */

struct HistVisitCtx {
  const NevercHostAPI *API;
  NevercStrBuilderRef SB;
};

static int histVisitor(const char *Key, uint64_t Value, void *Ctx) {
  struct HistVisitCtx *H = (struct HistVisitCtx *)Ctx;
  H->API->StrBuilderAppendF(H->SB, " %s=%" PRIu64, Key, Value);
  return 0;
}

static int intrinsicHistogramPass(NevercModuleRef M, const NevercHostAPI *API,
                                  void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, StrMapCreate) ||
      !NEVERC_API_FN(API, StrBuilderCreate) ||
      !NEVERC_API_FN(API, StrStartsWith))
    return 0;

  unsigned AllCount = 0;
  NevercValueRef *AllFns = NEVERC_COLLECT_FUNCTIONS(API, M, &AllCount);
  if (!AllFns)
    return 0;

  NevercStrMapRef Hist = NEVERC_STRMAP_NEW(API, 64);
  if (!Hist) {
    API->Free(AllFns);
    return 0;
  }

  int HasIncrement = NEVERC_API_FN(API, StrMapIncrement) != 0;
  char CatBuf[128];
  for (unsigned I = 0; I < AllCount; I++) {
    const char *Name = API->ValueGetName(AllFns[I]);
    if (!API->StrStartsWith(Name, "llvm."))
      continue;
    uint64_t DotPos = API->StrFindChar(Name + 5, '.');
    if (DotPos == (uint64_t)-1 || DotPos >= sizeof(CatBuf))
      continue;
    API->StrCopyBuf(CatBuf, DotPos + 1, Name + 5);

    if (HasIncrement) {
      API->StrMapIncrement(Hist, CatBuf, 1);
    } else {
      uint64_t Prev = 0;
      API->StrMapGet(Hist, CatBuf, &Prev);
      API->StrMapPut(Hist, CatBuf, Prev + 1);
    }
  }
  API->Free(AllFns);

  unsigned NumCategories = API->StrMapCount(Hist);
  if (NumCategories == 0) {
    API->StrMapDestroy(Hist);
    API->DiagNoteF(PLUGIN_TAG "No LLVM intrinsics found");
    return 0;
  }

  NevercStrBuilderRef SB = API->StrBuilderCreate();
  if (!SB) {
    API->StrMapDestroy(Hist);
    return 0;
  }
  API->StrBuilderAppendF(SB, PLUGIN_TAG "Intrinsics (%u categories):",
                         NumCategories);

  if (NEVERC_API_FN(API, StrMapForEach)) {
    struct HistVisitCtx Ctx;
    Ctx.API = API;
    Ctx.SB = SB;
    API->StrMapForEach(Hist, histVisitor, &Ctx);
  }

  API->StrMapDestroy(Hist);
  char *Summary = API->StrBuilderFinish(SB);
  API->StrBuilderDestroy(SB);
  if (Summary) {
    API->DiagNote(Summary);
    API->Free(Summary);
  }
  return 0;
}

/*
 * Opcode histogram using IntMap -- counts IR instruction opcodes.
 *
 * Prefers the batch path (ModuleCollectAllInstructions) for cache-friendly
 * linear scan; falls back to per-element iterator when the batch API is
 * unavailable.  IntMapIncrement does single-probe amortized O(1) counting.
 * IntMapCreateSized(128) pre-allocates for the ~70 LLVM IR opcodes.
 *
 * Shows: IntMap (integer-keyed hash table), IntMapIncrement (single-
 *        probe counting), IntMapForEach (zero-alloc callback iteration),
 *        InstOpcodeToName (human-readable opcode), StrBuilder,
 *        ModuleCollectAllInstructions (batch collection).
 */

struct OpcodeVisitCtx {
  const NevercHostAPI *API;
  NevercStrBuilderRef SB;
  int HasOpcodeToName;
};

static int opcodeVisitor(uint64_t Key, uint64_t Value, void *Ctx) {
  struct OpcodeVisitCtx *O = (struct OpcodeVisitCtx *)Ctx;
  if (O->HasOpcodeToName) {
    const char *Name = O->API->InstOpcodeToName((unsigned)Key);
    O->API->StrBuilderAppendF(O->SB, " %s=%" PRIu64, Name, Value);
  } else {
    O->API->StrBuilderAppendF(O->SB, " op%u=%" PRIu64, (unsigned)Key, Value);
  }
  return 0;
}

static int opcodeHistogramPass(NevercModuleRef M, const NevercHostAPI *API,
                               void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, IntMapCreate) ||
      !NEVERC_API_FN(API, StrBuilderCreate))
    return 0;

  NevercIntMapRef Hist = NEVERC_INTMAP_NEW(API, 128);
  if (!Hist)
    return 0;

  unsigned InstCount = 0;
  NevercValueRef *Insts = NEVERC_COLLECT_INSTRUCTIONS(API, M, &InstCount);
  if (Insts) {
    for (unsigned I = 0; I < InstCount; I++)
      API->IntMapIncrement(Hist, API->InstGetOpcode(Insts[I]), 1);
    API->Free(Insts);
  } else {
    NevercValueRef F = API->ModuleGetFirstFunction(M);
    for (; F; F = API->ModuleGetNextFunction(F)) {
      if (API->FunctionIsDeclaration(F))
        continue;
      NevercBasicBlockRef BB = API->FunctionGetFirstBB(F);
      for (; BB; BB = API->FunctionGetNextBB(BB)) {
        NevercValueRef I = API->BBGetFirstInst(BB);
        for (; I; I = API->BBGetNextInst(I)) {
          API->IntMapIncrement(Hist, API->InstGetOpcode(I), 1);
          ++InstCount;
        }
      }
    }
  }

  unsigned UniqueOps = API->IntMapCount(Hist);
  if (UniqueOps == 0) {
    API->IntMapDestroy(Hist);
    return 0;
  }

  NevercStrBuilderRef SB = API->StrBuilderCreate();
  if (!SB) {
    API->IntMapDestroy(Hist);
    return 0;
  }
  API->StrBuilderAppendF(SB, PLUGIN_TAG "Opcodes (%u unique, %u total):",
                         UniqueOps, InstCount);

  struct OpcodeVisitCtx Ctx;
  Ctx.API = API;
  Ctx.SB = SB;
  Ctx.HasOpcodeToName = NEVERC_API_FN(API, InstOpcodeToName) != 0;
  API->IntMapForEach(Hist, opcodeVisitor, &Ctx);

  API->IntMapDestroy(Hist);
  char *Summary = API->StrBuilderFinish(SB);
  API->StrBuilderDestroy(SB);
  if (Summary) {
    API->DiagNote(Summary);
    API->Free(Summary);
  }
  return 0;
}

/*
 * Sort API demo: collect defined functions, sort by BB count descending,
 * report top-N "hottest" functions (most basic blocks).
 *
 * Shows: DynArray (push entries, sort in-place, read via Get),
 *        branchless comparator for qsort.
 */

struct FuncSortEntry {
  NevercValueRef Fn;
  unsigned BBCount;
};

static int cmpFuncByBBCountDesc(const void *A, const void *B) {
  unsigned CA = ((const struct FuncSortEntry *)A)->BBCount;
  unsigned CB = ((const struct FuncSortEntry *)B)->BBCount;
  return (CA < CB) - (CA > CB);
}

static int sortedFuncAnalysisPass(NevercModuleRef M, const NevercHostAPI *API,
                                  void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, DynArrayCreate))
    return 0;

  unsigned FnCount = 0;
  NevercValueRef *Fns = NEVERC_COLLECT_DEFINED_FUNCTIONS(API, M, &FnCount);
  if (!Fns || FnCount == 0)
    return 0;

  NevercDynArrayRef Entries =
      API->DynArrayCreate(sizeof(struct FuncSortEntry));
  if (!Entries) {
    API->Free(Fns);
    return 0;
  }
  API->DynArrayReserve(Entries, FnCount);

  for (unsigned I = 0; I < FnCount; I++) {
    struct FuncSortEntry E;
    E.Fn = Fns[I];
    E.BBCount = API->FunctionGetBBCount(Fns[I]);
    API->DynArrayPush(Entries, &E);
  }
  API->Free(Fns);

  API->DynArraySort(Entries, cmpFuncByBBCountDesc);

  unsigned Count = API->DynArrayCount(Entries);
  unsigned Top = Count < 5 ? Count : 5;
  for (unsigned I = 0; I < Top; I++) {
    const struct FuncSortEntry *E =
        (const struct FuncSortEntry *)API->DynArrayGet(Entries, I);
    API->DiagNoteF(PLUGIN_TAG "Top func: #%u %s (%u BBs)", I + 1,
                   API->ValueGetName(E->Fn), E->BBCount);
  }

  API->DynArrayDestroy(Entries);
  return 0;
}

/* Plugin command-line argument demo (-fplugin-pass-arg=key=value). */
static int pluginArgDemoPass(NevercModuleRef M, const NevercHostAPI *API,
                             void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, PluginGetArg))
    return 0;

  API->DiagNoteF(PLUGIN_TAG "Plugin args: %u", API->PluginGetArgCount());

  const char *Verbose = API->PluginGetArg("verbose");
  if (Verbose && Verbose[0] == '1') {
    unsigned FnCount = 0;
    NevercValueRef *Fns = NEVERC_COLLECT_DEFINED_FUNCTIONS(API, M, &FnCount);
    if (Fns) {
      for (unsigned I = 0; I < FnCount; I++)
        API->DiagNoteF(PLUGIN_TAG "  fn: %s", API->ValueGetName(Fns[I]));
      API->Free(Fns);
    }
  }

  if (API->PluginHasArg("prefix")) {
    const char *Prefix = API->PluginGetArg("prefix");
    API->DiagNoteF(PLUGIN_TAG "Custom prefix: %s",
                   Prefix ? Prefix : "(empty)");
  }
  return 0;
}

/* ======================================================================== */
/*  MIR Pass                                                                */
/* ======================================================================== */

/*
 * MIR analysis: count instructions and classify operands.
 *
 * Uses batch collection (MFuncCollectBBs / MBBCollectInstructions) when
 * available, falling back to per-element iteration.  The batch path uses
 * a two-pass strategy: first scan finds the max per-BB instruction count
 * for a single allocation, then the second pass reuses that buffer for
 * every BB -- eliminates N alloc/free cycles.
 */
static int mirAnalysisPass(NevercMachineFuncRef MF, const NevercHostAPI *API,
                           void *UserData) {
  (void)UserData;
  const char *FnName = API->MFuncGetName(MF);
  unsigned InstrCount = 0;
  unsigned RegOps = 0;
  unsigned ImmOps = 0;

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

    unsigned MaxIC = 0;
    for (unsigned B = 0; B < BBCount; B++) {
      unsigned IC = API->MBBGetInstCount(MBBs[B]);
      InstrCount += IC;
      if (IC > MaxIC)
        MaxIC = IC;
    }

    NevercMachineInstrRef *MIs =
        MaxIC > 0 ? NEVERC_ALLOC_ARRAY(API, NevercMachineInstrRef, MaxIC)
                  : NULL;
    for (unsigned B = 0; B < BBCount && MIs; B++) {
      unsigned IC = API->MBBGetInstCount(MBBs[B]);
      if (IC == 0)
        continue;
      API->MBBCollectInstructions(MBBs[B], MIs);
      for (unsigned J = 0; J < IC; J++) {
        unsigned NumOps = API->MInstGetNumOperands(MIs[J]);
        for (unsigned K = 0; K < NumOps; K++) {
          if (API->MInstGetOperandIsReg(MIs[J], K))
            RegOps++;
          else if (API->MInstGetOperandIsImm(MIs[J], K))
            ImmOps++;
        }
      }
    }
    API->Free(MIs);
    API->Free(MBBs);
  } else {
    NevercMachineBBRef MBB = API->MFuncGetFirstBB(MF);
    while (MBB) {
      NevercMachineInstrRef MI = API->MBBGetFirstInst(MBB);
      while (MI) {
        InstrCount++;
        unsigned NumOps = API->MInstGetNumOperands(MI);
        for (unsigned I = 0; I < NumOps; I++) {
          if (API->MInstGetOperandIsReg(MI, I))
            RegOps++;
          else if (API->MInstGetOperandIsImm(MI, I))
            ImmOps++;
        }
        MI = API->MBBGetNextInst(MI);
      }
      MBB = API->MFuncGetNextBB(MBB);
    }
  }

  API->DiagNoteF(PLUGIN_TAG "MIR '%s': %u BBs, %u instrs, %u reg, %u imm",
                 FnName, BBCount, InstrCount, RegOps, ImmOps);
  return 0;
}

/* ======================================================================== */
/*  Binary Passes                                                           */
/* ======================================================================== */

/* Report extracted shellcode size. */
static int binaryInfoPass(uint8_t **Data, uint64_t *Len, uint64_t *Capacity,
                          const NevercHostAPI *API, void *UserData) {
  (void)Data;
  (void)Capacity;
  (void)UserData;
  API->DiagNoteF(PLUGIN_TAG "Binary: %" PRIu64 " bytes", *Len);
  return 0;
}

/* Append a 16-byte NOP sled -- binary mutation demo. */
static int binaryNopSledPass(uint8_t **Data, uint64_t *Len, uint64_t *Capacity,
                             const NevercHostAPI *API, void *UserData) {
  (void)UserData;
  uint64_t OldLen = *Len;
  uint64_t NewLen = OldLen + 16;

  if (!API->BinaryResize(Data, Len, Capacity, NewLen)) {
    API->DiagWarningF(PLUGIN_TAG "BinaryResize failed");
    return 0;
  }

  API->MemSet(*Data + OldLen, 0x90, 16);
  API->DiagNoteF(PLUGIN_TAG "NOP sled: %" PRIu64 " -> %" PRIu64 " bytes",
                 OldLen, NewLen);
  return 1;
}

/* ======================================================================== */
/*  LTO Pass                                                                */
/* ======================================================================== */

/* LTO pipeline tracker -- reports defined function count. */
static int ltoInfoPass(NevercModuleRef M, const NevercHostAPI *API,
                       void *UserData) {
  const char *Stage = NEVERC_HOOK_NAME(API, UserData);
  unsigned Defined = 0;
  NevercValueRef *Fns = NEVERC_COLLECT_DEFINED_FUNCTIONS(API, M, &Defined);
  if (Fns)
    API->Free(Fns);
  API->DiagNoteF(PLUGIN_TAG "%s: %u defined functions", Stage, Defined);
  return 0;
}

/* ======================================================================== */
/*  Linker Pass                                                             */
/* ======================================================================== */

/* Symbol and section census -- linker hook demo. */
static int linkerCensusPass(const NevercHostAPI *API, void *UserData) {
  const char *Stage = NEVERC_HOOK_NAME(API, UserData);
  unsigned Defined = 0, Undefined = 0;

  NevercLinkerSymbolRef Sym = API->LinkGetFirstSymbol();
  while (Sym) {
    if (API->LinkSymbolIsDefined(Sym))
      Defined++;
    else
      Undefined++;
    Sym = API->LinkGetNextSymbol(Sym);
  }

  unsigned Sections = 0;
  NevercLinkerSectionRef Sec = API->LinkGetFirstSection();
  while (Sec) {
    Sections++;
    Sec = API->LinkGetNextSection(Sec);
  }

  const char *Fmt = NEVERC_API_FN(API, LinkGetOutputFormatName)
                        ? API->LinkGetOutputFormatName()
                        : "unknown";
  API->DiagNoteF(PLUGIN_TAG "Link %s [%s]: %u defined, %u undef, %u sections",
                 Stage, Fmt, Defined, Undefined, Sections);
  return 0;
}

/* ======================================================================== */
/*  Stage Trackers -- verify all hook points are wired correctly             */
/* ======================================================================== */

static int stageTrackerModulePass(NevercModuleRef M, const NevercHostAPI *API,
                                  void *UserData) {
  (void)M;
  API->DiagNoteF(PLUGIN_TAG "Stage: %s", NEVERC_HOOK_NAME(API, UserData));
  return 0;
}

static int stageTrackerMachinePass(NevercMachineFuncRef MF,
                                   const NevercHostAPI *API, void *UserData) {
  const char *FnName = API->MFuncGetName(MF);
  API->DiagNoteF(PLUGIN_TAG "Stage: %s (MF=%s)", NEVERC_HOOK_NAME(API, UserData),
                 FnName ? FnName : "?");
  return 0;
}

static int stageTrackerBinaryPass(uint8_t **Data, uint64_t *Len,
                                  uint64_t *Capacity, const NevercHostAPI *API,
                                  void *UserData) {
  (void)Data;
  (void)Capacity;
  API->DiagNoteF(PLUGIN_TAG "Stage: %s (binary=%" PRIu64 " bytes)",
                 NEVERC_HOOK_NAME(API, UserData), *Len);
  return 0;
}

/* ======================================================================== */
/*  Registration                                                            */
/* ======================================================================== */

static void registerPasses(const NevercHostAPI *API, void *Registrar) {

  /* Normal flow -- IR hooks */
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT, pluginArgDemoPass,
                          NULL, "example-args");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT, functionCounterPass,
                          NULL, "example-counter");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          functionEntryInstrPass, NULL, "example-instrument");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          intrinsicHistogramPass, NULL,
                          "example-intrinsic-histogram");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          opcodeHistogramPass, NULL,
                          "example-opcode-histogram");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          sortedFuncAnalysisPass, NULL,
                          "example-sorted-analysis");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_POST_OPT,
                          deadFunctionRemovalPass, NULL, "example-dead-remove");

  API->RegisterModulePass(Registrar, NEVERC_HOOK_PIPELINE_START,
                          pipelineStagePass,
                          NEVERC_HOOK_UD(NEVERC_HOOK_PIPELINE_START),
                          "example-pipeline-start");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PIPELINE_LAST,
                          pipelineStagePass,
                          NEVERC_HOOK_UD(NEVERC_HOOK_PIPELINE_LAST),
                          "example-pipeline-last");

  /* Normal flow -- MIR hooks */
  API->RegisterMachinePass(Registrar, NEVERC_HOOK_BEFORE_CODEGEN_PREEMIT,
                           stageTrackerMachinePass,
                           NEVERC_HOOK_UD(NEVERC_HOOK_BEFORE_CODEGEN_PREEMIT),
                           "example-stage-codegen-preemit");
  API->RegisterMachinePass(Registrar, NEVERC_HOOK_AFTER_CODEGEN_FINAL_MIR,
                           stageTrackerMachinePass,
                           NEVERC_HOOK_UD(NEVERC_HOOK_AFTER_CODEGEN_FINAL_MIR),
                           "example-stage-codegen-final-mir");

  /* Shellcode flow -- IR hooks */
  API->RegisterModulePass(Registrar, NEVERC_HOOK_SC_BEFORE_PREP,
                          stageTrackerModulePass,
                          NEVERC_HOOK_UD(NEVERC_HOOK_SC_BEFORE_PREP),
                          "example-stage-sc-before-prep");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_SC_AFTER_PREP,
                          stageTrackerModulePass,
                          NEVERC_HOOK_UD(NEVERC_HOOK_SC_AFTER_PREP),
                          "example-stage-sc-after-prep");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_SC_BEFORE_INLINING,
                          stageTrackerModulePass,
                          NEVERC_HOOK_UD(NEVERC_HOOK_SC_BEFORE_INLINING),
                          "example-stage-sc-before-inlining");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_SC_AFTER_INLINING,
                          stageTrackerModulePass,
                          NEVERC_HOOK_UD(NEVERC_HOOK_SC_AFTER_INLINING),
                          "example-stage-sc-after-inlining");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_SC_AFTER_STACKIFY,
                          stageTrackerModulePass,
                          NEVERC_HOOK_UD(NEVERC_HOOK_SC_AFTER_STACKIFY),
                          "example-stage-sc-after-stackify");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_SC_AFTER_FINAL_IR,
                          stageTrackerModulePass,
                          NEVERC_HOOK_UD(NEVERC_HOOK_SC_AFTER_FINAL_IR),
                          "example-stage-sc-after-final-ir");

  /* Shellcode flow -- MIR hooks */
  API->RegisterMachinePass(Registrar, NEVERC_HOOK_SC_BEFORE_PREEMIT,
                           mirAnalysisPass, NULL, "example-mir-analysis");
  API->RegisterMachinePass(Registrar, NEVERC_HOOK_SC_AFTER_PREEMIT,
                           stageTrackerMachinePass,
                           NEVERC_HOOK_UD(NEVERC_HOOK_SC_AFTER_PREEMIT),
                           "example-stage-sc-after-preemit");
  API->RegisterMachinePass(Registrar, NEVERC_HOOK_SC_AFTER_FINAL_MIR,
                           stageTrackerMachinePass,
                           NEVERC_HOOK_UD(NEVERC_HOOK_SC_AFTER_FINAL_MIR),
                           "example-stage-sc-after-final-mir");

  /* Shellcode flow -- binary hooks */
  API->RegisterBinaryPass(Registrar, NEVERC_HOOK_SC_POST_EXTRACT,
                          binaryInfoPass, NULL, "example-binary-info");
  API->RegisterBinaryPass(Registrar, NEVERC_HOOK_SC_POST_EXTRACT,
                          binaryNopSledPass, NULL, "example-nop-sled");
  API->RegisterBinaryPass(Registrar, NEVERC_HOOK_SC_POST_FINALIZE,
                          stageTrackerBinaryPass,
                          NEVERC_HOOK_UD(NEVERC_HOOK_SC_POST_FINALIZE),
                          "example-stage-sc-post-finalize");

  /* LTO flow */
  API->RegisterModulePass(Registrar, NEVERC_HOOK_LTO_PRE_OPT, ltoInfoPass,
                          NEVERC_HOOK_UD(NEVERC_HOOK_LTO_PRE_OPT),
                          "example-lto-pre-opt");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_LTO_POST_OPT, ltoInfoPass,
                          NEVERC_HOOK_UD(NEVERC_HOOK_LTO_POST_OPT),
                          "example-lto-post-opt");

  /* Linker flow */
  if (NEVERC_API_FN(API, RegisterLinkerPass)) {
    API->RegisterLinkerPass(Registrar, NEVERC_HOOK_LINK_PRE_LAYOUT,
                            linkerCensusPass,
                            NEVERC_HOOK_UD(NEVERC_HOOK_LINK_PRE_LAYOUT),
                            "example-link-pre-layout");
    API->RegisterLinkerPass(Registrar, NEVERC_HOOK_LINK_POST_LAYOUT,
                            linkerCensusPass,
                            NEVERC_HOOK_UD(NEVERC_HOOK_LINK_POST_LAYOUT),
                            "example-link-post-layout");
    API->RegisterLinkerPass(Registrar, NEVERC_HOOK_LINK_POST_EMIT,
                            linkerCensusPass,
                            NEVERC_HOOK_UD(NEVERC_HOOK_LINK_POST_EMIT),
                            "example-link-post-emit");
  }
}

/* ======================================================================== */
/*  Entry Point                                                             */
/* ======================================================================== */

NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void) {
  NevercPluginInfo Info;
  Info.APIVersion = NEVERC_PLUGIN_API_VERSION;
  Info.PluginName = "example-plugin";
  Info.PluginVersion = "1.0.0";
  Info.RegisterPasses = registerPasses;
  Info.Destroy = NULL;
  return Info;
}
