/*===-- CrtShimPlugin.c -- Zero-CRT-dependency plugin proof ----*- C -*-===*\
|*                                                                            *|
|* Demonstrates that an out-of-tree neverc plugin can be built with ZERO      *|
|* C-runtime dependency by routing every memory / I/O / string / formatting  *|
|* operation through the host-supplied NevercHostAPI vtable.                  *|
|*                                                                            *|
|* This is the central correctness argument behind the plugin API design:    *|
|*                                                                            *|
|*   On Windows a host built against UCRT cannot safely load a plugin built  *|
|*   against MSVCRT (or a different UCRT version), because the two CRTs      *|
|*   maintain independent malloc heaps, errno tables, and FILE* tables --    *|
|*   so passing a malloc'd pointer / FILE* / errno across the DLL boundary   *|
|*   silently corrupts memory or hangs.                                       *|
|*                                                                            *|
|*   By forbidding CRT use inside the plugin and forcing every host call to  *|
|*   go through the vtable, the plugin can be compiled with any CRT (or no   *|
|*   CRT at all) and remain compatible with hosts compiled against any       *|
|*   other CRT.                                                               *|
|*                                                                            *|
|* Build with maximum CRT lockdown to verify zero accidental references:     *|
|*                                                                            *|
|*   POSIX (Linux / macOS):                                                  *|
|*     cc -shared -fPIC -fno-stack-protector -fno-builtin -nodefaultlibs \   *|
|*        -O2 -std=c11 -Wl,-undefined,error                                  *|
|*        -o CrtShim.so CrtShimPlugin.c -I<neverc>/include                   *|
|*                                                                            *|
|*   Windows (clang-cl with MSVC link.exe):                                  *|
|*     clang-cl /LD /GS- /O2 /std:c11 /I<neverc>/include CrtShimPlugin.c \   *|
|*       /link /NODEFAULTLIB /NOENTRY                                        *|
|*                                                                            *|
|* The expected result is a shared object with NO undefined symbols other   *|
|* than the single required entry point `nevercGetPluginInfo`.                *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#include "neverc/Plugin/NevercPluginAPI.h"

/* The single rule for a zero-CRT plugin: do not include any non-freestanding
   header.  <stdint.h> / <stddef.h> / <stdarg.h> / <inttypes.h> are all
   "freestanding" per C11 4.6 -- they declare types and macros only and emit
   no object code.  Any of <stdio.h> / <stdlib.h> / <string.h> / <math.h> /
   <time.h> would pull in CRT object references and break the contract. */

#define PLUGIN_TAG "[crt-shim] "

/*===----------------------------------------------------------------------===*\
|* Pass 1: Module census via vtable -- no malloc, no fprintf, no strlen.     *|
\*===----------------------------------------------------------------------===*/
static int censusPass(NevercModuleRef M, const NevercHostAPI *API,
                      void *UserData) {
  (void)UserData;

  unsigned Defined =
      NEVERC_API_FN(API, ModuleGetDefinedFunctionCount)
          ? API->ModuleGetDefinedFunctionCount(M)
          : 0;
  unsigned Total = API->ModuleGetFunctionCount(M);
  unsigned Globals = API->ModuleGetGlobalCount(M);

  /* DiagNoteF goes through the host CRT, so the plugin's own CRT (or
     absence thereof) never participates in formatting or I/O. */
  const char *Endian =
      (NEVERC_API_FN(API, ModuleIsLittleEndian) &&
       !API->ModuleIsLittleEndian(M))
          ? "BE"
          : "LE";
  API->DiagNoteF(PLUGIN_TAG "%s [%s] -- functions: %u defined / %u total, "
                 "globals: %u",
                 API->ModuleGetTargetTriple(M), Endian, Defined, Total,
                 Globals);
  return 0;
}

/*===----------------------------------------------------------------------===*\
|* Pass 2: Symbol scan with arena-backed string ops -- proves we can do       *|
|*         non-trivial string work (case-folded prefix match) with zero       *|
|*         libc.                                                              *|
\*===----------------------------------------------------------------------===*/
static int symbolScanPass(NevercModuleRef M, const NevercHostAPI *API,
                          void *UserData) {
  (void)UserData;

  if (!NEVERC_API_FN(API, ArenaStrToLower) ||
      !NEVERC_API_FN(API, StrIEqual))
    return 0;
  NevercArenaRef A = NEVERC_TRY_ARENA(API);
  if (!A)
    return 0;

  unsigned Suspicious = 0;
  unsigned Total = 0;

  NEVERC_FOR_EACH_DEFINED_FUNCTION(API, M, F) {
    const char *Name = API->ValueGetName(F);
    if (!Name || !*Name)
      continue;
    ++Total;

    /* Case-fold once, reuse for both prefix tests.  This is a single
       arena allocation and zero individual frees. */
    char *Lower = API->ArenaStrToLower(A, Name);
    if (!Lower)
      continue;

    /* StrAfterPrefix returns the tail after the prefix or NULL.  Combined
       with the lowered name it gives a case-insensitive prefix match
       without ever calling tolower / strncasecmp from the CRT. */
    if (API->StrAfterPrefix(Lower, "debug_") ||
        API->StrAfterPrefix(Lower, "trace_") ||
        API->StrAfterPrefix(Lower, "__asan_"))
      ++Suspicious;
  }

  API->DiagNoteF(PLUGIN_TAG "Scanned %u functions, %u match "
                 "debug_/trace_/__asan_ prefix",
                 Total, Suspicious);

  API->ArenaDestroy(A);
  return 0;
}

/*===----------------------------------------------------------------------===*\
|* Registration                                                               *|
\*===----------------------------------------------------------------------===*/
static void registerPasses(const NevercHostAPI *API, void *Registrar) {
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT, censusPass, NULL,
                          "crt-shim-census");
  API->RegisterModulePass(Registrar, NEVERC_HOOK_PRE_OPT, symbolScanPass, NULL,
                          "crt-shim-symbol-scan");
}

NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void) {
  NevercPluginInfo Info;
  Info.APIVersion = NEVERC_PLUGIN_API_VERSION;
  Info.PluginName = "crt-shim-plugin";
  Info.PluginVersion = "1.0.0";
  Info.RegisterPasses = registerPasses;
  Info.Destroy = NULL;
  return Info;
}
