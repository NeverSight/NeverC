/*
 * CustomCallConvPlugin.c -- assign arbitrary physical-register calling
 * conventions to functions, entirely from an out-of-tree plugin.
 *
 * This is the reference example for CallingConv::NeverC_Custom. NeverC itself
 * only ships the data-driven backend executor plus the FunctionSetCustomCallConv
 * API; *all* policy lives here in the plugin. That means new conventions never
 * require editing or rebuilding NeverC -- you just ship a new plugin.
 *
 * Build:
 *   make CustomCallConvPlugin.dylib            (or .so / .dll)
 *
 * Two modes:
 *
 *   1. Attribute mode (default): only functions marked with
 *      __attribute__((custom_attr("neverc-callconv","<spec>"))) are affected.
 *      Just load the plugin, no extra args needed.
 *
 *   2. Global mode (cc-all=1): apply a convention to every defined function
 *      (optionally filtered by ccprefix). Requires an explicit opt-in flag.
 *
 * Usage:
 *   # attribute mode -- only source-annotated functions get custom CC
 *   neverc -fplugin-pass=./CustomCallConvPlugin.dylib in.c -o in.o
 *
 *   # global mode -- every defined function: args in r10,r11,rsi,rdi ; ret rdx
 *   neverc -fplugin-pass=./CustomCallConvPlugin.dylib \
 *          -fplugin-pass-arg=cc-all=1 \
 *          -fplugin-pass-arg=ccspec="gpr:r10,r11,rsi,rdi;ret:rdx" in.c -o in.o
 *
 *   # global mode, only functions whose name starts with "secret_"
 *   neverc -fplugin-pass=./CustomCallConvPlugin.dylib \
 *          -fplugin-pass-arg=cc-all=1 \
 *          -fplugin-pass-arg=ccprefix=secret_ \
 *          -fplugin-pass-arg=ccspec="gpr:r9,r8;ret:rdx" in.c -o in.o
 *
 *   # global mode + diversify: each function gets a different layout
 *   neverc -fplugin-pass=./CustomCallConvPlugin.dylib \
 *          -fplugin-pass-arg=cc-all=1 \
 *          -fplugin-pass-arg=ccshuffle=1 in.c -o in.o
 *
 *   # positional layout -- force the 2nd argument onto the stack:
 *   neverc -fplugin-pass=./CustomCallConvPlugin.dylib \
 *          -fplugin-pass-arg=cc-all=1 \
 *          -fplugin-pass-arg=ccspec="args:rcx,stack,r8;ret:rax" in.c -o in.o
 *
 * Spec format (the "neverc-callconv" string):
 *   args:<t0,t1,...>   positional: argument i uses token i, a register name or
 *                      "stack"/"mem" (takes precedence over gpr/xmm; the only
 *                      way to force a specific argument onto the stack)
 *   gpr:<r0,r1,...>    pool: integer/pointer argument registers, in order
 *   xmm:<x0,x1,...>    pool: float/vector argument registers, in order
 *   ret:<r0,...>       integer/pointer return registers
 *   ret_xmm:<x0,...>   float/vector return registers
 * Any segment may be omitted; pool-mode values that don't fit spill to the
 * stack. Don't use callee-saved registers (rbx,rbp,r12-r15) as argument regs.
 */

#include "neverc/Plugin/NevercPluginAPI.h"

#define PLUGIN_TAG "[customcc] "

/* Preset layouts used by ccshuffle=1 to diversify functions. A real plugin
 * could generate these from a seeded RNG; the point is that the *decision*
 * lives entirely in the plugin. */
static const char *const kVariants[] = {
    "gpr:rcx,rdx,r8,r9;ret:rax",
    "gpr:rdi,rsi,rdx,rcx;ret:rax",
    "gpr:r9,r8,r10,r11;ret:rdx",
    "gpr:rsi,rdi,rcx,rdx;ret:rcx",
};
#define NUM_VARIANTS ((unsigned)(sizeof(kVariants) / sizeof(kVariants[0])))

struct ApplyCtx {
  const NevercHostAPI *API;
  const char *Spec;
  const char *Prefix;
  int Shuffle;
  int ArgDriven; /* cc-all=1: global mode, apply to all (or ccprefix-matched) */
  unsigned Index;
  unsigned Applied;
};

/* Decide and apply the convention for one function. Global mode (cc-all=1,
 * optionally filtered by ccprefix, using ccspec or ccshuffle) takes precedence;
 * any function not handled that way but carrying a source-level custom_attr
 * (lowered to a "neverc-callconv" string attribute) is handled from that. Each
 * function is touched at most once, so notes/warnings never duplicate. */
static int applyToFunction(NevercValueRef F, void *Ctx) {
  struct ApplyCtx *C = (struct ApplyCtx *)Ctx;
  const NevercHostAPI *API = C->API;
  const char *Name = API->ValueGetName(F);

  if (C->ArgDriven) {
    int Match = !C->Prefix || !*C->Prefix ||
                (NEVERC_API_FN(API, StrStartsWith) &&
                 API->StrStartsWith(Name, C->Prefix));
    if (Match) {
      const char *Spec =
          C->Shuffle ? kVariants[C->Index % NUM_VARIANTS] : C->Spec;
      API->FunctionSetCustomCallConv(F, Spec);
      API->DiagNoteF(PLUGIN_TAG "%s -> cc1000 [%s]", NEVERC_STR_OR(Name, "?"),
                     Spec);
      C->Index++;
      C->Applied++;
      return 0;
    }
  }

  /* Source-declared convention via __attribute__((custom_attr(...))) /
   * __declspec(custom_attr(...)), lowered by the frontend to a plain
   * "neverc-callconv" function string attribute. */
  if (NEVERC_API_FN(API, FunctionHasStringAttr) &&
      NEVERC_API_FN(API, FunctionGetStringAttr) &&
      API->FunctionHasStringAttr(F, "neverc-callconv")) {
    const char *Spec = API->FunctionGetStringAttr(F, "neverc-callconv");
    if (Spec && *Spec) {
      API->FunctionSetCustomCallConv(F, Spec);
      API->DiagNoteF(PLUGIN_TAG "from custom_attr: %s -> cc1000 [%s]",
                     NEVERC_STR_OR(Name, "?"), Spec);
      C->Applied++;
    }
  }
  return 0;
}

static int customCallConvPass(NevercModuleRef M, const NevercHostAPI *API,
                              void *UserData) {
  (void)UserData;

  /* Backwards compatible: cleanly no-op on hosts without the v2 entry. */
  if (!NEVERC_API_FN(API, FunctionSetCustomCallConv)) {
    API->DiagWarningF(PLUGIN_TAG "host too old: no FunctionSetCustomCallConv");
    return 0;
  }

  struct ApplyCtx Ctx;
  Ctx.API = API;
  Ctx.Spec = "gpr:r10,r11,rsi,rdi;ret:rdx";
  Ctx.Prefix = "";
  Ctx.Shuffle = 0;
  Ctx.ArgDriven = 0;
  Ctx.Index = 0;
  Ctx.Applied = 0;

  if (NEVERC_API_FN(API, PluginGetArg)) {
    const char *S = API->PluginGetArg("ccspec");
    if (S && *S)
      Ctx.Spec = S;
    const char *P = API->PluginGetArg("ccprefix");
    if (P)
      Ctx.Prefix = P;
  }
  if (NEVERC_API_FN(API, PluginGetArgBool)) {
    if (API->PluginGetArgBool("ccshuffle", 0))
      Ctx.Shuffle = 1;
    Ctx.ArgDriven = API->PluginGetArgBool("cc-all", 0);
  }

  if (NEVERC_API_FN(API, ModuleForEachDefinedFunction)) {
    API->ModuleForEachDefinedFunction(M, applyToFunction, &Ctx);
  } else {
    NEVERC_FOR_EACH_DEFINED_FUNCTION(API, M, F) {
      applyToFunction(F, &Ctx);
    }
  }

  if (Ctx.Applied)
    API->DiagNoteF(PLUGIN_TAG "applied %u custom calling convention(s)",
                   Ctx.Applied);
  return Ctx.Applied > 0;
}

static int ltoPostOptPass(NevercModuleRef M, const NevercHostAPI *API,
                          void *UserData) {
  (void)M;
  (void)UserData;
  API->DiagNoteF(PLUGIN_TAG "LTO_POST_OPT hook fired (after LTO pipeline)");
  return 0;
}

static void registerPasses(const NevercHostAPI *API, void *Registrar) {
  /* POST_OPT: after the mid-end optimizer, just before code generation, so the
   * convention isn't perturbed by later IR transforms. A single pass handles
   * both argument-driven (ccspec/ccprefix/ccshuffle) and source-driven
   * (custom_attr) conventions, so each function is touched at most once. */
  API->RegisterModulePass(Registrar, NEVERC_HOOK_POST_OPT, customCallConvPass,
                          NULL, "customcc-apply");

  API->RegisterModulePass(Registrar, NEVERC_HOOK_LTO_POST_OPT, ltoPostOptPass,
                          NULL, "customcc-lto-probe");
}

NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void) {
  NevercPluginInfo Info;
  Info.APIVersion = NEVERC_PLUGIN_API_VERSION;
  Info.PluginName = "custom-callconv-plugin";
  Info.PluginVersion = "1.0.0";
  Info.RegisterPasses = registerPasses;
  Info.Destroy = NULL;
  return Info;
}
