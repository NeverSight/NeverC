#ifndef NEVERC_LIB_PLUGIN_FRONTEND_SEMABRIDGEINTERNAL_H
#define NEVERC_LIB_PLUGIN_FRONTEND_SEMABRIDGEINTERNAL_H

#include "neverc/Analyze/Sema.h"
#include "neverc/Plugin/Host/FrontendPluginBridge.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Tree/Core/APValue.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Expr/Expr.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace neverc::plugin {

NevercStatus semaStatus(NevercStatusCode Code);
bool semaSameHandle(NevercHandle Left, NevercHandle Right);
bool semaValidHeader(const NevercABITableHeader &Header, size_t Size);
bool semaStringView(NevercStringView View, llvm::StringRef &Out,
                    bool AllowEmpty = false);

template <typename T>
NevercStatus writeSemaRecord(T *OutValue, const T &Value) {
  if (!OutValue)
    return semaStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const uint32_t Capacity = OutValue->Header.StructSize;
  if (Capacity < sizeof(NevercABITableHeader))
    return semaStatus(NEVERC_STATUS_ABI_MISMATCH);
  const size_t Writable = std::min<size_t>(Capacity, sizeof(Value));
  std::memset(OutValue, 0, Writable);
  std::memcpy(OutValue, &Value, Writable);
  return Capacity < sizeof(Value) ? semaStatus(NEVERC_STATUS_ABI_MISMATCH)
                                  : neverc_status_ok();
}

struct PluginSemaBridge::Impl {
  struct ScopePayload {
    Impl *Owner = nullptr;
    DeclContext *Context = nullptr;
  };

  struct LookupPayload {
    Impl *Owner = nullptr;
    NevercSemaLookupResultKind Kind = NEVERC_SEMA_LOOKUP_NOT_FOUND;
    llvm::SmallVector<const NamedDecl *, 4> Candidates;
  };

  struct MutationLeasePayload {
    Impl *Owner = nullptr;
    bool Active = false;
  };

  struct ConversionPayload {
    Impl *Owner = nullptr;
    NevercTypeHandle SourceHandle{};
    NevercTypeHandle DestinationHandle{};
    QualType Source;
    QualType Destination;
    Sema::AssignConvertType NativeKind = Sema::Incompatible;
    NevercSemaConversionKind Kind = NEVERC_SEMA_CONVERSION_INCOMPATIBLE;
  };

  struct ConstantPayload {
    Impl *Owner = nullptr;
    APValue Value;
    QualType Type;
    bool HasSideEffects = false;
    std::string Text;
  };

  PluginTaskContext &Task;
  Sema &SemanticAnalyzer;
  PluginASTBridge &AST;
  FrontendPluginBridge &Locations;
  PluginSemaExtensionAPI *Extensions = nullptr;
  NevercSemaAPI API{};
  llvm::DenseMap<const DeclContext *, NevercSemaScopeHandle> ScopeHandles;
  bool MutationLeaseActive = false;

  Impl(PluginTaskContext &TaskValue, Sema &SemanticAnalyzerValue,
       PluginASTBridge &ASTValue, FrontendPluginBridge &LocationsValue);

  bool validTask(NevercTaskHandle Handle) const;
  llvm::Expected<NevercSemaScopeHandle> createScope(DeclContext *Context);
  llvm::Expected<NevercConstantValueHandle>
  createConstant(const APValue &Value, QualType Type, bool HasSideEffects);
  NevercStatus resolveScope(NevercTaskHandle TaskHandle,
                            NevercSemaScopeHandle Handle,
                            ScopePayload **OutPayload);
  NevercStatus resolveLookup(NevercTaskHandle TaskHandle,
                             NevercLookupResultHandle Handle,
                             LookupPayload **OutPayload);
  NevercStatus resolveLease(NevercTaskHandle TaskHandle,
                            NevercSemaMutationLeaseHandle Handle,
                            MutationLeasePayload **OutPayload);
  NevercStatus resolveConversion(NevercTaskHandle TaskHandle,
                                 NevercConversionSequenceHandle Handle,
                                 ConversionPayload **OutPayload);
  NevercStatus resolveConstant(NevercTaskHandle TaskHandle,
                               NevercConstantValueHandle Handle,
                               ConstantPayload **OutPayload);

  static NevercStatus NEVERC_CALL getCurrentScope(
      void *Context, NevercTaskHandle Task, NevercSemaScopeHandle *OutScope);
  static NevercStatus NEVERC_CALL
  getScopeInfo(void *Context, NevercTaskHandle Task,
               NevercSemaScopeHandle Scope, NevercSemaScopeInfo *OutInfo);
  static NevercStatus NEVERC_CALL getScopeDeclaration(
      void *Context, NevercTaskHandle Task, NevercSemaScopeHandle Scope,
      uint64_t Index, NevercDeclHandle *OutDeclaration);
  static NevercStatus NEVERC_CALL lookupName(
      void *Context, NevercTaskHandle Task,
      const NevercSemaLookupRequest *Request,
      NevercLookupResultHandle *OutResult);
  static NevercStatus NEVERC_CALL getLookupResultInfo(
      void *Context, NevercTaskHandle Task, NevercLookupResultHandle Result,
      NevercSemaLookupResultInfo *OutInfo);
  static NevercStatus NEVERC_CALL getLookupCandidate(
      void *Context, NevercTaskHandle Task, NevercLookupResultHandle Result,
      uint64_t Index, NevercDeclHandle *OutDeclaration);
  static NevercStatus NEVERC_CALL destroyLookupResult(
      void *Context, NevercTaskHandle Task, NevercLookupResultHandle Result);
  static NevercStatus NEVERC_CALL acquireMutationLease(
      void *Context, NevercTaskHandle Task,
      NevercSemaMutationLeaseHandle *OutLease);
  static NevercStatus NEVERC_CALL releaseMutationLease(
      void *Context, NevercTaskHandle Task,
      NevercSemaMutationLeaseHandle Lease);
  static NevercStatus NEVERC_CALL
  getBuiltinType(void *Context, NevercTaskHandle Task,
                 NevercBuiltinTypeKind Kind, NevercTypeHandle *OutType);
  static NevercStatus NEVERC_CALL createPointerType(
      void *Context, NevercTaskHandle Task,
      NevercSemaMutationLeaseHandle Lease, NevercTypeHandle Pointee,
      NevercTypeHandle *OutType);
  static NevercStatus NEVERC_CALL createConstantArrayType(
      void *Context, NevercTaskHandle Task,
      NevercSemaMutationLeaseHandle Lease, NevercTypeHandle Element,
      uint64_t ElementCount, NevercTypeHandle *OutType);
  static NevercStatus NEVERC_CALL createFunctionType(
      void *Context, NevercTaskHandle Task,
      NevercSemaMutationLeaseHandle Lease,
      const NevercSemaFunctionTypeDescriptor *Descriptor,
      NevercTypeHandle *OutType);
  static NevercStatus NEVERC_CALL
  getTagType(void *Context, NevercTaskHandle Task,
             NevercDeclHandle Declaration, NevercTypeHandle *OutType);
  static NevercStatus NEVERC_CALL createAtomicType(
      void *Context, NevercTaskHandle Task,
      NevercSemaMutationLeaseHandle Lease, NevercTypeHandle ValueType,
      NevercTypeHandle *OutType);
  static NevercStatus NEVERC_CALL createVectorType(
      void *Context, NevercTaskHandle Task,
      NevercSemaMutationLeaseHandle Lease, NevercTypeHandle Element,
      uint32_t ElementCount, NevercSemaVectorKind Kind,
      NevercTypeHandle *OutType);
  static NevercStatus NEVERC_CALL
  getCanonicalType(void *Context, NevercTaskHandle Task, NevercTypeHandle Type,
                   NevercTypeHandle *OutType);
  static NevercStatus NEVERC_CALL areTypesCompatible(
      void *Context, NevercTaskHandle Task, NevercTypeHandle Left,
      NevercTypeHandle Right, NevercBool *OutCompatible);
  static NevercStatus NEVERC_CALL classifyImplicitConversion(
      void *Context, NevercTaskHandle Task, NevercTypeHandle Source,
      NevercTypeHandle Destination, NevercConversionSequenceHandle *OutSequence);
  static NevercStatus NEVERC_CALL getConversionSequenceInfo(
      void *Context, NevercTaskHandle Task,
      NevercConversionSequenceHandle Sequence,
      NevercSemaConversionSequenceInfo *OutInfo);
  static NevercStatus NEVERC_CALL applyImplicitConversion(
      void *Context, NevercTaskHandle Task,
      NevercSemaMutationLeaseHandle Lease,
      NevercConversionSequenceHandle Sequence, NevercExprHandle Expression,
      NevercSemaConversionContext ConversionContext,
      NevercExprHandle *OutExpression);
  static NevercStatus NEVERC_CALL createExplicitCast(
      void *Context, NevercTaskHandle Task,
      NevercSemaMutationLeaseHandle Lease, NevercExprHandle Expression,
      NevercTypeHandle Destination, NevercExprHandle *OutExpression);
  static NevercStatus NEVERC_CALL destroyConversionSequence(
      void *Context, NevercTaskHandle Task,
      NevercConversionSequenceHandle Sequence);
  static NevercStatus NEVERC_CALL evaluateConstant(
      void *Context, NevercTaskHandle Task, NevercExprHandle Expression,
      NevercConstantValueHandle *OutValue);
  static NevercStatus NEVERC_CALL getConstantValueInfo(
      void *Context, NevercTaskHandle Task, NevercConstantValueHandle Value,
      NevercSemaConstantValueInfo *OutInfo);
  static NevercStatus NEVERC_CALL getConstantIntegerWord(
      void *Context, NevercTaskHandle Task, NevercConstantValueHandle Value,
      uint64_t Index, uint64_t *OutWord);
  static NevercStatus NEVERC_CALL getConstantElement(
      void *Context, NevercTaskHandle Task, NevercConstantValueHandle Value,
      uint64_t Index, NevercConstantValueHandle *OutElement);
  static NevercStatus NEVERC_CALL destroyConstantValue(
      void *Context, NevercTaskHandle Task, NevercConstantValueHandle Value);
  static NevercStatus NEVERC_CALL hasDeclAttribute(
      void *Context, NevercTaskHandle Task, NevercDeclHandle Declaration,
      NevercStringView AttributeName, NevercBool *OutPresent);
  static NevercStatus NEVERC_CALL
  getBuiltinInfo(void *Context, NevercTaskHandle Task, NevercStringView Name,
                 NevercSemaBuiltinInfo *OutInfo);
  static NevercStatus NEVERC_CALL emitDiagnostic(
      void *Context, NevercTaskHandle Task,
      NevercSemaMutationLeaseHandle Lease,
      const NevercSemaDiagnosticDescriptor *Descriptor);
  static NevercStatus NEVERC_CALL getExpressionExtensionInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaExpressionExtensionInput *OutInput);
  static NevercStatus NEVERC_CALL createExpressionExtensionOutput(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaExpressionExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput);
  static NevercStatus NEVERC_CALL getStatementExtensionInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaStatementExtensionInput *OutInput);
  static NevercStatus NEVERC_CALL createStatementExtensionOutput(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaStatementExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput);
  static NevercStatus NEVERC_CALL getDeclarationExtensionInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaDeclarationExtensionInput *OutInput);
  static NevercStatus NEVERC_CALL createDeclarationExtensionOutput(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaDeclarationExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput);
  static NevercStatus NEVERC_CALL getTypeExtensionInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaTypeExtensionInput *OutInput);
  static NevercStatus NEVERC_CALL createTypeExtensionOutput(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaTypeExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput);
  static NevercStatus NEVERC_CALL getLookupExtensionInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaLookupExtensionInput *OutInput);
  static NevercStatus NEVERC_CALL createLookupExtensionOutput(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaLookupExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput);
  static NevercStatus NEVERC_CALL getConversionExtensionInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaConversionExtensionInput *OutInput);
  static NevercStatus NEVERC_CALL createConversionExtensionOutput(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaConversionExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput);
};

} // namespace neverc::plugin

#endif
