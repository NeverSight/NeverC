/*
 * ExamplePlugin.c — Pure C neverc out-of-tree pass plugin.
 *
 * Demonstrates:
 *   - Module-level passes at PRE_OPT and POST_OPT hooks (normal flow)
 *   - IR mutation: inserting function-entry instrumentation via IRBuilder
 *   - MIR-level pass at SC_BEFORE_PREEMIT hook (shellcode flow)
 *   - Binary-level pass at SC_POST_EXTRACT hook (shellcode flow)
 *   - Using the NevercHostAPI vtable to iterate IR/MIR and log diagnostics
 *   - String/memory utility API (StrFormat, StrFormatV, StrConcat, etc.)
 *   - Zero LLVM C++ or CRT dependencies — everything goes through the vtable
 *
 * Build:
 *   cc -shared -o ExamplePlugin.dll ExamplePlugin.c -I<neverc>/include
 *
 * Usage:
 *   neverc -fplugin-pass=./ExamplePlugin.dll input.c -o output.obj
 */

#include "neverc/Plugin/NevercPluginAPI.h"

/* ---- Diagnostic helpers using the host string API ----
   These wrap StrFormatV (the va_list variant) to provide a clean one-liner
   for formatting + emitting + freeing diagnostic messages.  This is the
   canonical pattern for plugin code — no stack buffers, no manual
   concatenation, all memory routed through the host allocator.           */

static void diagNote(const NevercHostAPI *API, const char *Fmt, ...) {
  va_list Args;
  va_start(Args, Fmt);
  char *Msg = API->StrFormatV(Fmt, Args);
  va_end(Args);
  if (Msg) {
    API->DiagNote(Msg);
    API->Free(Msg);
  }
}

static void diagWarning(const NevercHostAPI *API, const char *Fmt, ...) {
  va_list Args;
  va_start(Args, Fmt);
  char *Msg = API->StrFormatV(Fmt, Args);
  va_end(Args);
  if (Msg) {
    API->DiagWarning(Msg);
    API->Free(Msg);
  }
}

/* ---- Module pass: print module info and count defined functions ---- */
static int functionCounterPass(NevercModuleRef M,
                               const NevercHostAPI *API,
                               void *UserData) {
  (void)UserData;

  const char *Triple = API->ModuleGetTargetTriple(M);
  diagNote(API, "[example-plugin] Target: %s", Triple);

  int Count = 0;
  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    if (!API->FunctionIsDeclaration(F))
      Count++;
    F = API->ModuleGetNextFunction(F);
  }

  diagNote(API, "[example-plugin] Found %d defined functions", Count);
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
      diagNote(API, "[example-plugin] Processing function: %s",
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

  if (NEVERC_API_HAS(API, HostIsShellcodeMode) &&
      API->HostIsShellcodeMode()) {
    diagNote(API,
             "[example-plugin] Shellcode mode active; skipping trace "
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
    diagNote(API, "[example-plugin] Inserted function-entry tracing calls");
  return Modified;
}

/* ---- MIR pass: detailed MachineFunction analysis ---- */
static int mirInstrCountPass(NevercMachineFuncRef MF,
                             const NevercHostAPI *API,
                             void *UserData) {
  (void)UserData;
  const char *FnName = API->MFuncGetName(MF);
  int InstrCount = 0;
  int BBCount = 0;
  int RegOpCount = 0;
  int ImmOpCount = 0;

  NevercMachineBBRef MBB = API->MFuncGetFirstBB(MF);
  while (MBB) {
    BBCount++;

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

  diagNote(API,
           "[example-plugin] MIR '%s': %d BBs, %d instrs, %d reg ops, "
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

  diagNote(API, "[example-plugin] Binary: %llu bytes",
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
    diagWarning(API, "[example-plugin] BinaryResize failed");
    return 0;
  }

  API->MemSet(*Data + OldLen, 0x90, 16);

  diagNote(API,
           "[example-plugin] Appended 16-byte NOP sled (%llu -> %llu bytes)",
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

  diagNote(API,
           "[example-plugin] GEP index analysis: %d GEPs, %d const indices, "
           "%d dynamic indices",
           TotalGEPs, ConstIdxCount, DynIdxCount);
  return 0;
}

/*
 * Module pass: demonstrate reverse global iteration.
 *
 * Uses ModuleGetLastGlobal / ModuleGetPrevGlobal to iterate globals
 * in reverse order and count those with initializers.
 */
static int reverseGlobalWalkPass(NevercModuleRef M,
                                  const NevercHostAPI *API,
                                  void *UserData) {
  (void)UserData;
  if (!NEVERC_API_HAS(API, ModuleGetPrevGlobal))
    return 0;

  int Total = 0, WithInit = 0;
  NevercValueRef G = API->ModuleGetLastGlobal(M);
  while (G) {
    Total++;
    if (API->GlobalHasInitializer(G))
      WithInit++;
    G = API->ModuleGetPrevGlobal(G);
  }

  diagNote(API,
           "[example-plugin] ReverseGlobals: %d total, %d with initializer",
           Total, WithInit);
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

  diagNote(API, "[example-plugin] ValueKind: %d functions, %d globals",
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
    diagNote(API, "[example-plugin] Created struct with %u fields",
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

  diagNote(API, "[example-plugin] Module stats: %u functions, %u globals",
           FnCount, GvCount);

  NevercValueRef Main = API->ModuleGetNamedGlobal(M, "main_var");
  if (Main)
    diagNote(API, "[example-plugin] Found global 'main_var'");

  return 0;
}

/* ---- Module pass: instruction type analysis using the kind queries ---- */
static int instrTypeDemoPass(NevercModuleRef M,
                             const NevercHostAPI *API,
                             void *UserData) {
  (void)UserData;

  if (!NEVERC_API_HAS(API, InstIsCall))
    return 0;

  int Calls = 0, Branches = 0, Loads = 0, Stores = 0;
  int Allocas = 0, PHIs = 0, GEPs = 0;

  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    if (!API->FunctionIsDeclaration(F)) {
      NevercBasicBlockRef BB = API->FunctionGetFirstBB(F);
      while (BB) {
        NevercValueRef I = API->BBGetFirstInst(BB);
        while (I) {
          if (API->InstIsCall(I))
            Calls++;
          if (API->InstIsBranch(I))
            Branches++;
          if (API->InstIsLoad(I))
            Loads++;
          if (API->InstIsStore(I))
            Stores++;
          if (API->InstIsAlloca(I))
            Allocas++;
          if (API->InstIsPHI(I))
            PHIs++;
          if (API->InstIsGEP(I))
            GEPs++;
          I = API->BBGetNextInst(I);
        }
        BB = API->FunctionGetNextBB(BB);
      }
    }
    F = API->ModuleGetNextFunction(F);
  }

  diagNote(API,
           "[example-plugin] Instr stats: %d calls, %d branches, %d loads, "
           "%d stores, %d allocas, %d phis, %d geps",
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

  diagNote(API,
           "[example-plugin] Cmp analysis: %d eq, %d relational, %d fcmp",
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

  diagNote(API,
           "[example-plugin] CFG: %d edges, %d conditional terminators",
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

  diagNote(API, "[example-plugin] Globals: %d internal, %d external",
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

  NevercValueRef ToRemove[64];
  int RemoveCount = 0;

  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    NevercValueRef Next = API->ModuleGetNextFunction(F);
    if (!API->FunctionIsDeclaration(F)) {
      unsigned Linkage = API->FunctionGetLinkage(F);
      if (Linkage == NEVERC_LINKAGE_INTERNAL ||
          Linkage == NEVERC_LINKAGE_PRIVATE) {
        unsigned NumUses = API->ValueGetNumUses(F);
        if (NumUses == 0 && RemoveCount < 64)
          ToRemove[RemoveCount++] = F;
      }
    }
    F = Next;
  }

  int I;
  for (I = 0; I < RemoveCount; I++) {
    diagNote(API,
             "[example-plugin] Removing dead internal function: %s",
             API->ValueGetName(ToRemove[I]));
    API->ModuleRemoveFunction(M, ToRemove[I]);
  }

  if (RemoveCount > 0)
    diagNote(API, "[example-plugin] Removed %d dead internal functions",
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

  diagNote(API,
           "[example-plugin] Switch: %d switches, %d total cases",
           SwitchCount, TotalCases);
  return 0;
}

/*
 * Module pass: demonstrate reverse iteration and BB count.
 *
 * Uses FunctionGetBBCount, FunctionGetPrevBB, InstGetPrevInst, BBGetName,
 * and ModuleGetSourceFileName to do a reverse walk of the IR.
 */
static int reverseWalkDemoPass(NevercModuleRef M,
                               const NevercHostAPI *API,
                               void *UserData) {
  (void)UserData;
  if (!NEVERC_API_HAS(API, FunctionGetBBCount))
    return 0;

  diagNote(API, "[example-plugin] Source: %s",
           API->ModuleGetSourceFileName(M));

  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    if (!API->FunctionIsDeclaration(F)) {
      unsigned BBCount = API->FunctionGetBBCount(F);
      int TermCount = 0;
      NevercBasicBlockRef BB = API->FunctionGetLastBB(F);
      while (BB) {
        NevercValueRef I = API->BBGetLastInst(BB);
        while (I) {
          if (API->InstIsTerminator(I))
            TermCount++;
          I = API->InstGetPrevInst(I);
        }
        BB = API->FunctionGetPrevBB(BB);
      }

      diagNote(API,
               "[example-plugin] RevWalk '%s': %u BBs, %d terminators",
               API->ValueGetName(F), BBCount, TermCount);
    }
    F = API->ModuleGetNextFunction(F);
  }
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

  diagNote(API,
           "[example-plugin] CallLike: %d direct, %d indirect, %d invokes",
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

  diagNote(API,
           "[example-plugin] MemAccess: loads=%dptr/%dint/%dother, "
           "GEPs=%dinbounds/%dregular",
           PtrLoads, IntLoads, OtherLoads, InboundsGEPs, RegularGEPs);
  return 0;
}

/*
 * Module pass: run at PIPELINE_START (inside PassBuilder pipeline).
 *
 * Counts the number of functions and basic blocks before optimization.
 */
static int pipelineStartPass(NevercModuleRef M,
                              const NevercHostAPI *API,
                              void *UserData) {
  (void)UserData;
  int FnCount = 0, BBTotal = 0;
  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    if (!API->FunctionIsDeclaration(F)) {
      FnCount++;
      NevercBasicBlockRef BB = API->FunctionGetFirstBB(F);
      while (BB) {
        BBTotal++;
        BB = API->FunctionGetNextBB(BB);
      }
    }
    F = API->ModuleGetNextFunction(F);
  }
  diagNote(API,
           "[example-plugin] PipelineStart: %d functions, %d BBs before opt",
           FnCount, BBTotal);
  return 0;
}

/*
 * Module pass: run at PIPELINE_LAST (OptimizerLast EP).
 *
 * Counts the number of functions and basic blocks after optimization,
 * allowing comparison with the PipelineStart pass.
 */
static int pipelineLastPass(NevercModuleRef M,
                             const NevercHostAPI *API,
                             void *UserData) {
  (void)UserData;
  int FnCount = 0, BBTotal = 0;
  NevercValueRef F = API->ModuleGetFirstFunction(M);
  while (F) {
    if (!API->FunctionIsDeclaration(F)) {
      FnCount++;
      NevercBasicBlockRef BB = API->FunctionGetFirstBB(F);
      while (BB) {
        BBTotal++;
        BB = API->FunctionGetNextBB(BB);
      }
    }
    F = API->ModuleGetNextFunction(F);
  }
  diagNote(API,
           "[example-plugin] PipelineLast: %d functions, %d BBs after opt",
           FnCount, BBTotal);
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

  diagNote(API,
           "[example-plugin] DebugLoc: %d with, %d without, "
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
            diagNote(API,
                     "[example-plugin] Entry terminator of '%s': %s",
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
 * Registered at every shellcode hook we don't otherwise demonstrate, so
 * the user can verify all pipeline insertion points are wired correctly.
 *
 * UserData carries a pointer to a stable static string identifying the stage.
 */
static int stageTrackerModulePass(NevercModuleRef M,
                                  const NevercHostAPI *API,
                                  void *UserData) {
  (void)M;
  const char *Stage = (const char *)UserData;
  diagNote(API, "[example-plugin] Stage: %s", Stage ? Stage : "<unknown>");
  return 0;
}

static int stageTrackerMachinePass(NevercMachineFuncRef MF,
                                   const NevercHostAPI *API,
                                   void *UserData) {
  const char *Stage = (const char *)UserData;
  const char *FnName = API->MFuncGetName(MF);
  diagNote(API, "[example-plugin] Stage: %s (MF=%s)",
           Stage ? Stage : "<unknown>", FnName ? FnName : "?");
  return 0;
}

static int stageTrackerBinaryPass(uint8_t **Data, uint64_t *Len,
                                  uint64_t *Capacity,
                                  const NevercHostAPI *API,
                                  void *UserData) {
  (void)Data;
  (void)Capacity;
  const char *Stage = (const char *)UserData;
  diagNote(API, "[example-plugin] Stage: %s (binary=%llu bytes)",
           Stage ? Stage : "<unknown>", (unsigned long long)*Len);
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
      diagNote(API, "[example-plugin] Intrinsic '%s' id=%u%s",
               Name, TrapID,
               API->IntrinsicIsOverloaded(TrapID) ? " (overloaded)"
                                                  : " (not overloaded)");
      API->Free(Name);
    }
  }

  if (NEVERC_API_HAS(API, ModuleGetNamedMetadata)) {
    NevercNamedMDRef Ident = API->ModuleGetNamedMetadata(M, "llvm.ident");
    if (Ident)
      diagNote(API, "[example-plugin] llvm.ident has %u operand(s)",
               API->NamedMDGetNumOperands(Ident));
    else
      diagNote(API, "[example-plugin] No llvm.ident metadata found");
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

  diagNote(API, "[example-plugin] Plugin arg count: %u",
           API->PluginGetArgCount());

  const char *Verbose = API->PluginGetArg("verbose");
  if (Verbose && Verbose[0] == '1') {
    NevercValueRef F = API->ModuleGetFirstFunction(M);
    while (F) {
      if (!API->FunctionIsDeclaration(F))
        diagNote(API, "[example-plugin][verbose] Function: %s",
                 API->ValueGetName(F));
      F = API->ModuleGetNextFunction(F);
    }
  }

  if (API->PluginHasArg("prefix")) {
    const char *Prefix = API->PluginGetArg("prefix");
    diagNote(API, "[example-plugin] Using custom prefix: %s",
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
  diagNote(API, "[example-plugin] Pointer size: %u bits", PtrBits);

  NevercTypeRef I32Ty = API->TypeGetInt32(C);
  NevercTypeRef I64Ty = API->TypeGetInt64(C);
  NevercTypeRef DblTy = API->TypeGetDouble(C);

  uint64_t I32Size = API->TypeAllocSize(M, I32Ty);
  uint64_t I64Size = API->TypeAllocSize(M, I64Ty);
  unsigned I32Align = API->TypeABIAlignment(M, I32Ty);
  unsigned DblAlign = API->TypeABIAlignment(M, DblTy);

  diagNote(API,
           "[example-plugin] i32: %llu bytes, align %u | i64: %llu bytes "
           "| double align %u",
           (unsigned long long)I32Size, I32Align,
           (unsigned long long)I64Size, DblAlign);

  if (NEVERC_API_HAS(API, ConstPoison)) {
    NevercValueRef Poison = API->ConstPoison(I32Ty);
    diagNote(API,
             "[example-plugin] PoisonValue(i32): isPoison=%d isUndef=%d",
             API->ValueIsPoison(Poison), API->ValueIsUndef(Poison));
  }

  if (NEVERC_API_HAS(API, TypeGetFixedVector)) {
    NevercTypeRef V4I32 = API->TypeGetFixedVector(I32Ty, 4);
    diagNote(API,
             "[example-plugin] <4 x i32>: isVector=%d numElems=%u "
             "sizeBits=%llu",
             API->TypeIsVector(V4I32),
             API->TypeGetVectorNumElements(V4I32),
             (unsigned long long)API->TypeSizeInBits(M, V4I32));
  }

  if (NEVERC_API_HAS(API, TypeGetPtrInAddrSpace)) {
    NevercTypeRef DefaultPtr = API->TypeGetPtr(C);
    NevercTypeRef AS1Ptr = API->TypeGetPtrInAddrSpace(C, 1);
    diagNote(API,
             "[example-plugin] ptr addrspace(0)=%u ptr addrspace(1)=%u",
             API->TypeGetPointerAddrSpace(DefaultPtr),
             API->TypeGetPointerAddrSpace(AS1Ptr));
  }

  return 0;
}

/*
 * Module pass: demonstrate reverse function & alias iteration.
 *
 * Uses ModuleGetPrevFunction (complement to ModuleGetLastFunction) to
 * walk the function list backwards, and ModuleGetLastAlias /
 * ModuleGetPrevAlias for reverse alias traversal.
 */
static int reverseIterDemoPass(NevercModuleRef M,
                               const NevercHostAPI *API,
                               void *UserData) {
  (void)UserData;

  int RevFnCount = 0;
  if (NEVERC_API_HAS(API, ModuleGetPrevFunction)) {
    NevercValueRef F = API->ModuleGetLastFunction(M);
    while (F) {
      if (!API->FunctionIsDeclaration(F))
        RevFnCount++;
      F = API->ModuleGetPrevFunction(F);
    }
  }

  int AliasCount = 0;
  if (NEVERC_API_HAS(API, ModuleGetPrevAlias)) {
    NevercValueRef A = API->ModuleGetLastAlias(M);
    while (A) {
      AliasCount++;
      A = API->ModuleGetPrevAlias(A);
    }
  }

  diagNote(API,
           "[example-plugin] ReverseIter: %d defined fns (rev), "
           "%d aliases (rev)",
           RevFnCount, AliasCount);
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

  diagNote(API,
           "[example-plugin] Globals: %d with COMDAT, %d thread-local",
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

  diagNote(API,
           "[example-plugin] %d globals with aggregate init, "
           "%d noinline functions",
           AggrGlobals, NoInlineFns);
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
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          reverseWalkDemoPass, NULL,
                          "example-reverse-walk");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_POST_OPT,
                          callLikeDemoPass, NULL,
                          "example-call-like");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_POST_OPT,
                          memAccessDemoPass, NULL,
                          "example-mem-access");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PIPELINE_START,
                          pipelineStartPass, NULL,
                          "example-pipeline-start");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PIPELINE_LAST,
                          pipelineLastPass, NULL,
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
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          reverseGlobalWalkPass, NULL,
                          "example-reverse-globals");
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
                          reverseIterDemoPass, NULL,
                          "example-reverse-iter");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          nswAndComdatDemoPass, NULL,
                          "example-nsw-comdat");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT,
                          constAggrAndAttrDemoPass, NULL,
                          "example-const-aggr-attr");

  /* Stage trackers — verify every hook point is wired correctly.
     Using stable string-literal pointers so UserData stays valid for the
     lifetime of the host process. */

  /* Normal flow — MIR hooks */
  static const char kStageBeforeCodegenPreEmit[] = "BEFORE_CODEGEN_PREEMIT";
  static const char kStageAfterCodegenFinalMIR[] = "AFTER_CODEGEN_FINAL_MIR";

  API->RegisterMachinePass(Registrar, NEVERC_HOOK_BEFORE_CODEGEN_PREEMIT,
                           stageTrackerMachinePass,
                           (void *)kStageBeforeCodegenPreEmit,
                           "example-stage-before-codegen-preemit");
  API->RegisterMachinePass(Registrar, NEVERC_HOOK_AFTER_CODEGEN_FINAL_MIR,
                           stageTrackerMachinePass,
                           (void *)kStageAfterCodegenFinalMIR,
                           "example-stage-after-codegen-final-mir");

  /* Shellcode flow — IR hooks */
  static const char kStageBeforePrep[]     = "SC_BEFORE_PREP";
  static const char kStageAfterPrep[]      = "SC_AFTER_PREP";
  static const char kStageBeforeInlining[] = "SC_BEFORE_INLINING";
  static const char kStageAfterInlining[]  = "SC_AFTER_INLINING";
  static const char kStageAfterStackify[]  = "SC_AFTER_STACKIFY";
  static const char kStageAfterFinalIR[]   = "SC_AFTER_FINAL_IR";

  API->RegisterModulePass(Registrar, NEVERC_HOOK_SC_BEFORE_PREP,
                          stageTrackerModulePass, (void *)kStageBeforePrep,
                          "example-stage-sc-before-prep");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_SC_AFTER_PREP,
                          stageTrackerModulePass, (void *)kStageAfterPrep,
                          "example-stage-sc-after-prep");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_SC_BEFORE_INLINING,
                          stageTrackerModulePass, (void *)kStageBeforeInlining,
                          "example-stage-sc-before-inlining");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_SC_AFTER_INLINING,
                          stageTrackerModulePass, (void *)kStageAfterInlining,
                          "example-stage-sc-after-inlining");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_SC_AFTER_STACKIFY,
                          stageTrackerModulePass, (void *)kStageAfterStackify,
                          "example-stage-sc-after-stackify");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_SC_AFTER_FINAL_IR,
                          stageTrackerModulePass, (void *)kStageAfterFinalIR,
                          "example-stage-sc-after-final-ir");

  /* Shellcode flow — MIR hooks */
  static const char kStageBeforePreEmit[]  = "SC_BEFORE_PREEMIT";
  static const char kStageAfterPreEmit[]   = "SC_AFTER_PREEMIT";
  static const char kStageAfterFinalMIR[]  = "SC_AFTER_FINAL_MIR";

  API->RegisterMachinePass(Registrar, NEVERC_HOOK_SC_BEFORE_PREEMIT,
                           stageTrackerMachinePass,
                           (void *)kStageBeforePreEmit,
                           "example-stage-sc-before-preemit");
  API->RegisterMachinePass(Registrar, NEVERC_HOOK_SC_AFTER_PREEMIT,
                           stageTrackerMachinePass, (void *)kStageAfterPreEmit,
                           "example-stage-sc-after-preemit");
  API->RegisterMachinePass(Registrar, NEVERC_HOOK_SC_AFTER_FINAL_MIR,
                           stageTrackerMachinePass, (void *)kStageAfterFinalMIR,
                           "example-stage-sc-after-final-mir");

  /* Shellcode flow — binary hooks */
  static const char kStagePostExtract[]    = "SC_POST_EXTRACT";
  static const char kStagePostFinalize[]   = "SC_POST_FINALIZE";

  API->RegisterBinaryPass(Registrar, NEVERC_HOOK_SC_POST_EXTRACT,
                          stageTrackerBinaryPass, (void *)kStagePostExtract,
                          "example-stage-sc-post-extract");
  API->RegisterBinaryPass(Registrar, NEVERC_HOOK_SC_POST_FINALIZE,
                          stageTrackerBinaryPass, (void *)kStagePostFinalize,
                          "example-stage-sc-post-finalize");
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
