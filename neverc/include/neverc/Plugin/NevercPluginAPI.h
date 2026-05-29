/*===-- NevercPluginAPI.h - Out-of-tree plugin C ABI header --------*- C -*-===*\
|*                                                                            *|
|* Pure C public header for neverc out-of-tree pass plugins.                  *|
|* This is the ONLY file a plugin needs to compile against.                   *|
|*                                                                            *|
|* All IR/MIR/Binary manipulation goes through the NevercHostAPI vtable       *|
|* provided by the host process -- no direct CRT or LLVM C++ calls needed.   *|
|*                                                                            *|
|* LIFETIME RULES:                                                            *|
|*   - All opaque handles (NevercModuleRef, NevercValueRef, etc.) are valid   *|
|*     only within the scope of the pass callback that received them.         *|
|*   - const char* returned by ValueGetName, ModuleGetTargetTriple, and       *|
|*     ModuleGetDataLayout point to internal storage; valid as long as the    *|
|*     owning Module/Value exists (i.e., within the pass callback).           *|
|*   - NevercBuilderRef created by BuilderCreate must be freed by calling     *|
|*     BuilderDispose before the pass returns.                                *|
|*   - Memory obtained via Alloc/Realloc/MemDup/AllocZeroed/ReallocArray      *|
|*     must be freed via Free.                                                *|
|*   - Strings returned by StrDup/StrNDup/StrConcat/StrSubstring/StrReplace/ *|
|*     StrFormat/IntToStr/ValuePrintToString/etc. are host-allocated;        *|
|*     caller frees via Free.  DiagNoteF/DiagWarningF/DiagErrorF do NOT      *|
|*     require Free.  HookPointGetName returns a static string.              *|
|*   - NevercDynArrayRef / NevercStrMapRef / NevercIntMapRef /               *|
|*     NevercStrBuilderRef created by DynArrayCreate / StrMapCreate /       *|
|*     IntMapCreate / StrBuilderCreate must be freed via the corresponding  *|
|*     Destroy function before the pass returns.  StrBuilderFinish returns  *|
|*     a host-allocated string; caller frees via Free.                      *|
|*   - Do NOT call RegisterModulePass/MachinePass/BinaryPass outside of      *|
|*     the RegisterPasses callback -- they are no-ops after registration.     *|
|*   - Before using fields added in later versions, use NEVERC_API_FN or     *|
|*     NEVERC_API_HAS (layout only) before calling optional vtable entries.  *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef NEVERC_PLUGIN_API_H
#define NEVERC_PLUGIN_API_H

#include <inttypes.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#define NEVERC_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define NEVERC_EXPORT __attribute__((visibility("default")))
#else
#define NEVERC_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/*  Version                                                                   */
/* -------------------------------------------------------------------------- */

#define NEVERC_PLUGIN_API_VERSION 1

/* Check at runtime whether the host vtable includes a specific field.
 * Usage: if (NEVERC_API_HAS(API, BuildSwitch)) { API->BuildSwitch(...); }  */
#define NEVERC_API_HAS(api, field) \
    ((api)->StructSize >= offsetof(NevercHostAPI, field) + sizeof((api)->field))

/* Same as NEVERC_API_HAS but also requires a non-NULL function pointer.
 * Use for optional vtable entries that may be absent on older hosts. */
#define NEVERC_API_FN(api, field) \
    (NEVERC_API_HAS(api, field) && (api)->field)

/* Sentinel value returned by all "find" APIs that yield a uint64_t offset
 * (StrFindChar, StrFindLastChar, StrFindStr, MemFind, ...).  Use this
 * macro instead of (uint64_t)-1 to make intent explicit at call sites. */
#define NEVERC_NPOS ((uint64_t)-1)

/* ---- Convenience allocation macros ----
 * Typed array allocation through the host vtable -- eliminates the
 * repetitive (Type*)API->Alloc(Count * sizeof(Type)) pattern and
 * guards against count*size overflow via ReallocArray/AllocZeroed.
 *
 * NEVERC_ALLOC_ARRAY  -- uninitialized (fast, for immediately-filled arrays)
 * NEVERC_CALLOC_ARRAY -- zero-initialized (for arrays that need a clean slate)
 * NEVERC_REALLOC_ARRAY -- grow/shrink with overflow check                   */
#define NEVERC_ALLOC_ARRAY(api, type, count) \
    ((type *)(api)->ReallocArray(NULL, (count), sizeof(type)))

#define NEVERC_CALLOC_ARRAY(api, type, count) \
    ((type *)(api)->AllocZeroed((count), sizeof(type)))

#define NEVERC_REALLOC_ARRAY(api, ptr, type, count) \
    ((type *)(api)->ReallocArray((ptr), (count), sizeof(type)))

/* ---- Convenience batch-collect macros ----
 * Version-safe wrappers around the ModuleCollectAll* family.
 * Return the host-allocated array or NULL; caller frees via Free. */
#define NEVERC_COLLECT_FUNCTIONS(api, m, count) \
    (NEVERC_API_FN(api, ModuleCollectAllFunctions) \
     ? (api)->ModuleCollectAllFunctions((m), (count)) : NULL)

#define NEVERC_COLLECT_GLOBALS(api, m, count) \
    (NEVERC_API_FN(api, ModuleCollectAllGlobals) \
     ? (api)->ModuleCollectAllGlobals((m), (count)) : NULL)

#define NEVERC_COLLECT_INSTRUCTIONS(api, m, count) \
    (NEVERC_API_FN(api, ModuleCollectAllInstructions) \
     ? (api)->ModuleCollectAllInstructions((m), (count)) : NULL)

#define NEVERC_COLLECT_DEFINED_FUNCTIONS(api, m, count) \
    (NEVERC_API_FN(api, ModuleCollectDefinedFunctions) \
     ? (api)->ModuleCollectDefinedFunctions((m), (count)) : NULL)

#define NEVERC_COLLECT_OPCODES(api, m, count) \
    (NEVERC_API_FN(api, ModuleCollectAllOpcodes) \
     ? (api)->ModuleCollectAllOpcodes((m), (count)) : NULL)

/* ---- Iteration macros ----
 * For-each loops over IR / MIR linked lists.  Eliminates the repetitive
 * GetFirst+GetNext boilerplate.  The variable is scoped to the loop body.
 *
 * WARNING: Do NOT erase or move the current element inside the loop body.
 * GetNext is called on the current element to advance, so deleting it
 * causes undefined behaviour.  Use a two-phase collect-then-modify
 * pattern instead (e.g. DynArray + separate erasure loop).
 *
 * Example:
 *   NEVERC_FOR_EACH_FUNCTION(API, M, F) {
 *     if (API->FunctionIsDeclaration(F)) continue;
 *     ...
 *   }                                                                       */
#define NEVERC_FOR_EACH_FUNCTION(api, m, var)                                  \
    for (NevercValueRef var = (api)->ModuleGetFirstFunction(m);                \
         var; var = (api)->ModuleGetNextFunction(var))

#define NEVERC_FOR_EACH_GLOBAL(api, m, var)                                    \
    for (NevercValueRef var = (api)->ModuleGetFirstGlobal(m);                  \
         var; var = (api)->ModuleGetNextGlobal(var))

#define NEVERC_FOR_EACH_ALIAS(api, m, var)                                     \
    for (NevercValueRef var = (api)->ModuleGetFirstAlias(m);                   \
         var; var = (api)->ModuleGetNextAlias(var))

#define NEVERC_FOR_EACH_BB(api, fn, var)                                       \
    for (NevercBasicBlockRef var = (api)->FunctionGetFirstBB(fn);              \
         var; var = (api)->FunctionGetNextBB(var))

#define NEVERC_FOR_EACH_INST(api, bb, var)                                     \
    for (NevercValueRef var = (api)->BBGetFirstInst(bb);                       \
         var; var = (api)->BBGetNextInst(var))

#define NEVERC_FOR_EACH_MBB(api, mf, var)                                     \
    for (NevercMachineBBRef var = (api)->MFuncGetFirstBB(mf);                 \
         var; var = (api)->MFuncGetNextBB(var))

#define NEVERC_FOR_EACH_MI(api, mbb, var)                                     \
    for (NevercMachineInstrRef var = (api)->MBBGetFirstInst(mbb);             \
         var; var = (api)->MBBGetNextInst(var))

#define NEVERC_FOR_EACH_USE(api, val, var)                                    \
    for (NevercUseRef var = (api)->ValueGetFirstUse(val);                     \
         var; var = (api)->UseGetNext(var))

#define NEVERC_FOR_EACH_SYMBOL(api, var)                                      \
    for (NevercLinkerSymbolRef var = (api)->LinkGetFirstSymbol();              \
         var; var = (api)->LinkGetNextSymbol(var))

#define NEVERC_FOR_EACH_SECTION(api, var)                                     \
    for (NevercLinkerSectionRef var = (api)->LinkGetFirstSection();            \
         var; var = (api)->LinkGetNextSection(var))

/* -------------------------------------------------------------------------- */
/*  Opaque handle types                                                       */
/*  Underlying: reinterpret_cast of llvm::Module*, llvm::Value*, etc.         */
/* -------------------------------------------------------------------------- */

typedef struct NevercOpaqueModule       *NevercModuleRef;
typedef struct NevercOpaqueValue        *NevercValueRef;
typedef struct NevercOpaqueBasicBlock   *NevercBasicBlockRef;
typedef struct NevercOpaqueType         *NevercTypeRef;
typedef struct NevercOpaqueBuilder      *NevercBuilderRef;
typedef struct NevercOpaqueContext      *NevercContextRef;
typedef struct NevercOpaqueMetadata     *NevercMetadataRef;
typedef struct NevercOpaqueMachineFunc  *NevercMachineFuncRef;
typedef struct NevercOpaqueMachineBB   *NevercMachineBBRef;
typedef struct NevercOpaqueMachineInstr *NevercMachineInstrRef;
typedef struct NevercOpaqueUse         *NevercUseRef;
typedef struct NevercOpaqueNamedMD     *NevercNamedMDRef;
typedef struct NevercOpaqueComdat      *NevercComdatRef;
typedef struct NevercOpaqueLinkerSymbol  *NevercLinkerSymbolRef;
typedef struct NevercOpaqueLinkerSection *NevercLinkerSectionRef;
typedef struct NevercOpaqueDynArray      *NevercDynArrayRef;
typedef struct NevercOpaqueStrMap        *NevercStrMapRef;
typedef struct NevercOpaqueStrBuilder    *NevercStrBuilderRef;
typedef struct NevercOpaqueIntMap        *NevercIntMapRef;
typedef struct NevercOpaqueDomTree       *NevercDomTreeRef;
typedef struct NevercOpaquePostDomTree   *NevercPostDomTreeRef;
typedef struct NevercOpaqueLoopInfo      *NevercLoopInfoRef;
typedef struct NevercOpaqueLoop          *NevercLoopRef;
typedef struct NevercOpaqueSCEVInfo      *NevercSCEVInfoRef;
typedef struct NevercOpaqueCallGraph     *NevercCallGraphRef;

/* -------------------------------------------------------------------------- */
/*  Hook points                                                               */
/* -------------------------------------------------------------------------- */

typedef enum {
  /* ---- Normal flow -- IR (BackendUtil.cpp optimization pipeline) ---- */
  NEVERC_HOOK_PRE_OPT        = 0x0001,
  NEVERC_HOOK_POST_OPT       = 0x0002,
  NEVERC_HOOK_PIPELINE_START = 0x0003,
  NEVERC_HOOK_PIPELINE_LAST  = 0x0004,

  /* ---- Normal flow -- MIR (code generation pipeline) ---- */
  NEVERC_HOOK_BEFORE_CODEGEN_PREEMIT  = 0x0010,
  NEVERC_HOOK_AFTER_CODEGEN_FINAL_MIR = 0x0011,

  /* ---- Shellcode flow -- IR hooks ---- */
  NEVERC_HOOK_SC_BEFORE_PREP     = 0x0100,
  NEVERC_HOOK_SC_AFTER_PREP      = 0x0101,
  NEVERC_HOOK_SC_BEFORE_INLINING = 0x0102,
  NEVERC_HOOK_SC_AFTER_INLINING  = 0x0103,
  NEVERC_HOOK_SC_AFTER_STACKIFY  = 0x0104,
  NEVERC_HOOK_SC_AFTER_FINAL_IR  = 0x0105,

  /* ---- Shellcode flow -- MIR hooks ---- */
  NEVERC_HOOK_SC_BEFORE_PREEMIT  = 0x0200,
  NEVERC_HOOK_SC_AFTER_PREEMIT   = 0x0201,
  NEVERC_HOOK_SC_AFTER_FINAL_MIR = 0x0202,

  /* ---- Shellcode flow -- binary hooks ---- */
  NEVERC_HOOK_SC_POST_EXTRACT    = 0x0300,
  NEVERC_HOOK_SC_POST_FINALIZE   = 0x0301,

  /* ---- LTO flow -- IR hooks (inside LTO optimization pipeline) ---- */
  NEVERC_HOOK_LTO_PRE_OPT        = 0x0400,
  NEVERC_HOOK_LTO_POST_OPT       = 0x0401,

  /* ---- Linker flow -- object-level hooks ---- */
  NEVERC_HOOK_LINK_PRE_LAYOUT    = 0x0500,
  NEVERC_HOOK_LINK_POST_LAYOUT   = 0x0501,
  NEVERC_HOOK_LINK_POST_EMIT     = 0x0502
} NevercHookPoint;

/* -------------------------------------------------------------------------- */
/*  Comparison predicates (matches CmpInst::Predicate numeric values)         */
/* -------------------------------------------------------------------------- */

typedef enum {
  NEVERC_FCMP_FALSE = 0,
  NEVERC_FCMP_OEQ   = 1,
  NEVERC_FCMP_OGT   = 2,
  NEVERC_FCMP_OGE   = 3,
  NEVERC_FCMP_OLT   = 4,
  NEVERC_FCMP_OLE   = 5,
  NEVERC_FCMP_ONE   = 6,
  NEVERC_FCMP_ORD   = 7,
  NEVERC_FCMP_UNO   = 8,
  NEVERC_FCMP_UEQ   = 9,
  NEVERC_FCMP_UGT   = 10,
  NEVERC_FCMP_UGE   = 11,
  NEVERC_FCMP_ULT   = 12,
  NEVERC_FCMP_ULE   = 13,
  NEVERC_FCMP_UNE   = 14,
  NEVERC_FCMP_TRUE  = 15,

  NEVERC_ICMP_EQ    = 32,
  NEVERC_ICMP_NE    = 33,
  NEVERC_ICMP_UGT   = 34,
  NEVERC_ICMP_UGE   = 35,
  NEVERC_ICMP_ULT   = 36,
  NEVERC_ICMP_ULE   = 37,
  NEVERC_ICMP_SGT   = 38,
  NEVERC_ICMP_SGE   = 39,
  NEVERC_ICMP_SLT   = 40,
  NEVERC_ICMP_SLE   = 41
} NevercCmpPredicate;

/* -------------------------------------------------------------------------- */
/*  Linkage types (matches GlobalValue::LinkageTypes numeric values)          */
/* -------------------------------------------------------------------------- */

typedef enum {
  NEVERC_LINKAGE_EXTERNAL              = 0,
  NEVERC_LINKAGE_AVAILABLE_EXTERNALLY  = 1,
  NEVERC_LINKAGE_LINKONCE_ANY          = 2,
  NEVERC_LINKAGE_LINKONCE_ODR          = 3,
  NEVERC_LINKAGE_WEAK_ANY              = 4,
  NEVERC_LINKAGE_WEAK_ODR              = 5,
  NEVERC_LINKAGE_APPENDING             = 6,
  NEVERC_LINKAGE_INTERNAL              = 7,
  NEVERC_LINKAGE_PRIVATE               = 8,
  NEVERC_LINKAGE_EXTERNAL_WEAK         = 9,
  NEVERC_LINKAGE_COMMON                = 10
} NevercLinkage;

/* -------------------------------------------------------------------------- */
/*  COMDAT selection kind (matches Comdat::SelectionKind)                      */
/* -------------------------------------------------------------------------- */

typedef enum {
  NEVERC_COMDAT_ANY           = 0,
  NEVERC_COMDAT_EXACT_MATCH   = 1,
  NEVERC_COMDAT_LARGEST       = 2,
  NEVERC_COMDAT_NO_DEDUPLICATE = 3,
  NEVERC_COMDAT_SAME_SIZE     = 4
} NevercComdatSelectionKind;

/* -------------------------------------------------------------------------- */
/*  MachineOperand kinds (returned by MInstCollectOperandKinds)               */
/*  Numeric values are stable across API versions; new kinds append at the    */
/*  end with the next unused integer.                                          */
/* -------------------------------------------------------------------------- */

typedef enum {
  NEVERC_MIR_OP_OTHER     = 0,
  NEVERC_MIR_OP_REG       = 1,
  NEVERC_MIR_OP_IMM       = 2,
  NEVERC_MIR_OP_FPIMM     = 3,
  NEVERC_MIR_OP_MBB       = 4,
  NEVERC_MIR_OP_FRAMEIDX  = 5,
  NEVERC_MIR_OP_GLOBAL    = 6,
  NEVERC_MIR_OP_EXTSYM    = 7,
  NEVERC_MIR_OP_METADATA  = 8,
  NEVERC_MIR_OP_REGMASK   = 9,
  NEVERC_MIR_OP_BLOCKADDR = 10
} NevercMIROperandKind;

/* -------------------------------------------------------------------------- */
/*  Linker output object format (returned by LinkGetOutputFormat)             */
/* -------------------------------------------------------------------------- */

typedef enum {
  NEVERC_LINK_FORMAT_UNKNOWN = 0,
  NEVERC_LINK_FORMAT_ELF     = 1,
  NEVERC_LINK_FORMAT_COFF    = 2,
  NEVERC_LINK_FORMAT_MACHO   = 3
} NevercLinkerOutputFormat;

/* -------------------------------------------------------------------------- */
/*  Pass callback signatures                                                  */
/* -------------------------------------------------------------------------- */

struct NevercHostAPI;

/* IR-level pass.  Return 0 = module not modified, nonzero = modified. */
typedef int (*NevercModulePassFn)(NevercModuleRef M,
                                  const struct NevercHostAPI *API,
                                  void *UserData);

/* MIR-level pass (per MachineFunction). */
typedef int (*NevercMachinePassFn)(NevercMachineFuncRef MF,
                                   const struct NevercHostAPI *API,
                                   void *UserData);

/* Binary-level pass.  Operates on raw bytes; may resize via API->BinaryResize.
 * Data/Len/Capacity are double-indirected so BinaryResize can reallocate. */
typedef int (*NevercBinaryPassFn)(uint8_t **Data, uint64_t *Len,
                                  uint64_t *Capacity,
                                  const struct NevercHostAPI *API,
                                  void *UserData);

/* Linker-level pass (object-level hooks).
 * Called during linking for symbol/section inspection and manipulation.
 * Context is an opaque handle to backend-specific linker state. */
typedef int (*NevercLinkerPassFn)(const struct NevercHostAPI *API,
                                  void *UserData);

/* -------------------------------------------------------------------------- */
/*  Host API vtable                                                           */
/*  The host (neverc) populates this struct and passes it to the plugin.      */
/*  Append-only: new fields go at the end; bump NEVERC_PLUGIN_API_VERSION.    */
/* -------------------------------------------------------------------------- */

typedef struct NevercHostAPI {
  uint32_t Version;
  uint32_t StructSize; /* sizeof(NevercHostAPI) -- plugin MUST NOT access
                          fields past this boundary even if its compiled
                          header defines more fields. */

  /* ---- Memory (host heap -- mimalloc when enabled; no plugin CRT malloc) ---- */
  void *(*Alloc)(uint64_t Size);
  void *(*Realloc)(void *Ptr, uint64_t Size);
  void  (*Free)(void *Ptr);

  /* ---- Diagnostics (Msg must be null-terminated) ---- */
  void (*DiagNote)(const char *Msg);
  void (*DiagWarning)(const char *Msg);
  void (*DiagError)(const char *Msg);

  /* ---- Context ---- */
  NevercContextRef (*ModuleGetContext)(NevercModuleRef M);

  /* ---- Module iteration (returns NULL at end of list) ---- */
  NevercValueRef (*ModuleGetFirstFunction)(NevercModuleRef M);
  NevercValueRef (*ModuleGetLastFunction)(NevercModuleRef M);
  NevercValueRef (*ModuleGetNextFunction)(NevercValueRef F);
  NevercValueRef (*ModuleGetNamedFunction)(NevercModuleRef M, const char *Name);
  NevercValueRef (*ModuleGetFirstGlobal)(NevercModuleRef M);
  NevercValueRef (*ModuleGetNextGlobal)(NevercValueRef G);
  NevercValueRef (*ModuleAddFunction)(NevercModuleRef M, const char *Name,
                                      NevercTypeRef FnTy);

  /* ---- Value ops (returned const char* valid while owning value exists) ---- */
  const char *(*ValueGetName)(NevercValueRef V);
  NevercTypeRef (*ValueGetType)(NevercValueRef V);
  void (*ValueReplaceAllUsesWith)(NevercValueRef Old, NevercValueRef New);
  void (*ValueEraseFromParent)(NevercValueRef V);

  /* ---- Function ops ---- */
  NevercBasicBlockRef (*FunctionGetFirstBB)(NevercValueRef F);
  NevercBasicBlockRef (*FunctionGetLastBB)(NevercValueRef F);
  NevercBasicBlockRef (*FunctionGetNextBB)(NevercBasicBlockRef BB);
  unsigned (*FunctionGetArgCount)(NevercValueRef F);
  NevercValueRef (*FunctionGetArg)(NevercValueRef F, unsigned Idx);
  int (*FunctionIsDeclaration)(NevercValueRef F);

  /* ---- BasicBlock ops ---- */
  NevercValueRef (*BBGetFirstInst)(NevercBasicBlockRef BB);
  NevercValueRef (*BBGetLastInst)(NevercBasicBlockRef BB);
  NevercValueRef (*BBGetNextInst)(NevercValueRef I);
  NevercValueRef (*BBGetTerminator)(NevercBasicBlockRef BB);
  NevercValueRef (*BBGetParentFunction)(NevercBasicBlockRef BB);
  NevercBasicBlockRef (*BBCreate)(NevercContextRef C, const char *Name,
                                  NevercValueRef F);

  /* ---- Instruction ops ---- */
  unsigned (*InstGetOpcode)(NevercValueRef I);
  unsigned (*InstGetNumOperands)(NevercValueRef I);
  NevercValueRef (*InstGetOperand)(NevercValueRef I, unsigned Idx);
  void (*InstSetOperand)(NevercValueRef I, unsigned Idx, NevercValueRef V);
  void (*InstEraseFromParent)(NevercValueRef I);
  NevercValueRef (*InstClone)(NevercValueRef I);
  NevercBasicBlockRef (*InstGetParentBB)(NevercValueRef I);

  /* ---- Type ops ---- */
  NevercTypeRef (*TypeGetInt1)(NevercContextRef C);
  NevercTypeRef (*TypeGetInt8)(NevercContextRef C);
  NevercTypeRef (*TypeGetInt16)(NevercContextRef C);
  NevercTypeRef (*TypeGetInt32)(NevercContextRef C);
  NevercTypeRef (*TypeGetInt64)(NevercContextRef C);
  NevercTypeRef (*TypeGetIntN)(NevercContextRef C, unsigned NumBits);
  NevercTypeRef (*TypeGetFloat)(NevercContextRef C);
  NevercTypeRef (*TypeGetDouble)(NevercContextRef C);
  NevercTypeRef (*TypeGetVoid)(NevercContextRef C);
  NevercTypeRef (*TypeGetPtr)(NevercContextRef C);
  NevercTypeRef (*TypeGetArray)(NevercTypeRef ElemTy, uint64_t Count);
  NevercTypeRef (*TypeGetFunction)(NevercTypeRef RetTy,
                                   NevercTypeRef *ParamTys,
                                   unsigned ParamCount, int IsVarArg);
  int (*TypeIsInteger)(NevercTypeRef T);
  int (*TypeIsFloat)(NevercTypeRef T);
  int (*TypeIsPointer)(NevercTypeRef T);
  int (*TypeIsVoid)(NevercTypeRef T);
  unsigned (*TypeGetIntWidth)(NevercTypeRef T);

  /* ---- Constant creation ---- */
  NevercValueRef (*ConstInt)(NevercTypeRef IntTy, uint64_t Val, int SignExtend);
  NevercValueRef (*ConstFloat)(NevercTypeRef FloatTy, double Val);
  NevercValueRef (*ConstNull)(NevercTypeRef Ty);
  NevercValueRef (*ConstUndef)(NevercTypeRef Ty);
  NevercValueRef (*ConstString)(NevercContextRef C, const char *Str,
                                uint32_t Len, int DontNullTerminate);

  /* ---- IRBuilder (caller must call BuilderDispose before pass returns) ---- */
  NevercBuilderRef (*BuilderCreate)(NevercContextRef C);
  void (*BuilderDispose)(NevercBuilderRef B);
  void (*BuilderSetInsertPoint)(NevercBuilderRef B, NevercBasicBlockRef BB);
  void (*BuilderSetInsertPointBefore)(NevercBuilderRef B, NevercValueRef Inst);

  NevercValueRef (*BuildAdd)(NevercBuilderRef B, NevercValueRef LHS,
                             NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildSub)(NevercBuilderRef B, NevercValueRef LHS,
                             NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildMul)(NevercBuilderRef B, NevercValueRef LHS,
                             NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildUDiv)(NevercBuilderRef B, NevercValueRef LHS,
                              NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildSDiv)(NevercBuilderRef B, NevercValueRef LHS,
                              NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildAnd)(NevercBuilderRef B, NevercValueRef LHS,
                             NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildOr)(NevercBuilderRef B, NevercValueRef LHS,
                            NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildXor)(NevercBuilderRef B, NevercValueRef LHS,
                             NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildShl)(NevercBuilderRef B, NevercValueRef LHS,
                             NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildLShr)(NevercBuilderRef B, NevercValueRef LHS,
                              NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildAShr)(NevercBuilderRef B, NevercValueRef LHS,
                              NevercValueRef RHS, const char *Name);

  NevercValueRef (*BuildICmp)(NevercBuilderRef B, unsigned Pred,
                              NevercValueRef LHS, NevercValueRef RHS,
                              const char *Name);
  NevercValueRef (*BuildFCmp)(NevercBuilderRef B, unsigned Pred,
                              NevercValueRef LHS, NevercValueRef RHS,
                              const char *Name);

  NevercValueRef (*BuildBr)(NevercBuilderRef B, NevercBasicBlockRef Dest);
  NevercValueRef (*BuildCondBr)(NevercBuilderRef B, NevercValueRef Cond,
                                NevercBasicBlockRef Then,
                                NevercBasicBlockRef Else);
  NevercValueRef (*BuildRet)(NevercBuilderRef B, NevercValueRef V);
  NevercValueRef (*BuildRetVoid)(NevercBuilderRef B);

  NevercValueRef (*BuildCall)(NevercBuilderRef B, NevercTypeRef FnTy,
                              NevercValueRef Fn, NevercValueRef *Args,
                              unsigned ArgCount, const char *Name);

  NevercValueRef (*BuildGEP)(NevercBuilderRef B, NevercTypeRef Ty,
                             NevercValueRef Ptr, NevercValueRef *Indices,
                             unsigned NumIndices, const char *Name);
  NevercValueRef (*BuildLoad)(NevercBuilderRef B, NevercTypeRef Ty,
                              NevercValueRef Ptr, const char *Name);
  NevercValueRef (*BuildStore)(NevercBuilderRef B, NevercValueRef Val,
                               NevercValueRef Ptr);
  NevercValueRef (*BuildAlloca)(NevercBuilderRef B, NevercTypeRef Ty,
                                const char *Name);

  NevercValueRef (*BuildBitCast)(NevercBuilderRef B, NevercValueRef V,
                                 NevercTypeRef DestTy, const char *Name);
  NevercValueRef (*BuildIntToPtr)(NevercBuilderRef B, NevercValueRef V,
                                  NevercTypeRef DestTy, const char *Name);
  NevercValueRef (*BuildPtrToInt)(NevercBuilderRef B, NevercValueRef V,
                                  NevercTypeRef DestTy, const char *Name);
  NevercValueRef (*BuildZExt)(NevercBuilderRef B, NevercValueRef V,
                              NevercTypeRef DestTy, const char *Name);
  NevercValueRef (*BuildSExt)(NevercBuilderRef B, NevercValueRef V,
                              NevercTypeRef DestTy, const char *Name);
  NevercValueRef (*BuildTrunc)(NevercBuilderRef B, NevercValueRef V,
                               NevercTypeRef DestTy, const char *Name);

  NevercValueRef (*BuildSelect)(NevercBuilderRef B, NevercValueRef Cond,
                                NevercValueRef Then, NevercValueRef Else,
                                const char *Name);
  NevercValueRef (*BuildPhi)(NevercBuilderRef B, NevercTypeRef Ty,
                             const char *Name);
  void (*PhiAddIncoming)(NevercValueRef Phi, NevercValueRef *Values,
                         NevercBasicBlockRef *Blocks, unsigned Count);

  /* ---- Metadata ---- */
  NevercMetadataRef (*MDStringCreate)(NevercContextRef C, const char *Str,
                                      uint32_t Len);
  NevercMetadataRef (*MDNodeCreate)(NevercContextRef C,
                                    NevercMetadataRef *Vals, unsigned Count);
  void (*InstSetMetadata)(NevercValueRef Inst, unsigned KindID,
                          NevercMetadataRef MD);
  NevercMetadataRef (*InstGetMetadata)(NevercValueRef Inst, unsigned KindID);
  unsigned (*MDKindGetID)(NevercContextRef C, const char *Name);

  /* ---- MIR ops (for shellcode MIR hooks) ---- */
  NevercMachineBBRef (*MFuncGetFirstBB)(NevercMachineFuncRef MF);
  NevercMachineBBRef (*MFuncGetNextBB)(NevercMachineBBRef MBB);
  NevercMachineInstrRef (*MBBGetFirstInst)(NevercMachineBBRef MBB);
  NevercMachineInstrRef (*MBBGetNextInst)(NevercMachineInstrRef MI);
  unsigned (*MInstGetOpcode)(NevercMachineInstrRef MI);
  unsigned (*MInstGetNumOperands)(NevercMachineInstrRef MI);
  void (*MInstEraseFromParent)(NevercMachineInstrRef MI);

  /* ---- Pass registration (only valid inside RegisterPasses callback) ---- */
  void (*RegisterModulePass)(void *Registrar, NevercHookPoint Hook,
                             NevercModulePassFn Fn, void *UserData,
                             const char *PassName);
  void (*RegisterMachinePass)(void *Registrar, NevercHookPoint Hook,
                              NevercMachinePassFn Fn, void *UserData,
                              const char *PassName);
  void (*RegisterBinaryPass)(void *Registrar, NevercHookPoint Hook,
                             NevercBinaryPassFn Fn, void *UserData,
                             const char *PassName);

  /* ---- Binary buffer ops (for shellcode binary hooks) ---- */
  int (*BinaryResize)(uint8_t **Data, uint64_t *Len, uint64_t *Capacity,
                      uint64_t NewLen);

  /* ---- Module info (returned strings valid while Module exists) ---- */
  const char *(*ModuleGetDataLayout)(NevercModuleRef M);
  const char *(*ModuleGetTargetTriple)(NevercModuleRef M);
  void (*ModulePrint)(NevercModuleRef M);

  /* ---- Value debug ---- */
  void (*ValueDump)(NevercValueRef V);
  unsigned (*ValueGetNumUses)(NevercValueRef V);

  /* ---- Function linkage & calling convention ---- */
  unsigned (*FunctionGetLinkage)(NevercValueRef F);
  void (*FunctionSetLinkage)(NevercValueRef F, unsigned Linkage);
  unsigned (*FunctionGetCallingConv)(NevercValueRef F);
  void (*FunctionSetCallingConv)(NevercValueRef F, unsigned CC);

  /* ---- Instruction positioning ---- */
  void (*InstMoveBefore)(NevercValueRef I, NevercValueRef Before);

  /* ---- Module set ops ---- */
  void (*ModuleSetDataLayout)(NevercModuleRef M, const char *DL);
  void (*ModuleSetTargetTriple)(NevercModuleRef M, const char *Triple);

  /* ---- Value user iteration ---- */
  NevercUseRef (*ValueGetFirstUse)(NevercValueRef V);
  NevercUseRef (*UseGetNext)(NevercUseRef U);
  NevercValueRef (*UseGetUser)(NevercUseRef U);

  /* ---- Function attributes (string attributes for custom tags) ---- */
  void (*FunctionAddStringAttr)(NevercValueRef F, const char *Kind,
                                const char *Val);
  int (*FunctionHasStringAttr)(NevercValueRef F, const char *Kind);
  const char *(*FunctionGetStringAttr)(NevercValueRef F, const char *Kind);
  void (*FunctionRemoveStringAttr)(NevercValueRef F, const char *Kind);

  /* ---- BasicBlock removal ---- */
  void (*BBRemoveFromParent)(NevercBasicBlockRef BB);
  void (*BBEraseFromParent)(NevercBasicBlockRef BB);

  /* ---- Value kind queries ---- */
  int (*ValueIsFunction)(NevercValueRef V);
  int (*ValueIsGlobalVariable)(NevercValueRef V);
  int (*ValueIsInstruction)(NevercValueRef V);
  int (*ValueIsConstant)(NevercValueRef V);
  int (*ValueIsArgument)(NevercValueRef V);
  int (*ValueIsBasicBlock)(NevercValueRef V);

  /* ---- Struct type ops ---- */
  NevercTypeRef (*TypeGetStruct)(NevercContextRef C,
                                 NevercTypeRef *ElemTys, unsigned ElemCount,
                                 int IsPacked);
  NevercTypeRef (*TypeGetNamedStruct)(NevercContextRef C, const char *Name);
  void (*StructSetBody)(NevercTypeRef StructTy,
                        NevercTypeRef *ElemTys, unsigned ElemCount,
                        int IsPacked);
  unsigned (*StructGetNumElements)(NevercTypeRef StructTy);
  NevercTypeRef (*StructGetElementType)(NevercTypeRef StructTy, unsigned Idx);
  int (*TypeIsStruct)(NevercTypeRef T);

  /* ---- GlobalVariable ops ---- */
  NevercValueRef (*ModuleAddGlobal)(NevercModuleRef M, NevercTypeRef Ty,
                                    int IsConstant, const char *Name);
  NevercValueRef (*GlobalGetInitializer)(NevercValueRef GV);
  void (*GlobalSetInitializer)(NevercValueRef GV, NevercValueRef Init);
  int (*GlobalHasInitializer)(NevercValueRef GV);
  int (*GlobalIsConstant)(NevercValueRef GV);
  void (*GlobalSetConstant)(NevercValueRef GV, int IsConstant);

  /* ---- Module named-global lookup ---- */
  NevercValueRef (*ModuleGetNamedGlobal)(NevercModuleRef M, const char *Name);

  /* ---- Instruction insertion ---- */
  void (*InstInsertBefore)(NevercValueRef Inst, NevercValueRef Before);
  void (*InstInsertAfter)(NevercValueRef Inst, NevercValueRef After);

  /* ---- MIR name & operand access ---- */
  const char *(*MFuncGetName)(NevercMachineFuncRef MF);
  int64_t (*MInstGetOperandImm)(NevercMachineInstrRef MI, unsigned Idx);
  int (*MInstGetOperandIsReg)(NevercMachineInstrRef MI, unsigned Idx);
  unsigned (*MInstGetOperandReg)(NevercMachineInstrRef MI, unsigned Idx);
  int (*MInstGetOperandIsImm)(NevercMachineInstrRef MI, unsigned Idx);

  /* ---- MBB navigation ---- */
  unsigned (*MBBGetNumber)(NevercMachineBBRef MBB);
  unsigned (*MBBGetSuccCount)(NevercMachineBBRef MBB);
  unsigned (*MBBGetPredCount)(NevercMachineBBRef MBB);

  /* ---- Function/global removal ---- */
  void (*ModuleRemoveFunction)(NevercModuleRef M, NevercValueRef F);
  void (*ModuleRemoveGlobal)(NevercModuleRef M, NevercValueRef GV);

  /* ---- Module convenience counters ---- */
  unsigned (*ModuleGetFunctionCount)(NevercModuleRef M);
  unsigned (*ModuleGetGlobalCount)(NevercModuleRef M);
  NevercTypeRef (*ValueGetTypeAsFunction)(NevercValueRef F);

  /* ---- Function return type ---- */
  NevercTypeRef (*FunctionGetReturnType)(NevercValueRef F);

  /* ---- Instruction queries ---- */
  int (*InstIsTerminator)(NevercValueRef I);

  /* ---- Unary builder ops ---- */
  NevercValueRef (*BuildNeg)(NevercBuilderRef B, NevercValueRef V,
                             const char *Name);
  NevercValueRef (*BuildNot)(NevercBuilderRef B, NevercValueRef V,
                             const char *Name);
  NevercValueRef (*BuildFNeg)(NevercBuilderRef B, NevercValueRef V,
                              const char *Name);
  NevercValueRef (*BuildUnreachable)(NevercBuilderRef B);

  /* ---- Switch instruction ---- */
  NevercValueRef (*BuildSwitch)(NevercBuilderRef B, NevercValueRef V,
                                NevercBasicBlockRef DefaultBB,
                                unsigned NumCases);
  void (*SwitchAddCase)(NevercValueRef Switch, NevercValueRef OnVal,
                        NevercBasicBlockRef Dest);

  /* ---- Constant null pointer ---- */
  NevercValueRef (*ConstPointerNull)(NevercTypeRef PtrTy);

  /* ---- URem/SRem ---- */
  NevercValueRef (*BuildURem)(NevercBuilderRef B, NevercValueRef LHS,
                              NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildSRem)(NevercBuilderRef B, NevercValueRef LHS,
                              NevercValueRef RHS, const char *Name);

  /* ---- Floating-point cast ops ---- */
  NevercValueRef (*BuildFPToSI)(NevercBuilderRef B, NevercValueRef V,
                                NevercTypeRef DestTy, const char *Name);
  NevercValueRef (*BuildSIToFP)(NevercBuilderRef B, NevercValueRef V,
                                NevercTypeRef DestTy, const char *Name);
  NevercValueRef (*BuildFPToUI)(NevercBuilderRef B, NevercValueRef V,
                                NevercTypeRef DestTy, const char *Name);
  NevercValueRef (*BuildUIToFP)(NevercBuilderRef B, NevercValueRef V,
                                NevercTypeRef DestTy, const char *Name);

  /* ---- Instruction kind queries ---- */
  int (*InstIsCall)(NevercValueRef I);
  int (*InstIsBranch)(NevercValueRef I);
  int (*InstIsLoad)(NevercValueRef I);
  int (*InstIsStore)(NevercValueRef I);
  int (*InstIsAlloca)(NevercValueRef I);
  int (*InstIsPHI)(NevercValueRef I);
  int (*InstIsGEP)(NevercValueRef I);
  int (*InstIsCast)(NevercValueRef I);
  int (*InstIsBinaryOp)(NevercValueRef I);
  int (*InstIsUnaryOp)(NevercValueRef I);
  int (*InstIsSwitch)(NevercValueRef I);
  int (*InstIsReturn)(NevercValueRef I);
  int (*InstIsSelect)(NevercValueRef I);

  /* ---- CallInst ops ---- */
  NevercValueRef (*CallGetCalledOperand)(NevercValueRef I);
  unsigned (*CallGetNumArgs)(NevercValueRef I);
  NevercValueRef (*CallGetArg)(NevercValueRef I, unsigned Idx);
  NevercTypeRef (*CallGetFunctionType)(NevercValueRef I);

  /* ---- FunctionType access ---- */
  unsigned (*FuncTypeGetParamCount)(NevercTypeRef FnTy);
  NevercTypeRef (*FuncTypeGetParamType)(NevercTypeRef FnTy, unsigned Idx);
  NevercTypeRef (*FuncTypeGetReturnType)(NevercTypeRef FnTy);
  int (*FuncTypeIsVarArg)(NevercTypeRef FnTy);
  int (*TypeIsFunctionTy)(NevercTypeRef T);
  int (*TypeIsArrayTy)(NevercTypeRef T);
  uint64_t (*TypeGetArrayLength)(NevercTypeRef T);
  NevercTypeRef (*TypeGetArrayElementType)(NevercTypeRef T);

  /* ---- BranchInst ops ---- */
  int (*BrIsConditional)(NevercValueRef I);
  NevercValueRef (*BrGetCondition)(NevercValueRef I);
  NevercBasicBlockRef (*BrGetSuccessor)(NevercValueRef I, unsigned Idx);
  unsigned (*BrGetNumSuccessors)(NevercValueRef I);

  /* ---- Load/Store operand access ---- */
  NevercValueRef (*LoadGetPointerOperand)(NevercValueRef I);
  NevercValueRef (*StoreGetValueOperand)(NevercValueRef I);
  NevercValueRef (*StoreGetPointerOperand)(NevercValueRef I);

  /* ---- GEP access ---- */
  NevercValueRef (*GEPGetPointerOperand)(NevercValueRef I);
  unsigned (*GEPGetNumIndices)(NevercValueRef I);

  /* ---- BasicBlock predecessor/successor count ---- */
  unsigned (*BBGetPredCount)(NevercBasicBlockRef BB);
  unsigned (*BBGetSuccCount)(NevercBasicBlockRef BB);

  /* ---- Floating-point arithmetic ---- */
  NevercValueRef (*BuildFAdd)(NevercBuilderRef B, NevercValueRef LHS,
                              NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildFSub)(NevercBuilderRef B, NevercValueRef LHS,
                              NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildFMul)(NevercBuilderRef B, NevercValueRef LHS,
                              NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildFDiv)(NevercBuilderRef B, NevercValueRef LHS,
                              NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildFRem)(NevercBuilderRef B, NevercValueRef LHS,
                              NevercValueRef RHS, const char *Name);

  /* ---- Aggregate value ops ---- */
  NevercValueRef (*BuildExtractValue)(NevercBuilderRef B, NevercValueRef Agg,
                                      unsigned Idx, const char *Name);
  NevercValueRef (*BuildInsertValue)(NevercBuilderRef B, NevercValueRef Agg,
                                     NevercValueRef Val, unsigned Idx,
                                     const char *Name);

  /* ---- Constant aggregate creation ---- */
  NevercValueRef (*ConstArray)(NevercTypeRef ElemTy, NevercValueRef *Vals,
                               unsigned Count);
  NevercValueRef (*ConstStruct)(NevercContextRef C, NevercValueRef *Vals,
                                unsigned Count, int IsPacked);
  NevercValueRef (*ConstNamedStruct)(NevercTypeRef StructTy,
                                     NevercValueRef *Vals, unsigned Count);

  /* ---- Global string pointer convenience ---- */
  NevercValueRef (*BuildGlobalStringPtr)(NevercBuilderRef B, const char *Str,
                                         const char *Name);

  /* ---- Value name mutation ---- */
  void (*ValueSetName)(NevercValueRef V, const char *Name);

  /* ---- GlobalVariable linkage ---- */
  unsigned (*GlobalGetLinkage)(NevercValueRef GV);
  void (*GlobalSetLinkage)(NevercValueRef GV, unsigned Linkage);

  /* ---- InBounds GEP ---- */
  NevercValueRef (*BuildInBoundsGEP)(NevercBuilderRef B, NevercTypeRef Ty,
                                     NevercValueRef Ptr,
                                     NevercValueRef *Indices,
                                     unsigned NumIndices, const char *Name);

  /* ---- Module verification (returns 0 on success) ---- */
  int (*ModuleVerify)(NevercModuleRef M);

  /* ---- Terminator successor navigation ---- */
  unsigned (*InstGetNumSuccessors)(NevercValueRef I);
  NevercBasicBlockRef (*InstGetSuccessor)(NevercValueRef I, unsigned Idx);

  /* ---- AllocaInst source element type ---- */
  NevercTypeRef (*AllocaGetAllocatedType)(NevercValueRef I);

  /* ---- Global variable alignment ---- */
  unsigned (*GlobalGetAlignment)(NevercValueRef GV);
  void (*GlobalSetAlignment)(NevercValueRef GV, unsigned Align);

  /* ---- BasicBlock predecessor/successor by index ---- */
  NevercBasicBlockRef (*BBGetPredecessor)(NevercBasicBlockRef BB, unsigned Idx);
  NevercBasicBlockRef (*BBGetSuccessor)(NevercBasicBlockRef BB, unsigned Idx);

  /* ---- Value identity comparison ---- */
  int (*ValueIsNull)(NevercValueRef V);
  int (*ValueIsSameAs)(NevercValueRef A, NevercValueRef B);

  /* ---- PHI node access ---- */
  unsigned (*PhiGetNumIncoming)(NevercValueRef Phi);
  NevercValueRef (*PhiGetIncomingValue)(NevercValueRef Phi, unsigned Idx);
  NevercBasicBlockRef (*PhiGetIncomingBlock)(NevercValueRef Phi, unsigned Idx);

  /* ---- SelectInst operand access ---- */
  NevercValueRef (*SelectGetCondition)(NevercValueRef I);
  NevercValueRef (*SelectGetTrueValue)(NevercValueRef I);
  NevercValueRef (*SelectGetFalseValue)(NevercValueRef I);

  /* ---- ReturnInst operand access ---- */
  NevercValueRef (*ReturnGetValue)(NevercValueRef I);

  /* ---- CastInst type access ---- */
  NevercTypeRef (*CastGetSrcTy)(NevercValueRef I);
  NevercTypeRef (*CastGetDestTy)(NevercValueRef I);

  /* ---- GEP source element type ---- */
  NevercTypeRef (*GEPGetSourceElementType)(NevercValueRef I);

  /* ---- ICmp/FCmp predicate access ---- */
  unsigned (*CmpGetPredicate)(NevercValueRef I);

  /* ---- CmpInst kind queries (use before CmpGetPredicate) ---- */
  int (*InstIsICmp)(NevercValueRef I);
  int (*InstIsFCmp)(NevercValueRef I);

  /* ---- BasicBlock name (useful for debugging; may be empty) ---- */
  const char *(*BBGetName)(NevercBasicBlockRef BB);

  /* ---- Function BasicBlock count ---- */
  unsigned (*FunctionGetBBCount)(NevercValueRef F);

  /* ---- Reverse iteration (needed for safe deletion during traversal) ---- */
  NevercValueRef (*InstGetPrevInst)(NevercValueRef I);
  NevercBasicBlockRef (*FunctionGetPrevBB)(NevercBasicBlockRef BB);

  /* ---- StructType name (NULL for anonymous structs) ---- */
  const char *(*TypeGetStructName)(NevercTypeRef T);

  /* ---- SwitchInst inspection ---- */
  unsigned (*SwitchGetNumCases)(NevercValueRef I);
  NevercValueRef (*SwitchGetCaseValue)(NevercValueRef I, unsigned Idx);
  NevercBasicBlockRef (*SwitchGetCaseSuccessor)(NevercValueRef I, unsigned Idx);
  NevercBasicBlockRef (*SwitchGetDefaultDest)(NevercValueRef I);

  /* ---- MIR BasicBlock navigation by index ---- */
  NevercMachineBBRef (*MBBGetSuccessor)(NevercMachineBBRef MBB, unsigned Idx);
  NevercMachineBBRef (*MBBGetPredecessor)(NevercMachineBBRef MBB, unsigned Idx);

  /* ---- Module source file name ---- */
  const char *(*ModuleGetSourceFileName)(NevercModuleRef M);

  /* ---- InvokeInst support (for EH-aware code) ---- */
  int (*InstIsInvoke)(NevercValueRef I);
  NevercBasicBlockRef (*InvokeGetNormalDest)(NevercValueRef I);
  NevercBasicBlockRef (*InvokeGetUnwindDest)(NevercValueRef I);

  /* ---- BasicBlock splitting (returns the new BB containing Inst and after) */
  NevercBasicBlockRef (*BBSplitBefore)(NevercBasicBlockRef BB,
                                       NevercValueRef Inst);

  /* ---- IRBuilder insert-point query ---- */
  NevercBasicBlockRef (*BuilderGetInsertBlock)(NevercBuilderRef B);

  /* ---- ConstantInt value extraction ---- */
  uint64_t (*ConstIntGetZExtValue)(NevercValueRef V);
  int64_t  (*ConstIntGetSExtValue)(NevercValueRef V);
  int (*ValueIsConstantInt)(NevercValueRef V);

  /* ---- GlobalVariable value type ---- */
  NevercTypeRef (*GlobalGetValueType)(NevercValueRef GV);

  /* ---- Function body removal (turns it into a declaration) ---- */
  void (*FunctionDeleteBody)(NevercValueRef F);

  /* ---- Value undef check ---- */
  int (*ValueIsUndef)(NevercValueRef V);

  /* ---- Instruction operand BB access (for terminator branch targets) ---- */
  NevercBasicBlockRef (*InstGetOperandAsBB)(NevercValueRef I, unsigned Idx);

  /* ---- FP precision cast ops ---- */
  NevercValueRef (*BuildFPExt)(NevercBuilderRef B, NevercValueRef V,
                               NevercTypeRef DestTy, const char *Name);
  NevercValueRef (*BuildFPTrunc)(NevercBuilderRef B, NevercValueRef V,
                                 NevercTypeRef DestTy, const char *Name);

  /* ---- LoadInst source type ---- */
  NevercTypeRef (*LoadGetType)(NevercValueRef I);

  /* ---- GEP inbounds query ---- */
  int (*GEPIsInBounds)(NevercValueRef I);

  /* ---- CallBase convenience (matches call, invoke, callbr) ---- */
  int (*InstIsCallLike)(NevercValueRef I);

  /* ---- Load/Store alignment and volatile flags ---- */
  unsigned (*LoadGetAlignment)(NevercValueRef I);
  unsigned (*StoreGetAlignment)(NevercValueRef I);
  int (*LoadIsVolatile)(NevercValueRef I);
  int (*StoreIsVolatile)(NevercValueRef I);

  /* ---- Builder: insert before terminator of a BB (common pattern) ---- */
  void (*BuilderSetInsertPointBeforeTerminator)(NevercBuilderRef B,
                                                NevercBasicBlockRef BB);

  /* ---- Intrinsic detection (critical for skipping LLVM builtins) ---- */
  int (*InstIsIntrinsic)(NevercValueRef I);
  unsigned (*CallGetIntrinsicID)(NevercValueRef I);

  /* ---- Aligned Load/Store (preserves alignment when cloning/moving) ---- */
  NevercValueRef (*BuildAlignedLoad)(NevercBuilderRef B, NevercTypeRef Ty,
                                     NevercValueRef Ptr, unsigned Align,
                                     int IsVolatile, const char *Name);
  NevercValueRef (*BuildAlignedStore)(NevercBuilderRef B, NevercValueRef Val,
                                      NevercValueRef Ptr, unsigned Align,
                                      int IsVolatile);

  /* ---- Debug location (critical for preserving line info in IR-mutating
         passes -- without this, debugger single-stepping breaks) ---- */
  void (*InstCopyDebugLoc)(NevercValueRef Dst, NevercValueRef Src);
  int  (*InstHasDebugLoc)(NevercValueRef I);
  unsigned (*InstGetDebugLocLine)(NevercValueRef I);
  unsigned (*InstGetDebugLocCol)(NevercValueRef I);

  /* ---- GlobalObject section (works on both Function and GlobalVariable;
         needed for section placement, e.g. .bss, .rdata, custom) ---- */
  const char *(*GlobalGetSection)(NevercValueRef GV);
  void (*GlobalSetSection)(NevercValueRef GV, const char *Section);

  /* ---- Function parameter attributes ---- */
  void (*FunctionAddParamAttr)(NevercValueRef F, unsigned ParamIdx,
                               const char *Kind, const char *Val);
  int (*FunctionHasParamAttr)(NevercValueRef F, unsigned ParamIdx,
                              const char *Kind);

  /* ---- Instruction count in a BasicBlock ---- */
  unsigned (*BBGetInstCount)(NevercBasicBlockRef BB);

  /* ---- Module-level iteration: function/global count already exists,
         add alias iteration ---- */
  NevercValueRef (*ModuleGetFirstAlias)(NevercModuleRef M);
  NevercValueRef (*ModuleGetNextAlias)(NevercValueRef A);

  /* ---- GEP index access (Idx is 0-based into the index list,
         NOT the LLVM operand index -- operand 0 is the pointer) ---- */
  NevercValueRef (*GEPGetIndex)(NevercValueRef I, unsigned Idx);

  /* ---- Reverse global iteration ---- */
  NevercValueRef (*ModuleGetLastGlobal)(NevercModuleRef M);
  NevercValueRef (*ModuleGetPrevGlobal)(NevercValueRef G);

  /* ---- Text representation (returned string is host-allocated;
         caller MUST free via API->Free) ---- */
  char *(*ValuePrintToString)(NevercValueRef V);
  char *(*TypePrintToString)(NevercTypeRef T);

  /* ---- Additional Value kind queries ---- */
  int (*ValueIsConstantExpr)(NevercValueRef V);
  int (*ValueIsAlias)(NevercValueRef V);

  /* ---- Compilation mode queries (avoid breaking shellcode extraction) ----
     Returns 1 when the current compilation is producing position-independent
     shellcode (via -fshellcode).  Plugin passes that introduce external
     symbols, relocations, or other shellcode-unfriendly constructs should
     check this and skip themselves when shellcode mode is active.        */
  int (*HostIsShellcodeMode)(void);
  /* The configured shellcode entry symbol (or empty string if not set or
     shellcode mode is off).  The returned string is host-owned and valid
     while the host process is alive.                                      */
  const char *(*HostGetShellcodeEntrySymbol)(void);

  /* ---- Intrinsic lookup (find LLVM intrinsic function declarations) ----
     IntrinsicID uses LLVM's Intrinsic::ID numbering.  Pass 0 OverloadTys
     for non-overloaded intrinsics like llvm.debugtrap.                    */
  NevercValueRef (*IntrinsicGetDeclaration)(NevercModuleRef M,
                                            unsigned IntrinsicID,
                                            NevercTypeRef *OverloadTys,
                                            unsigned NumTys);
  /* Returns a host-allocated string.  Caller MUST free via API->Free.     */
  char *(*IntrinsicGetName)(unsigned IntrinsicID);
  int (*IntrinsicIsOverloaded)(unsigned IntrinsicID);
  /* Look up an intrinsic ID by name (e.g. "llvm.memcpy").
     Returns 0 (Intrinsic::not_intrinsic) if not found.                   */
  unsigned (*IntrinsicLookupByName)(const char *Name);

  /* ---- NamedMetadata (module-level metadata nodes like llvm.module.flags,
         llvm.ident, llvm.dbg.cu, etc.) ---- */
  NevercNamedMDRef (*ModuleGetNamedMetadata)(NevercModuleRef M,
                                             const char *Name);
  NevercNamedMDRef (*ModuleGetOrInsertNamedMetadata)(NevercModuleRef M,
                                                     const char *Name);
  unsigned (*NamedMDGetNumOperands)(NevercNamedMDRef NMD);
  NevercMetadataRef (*NamedMDGetOperand)(NevercNamedMDRef NMD, unsigned Idx);
  void (*NamedMDAddOperand)(NevercNamedMDRef NMD, NevercMetadataRef MD);

  /* ---- MetadataAsValue / ValueAsMetadata conversions ----
     Needed when instructions reference metadata (e.g. dbg.value) or when
     metadata wraps a constant value.                                      */
  NevercValueRef (*MetadataAsValue)(NevercContextRef C, NevercMetadataRef MD);
  NevercMetadataRef (*ValueAsMetadata)(NevercValueRef V);

  /* ---- Plugin argument access (from -fplugin-pass-arg=key=value) ----
     Plugins use these to read configuration from the command line without
     needing to parse argv themselves.  Keys are case-sensitive.
     PluginGetArg returns the value string or NULL if not found.
     Returned string is host-owned and valid for the process lifetime.     */
  const char *(*PluginGetArg)(const char *Key);
  int (*PluginHasArg)(const char *Key);
  unsigned (*PluginGetArgCount)(void);

  /* ---- BasicBlock reordering (needed for control-flow flattening,
         opaque predicate insertion, and CFG layout manipulation) ----
     MoveAfter(BB, AfterBB): moves BB to be immediately after AfterBB.
     MoveBefore(BB, BeforeBB): moves BB to be immediately before BeforeBB.
     Both operate within the same parent function.                        */
  void (*BBMoveAfter)(NevercBasicBlockRef BB, NevercBasicBlockRef AfterBB);
  void (*BBMoveBefore)(NevercBasicBlockRef BB, NevercBasicBlockRef BeforeBB);

  /* ---- Memory intrinsic builders (memcpy, memset, memmove) ----
     These generate LLVM intrinsic calls rather than libc calls, so they
     remain valid in shellcode mode where libc is unavailable.
     Align is in bytes (0 = unaligned).  IsVolatile controls the volatile
     flag on the generated memory access.                                 */
  NevercValueRef (*BuildMemCpy)(NevercBuilderRef B,
                                NevercValueRef Dst, unsigned DstAlign,
                                NevercValueRef Src, unsigned SrcAlign,
                                NevercValueRef Len, int IsVolatile);
  NevercValueRef (*BuildMemSet)(NevercBuilderRef B,
                                NevercValueRef Dst, unsigned DstAlign,
                                NevercValueRef Val,
                                NevercValueRef Len, int IsVolatile);
  NevercValueRef (*BuildMemMove)(NevercBuilderRef B,
                                 NevercValueRef Dst, unsigned DstAlign,
                                 NevercValueRef Src, unsigned SrcAlign,
                                 NevercValueRef Len, int IsVolatile);

  /* ---- DataLayout queries (essential for any optimisation pass) ----
     All size/alignment queries use the Module's DataLayout.  Results are
     in BITS unless the name says "Bytes" or "AllocSize".
     Returns 0 on error (null args, opaque types, etc.).             */
  uint64_t (*TypeSizeInBits)(NevercModuleRef M, NevercTypeRef T);
  uint64_t (*TypeAllocSize)(NevercModuleRef M, NevercTypeRef T);
  uint64_t (*TypeStoreSize)(NevercModuleRef M, NevercTypeRef T);
  unsigned (*TypeABIAlignment)(NevercModuleRef M, NevercTypeRef T);
  unsigned (*TypePrefAlignment)(NevercModuleRef M, NevercTypeRef T);
  unsigned (*PointerSizeInBits)(NevercModuleRef M, unsigned AddrSpace);

  /* ---- PoisonValue (modern LLVM replacement for undef) ---- */
  NevercValueRef (*ConstPoison)(NevercTypeRef Ty);
  int (*ValueIsPoison)(NevercValueRef V);

  /* ---- Address-space-aware pointer types ---- */
  NevercTypeRef (*TypeGetPtrInAddrSpace)(NevercContextRef C,
                                         unsigned AddrSpace);
  unsigned (*TypeGetPointerAddrSpace)(NevercTypeRef T);

  /* ---- Vector types ---- */
  NevercTypeRef (*TypeGetFixedVector)(NevercTypeRef ElemTy, unsigned Count);
  int (*TypeIsVector)(NevercTypeRef T);
  unsigned (*TypeGetVectorNumElements)(NevercTypeRef T);
  NevercTypeRef (*TypeGetVectorElementType)(NevercTypeRef T);
  NevercValueRef (*ConstVector)(NevercValueRef *Vals, unsigned Count);

  /* ---- Vector element operations ---- */
  NevercValueRef (*BuildExtractElement)(NevercBuilderRef B, NevercValueRef Vec,
                                        NevercValueRef Idx, const char *Name);
  NevercValueRef (*BuildInsertElement)(NevercBuilderRef B, NevercValueRef Vec,
                                       NevercValueRef Val, NevercValueRef Idx,
                                       const char *Name);
  NevercValueRef (*BuildShuffleVector)(NevercBuilderRef B, NevercValueRef V1,
                                       NevercValueRef V2,
                                       int *MaskVals, unsigned MaskLen,
                                       const char *Name);

  /* ---- Freeze instruction (needed to safely use values that may be poison
         -- e.g., branch on a potentially-poison condition without UB) ---- */
  NevercValueRef (*BuildFreeze)(NevercBuilderRef B, NevercValueRef V,
                                const char *Name);

  /* ---- AddrSpaceCast (cast pointer between address spaces) ---- */
  NevercValueRef (*BuildAddrSpaceCast)(NevercBuilderRef B, NevercValueRef V,
                                       NevercTypeRef DestTy,
                                       const char *Name);

  /* ---- Aggregate zero initializer (e.g. for zeroing structs, arrays) ---- */
  NevercValueRef (*ConstZeroInitializer)(NevercTypeRef Ty);

  /* ---- GlobalValue unnamed_addr (enables merging of identical globals
         and internalisation optimisations) ---- */
  int (*GlobalHasUnnamedAddr)(NevercValueRef GV);
  void (*GlobalSetUnnamedAddr)(NevercValueRef GV, int HasUnnamedAddr);

  /* ---- Reverse function iteration (complements ModuleGetLastFunction) ---- */
  NevercValueRef (*ModuleGetPrevFunction)(NevercValueRef F);

  /* ---- Reverse alias iteration (complements ModuleGetFirstAlias) ---- */
  NevercValueRef (*ModuleGetLastAlias)(NevercModuleRef M);
  NevercValueRef (*ModuleGetPrevAlias)(NevercValueRef A);

  /* ---- String utilities (host-allocated results; caller frees via Free) ----
     These exist so that plugins never need to call CRT string functions
     directly, keeping everything routed through the host allocator.       */
  uint64_t (*StrLen)(const char *S);
  char *(*StrDup)(const char *S);
  char *(*StrConcat)(const char *A, const char *B);
  int (*StrEqual)(const char *A, const char *B);

  /* Integer-to-string conversion (host-allocated; caller frees via Free). */
  char *(*IntToStr)(int64_t Val);
  char *(*UIntToStr)(uint64_t Val);

  /* Printf-style formatting (host-allocated; caller frees via Free).
     Supports standard printf format specifiers (%s, %d, %u, %ld, %lu,
     %lld, %llu, %x, %p, %c, %f, %% etc.).                              */
  char *(*StrFormat)(const char *Fmt, ...);
  /* va_list version for wrappers / forwarding.                           */
  char *(*StrFormatV)(const char *Fmt, va_list Args);

  /* ---- Raw memory utilities (safe across CRT boundaries) ---- */
  void (*MemCopy)(void *Dst, const void *Src, uint64_t Len);
  void (*MemSet)(void *Dst, int Val, uint64_t Len);
  void (*MemMove)(void *Dst, const void *Src, uint64_t Len);
  int (*MemCompare)(const void *A, const void *B, uint64_t Len);

  /* ---- NSW/NUW arithmetic (overflow flag variants) ---- */
  NevercValueRef (*BuildNSWAdd)(NevercBuilderRef B, NevercValueRef LHS,
                                NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildNUWAdd)(NevercBuilderRef B, NevercValueRef LHS,
                                NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildNSWSub)(NevercBuilderRef B, NevercValueRef LHS,
                                NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildNUWSub)(NevercBuilderRef B, NevercValueRef LHS,
                                NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildNSWMul)(NevercBuilderRef B, NevercValueRef LHS,
                                NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildNUWMul)(NevercBuilderRef B, NevercValueRef LHS,
                                NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildExactSDiv)(NevercBuilderRef B, NevercValueRef LHS,
                                   NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildExactUDiv)(NevercBuilderRef B, NevercValueRef LHS,
                                   NevercValueRef RHS, const char *Name);
  NevercValueRef (*BuildNSWNeg)(NevercBuilderRef B, NevercValueRef V,
                                const char *Name);

  /* ---- Exception handling instructions ---- */
  NevercValueRef (*BuildInvoke)(NevercBuilderRef B, NevercTypeRef FnTy,
                                NevercValueRef Fn, NevercValueRef *Args,
                                unsigned ArgCount,
                                NevercBasicBlockRef NormalDest,
                                NevercBasicBlockRef UnwindDest,
                                const char *Name);
  NevercValueRef (*BuildLandingPad)(NevercBuilderRef B, NevercTypeRef Ty,
                                    unsigned NumClauses, const char *Name);
  void (*LandingPadAddClause)(NevercValueRef LPad, NevercValueRef ClauseVal);
  void (*LandingPadSetCleanup)(NevercValueRef LPad, int IsCleanup);
  NevercValueRef (*BuildResume)(NevercBuilderRef B, NevercValueRef Val);

  /* ---- Thread-local global variables ---- */
  int (*GlobalIsThreadLocal)(NevercValueRef GV);
  void (*GlobalSetThreadLocal)(NevercValueRef GV, int IsThreadLocal);

  /* ---- Function enum attributes (Attribute::AttrKind numbering) ---- */
  void (*FunctionAddEnumAttr)(NevercValueRef F, unsigned AttrKind);
  int (*FunctionHasEnumAttr)(NevercValueRef F, unsigned AttrKind);
  void (*FunctionRemoveEnumAttr)(NevercValueRef F, unsigned AttrKind);

  /* ---- COMDAT support (Windows linkage deduplication) ---- */
  NevercComdatRef (*ModuleGetOrInsertComdat)(NevercModuleRef M,
                                             const char *Name);
  unsigned (*ComdatGetSelectionKind)(NevercComdatRef C);
  void (*ComdatSetSelectionKind)(NevercComdatRef C, unsigned Kind);
  void (*GlobalSetComdat)(NevercValueRef GV, NevercComdatRef C);
  NevercComdatRef (*GlobalGetComdat)(NevercValueRef GV);

  /* ---- Instruction movement ---- */
  void (*InstMoveAfter)(NevercValueRef I, NevercValueRef After);

  /* ---- Constant aggregate element access ---- */
  NevercValueRef (*ConstGetAggregateElement)(NevercValueRef Agg, unsigned Idx);

  /* ---- MIR extended navigation ---- */
  NevercMachineInstrRef (*MBBGetLastInst)(NevercMachineBBRef MBB);
  NevercMachineInstrRef (*MBBGetPrevInst)(NevercMachineInstrRef MI);
  NevercMachineBBRef (*MFuncGetLastBB)(NevercMachineFuncRef MF);
  NevercMachineBBRef (*MFuncGetPrevBB)(NevercMachineBBRef MBB);

  /* ---- MIR operand mutation ---- */
  void (*MInstSetOperandReg)(NevercMachineInstrRef MI, unsigned Idx,
                             unsigned Reg);
  void (*MInstSetOperandImm)(NevercMachineInstrRef MI, unsigned Idx,
                             int64_t Val);

  /* ---- MIR instruction flags & properties ---- */
  unsigned (*MInstGetFlags)(NevercMachineInstrRef MI);
  void (*MInstSetFlags)(NevercMachineInstrRef MI, unsigned Flags);
  int (*MInstIsBranch)(NevercMachineInstrRef MI);
  int (*MInstIsCall)(NevercMachineInstrRef MI);
  int (*MInstIsReturn)(NevercMachineInstrRef MI);
  int (*MInstIsTerminator)(NevercMachineInstrRef MI);
  int (*MInstIsMoveImmediate)(NevercMachineInstrRef MI);
  int (*MInstHasDelaySlot)(NevercMachineInstrRef MI);
  const char *(*MInstGetDesc)(NevercMachineInstrRef MI);

  /* ---- MIR register queries ---- */
  int (*MInstOperandIsVirtReg)(NevercMachineInstrRef MI, unsigned Idx);
  int (*MInstOperandIsPhysReg)(NevercMachineInstrRef MI, unsigned Idx);
  int (*MInstOperandIsDef)(NevercMachineInstrRef MI, unsigned Idx);
  int (*MInstOperandIsUse)(NevercMachineInstrRef MI, unsigned Idx);

  /* ---- MIR instruction insertion / movement ---- */
  void (*MInstMoveBefore)(NevercMachineInstrRef MI,
                          NevercMachineInstrRef Before);
  unsigned (*MBBGetInstCount)(NevercMachineBBRef MBB);
  unsigned (*MFuncGetBBCount)(NevercMachineFuncRef MF);

  /* ---- Linker pass registration ---- */
  void (*RegisterLinkerPass)(void *Registrar, NevercHookPoint Hook,
                             NevercLinkerPassFn Fn, void *UserData,
                             const char *PassName);

  /* ---- Linker symbol queries (valid during LINK_* hooks) ----
     Symbol iteration returns NULL at end of list.  Names point to
     internal storage valid for the duration of the linker pass.    */
  NevercLinkerSymbolRef (*LinkGetFirstSymbol)(void);
  NevercLinkerSymbolRef (*LinkGetNextSymbol)(NevercLinkerSymbolRef S);
  NevercLinkerSymbolRef (*LinkFindSymbol)(const char *Name);
  const char *(*LinkSymbolGetName)(NevercLinkerSymbolRef S);
  uint64_t (*LinkSymbolGetValue)(NevercLinkerSymbolRef S);
  uint64_t (*LinkSymbolGetSize)(NevercLinkerSymbolRef S);
  int (*LinkSymbolIsDefined)(NevercLinkerSymbolRef S);
  int (*LinkSymbolIsLocal)(NevercLinkerSymbolRef S);
  int (*LinkSymbolIsHidden)(NevercLinkerSymbolRef S);
  void (*LinkSymbolSetVisibilityHidden)(NevercLinkerSymbolRef S,
                                        int IsHidden);

  /* ---- Linker section queries (valid during LINK_* hooks) ---- */
  NevercLinkerSectionRef (*LinkGetFirstSection)(void);
  NevercLinkerSectionRef (*LinkGetNextSection)(NevercLinkerSectionRef S);
  NevercLinkerSectionRef (*LinkFindSection)(const char *Name);
  const char *(*LinkSectionGetName)(NevercLinkerSectionRef S);
  uint64_t (*LinkSectionGetSize)(NevercLinkerSectionRef S);
  uint64_t (*LinkSectionGetAlignment)(NevercLinkerSectionRef S);
  unsigned (*LinkSectionGetFlags)(NevercLinkerSectionRef S);

  /* ---- Linker output info ---- */
  const char *(*LinkGetOutputPath)(void);
  unsigned (*LinkGetOutputFormat)(void);

  /* ---- Formatted diagnostics (printf-style: format + emit in one call,
         no manual StrFormat/Free dance needed) ---- */
  void (*DiagNoteF)(const char *Fmt, ...);
  void (*DiagWarningF)(const char *Fmt, ...);
  void (*DiagErrorF)(const char *Fmt, ...);
  void (*DiagNoteV)(const char *Fmt, va_list Args);
  void (*DiagWarningV)(const char *Fmt, va_list Args);
  void (*DiagErrorV)(const char *Fmt, va_list Args);

  /* ---- Zero-initialized allocation (calloc equivalent) ---- */
  void *(*AllocZeroed)(uint64_t Count, uint64_t ElemSize);

  /* ---- Extended string operations ---- */
  int (*StrStartsWith)(const char *S, const char *Prefix);
  int (*StrEndsWith)(const char *S, const char *Suffix);
  int (*StrContains)(const char *Haystack, const char *Needle);
  int (*StrCompare)(const char *A, const char *B);
  int (*StrToInt64)(const char *S, int64_t *Out);
  int (*StrToUInt64)(const char *S, uint64_t *Out);
  char *(*StrSubstring)(const char *S, uint64_t Start, uint64_t Len);
  char *(*StrTrim)(const char *S);

  /* ---- Convenience zero-fill (equivalent to MemSet(Dst, 0, Len)) ---- */
  void (*MemZero)(void *Dst, uint64_t Len);

  /* ---- Character search: returns byte offset, or NEVERC_NPOS if not
         found.  These mirror strchr / strrchr semantics. ---- */
  uint64_t (*StrFindChar)(const char *S, int C);
  uint64_t (*StrFindLastChar)(const char *S, int C);

  /* ---- Overflow-safe array reallocation.  Returns NULL (without freeing
         Ptr) on Count*ElemSize overflow or allocation failure. ---- */
  void *(*ReallocArray)(void *Ptr, uint64_t Count, uint64_t ElemSize);

  /* ---- Batch collection (fills a pre-allocated buffer in ONE vtable call,
         eliminating per-element indirect-call overhead).
         Caller sizes the buffer via ModuleGetFunctionCount /
         ModuleGetGlobalCount / FunctionGetBBCount / BBGetInstCount. ---- */
  void (*ModuleCollectFunctions)(NevercModuleRef M, NevercValueRef *Out);
  void (*ModuleCollectGlobals)(NevercModuleRef M, NevercValueRef *Out);
  void (*FunctionCollectBBs)(NevercValueRef F, NevercBasicBlockRef *Out);
  void (*BBCollectInstructions)(NevercBasicBlockRef BB, NevercValueRef *Out);

  /* ---- MIR batch collection (same pattern as IR batch above).
         Caller sizes via MFuncGetBBCount / MBBGetInstCount. ---- */
  void (*MFuncCollectBBs)(NevercMachineFuncRef MF, NevercMachineBBRef *Out);
  void (*MBBCollectInstructions)(NevercMachineBBRef MBB,
                                 NevercMachineInstrRef *Out);

  /* ---- Substring search: returns byte offset of the first occurrence of
         Needle in Haystack, or NEVERC_NPOS if not found.  Complements
         StrFindChar (single char) and StrContains (bool). ---- */
  uint64_t (*StrFindStr)(const char *Haystack, const char *Needle);

  /* ---- Hook-point name lookup (returns a static string, never NULL) ---- */
  const char *(*HookPointGetName)(unsigned Hook);

  /* ---- Memory duplicate: allocate Len bytes and copy Src into it.
         Returns NULL on allocation failure.  Caller frees via Free. ---- */
  void *(*MemDup)(const void *Src, uint64_t Len);

  /* ---- String replacement (host-allocated result; caller frees via Free).
         StrReplace replaces the FIRST occurrence of Old with New.
         StrReplaceAll replaces ALL occurrences.
         Returns a copy of S unchanged if Old is not found or is empty. ---- */
  char *(*StrReplace)(const char *S, const char *Old, const char *New);
  char *(*StrReplaceAll)(const char *S, const char *Old, const char *New);

  /* ---- Case conversion (host-allocated result; caller frees via Free).
         Operates on ASCII characters only (0x41-0x5A / 0x61-0x7A). ---- */
  char *(*StrToLower)(const char *S);
  char *(*StrToUpper)(const char *S);

  /* ---- Linker output format name (returns a static string, never NULL).
         Convenience wrapper around LinkGetOutputFormat; avoids repeated
         switch/if-else chains in every plugin.  Returns "ELF", "COFF",
         "Mach-O", or "unknown". ---- */
  const char *(*LinkGetOutputFormatName)(void);

  /* ---- Alias count + batch collection (completes the Module batch API set:
         functions, globals, aliases all have count + collect). ---- */
  unsigned (*ModuleGetAliasCount)(NevercModuleRef M);
  void (*ModuleCollectAliases)(NevercModuleRef M, NevercValueRef *Out);

  /* ---- One-call batch collection (host allocates; caller frees via Free).
         Returns NULL and sets *OutCount to 0 when the module has no
         matching items.  Eliminates the count->alloc->fill dance; the
         host iterates LLVM data structures directly with zero vtable
         overhead.
         ModuleCollectAllInstructions returns every Instruction in every
         defined (non-declaration) Function, flattened into a single
         contiguous array for cache-friendly linear scans. ---- */
  NevercValueRef *(*ModuleCollectAllFunctions)(NevercModuleRef M,
                                               unsigned *OutCount);
  NevercValueRef *(*ModuleCollectAllGlobals)(NevercModuleRef M,
                                             unsigned *OutCount);
  NevercValueRef *(*ModuleCollectAllInstructions)(NevercModuleRef M,
                                                  unsigned *OutCount);

  /* ---- String join: concatenate an array of strings with a separator.
         Returns a host-allocated string; caller frees via Free.
         Sep may be NULL or "" for no separator.
         Returns "" (allocated) when Count == 0. ---- */
  char *(*StrJoin)(const char *const *Strings, unsigned Count,
                   const char *Sep);

  /* ---- String split: split S by Delim into an array of host-allocated
         strings.  *OutCount receives the number of parts.  Returns NULL
         on allocation failure.  Each element AND the array itself must be
         freed via Free.  Empty Delim or NULL Delim returns a single-element
         array containing a copy of S. ---- */
  char **(*StrSplit)(const char *S, const char *Delim, unsigned *OutCount);

  /* ---- Fast string hash (xxh3 64-bit, SIMD-accelerated).  Useful for
         building lookup tables or fast string comparisons in plugins.
         Returns 0 for NULL input. ---- */
  uint64_t (*StrHash)(const char *S);

  /* ---- One-call batch collection of DEFINED functions only.
         Skips declarations on the host side -- eliminates the
         ubiquitous collect->filter->loop->free boilerplate in plugins.
         Returns NULL and sets *OutCount to 0 when no defined functions
         exist.  Caller frees via Free. ---- */
  NevercValueRef *(*ModuleCollectDefinedFunctions)(NevercModuleRef M,
                                                   unsigned *OutCount);

  /* ---- Sort / binary-search (routed through the host so plugin code
         never touches CRT qsort/bsearch across DLL boundaries) ----
     Sort is NOT stable.  Cmp returns <0, 0, >0 like strcmp.            */
  void (*Sort)(void *Base, uint64_t NumElements, uint64_t ElemSize,
               int (*Cmp)(const void *A, const void *B));
  const void *(*BSearch)(const void *Key, const void *Base,
                         uint64_t NumElements, uint64_t ElemSize,
                         int (*Cmp)(const void *A, const void *B));

  /* ---- snprintf to caller-owned buffer (zero allocation).
         Returns the number of characters that WOULD have been written
         (excluding the null terminator), regardless of BufSize.
         If return value >= BufSize, the output was truncated.
         Buf may be NULL when BufSize is 0 (dry-run to measure).       */
  int (*StrFormatBuf)(char *Buf, uint64_t BufSize, const char *Fmt, ...);
  int (*StrFormatBufV)(char *Buf, uint64_t BufSize, const char *Fmt,
                       va_list Args);

  /* ---- Bounded string compare (strncmp equivalent).
         Compares at most MaxLen bytes.  Returns <0, 0, >0. ---- */
  int (*StrNCompare)(const char *A, const char *B, uint64_t MaxLen);

  /* ---- Copy string into caller-owned buffer (strlcpy semantics).
         Always null-terminates when BufSize > 0.  Returns strlen(Src)
         so the caller can detect truncation (result >= BufSize).
         Zero allocation -- use instead of StrDup when a stack buffer
         suffices, or instead of StrFormatBuf(buf, n, "%s", s). ---- */
  uint64_t (*StrCopyBuf)(char *Buf, uint64_t BufSize, const char *Src);

  /* ---- Dynamic array (opaque, host-allocated, type-erased) ----
     A growable contiguous buffer for elements of uniform size.
     Internally: 2x geometric growth, cache-friendly linear layout.
     All memory goes through the host allocator -- CRT-safe on Windows.

     Lifecycle: Create -> Push/Reserve/Sort -> read via Get/Data -> Destroy.
     Detach transfers ownership of the raw buffer to the caller (free
     via API->Free); the array handle itself is destroyed.              */
  NevercDynArrayRef (*DynArrayCreate)(uint64_t ElemSize);
  void (*DynArrayDestroy)(NevercDynArrayRef Arr);
  int (*DynArrayPush)(NevercDynArrayRef Arr, const void *Elem);
  void *(*DynArrayGet)(NevercDynArrayRef Arr, unsigned Idx);
  unsigned (*DynArrayCount)(NevercDynArrayRef Arr);
  void *(*DynArrayData)(NevercDynArrayRef Arr);
  void (*DynArrayClear)(NevercDynArrayRef Arr);
  void (*DynArraySort)(NevercDynArrayRef Arr,
                       int (*Cmp)(const void *, const void *));
  /* Returns a pointer to the removed tail element.  The pointer is valid
     only until the next mutation (Push/Reserve/Sort/Destroy).  Copy
     the data immediately if you need it beyond that. */
  void *(*DynArrayPop)(NevercDynArrayRef Arr);
  int (*DynArrayReserve)(NevercDynArrayRef Arr, unsigned MinCapacity);
  void *(*DynArrayDetach)(NevercDynArrayRef Arr, unsigned *OutCount);

  /* ---- String map (opaque, host-allocated, StringMap<uint64_t>) ----
     A fast string-keyed hash table storing uint64_t values.  Keys are
     copied internally; the caller's key pointer need not remain valid.
     Use the value as an integer counter, a pointer (via cast), or an
     opaque tag -- whatever fits in 64 bits.

     Backed by LLVM's StringMap for cache-friendly, allocation-dense
     bucket storage with quadratic probing.                             */
  NevercStrMapRef (*StrMapCreate)(void);
  void (*StrMapDestroy)(NevercStrMapRef Map);
  int (*StrMapPut)(NevercStrMapRef Map, const char *Key, uint64_t Value);
  int (*StrMapGet)(NevercStrMapRef Map, const char *Key, uint64_t *OutValue);
  int (*StrMapHas)(NevercStrMapRef Map, const char *Key);
  void (*StrMapRemove)(NevercStrMapRef Map, const char *Key);
  unsigned (*StrMapCount)(NevercStrMapRef Map);
  /* Zero-allocation callback iteration.  Fn is called for each entry;
     returning nonzero from Fn aborts the traversal early.
     Iteration order is unspecified (hash table internal order).
     IMPORTANT: Do NOT modify the map (Put/Remove/Increment) inside Fn --
     insertions may trigger a rehash that invalidates internal iterators. */
  void (*StrMapForEach)(NevercStrMapRef Map,
                        int (*Fn)(const char *Key, uint64_t Value, void *Ctx),
                        void *Ctx);
  /* Collect all keys into a host-allocated array of host-allocated strings.
     Caller frees each key AND the array itself via Free.
     Returns NULL and sets *OutCount to 0 when the map is empty. */
  char **(*StrMapCollectKeys)(NevercStrMapRef Map, unsigned *OutCount);
  /* Single-lookup increment (or initialize to Delta) the value for Key.
     One hash probe instead of separate Get+Put -- 2x fewer lookups for
     counting patterns.  Returns the new value after adding Delta.
     If Key did not exist, inserts it with value = Delta.  NOT thread-safe. */
  uint64_t (*StrMapIncrement)(NevercStrMapRef Map, const char *Key,
                              uint64_t Delta);

  /* ---- String builder (opaque, host-allocated, SmallString<256>) ----
     Efficient incremental string construction without repeated
     alloc/format/concat/free.  Inline buffer avoids heap allocation
     for strings <= 256 bytes; grows geometrically beyond that.

     Finish returns a host-allocated null-terminated copy; caller frees
     via API->Free.  The builder can be reused after Finish or Clear.   */
  NevercStrBuilderRef (*StrBuilderCreate)(void);
  void (*StrBuilderDestroy)(NevercStrBuilderRef SB);
  void (*StrBuilderAppend)(NevercStrBuilderRef SB, const char *S);
  void (*StrBuilderAppendN)(NevercStrBuilderRef SB, const char *S,
                            uint64_t Len);
  void (*StrBuilderAppendChar)(NevercStrBuilderRef SB, char C);
  void (*StrBuilderAppendF)(NevercStrBuilderRef SB, const char *Fmt, ...);
  void (*StrBuilderAppendV)(NevercStrBuilderRef SB, const char *Fmt,
                            va_list Args);
  char *(*StrBuilderFinish)(NevercStrBuilderRef SB);
  uint64_t (*StrBuilderLen)(NevercStrBuilderRef SB);
  void (*StrBuilderClear)(NevercStrBuilderRef SB);

  /* ---- DynArray batch / mutate operations ----
     PushN copies Count elements from Data in one call -- eliminates
     N vtable calls, N bounds checks, and N growth checks.  Single
     geometric growth to accommodate all elements.
     RemoveSwap does O(1) unordered removal: copies the last element
     into position Idx, then decrements count.  Invalidates any pointer
     returned by DynArrayGet for position Idx or the former last element.
     ShrinkToFit reallocs the backing buffer to exactly Count * ElemSize,
     releasing unused capacity.  No-op when Count == Capacity.
     When Count == 0, frees the backing buffer entirely. */
  int (*DynArrayPushN)(NevercDynArrayRef Arr, const void *Data,
                       unsigned Count);
  void (*DynArrayRemoveSwap)(NevercDynArrayRef Arr, unsigned Idx);
  void (*DynArrayShrinkToFit)(NevercDynArrayRef Arr);

  /* ---- StrMap with pre-allocated capacity ----
     Same as StrMapCreate but reserves bucket space for at least
     InitialCapacity entries.  Avoids rehashing when the approximate
     entry count is known upfront -- single allocation for all buckets. */
  NevercStrMapRef (*StrMapCreateSized)(unsigned InitialCapacity);

  /* ---- Integer-keyed hash table (opaque, host-allocated, DenseMap) ----
     A fast integer-keyed hash table storing uint64_t values.  Ideal for
     opcode counting, instruction-ID mapping, and other numeric-keyed
     lookups -- avoids the integer-to-string conversion overhead of StrMap.

     Backed by LLVM DenseMap: open addressing with quadratic probing,
     cache-friendly contiguous buckets, O(1) amortized lookup/insert.

     RESERVED KEYS: 0xFFFFFFFFFFFFFFFF and 0xFFFFFFFFFFFFFFFE are used
     as DenseMap sentinel values and MUST NOT be used as keys.  Put/Get/
     Has/Remove silently ignore them.  All other uint64_t values are
     valid keys.                                                         */
  NevercIntMapRef (*IntMapCreate)(void);
  void (*IntMapDestroy)(NevercIntMapRef Map);
  int (*IntMapPut)(NevercIntMapRef Map, uint64_t Key, uint64_t Value);
  int (*IntMapGet)(NevercIntMapRef Map, uint64_t Key, uint64_t *OutValue);
  int (*IntMapHas)(NevercIntMapRef Map, uint64_t Key);
  void (*IntMapRemove)(NevercIntMapRef Map, uint64_t Key);
  unsigned (*IntMapCount)(NevercIntMapRef Map);
  /* Single-probe increment (or initialize to Delta).  Returns the new
     value after adding Delta.  If Key did not exist, inserts it with
     value = Delta.  NOT thread-safe. */
  uint64_t (*IntMapIncrement)(NevercIntMapRef Map, uint64_t Key,
                              uint64_t Delta);
  /* Zero-allocation callback iteration.  Fn is called for each entry;
     returning nonzero from Fn aborts the traversal early.
     Iteration order is unspecified (hash table internal order).
     IMPORTANT: Do NOT modify the map (Put/Remove/Increment) inside Fn --
     insertions may trigger a rehash that invalidates internal iterators. */
  void (*IntMapForEach)(NevercIntMapRef Map,
                        int (*Fn)(uint64_t Key, uint64_t Value, void *Ctx),
                        void *Ctx);
  void (*IntMapClear)(NevercIntMapRef Map);
  NevercIntMapRef (*IntMapCreateSized)(unsigned InitialCapacity);

  /* ---- DynArray binary search (requires prior DynArraySort) ----
     Returns a pointer to the matching element, or NULL if not found.
     The array MUST be sorted with the same Cmp comparator.  If multiple
     elements match, which one is returned is unspecified. */
  void *(*DynArrayBSearch)(NevercDynArrayRef Arr, const void *Key,
                           int (*Cmp)(const void *, const void *));

  /* ---- IR instruction opcode name ----
     Returns the human-readable name of the instruction's opcode (e.g.
     "add", "load", "call", "br").  The returned pointer is a static
     string valid for the lifetime of the process -- never free it.
     Returns "" for null or non-instruction values. */
  const char *(*InstGetOpcodeName)(NevercValueRef I);

  /* Opcode number -> name without needing an instruction handle.
     Useful inside IntMapForEach callbacks where only the numeric key
     is available.  Returns a static string; never free it.
     Returns "" for out-of-range opcodes. */
  const char *(*InstOpcodeToName)(unsigned Opcode);

  /* ---- Bounded string duplication (strndup equivalent) ----
     Copies at most MaxLen bytes from S into a host-allocated null-
     terminated string.  If S is shorter than MaxLen, the full string
     is copied.  Returns NULL on allocation failure or NULL input.
     Caller frees via Free.  Unlike the stack-buffer StrCopyBuf
     pattern, this handles arbitrary lengths without truncation. */
  char *(*StrNDup)(const char *S, uint64_t MaxLen);

  /* ---- Character occurrence count ----
     Returns the number of times byte C appears in S.
     Returns 0 for NULL input.  Useful for pre-sizing arrays
     before StrSplit, or counting delimiters in paths/names. */
  uint64_t (*StrCountChar)(const char *S, int C);

  /* ---- N-bounded StrMap operations (key pointer + length) ----
     Same semantics as StrMapPut/Get/Has/Increment/Remove but accept
     a (Key, KeyLen) pair instead of requiring a null-terminated key.
     This eliminates the need for temporary string allocation when the
     caller already knows the key bounds (e.g. a substring of a larger
     string).  The key data is copied into the map on insert, so the
     caller's buffer need not outlive the call. */
  int (*StrMapPutN)(NevercStrMapRef Map, const char *Key,
                    uint64_t KeyLen, uint64_t Value);
  int (*StrMapGetN)(NevercStrMapRef Map, const char *Key,
                    uint64_t KeyLen, uint64_t *OutValue);
  int (*StrMapHasN)(NevercStrMapRef Map, const char *Key, uint64_t KeyLen);
  uint64_t (*StrMapIncrementN)(NevercStrMapRef Map, const char *Key,
                               uint64_t KeyLen, uint64_t Delta);
  void (*StrMapRemoveN)(NevercStrMapRef Map, const char *Key, uint64_t KeyLen);

  /* ---- Prefix-skip helper (zero allocation, no temporary buffer) ----
     If S starts with Prefix, return the position immediately AFTER the
     prefix (S + strlen(Prefix)).  Otherwise return NULL.  This eliminates
     the common StrStartsWith + hardcoded-offset pattern, e.g.
         if (!API->StrStartsWith(Name, "llvm.")) continue;
         const char *After = Name + 5;       // <-- magic number
     becomes
         const char *After = API->StrAfterPrefix(Name, "llvm.");
         if (!After) continue;
     Returns S unchanged when Prefix is NULL or empty. */
  const char *(*StrAfterPrefix)(const char *S, const char *Prefix);

  /* ---- Memory needle search (memmem equivalent) ----
     Locate the first occurrence of NeedleLen bytes from Needle inside
     HaystackLen bytes of Haystack.  Returns the byte offset (not a
     pointer -- offsets survive Haystack relocation, e.g. inside a
     binary-pass BinaryResize).  Returns NEVERC_NPOS when not found
     or on any NULL/empty input.  An empty needle is treated as not
     found to avoid the libc divergence around memmem("", 0).  Useful
     for binary passes scanning shellcode for byte signatures. */
  uint64_t (*MemFind)(const void *Haystack, uint64_t HaystackLen,
                      const void *Needle, uint64_t NeedleLen);

  /* ---- Byte occurrence count (paired with MemFind) ----
     Count how many times Byte (low 8 bits used) appears in HaystackLen
     bytes of Haystack.  Returns 0 on NULL or zero-length input.  Single-
     byte sweep delegated to libc memchr -- typically SIMD-accelerated
     and faster than a hand-written branchless loop in the plugin. */
  uint64_t (*MemCount)(const void *Haystack, uint64_t HaystackLen, int Byte);

  /* ---- Character class scanning (strspn / strcspn equivalents) ----
     StrSpan returns the length of the initial segment of S consisting
     entirely of bytes from Accept.  StrCSpn returns the length of the
     initial segment that contains NO bytes from Reject.
     Both return 0 when S is NULL or empty.  Both treat a NULL/empty
     class as "no characters match" (StrSpan -> 0, StrCSpn -> strlen(S)).
     Lookup uses a 256-bit bitset built from the class on entry; the
     scan itself is one branch per byte. */
  uint64_t (*StrSpan)(const char *S, const char *Accept);
  uint64_t (*StrCSpn)(const char *S, const char *Reject);

  /* ---- ASCII case-insensitive comparison ----
     Compare A and B byte-by-byte after lowercasing ASCII letters
     (0x41-0x5A -> 0x61-0x7A).  Non-ASCII bytes compared verbatim.
     StrIEqual returns 1 when equal, 0 otherwise (cheaper than
     StrICompare(...) == 0 because it can short-circuit on length
     mismatch when both arguments are walked from a common origin).
     StrICompare returns <0, 0, >0 like strcmp.
     Both return safe defaults on NULL inputs (0 / inequality).        */
  int (*StrIEqual)(const char *A, const char *B);
  int (*StrICompare)(const char *A, const char *B);

  /* ---- Path manipulation (zero-allocation offset helpers) ----
     Both helpers recognize '/' AND '\\' as separators, so they work
     uniformly on Unix and Windows paths without the plugin author
     having to call StrFindLastChar twice and merge.

     PathBaseNameOffset returns the byte offset to the start of the
     basename (immediately after the last separator).  Returns 0 when
     the path has no separator (the whole string is the basename).
     Returns 0 for NULL input.

     PathExtOffset returns the byte offset to the dot of the file
     extension (e.g. for "src/foo.c" returns 7).  Returns NEVERC_NPOS
     when there is no extension after the basename.  Hidden files like
     ".bashrc" report NEVERC_NPOS (no extension), matching POSIX
     basename(1) intuition.  Returns NEVERC_NPOS for NULL input.       */
  uint64_t (*PathBaseNameOffset)(const char *Path);
  uint64_t (*PathExtOffset)(const char *Path);

  /* ---- Byte-reverse copy ----
     Copy Len bytes from Src into Dst in reverse order.  Two supported
     modes: (1) Dst == Src for in-place reversal (meet-in-the-middle
     swap, no temporary buffer), or (2) Dst and Src ranges fully
     disjoint.  Partial overlap is undefined behaviour and treated as
     a no-op, mirroring memcpy's contract.  Useful for endianness
     flips on big-endian fields embedded in shellcode. */
  void (*MemReverse)(void *Dst, const void *Src, uint64_t Len);

  /* ---- DynArray order-preserving mutation ----
     Insert pushes Elem into Arr at position Idx, shifting elements
     [Idx..Count) right by one.  Idx == Count appends (equivalent to
     Push); Idx > Count is an error and returns 0.  Returns 1 on
     success, 0 on allocation failure or invalid arguments.

     RemoveOrdered erases the element at Idx and shifts elements
     (Idx, Count) left by one to close the gap.  Out-of-range Idx
     is a no-op.  Use RemoveSwap for O(1) unordered deletion when
     iteration order does not matter -- this routine is O(N) due to
     the memmove. */
  int (*DynArrayInsert)(NevercDynArrayRef Arr, unsigned Idx,
                        const void *Elem);
  void (*DynArrayRemoveOrdered)(NevercDynArrayRef Arr, unsigned Idx);

  /* ---- Batch operand-kind collection (MIR hot-path) ----
     Walk all operands of MI and write each operand's kind tag (one of
     the NEVERC_MIR_OP_* enum values) into OutKinds.  Caller sizes the
     buffer to MInstGetNumOperands(MI) bytes.  Returns the actual
     operand count written.

     This collapses the typical N x 3 vtable dispatch (NumOps + IsReg +
     IsImm per operand) into a single vtable call.  On a 1k-instruction
     function with three operands per instruction, that turns 9 000
     indirect calls into 1 000 -- meaningful when MIR analysis is the
     bottleneck.

     Returns 0 on NULL MI / OutKinds.  Bytes beyond the actual operand
     count are left untouched. */
  unsigned (*MInstCollectOperandKinds)(NevercMachineInstrRef MI,
                                       uint8_t *OutKinds);

  /* ---- Batch opcode collection (IR hot-path) ----
     Single-vtable-call equivalents of "iterate Instructions, query each
     opcode".  Useful for opcode histograms / hot-loop analysis where
     plugins don't need the Value handles, only the numeric opcodes.

     BBCollectOpcodes fills OutOpcodes with one entry per instruction in
     BB (caller sizes via BBGetInstCount).  Returns the actual count.

     ModuleCollectAllOpcodes is the module-wide equivalent: returns a
     host-allocated array of opcodes for every Instruction in every
     defined Function, plus *OutCount.  Caller frees via Free.  Returns
     NULL when the module has zero defined instructions.

     Both eliminate the per-instruction vtable hop that
     ModuleCollectAllInstructions + InstGetOpcode would incur. */
  unsigned (*BBCollectOpcodes)(NevercBasicBlockRef BB, unsigned *OutOpcodes);
  unsigned *(*ModuleCollectAllOpcodes)(NevercModuleRef M, unsigned *OutCount);

  /* ---- Function instruction count (convenience, avoids per-BB loop) ----
     Returns the total number of instructions in all basic blocks of F.
     Equivalent to summing BBGetInstCount over every BB, but in a single
     vtable call using the O(1) BB::size() internally.  Returns 0 for
     declarations or NULL input. */
  unsigned (*FunctionGetInstructionCount)(NevercValueRef F);

  /* ---- DominatorTree (on-demand CFG analysis) ----
     Computes the full dominator tree from the function's CFG.  Analysis
     objects are snapshot-in-time: they are NOT updated when the IR is
     modified.  Destroy and rebuild after any IR mutation.

     Returns NULL for declarations, NULL input, or allocation failure.
     The caller MUST call DomTreeDestroy before the pass returns. */
  NevercDomTreeRef (*FunctionBuildDomTree)(NevercValueRef F);
  void (*DomTreeDestroy)(NevercDomTreeRef DT);
  /* Returns 1 if BB A dominates BB B (A == B counts as dominance). */
  int (*DomTreeDominates)(NevercDomTreeRef DT,
                          NevercBasicBlockRef A, NevercBasicBlockRef B);
  /* Returns 1 if A strictly dominates B (A != B required). */
  int (*DomTreeProperlyDominates)(NevercDomTreeRef DT,
                                  NevercBasicBlockRef A,
                                  NevercBasicBlockRef B);
  /* Returns the immediate dominator of BB, or NULL for the entry block. */
  NevercBasicBlockRef (*DomTreeGetIDom)(NevercDomTreeRef DT,
                                        NevercBasicBlockRef BB);
  /* Returns 1 if BB is reachable from the function entry. */
  int (*DomTreeIsReachable)(NevercDomTreeRef DT, NevercBasicBlockRef BB);

  /* ---- LoopInfo (on-demand loop nest analysis) ----
     Detects natural loops using dominance information.  Internally owns
     its own DominatorTree; the plugin does NOT need to build one first.
     Same snapshot-in-time rule as DomTree: rebuild after IR mutation.

     LoopRef handles point into the LoopInfo's internal storage and are
     valid only until LoopInfoDestroy is called.

     Returns NULL for declarations, NULL input, or allocation failure.
     A function with no loops returns a valid handle where
     LoopInfoGetLoopFor returns NULL for all BBs and
     LoopInfoGetTopLevelLoopCount returns 0. */
  NevercLoopInfoRef (*FunctionBuildLoopInfo)(NevercValueRef F);
  void (*LoopInfoDestroy)(NevercLoopInfoRef LI);
  /* Returns the innermost loop containing BB, or NULL if not in a loop. */
  NevercLoopRef (*LoopInfoGetLoopFor)(NevercLoopInfoRef LI,
                                      NevercBasicBlockRef BB);
  /* Number of top-level (outermost) loops in the function. */
  unsigned (*LoopInfoGetTopLevelLoopCount)(NevercLoopInfoRef LI);
  /* Loop header -- the single entry block of the loop. */
  NevercBasicBlockRef (*LoopGetHeader)(NevercLoopRef L);
  /* Nesting depth (1 = outermost, 2 = nested once, etc.).  0 for NULL. */
  unsigned (*LoopGetDepth)(NevercLoopRef L);
  /* Parent loop in the nest, or NULL for top-level loops. */
  NevercLoopRef (*LoopGetParentLoop)(NevercLoopRef L);
  /* Returns 1 if BB is contained in loop L. */
  int (*LoopContains)(NevercLoopRef L, NevercBasicBlockRef BB);
  /* Number of basic blocks in this loop (including sub-loop blocks). */
  unsigned (*LoopGetNumBlocks)(NevercLoopRef L);
  /* Number of direct child sub-loops (not recursively). */
  unsigned (*LoopGetNumSubLoops)(NevercLoopRef L);
  /* Returns 1 if this loop has no sub-loops (leaf in the loop tree). */
  int (*LoopIsInnermost)(NevercLoopRef L);

  /* ---- PostDominatorTree (reverse-CFG dominance analysis) ----
     Same lifecycle rules as DominatorTree: snapshot-in-time, rebuild
     after IR mutation, caller MUST call PostDomTreeDestroy.

     A post-dominates B means every path from B to any function exit
     must pass through A.  Useful for identifying where control flow
     converges (e.g. for opaque predicate cleanup, region analysis). */
  NevercPostDomTreeRef (*FunctionBuildPostDomTree)(NevercValueRef F);
  void (*PostDomTreeDestroy)(NevercPostDomTreeRef PDT);
  int (*PostDomTreeDominates)(NevercPostDomTreeRef PDT,
                              NevercBasicBlockRef A, NevercBasicBlockRef B);
  int (*PostDomTreeProperlyDominates)(NevercPostDomTreeRef PDT,
                                      NevercBasicBlockRef A,
                                      NevercBasicBlockRef B);
  /* Returns the immediate post-dominator, or NULL for exit-like blocks. */
  NevercBasicBlockRef (*PostDomTreeGetIPDom)(NevercPostDomTreeRef PDT,
                                             NevercBasicBlockRef BB);

  /* ---- Function cloning (deep copy of an entire function) ----
     Creates a complete copy of F, inserted into the same Module.
     All instructions, basic blocks, and metadata are duplicated;
     internal references are remapped to the clone's own values.
     The clone is initially given InternalLinkage.
     Returns NULL on failure or if F is a declaration.  The cloned
     function is owned by the Module and does NOT need to be freed. */
  NevercValueRef (*FunctionClone)(NevercValueRef F, const char *NewName);

  /* ---- SCEV (Scalar Evolution -- on-demand loop trip count analysis) ----
     Bundles DominatorTree + LoopInfo + TargetLibraryInfo + AssumptionCache +
     ScalarEvolution internally.  Same snapshot-in-time rule as DomTree:
     rebuild after any IR mutation.  Caller MUST call SCEVInfoDestroy.

     Trip count queries take a LoopHeader BB (not a LoopRef) so the SCEV
     can look up the loop in its own LoopInfo, avoiding cross-LoopInfo
     aliasing when the plugin also uses FunctionBuildLoopInfo separately.

     GetTripCount returns a constant trip count or 0 when the count is not
     a small compile-time constant.  GetMaxTripCount returns a constant
     upper bound or 0 when not computable. */
  NevercSCEVInfoRef (*FunctionBuildSCEV)(NevercValueRef F);
  void (*SCEVInfoDestroy)(NevercSCEVInfoRef SI);
  unsigned (*SCEVGetTripCount)(NevercSCEVInfoRef SI,
                               NevercBasicBlockRef LoopHeader);
  unsigned (*SCEVGetMaxTripCount)(NevercSCEVInfoRef SI,
                                  NevercBasicBlockRef LoopHeader);

  /* ---- CallGraph (on-demand module-wide call graph) ----
     Builds the call graph and pre-computes the set of recursive functions
     (via SCC decomposition) at construction time.  IsRecursive is an O(1)
     set lookup -- covers both direct and indirect (mutual) recursion.

     CollectCallees returns a host-allocated array of distinct callees
     (Function values); caller frees via Free.  Returns NULL and sets
     *OutCount to 0 when F has no callees or is external.

     Caller MUST call CallGraphDestroy before the pass returns. */
  NevercCallGraphRef (*ModuleBuildCallGraph)(NevercModuleRef M);
  void (*CallGraphDestroy)(NevercCallGraphRef CG);
  unsigned (*CallGraphGetCalleeCount)(NevercCallGraphRef CG,
                                      NevercValueRef F);
  NevercValueRef *(*CallGraphCollectCallees)(NevercCallGraphRef CG,
                                             NevercValueRef F,
                                             unsigned *OutCount);
  int (*CallGraphIsRecursive)(NevercCallGraphRef CG, NevercValueRef F);

  /* ---- CFG mutation helpers (edge splitting, block merging) ----
     SplitEdge inserts a new empty BasicBlock on the edge from From to To.
     Returns the new BB, or NULL if From->To is not a valid edge.  Analysis
     objects (DomTree, LoopInfo, SCEV) are NOT updated -- rebuild them
     after mutation.

     MergeBlockIntoPredecessor merges BB into its single predecessor,
     removing BB from the function.  Returns 1 on success, 0 if BB has
     multiple predecessors or other constraints prevent merging.  The
     merged BB handle becomes dangling after a successful merge. */
  NevercBasicBlockRef (*SplitEdge)(NevercBasicBlockRef From,
                                   NevercBasicBlockRef To);
  int (*MergeBlockIntoPredecessor)(NevercBasicBlockRef BB);

  /* ---- Loop invariant check ----
     Returns 1 if V is defined outside Loop L (i.e., V is loop-invariant
     with respect to L).  Constants always return 1.  Returns 0 for NULL
     inputs or if V is defined inside L. */
  int (*LoopIsLoopInvariant)(NevercLoopRef L, NevercValueRef V);
} NevercHostAPI;

/* ---- Convenience: cast a NevercHookPoint to a void* UserData value ----
 * Plugins often store the hook-point enum as UserData so that a single
 * callback can identify which stage it was invoked from.  This macro
 * makes the cast explicit and avoids platform width warnings. */
#define NEVERC_HOOK_UD(hook) ((void *)(uintptr_t)(hook))

/* ---- Convenience: resolve a hook name from a UserData that was set
 *      via NEVERC_HOOK_UD.  Falls back to "<unknown>" when the host
 *      is too old to provide HookPointGetName.  The returned pointer
 *      is a static string -- never free it. ---- */
#define NEVERC_HOOK_NAME(api, ud) \
    (NEVERC_API_FN(api, HookPointGetName) \
     ? (api)->HookPointGetName((unsigned)(uintptr_t)(ud)) \
     : "<unknown>")

/* ---- Convenience: create a StrMap/IntMap with optional pre-allocation ----
 * Uses the Sized variant when the host supports it, otherwise falls back
 * to the default zero-capacity constructor.  cap==0 always uses Create. */
#define NEVERC_STRMAP_NEW(api, cap) \
    (((cap) > 0 && NEVERC_API_FN(api, StrMapCreateSized)) \
     ? (api)->StrMapCreateSized(cap) : (api)->StrMapCreate())

#define NEVERC_INTMAP_NEW(api, cap) \
    (((cap) > 0 && NEVERC_API_FN(api, IntMapCreateSized)) \
     ? (api)->IntMapCreateSized(cap) : (api)->IntMapCreate())

/* -------------------------------------------------------------------------- */
/*  Plugin entry protocol                                                     */
/* -------------------------------------------------------------------------- */

typedef struct NevercPluginInfo {
  uint32_t APIVersion;
  const char *PluginName;
  const char *PluginVersion;
  void (*RegisterPasses)(const NevercHostAPI *API, void *Registrar);
  void (*Destroy)(void); /* optional cleanup, may be NULL */
} NevercPluginInfo;

#define NEVERC_PLUGIN_ENTRY_POINT "nevercGetPluginInfo"

/* Plugin must export:
 *   NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void);
 *
 * Typical usage:
 *
 *   static int myModulePass(NevercModuleRef M,
 *                           const NevercHostAPI *API, void *UD) {
 *       // use API->... to manipulate IR
 *       return 0;
 *   }
 *
 *   static void registerPasses(const NevercHostAPI *API, void *Reg) {
 *       API->RegisterModulePass(Reg, NEVERC_HOOK_POST_OPT,
 *                               myModulePass, NULL, "my-pass");
 *   }
 *
 *   NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void) {
 *       NevercPluginInfo Info;
 *       Info.APIVersion = NEVERC_PLUGIN_API_VERSION;
 *       Info.PluginName = "my-plugin";
 *       Info.PluginVersion = "1.0";
 *       Info.RegisterPasses = registerPasses;
 *       Info.Destroy = NULL;
 *       return Info;
 *   }
 */

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_PLUGIN_API_H */
