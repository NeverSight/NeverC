**Languages**: [English](dyncode.md) | [简体中文](dyncode.zh-CN.md) | [繁體中文](dyncode.zh-TW.md) | [日本語](dyncode.ja.md) | [한국어](dyncode.ko.md) | [Français](dyncode.fr.md) | [Deutsch](dyncode.de.md) | [Español](dyncode.es.md) | [Italiano](dyncode.it.md) | [Русский](dyncode.ru.md) | [العربية](dyncode.ar.md)

[← NeverC Plugin ABI](README.md)

# DynCode Plugins

`-fdyncode` compiles a translation unit into a flat, position-independent image
(`.bin`) whose code has zero relocations and no data section. It targets
arm64/x86_64 across macOS, Linux, Android, and Windows, at either user or kernel
execution level. Plugins observe, intercept, or replace the typed phases that
turn C into that image through the same pure C ABI used by the other domains:
no LLVM C++ objects, STL types, exceptions, or host pointers whose lifetime is
not stated by an API table.

## Interfaces

```c
#include "neverc/Plugin/PluginDynCode.h"
```

| Interface | Table | Slots | Purpose |
|---|---|--:|---|
| `NEVERC_INTERFACE_DYNCODE_{HIGH,LOW}` | `NevercDynCodeAPI` | 16 | Read the request, image, report, and the section/symbol/relocation/external maps |
| `NEVERC_INTERFACE_DYNCODE_REGISTRAR_{HIGH,LOW}` | `NevercDynCodeRegistrarAPI` | 5 | `RegisterTarget`, `RegisterImportProvider`, `RegisterExtractor`, `RegisterCharsetEncoder`, `RegisterBinaryVerifier` |
| `NEVERC_INTERFACE_DYNCODE_PHASE_{HIGH,LOW}` | `NevercDynCodePhaseAPI` | 4 | `GetPhaseInfo`, `GetRequest`, `GetImage`, `GetReport` |

All three are `NEVERC_INTERFACE_STABLE` at major 1. From inside a phase
callback, `NevercDynCodePhaseAPI` is the entry point — it turns the frame
into the handles the other table consumes:

```c
NevercDynCodeRequestHandle Request;
Phase->GetRequest(Phase->Context, Frame, Frame->Input, &Request);

NevercDynCodeRequestInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_DYNCODE_API_MAJOR,
                                     NEVERC_DYNCODE_API_MINOR, 0};
DynCode->GetRequestInfo(DynCode->Context, Task, Request, &Info);
```

The four map families — section maps, symbol maps, relocations, and external
references — are all walked with the same first/next/info triple, for example
`GetFirstRelocation`, `GetNextRelocation`, `GetRelocationInfo`. That is how a
plugin reads what extraction decided without parsing the report JSON.

## DynCode is a compilation product, not a `main()` post-step

`-fdyncode` is a normal Action/Job in the driver DAG. The compile job publishes
a verified in-memory `ObjectGraph`; a `-dyncode-extract` job consumes that graph
and writes the user's `-o` image. `-###`, phase printing, and the job graph all
show the extraction job, so a plugin never has to reconstruct a rewritten argv
to discover the mode. The frozen request is shared task-locally with the
in-process codegen; there is no `getCurrentDynCodeOptions()`, no process-global
mode flag, and no temp-object round-trip.

Exactly one translation unit is lowered to one image. Multiple inputs, `-c/-S/-E`,
and unsupported triples are rejected up front with stable diagnostics.

## Compatibility tiers

Phase IDs, artifact IDs, the request/report/image containers, and the callback
contracts are STABLE first-release ABI. Target-specific relocation kinds and the
object-format section/symbol schemas are LOCKSTEP: compare the target schema ID
and digest before consuming them. NeverC rejects a mismatched schema before
invoking a provider.

## The frozen request

At job start the driver normalizes the command line into an immutable
`DynCodeRequest` and freezes it. Child tasks borrow the snapshot; they never
mutate it. The request carries the target key and object format, the execution
level (user/kernel), the entry policy (explicit symbol, default candidate list,
entry-at-zero requirement), the PIC/section policy, the external-reference
policy, the bad-byte set/profile and rewrite flag, the charset provider ID, and
the max length, alignment, and pad byte.

## The typed phase graph

DynCode is a fixed graph of 34 phases. Thirty ordinary transitions are
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE`; four are `OBSERVABLE |
SEALED_HOST_GATE`. The sealed gates are the IR final verify, the MIR final
verify, the image verify, and the commit. A plugin can observe any phase, wrap a
replaceable transition with an interceptor, or replace its provider outright; it
can never replace, skip, or bypass a sealed gate, and it cannot express a
disabled transform as a skipped callback — a disabled transform runs an explicit
no-op provider whose equivalent output the host verifier still proves.

The phases, in order, are:

1. request freeze;
2. the IR transforms — prepare, indirect-branch lowering, memory-intrinsic
   lowering (pre and post-heap), string-runtime lowering, heap arena, three
   `compiler_rt` positions (pre/post/final), syscall/PEB/kernel import lowering,
   two `data_to_text` positions (pre/post), inline optimization, string
   finalize, stackify, all-`blr`, and the sealed IR final verify;
3. the MIR prepare transform and the sealed MIR final verify;
4. object import — bind the verified `ObjectGraph` to the task;
5. extraction — plan, layout, relocate, and build the candidate image;
6. the bounded binary phases — post-extract, bad-byte rewrite, charset encode,
   size/alignment/padding, and pre-verify;
7. the sealed image verify;
8. the sealed commit.

The normative source for the IDs, policies, stability tiers, and gates is
[`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`]; the executable coverage
contract is [`docs/plugin-api/coverage.json`].

## Built-in transforms are providers too

Every built-in IR/MIR pass is wrapped as a typed provider; the LLVM pass object
is never exposed across the C ABI. Replacing a phase means the built-in provider
does not run — the passing test proves the behavior or trace, not merely that a
registration succeeded. The `mem_intrin`, `compiler_rt`, and `data_to_text`
phases appear at more than one position; each position is a distinct phase ID
with its own proof, so a re-run is idempotent and never relies on hidden pass
state.

## ObjectGraph is the only ordinary-object input

Extraction consumes exactly one verified `ObjectGraph` produced by the target's
code-generation route. `dyncode.object.import` binds that graph and checks the
target key and provenance; it never re-reads bytes from disk or runs a second
object parse. A custom object format enters DynCode as soon as it can be read
into an `ObjectGraph` and has matching relocation and target providers. Multiple
objects and LTO graph-sets are rejected at freeze with a stable
`CAPABILITY_UNAVAILABLE`.

## External references and import lowering

The request's allowed-external set only means "a provider may handle this"; it
never permits an unresolved relocation to survive into the flat image. Every
external reference must end as one of: eliminated in IR/MIR, resolved to an
in-image symbol, converted to a declared and verifier-accepted runtime resolver
contract, or a hard error. Syscall stub, PEB import, and kernel import are the
three built-in `ImportProvider`s; each declares its target/level/symbol matcher
and the ABI contract it produces. A plugin may add an `ImportProvider`, but it
must return the replacement provenance, entry-ABI change, resolver parameters,
and residual references.

## Image, report, and bounded byte edits

Extraction produces a `DynCodeImage` and a `DynCodeReport`. The image is a
bounded byte builder plus the entry offset/symbol, the source-section and
source-symbol output maps, the relocation dispositions, and the external/runtime
contract records. Every byte edit goes through the builder's checked
read/write/insert/append/resize API; there is no `uint8_t **`. An edit updates
the image generation and invalidates any relocation/PIC/entry proof that
overlaps the changed range.

The report is an immutable, deterministic audit product: request/route/input/
output digests, the per-phase provider journal, selected/rejected sections and
why, the entry choice, patched/rejected/runtime-contract relocations, remaining
externals, size/alignment/padding, the bad-byte scan, and the verifier
checklist. `-fdyncode-report=<path>` writes its canonical JSON; the verbose
diagnostics render from the same report rather than a second set of counts.

The bad-byte rewrite chain runs in a frozen topological order and each step
returns a change record. The charset encoder is selected by exact stable ID and
returns a decoder stub, encoded payload, entry update, and target proof; an
unknown or ambiguous ID is a hard error. Disabling the rewrite selects an
explicit no-op step — the final audit still runs.

## Final verifier and post-finalize timing

All writable phases finish before the sealed final verifier. The verifier checks
that no unhandled external relocation/reference remains, that no forbidden
data/TLS/unwind/debug/metadata section is present, that the entry exists and is
correctly aligned and (when required) at offset zero, that every relocation site
lies in range with a matching PIC proof for the current image bytes, that the
section/symbol maps do not overlap, that the length/alignment/padding rules
hold, and that the final bytes — including decoder, header, and padding —
contain no forbidden byte. Any failure returns a structured diagnostic and
discards the whole output bundle.

There is no writable hook after the audit. If a byte transform touches an
executable range, the frozen route must supply a matching binary verifier
capability that the host calls to re-issue the PIC proof over the final,
immutable image.

## Driver options

`-fdyncode` enables the mode. `-fdyncode-entry=` picks the entry symbol.
`-fdyncode-bad-bytes=` / `-fdyncode-bad-byte-profile=` set the forbidden bytes,
`-fdyncode-bad-byte-rewrite` (default on) selects the rewrite chain, and
`-fdyncode-charset=` selects a registered encoder. `-fdyncode-max-length=`,
`-fdyncode-align=`, and `-fdyncode-pad=` bound the final size. `-fdyncode-keep-obj=`
tees the intermediate relocatable object and `-fdyncode-report=` writes the audit
report. `-mdyncode-context=user|kernel` selects the execution level.

## Concurrency and failure rules

- Keep mutable state in the host-supplied process/session/task scopes; never use
  a current-plugin or current-options singleton.
- Do not cache task handles or borrowed views after a callback returns.
- Invoke an interceptor continuation at most once, on the callback thread.
- Return the original `NevercStatus`; a declared `REPLACE` that fails does not
  silently fall back to the built-in provider.
- Declare the narrowest truthful concurrency and reentrancy modes.

See [`pluginsdk/examples/DynCodeTracePlugin.c`] for a read-only phase tracer and
[`pluginsdk/examples/DynCodeEncoderPlugin.c`] for a charset encoder.

<!-- reference links -->
[`docs/plugin-api/coverage.json`]: coverage.json
[`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`pluginsdk/examples/DynCodeEncoderPlugin.c`]: ../../pluginsdk/examples/DynCodeEncoderPlugin.c
[`pluginsdk/examples/DynCodeTracePlugin.c`]: ../../pluginsdk/examples/DynCodeTracePlugin.c
