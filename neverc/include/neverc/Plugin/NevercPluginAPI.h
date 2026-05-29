/*===-- NevercPluginAPI.h - Out-of-tree plugin C ABI header --------*- C -*-===*\
|*                                                                            *|
|* Pure C public header for neverc out-of-tree pass plugins.                  *|
|* This is the ONLY file a plugin needs to compile against.                   *|
|*                                                                            *|
|* All IR/MIR/Binary manipulation goes through the NevercHostAPI vtable       *|
|* provided by the host process — no direct CRT or LLVM C++ calls needed.    *|
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
|*   - Strings returned by StrDup/StrConcat/StrSubstring/StrReplace/        *|
|*     StrFormat/IntToStr/ValuePrintToString/etc. are host-allocated;       *|
|*     caller frees via Free.  DiagNoteF/DiagWarningF/DiagErrorF do NOT       *|
|*     require Free.  HookPointGetName returns a static string.               *|
|*   - Do NOT call RegisterModulePass/MachinePass/BinaryPass outside of      *|
|*     the RegisterPasses callback — they are no-ops after registration.      *|
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

/* ---- Convenience allocation macros ----
 * Typed array allocation through the host vtable — eliminates the
 * repetitive (Type*)API->Alloc(Count * sizeof(Type)) pattern and
 * guards against count*size overflow via ReallocArray/AllocZeroed.
 *
 * NEVERC_ALLOC_ARRAY  — uninitialized (fast, for immediately-filled arrays)
 * NEVERC_CALLOC_ARRAY — zero-initialized (for arrays that need a clean slate)
 * NEVERC_REALLOC_ARRAY — grow/shrink with overflow check                    */
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

/* -------------------------------------------------------------------------- */
/*  Hook points                                                               */
/* -------------------------------------------------------------------------- */

typedef enum {
  /* ---- Normal flow — IR (BackendUtil.cpp optimization pipeline) ---- */
  NEVERC_HOOK_PRE_OPT        = 0x0001,
  NEVERC_HOOK_POST_OPT       = 0x0002,
  NEVERC_HOOK_PIPELINE_START = 0x0003,
  NEVERC_HOOK_PIPELINE_LAST  = 0x0004,

  /* ---- Normal flow — MIR (code generation pipeline) ---- */
  NEVERC_HOOK_BEFORE_CODEGEN_PREEMIT  = 0x0010,
  NEVERC_HOOK_AFTER_CODEGEN_FINAL_MIR = 0x0011,

  /* ---- Shellcode flow — IR hooks ---- */
  NEVERC_HOOK_SC_BEFORE_PREP     = 0x0100,
  NEVERC_HOOK_SC_AFTER_PREP      = 0x0101,
  NEVERC_HOOK_SC_BEFORE_INLINING = 0x0102,
  NEVERC_HOOK_SC_AFTER_INLINING  = 0x0103,
  NEVERC_HOOK_SC_AFTER_STACKIFY  = 0x0104,
  NEVERC_HOOK_SC_AFTER_FINAL_IR  = 0x0105,

  /* ---- Shellcode flow — MIR hooks ---- */
  NEVERC_HOOK_SC_BEFORE_PREEMIT  = 0x0200,
  NEVERC_HOOK_SC_AFTER_PREEMIT   = 0x0201,
  NEVERC_HOOK_SC_AFTER_FINAL_MIR = 0x0202,

  /* ---- Shellcode flow — binary hooks ---- */
  NEVERC_HOOK_SC_POST_EXTRACT    = 0x0300,
  NEVERC_HOOK_SC_POST_FINALIZE   = 0x0301,

  /* ---- LTO flow — IR hooks (inside LTO optimization pipeline) ---- */
  NEVERC_HOOK_LTO_PRE_OPT        = 0x0400,
  NEVERC_HOOK_LTO_POST_OPT       = 0x0401,

  /* ---- Linker flow — object-level hooks ---- */
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
  uint32_t StructSize; /* sizeof(NevercHostAPI) — plugin MUST NOT access
                          fields past this boundary even if its compiled
                          header defines more fields. */

  /* ---- Memory (host heap — mimalloc when enabled; no plugin CRT malloc) ---- */
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
         passes — without this, debugger single-stepping breaks) ---- */
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
         NOT the LLVM operand index — operand 0 is the pointer) ---- */
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
         — e.g., branch on a potentially-poison condition without UB) ---- */
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

  /* ---- Character search: returns byte offset, or (uint64_t)-1 if not
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
         Needle in Haystack, or (uint64_t)-1 if not found.  Complements
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

  /* ---- Fast string hash (FNV-1a 64-bit).  Useful for building lookup
         tables or fast string comparisons in plugins.  Returns 0 for
         NULL input. ---- */
  uint64_t (*StrHash)(const char *S);

  /* ---- One-call batch collection of DEFINED functions only.
         Skips declarations on the host side — eliminates the
         ubiquitous collect→filter→loop→free boilerplate in plugins.
         Returns NULL and sets *OutCount to 0 when no defined functions
         exist.  Caller frees via Free. ---- */
  NevercValueRef *(*ModuleCollectDefinedFunctions)(NevercModuleRef M,
                                                   unsigned *OutCount);
} NevercHostAPI;

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
