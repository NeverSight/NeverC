#include "BridgeCastHelpers.h"
#include "HostAPIBridge.h"
#include "neverc/Plugin/PluginLoader.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <cerrno>
#include <chrono>
#include <cstdlib>

using namespace llvm;

namespace neverc {
namespace plugin {

bool gShellcodeModeEnabled = false;

// ===----------------------------------------------------------------------===
//  Memory -- route through host process allocator
//  When NEVERC_ENABLE_MIMALLOC=ON the neverc binary links mimalloc with
//  MI_OVERRIDE=ON + whole-archive, so ::malloc/::realloc/::free are
//  transparently replaced by mi_malloc/mi_realloc/mi_free at the process
//  level.  Plugin allocations through these pointers thus go through the
//  host's mimalloc heap -- no CRT boundary crossing on Windows.
// ===----------------------------------------------------------------------===
void *bridgeAlloc(uint64_t Size) {
  if (LLVM_UNLIKELY(exceedsSizeT(Size)))
    return nullptr;
  return ::malloc(static_cast<size_t>(Size));
}

void *bridgeRealloc(void *Ptr, uint64_t Size) {
  if (LLVM_UNLIKELY(exceedsSizeT(Size)))
    return nullptr;
  return ::realloc(Ptr, static_cast<size_t>(Size));
}

void bridgeFree(void *Ptr) { ::free(Ptr); }

// ===----------------------------------------------------------------------===
//  Diagnostics -- simple stderr output for now
// ===----------------------------------------------------------------------===

void bridgeDiagNote(const char *Msg) {
  WithColor::note(errs(), "neverc-plugin") << (Msg ? Msg : "") << "\n";
}
void bridgeDiagWarning(const char *Msg) {
  WithColor::warning(errs(), "neverc-plugin") << (Msg ? Msg : "") << "\n";
}
void bridgeDiagError(const char *Msg) {
  WithColor::error(errs(), "neverc-plugin") << (Msg ? Msg : "") << "\n";
}

// ===----------------------------------------------------------------------===
//  Register stubs -- safe no-ops so the vtable is never null for these slots.
//  PluginLoader replaces them with real registrars during RegisterPasses and
//  restores NoOps afterward.
// ===----------------------------------------------------------------------===

static void bridgeNoOpRegisterModulePass(void *, NevercHookPoint,
                                         NevercModulePassFn, void *,
                                         const char *) {}
static void bridgeNoOpRegisterMachinePass(void *, NevercHookPoint,
                                          NevercMachinePassFn, void *,
                                          const char *) {}
static void bridgeNoOpRegisterBinaryPass(void *, NevercHookPoint,
                                         NevercBinaryPassFn, void *,
                                         const char *) {}
static void bridgeNoOpRegisterLinkerPass(void *, NevercHookPoint,
                                         NevercLinkerPassFn, void *,
                                         const char *) {}

// ===----------------------------------------------------------------------===
//  Binary buffer ops
// ===----------------------------------------------------------------------===

static int bridgeBinaryResize(uint8_t **Data, uint64_t *Len,
                               uint64_t *Capacity, uint64_t NewLen) {
  if (LLVM_UNLIKELY(!Data || !Len || !Capacity))
    return 0;
  if (NewLen <= *Capacity) {
    *Len = NewLen;
    return 1;
  }
  uint64_t NewCap;
  if (NewLen > (UINT64_MAX / 2))
    NewCap = NewLen;
  else
    NewCap = NewLen * 2;
  if (NewCap < 64)
    NewCap = 64;
  uint8_t *New = static_cast<uint8_t *>(bridgeRealloc(*Data, NewCap));
  if (LLVM_UNLIKELY(!New))
    return 0;
  *Data = New;
  *Len = NewLen;
  *Capacity = NewCap;
  return 1;
}

// ===----------------------------------------------------------------------===
//  Compilation mode queries
//  The shellcode library publishes its current state via
//  setShellcodeModeState() so that the Plugin lib does not need to depend
//  on Shellcode (which itself depends on Plugin).
// ===----------------------------------------------------------------------===

namespace {
struct ShellcodeModeState {
  bool Enabled = false;
  SmallString<64> EntrySymbol;
};
ShellcodeModeState &shellcodeModeStorage() {
  static ShellcodeModeState S;
  return S;
}
} // namespace

void setShellcodeModeState(bool Enabled, llvm::StringRef EntrySymbol) {
  auto &S = shellcodeModeStorage();
  S.Enabled = Enabled;
  S.EntrySymbol = EntrySymbol;
  gShellcodeModeEnabled = Enabled;
}

static int bridgeHostIsShellcodeMode(void) {
  return gShellcodeModeEnabled;
}

static const char *bridgeHostGetShellcodeEntrySymbol(void) {
  auto &S = shellcodeModeStorage();
  if (!S.Enabled)
    return "";
  return S.EntrySymbol.c_str();
}

// ===----------------------------------------------------------------------===
//  Plugin argument storage
//  Populated by setPluginArgs() before plugins are loaded.  Bridge functions
//  look up into this map so plugins can query -fplugin-pass-arg=key=value.
// ===----------------------------------------------------------------------===

namespace {
struct PluginArgStorage {
  StringMap<SmallString<32>> Args;
};
PluginArgStorage &pluginArgStorage() {
  static PluginArgStorage S;
  return S;
}
} // namespace

void setPluginArgs(const std::vector<std::string> &RawArgs) {
  auto &Store = pluginArgStorage();
  Store.Args.clear();
  for (const auto &A : RawArgs) {
    auto [Key, Val] = StringRef(A).split('=');
    Store.Args[Key] = Val;
  }
}

const char *bridgePluginGetArg(const char *Key) {
  if (LLVM_UNLIKELY(!Key))
    return nullptr;
  auto &Args = pluginArgStorage().Args;
  auto It = Args.find(Key);
  if (It == Args.end())
    return nullptr;
  return It->second.c_str();
}

static int bridgePluginHasArg(const char *Key) {
  if (LLVM_UNLIKELY(!Key))
    return 0;
  return pluginArgStorage().Args.count(Key) != 0;
}

static unsigned bridgePluginGetArgCount(void) {
  return static_cast<unsigned>(pluginArgStorage().Args.size());
}

// ===----------------------------------------------------------------------===
//  Batch collection
// ===----------------------------------------------------------------------===

static void bridgeModuleCollectFunctions(NevercModuleRef M,
                                         NevercValueRef *Out) {
  if (LLVM_UNLIKELY(!M || !Out))
    return;
  unsigned Idx = 0;
  for (auto &F : *unwrap(M))
    Out[Idx++] = wrapV(&F);
}

static void bridgeModuleCollectGlobals(NevercModuleRef M,
                                       NevercValueRef *Out) {
  if (LLVM_UNLIKELY(!M || !Out))
    return;
  unsigned Idx = 0;
  for (auto &G : unwrap(M)->globals())
    Out[Idx++] = wrapV(&G);
}

static unsigned bridgeModuleGetAliasCount(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return 0;
  return unwrap(M)->alias_size();
}

static void bridgeModuleCollectAliases(NevercModuleRef M,
                                       NevercValueRef *Out) {
  if (LLVM_UNLIKELY(!M || !Out))
    return;
  unsigned Idx = 0;
  for (auto &A : unwrap(M)->aliases())
    Out[Idx++] = wrapV(&A);
}

static NevercValueRef *
bridgeModuleCollectAllFunctions(NevercModuleRef M, unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!M || !OutCount))
    return nullptr;
  auto *Mod = unwrap(M);
  size_t Count = Mod->size();
  if (LLVM_UNLIKELY(Count == 0 || Count > UINT_MAX))
    return nullptr;
  auto *Buf = static_cast<NevercValueRef *>(
      bridgeAlloc(static_cast<uint64_t>(Count) * sizeof(NevercValueRef)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  unsigned Idx = 0;
  for (auto &F : *Mod)
    Buf[Idx++] = wrapV(&F);
  *OutCount = Idx;
  return Buf;
}

static NevercValueRef *
bridgeModuleCollectAllGlobals(NevercModuleRef M, unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!M || !OutCount))
    return nullptr;
  auto *Mod = unwrap(M);
  size_t Count = Mod->global_size();
  if (LLVM_UNLIKELY(Count == 0 || Count > UINT_MAX))
    return nullptr;
  auto *Buf = static_cast<NevercValueRef *>(
      bridgeAlloc(static_cast<uint64_t>(Count) * sizeof(NevercValueRef)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  unsigned Idx = 0;
  for (auto &G : Mod->globals())
    Buf[Idx++] = wrapV(&G);
  *OutCount = Idx;
  return Buf;
}

static NevercValueRef *
bridgeModuleCollectAllInstructions(NevercModuleRef M, unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!M || !OutCount))
    return nullptr;
  auto *Mod = unwrap(M);

  // Two-pass: count via BB.size() (O(1) per BB), then exact alloc + fill.
  // BB.size() is O(1) in modern LLVM, so the count pass only touches
  // Function and BasicBlock nodes -- never instruction nodes.  This beats
  // geometric growth because it eliminates all realloc copies.
  size_t Total = 0;
  for (auto &F : *Mod) {
    if (F.isDeclaration())
      continue;
    for (auto &BB : F)
      Total += BB.size();
  }
  if (LLVM_UNLIKELY(Total == 0 || Total > UINT_MAX))
    return nullptr;

  auto *Buf = static_cast<NevercValueRef *>(
      bridgeAlloc(static_cast<uint64_t>(Total) * sizeof(NevercValueRef)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;

  unsigned Idx = 0;
  for (auto &F : *Mod) {
    if (F.isDeclaration())
      continue;
    for (auto &BB : F)
      for (auto &I : BB)
        Buf[Idx++] = wrapV(&I);
  }

  *OutCount = Idx;
  return Buf;
}
static void bridgeFunctionCollectBBs(NevercValueRef F,
                                     NevercBasicBlockRef *Out) {
  if (LLVM_UNLIKELY(!F || !Out))
    return;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || Fn->isDeclaration()))
    return;
  unsigned Idx = 0;
  for (auto &BB : *Fn)
    Out[Idx++] = wrapBB(&BB);
}

static void bridgeBBCollectInstructions(NevercBasicBlockRef BB,
                                        NevercValueRef *Out) {
  if (LLVM_UNLIKELY(!BB || !Out))
    return;
  unsigned Idx = 0;
  for (auto &Inst : *unwrapBB(BB))
    Out[Idx++] = wrapV(&Inst);
}

// ===----------------------------------------------------------------------===
//  Batch opcode collection -- direct-iterate without wrap/unwrap of every
//  Instruction handle.  Cheaper than collect-then-foreach because the
//  call to InstGetOpcode does an unwrap each time; we avoid that hop.
// ===----------------------------------------------------------------------===

static unsigned bridgeBBCollectOpcodes(NevercBasicBlockRef BB,
                                       unsigned *OutOpcodes) {
  if (LLVM_UNLIKELY(!BB || !OutOpcodes))
    return 0;
  unsigned Idx = 0;
  for (auto &Inst : *unwrapBB(BB))
    OutOpcodes[Idx++] = Inst.getOpcode();
  return Idx;
}

static unsigned *bridgeModuleCollectAllOpcodes(NevercModuleRef M,
                                               unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!M || !OutCount))
    return nullptr;
  auto *Mod = unwrap(M);

  // Same exact-count approach as bridgeModuleCollectAllInstructions:
  // BB::size() is O(1) so the count pass touches only Function and BB
  // nodes; the fill pass walks instructions exactly once.
  size_t Total = 0;
  for (auto &F : *Mod) {
    if (F.isDeclaration())
      continue;
    for (auto &BB : F)
      Total += BB.size();
  }
  if (LLVM_UNLIKELY(Total == 0 || Total > UINT_MAX))
    return nullptr;

  auto *Buf = static_cast<unsigned *>(
      bridgeAlloc(static_cast<uint64_t>(Total) * sizeof(unsigned)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;

  size_t Idx = 0;
  for (auto &F : *Mod) {
    if (F.isDeclaration())
      continue;
    for (auto &BB : F)
      for (auto &Inst : BB)
        Buf[Idx++] = Inst.getOpcode();
  }

  *OutCount = static_cast<unsigned>(Idx);
  return Buf;
}

static unsigned bridgeFunctionGetInstructionCount(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return 0;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || Fn->isDeclaration()))
    return 0;
  unsigned Count = 0;
  for (const auto &BB : *Fn)
    Count += BB.size();
  return Count;
}

// ===----------------------------------------------------------------------===
//  One-call defined function collection
// ===----------------------------------------------------------------------===

static NevercValueRef *
bridgeModuleCollectDefinedFunctions(NevercModuleRef M, unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!M || !OutCount))
    return nullptr;
  auto *Mod = unwrap(M);

  // Two-pass: exact count then exact allocation.  The arena variant uses
  // Mod->size() as an upper bound (over-allocation is free for arenas),
  // but the heap variant must allocate precisely since the caller Frees
  // the buffer -- over-allocating wastes memory for modules where
  // declarations vastly outnumber definitions.
  unsigned Count = 0;
  for (auto &F : *Mod)
    if (!F.isDeclaration())
      ++Count;
  if (LLVM_UNLIKELY(Count == 0 ||
                    Count > SIZE_MAX / sizeof(NevercValueRef)))
    return nullptr;

  auto *Buf = static_cast<NevercValueRef *>(
      bridgeAlloc(static_cast<uint64_t>(Count) * sizeof(NevercValueRef)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  unsigned Idx = 0;
  for (auto &F : *Mod)
    if (!F.isDeclaration())
      Buf[Idx++] = wrapV(&F);
  *OutCount = Count;
  return Buf;
}

// ===----------------------------------------------------------------------===
//  Defined function iterators
//  Skip declarations at the C++ level so the plugin pays one vtable call
//  per defined function instead of two (GetNext + IsDeclaration).  For
//  modules with many declarations (system headers, libc) this eliminates
//  the per-declaration vtable overhead entirely.
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeModuleGetFirstDefinedFunction(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return nullptr;
  for (auto &F : *unwrap(M))
    if (!F.isDeclaration())
      return wrapV(&F);
  return nullptr;
}

static NevercValueRef
bridgeModuleGetNextDefinedFunction(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || !Fn->getParent()))
    return nullptr;
  auto It = std::next(Fn->getIterator());
  auto End = Fn->getParent()->end();
  while (It != End) {
    if (!It->isDeclaration())
      return wrapV(&*It);
    ++It;
  }
  return nullptr;
}

// ===----------------------------------------------------------------------===
//  Timing and module metadata
// ===----------------------------------------------------------------------===

static uint64_t bridgeMonotonicNanos(void) {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<nanoseconds>(steady_clock::now().time_since_epoch())
          .count());
}

// Module endianness -- thin wrapper over llvm::DataLayout::isLittleEndian.
// Returns 1 for LE / 0 for BE.  Defaults to LE (1) when the module pointer
// is null so passes can use the result unconditionally.
static int bridgeModuleIsLittleEndian(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return 1;
  return unwrap(M)->getDataLayout().isLittleEndian() ? 1 : 0;
}

// ===----------------------------------------------------------------------===
//  Zero-allocation callback iteration over IR structures
//  One vtable call replaces N GetNext vtable calls.  The callback runs
//  entirely inside the host process, so per-element overhead is a single
//  indirect call (function pointer) rather than two (vtable lookup +
//  function pointer).  Early exit when the callback returns non-zero.
// ===----------------------------------------------------------------------===

static void bridgeModuleForEachFunction(
    NevercModuleRef M, int (*Fn)(NevercValueRef F, void *Ctx), void *Ctx) {
  if (LLVM_UNLIKELY(!M || !Fn))
    return;
  for (auto &F : *unwrap(M))
    if (Fn(wrapV(&F), Ctx) != 0)
      return;
}

static void bridgeModuleForEachDefinedFunction(
    NevercModuleRef M, int (*Fn)(NevercValueRef F, void *Ctx), void *Ctx) {
  if (LLVM_UNLIKELY(!M || !Fn))
    return;
  for (auto &F : *unwrap(M))
    if (!F.isDeclaration())
      if (Fn(wrapV(&F), Ctx) != 0)
        return;
}

static void bridgeFunctionForEachBB(
    NevercValueRef F, int (*Fn)(NevercBasicBlockRef BB, void *Ctx),
    void *Ctx) {
  if (LLVM_UNLIKELY(!F || !Fn))
    return;
  auto *Func = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Func || Func->isDeclaration()))
    return;
  for (auto &BB : *Func)
    if (Fn(wrapBB(&BB), Ctx) != 0)
      return;
}

static void bridgeBBForEachInst(
    NevercBasicBlockRef BB, int (*Fn)(NevercValueRef I, void *Ctx),
    void *Ctx) {
  if (LLVM_UNLIKELY(!BB || !Fn))
    return;
  for (auto &I : *unwrapBB(BB))
    if (Fn(wrapV(&I), Ctx) != 0)
      return;
}

static void bridgeModuleForEachGlobal(
    NevercModuleRef M, int (*Fn)(NevercValueRef G, void *Ctx), void *Ctx) {
  if (LLVM_UNLIKELY(!M || !Fn))
    return;
  for (auto &G : unwrap(M)->globals())
    if (Fn(wrapV(&G), Ctx) != 0)
      return;
}

static void bridgeModuleForEachInstruction(
    NevercModuleRef M, int (*Fn)(NevercValueRef I, void *Ctx), void *Ctx) {
  if (LLVM_UNLIKELY(!M || !Fn))
    return;
  for (auto &F : *unwrap(M)) {
    if (F.isDeclaration())
      continue;
    for (auto &BB : F)
      for (auto &I : BB)
        if (Fn(wrapV(&I), Ctx) != 0)
          return;
  }
}
// ===----------------------------------------------------------------------===
//  Alias / Use / per-function-instruction callback iteration
// ===----------------------------------------------------------------------===

static void bridgeModuleForEachAlias(
    NevercModuleRef M, int (*Fn)(NevercValueRef A, void *Ctx), void *Ctx) {
  if (LLVM_UNLIKELY(!M || !Fn))
    return;
  for (auto &A : unwrap(M)->aliases())
    if (Fn(wrapV(&A), Ctx) != 0)
      return;
}

static void bridgeValueForEachUse(
    NevercValueRef V, int (*Fn)(NevercUseRef U, void *Ctx), void *Ctx) {
  if (LLVM_UNLIKELY(!V || !Fn))
    return;
  for (auto &U : unwrapV(V)->uses())
    if (Fn(reinterpret_cast<NevercUseRef>(&U), Ctx) != 0)
      return;
}

static void bridgeFunctionForEachInst(
    NevercValueRef F, int (*Fn)(NevercValueRef I, void *Ctx), void *Ctx) {
  if (LLVM_UNLIKELY(!F || !Fn))
    return;
  auto *Func = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Func || Func->isDeclaration()))
    return;
  for (auto &BB : *Func)
    for (auto &I : BB)
      if (Fn(wrapV(&I), Ctx) != 0)
        return;
}

// ===----------------------------------------------------------------------===
//  Typed plugin argument helpers -- centralized parsing / range checking.
//  All three helpers return Default when the key is absent or the value
//  fails to parse, so callers can write a single-line lookup.
// ===----------------------------------------------------------------------===

static int bridgePluginGetArgBool(const char *Key, int Default) {
  const char *V = bridgePluginGetArg(Key);
  if (LLVM_UNLIKELY(!V))
    return Default;
  while (bridgeIsWhitespace(static_cast<unsigned char>(*V)))
    ++V;
  if (*V == '\0')
    return Default;

  const char *End = V;
  while (*End && !bridgeIsWhitespace(static_cast<unsigned char>(*End)))
    ++End;
  for (const char *T = End; *T; ++T)
    if (LLVM_UNLIKELY(!bridgeIsWhitespace(static_cast<unsigned char>(*T))))
      return Default;

  size_t Len = static_cast<size_t>(End - V);
  if (Len == 1) {
    char C = V[0];
    if (C == '1' || C == 'y' || C == 'Y' || C == 't' || C == 'T')
      return 1;
    if (C == '0' || C == 'n' || C == 'N' || C == 'f' || C == 'F')
      return 0;
    return Default;
  }

  char Buf[8];
  if (LLVM_UNLIKELY(Len >= sizeof(Buf)))
    return Default;
  std::memcpy(Buf, V, Len);
  Buf[Len] = '\0';

  static const char *const TrueWords[] = {"true", "yes", "on"};
  static const char *const FalseWords[] = {"false", "no", "off"};
  for (const char *W : TrueWords)
    if (bridgeStrIEqual(Buf, W))
      return 1;
  for (const char *W : FalseWords)
    if (bridgeStrIEqual(Buf, W))
      return 0;
  return Default;
}

static int64_t bridgePluginGetArgInt64(const char *Key, int64_t Default) {
  const char *V = bridgePluginGetArg(Key);
  if (LLVM_UNLIKELY(!V))
    return Default;
  while (bridgeIsWhitespace(static_cast<unsigned char>(*V)))
    ++V;
  char *End = nullptr;
  errno = 0;
  long long Val = std::strtoll(V, &End, 10);
  if (LLVM_UNLIKELY(errno != 0 || End == V))
    return Default;
  while (bridgeIsWhitespace(static_cast<unsigned char>(*End)))
    ++End;
  if (LLVM_UNLIKELY(*End != '\0'))
    return Default;
  return static_cast<int64_t>(Val);
}

static uint64_t bridgePluginGetArgUInt64(const char *Key, uint64_t Default) {
  const char *V = bridgePluginGetArg(Key);
  if (LLVM_UNLIKELY(!V))
    return Default;
  const char *P = V;
  while (bridgeIsWhitespace(static_cast<unsigned char>(*P)))
    ++P;
  if (LLVM_UNLIKELY(*P == '-'))
    return Default;
  char *End = nullptr;
  errno = 0;
  unsigned long long Val = std::strtoull(P, &End, 10);
  if (LLVM_UNLIKELY(errno != 0 || End == P))
    return Default;
  while (bridgeIsWhitespace(static_cast<unsigned char>(*End)))
    ++End;
  if (LLVM_UNLIKELY(*End != '\0'))
    return Default;
  return static_cast<uint64_t>(Val);
}

// ===----------------------------------------------------------------------===
//  Arena-backed batch collection -- mirror the host-heap variants but
//  allocate the result array straight from the BumpPtrAllocator.  The
//  iteration logic is identical so the count + fill cost matches the
//  existing ModuleCollect* path.  Eliminates the per-call mi_malloc/
//  mi_free pair on the plugin's hot path.
// ===----------------------------------------------------------------------===

static NevercValueRef *
bridgeArenaCollectFunctions(NevercArenaRef Arena, NevercModuleRef M,
                            unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!Arena || !M || !OutCount))
    return nullptr;
  auto *Mod = unwrap(M);
  size_t Count = Mod->size();
  if (LLVM_UNLIKELY(Count == 0 || Count > UINT_MAX ||
                    Count > SIZE_MAX / sizeof(NevercValueRef)))
    return nullptr;
  auto *Buf = static_cast<NevercValueRef *>(unwrapArena(Arena)->Alloc.Allocate(
      Count * sizeof(NevercValueRef), alignof(NevercValueRef)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  unsigned Idx = 0;
  for (auto &F : *Mod)
    Buf[Idx++] = wrapV(&F);
  *OutCount = Idx;
  return Buf;
}

static NevercValueRef *
bridgeArenaCollectDefinedFunctions(NevercArenaRef Arena, NevercModuleRef M,
                                   unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!Arena || !M || !OutCount))
    return nullptr;
  auto *Mod = unwrap(M);
  // Arena allocation is freed in bulk, so over-allocating by the declaration
  // count is harmless.  Use Mod->size() (O(1) on modern LLVM) as the upper
  // bound and avoid a separate counting pass.
  size_t MaxCount = Mod->size();
  if (LLVM_UNLIKELY(MaxCount == 0 || MaxCount > UINT_MAX ||
                    MaxCount > SIZE_MAX / sizeof(NevercValueRef)))
    return nullptr;
  auto *Buf = static_cast<NevercValueRef *>(unwrapArena(Arena)->Alloc.Allocate(
      MaxCount * sizeof(NevercValueRef), alignof(NevercValueRef)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  unsigned Idx = 0;
  for (auto &F : *Mod)
    if (!F.isDeclaration())
      Buf[Idx++] = wrapV(&F);
  if (LLVM_UNLIKELY(Idx == 0))
    return nullptr;
  *OutCount = Idx;
  return Buf;
}

static NevercValueRef *
bridgeArenaCollectInstructions(NevercArenaRef Arena, NevercModuleRef M,
                               unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!Arena || !M || !OutCount))
    return nullptr;
  auto *Mod = unwrap(M);
  size_t Total = 0;
  for (auto &F : *Mod) {
    if (F.isDeclaration())
      continue;
    for (auto &BB : F)
      Total += BB.size();
  }
  if (LLVM_UNLIKELY(Total == 0 || Total > UINT_MAX ||
                    Total > SIZE_MAX / sizeof(NevercValueRef)))
    return nullptr;
  auto *Buf = static_cast<NevercValueRef *>(unwrapArena(Arena)->Alloc.Allocate(
      Total * sizeof(NevercValueRef), alignof(NevercValueRef)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  unsigned Idx = 0;
  for (auto &F : *Mod) {
    if (F.isDeclaration())
      continue;
    for (auto &BB : F)
      for (auto &I : BB)
        Buf[Idx++] = wrapV(&I);
  }
  *OutCount = Idx;
  return Buf;
}

static unsigned *bridgeArenaCollectAllOpcodes(NevercArenaRef Arena,
                                              NevercModuleRef M,
                                              unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!Arena || !M || !OutCount))
    return nullptr;
  auto *Mod = unwrap(M);
  size_t Total = 0;
  for (auto &F : *Mod) {
    if (F.isDeclaration())
      continue;
    for (auto &BB : F)
      Total += BB.size();
  }
  if (LLVM_UNLIKELY(Total == 0 || Total > UINT_MAX ||
                    Total > SIZE_MAX / sizeof(unsigned)))
    return nullptr;
  auto *Buf = static_cast<unsigned *>(unwrapArena(Arena)->Alloc.Allocate(
      Total * sizeof(unsigned), alignof(unsigned)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  size_t Idx = 0;
  for (auto &F : *Mod) {
    if (F.isDeclaration())
      continue;
    for (auto &BB : F)
      for (auto &Inst : BB)
        Buf[Idx++] = Inst.getOpcode();
  }
  *OutCount = static_cast<unsigned>(Idx);
  return Buf;
}

// ===----------------------------------------------------------------------===
//  Zero-allocation defined-function census
//  O(N) single scan, zero allocation.  Eliminates the
//  "ModuleCollectDefinedFunctions + Free" pattern when only the count
//  is needed.  (ModuleGetFunctionCount / ModuleGetGlobalCount are
//  implemented earlier in this file.)
// ===----------------------------------------------------------------------===

static unsigned bridgeModuleGetDefinedFunctionCount(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return 0;
  unsigned Count = 0;
  for (auto &F : *unwrap(M))
    if (!F.isDeclaration())
      ++Count;
  return Count;
}
// ===----------------------------------------------------------------------===
//  Arena-backed BB / MBB collection
//  Single vtable call replaces the GetCount + AllocArray + Fill pattern.
// ===----------------------------------------------------------------------===

static NevercBasicBlockRef *
bridgeArenaCollectBBs(NevercArenaRef Arena, NevercValueRef F,
                      unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!Arena || !F || !OutCount))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || Fn->isDeclaration()))
    return nullptr;
  size_t RawCount = Fn->size();
  if (LLVM_UNLIKELY(RawCount == 0 || RawCount > UINT_MAX ||
                    RawCount > SIZE_MAX / sizeof(NevercBasicBlockRef)))
    return nullptr;
  auto *Buf = static_cast<NevercBasicBlockRef *>(
      unwrapArena(Arena)->Alloc.Allocate(
          RawCount * sizeof(NevercBasicBlockRef),
          alignof(NevercBasicBlockRef)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  unsigned Idx = 0;
  for (auto &BB : *Fn)
    Buf[Idx++] = wrapBB(&BB);
  *OutCount = Idx;
  return Buf;
}
// ===----------------------------------------------------------------------===
//  Build the vtable
// ===----------------------------------------------------------------------===

NevercHostAPI buildHostAPI() {
  NevercHostAPI API = {};
  API.Version = NEVERC_PLUGIN_API_VERSION;
  API.StructSize = sizeof(NevercHostAPI);

  populateIRBridge(API);
  populateIRBuilderBridge(API);
  populateMIRBridge(API);
  populateStringBridge(API);
  populateDataStructuresBridge(API);
  populateAnalysisBridge(API);
  populateLinkerBridge(API);

  API.Alloc = bridgeAlloc;
  API.Realloc = bridgeRealloc;
  API.Free = bridgeFree;

  API.DiagNote = bridgeDiagNote;
  API.DiagWarning = bridgeDiagWarning;
  API.DiagError = bridgeDiagError;

  API.RegisterModulePass = bridgeNoOpRegisterModulePass;
  API.RegisterMachinePass = bridgeNoOpRegisterMachinePass;
  API.RegisterBinaryPass = bridgeNoOpRegisterBinaryPass;
  API.RegisterLinkerPass = bridgeNoOpRegisterLinkerPass;

  API.BinaryResize = bridgeBinaryResize;

  API.HostIsShellcodeMode = bridgeHostIsShellcodeMode;
  API.HostGetShellcodeEntrySymbol = bridgeHostGetShellcodeEntrySymbol;

  API.PluginGetArg = bridgePluginGetArg;
  API.PluginHasArg = bridgePluginHasArg;
  API.PluginGetArgCount = bridgePluginGetArgCount;
  API.PluginGetArgBool = bridgePluginGetArgBool;
  API.PluginGetArgInt64 = bridgePluginGetArgInt64;
  API.PluginGetArgUInt64 = bridgePluginGetArgUInt64;

  API.ModuleCollectFunctions = bridgeModuleCollectFunctions;
  API.ModuleCollectGlobals = bridgeModuleCollectGlobals;
  API.FunctionCollectBBs = bridgeFunctionCollectBBs;
  API.BBCollectInstructions = bridgeBBCollectInstructions;
  API.ModuleGetAliasCount = bridgeModuleGetAliasCount;
  API.ModuleCollectAliases = bridgeModuleCollectAliases;
  API.ModuleCollectAllFunctions = bridgeModuleCollectAllFunctions;
  API.ModuleCollectAllGlobals = bridgeModuleCollectAllGlobals;
  API.ModuleCollectAllInstructions = bridgeModuleCollectAllInstructions;
  API.ModuleCollectDefinedFunctions = bridgeModuleCollectDefinedFunctions;
  API.BBCollectOpcodes = bridgeBBCollectOpcodes;
  API.ModuleCollectAllOpcodes = bridgeModuleCollectAllOpcodes;
  API.FunctionGetInstructionCount = bridgeFunctionGetInstructionCount;
  API.ModuleGetDefinedFunctionCount = bridgeModuleGetDefinedFunctionCount;

  API.ArenaCollectFunctions = bridgeArenaCollectFunctions;
  API.ArenaCollectDefinedFunctions = bridgeArenaCollectDefinedFunctions;
  API.ArenaCollectInstructions = bridgeArenaCollectInstructions;
  API.ArenaCollectAllOpcodes = bridgeArenaCollectAllOpcodes;
  API.ArenaCollectBBs = bridgeArenaCollectBBs;

  API.ModuleGetFirstDefinedFunction = bridgeModuleGetFirstDefinedFunction;
  API.ModuleGetNextDefinedFunction = bridgeModuleGetNextDefinedFunction;

  API.MonotonicNanos = bridgeMonotonicNanos;
  API.ModuleIsLittleEndian = bridgeModuleIsLittleEndian;

  API.ModuleForEachFunction = bridgeModuleForEachFunction;
  API.ModuleForEachDefinedFunction = bridgeModuleForEachDefinedFunction;
  API.FunctionForEachBB = bridgeFunctionForEachBB;
  API.BBForEachInst = bridgeBBForEachInst;
  API.ModuleForEachGlobal = bridgeModuleForEachGlobal;
  API.ModuleForEachInstruction = bridgeModuleForEachInstruction;

  API.ModuleForEachAlias = bridgeModuleForEachAlias;
  API.ValueForEachUse = bridgeValueForEachUse;
  API.FunctionForEachInst = bridgeFunctionForEachInst;

  static_assert(
      offsetof(NevercHostAPI, FunctionForEachInst) +
              sizeof(NevercHostAPI::FunctionForEachInst) ==
          sizeof(NevercHostAPI),
      "New fields added after FunctionForEachInst. "
      "Wire them in buildHostAPI and update this static_assert.");

  return API;
}

} // namespace plugin
} // namespace neverc
