#include "neverc/Plugin/Host/CoreAPIBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>

namespace neverc::plugin {
namespace {

NevercStatus makeStatus(NevercStatusCode Code) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  return Result;
}

bool normalizeAllocation(uint64_t Size, uint64_t Alignment,
                         size_t &NativeSize, size_t &NativeAlignment) {
  if (Size > std::numeric_limits<size_t>::max() || Alignment == 0 ||
      Alignment > std::numeric_limits<size_t>::max() ||
      (Alignment & (Alignment - 1)) != 0)
    return false;
  NativeSize = std::max<size_t>(static_cast<size_t>(Size), 1);
  NativeAlignment =
      std::max<size_t>(static_cast<size_t>(Alignment), alignof(max_align_t));
  return true;
}

NevercStatus NEVERC_CALL allocate(void *, uint64_t Size, uint64_t Alignment,
                                  void **OutPointer) {
  if (!OutPointer)
    return makeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutPointer = nullptr;
  size_t NativeSize = 0;
  size_t NativeAlignment = 0;
  if (!normalizeAllocation(Size, Alignment, NativeSize, NativeAlignment))
    return makeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  void *Pointer =
      ::operator new(NativeSize, std::align_val_t(NativeAlignment),
                     std::nothrow);
  if (!Pointer)
    return makeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  *OutPointer = Pointer;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL deallocate(void *, void *Pointer, uint64_t Size,
                                    uint64_t Alignment) {
  size_t NativeSize = 0;
  size_t NativeAlignment = 0;
  if (!normalizeAllocation(Size, Alignment, NativeSize, NativeAlignment))
    return makeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  (void)NativeSize;
  if (Pointer)
    ::operator delete(Pointer, std::align_val_t(NativeAlignment));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL reallocate(void *Context, void *Pointer,
                                    uint64_t OldSize, uint64_t NewSize,
                                    uint64_t Alignment, void **OutPointer) {
  if (!OutPointer)
    return makeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutPointer = nullptr;
  if (!Pointer)
    return allocate(Context, NewSize, Alignment, OutPointer);

  size_t NativeOldSize = 0;
  size_t NativeOldAlignment = 0;
  size_t NativeNewSize = 0;
  size_t NativeNewAlignment = 0;
  if (!normalizeAllocation(OldSize, Alignment, NativeOldSize,
                           NativeOldAlignment) ||
      !normalizeAllocation(NewSize, Alignment, NativeNewSize,
                           NativeNewAlignment))
    return makeStatus(NEVERC_STATUS_INVALID_ARGUMENT);

  void *Replacement = ::operator new(
      NativeNewSize, std::align_val_t(NativeNewAlignment), std::nothrow);
  if (!Replacement)
    return makeStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  std::memcpy(Replacement, Pointer,
              std::min(static_cast<size_t>(OldSize),
                       static_cast<size_t>(NewSize)));
  ::operator delete(Pointer, std::align_val_t(NativeOldAlignment));
  *OutPointer = Replacement;
  return neverc_status_ok();
}

bool validStringView(NevercStringView View, bool RequireUTF8) {
  if (View.Length > std::numeric_limits<size_t>::max() ||
      (!View.Data && View.Length != 0))
    return false;
  llvm::StringRef Text(View.Data ? View.Data : "",
                       static_cast<size_t>(View.Length));
  return !Text.contains('\0') && (!RequireUTF8 || llvm::json::isUTF8(Text));
}

NevercStatus NEVERC_CALL
emitDiagnostic(void *Context, const NevercDiagnosticDescriptor *Diagnostic,
               NevercDiagnosticHandle *OutDiagnostic) {
  if (!Context || !Diagnostic || !OutDiagnostic)
    return makeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutDiagnostic = {};
  return static_cast<PluginProcessServices *>(Context)->emitDiagnostic(
      *Diagnostic, *OutDiagnostic);
}

NevercStatus NEVERC_CALL queryInterface(
    void *Context, NevercInterfaceID Interface, uint16_t Major,
    uint16_t MinimumMinor, const void **OutTable, uint16_t *OutMinor,
    uint64_t *OutStructSize) {
  if (!Context)
    return makeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return static_cast<PluginProcessServices *>(Context)
      ->interfaces()
      .queryForC(Interface, Major, MinimumMinor, OutTable, OutMinor,
                 OutStructSize);
}

NevercStatus NEVERC_CALL checkCancelled(void *Context,
                                        NevercTaskHandle Task) {
  if (!Context)
    return makeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return static_cast<PluginProcessServices *>(Context)->checkCancelled(Task);
}

NevercStatus NEVERC_CALL getSessionState(void *Context,
                                         NevercSessionHandle Session,
                                         NevercStringView PluginID,
                                         void **OutState) {
  if (!Context || !OutState || !validStringView(PluginID, false))
    return makeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return static_cast<PluginProcessServices *>(Context)->querySessionState(
      Session,
      llvm::StringRef(PluginID.Data ? PluginID.Data : "",
                      static_cast<size_t>(PluginID.Length)),
      OutState);
}

NevercStatus NEVERC_CALL getTaskState(void *Context, NevercTaskHandle Task,
                                      NevercStringView PluginID,
                                      void **OutState) {
  if (!Context || !OutState || !validStringView(PluginID, false))
    return makeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return static_cast<PluginProcessServices *>(Context)->queryTaskState(
      Task,
      llvm::StringRef(PluginID.Data ? PluginID.Data : "",
                      static_cast<size_t>(PluginID.Length)),
      OutState);
}

NevercStatus NEVERC_CALL getPluginOptionValueCount(
    void *Context, NevercSessionHandle Session, NevercStringView PluginID,
    NevercStringView Spelling, uint64_t *OutCount) {
  if (!Context || !OutCount || !validStringView(PluginID, false) ||
      !validStringView(Spelling, false))
    return makeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return static_cast<PluginProcessServices *>(Context)
      ->queryPluginOptionValueCount(
          Session,
          llvm::StringRef(PluginID.Data ? PluginID.Data : "",
                          static_cast<size_t>(PluginID.Length)),
          llvm::StringRef(Spelling.Data ? Spelling.Data : "",
                          static_cast<size_t>(Spelling.Length)),
          OutCount);
}

NevercStatus NEVERC_CALL getPluginOptionValue(
    void *Context, NevercSessionHandle Session, NevercStringView PluginID,
    NevercStringView Spelling, uint64_t Index, NevercStringView *OutValue) {
  if (!Context || !OutValue || !validStringView(PluginID, false) ||
      !validStringView(Spelling, false))
    return makeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return static_cast<PluginProcessServices *>(Context)
      ->queryPluginOptionValue(
          Session,
          llvm::StringRef(PluginID.Data ? PluginID.Data : "",
                          static_cast<size_t>(PluginID.Length)),
          llvm::StringRef(Spelling.Data ? Spelling.Data : "",
                          static_cast<size_t>(Spelling.Length)),
          Index, OutValue);
}

} // namespace

void initializeCoreAPI(NevercCoreAPI &API,
                       PluginProcessServices &ProcessServices) {
  API = {};
  API.Header = {sizeof(NevercCoreAPI), NEVERC_CORE_API_MAJOR,
                NEVERC_CORE_API_MINOR, 0};
  API.Context = &ProcessServices;
  API.Allocate = allocate;
  API.Reallocate = reallocate;
  API.Deallocate = deallocate;
  API.EmitDiagnostic = emitDiagnostic;
  API.QueryInterface = queryInterface;
  API.CheckCancelled = checkCancelled;
  API.GetSessionState = getSessionState;
  API.GetTaskState = getTaskState;
  API.GetPluginOptionValueCount = getPluginOptionValueCount;
  API.GetPluginOptionValue = getPluginOptionValue;
}

} // namespace neverc::plugin
