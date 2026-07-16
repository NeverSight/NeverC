/*===-- PluginDriver.h - NeverC plugin driver C ABI ---------------- C ---===*/

#ifndef NEVERC_PLUGIN_PLUGINDRIVER_H
#define NEVERC_PLUGIN_PLUGINDRIVER_H

#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginPhaseSchema.h" // IWYU pragma: export
#include "neverc/Plugin/PluginSource.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_DRIVER_API_MAJOR UINT16_C(1)
#define NEVERC_DRIVER_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_DRIVER_HIGH UINT64_C(0x4e65766572434472)
#define NEVERC_INTERFACE_DRIVER_LOW UINT64_C(0x6976657241504901)

typedef uint32_t NevercOptionForm;
#define NEVERC_OPTION_FLAG UINT32_C(0)
#define NEVERC_OPTION_JOINED UINT32_C(1)
#define NEVERC_OPTION_SEPARATE UINT32_C(2)
#define NEVERC_OPTION_MULTI_ARG UINT32_C(3)

typedef uint32_t NevercOptionValueType;
#define NEVERC_OPTION_BOOL UINT32_C(0)
#define NEVERC_OPTION_INT UINT32_C(1)
#define NEVERC_OPTION_UINT UINT32_C(2)
#define NEVERC_OPTION_STRING UINT32_C(3)
#define NEVERC_OPTION_ENUM UINT32_C(4)
#define NEVERC_OPTION_PATH UINT32_C(5)

typedef uint32_t NevercOptionMultiplicity;
#define NEVERC_OPTION_SINGLE UINT32_C(0)
#define NEVERC_OPTION_LAST_WINS UINT32_C(1)
#define NEVERC_OPTION_APPEND UINT32_C(2)

NEVERC_ABI_PACK_BEGIN

typedef struct NevercStringList {
  const NevercStringView *Data;
  uint64_t Count;
  uint64_t ElementStride;
} NevercStringList;

typedef struct NevercOptionEnumValue {
  NevercABITableHeader Header;
  NevercStringView Name;
  int64_t Value;
  NevercStringView Help;
} NevercOptionEnumValue;

typedef struct NevercOptionValidationContext {
  NevercABITableHeader Header;
  NevercStringView PluginID;
  NevercStringView Spelling;
  NevercStringView TargetTriple;
  uint64_t Occurrence;
} NevercOptionValidationContext;

typedef NevercStatus(NEVERC_CALL *NevercOptionValidatorFn)(
    const NevercOptionValidationContext *Context, NevercStringView Value,
    void *UserData);

typedef struct NevercOptionDescriptor {
  NevercABITableHeader Header;
  NevercStringView Spelling;
  NevercStringList Aliases;
  NevercOptionForm Form;
  NevercOptionValueType ValueType;
  NevercOptionMultiplicity Multiplicity;
  uint32_t ArgumentCount;
  NevercBool Required;
  NevercBool Hidden;
  NevercStringView Help;
  NevercStringView Metavar;
  NevercStructArrayView EnumValues;
  NevercStringList Conflicts;
  NevercStringList Requires;
  NevercStringView TargetPredicate;
  NevercOptionValidatorFn Validator;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercOptionDescriptor;

typedef uint32_t NevercArgumentOrigin;
#define NEVERC_ARGUMENT_ORIGIN_COMMAND_LINE UINT32_C(0)
#define NEVERC_ARGUMENT_ORIGIN_CONFIGURATION UINT32_C(1)
#define NEVERC_ARGUMENT_ORIGIN_PLUGIN UINT32_C(2)

typedef NevercHandle NevercArgumentMutationHandle;

typedef NevercStatus(NEVERC_CALL *NevercGetArgumentCountFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Arguments, uint64_t *OutCount);
typedef NevercStatus(NEVERC_CALL *NevercGetArgumentFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Arguments, uint64_t Index, NevercStringView *OutValue,
    NevercArgumentOrigin *OutOrigin, NevercStringView *OutSource,
    uint64_t *OutPosition);
typedef NevercStatus(NEVERC_CALL *NevercBeginArgumentMutationFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation, NevercArtifactHandle Arguments,
    NevercArgumentMutationHandle *OutMutation);
typedef NevercStatus(NEVERC_CALL *NevercInsertArgumentFn)(
    void *Context, NevercArgumentMutationHandle Mutation, uint64_t Index,
    NevercStringView Value);
typedef NevercStatus(NEVERC_CALL *NevercReplaceArgumentFn)(
    void *Context, NevercArgumentMutationHandle Mutation, uint64_t Index,
    NevercStringView Value);
typedef NevercStatus(NEVERC_CALL *NevercEraseArgumentFn)(
    void *Context, NevercArgumentMutationHandle Mutation, uint64_t Index);
typedef NevercStatus(NEVERC_CALL *NevercCommitArgumentMutationFn)(
    void *Context, NevercArgumentMutationHandle Mutation);
typedef NevercStatus(NEVERC_CALL *NevercAbortArgumentMutationFn)(
    void *Context, NevercArgumentMutationHandle Mutation);

typedef struct NevercOptionOccurrence {
  NevercABITableHeader Header;
  uint64_t Occurrence;
  NevercStringView Spelling;
  NevercStringList Values;
  NevercArgumentOrigin Origin;
  uint32_t Reserved;
} NevercOptionOccurrence;

typedef NevercHandle NevercParsedArgumentMutationHandle;

typedef NevercStatus(NEVERC_CALL *NevercGetOptionOccurrenceCountFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Arguments, uint64_t *OutCount);
typedef NevercStatus(NEVERC_CALL *NevercGetOptionOccurrenceFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Arguments, uint64_t Index,
    NevercOptionOccurrence *OutOccurrence);
typedef NevercStatus(NEVERC_CALL *NevercBeginParsedArgumentMutationFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation, NevercArtifactHandle Arguments,
    NevercParsedArgumentMutationHandle *OutMutation);
typedef NevercStatus(NEVERC_CALL *NevercAddOptionOccurrenceFn)(
    void *Context, NevercParsedArgumentMutationHandle Mutation,
    NevercStringView Spelling, NevercStringList Values);
typedef NevercStatus(NEVERC_CALL *NevercRemoveOptionOccurrenceFn)(
    void *Context, NevercParsedArgumentMutationHandle Mutation,
    uint64_t Occurrence);
typedef NevercStatus(NEVERC_CALL *NevercReplaceOptionOccurrenceFn)(
    void *Context, NevercParsedArgumentMutationHandle Mutation,
    uint64_t Occurrence, NevercStringView Spelling, NevercStringList Values);
typedef NevercStatus(NEVERC_CALL *NevercCommitParsedArgumentMutationFn)(
    void *Context, NevercParsedArgumentMutationHandle Mutation);
typedef NevercStatus(NEVERC_CALL *NevercAbortParsedArgumentMutationFn)(
    void *Context, NevercParsedArgumentMutationHandle Mutation);

typedef uint32_t NevercExecutionLevel;
#define NEVERC_EXECUTION_LEVEL_UNSPECIFIED UINT32_C(0)
#define NEVERC_EXECUTION_LEVEL_USER UINT32_C(1)
#define NEVERC_EXECUTION_LEVEL_KERNEL UINT32_C(2)

#define NEVERC_TOOLCHAIN_ID_DARWIN "neverc.builtin.darwin"
#define NEVERC_TOOLCHAIN_ID_LINUX "neverc.builtin.linux"
#define NEVERC_TOOLCHAIN_ID_MSVC "neverc.builtin.msvc"
#define NEVERC_TOOLCHAIN_ID_GENERIC_ELF "neverc.builtin.generic-elf"
#define NEVERC_TOOLCHAIN_ID_MACHO "neverc.builtin.macho"
#define NEVERC_TOOLCHAIN_ID_GENERIC_GCC "neverc.builtin.generic-gcc"

typedef NevercHandle NevercToolChainMutationHandle;
typedef NevercHandle NevercToolChainProviderHandle;

typedef struct NevercToolChainRequest {
  NevercABITableHeader Header;
  NevercStringView RequestedTriple;
  NevercStringView ComputedTriple;
  NevercStringView SysRoot;
  NevercStringView ResourceDir;
  NevercStringView CPU;
  NevercStringList Features;
  NevercExecutionLevel ExecutionLevel;
  NevercBool DynamicCodeProfile;
  uint32_t Reserved;
} NevercToolChainRequest;

typedef struct NevercToolChainSelectionDescriptor {
  NevercABITableHeader Header;
  NevercStringView ToolChainID;
  NevercStringView TargetKey;
  NevercStringView TargetTriple;
  NevercStringView CPU;
  NevercStringList Features;
  NevercToolChainProviderHandle Provider;
} NevercToolChainSelectionDescriptor;

typedef struct NevercToolChainSelection {
  NevercABITableHeader Header;
  NevercStringView ToolChainID;
  NevercStringView TargetKey;
  NevercStringView TargetTriple;
  NevercStringView CPU;
  NevercStringList Features;
  NevercToolChainProviderHandle Provider;
  NevercBool BuiltinProviderUsed;
  uint32_t Reserved;
} NevercToolChainSelection;

typedef NevercStatus(NEVERC_CALL *NevercGetToolChainRequestFn)(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Request,
    NevercToolChainRequest *OutRequest);
typedef NevercStatus(NEVERC_CALL *NevercBeginToolChainMutationFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation, NevercArtifactHandle Request,
    NevercToolChainMutationHandle *OutMutation);
typedef NevercStatus(NEVERC_CALL *NevercSetToolChainTripleFn)(
    void *Context, NevercToolChainMutationHandle Mutation,
    NevercStringView Triple);
typedef NevercStatus(NEVERC_CALL *NevercSetToolChainCPUFn)(
    void *Context, NevercToolChainMutationHandle Mutation,
    NevercStringView CPU);
typedef NevercStatus(NEVERC_CALL *NevercSetToolChainFeaturesFn)(
    void *Context, NevercToolChainMutationHandle Mutation,
    NevercStringList Features);
typedef NevercStatus(NEVERC_CALL *NevercCommitToolChainMutationFn)(
    void *Context, NevercToolChainMutationHandle Mutation);
typedef NevercStatus(NEVERC_CALL *NevercAbortToolChainMutationFn)(
    void *Context, NevercToolChainMutationHandle Mutation);
typedef NevercStatus(NEVERC_CALL *NevercCreateToolChainSelectionFn)(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Request,
    const NevercToolChainSelectionDescriptor *Descriptor,
    NevercArtifactHandle *OutSelection);
typedef NevercStatus(NEVERC_CALL *NevercGetToolChainSelectionFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Selection, NevercToolChainSelection *OutSelection);

typedef uint32_t NevercDriverType;
#define NEVERC_DRIVER_TYPE_INVALID UINT32_C(0)
#define NEVERC_DRIVER_TYPE_PP_C UINT32_C(1)
#define NEVERC_DRIVER_TYPE_C UINT32_C(2)
#define NEVERC_DRIVER_TYPE_C_HEADER UINT32_C(3)
#define NEVERC_DRIVER_TYPE_PP_ASM UINT32_C(4)
#define NEVERC_DRIVER_TYPE_ASM UINT32_C(5)
#define NEVERC_DRIVER_TYPE_LLVM_IR UINT32_C(6)
#define NEVERC_DRIVER_TYPE_LLVM_BC UINT32_C(7)
#define NEVERC_DRIVER_TYPE_LTO_IR UINT32_C(8)
#define NEVERC_DRIVER_TYPE_LTO_BC UINT32_C(9)
#define NEVERC_DRIVER_TYPE_OBJECT UINT32_C(10)
#define NEVERC_DRIVER_TYPE_IMAGE UINT32_C(11)
#define NEVERC_DRIVER_TYPE_DSYM UINT32_C(12)
#define NEVERC_DRIVER_TYPE_DEPENDENCIES UINT32_C(13)
#define NEVERC_DRIVER_TYPE_NOTHING UINT32_C(14)

typedef uint32_t NevercActionKind;
#define NEVERC_ACTION_INVALID UINT32_C(0)
#define NEVERC_ACTION_INPUT UINT32_C(1)
#define NEVERC_ACTION_BIND_ARCH UINT32_C(2)
#define NEVERC_ACTION_PREPROCESS UINT32_C(3)
#define NEVERC_ACTION_COMPILE UINT32_C(4)
#define NEVERC_ACTION_BACKEND UINT32_C(5)
#define NEVERC_ACTION_ASSEMBLE UINT32_C(6)
#define NEVERC_ACTION_LINK UINT32_C(7)
#define NEVERC_ACTION_LIPO UINT32_C(8)
#define NEVERC_ACTION_DSYMUTIL UINT32_C(9)
#define NEVERC_ACTION_STATIC_LIB UINT32_C(10)

typedef uint64_t NevercDriverInputID;
typedef uint64_t NevercActionNodeID;
typedef NevercHandle NevercActionGraphBuilderHandle;
typedef NevercHandle NevercActionGraphMutationHandle;

typedef struct NevercActionNodeIDList {
  const NevercActionNodeID *Data;
  uint64_t Count;
  uint64_t ElementStride;
} NevercActionNodeIDList;

typedef struct NevercDriverInput {
  NevercABITableHeader Header;
  NevercDriverInputID Input;
  NevercDriverType Type;
  uint32_t Reserved;
  NevercStringView Value;
} NevercDriverInput;

typedef struct NevercActionNode {
  NevercABITableHeader Header;
  NevercActionNodeID Node;
  NevercActionKind Kind;
  NevercDriverType OutputType;
  uint64_t InputCount;
  NevercDriverInputID DriverInput;
  NevercStringView BindArch;
  uint64_t Reserved;
} NevercActionNode;

typedef struct NevercActionNodeDescriptor {
  NevercABITableHeader Header;
  NevercActionKind Kind;
  NevercDriverType OutputType;
  NevercDriverInputID DriverInput;
  NevercStringView BindArch;
  NevercActionNodeIDList Inputs;
  uint64_t Reserved;
} NevercActionNodeDescriptor;

typedef NevercStatus(NEVERC_CALL *NevercGetDriverInputCountFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Request, uint64_t *OutCount);
typedef NevercStatus(NEVERC_CALL *NevercGetDriverInputFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Request, uint64_t Index,
    NevercDriverInput *OutInput);
typedef NevercStatus(NEVERC_CALL *NevercGetActionNodeCountFn)(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
    uint64_t *OutCount);
typedef NevercStatus(NEVERC_CALL *NevercGetActionNodeFn)(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
    uint64_t Index, NevercActionNode *OutNode);
typedef NevercStatus(NEVERC_CALL *NevercGetActionNodeInputFn)(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
    NevercActionNodeID Node, uint64_t Index,
    NevercActionNodeID *OutInput);
typedef NevercStatus(NEVERC_CALL *NevercGetActionRootCountFn)(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
    uint64_t *OutCount);
typedef NevercStatus(NEVERC_CALL *NevercGetActionRootFn)(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
    uint64_t Index, NevercActionNodeID *OutRoot);
typedef NevercStatus(NEVERC_CALL *NevercCreateActionGraphBuilderFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Request, NevercActionGraphBuilderHandle *OutBuilder);
typedef NevercStatus(NEVERC_CALL *NevercBeginActionGraphMutationFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation, NevercArtifactHandle Graph,
    NevercActionGraphMutationHandle *OutMutation);
typedef NevercStatus(NEVERC_CALL *NevercAddActionNodeFn)(
    void *Context, NevercActionGraphBuilderHandle Builder,
    const NevercActionNodeDescriptor *Descriptor,
    NevercActionNodeID *OutNode);
typedef NevercStatus(NEVERC_CALL *NevercRemoveActionNodeFn)(
    void *Context, NevercActionGraphBuilderHandle Builder,
    NevercActionNodeID Node);
typedef NevercStatus(NEVERC_CALL *NevercReplaceActionNodeInputsFn)(
    void *Context, NevercActionGraphBuilderHandle Builder,
    NevercActionNodeID Node, NevercActionNodeIDList Inputs);
typedef NevercStatus(NEVERC_CALL *NevercSetActionNodeOutputTypeFn)(
    void *Context, NevercActionGraphBuilderHandle Builder,
    NevercActionNodeID Node, NevercDriverType OutputType);
typedef NevercStatus(NEVERC_CALL *NevercSetActionNodeBindArchFn)(
    void *Context, NevercActionGraphBuilderHandle Builder,
    NevercActionNodeID Node, NevercStringView BindArch);
typedef NevercStatus(NEVERC_CALL *NevercSetActionRootsFn)(
    void *Context, NevercActionGraphBuilderHandle Builder,
    NevercActionNodeIDList Roots);
typedef NevercStatus(NEVERC_CALL *NevercPublishActionGraphFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercActionGraphBuilderHandle Builder,
    NevercArtifactHandle *OutGraph);
typedef NevercStatus(NEVERC_CALL *NevercCommitActionGraphMutationFn)(
    void *Context, NevercActionGraphMutationHandle Mutation);
typedef NevercStatus(NEVERC_CALL *NevercAbortActionGraphEditFn)(
    void *Context, NevercHandle Edit);

typedef uint64_t NevercJobID;
typedef NevercHandle NevercJobGraphBuilderHandle;
typedef NevercHandle NevercJobGraphMutationHandle;

typedef uint32_t NevercJobKind;
#define NEVERC_JOB_COMMAND UINT32_C(1)
#define NEVERC_JOB_FRONTEND UINT32_C(2)
#define NEVERC_JOB_LINKER UINT32_C(3)
#define NEVERC_JOB_ARCHIVE UINT32_C(4)
#define NEVERC_JOB_PLUGIN UINT32_C(5)

typedef uint32_t NevercResponseFileKind;
#define NEVERC_RESPONSE_FILE_NONE UINT32_C(0)
#define NEVERC_RESPONSE_FILE_FULL UINT32_C(1)
#define NEVERC_RESPONSE_FILE_LIST UINT32_C(2)

typedef uint32_t NevercResponseFileEncoding;
#define NEVERC_RESPONSE_ENCODING_UTF8 UINT32_C(0)
#define NEVERC_RESPONSE_ENCODING_CURRENT_CODE_PAGE UINT32_C(1)
#define NEVERC_RESPONSE_ENCODING_UTF16 UINT32_C(2)

typedef uint32_t NevercLinkerFlavor;
#define NEVERC_LINKER_FLAVOR_NONE UINT32_C(0)
#define NEVERC_LINKER_FLAVOR_GNU UINT32_C(1)
#define NEVERC_LINKER_FLAVOR_WIN_LINK UINT32_C(2)
#define NEVERC_LINKER_FLAVOR_DARWIN UINT32_C(3)

typedef struct NevercJobIDList {
  const NevercJobID *Data;
  uint64_t Count;
  uint64_t ElementStride;
} NevercJobIDList;

typedef struct NevercJobFile {
  NevercABITableHeader Header;
  NevercStringView Path;
  NevercDriverType Type;
  uint32_t Reserved;
} NevercJobFile;

typedef struct NevercJobFileList {
  const NevercJobFile *Data;
  uint64_t Count;
  uint64_t ElementStride;
} NevercJobFileList;

typedef struct NevercPluginJobContext {
  NevercABITableHeader Header;
  NevercSessionHandle Session;
  NevercJobID Job;
  NevercStringView CallbackID;
  NevercStringList Arguments;
  NevercStringList Environment;
  NevercJobFileList Inputs;
  NevercJobFileList Outputs;
} NevercPluginJobContext;

typedef NevercStatus(NEVERC_CALL *NevercPluginJobCallbackFn)(
    const NevercPluginJobContext *Context, int32_t *OutExitCode,
    void *UserData);

typedef struct NevercJobDescriptor {
  NevercABITableHeader Header;
  NevercJobKind Kind;
  NevercResponseFileKind ResponseFileKind;
  NevercResponseFileEncoding ResponseFileEncoding;
  NevercBool InProcess;
  NevercActionNodeID SourceAction;
  NevercLinkerFlavor LinkerFlavor;
  uint32_t Reserved;
  NevercStringView Executable;
  NevercStringList Arguments;
  NevercStringList Environment;
  NevercJobFileList Inputs;
  NevercJobFileList Outputs;
  NevercJobIDList Dependencies;
  NevercStringView CallbackID;
  NevercPluginJobCallbackFn Callback;
  void *UserData;
} NevercJobDescriptor;

typedef struct NevercJob {
  NevercABITableHeader Header;
  NevercJobID Job;
  NevercJobKind Kind;
  NevercResponseFileKind ResponseFileKind;
  NevercResponseFileEncoding ResponseFileEncoding;
  NevercBool InProcess;
  NevercActionNodeID SourceAction;
  NevercLinkerFlavor LinkerFlavor;
  uint32_t Reserved;
  NevercStringView Executable;
  NevercStringView CallbackID;
  uint64_t ArgumentCount;
  uint64_t EnvironmentCount;
  uint64_t InputCount;
  uint64_t OutputCount;
  uint64_t DependencyCount;
} NevercJob;

typedef struct NevercJobExecutionRequest {
  NevercABITableHeader Header;
  NevercJob Job;
  NevercStringList Arguments;
  NevercStringList Environment;
  NevercJobFileList Inputs;
  NevercJobFileList Outputs;
  NevercJobIDList Dependencies;
} NevercJobExecutionRequest;

typedef struct NevercJobResultDescriptor {
  NevercABITableHeader Header;
  int32_t ExitCode;
  NevercBool ExecutionFailed;
  NevercBool HasProcessStatistics;
  uint32_t Reserved;
  NevercStringView ErrorMessage;
  NevercOutputSealList OutputSeals;
  uint64_t TotalTimeMicroseconds;
  uint64_t UserTimeMicroseconds;
  uint64_t PeakMemoryKiB;
} NevercJobResultDescriptor;

typedef struct NevercJobResult {
  NevercABITableHeader Header;
  NevercJobID Job;
  int32_t ExitCode;
  NevercBool ExecutionFailed;
  NevercBool HasProcessStatistics;
  NevercBool BuiltinProviderUsed;
  uint32_t Reserved;
  uint64_t OutputSealCount;
  NevercStringView ErrorMessage;
  uint64_t TotalTimeMicroseconds;
  uint64_t UserTimeMicroseconds;
  uint64_t PeakMemoryKiB;
} NevercJobResult;

typedef NevercStatus(NEVERC_CALL *NevercGetJobCountFn)(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
    uint64_t *OutCount);
typedef NevercStatus(NEVERC_CALL *NevercGetJobFn)(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
    uint64_t Index, NevercJob *OutJob);
typedef NevercStatus(NEVERC_CALL *NevercGetJobDependencyFn)(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
    NevercJobID Job, uint64_t Index, NevercJobID *OutDependency);
typedef NevercStatus(NEVERC_CALL *NevercGetJobStringFn)(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
    NevercJobID Job, uint64_t Index, NevercStringView *OutValue);
typedef NevercStatus(NEVERC_CALL *NevercGetJobFileFn)(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
    NevercJobID Job, uint64_t Index, NevercJobFile *OutFile);
typedef NevercStatus(NEVERC_CALL *NevercCreateJobGraphBuilderFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle ActionGraph, NevercJobGraphBuilderHandle *OutBuilder);
typedef NevercStatus(NEVERC_CALL *NevercBeginJobGraphMutationFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation, NevercArtifactHandle Graph,
    NevercJobGraphMutationHandle *OutMutation);
typedef NevercStatus(NEVERC_CALL *NevercAddJobFn)(
    void *Context, NevercHandle Edit, const NevercJobDescriptor *Descriptor,
    NevercJobID *OutJob);
typedef NevercStatus(NEVERC_CALL *NevercRemoveJobFn)(
    void *Context, NevercHandle Edit, NevercJobID Job);
typedef NevercStatus(NEVERC_CALL *NevercMoveJobBeforeFn)(
    void *Context, NevercHandle Edit, NevercJobID Job, NevercJobID Before);
typedef NevercStatus(NEVERC_CALL *NevercReplaceJobFn)(
    void *Context, NevercHandle Edit, NevercJobID Job,
    const NevercJobDescriptor *Descriptor);
typedef NevercStatus(NEVERC_CALL *NevercSetJobStringFn)(
    void *Context, NevercHandle Edit, NevercJobID Job, uint64_t Index,
    NevercStringView Value);
typedef NevercStatus(NEVERC_CALL *NevercSetJobFileFn)(
    void *Context, NevercHandle Edit, NevercJobID Job, uint64_t Index,
    const NevercJobFile *File);
typedef NevercStatus(NEVERC_CALL *NevercReplaceJobDependenciesFn)(
    void *Context, NevercHandle Edit, NevercJobID Job,
    NevercJobIDList Dependencies);
typedef NevercStatus(NEVERC_CALL *NevercPublishJobGraphFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercJobGraphBuilderHandle Builder, NevercArtifactHandle *OutGraph);
typedef NevercStatus(NEVERC_CALL *NevercCommitJobGraphMutationFn)(
    void *Context, NevercJobGraphMutationHandle Mutation);
typedef NevercStatus(NEVERC_CALL *NevercAbortJobGraphEditFn)(
    void *Context, NevercHandle Edit);
typedef NevercStatus(NEVERC_CALL *NevercGetJobExecutionRequestFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Request, NevercJobExecutionRequest *OutRequest);
typedef NevercStatus(NEVERC_CALL *NevercCreateJobResultFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Request,
    const NevercJobResultDescriptor *Descriptor,
    NevercArtifactHandle *OutResult);
typedef NevercStatus(NEVERC_CALL *NevercGetJobResultFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Result, NevercJobResult *OutResult);

typedef struct NevercDriverAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercGetArgumentCountFn GetArgumentCount;
  NevercGetArgumentFn GetArgument;
  NevercBeginArgumentMutationFn BeginArgumentMutation;
  NevercInsertArgumentFn InsertArgument;
  NevercReplaceArgumentFn ReplaceArgument;
  NevercEraseArgumentFn EraseArgument;
  NevercCommitArgumentMutationFn CommitArgumentMutation;
  NevercAbortArgumentMutationFn AbortArgumentMutation;
  NevercGetOptionOccurrenceCountFn GetOptionOccurrenceCount;
  NevercGetOptionOccurrenceFn GetOptionOccurrence;
  NevercBeginParsedArgumentMutationFn BeginParsedArgumentMutation;
  NevercAddOptionOccurrenceFn AddOptionOccurrence;
  NevercRemoveOptionOccurrenceFn RemoveOptionOccurrence;
  NevercReplaceOptionOccurrenceFn ReplaceOptionOccurrence;
  NevercCommitParsedArgumentMutationFn CommitParsedArgumentMutation;
  NevercAbortParsedArgumentMutationFn AbortParsedArgumentMutation;
  NevercGetToolChainRequestFn GetToolChainRequest;
  NevercBeginToolChainMutationFn BeginToolChainMutation;
  NevercSetToolChainTripleFn SetToolChainTriple;
  NevercSetToolChainCPUFn SetToolChainCPU;
  NevercSetToolChainFeaturesFn SetToolChainFeatures;
  NevercCommitToolChainMutationFn CommitToolChainMutation;
  NevercAbortToolChainMutationFn AbortToolChainMutation;
  NevercCreateToolChainSelectionFn CreateToolChainSelection;
  NevercGetToolChainSelectionFn GetToolChainSelection;
  NevercGetDriverInputCountFn GetDriverInputCount;
  NevercGetDriverInputFn GetDriverInput;
  NevercGetActionNodeCountFn GetActionNodeCount;
  NevercGetActionNodeFn GetActionNode;
  NevercGetActionNodeInputFn GetActionNodeInput;
  NevercGetActionRootCountFn GetActionRootCount;
  NevercGetActionRootFn GetActionRoot;
  NevercCreateActionGraphBuilderFn CreateActionGraphBuilder;
  NevercBeginActionGraphMutationFn BeginActionGraphMutation;
  NevercAddActionNodeFn AddActionNode;
  NevercRemoveActionNodeFn RemoveActionNode;
  NevercReplaceActionNodeInputsFn ReplaceActionNodeInputs;
  NevercSetActionNodeOutputTypeFn SetActionNodeOutputType;
  NevercSetActionNodeBindArchFn SetActionNodeBindArch;
  NevercSetActionRootsFn SetActionRoots;
  NevercPublishActionGraphFn PublishActionGraph;
  NevercCommitActionGraphMutationFn CommitActionGraphMutation;
  NevercAbortActionGraphEditFn AbortActionGraphEdit;
  NevercGetJobCountFn GetJobCount;
  NevercGetJobFn GetJob;
  NevercGetJobDependencyFn GetJobDependency;
  NevercGetJobStringFn GetJobArgument;
  NevercGetJobStringFn GetJobEnvironment;
  NevercGetJobFileFn GetJobInput;
  NevercGetJobFileFn GetJobOutput;
  NevercCreateJobGraphBuilderFn CreateJobGraphBuilder;
  NevercBeginJobGraphMutationFn BeginJobGraphMutation;
  NevercAddJobFn AddJob;
  NevercRemoveJobFn RemoveJob;
  NevercMoveJobBeforeFn MoveJobBefore;
  NevercReplaceJobFn ReplaceJob;
  NevercSetJobStringFn SetJobArgument;
  NevercSetJobStringFn SetJobEnvironment;
  NevercSetJobFileFn SetJobInput;
  NevercSetJobFileFn SetJobOutput;
  NevercReplaceJobDependenciesFn ReplaceJobDependencies;
  NevercPublishJobGraphFn PublishJobGraph;
  NevercCommitJobGraphMutationFn CommitJobGraphMutation;
  NevercAbortJobGraphEditFn AbortJobGraphEdit;
  NevercGetJobExecutionRequestFn GetJobExecutionRequest;
  NevercCreateJobResultFn CreateJobResult;
  NevercGetJobResultFn GetJobResult;
} NevercDriverAPI;

NEVERC_ABI_PACK_END

#ifdef __cplusplus
}
#endif

#endif
