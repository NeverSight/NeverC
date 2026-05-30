/*===-- BenchPlugin.c -- HostAPI micro-benchmark plugin --------*- C -*-===*\
|*                                                                            *|
|* Quantifies the "极致快" claim:                                              *|
|*                                                                            *|
|*   1. Heap allocator throughput  (Alloc + Free pair, mimalloc-backed)      *|
|*   2. Arena allocator throughput (ArenaAlloc + single ArenaDestroy)        *|
|*   3. StrFormat throughput       (vtable -> snprintf -> bridgeStrDup)      *|
|*   4. ArenaStrFormat throughput  (vtable -> snprintf -> arena bump)        *|
|*   5. StrMap insert throughput   (StringMap-backed)                        *|
|*   6. DynArray push throughput   (geometric-growth vector)                 *|
|*   7. Vtable call overhead       (no-op MonotonicNanos round-trip)         *|
|*                                                                            *|
|* All timing is taken via the host-supplied MonotonicNanos vtable entry so  *|
|* the plugin still satisfies the zero-CRT contract demonstrated in          *|
|* CrtShimPlugin.c.                                                          *|
|*                                                                            *|
|* Build:                                                                    *|
|*   cc -shared -fPIC -O2 -std=c11 -I<neverc>/include                        *|
|*      -o BenchPlugin.so BenchPlugin.c                                      *|
|*                                                                            *|
|* Run:                                                                      *|
|*   neverc -fplugin-pass=./BenchPlugin.so -c <any-c-file> -o /dev/null      *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#include "neverc/Plugin/NevercPluginAPI.h"

#define BENCH_TAG "[bench] "

/* Tunable: how many operations per benchmark.  Kept modest so the
   plugin completes in a few hundred milliseconds on a single TU. */
enum {
  BENCH_ALLOC_N = 200000,
  BENCH_ARENA_N = 200000,
  BENCH_FORMAT_N = 50000,
  BENCH_STRMAP_N = 50000,
  BENCH_DYNARRAY_N = 200000,
  BENCH_VTABLE_N = 200000
};

/*===----------------------------------------------------------------------===*\
|* Helper: format "X ops in Y ns => Z ns/op, W M ops/s" via DiagNoteF.       *|
|* Avoids floating-point so we don't risk pulling libgcc soft-float symbols  *|
|* on platforms where the plugin is built with -nodefaultlibs.               *|
\*===----------------------------------------------------------------------===*/
static void reportTiming(const NevercHostAPI *API, const char *Label,
                         uint64_t Ops, uint64_t Nanos) {
  if (Ops == 0) {
    API->DiagNoteF(BENCH_TAG "%s: 0 ops", Label);
    return;
  }
  uint64_t NsPerOp = Nanos / Ops;
  uint64_t Us = Nanos / 1000;
  if (Us == 0)
    Us = 1;
  uint64_t MopsX10 = (Ops * 10ULL) / Us;
  uint64_t MopsInt = MopsX10 / 10;
  uint64_t MopsFrac = MopsX10 % 10;
  API->DiagNoteF(BENCH_TAG "%-22s %8" PRIu64 " ops in %10" PRIu64 " ns "
                 "=> %5" PRIu64 " ns/op, %4" PRIu64 ".%" PRIu64 " Mops/s",
                 Label, Ops, Nanos, NsPerOp, MopsInt, MopsFrac);
}

/*===----------------------------------------------------------------------===*\
|* Bench 1: Alloc / Free round-trip throughput                                *|
\*===----------------------------------------------------------------------===*/
static void benchAllocFree(const NevercHostAPI *API) {
  /* Pre-allocate a pointer table so the timed loop does one Alloc + one
     Free per iteration without amplifying any other cost. */
  void **Ptrs = (void **)API->Alloc(sizeof(void *) * BENCH_ALLOC_N);
  if (!Ptrs)
    return;

  uint64_t T0 = API->MonotonicNanos();
  for (unsigned I = 0; I < BENCH_ALLOC_N; ++I)
    Ptrs[I] = API->Alloc(64);
  uint64_t T1 = API->MonotonicNanos();
  for (unsigned I = 0; I < BENCH_ALLOC_N; ++I)
    API->Free(Ptrs[I]);
  uint64_t T2 = API->MonotonicNanos();

  reportTiming(API, "Alloc(64)", BENCH_ALLOC_N, T1 - T0);
  reportTiming(API, "Free", BENCH_ALLOC_N, T2 - T1);
  API->Free(Ptrs);
}

/*===----------------------------------------------------------------------===*\
|* Bench 2: Arena allocator throughput                                        *|
\*===----------------------------------------------------------------------===*/
static void benchArena(const NevercHostAPI *API) {
  if (!NEVERC_API_FN(API, ArenaCreate))
    return;
  NevercArenaRef A = API->ArenaCreate();
  if (!A)
    return;

  uint64_t T0 = API->MonotonicNanos();
  for (unsigned I = 0; I < BENCH_ARENA_N; ++I) {
    void *P = API->ArenaAlloc(A, 64);
    /* Touch the first byte so the optimizer can't elide the allocation
       entirely.  This costs a single store but reflects realistic use. */
    if (P)
      *(volatile char *)P = 0;
  }
  uint64_t T1 = API->MonotonicNanos();
  API->ArenaDestroy(A);
  uint64_t T2 = API->MonotonicNanos();

  reportTiming(API, "ArenaAlloc(64)", BENCH_ARENA_N, T1 - T0);
  /* ArenaDestroy is a single bulk free so report it as one op divided by
     the same N to give a per-allocation amortized cost. */
  reportTiming(API, "ArenaDestroy/amort", BENCH_ARENA_N, T2 - T1);
}

/*===----------------------------------------------------------------------===*\
|* Bench 3: Heap StrFormat throughput                                         *|
\*===----------------------------------------------------------------------===*/
static void benchStrFormat(const NevercHostAPI *API) {
  uint64_t T0 = API->MonotonicNanos();
  for (unsigned I = 0; I < BENCH_FORMAT_N; ++I) {
    char *S = API->StrFormat("iteration %u of %u with payload %x",
                             I, BENCH_FORMAT_N, I * 31u);
    if (S)
      API->Free(S);
  }
  uint64_t T1 = API->MonotonicNanos();
  reportTiming(API, "StrFormat+Free", BENCH_FORMAT_N, T1 - T0);
}

/*===----------------------------------------------------------------------===*\
|* Bench 4: Arena StrFormat throughput (no individual frees)                  *|
\*===----------------------------------------------------------------------===*/
static void benchArenaStrFormat(const NevercHostAPI *API) {
  if (!NEVERC_API_FN(API, ArenaCreate) ||
      !NEVERC_API_FN(API, ArenaStrFormat))
    return;
  NevercArenaRef A = API->ArenaCreate();
  if (!A)
    return;

  uint64_t T0 = API->MonotonicNanos();
  for (unsigned I = 0; I < BENCH_FORMAT_N; ++I) {
    char *S = API->ArenaStrFormat(A, "iteration %u of %u with payload %x",
                                  I, BENCH_FORMAT_N, I * 31u);
    (void)S;
  }
  uint64_t T1 = API->MonotonicNanos();
  API->ArenaDestroy(A);
  reportTiming(API, "ArenaStrFormat", BENCH_FORMAT_N, T1 - T0);
}

/*===----------------------------------------------------------------------===*\
|* Bench 5: StrMap insert throughput                                          *|
\*===----------------------------------------------------------------------===*/
static void benchStrMap(const NevercHostAPI *API) {
  if (!NEVERC_API_FN(API, StrMapCreate))
    return;
  NevercStrMapRef Map = API->StrMapCreate();
  if (!Map)
    return;

  /* Pre-build the keys so the timed loop measures only insert cost. */
  char **Keys = (char **)API->Alloc(sizeof(char *) * BENCH_STRMAP_N);
  if (!Keys) {
    API->StrMapDestroy(Map);
    return;
  }
  for (unsigned I = 0; I < BENCH_STRMAP_N; ++I)
    Keys[I] = API->StrFormat("key_%u", I);

  uint64_t T0 = API->MonotonicNanos();
  for (unsigned I = 0; I < BENCH_STRMAP_N; ++I)
    API->StrMapPut(Map, Keys[I], (uint64_t)I);
  uint64_t T1 = API->MonotonicNanos();

  for (unsigned I = 0; I < BENCH_STRMAP_N; ++I)
    API->Free(Keys[I]);
  API->Free(Keys);
  API->StrMapDestroy(Map);

  reportTiming(API, "StrMapPut", BENCH_STRMAP_N, T1 - T0);
}

/*===----------------------------------------------------------------------===*\
|* Bench 6: DynArray push throughput                                          *|
\*===----------------------------------------------------------------------===*/
static void benchDynArray(const NevercHostAPI *API) {
  if (!NEVERC_API_FN(API, DynArrayCreate))
    return;
  NevercDynArrayRef Arr = API->DynArrayCreate(sizeof(uint64_t));
  if (!Arr)
    return;

  uint64_t T0 = API->MonotonicNanos();
  for (unsigned I = 0; I < BENCH_DYNARRAY_N; ++I) {
    uint64_t V = I;
    API->DynArrayPush(Arr, &V);
  }
  uint64_t T1 = API->MonotonicNanos();
  API->DynArrayDestroy(Arr);

  reportTiming(API, "DynArrayPush(u64)", BENCH_DYNARRAY_N, T1 - T0);
}

/*===----------------------------------------------------------------------===*\
|* Bench 7: Vtable call overhead -- repeated MonotonicNanos calls.            *|
|*           This isolates the cost of an indirect-call through the host     *|
|*           dispatch table, including the MonotonicNanos work itself.        *|
\*===----------------------------------------------------------------------===*/
static void benchVtableOverhead(const NevercHostAPI *API) {
  uint64_t Sum = 0;
  uint64_t T0 = API->MonotonicNanos();
  for (unsigned I = 0; I < BENCH_VTABLE_N; ++I) {
    /* Mix the result into Sum so the optimizer can't hoist or DCE the
       loop.  We still measure call_overhead + clock_gettime cost which
       is itself dominated by a single VDSO syscall on Linux and a
       single mach_absolute_time on macOS. */
    Sum += API->MonotonicNanos();
  }
  uint64_t T1 = API->MonotonicNanos();
  API->DiagNoteF(BENCH_TAG "(vtable sentinel %" PRIu64 ")", Sum);
  reportTiming(API, "MonotonicNanos x2", BENCH_VTABLE_N, T1 - T0);
}

/*===----------------------------------------------------------------------===*\
|* Pass driver -- runs every benchmark exactly once.                          *|
\*===----------------------------------------------------------------------===*/
static int benchPass(NevercModuleRef M, const NevercHostAPI *API,
                     void *UserData) {
  (void)M;
  (void)UserData;

  if (!NEVERC_API_FN(API, MonotonicNanos)) {
    API->DiagNoteF(BENCH_TAG "Host does not expose MonotonicNanos -- "
                   "skipping benchmarks");
    return 0;
  }

  API->DiagNoteF(BENCH_TAG "==== HostAPI micro-benchmarks ====");
  benchAllocFree(API);
  benchArena(API);
  benchStrFormat(API);
  benchArenaStrFormat(API);
  benchStrMap(API);
  benchDynArray(API);
  benchVtableOverhead(API);
  API->DiagNoteF(BENCH_TAG "==== done ====");
  return 0;
}

static void registerPasses(const NevercHostAPI *API, void *Registrar) {
  /* PRE_OPT runs once per module before the optimization pipeline -- the
     ideal place for a one-shot benchmark that doesn't want to interleave
     with optimizer-reported timings. */
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT, benchPass, NULL,
                          "bench");
}

NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void) {
  NevercPluginInfo Info;
  Info.APIVersion = NEVERC_PLUGIN_API_VERSION;
  Info.PluginName = "bench-plugin";
  Info.PluginVersion = "1.0.0";
  Info.RegisterPasses = registerPasses;
  Info.Destroy = NULL;
  return Info;
}
