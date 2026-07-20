#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "IRBuilderPluginBridge.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include <new>
#include <utility>

using namespace llvm;

namespace neverc::plugin {

IRPluginBridge::IRPluginBridge(PluginTaskContext &TaskValue)
    : Task(TaskValue) {
  initializeCoreAPI();
  initializeSerializationAPI();
}

IRPluginBridge::~IRPluginBridge() {
  BuilderBridge.reset();
  invalidateSerializedBuffers();
  invalidateHandles();
  Module = nullptr;
  OwnedModule.reset();
  Context = nullptr;
  OwnedContext.reset();
}

Expected<std::unique_ptr<IRPluginBridge>>
IRPluginBridge::create(PluginTaskContext &Task, StringRef ModuleIdentifier) {
  auto Bridge =
      std::unique_ptr<IRPluginBridge>(new (std::nothrow) IRPluginBridge(Task));
  if (!Bridge)
    return createStringError(inconvertibleErrorCode(),
                             "IR plugin bridge allocation failed");
  if (Error E = Bridge->initialize(ModuleIdentifier))
    return std::move(E);
  return std::move(Bridge);
}

Expected<std::unique_ptr<IRPluginBridge>>
IRPluginBridge::createInContext(PluginTaskContext &Task, LLVMContext &Context,
                                StringRef ModuleIdentifier) {
  auto Bridge =
      std::unique_ptr<IRPluginBridge>(new (std::nothrow) IRPluginBridge(Task));
  if (!Bridge)
    return createStringError(inconvertibleErrorCode(),
                             "IR plugin bridge allocation failed");
  if (Error E = Bridge->initializeInContext(Context, ModuleIdentifier))
    return std::move(E);
  return std::move(Bridge);
}

Expected<std::unique_ptr<IRPluginBridge>>
IRPluginBridge::adopt(PluginTaskContext &Task,
                      std::unique_ptr<LLVMContext> Context,
                      std::unique_ptr<llvm::Module> Module) {
  auto Bridge =
      std::unique_ptr<IRPluginBridge>(new (std::nothrow) IRPluginBridge(Task));
  if (!Bridge)
    return createStringError(inconvertibleErrorCode(),
                             "IR plugin bridge allocation failed");
  if (Error E =
          Bridge->initializeOwned(std::move(Context), std::move(Module)))
    return std::move(E);
  return std::move(Bridge);
}

Expected<std::unique_ptr<IRPluginBridge>>
IRPluginBridge::borrow(PluginTaskContext &Task, llvm::Module &Module) {
  auto Bridge =
      std::unique_ptr<IRPluginBridge>(new (std::nothrow) IRPluginBridge(Task));
  if (!Bridge)
    return createStringError(inconvertibleErrorCode(),
                             "IR plugin bridge allocation failed");
  if (Error E = Bridge->initializeBorrowed(Module))
    return std::move(E);
  return std::move(Bridge);
}

Error IRPluginBridge::initialize(StringRef ModuleIdentifier) {
  auto OwnedContext = std::make_unique<LLVMContext>();
  auto OwnedModule =
      std::make_unique<llvm::Module>(ModuleIdentifier, *OwnedContext);
  return initializeOwned(std::move(OwnedContext), std::move(OwnedModule));
}

Error IRPluginBridge::initializeOwned(
    std::unique_ptr<LLVMContext> NewContext,
    std::unique_ptr<llvm::Module> OwnedModule) {
  if (!NewContext || !OwnedModule)
    return createStringError(inconvertibleErrorCode(),
                             "IR context and module must be non-null");
  if (&OwnedModule->getContext() != NewContext.get())
    return createStringError(inconvertibleErrorCode(),
                             "IR module belongs to another context");
  OwnedContext = std::move(NewContext);
  return initializeModule(*OwnedContext, std::move(OwnedModule));
}

Error IRPluginBridge::initializeInContext(LLVMContext &NewContext,
                                          StringRef ModuleIdentifier) {
  auto NewModule =
      std::make_unique<llvm::Module>(ModuleIdentifier, NewContext);
  return initializeModule(NewContext, std::move(NewModule));
}

Error IRPluginBridge::initializeModule(
    LLVMContext &NewContext, std::unique_ptr<llvm::Module> NewModule) {
  if (!NewModule || &NewModule->getContext() != &NewContext)
    return createStringError(inconvertibleErrorCode(),
                             "IR module belongs to another context");
  OwnedModule = std::move(NewModule);
  Module = OwnedModule.get();
  return initializeModuleHandles(NewContext, *Module);
}

Error IRPluginBridge::initializeBorrowed(llvm::Module &NewModule) {
  Module = &NewModule;
  return initializeModuleHandles(NewModule.getContext(), NewModule);
}

Error IRPluginBridge::initializeModuleHandles(LLVMContext &NewContext,
                                               llvm::Module &NewModule) {
  if (&NewModule.getContext() != &NewContext)
    return createStringError(inconvertibleErrorCode(),
                             "IR module belongs to another context");
  if (NewModule.getModuleIdentifier().empty())
    return createStringError(inconvertibleErrorCode(),
                             "IR module identifier must not be empty");
  if (Task.kind() != NEVERC_TASK_TRANSLATION_UNIT &&
      Task.kind() != NEVERC_TASK_LTO &&
      Task.kind() != NEVERC_TASK_CODEGEN &&
      Task.kind() != NEVERC_TASK_DYNCODE)
    return createStringError(inconvertibleErrorCode(),
                             "plugin task kind cannot own LLVM IR");

  Context = &NewContext;

  auto CreatedContext =
      Task.handles().create(PluginIRContextHandleKind, Context);
  if (!CreatedContext)
    return CreatedContext.takeError();
  ContextHandle = *CreatedContext;

  auto CreatedModule =
      Task.handles().create(PluginIRModuleHandleKind, Module);
  if (!CreatedModule) {
    (void)Task.handles().release(ContextHandle,
                                 PluginIRContextHandleKind);
    ContextHandle = {};
    return CreatedModule.takeError();
  }
  ModuleHandle = *CreatedModule;
  auto CreatedBuilder = IRBuilderPluginBridge::create(*this);
  if (!CreatedBuilder)
    return CreatedBuilder.takeError();
  BuilderBridge = std::move(*CreatedBuilder);
  return Error::success();
}

LLVMContext &IRPluginBridge::context() const { return *Context; }

llvm::Module &IRPluginBridge::module() const { return *Module; }

std::unique_ptr<llvm::Module> IRPluginBridge::releaseModule() {
  if (!OwnedModule)
    return nullptr;
  invalidateModuleHandles();
  Module = nullptr;
  return std::move(OwnedModule);
}

NevercTaskHandle IRPluginBridge::taskHandle() const {
  return Task.handle();
}

const NevercIRBuilderAPI &IRPluginBridge::builderAPI() const {
  return BuilderBridge->api();
}

NevercStatus IRPluginBridge::commitInProgressFunctionMutation(
    NevercIRMutationHandle Mutation) {
  return BuilderBridge->commitInProgressFunctionMutation(Mutation);
}

void IRPluginBridge::invalidateHandles() {
  invalidateModuleHandles();
  for (const auto &Entry : AttributeHandles)
    (void)Task.handles().release(Entry.second,
                                 PluginIRAttributeHandleKind);
  AttributeHandles.clear();
  for (const auto &Entry : TypeHandles)
    (void)Task.handles().release(Entry.second, PluginIRTypeHandleKind);
  TypeHandles.clear();
  if (ContextHandle.Owner != 0 && ContextHandle.Value != 0)
    (void)Task.handles().release(ContextHandle, PluginIRContextHandleKind);
  ContextHandle = {};
}

void IRPluginBridge::invalidateModuleHandles() {
  for (const auto &Entry : ComdatHandles)
    (void)Task.handles().release(Entry.second,
                                 PluginIRComdatHandleKind);
  ComdatHandles.clear();
  for (const auto &Entry : NamedMetadataHandles)
    (void)Task.handles().release(Entry.second,
                                 PluginIRNamedMetadataHandleKind);
  NamedMetadataHandles.clear();
  for (const auto &Entry : MetadataHandles)
    (void)Task.handles().release(Entry.second,
                                 PluginIRMetadataHandleKind);
  MetadataHandles.clear();
  for (const auto &Entry : ValueHandles)
    (void)Task.handles().release(Entry.second, PluginIRValueHandleKind);
  ValueHandles.clear();
  if (ModuleHandle.Owner != 0 && ModuleHandle.Value != 0)
    (void)Task.handles().release(ModuleHandle, PluginIRModuleHandleKind);
  ModuleHandle = {};
}

} // namespace neverc::plugin
