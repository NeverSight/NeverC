/*
 * ExamplePlugin.c -- Pure C neverc out-of-tree pass plugin.
 *
 * Demonstrates:
 *   - Module-level IR reading and mutation
 *   - ModuleCollectDefinedFunctions -- host-side filter+collect in one call
 *   - Arena arrays for collect-filter-sort workflows (zero individual Free)
 *   - StrMap -- opaque string-keyed hash table for histogram / counting
 *   - IntMap -- opaque integer-keyed hash table for opcode counting
 *   - InstOpcodeToName -- numeric opcode -> human-readable name lookup
 *   - StrBuilder -- opaque incremental string construction
 *   - MIR-level analysis with batch collection
 *   - Binary-level inspection and patching (BinaryResize + NOP sled)
 *   - LTO pipeline hooks (LTO_PRE_OPT / LTO_POST_OPT)
 *   - Linker hooks (LINK_PRE_LAYOUT / POST_LAYOUT / POST_EMIT)
 *   - Use-def chain traversal (NEVERC_FOR_EACH_USE, UseGetUser)
 *   - FunctionGetInstructionCount (single-call instruction census)
 *   - DominatorTree / PostDominatorTree (on-demand CFG dominance analysis)
 *   - DomTreeGetDepth / PostDomTreeGetDepth (O(1) dom-tree level query)
 *   - ModuleGetFirstDefinedFunction / ModuleGetNextDefinedFunction
 *     (host-side declaration skip -- one vtable call per defined function)
 *   - LoopInfo (on-demand loop nest detection and queries)
 *   - FunctionClone (deep copy of entire functions)
 *   - SCEV (ScalarEvolution trip count / max trip count queries)
 *   - CallGraph (callee enumeration, SCC-based recursion detection)
 *   - LoopIsLoopInvariant (value invariance check)
 *   - Host-provided string/memory utilities (StrAfterPrefix, StrFindChar,
 *     StrNDup, StrBuilder, MemFindByte, MemCount, NEVERC_NPOS, etc.)
 *   - Cross-platform path manipulation (PathBaseNameOffset, PathExtOffset)
 *   - Sort (host-routed qsort -- no cross-DLL CRT calls)
 *   - Formatted diagnostics via DiagNoteF -- no manual StrFormat/Free dance
 *   - Plugin command-line arguments via PluginGetArg / PluginHasArg
 *   - HookPointGetName for runtime hook-name resolution
 *   - NEVERC_ALLOC_ARRAY / NEVERC_COLLECT_* convenience macros
 *   - NEVERC_COLLECT_OPCODES (batch opcode collection without Value handles)
 *   - NEVERC_FOR_EACH_{FUNCTION,DEFINED_FUNCTION,GLOBAL,ALIAS,BB,INST,...}
 *   - NEVERC_TRY_ARENA / NEVERC_AUTO_COLLECT_* / NEVERC_FREE_IF_HEAP /
 *     NEVERC_ARENA_DESTROY (arena-preferred collection with auto fallback)
 *   - ValueSet -- opaque hash set for O(1) membership testing
 *   - Arena (bump-pointer allocator -- single ArenaDestroy replaces N Frees)
 *   - NEVERC_ARENA_ALLOC_ARRAY / NEVERC_ARENA_CALLOC_ARRAY
 *   - NEVERC_ARENA_COLLECT_BBS / NEVERC_ARENA_COLLECT_MBBS (single-call
 *     BB/MBB collection replacing the GetCount+AllocArray+Fill pattern)
 *   - SortCtx (comparator receives void *Ctx for API access)
 *   - NEVERC_STRBUILDER_DIAG (zero-alloc StrBuilder -> diagnostic macro)
 *   - ArenaStrConcat / ArenaStrFormat (formatted strings into Arena)
 *   - PluginGetArgBool / PluginGetArgInt64 / PluginGetArgUInt64
 *   - ModuleGetDefinedFunctionCount (zero-allocation census)
 *   - NEVERC_HOOK_UD / NEVERC_HOOK_NAME / NEVERC_STRMAP_NEW / NEVERC_INTMAP_NEW
 *   - NEVERC_STR_OR (null/empty string fallback -- zero allocation, no vtable)
 *   - ModuleForEachFunction / ModuleForEachDefinedFunction /
 *     ModuleForEachInstruction / ModuleForEachGlobal / FunctionForEachBB /
 *     BBForEachInst (zero-alloc callback iteration -- one vtable call
 *     replaces N GetNext calls; fastest iteration mechanism)
 *   - MFuncForEachBB / MBBForEachInst (MIR zero-alloc callback iteration)
 *   - ModuleForEachAlias / ValueForEachUse / FunctionForEachInst
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

/*
 * Count defined functions and report module info.
 *
 * Shows: ModuleGetDefinedFunctionCount (zero-allocation census --
 *        no Collect + Free dance when only the count is needed),
 *        ModuleGetFunctionCount / ModuleGetGlobalCount (O(1) totals).
 *        Falls back to NEVERC_FOR_EACH_DEFINED_FUNCTION (zero-alloc
 *        iterator) on older hosts.
 */
static int functionCounterPass(NevercModuleRef M, const NevercHostAPI *API,
                               void *UserData) {
  (void)UserData;

  const char *Triple = API->ModuleGetTargetTriple(M);

  if (NEVERC_API_FN(API, ModuleGetDefinedFunctionCount)) {
    unsigned Defined = API->ModuleGetDefinedFunctionCount(M);
    unsigned Total = API->ModuleGetFunctionCount(M);
    unsigned Globals = API->ModuleGetGlobalCount(M);
    API->DiagNoteF(PLUGIN_TAG "Target: %s -- %u/%u functions (%u decls), "
                   "%u globals",
                   Triple, Defined, Total, Total - Defined, Globals);
  } else {
    unsigned Defined = 0;
    NEVERC_FOR_EACH_DEFINED_FUNCTION(API, M, F) {
      (void)F;
      ++Defined;
    }
    API->DiagNoteF(PLUGIN_TAG "Target: %s -- %u defined functions",
                   Triple, Defined);
  }
  return 0;
}

/*
 * Insert a call to an external tracing function at every function entry:
 *
 *   declare void @__neverc_plugin_trace(ptr %fn_name)
 *
 * Shows: type creation, ModuleAddFunction, BuilderCreate, BuildGlobalStringPtr,
 *        BuildCall, HostIsShellcodeMode guard,
 *        NEVERC_FOR_EACH_DEFINED_FUNCTION (zero-allocation iteration --
 *        no Collect+Free needed for a single-pass instrumentation).
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

  int Modified = 0;
  NevercBuilderRef Builder = API->BuilderCreate(Ctx);

  NEVERC_FOR_EACH_DEFINED_FUNCTION(API, M, F) {
    if (F == TraceFn)
      continue;
    NevercBasicBlockRef EntryBB = API->FunctionGetFirstBB(F);
    if (!EntryBB)
      continue;
    NevercValueRef FirstInst = API->BBGetFirstInst(EntryBB);
    if (FirstInst)
      API->BuilderSetInsertPointBefore(Builder, FirstInst);
    else
      API->BuilderSetInsertPoint(Builder, EntryBB);

    NevercValueRef NamePtr =
        API->BuildGlobalStringPtr(Builder, API->ValueGetName(F), "fn.name");
    NevercValueRef CallArgs[1];
    CallArgs[0] = NamePtr;
    API->BuildCall(Builder, TraceFnTy, TraceFn, CallArgs, 1, "");
    Modified = 1;
  }

  API->BuilderDispose(Builder);

  if (Modified)
    API->DiagNoteF(PLUGIN_TAG "Inserted function-entry tracing calls");
  return Modified;
}

/*
 * Remove dead internal functions (analyze -> modify workflow).
 *
 * Shows: FunctionGetLinkage, ValueGetNumUses, ModuleRemoveFunction,
 *        Arena (flat arrays for the two-phase collect-then-delete
 *        pattern -- zero per-element vtable calls, single ArenaDestroy
 *        reclaims both Fns and Dead arrays).
 */
static int deadFunctionRemovalPass(NevercModuleRef M, const NevercHostAPI *API,
                                   void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, ModuleRemoveFunction))
    return 0;

  NevercArenaRef Scratch = NEVERC_TRY_ARENA(API);
  if (!Scratch)
    return 0;

  unsigned FnCount = 0;
  NevercValueRef *Fns =
      NEVERC_ARENA_COLLECT_DEFINED_FUNCTIONS(API, Scratch, M, &FnCount);
  if (!Fns) {
    API->ArenaDestroy(Scratch);
    return 0;
  }

  NevercValueRef *Dead =
      NEVERC_ARENA_ALLOC_ARRAY(API, Scratch, NevercValueRef, FnCount);
  if (!Dead) {
    API->ArenaDestroy(Scratch);
    return 0;
  }

  unsigned DeadCount = 0;
  for (unsigned I = 0; I < FnCount; I++) {
    unsigned Linkage = API->FunctionGetLinkage(Fns[I]);
    if ((Linkage == NEVERC_LINKAGE_INTERNAL ||
         Linkage == NEVERC_LINKAGE_PRIVATE) &&
        API->ValueGetNumUses(Fns[I]) == 0)
      Dead[DeadCount++] = Fns[I];
  }

  for (unsigned I = 0; I < DeadCount; I++) {
    API->DiagNoteF(PLUGIN_TAG "Removing dead: %s",
                   API->ValueGetName(Dead[I]));
    API->ModuleRemoveFunction(M, Dead[I]);
  }

  API->ArenaDestroy(Scratch);

  if (DeadCount > 0)
    API->DiagNoteF(PLUGIN_TAG "Removed %u dead internal functions",
                   DeadCount);
  return DeadCount > 0;
}

/*
 * Pipeline stage tracker -- reports function/BB counts at a specific stage.
 *
 * Registered at PIPELINE_START and PIPELINE_LAST so users can see the
 * optimizer's impact.  UserData carries NevercHookPoint as (void*).
 *
 * Shows: ModuleForEachDefinedFunction (zero-alloc callback iteration --
 *        one vtable call replaces N GetNext calls; fastest path),
 *        NEVERC_FOR_EACH_DEFINED_FUNCTION (fallback for older hosts),
 *        NEVERC_HOOK_NAME (resolve UserData -> name).
 */

struct PipelineStageCtx {
  const NevercHostAPI *API;
  unsigned Defined;
  unsigned BBTotal;
  unsigned InstTotal;
  int HasInstCount;
};

static int pipelineStageVisitor(NevercValueRef F, void *Ctx) {
  struct PipelineStageCtx *S = (struct PipelineStageCtx *)Ctx;
  S->Defined++;
  S->BBTotal += S->API->FunctionGetBBCount(F);
  if (S->HasInstCount)
    S->InstTotal += S->API->FunctionGetInstructionCount(F);
  return 0;
}

static int pipelineStagePass(NevercModuleRef M, const NevercHostAPI *API,
                             void *UserData) {
  const char *Stage = NEVERC_HOOK_NAME(API, UserData);

  struct PipelineStageCtx Ctx;
  Ctx.API = API;
  Ctx.Defined = 0;
  Ctx.BBTotal = 0;
  Ctx.InstTotal = 0;
  Ctx.HasInstCount = NEVERC_API_FN(API, FunctionGetInstructionCount) != 0;

  if (NEVERC_API_FN(API, ModuleForEachDefinedFunction)) {
    API->ModuleForEachDefinedFunction(M, pipelineStageVisitor, &Ctx);
  } else {
    NEVERC_FOR_EACH_DEFINED_FUNCTION(API, M, F) {
      pipelineStageVisitor(F, &Ctx);
    }
  }

  if (Ctx.HasInstCount)
    API->DiagNoteF(PLUGIN_TAG "%s: %u functions, %u BBs, %u instrs", Stage,
                   Ctx.Defined, Ctx.BBTotal, Ctx.InstTotal);
  else
    API->DiagNoteF(PLUGIN_TAG "%s: %u functions, %u BBs", Stage,
                   Ctx.Defined, Ctx.BBTotal);
  return 0;
}

/*
 * Intrinsic category histogram using StrMap + StrBuilder + StrMapForEach.
 *
 * Groups all LLVM intrinsics by category (the segment between "llvm."
 * and the next dot), counts occurrences in a StrMap, then iterates the
 * map via StrMapForEach to build a summary with StrBuilder.
 *
 * Shows: ModuleForEachFunction (zero-alloc callback iteration over ALL
 *        functions including declarations -- intrinsics are declarations),
 *        StrAfterPrefix (zero-allocation prefix skip), StrMapIncrementN
 *        (single-probe bounded-key counting -- no temp string copy),
 *        StrBuilder (incremental string construction), StrMapForEach
 *        (zero-alloc callback iteration), NEVERC_NPOS sentinel.
 */

struct IntrinsicHistCtx {
  const NevercHostAPI *API;
  NevercStrMapRef Hist;
};

static int intrinsicHistVisitor(NevercValueRef F, void *Ctx) {
  struct IntrinsicHistCtx *H = (struct IntrinsicHistCtx *)Ctx;
  const char *After =
      H->API->StrAfterPrefix(H->API->ValueGetName(F), "llvm.");
  if (!After)
    return 0;
  uint64_t DotPos = H->API->StrFindChar(After, '.');
  if (DotPos == NEVERC_NPOS)
    return 0;
  H->API->StrMapIncrementN(H->Hist, After, DotPos, 1);
  return 0;
}

struct HistOutputCtx {
  const NevercHostAPI *API;
  NevercStrBuilderRef SB;
};

static int histOutputVisitor(const char *Key, uint64_t Value, void *Ctx) {
  struct HistOutputCtx *H = (struct HistOutputCtx *)Ctx;
  H->API->StrBuilderAppendF(H->SB, " %s=%" PRIu64, Key, Value);
  return 0;
}

static int intrinsicHistogramPass(NevercModuleRef M, const NevercHostAPI *API,
                                  void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, StrMapCreate) ||
      !NEVERC_API_FN(API, StrBuilderCreate) ||
      !NEVERC_API_FN(API, StrAfterPrefix) ||
      !NEVERC_API_FN(API, StrMapIncrementN))
    return 0;

  NevercStrMapRef Hist = NEVERC_STRMAP_NEW(API, 64);
  if (!Hist)
    return 0;

  struct IntrinsicHistCtx HistCtx;
  HistCtx.API = API;
  HistCtx.Hist = Hist;

  if (NEVERC_API_FN(API, ModuleForEachFunction)) {
    API->ModuleForEachFunction(M, intrinsicHistVisitor, &HistCtx);
  } else {
    NEVERC_FOR_EACH_FUNCTION(API, M, F) {
      intrinsicHistVisitor(F, &HistCtx);
    }
  }

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
    struct HistOutputCtx OutCtx;
    OutCtx.API = API;
    OutCtx.SB = SB;
    API->StrMapForEach(Hist, histOutputVisitor, &OutCtx);
  }

  API->StrMapDestroy(Hist);
  NEVERC_STRBUILDER_DIAG(API, SB, DiagNote);
  API->StrBuilderDestroy(SB);
  return 0;
}

/*
 * Opcode histogram using IntMap -- counts IR instruction opcodes.
 *
 * Four-tier fallback for maximum performance on every host version:
 *   1. Batch opcodes  (NEVERC_AUTO_COLLECT_OPCODES -- fastest, 1 vtable call)
 *   2. Batch instrs   (NEVERC_AUTO_COLLECT_INSTRUCTIONS -- 1 vtable + N opcode)
 *   3. ForEach instrs (ModuleForEachInstruction -- 1 vtable + N cb + N opcode)
 *   4. Nested macros  (FOR_EACH triple loop -- ~4N vtable calls)
 *
 * Shows: NEVERC_AUTO_COLLECT_OPCODES / NEVERC_AUTO_COLLECT_INSTRUCTIONS,
 *        ModuleForEachInstruction (flattened zero-alloc callback iteration),
 *        IntMap, IntMapIncrement, IntMapForEach, InstOpcodeToName, StrBuilder.
 */

struct OpcodeCountCtx {
  const NevercHostAPI *API;
  NevercIntMapRef Hist;
  unsigned InstCount;
};

static int opcodeCountVisitor(NevercValueRef I, void *Ctx) {
  struct OpcodeCountCtx *O = (struct OpcodeCountCtx *)Ctx;
  O->API->IntMapIncrement(O->Hist, O->API->InstGetOpcode(I), 1);
  O->InstCount++;
  return 0;
}

struct OpcodeOutputCtx {
  const NevercHostAPI *API;
  NevercStrBuilderRef SB;
  int HasOpcodeToName;
};

static int opcodeOutputVisitor(uint64_t Key, uint64_t Value, void *Ctx) {
  struct OpcodeOutputCtx *O = (struct OpcodeOutputCtx *)Ctx;
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

  NevercArenaRef Scratch = NEVERC_TRY_ARENA(API);
  unsigned InstCount = 0;

  /* Tier 1: batch opcodes -- host walks IR, returns raw unsigned array. */
  unsigned *Opcodes = NEVERC_AUTO_COLLECT_OPCODES(API, Scratch, M, &InstCount);
  if (Opcodes) {
    for (unsigned I = 0; I < InstCount; I++)
      API->IntMapIncrement(Hist, Opcodes[I], 1);
    NEVERC_FREE_IF_HEAP(API, Opcodes, Scratch);
  } else {
    /* Tier 2: batch instructions -- host walks IR, returns Value handles. */
    NevercValueRef *Insts =
        NEVERC_AUTO_COLLECT_INSTRUCTIONS(API, Scratch, M, &InstCount);
    if (Insts) {
      for (unsigned I = 0; I < InstCount; I++)
        API->IntMapIncrement(Hist, API->InstGetOpcode(Insts[I]), 1);
      NEVERC_FREE_IF_HEAP(API, Insts, Scratch);
    } else if (NEVERC_API_FN(API, ModuleForEachInstruction)) {
      /* Tier 3: callback iteration -- 1 vtable call + N callbacks. */
      struct OpcodeCountCtx CountCtx;
      CountCtx.API = API;
      CountCtx.Hist = Hist;
      CountCtx.InstCount = 0;
      API->ModuleForEachInstruction(M, opcodeCountVisitor, &CountCtx);
      InstCount = CountCtx.InstCount;
    } else {
      /* Tier 4: per-element vtable iteration (oldest hosts). */
      NEVERC_FOR_EACH_DEFINED_FUNCTION(API, M, F) {
        NEVERC_FOR_EACH_BB(API, F, BB) {
          NEVERC_FOR_EACH_INST(API, BB, I) {
            API->IntMapIncrement(Hist, API->InstGetOpcode(I), 1);
            ++InstCount;
          }
        }
      }
    }
  }

  NEVERC_ARENA_DESTROY(API, Scratch);

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

  struct OpcodeOutputCtx OutCtx;
  OutCtx.API = API;
  OutCtx.SB = SB;
  OutCtx.HasOpcodeToName = NEVERC_API_FN(API, InstOpcodeToName) != 0;
  API->IntMapForEach(Hist, opcodeOutputVisitor, &OutCtx);

  API->IntMapDestroy(Hist);
  NEVERC_STRBUILDER_DIAG(API, SB, DiagNote);
  API->StrBuilderDestroy(SB);
  return 0;
}

/*
 * Sort API demo: collect defined functions, sort by instruction count
 * descending, report top-N largest functions with both instruction and
 * BB counts.  Falls back to BB count when FunctionGetInstructionCount
 * is unavailable on older hosts.
 *
 * Shows: Arena (Fns + Entries in the same arena -- single ArenaDestroy
 *        reclaims everything; contiguous array layout for cache-friendly
 *        sort),
 *        Sort (in-place qsort on arena-allocated array with direct
 *        indexing),
 *        FunctionGetInstructionCount (single-call census),
 *        branchless comparator for qsort.
 */

struct FuncSortEntry {
  NevercValueRef Fn;
  unsigned InstCount;
  unsigned BBCount;
};

static int cmpFuncByInstCountDesc(const void *A, const void *B) {
  unsigned CA = ((const struct FuncSortEntry *)A)->InstCount;
  unsigned CB = ((const struct FuncSortEntry *)B)->InstCount;
  return (CA < CB) - (CA > CB);
}

static int sortedFuncAnalysisPass(NevercModuleRef M, const NevercHostAPI *API,
                                  void *UserData) {
  (void)UserData;

  NevercArenaRef Scratch = NEVERC_TRY_ARENA(API);
  if (!Scratch)
    return 0;

  unsigned FnCount = 0;
  NevercValueRef *Fns =
      NEVERC_ARENA_COLLECT_DEFINED_FUNCTIONS(API, Scratch, M, &FnCount);
  if (!Fns || FnCount == 0) {
    API->ArenaDestroy(Scratch);
    return 0;
  }

  struct FuncSortEntry *Entries =
      NEVERC_ARENA_ALLOC_ARRAY(API, Scratch, struct FuncSortEntry, FnCount);
  if (!Entries) {
    API->ArenaDestroy(Scratch);
    return 0;
  }

  int HasInstCount = NEVERC_API_FN(API, FunctionGetInstructionCount) != 0;
  for (unsigned I = 0; I < FnCount; I++) {
    Entries[I].Fn = Fns[I];
    Entries[I].BBCount = API->FunctionGetBBCount(Fns[I]);
    Entries[I].InstCount = HasInstCount
                               ? API->FunctionGetInstructionCount(Fns[I])
                               : Entries[I].BBCount;
  }

  API->Sort(Entries, FnCount, sizeof(struct FuncSortEntry),
            cmpFuncByInstCountDesc);

  unsigned Top = FnCount < 5 ? FnCount : 5;
  for (unsigned I = 0; I < Top; I++)
    API->DiagNoteF(PLUGIN_TAG "Top func: #%u %s (%u instrs, %u BBs)", I + 1,
                   API->ValueGetName(Entries[I].Fn), Entries[I].InstCount,
                   Entries[I].BBCount);

  API->ArenaDestroy(Scratch);
  return 0;
}

/*
 * Source-file inspection demo: split the module's source path into
 * directory / basename / extension using zero-allocation Path*Offset
 * for the offsets, then materialize each slice into a single Arena via
 * ArenaStrSubstring.  ArenaStrToLower normalizes the extension for case-
 * insensitive dispatch (".C" / ".CPP" -> ".c" / ".cpp").  All slices
 * share one ArenaDestroy at the end -- no individual Free calls.
 *
 * Shows: PathBaseNameOffset / PathExtOffset (zero-alloc cross-platform
 *        path split), ArenaStrSubstring (slice into arena, no host
 *        malloc/Free pair), ArenaStrToLower (ASCII-only lowercase into
 *        arena), NEVERC_NPOS sentinel, NEVERC_TRY_ARENA fallback.
 */
static int sourceInfoPass(NevercModuleRef M, const NevercHostAPI *API,
                          void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, ModuleGetSourceFileName) ||
      !NEVERC_API_FN(API, PathBaseNameOffset))
    return 0;

  const char *Path = API->ModuleGetSourceFileName(M);
  if (!Path || !*Path) {
    API->DiagNoteF(PLUGIN_TAG "Source: <none>");
    return 0;
  }

  uint64_t BaseOff = API->PathBaseNameOffset(Path);
  uint64_t ExtOff = NEVERC_API_FN(API, PathExtOffset)
                        ? API->PathExtOffset(Path)
                        : NEVERC_NPOS;

  if (NEVERC_API_FN(API, ArenaStrSubstring)) {
    NevercArenaRef A = NEVERC_TRY_ARENA(API);
    if (A) {
      char *Dir = API->ArenaStrSubstring(A, Path, 0, BaseOff);
      char *Base =
          (ExtOff != NEVERC_NPOS)
              ? API->ArenaStrSubstring(A, Path, BaseOff, ExtOff - BaseOff)
              : API->ArenaStrSubstring(A, Path, BaseOff, NEVERC_NPOS);
      char *Ext = (ExtOff != NEVERC_NPOS)
                      ? API->ArenaStrSubstring(A, Path, ExtOff, NEVERC_NPOS)
                      : NULL;
      char *ExtLower = (Ext && NEVERC_API_FN(API, ArenaStrToLower))
                           ? API->ArenaStrToLower(A, Ext)
                           : Ext;
      int ExtNormalized = Ext && ExtLower && Ext != ExtLower &&
                          !API->StrEqual(Ext, ExtLower);

      if (Ext)
        API->DiagNoteF(PLUGIN_TAG "Source: dir=%s base=%s ext=%s%s%s",
                       NEVERC_STR_OR(Dir, ""),
                       NEVERC_STR_OR(Base, ""),
                       NEVERC_STR_OR(Ext, ""),
                       ExtNormalized ? " norm=" : "",
                       ExtNormalized ? ExtLower : "");
      else
        API->DiagNoteF(PLUGIN_TAG "Source: dir=%s base=%s",
                       NEVERC_STR_OR(Dir, ""), NEVERC_STR_OR(Base, ""));

      API->ArenaDestroy(A);
      return 0;
    }
  }

  if (ExtOff != NEVERC_NPOS)
    API->DiagNoteF(PLUGIN_TAG "Source: dir=%.*s base=%.*s ext=%s",
                   (int)BaseOff, Path,
                   (int)(ExtOff - BaseOff), Path + BaseOff,
                   Path + ExtOff);
  else
    API->DiagNoteF(PLUGIN_TAG "Source: dir=%.*s base=%s",
                   (int)BaseOff, Path, Path + BaseOff);
  return 0;
}

/*
 * Call-site analysis: count how many call sites each defined function has.
 *
 * Shows: NEVERC_FOR_EACH_USE (use-def chain traversal), InstIsCall /
 *        InstIsCallLike (filtering uses to calls only),
 *        FunctionGetInstructionCount (single-vtable-call convenience counter),
 *        Arena + Sort (contiguous array sort -- zero per-element vtable
 *        Push, direct indexing for top-N, single ArenaDestroy cleanup).
 */

struct CallSiteEntry {
  NevercValueRef Fn;
  unsigned CallSites;
  unsigned InstCount;
};

static int cmpCallSitesDesc(const void *A, const void *B) {
  unsigned CA = ((const struct CallSiteEntry *)A)->CallSites;
  unsigned CB = ((const struct CallSiteEntry *)B)->CallSites;
  return (CA < CB) - (CA > CB);
}

static int callSiteAnalysisPass(NevercModuleRef M, const NevercHostAPI *API,
                                void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, ValueGetFirstUse))
    return 0;

  NevercArenaRef Scratch = NEVERC_TRY_ARENA(API);
  if (!Scratch)
    return 0;

  unsigned FnCount = 0;
  NevercValueRef *Fns =
      NEVERC_ARENA_COLLECT_DEFINED_FUNCTIONS(API, Scratch, M, &FnCount);
  if (!Fns || FnCount == 0) {
    API->ArenaDestroy(Scratch);
    return 0;
  }

  struct CallSiteEntry *Entries =
      NEVERC_ARENA_ALLOC_ARRAY(API, Scratch, struct CallSiteEntry, FnCount);
  if (!Entries) {
    API->ArenaDestroy(Scratch);
    return 0;
  }

  int HasCalledOp = NEVERC_API_FN(API, CallGetCalledOperand) != 0;
  int HasCallLike = NEVERC_API_FN(API, InstIsCallLike) != 0;
  int HasInstCount = NEVERC_API_FN(API, FunctionGetInstructionCount) != 0;

  for (unsigned I = 0; I < FnCount; I++) {
    unsigned Sites = 0;
    NEVERC_FOR_EACH_USE(API, Fns[I], U) {
      NevercValueRef User = API->UseGetUser(U);
      int IsCallLike = HasCallLike ? API->InstIsCallLike(User)
                                   : API->InstIsCall(User);
      if (!IsCallLike)
        continue;
      if (HasCalledOp) {
        if (API->CallGetCalledOperand(User) == Fns[I])
          Sites++;
      } else {
        Sites++;
      }
    }
    Entries[I].Fn = Fns[I];
    Entries[I].CallSites = Sites;
    Entries[I].InstCount = HasInstCount
                               ? API->FunctionGetInstructionCount(Fns[I])
                               : API->FunctionGetBBCount(Fns[I]);
  }

  API->Sort(Entries, FnCount, sizeof(struct CallSiteEntry), cmpCallSitesDesc);

  unsigned Top = FnCount < 5 ? FnCount : 5;
  for (unsigned I = 0; I < Top; I++)
    API->DiagNoteF(PLUGIN_TAG "Call sites: #%u %s (%u sites, %u instrs)",
                   I + 1, API->ValueGetName(Entries[I].Fn),
                   Entries[I].CallSites, Entries[I].InstCount);

  API->ArenaDestroy(Scratch);
  return 0;
}

/*
 * Unique call target analysis: count distinct call targets across the
 * entire module using a ValueSet for O(1) deduplication, and print the
 * first few unique targets via ValueSetForEach.
 *
 * Three-tier instruction iteration fallback:
 *   1. Batch collect (NEVERC_AUTO_COLLECT_INSTRUCTIONS)
 *   2. Callback iteration (ModuleForEachInstruction -- zero-alloc)
 *   3. Triple-nested FOR_EACH macros (oldest hosts)
 *
 * Shows: ValueSetCreate, ValueSetInsert, ValueSetCount,
 *        ValueSetForEach (zero-alloc callback iteration with early-exit),
 *        ModuleForEachInstruction (flattened zero-alloc instruction scan),
 *        NEVERC_AUTO_COLLECT_INSTRUCTIONS, NEVERC_FREE_IF_HEAP,
 *        CallGetCalledOperand, InstIsCall.
 */

struct CallTargetScanCtx {
  const NevercHostAPI *API;
  NevercValueSetRef Seen;
  unsigned TotalCalls;
};

static int callTargetScanVisitor(NevercValueRef I, void *Ctx) {
  struct CallTargetScanCtx *S = (struct CallTargetScanCtx *)Ctx;
  if (!S->API->InstIsCall(I))
    return 0;
  S->TotalCalls++;
  NevercValueRef Target = S->API->CallGetCalledOperand(I);
  if (Target)
    S->API->ValueSetInsert(S->Seen, Target);
  return 0;
}

struct UniqueTargetPrintCtx {
  const NevercHostAPI *API;
  unsigned Remaining;
};

static int printTargetVisitor(NevercValueRef V, void *Ctx) {
  struct UniqueTargetPrintCtx *U = (struct UniqueTargetPrintCtx *)Ctx;
  if (U->Remaining == 0)
    return 1;
  const char *Name = U->API->ValueGetName(V);
  U->API->DiagNoteF(PLUGIN_TAG "  target: %s",
                    NEVERC_STR_OR(Name, "<anon>"));
  U->Remaining--;
  return 0;
}

static int uniqueCallTargetsPass(NevercModuleRef M, const NevercHostAPI *API,
                                 void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, ValueSetCreate) ||
      !NEVERC_API_FN(API, CallGetCalledOperand))
    return 0;

  NevercValueSetRef Seen = API->ValueSetCreate();
  if (!Seen)
    return 0;

  unsigned TotalCalls = 0;

  NevercArenaRef Scratch = NEVERC_TRY_ARENA(API);
  unsigned InstCount = 0;
  NevercValueRef *Insts =
      NEVERC_AUTO_COLLECT_INSTRUCTIONS(API, Scratch, M, &InstCount);

  if (Insts) {
    for (unsigned I = 0; I < InstCount; I++) {
      if (!API->InstIsCall(Insts[I]))
        continue;
      TotalCalls++;
      NevercValueRef Target = API->CallGetCalledOperand(Insts[I]);
      if (Target)
        API->ValueSetInsert(Seen, Target);
    }
    NEVERC_FREE_IF_HEAP(API, Insts, Scratch);
  } else if (NEVERC_API_FN(API, ModuleForEachInstruction)) {
    struct CallTargetScanCtx ScanCtx;
    ScanCtx.API = API;
    ScanCtx.Seen = Seen;
    ScanCtx.TotalCalls = 0;
    API->ModuleForEachInstruction(M, callTargetScanVisitor, &ScanCtx);
    TotalCalls = ScanCtx.TotalCalls;
  } else {
    NEVERC_FOR_EACH_DEFINED_FUNCTION(API, M, F) {
      NEVERC_FOR_EACH_BB(API, F, BB) {
        NEVERC_FOR_EACH_INST(API, BB, I) {
          if (!API->InstIsCall(I))
            continue;
          TotalCalls++;
          NevercValueRef Target = API->CallGetCalledOperand(I);
          if (Target)
            API->ValueSetInsert(Seen, Target);
        }
      }
    }
  }

  NEVERC_ARENA_DESTROY(API, Scratch);

  unsigned UniqueTargets = API->ValueSetCount(Seen);
  API->DiagNoteF(PLUGIN_TAG "Call targets: %u total calls, %u unique targets",
                 TotalCalls, UniqueTargets);

  if (NEVERC_API_FN(API, ValueSetForEach) && UniqueTargets > 0) {
    struct UniqueTargetPrintCtx PrintCtx;
    PrintCtx.API = API;
    PrintCtx.Remaining = UniqueTargets < 5 ? UniqueTargets : 5;
    API->ValueSetForEach(Seen, printTargetVisitor, &PrintCtx);
  }

  API->ValueSetDestroy(Seen);
  return 0;
}

/*
 * CFG analysis: report dominator tree depth, unreachable blocks, and
 * loop nest structure for each defined function.
 *
 * Shows: Arena (single bump-pointer allocator owns function list + every
 *        per-iteration BB array -- single ArenaDestroy reclaims all),
 *        ArenaCollectBBs (single vtable call replaces GetBBCount +
 *        ArenaAllocArray + FunctionCollectBBs three-step pattern),
 *        FunctionBuildDomTree / FunctionBuildPostDomTree (on-demand analysis),
 *        DomTreeGetDepth (O(1) level query -- replaces O(depth) idom walk,
 *        with idom-walk fallback for older hosts),
 *        DomTreeIsReachable / PostDomTreeGetIPDom,
 *        FunctionBuildLoopInfo (on-demand loop detection),
 *        LoopInfoGetLoopFor / LoopGetDepth / LoopGetHeader / LoopIsInnermost
 *        (loop nest queries), analysis object lifecycle management.
 */
static int cfgAnalysisPass(NevercModuleRef M, const NevercHostAPI *API,
                           void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, FunctionBuildDomTree) ||
      !NEVERC_API_FN(API, FunctionBuildLoopInfo))
    return 0;

  NevercArenaRef Scratch = NEVERC_TRY_ARENA(API);
  if (!Scratch)
    return 0;

  unsigned FnCount = 0;
  NevercValueRef *Fns =
      NEVERC_ARENA_COLLECT_DEFINED_FUNCTIONS(API, Scratch, M, &FnCount);
  if (!Fns) {
    API->ArenaDestroy(Scratch);
    return 0;
  }

  int HasDepth = NEVERC_API_FN(API, DomTreeGetDepth) != 0;

  for (unsigned I = 0; I < FnCount; I++) {
    unsigned BBCount = 0;
    NevercBasicBlockRef *BBs =
        NEVERC_ARENA_COLLECT_BBS(API, Scratch, Fns[I], &BBCount);
    if (!BBs)
      continue;

    NevercDomTreeRef DT = API->FunctionBuildDomTree(Fns[I]);
    if (!DT)
      continue;
    unsigned Unreachable = 0;
    unsigned MaxDomDepth = 0;
    for (unsigned B = 0; B < BBCount; B++) {
      if (!API->DomTreeIsReachable(DT, BBs[B])) {
        Unreachable++;
        continue;
      }
      unsigned Depth;
      if (HasDepth) {
        Depth = API->DomTreeGetDepth(DT, BBs[B]);
      } else {
        Depth = 0;
        NevercBasicBlockRef Cur = API->DomTreeGetIDom(DT, BBs[B]);
        while (Cur) {
          Cur = API->DomTreeGetIDom(DT, Cur);
          Depth++;
        }
      }
      if (Depth > MaxDomDepth)
        MaxDomDepth = Depth;
    }
    API->DomTreeDestroy(DT);

    NevercLoopInfoRef LI = API->FunctionBuildLoopInfo(Fns[I]);
    unsigned TopLoops = 0, InnermostLoops = 0, MaxLoopDepth = 0;
    if (LI) {
      TopLoops = API->LoopInfoGetTopLevelLoopCount(LI);
      for (unsigned B = 0; B < BBCount; B++) {
        NevercLoopRef L = API->LoopInfoGetLoopFor(LI, BBs[B]);
        if (!L)
          continue;
        unsigned LD = API->LoopGetDepth(L);
        if (LD > MaxLoopDepth)
          MaxLoopDepth = LD;
        if (API->LoopGetHeader(L) == BBs[B] && API->LoopIsInnermost(L))
          InnermostLoops++;
      }
      API->LoopInfoDestroy(LI);
    }

    unsigned PostDomConvergence = 0;
    if (NEVERC_API_FN(API, FunctionBuildPostDomTree)) {
      NevercPostDomTreeRef PDT = API->FunctionBuildPostDomTree(Fns[I]);
      if (PDT) {
        for (unsigned B = 0; B < BBCount; B++) {
          if (API->PostDomTreeGetIPDom(PDT, BBs[B]))
            PostDomConvergence++;
        }
        API->PostDomTreeDestroy(PDT);
      }
    }

    API->DiagNoteF(PLUGIN_TAG "CFG '%s': %u BBs, dom-depth %u, "
                   "%u unreachable, %u pdom-edges, %u top-loops, "
                   "%u innermost, max-loop-depth %u",
                   API->ValueGetName(Fns[I]), BBCount, MaxDomDepth,
                   Unreachable, PostDomConvergence, TopLoops,
                   InnermostLoops, MaxLoopDepth);
  }

  API->ArenaDestroy(Scratch);
  return 0;
}

/*
 * Arena demo: collect per-function name snapshots into arena memory,
 * build a sorted index, and report all function names in sorted order
 * using zero individual Free calls.  All temporaries (name copies, sort
 * entries) are arena-allocated and freed in one shot by ArenaDestroy.
 *
 * Shows: ArenaCreate / ArenaStrDup / NEVERC_ARENA_ALLOC_ARRAY /
 *        ArenaGetBytesUsed (allocation profiling) / ArenaDestroy
 *        (bulk free replacing N individual Free calls),
 *        SortCtx (context-aware sort -- comparator accesses API->StrCompare
 *        instead of hand-rolling strcmp).
 */

struct ArenaSortEntry {
  const char *Name;
  unsigned InstCount;
};

static int cmpArenaSortByName(const void *A, const void *B, void *Ctx) {
  const NevercHostAPI *API = (const NevercHostAPI *)Ctx;
  const struct ArenaSortEntry *EA = (const struct ArenaSortEntry *)A;
  const struct ArenaSortEntry *EB = (const struct ArenaSortEntry *)B;
  return API->StrCompare(EA->Name, EB->Name);
}

static int arenaCollectAnalysisPass(NevercModuleRef M,
                                    const NevercHostAPI *API,
                                    void *UserData) {
  (void)UserData;
  NevercArenaRef A = NEVERC_TRY_ARENA(API);
  if (!A)
    return 0;

  unsigned FnCount = 0;
  NevercValueRef *Fns =
      NEVERC_ARENA_COLLECT_DEFINED_FUNCTIONS(API, A, M, &FnCount);
  if (!Fns || FnCount == 0) {
    API->ArenaDestroy(A);
    return 0;
  }

  struct ArenaSortEntry *Entries =
      NEVERC_ARENA_ALLOC_ARRAY(API, A, struct ArenaSortEntry, FnCount);
  if (!Entries) {
    API->ArenaDestroy(A);
    return 0;
  }

  int HasInstCount = NEVERC_API_FN(API, FunctionGetInstructionCount) != 0;
  for (unsigned I = 0; I < FnCount; I++) {
    Entries[I].Name = API->ArenaStrDup(A, API->ValueGetName(Fns[I]));
    Entries[I].InstCount = HasInstCount
                               ? API->FunctionGetInstructionCount(Fns[I])
                               : API->FunctionGetBBCount(Fns[I]);
  }

  int Sorted = 0;
  if (NEVERC_API_FN(API, SortCtx)) {
    API->SortCtx(Entries, FnCount, sizeof(struct ArenaSortEntry),
                 cmpArenaSortByName, (void *)API);
    Sorted = 1;
  }

  unsigned Top = FnCount < 5 ? FnCount : 5;
  for (unsigned I = 0; I < Top; I++)
    API->DiagNoteF(PLUGIN_TAG "Arena %s: #%u %s (%u instrs)",
                   Sorted ? "sorted" : "unsorted", I + 1,
                   Entries[I].Name, Entries[I].InstCount);

  uint64_t BytesUsed = API->ArenaGetBytesUsed(A);
  API->DiagNoteF(PLUGIN_TAG "Arena: %u entries, %" PRIu64 " bytes, "
                 "0 individual frees",
                 FnCount, BytesUsed);

  API->ArenaDestroy(A);
  return 0;
}

/*
 * Function cloning demo: duplicate functions whose name starts with a
 * configurable prefix.  The clone gets InternalLinkage and a "_clone"
 * suffix.
 *
 * Shows: FunctionClone (deep copy), PluginGetArg (reading args),
 *        StrStartsWith (prefix matching), ArenaStrConcat (clone-name
 *        construction allocated directly into the Arena),
 *        ArenaCollectDefinedFunctions (the function array is also
 *        arena-owned -- a single ArenaDestroy reclaims everything,
 *        zero individual Free, zero mi_malloc/mi_free pair on the
 *        plugin's hot path).
 */
static int cloneDemoPass(NevercModuleRef M, const NevercHostAPI *API,
                         void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, FunctionClone) ||
      !NEVERC_API_FN(API, ArenaStrConcat))
    return 0;

  const char *Prefix = NULL;
  if (NEVERC_API_FN(API, PluginGetArg))
    Prefix = API->PluginGetArg("clone-prefix");
  if (!Prefix || !*Prefix)
    return 0;

  NevercArenaRef A = NEVERC_TRY_ARENA(API);
  if (!A)
    return 0;

  unsigned FnCount = 0;
  NevercValueRef *Fns =
      NEVERC_ARENA_COLLECT_DEFINED_FUNCTIONS(API, A, M, &FnCount);
  if (!Fns) {
    API->ArenaDestroy(A);
    return 0;
  }

  int Cloned = 0;
  for (unsigned I = 0; I < FnCount; I++) {
    const char *Name = API->ValueGetName(Fns[I]);
    if (!API->StrStartsWith(Name, Prefix))
      continue;
    char *CloneName = API->ArenaStrConcat(A, Name, "_clone");
    NevercValueRef Clone = API->FunctionClone(Fns[I], CloneName);
    if (Clone) {
      API->DiagNoteF(PLUGIN_TAG "Cloned '%s' -> '%s'", Name,
                     API->ValueGetName(Clone));
      Cloned++;
    }
  }

  API->ArenaDestroy(A);
  return Cloned > 0;
}

/*
 * Loop trip count analysis: report computable trip counts for all loops.
 *
 * Shows: FunctionBuildSCEV (on-demand ScalarEvolution), FunctionBuildLoopInfo
 *        (on-demand loop detection), SCEVGetTripCount / SCEVGetMaxTripCount
 *        (constant trip count queries), LoopIsLoopInvariant (invariant check),
 *        Arena (single bump-pointer allocator owns Fns + every per-iteration
 *        BBs array; one ArenaDestroy reclaims the lot),
 *        ArenaCollectBBs (single vtable call for BB array),
 *        analysis lifecycle management.
 */
static int loopTripCountPass(NevercModuleRef M, const NevercHostAPI *API,
                             void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, FunctionBuildSCEV) ||
      !NEVERC_API_FN(API, FunctionBuildLoopInfo))
    return 0;

  NevercArenaRef Scratch = NEVERC_TRY_ARENA(API);
  if (!Scratch)
    return 0;

  unsigned FnCount = 0;
  NevercValueRef *Fns =
      NEVERC_ARENA_COLLECT_DEFINED_FUNCTIONS(API, Scratch, M, &FnCount);
  if (!Fns) {
    API->ArenaDestroy(Scratch);
    return 0;
  }

  int HasInvariant = NEVERC_API_FN(API, LoopIsLoopInvariant) != 0;

  for (unsigned I = 0; I < FnCount; I++) {
    NevercLoopInfoRef LI = API->FunctionBuildLoopInfo(Fns[I]);
    if (!LI)
      continue;
    if (API->LoopInfoGetTopLevelLoopCount(LI) == 0) {
      API->LoopInfoDestroy(LI);
      continue;
    }

    NevercSCEVInfoRef SI = API->FunctionBuildSCEV(Fns[I]);
    if (!SI) {
      API->LoopInfoDestroy(LI);
      continue;
    }

    unsigned BBCount = 0;
    NevercBasicBlockRef *BBs =
        NEVERC_ARENA_COLLECT_BBS(API, Scratch, Fns[I], &BBCount);
    if (!BBs) {
      API->SCEVInfoDestroy(SI);
      API->LoopInfoDestroy(LI);
      continue;
    }

    for (unsigned B = 0; B < BBCount; B++) {
      NevercLoopRef L = API->LoopInfoGetLoopFor(LI, BBs[B]);
      if (!L || API->LoopGetHeader(L) != BBs[B])
        continue;

      unsigned TC = API->SCEVGetTripCount(SI, BBs[B]);
      unsigned MaxTC = API->SCEVGetMaxTripCount(SI, BBs[B]);
      unsigned InvariantArgs = 0;
      if (HasInvariant) {
        NevercValueRef Fn = API->BBGetParentFunction(BBs[B]);
        if (Fn) {
          unsigned ArgCount = API->FunctionGetArgCount(Fn);
          for (unsigned Arg = 0; Arg < ArgCount; Arg++) {
            if (API->LoopIsLoopInvariant(L, API->FunctionGetArg(Fn, Arg)))
              InvariantArgs++;
          }
        }
      }

      const char *HeaderName = API->BBGetName(BBs[B]);
      if (TC > 0)
        API->DiagNoteF(PLUGIN_TAG "Loop '%s' in '%s': trip=%u, "
                       "%u invariant args",
                       HeaderName, API->ValueGetName(Fns[I]), TC,
                       InvariantArgs);
      else if (MaxTC > 0)
        API->DiagNoteF(PLUGIN_TAG "Loop '%s' in '%s': max-trip=%u, "
                       "%u invariant args",
                       HeaderName, API->ValueGetName(Fns[I]), MaxTC,
                       InvariantArgs);
      else
        API->DiagNoteF(PLUGIN_TAG "Loop '%s' in '%s': trip=?, "
                       "%u invariant args",
                       HeaderName, API->ValueGetName(Fns[I]),
                       InvariantArgs);
    }

    API->SCEVInfoDestroy(SI);
    API->LoopInfoDestroy(LI);
  }

  API->ArenaDestroy(Scratch);
  return 0;
}

/*
 * Call graph analysis: report callee counts, detect recursive functions.
 *
 * Shows: NEVERC_AUTO_COLLECT_CALLEES (arena -> heap fallback),
 *        NEVERC_FREE_IF_HEAP (conditional cleanup),
 *        ModuleBuildCallGraph, CallGraphGetCalleeCount,
 *        CallGraphIsRecursive (SCC-based O(1) recursion detection),
 *        StrBuilder + NEVERC_STRBUILDER_DIAG (zero-alloc emission).
 */
static int callGraphAnalysisPass(NevercModuleRef M, const NevercHostAPI *API,
                                 void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, ModuleBuildCallGraph) ||
      !NEVERC_API_FN(API, StrBuilderCreate))
    return 0;

  NevercCallGraphRef CG = API->ModuleBuildCallGraph(M);
  if (!CG)
    return 0;

  NevercArenaRef Scratch = NEVERC_TRY_ARENA(API);
  if (!Scratch) {
    API->CallGraphDestroy(CG);
    return 0;
  }

  unsigned FnCount = 0;
  NevercValueRef *Fns =
      NEVERC_ARENA_COLLECT_DEFINED_FUNCTIONS(API, Scratch, M, &FnCount);
  if (!Fns) {
    API->ArenaDestroy(Scratch);
    API->CallGraphDestroy(CG);
    return 0;
  }

  NevercStrBuilderRef SB = API->StrBuilderCreate();
  if (!SB) {
    API->ArenaDestroy(Scratch);
    API->CallGraphDestroy(CG);
    return 0;
  }

  unsigned RecursiveCount = 0;
  for (unsigned I = 0; I < FnCount; I++) {
    int IsRec = API->CallGraphIsRecursive(CG, Fns[I]);
    if (IsRec)
      RecursiveCount++;

    unsigned CalleeCount = API->CallGraphGetCalleeCount(CG, Fns[I]);
    if (CalleeCount == 0 && !IsRec)
      continue;

    API->StrBuilderClear(SB);
    API->StrBuilderAppendF(SB, PLUGIN_TAG "CG '%s': %u callees",
                           API->ValueGetName(Fns[I]), CalleeCount);
    if (IsRec)
      API->StrBuilderAppend(SB, " [recursive]");

    if (CalleeCount > 0 && CalleeCount <= 8) {
      unsigned Collected = 0;
      NevercValueRef *Callees =
          NEVERC_AUTO_COLLECT_CALLEES(API, Scratch, CG, Fns[I], &Collected);
      if (Callees) {
        API->StrBuilderAppend(SB, " ->");
        for (unsigned C = 0; C < Collected; C++)
          API->StrBuilderAppendF(SB, " %s",
                                 API->ValueGetName(Callees[C]));
        NEVERC_FREE_IF_HEAP(API, Callees, Scratch);
      }
    }
    NEVERC_STRBUILDER_DIAG(API, SB, DiagNote);
  }

  API->StrBuilderDestroy(SB);
  API->ArenaDestroy(Scratch);
  API->CallGraphDestroy(CG);

  if (RecursiveCount > 0)
    API->DiagNoteF(PLUGIN_TAG "CG: %u recursive functions", RecursiveCount);
  return 0;
}

/*
 * Plugin command-line argument demo (-fplugin-pass-arg=key=value).
 *
 * Shows: PluginGetArgBool (typed bool: 1/0/true/false/yes/no/on/off),
 *        PluginGetArgInt64 (typed int with default), PluginHasArg
 *        + PluginGetArg (raw string), NEVERC_TRY_ARENA +
 *        NEVERC_AUTO_COLLECT_DEFINED_FUNCTIONS (arena -> heap fallback).
 */
static int pluginArgDemoPass(NevercModuleRef M, const NevercHostAPI *API,
                             void *UserData) {
  (void)UserData;
  if (!NEVERC_API_FN(API, PluginGetArg))
    return 0;

  API->DiagNoteF(PLUGIN_TAG "Plugin args: %u", API->PluginGetArgCount());

  int Verbose = NEVERC_API_FN(API, PluginGetArgBool)
                    ? API->PluginGetArgBool("verbose", 0)
                    : (API->PluginGetArg("verbose") != NULL);
  int64_t MaxFns = NEVERC_API_FN(API, PluginGetArgInt64)
                       ? API->PluginGetArgInt64("max-fns", -1)
                       : -1;

  if (Verbose) {
    NevercArenaRef Scratch = NEVERC_TRY_ARENA(API);
    unsigned FnCount = 0;
    NevercValueRef *Fns =
        NEVERC_AUTO_COLLECT_DEFINED_FUNCTIONS(API, Scratch, M, &FnCount);
    if (Fns) {
      unsigned Limit = MaxFns >= 0 && (uint64_t)MaxFns < FnCount
                           ? (unsigned)MaxFns
                           : FnCount;
      for (unsigned I = 0; I < Limit; I++)
        API->DiagNoteF(PLUGIN_TAG "  fn: %s", API->ValueGetName(Fns[I]));
    }
    NEVERC_FREE_IF_HEAP(API, Fns, Scratch);
    NEVERC_ARENA_DESTROY(API, Scratch);
  }

  if (API->PluginHasArg("prefix")) {
    const char *Prefix = API->PluginGetArg("prefix");
    API->DiagNoteF(PLUGIN_TAG "Custom prefix: %s",
                   NEVERC_STR_OR(Prefix, "(empty)"));
  }
  return 0;
}

/* ======================================================================== */
/*  MIR Pass                                                                */
/* ======================================================================== */

/*
 * MIR analysis: count instructions and classify operands.
 *
 * Three-tier fallback for maximum performance on every host version:
 *   1. ArenaCollectMBBs + MBBCollectInstructions + MInstCollectOperandKinds
 *      (batch everything: contiguous arrays, cache-friendly linear scan)
 *   2. MFuncForEachBB + MBBForEachInst (zero-alloc callback iteration --
 *      one vtable call per MBB/MI vs two for the linked-list macros)
 *   3. NEVERC_FOR_EACH_MBB + NEVERC_FOR_EACH_MI (oldest hosts)
 *
 * Shows: MFuncForEachBB / MBBForEachInst (MIR zero-alloc callback
 *        iteration), ArenaCollectMBBs (batch), MInstCollectOperandKinds
 *        (single vtable call per MI), stack buffer fast path.
 */

struct MirOpCountCtx {
  const NevercHostAPI *API;
  unsigned InstrCount;
  unsigned RegOps;
  unsigned ImmOps;
  int HasBatchKinds;
};

static void mirClassifyOps(NevercMachineInstrRef MI,
                           struct MirOpCountCtx *C) {
  unsigned NumOps = C->API->MInstGetNumOperands(MI);
  if (NumOps == 0)
    return;
  if (C->HasBatchKinds) {
    uint8_t StackKinds[128];
    uint8_t *KindsBuf = StackKinds;
    int HeapAlloced = 0;
    if (NumOps > sizeof(StackKinds)) {
      KindsBuf = (uint8_t *)C->API->Alloc(NumOps);
      HeapAlloced = KindsBuf != NULL;
    }
    if (KindsBuf) {
      C->API->MInstCollectOperandKinds(MI, KindsBuf);
      for (unsigned K = 0; K < NumOps; K++) {
        if (KindsBuf[K] == NEVERC_MIR_OP_REG)
          C->RegOps++;
        else if (KindsBuf[K] == NEVERC_MIR_OP_IMM)
          C->ImmOps++;
      }
      if (HeapAlloced)
        C->API->Free(KindsBuf);
      return;
    }
  }
  for (unsigned K = 0; K < NumOps; K++) {
    if (C->API->MInstGetOperandIsReg(MI, K))
      C->RegOps++;
    else if (C->API->MInstGetOperandIsImm(MI, K))
      C->ImmOps++;
  }
}

static int mirInstVisitor(NevercMachineInstrRef MI, void *Ctx) {
  struct MirOpCountCtx *C = (struct MirOpCountCtx *)Ctx;
  C->InstrCount++;
  mirClassifyOps(MI, C);
  return 0;
}

static int mirBBVisitor(NevercMachineBBRef MBB, void *Ctx) {
  struct MirOpCountCtx *C = (struct MirOpCountCtx *)Ctx;
  C->API->MBBForEachInst(MBB, mirInstVisitor, Ctx);
  return 0;
}

static int mirAnalysisPass(NevercMachineFuncRef MF, const NevercHostAPI *API,
                           void *UserData) {
  (void)UserData;
  const char *FnName = API->MFuncGetName(MF);

  unsigned BBCount = API->MFuncGetBBCount(MF);
  if (BBCount == 0) {
    API->DiagNoteF(PLUGIN_TAG "MIR '%s': empty", FnName);
    return 0;
  }

  struct MirOpCountCtx Ctx;
  Ctx.API = API;
  Ctx.InstrCount = 0;
  Ctx.RegOps = 0;
  Ctx.ImmOps = 0;
  Ctx.HasBatchKinds = NEVERC_API_FN(API, MInstCollectOperandKinds) != 0;

  NevercArenaRef Scratch = NEVERC_TRY_ARENA(API);
  if (Scratch && NEVERC_API_FN(API, ArenaCollectMBBs)) {
    /* Tier 1: batch collect + batch operand kinds. */
    unsigned MBBCount = 0;
    NevercMachineBBRef *MBBs =
        NEVERC_ARENA_COLLECT_MBBS(API, Scratch, MF, &MBBCount);
    if (!MBBs) {
      API->ArenaDestroy(Scratch);
      return 0;
    }

    unsigned MaxIC = 0;
    for (unsigned B = 0; B < MBBCount; B++) {
      unsigned IC = API->MBBGetInstCount(MBBs[B]);
      Ctx.InstrCount += IC;
      if (IC > MaxIC)
        MaxIC = IC;
    }

    NevercMachineInstrRef *MIs = MaxIC > 0
        ? NEVERC_ARENA_ALLOC_ARRAY(API, Scratch, NevercMachineInstrRef, MaxIC)
        : NULL;

    for (unsigned B = 0; B < MBBCount && MIs; B++) {
      unsigned IC = API->MBBGetInstCount(MBBs[B]);
      if (IC == 0)
        continue;
      API->MBBCollectInstructions(MBBs[B], MIs);
      for (unsigned J = 0; J < IC; J++)
        mirClassifyOps(MIs[J], &Ctx);
    }
    API->ArenaDestroy(Scratch);
  } else if (NEVERC_API_FN(API, MFuncForEachBB) &&
             NEVERC_API_FN(API, MBBForEachInst)) {
    /* Tier 2: zero-alloc callback iteration. */
    NEVERC_ARENA_DESTROY(API, Scratch);
    API->MFuncForEachBB(MF, mirBBVisitor, &Ctx);
  } else {
    /* Tier 3: per-element linked-list iteration. */
    NEVERC_ARENA_DESTROY(API, Scratch);
    NEVERC_FOR_EACH_MBB(API, MF, MBB) {
      NEVERC_FOR_EACH_MI(API, MBB, MI) {
        Ctx.InstrCount++;
        mirClassifyOps(MI, &Ctx);
      }
    }
  }

  API->DiagNoteF(PLUGIN_TAG "MIR '%s': %u BBs, %u instrs, %u reg, %u imm",
                 FnName, BBCount, Ctx.InstrCount, Ctx.RegOps, Ctx.ImmOps);
  return 0;
}

/* ======================================================================== */
/*  Binary Passes                                                           */
/* ======================================================================== */

/*
 * Report extracted shellcode size and scan for stray int3 (0xCC) bytes,
 * a common debug-build leftover that has no business living in finalized
 * shellcode.  Demonstrates:
 *   - MemFindByte: single-byte search returning an offset (not a pointer)
 *     so the result stays valid across BinaryResize relocation.  Simpler
 *     than MemFind when the needle is a single byte.
 *   - MemCount: SIMD-fast occurrence count, paired with MemFindByte so the
 *     pass reports BOTH the first offending offset AND the total tally.
 */
static int binaryInfoPass(uint8_t **Data, uint64_t *Len, uint64_t *Capacity,
                          const NevercHostAPI *API, void *UserData) {
  (void)Capacity;
  (void)UserData;
  API->DiagNoteF(PLUGIN_TAG "Binary: %" PRIu64 " bytes", *Len);

  if (NEVERC_API_FN(API, MemFindByte) && NEVERC_API_FN(API, MemCount)) {
    uint64_t Off = API->MemFindByte(*Data, *Len, 0xCC);
    if (Off != NEVERC_NPOS) {
      uint64_t Total = API->MemCount(*Data, *Len, 0xCC);
      API->DiagWarningF(PLUGIN_TAG
                        "Stray 0xCC: %" PRIu64 " total, first at offset "
                        "%" PRIu64,
                        Total, Off);
    }
  }
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

/* LTO pipeline tracker -- reports defined function count.
 *
 * Shows: ModuleGetDefinedFunctionCount (zero-allocation census),
 *        NEVERC_HOOK_NAME (resolve hook enum from UserData),
 *        NEVERC_FOR_EACH_DEFINED_FUNCTION (zero-alloc fallback).
 */
static int ltoInfoPass(NevercModuleRef M, const NevercHostAPI *API,
                       void *UserData) {
  const char *Stage = NEVERC_HOOK_NAME(API, UserData);
  unsigned Defined;
  if (NEVERC_API_FN(API, ModuleGetDefinedFunctionCount)) {
    Defined = API->ModuleGetDefinedFunctionCount(M);
  } else {
    Defined = 0;
    NEVERC_FOR_EACH_DEFINED_FUNCTION(API, M, F) {
      (void)F;
      ++Defined;
    }
  }
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

  NEVERC_FOR_EACH_SYMBOL(API, Sym) {
    if (API->LinkSymbolIsDefined(Sym))
      Defined++;
    else
      Undefined++;
  }

  unsigned Sections = 0;
  NEVERC_FOR_EACH_SECTION(API, Sec) {
    Sections++;
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
  API->DiagNoteF(PLUGIN_TAG "Stage: %s (MF=%s)",
                 NEVERC_HOOK_NAME(API, UserData),
                 NEVERC_STR_OR(FnName, "?"));
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
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT, sourceInfoPass,
                          NULL, "example-source-info");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          callSiteAnalysisPass, NULL,
                          "example-call-sites");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          uniqueCallTargetsPass, NULL,
                          "example-unique-targets");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          cfgAnalysisPass, NULL,
                          "example-cfg-analysis");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          arenaCollectAnalysisPass, NULL,
                          "example-arena-collect");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          cloneDemoPass, NULL,
                          "example-clone");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          loopTripCountPass, NULL,
                          "example-loop-trip-count");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          callGraphAnalysisPass, NULL,
                          "example-call-graph");
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
