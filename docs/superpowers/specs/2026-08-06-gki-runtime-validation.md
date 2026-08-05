# GKI Runtime Release Validation Specification

## Goal

Add a GitHub Actions gate that proves the Android kernel runtime in the current
NeverC commit remains compatible with the six stock GKI kernels published in
`NeverSight/NeverC` release `gki-build-20260701`:

- Android 12 / Linux 5.10 (`510`)
- Android 13 / Linux 5.15 (`515`)
- Android 14 / Linux 6.1 (`601`)
- Android 15 / Linux 6.6 (`606`)
- Android 16 / Linux 6.12 (`612`)
- Android 17 / Linux 6.18 (`618`)

## Existing behavior

`.github/workflows/build-gki-kernels.yml` builds full kernel trees manually,
checks only the `struct module` `init`, `exit`, and size values, and publishes
large release archives. The runtime now also has checked GKI manifests covering
55 kernel-owned layouts, generated compatibility-table offsets, and SDK export
evidence. There is no workflow that consumes the release archives, re-derives
those manifests, compiles the current runtime, and loads a produced module into
the released kernels.

Initial release re-derivation found that the pre-existing 5.10–6.12 manifests
were produced from different kernel outputs. The published 5.10/5.15 configs
disable Clang CFI, several releases add `task_struct` members, and 6.12 shifts a
group of scheduler fields. The checked manifests must therefore be re-anchored
to this pinned release rather than weakening byte-level comparison.

## Requirements

1. Pin the release tag, repository identity, asset names, byte sizes, SHA-256
   digests, each packaged kernel's exact full vermagic, and the exact internal
   `.ko` member used for relocation evidence in a reviewable JSON lock file.
2. Download release assets rather than rebuilding Android kernel source trees.
3. Reject corrupt, substituted, malformed, or path-traversing archives before
   extracting or executing any checkout code against them.
4. For every profile, re-extract all 55 layouts from the release `vmlinux`,
   generate canonical JSON, and compare it byte-for-byte with the checked
   manifest. A structural diff may be printed only as a diagnostic. Re-anchor
   stale manifests to reviewed release-derived output and regenerate the runtime
   compatibility table; never normalize away a real layout/config difference.
5. Independently derive `struct module` `init`, `exit`, and size from a
   deterministically selected packaged `.ko` relocation section and compare
   them with `nvkmod_version.h`. Isolate that `.ko` so the existing verifier
   cannot silently prefer a packaged `.mod.o`.
6. When a release archive contains `.config` or `Module.symvers`, verify every
   occurrence against its recorded evidence digest and independently regenerate
   that part of the config/export evidence, even when only one is present.
   Report each absent input as inherited/not revalidated; the authoritative
   layout check must still run from `vmlinux`.
7. Reuse the `linux-x64-neverc-compiler` artifact produced by the existing
   `linux-x64-neverc-build` workflow for the exact source SHA. Never validate a
   current checkout using a compiler whose embedded kernel runtime came from a
   different commit.
8. Run the existing six-profile SDK-layout, compatibility-table, full runtime
   linkage, and demo/module ELF validation suites.
9. Build one dedicated, checked-in zero-import offset-smoke module per profile.
   Assert that `nm -u` is empty and its `.modinfo` carries the locked release
   vermagic, boot every released `Image` under QEMU with a minimal initramfs,
   and require successful `finit_module` and `delete_module` markers. This is
   the end-to-end proof that the loader consumed the configured offsets and
   executed both entry points; runtime bootstrap behavior remains covered by
   the compile/link suites rather than being conflated with this loader test.
10. Run automatically as a reusable workflow job called by the existing
    same-commit Linux compiler workflow, so the validation jobs and required
    check stay in that source commit's check suite. Also support a manual
    single-profile/all-profiles dispatch for diagnosis. Resolve the exact
    caller/manual compiler run, workflow path, SHA, and exactly one unexpired
    compiler artifact before checking out executable source.
11. Use read-only Actions/content permissions, reject untrusted `workflow_run`
    origins, retain concise verification and QEMU logs, and expose one aggregate
    required-check result.
12. Never use a detached `workflow_run` result as the required gate: GitHub may
    associate that workflow's checks with the default branch tip rather than
    the source SHA it validates. Non-push, tag, and untrusted PR cases are not
    automatic invocations; once invoked, missing/mismatched artifacts and all
    normal validation failures are hard failures.

## Non-goals

- Rebuilding GKI source trees in the validation workflow.
- Claiming compatibility with OEM kernels whose config, local version, or KMI
  differs from the pinned stock GKI artifacts.
- Exercising privileged runtime subsystems or symbol bootstrap in QEMU; the
  smoke module intentionally proves only module format and loader init/exit
  layout correctness without KMI imports.
- Publishing or replacing GKI release assets.
