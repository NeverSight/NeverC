/*===-- PluginTarget.h - NeverC target plugin C ABI --------------- C ---===*/

#ifndef NEVERC_PLUGIN_PLUGINTARGET_H
#define NEVERC_PLUGIN_PLUGINTARGET_H

#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginPhaseSchema.h" /* IWYU pragma: export */

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_TARGET_API_MAJOR UINT16_C(1)
#define NEVERC_TARGET_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_TARGET_HIGH UINT64_C(0x4e43505447540001)
#define NEVERC_INTERFACE_TARGET_LOW UINT64_C(0x0000000000000001)
#define NEVERC_TARGET_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

#define NEVERC_TARGET_ABI_API_MAJOR UINT16_C(1)
#define NEVERC_TARGET_ABI_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_TARGET_ABI_HIGH UINT64_C(0x4e43505441424901)
#define NEVERC_INTERFACE_TARGET_ABI_LOW UINT64_C(0x0000000000000001)
#define NEVERC_TARGET_ABI_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

#define NEVERC_CALLING_CONVENTION_API_MAJOR UINT16_C(1)
#define NEVERC_CALLING_CONVENTION_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_CALLING_CONVENTION_HIGH \
  UINT64_C(0x4e43505443430001)
#define NEVERC_INTERFACE_CALLING_CONVENTION_LOW \
  UINT64_C(0x0000000000000001)
#define NEVERC_CALLING_CONVENTION_INTERFACE_STABILITY \
  NEVERC_INTERFACE_STABLE

typedef NevercInterfaceID NevercTargetID;
typedef NevercInterfaceID NevercTargetABIID;
typedef NevercInterfaceID NevercCallingConventionID;

typedef NevercHandle NevercTargetHandle;
typedef NevercHandle NevercTargetSnapshotHandle;
typedef NevercHandle NevercTargetSchemaHandle;
typedef NevercHandle NevercTargetKeyHandle;
typedef NevercHandle NevercTargetMachineHandle;
typedef NevercHandle NevercTargetRouteHandle;
typedef NevercHandle NevercTargetABIHandle;
typedef NevercHandle NevercCallingConventionHandle;

typedef uint32_t NevercCodeGenProductKind;
#define NEVERC_CODEGEN_PRODUCT_IR UINT32_C(1)
#define NEVERC_CODEGEN_PRODUCT_MIR UINT32_C(2)
#define NEVERC_CODEGEN_PRODUCT_MC UINT32_C(3)
#define NEVERC_CODEGEN_PRODUCT_ASSEMBLY UINT32_C(4)
#define NEVERC_CODEGEN_PRODUCT_OBJECT_GRAPH UINT32_C(5)
#define NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE UINT32_C(6)
#define NEVERC_CODEGEN_PRODUCT_CUSTOM UINT32_C(0x10000)

typedef uint32_t NevercTargetEndianness;
#define NEVERC_TARGET_ENDIAN_LITTLE UINT32_C(1)
#define NEVERC_TARGET_ENDIAN_BIG UINT32_C(2)

typedef uint32_t NevercTargetBuiltinVaListKind;
#define NEVERC_TARGET_VA_LIST_CHAR_POINTER UINT32_C(1)
#define NEVERC_TARGET_VA_LIST_VOID_POINTER UINT32_C(2)
#define NEVERC_TARGET_VA_LIST_AARCH64 UINT32_C(3)
#define NEVERC_TARGET_VA_LIST_X86_64 UINT32_C(4)

typedef uint64_t NevercTargetMacroFlags;
#define NEVERC_TARGET_MACRO_UNDEFINE UINT64_C(1)

typedef uint64_t NevercTargetConstraintFlags;
#define NEVERC_TARGET_CONSTRAINT_ALLOWS_MEMORY UINT64_C(1)
#define NEVERC_TARGET_CONSTRAINT_ALLOWS_REGISTER UINT64_C(2)
#define NEVERC_TARGET_CONSTRAINT_IMMEDIATE UINT64_C(4)

typedef uint32_t NevercTargetBuiltinLanguages;
#define NEVERC_TARGET_BUILTIN_LANGUAGE_GNU UINT32_C(1)
#define NEVERC_TARGET_BUILTIN_LANGUAGE_C UINT32_C(2)
#define NEVERC_TARGET_BUILTIN_LANGUAGE_MS UINT32_C(16)

typedef uint64_t NevercTargetExecutionLevel;
#define NEVERC_TARGET_EXECUTION_USER UINT64_C(1)
#define NEVERC_TARGET_EXECUTION_KERNEL UINT64_C(2)
#define NEVERC_TARGET_EXECUTION_HYPERVISOR UINT64_C(4)
#define NEVERC_TARGET_EXECUTION_FIRMWARE UINT64_C(8)

typedef uint32_t NevercTargetRelocationModel;
#define NEVERC_TARGET_RELOCATION_STATIC UINT32_C(1)
#define NEVERC_TARGET_RELOCATION_PIC UINT32_C(2)
#define NEVERC_TARGET_RELOCATION_DYNAMIC_NO_PIC UINT32_C(3)
#define NEVERC_TARGET_RELOCATION_ROPI UINT32_C(4)
#define NEVERC_TARGET_RELOCATION_RWPI UINT32_C(5)
#define NEVERC_TARGET_RELOCATION_ROPI_RWPI UINT32_C(6)
#define NEVERC_TARGET_RELOCATION_MASK_STATIC UINT64_C(1)
#define NEVERC_TARGET_RELOCATION_MASK_PIC UINT64_C(2)
#define NEVERC_TARGET_RELOCATION_MASK_DYNAMIC_NO_PIC UINT64_C(4)
#define NEVERC_TARGET_RELOCATION_MASK_ROPI UINT64_C(8)
#define NEVERC_TARGET_RELOCATION_MASK_RWPI UINT64_C(16)
#define NEVERC_TARGET_RELOCATION_MASK_ROPI_RWPI UINT64_C(32)

typedef uint32_t NevercTargetCodeModel;
#define NEVERC_TARGET_CODE_MODEL_TINY UINT32_C(1)
#define NEVERC_TARGET_CODE_MODEL_SMALL UINT32_C(2)
#define NEVERC_TARGET_CODE_MODEL_KERNEL UINT32_C(3)
#define NEVERC_TARGET_CODE_MODEL_MEDIUM UINT32_C(4)
#define NEVERC_TARGET_CODE_MODEL_LARGE UINT32_C(5)
#define NEVERC_TARGET_CODE_MODEL_MASK_TINY UINT64_C(1)
#define NEVERC_TARGET_CODE_MODEL_MASK_SMALL UINT64_C(2)
#define NEVERC_TARGET_CODE_MODEL_MASK_KERNEL UINT64_C(4)
#define NEVERC_TARGET_CODE_MODEL_MASK_MEDIUM UINT64_C(8)
#define NEVERC_TARGET_CODE_MODEL_MASK_LARGE UINT64_C(16)

typedef uint32_t NevercTargetExceptionModel;
#define NEVERC_TARGET_EXCEPTION_NONE UINT32_C(1)
#define NEVERC_TARGET_EXCEPTION_DWARF UINT32_C(2)
#define NEVERC_TARGET_EXCEPTION_SJLJ UINT32_C(3)
#define NEVERC_TARGET_EXCEPTION_SEH UINT32_C(4)
#define NEVERC_TARGET_EXCEPTION_WASM UINT32_C(5)

typedef uint32_t NevercTargetUnwindModel;
#define NEVERC_TARGET_UNWIND_NONE UINT32_C(1)
#define NEVERC_TARGET_UNWIND_DWARF UINT32_C(2)
#define NEVERC_TARGET_UNWIND_COMPACT UINT32_C(3)
#define NEVERC_TARGET_UNWIND_SEH UINT32_C(4)
#define NEVERC_TARGET_UNWIND_SJLJ UINT32_C(5)
#define NEVERC_TARGET_UNWIND_WASM UINT32_C(6)

typedef uint32_t NevercCodeGenOptimizationLevel;
#define NEVERC_CODEGEN_OPT_NONE UINT32_C(1)
#define NEVERC_CODEGEN_OPT_LESS UINT32_C(2)
#define NEVERC_CODEGEN_OPT_DEFAULT UINT32_C(3)
#define NEVERC_CODEGEN_OPT_AGGRESSIVE UINT32_C(4)

typedef uint32_t NevercABITypeKind;
#define NEVERC_ABI_TYPE_VOID UINT32_C(1)
#define NEVERC_ABI_TYPE_BOOLEAN UINT32_C(2)
#define NEVERC_ABI_TYPE_INTEGER UINT32_C(3)
#define NEVERC_ABI_TYPE_FLOAT UINT32_C(4)
#define NEVERC_ABI_TYPE_POINTER UINT32_C(5)
#define NEVERC_ABI_TYPE_VECTOR UINT32_C(6)
#define NEVERC_ABI_TYPE_RECORD UINT32_C(7)
#define NEVERC_ABI_TYPE_UNION UINT32_C(8)
#define NEVERC_ABI_TYPE_ARRAY UINT32_C(9)
#define NEVERC_ABI_TYPE_COMPLEX UINT32_C(10)
#define NEVERC_ABI_TYPE_ENUM UINT32_C(11)
#define NEVERC_ABI_TYPE_OTHER UINT32_C(0x10000)

typedef uint64_t NevercABITypeFlags;
#define NEVERC_ABI_TYPE_SIGNED UINT64_C(1)
#define NEVERC_ABI_TYPE_AGGREGATE UINT64_C(2)

typedef uint32_t NevercABIArgumentKind;
#define NEVERC_ABI_ARGUMENT_DIRECT UINT32_C(1)
#define NEVERC_ABI_ARGUMENT_EXTEND UINT32_C(2)
#define NEVERC_ABI_ARGUMENT_INDIRECT UINT32_C(3)
#define NEVERC_ABI_ARGUMENT_IGNORE UINT32_C(4)
#define NEVERC_ABI_ARGUMENT_EXPAND UINT32_C(5)

typedef uint32_t NevercABICoercionKind;
#define NEVERC_ABI_COERCE_NONE UINT32_C(0)
#define NEVERC_ABI_COERCE_INTEGER UINT32_C(1)
#define NEVERC_ABI_COERCE_FLOAT UINT32_C(2)
#define NEVERC_ABI_COERCE_POINTER UINT32_C(3)

typedef uint64_t NevercABIArgumentFlags;
#define NEVERC_ABI_ARGUMENT_BYVAL UINT64_C(1)
#define NEVERC_ABI_ARGUMENT_REALIGN UINT64_C(2)
#define NEVERC_ABI_ARGUMENT_INREG UINT64_C(4)
#define NEVERC_ABI_ARGUMENT_SRET_AFTER_THIS UINT64_C(8)
#define NEVERC_ABI_ARGUMENT_CAN_BE_FLATTENED UINT64_C(16)
#define NEVERC_ABI_ARGUMENT_SIGN_EXTEND UINT64_C(32)

typedef uint32_t NevercABIVAArgKind;
#define NEVERC_ABI_VA_ARG_LLVM UINT32_C(1)
#define NEVERC_ABI_VA_ARG_VOID_POINTER UINT32_C(2)

NEVERC_ABI_PACK_BEGIN

typedef struct NevercStringArrayView {
  const NevercStringView *Data;
  uint64_t Count;
  uint64_t ElementStride;
} NevercStringArrayView;

typedef struct NevercInterfaceIDArrayView {
  const NevercInterfaceID *Data;
  uint64_t Count;
  uint64_t ElementStride;
} NevercInterfaceIDArrayView;

typedef struct NevercABITypeDescriptor {
  NevercABITableHeader Header;
  NevercABITypeKind Kind;
  uint32_t BitWidth;
  uint32_t Alignment;
  uint32_t AddressSpace;
  NevercABITypeFlags Flags;
} NevercABITypeDescriptor;

typedef struct NevercABIArgumentClassification {
  NevercABITableHeader Header;
  NevercABIArgumentKind Kind;
  NevercABICoercionKind Coercion;
  uint32_t CoercionBitWidth;
  uint32_t Alignment;
  uint32_t AddressSpace;
  uint32_t DirectOffset;
  NevercABIArgumentFlags Flags;
} NevercABIArgumentClassification;

typedef struct NevercABIArgumentClassificationArray {
  NevercABIArgumentClassification *Data;
  uint64_t Count;
  uint64_t ElementStride;
} NevercABIArgumentClassificationArray;

typedef struct NevercABIFunctionQuery {
  NevercABITableHeader Header;
  NevercABITypeDescriptor ReturnType;
  NevercStructArrayView Parameters;
  NevercBool Variadic;
  uint32_t RequiredArgumentCount;
  uint64_t Reserved;
} NevercABIFunctionQuery;

typedef struct NevercTargetVAArgDescriptor {
  NevercABITableHeader Header;
  NevercABIVAArgKind Kind;
  uint32_t SlotSize;
  uint32_t SlotAlignment;
  NevercBool AllowHigherAlignment;
  uint64_t Reserved;
} NevercTargetVAArgDescriptor;

typedef NevercStatus(NEVERC_CALL *NevercClassifyABIFunctionFn)(
    void *UserData, const NevercABIFunctionQuery *Query,
    NevercABIArgumentClassification *ReturnValue,
    NevercABIArgumentClassificationArray *Arguments);

typedef struct NevercTargetTripleMatcher {
  NevercABITableHeader Header;
  NevercStringView Architecture;
  NevercStringView Vendor;
  NevercStringView OperatingSystem;
  NevercStringView Environment;
  uint32_t Priority;
  uint32_t Reserved;
} NevercTargetTripleMatcher;

typedef struct NevercTargetFeatureDescriptor {
  NevercABITableHeader Header;
  NevercStringView Name;
  NevercStringArrayView Implies;
  NevercStringArrayView Conflicts;
  NevercBool EnabledByDefault;
  uint32_t Reserved;
} NevercTargetFeatureDescriptor;

typedef struct NevercTargetMacroDescriptor {
  NevercABITableHeader Header;
  NevercStringView Name;
  NevercStringView Value;
  NevercTargetMacroFlags Flags;
} NevercTargetMacroDescriptor;

typedef struct NevercTargetBuiltinDescriptor {
  NevercABITableHeader Header;
  NevercStringView Name;
  NevercStringView TypeEncoding;
  NevercStringView Attributes;
  NevercStringView RequiredFeatures;
  NevercStringView HeaderName;
  NevercTargetBuiltinLanguages Languages;
  uint32_t Reserved;
} NevercTargetBuiltinDescriptor;

typedef struct NevercTargetRegisterDescriptor {
  NevercABITableHeader Header;
  NevercStringView Name;
  NevercStringArrayView Aliases;
  uint64_t Flags;
} NevercTargetRegisterDescriptor;

typedef struct NevercTargetConstraintDescriptor {
  NevercABITableHeader Header;
  NevercStringView Spelling;
  NevercStringView ConvertedConstraint;
  NevercTargetConstraintFlags Flags;
  int32_t ImmediateMinimum;
  int32_t ImmediateMaximum;
  uint64_t Reserved;
} NevercTargetConstraintDescriptor;

typedef struct NevercTargetAddressSpaceDescriptor {
  NevercABITableHeader Header;
  uint32_t AddressSpace;
  uint32_t PointerWidth;
  uint32_t ABIAlignment;
  uint32_t PreferredAlignment;
  uint64_t Flags;
} NevercTargetAddressSpaceDescriptor;

typedef struct NevercTargetMachineDescriptor {
  NevercABITableHeader Header;
  NevercStringView RawTriple;
  NevercStringView Architecture;
  NevercStringView Vendor;
  NevercStringView OperatingSystem;
  NevercStringView Environment;
  NevercStringView DataLayout;
  NevercStringView DefaultCPU;
  NevercStringView TuneCPU;
  NevercStringView GlobalLabelPrefix;
  NevercStringView SchemaDigest;
  NevercStringArrayView CPUs;
  NevercStructArrayView Features;
  NevercInterfaceIDArrayView ABIs;
  NevercInterfaceIDArrayView CallingConventions;
  NevercInterfaceIDArrayView ObjectFormats;
  NevercStructArrayView AddressSpaces;
  uint64_t SupportedRelocationModels;
  uint64_t SupportedCodeModels;
  NevercTargetRelocationModel DefaultRelocationModel;
  NevercTargetCodeModel DefaultCodeModel;
  NevercTargetExceptionModel ExceptionModel;
  NevercTargetUnwindModel UnwindModel;
  NevercTargetEndianness Endianness;
  uint32_t PointerWidth;
  uint32_t IntWidth;
  uint32_t LongWidth;
  uint32_t LongLongWidth;
  uint32_t StackAlignment;
  uint32_t MaximumAtomicWidth;
  uint32_t MaximumVectorAlignment;
  NevercTargetBuiltinVaListKind BuiltinVaListKind;
  NevercTargetExecutionLevel ExecutionLevels;
  NevercTargetExecutionLevel DefaultExecutionLevel;
  NevercBool TLSSupported;
  uint32_t Reserved;
} NevercTargetMachineDescriptor;

typedef struct NevercTargetKey {
  NevercABITableHeader Header;
  NevercTargetID TargetID;
  NevercStringView RawTriple;
  NevercStringView Architecture;
  NevercStringView Vendor;
  NevercStringView OperatingSystem;
  NevercStringView Environment;
  NevercStringView CPU;
  NevercStringView TuneCPU;
  NevercStringArrayView Features;
  NevercTargetABIID ABIID;
  NevercCallingConventionID CallingConventionID;
  NevercInterfaceID ObjectFormatID;
  NevercTargetRelocationModel RelocationModel;
  NevercTargetCodeModel CodeModel;
  NevercTargetExecutionLevel ExecutionLevel;
  uint32_t PointerWidth;
  NevercTargetEndianness Endianness;
  NevercStringView SchemaDigest;
} NevercTargetKey;

typedef struct NevercTargetMachineCreateRequest {
  NevercABITableHeader Header;
  NevercStringView RawTriple;
  NevercStringView CPU;
  NevercStringView Features;
  NevercTargetCodeModel CodeModel;
  NevercCodeGenOptimizationLevel OptimizationLevel;
  uint64_t Reserved;
} NevercTargetMachineCreateRequest;

typedef NevercStatus(NEVERC_CALL *NevercCreateTargetMachineFn)(
    void *UserData, const NevercTargetMachineCreateRequest *Request,
    NevercTargetMachineHandle *OutMachine);
typedef void(NEVERC_CALL *NevercDestroyTargetMachineFn)(
    void *UserData, NevercTargetMachineHandle Machine);

typedef struct NevercTargetDescriptor {
  NevercABITableHeader Header;
  NevercTargetID TargetID;
  NevercStringView CanonicalName;
  NevercStringArrayView Aliases;
  NevercStructArrayView TripleMatchers;
  NevercTargetABIID DefaultABI;
  NevercCallingConventionID DefaultCallingConvention;
  NevercInterfaceID MCSchemaID;
  NevercInterfaceID DefaultObjectFormatID;
  NevercTargetMachineDescriptor Machine;
  NevercStructArrayView Macros;
  NevercStructArrayView Builtins;
  NevercStructArrayView Registers;
  NevercStructArrayView Constraints;
  NevercStringView Clobbers;
  uint64_t Flags;
  NevercCreateTargetMachineFn CreateTargetMachine;
  NevercDestroyTargetMachineFn DestroyTargetMachine;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercTargetDescriptor;

typedef struct NevercTargetABIDescriptor {
  NevercABITableHeader Header;
  NevercTargetABIID ABIID;
  NevercTargetID TargetID;
  NevercStringView CanonicalName;
  NevercInterfaceIDArrayView Dependencies;
  NevercClassifyABIFunctionFn ClassifyFunction;
  NevercTargetVAArgDescriptor VAArg;
  uint64_t Flags;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercTargetABIDescriptor;

typedef struct NevercCallingConventionDescriptor {
  NevercABITableHeader Header;
  NevercCallingConventionID CallingConventionID;
  NevercTargetID TargetID;
  NevercStringView CanonicalName;
  NevercStringArrayView CalleeSavedRegisters;
  uint32_t LLVMCallingConvention;
  uint32_t Reserved;
  uint64_t Flags;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercCallingConventionDescriptor;

typedef struct NevercCodeGenEdgeDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID EdgeID;
  NevercStringView CanonicalName;
  NevercTargetID TargetID;
  NevercCodeGenProductKind InputKind;
  NevercCodeGenProductKind OutputKind;
  NevercInterfaceIDArrayView Dependencies;
  uint64_t Flags;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercCodeGenEdgeDescriptor;

typedef struct NevercTargetAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *RegisterTarget)(
      void *Context, void *RegistrarContext,
      const NevercTargetDescriptor *Descriptor);
  NevercStatus(NEVERC_CALL *RegisterCodeGenEdge)(
      void *Context, void *RegistrarContext,
      const NevercCodeGenEdgeDescriptor *Descriptor);
} NevercTargetAPI;

typedef struct NevercTargetABIAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *RegisterABI)(
      void *Context, void *RegistrarContext,
      const NevercTargetABIDescriptor *Descriptor);
} NevercTargetABIAPI;

typedef struct NevercCallingConventionAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *RegisterCallingConvention)(
      void *Context, void *RegistrarContext,
      const NevercCallingConventionDescriptor *Descriptor);
} NevercCallingConventionAPI;

NEVERC_ABI_PACK_END

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_PLUGIN_PLUGINTARGET_H */
