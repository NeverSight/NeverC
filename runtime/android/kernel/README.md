# `runtime/android/kernel` — Android GKI kernel-module runtime

Minimal, self-contained runtime for building **standard `.ko` Android kernel
modules** with `neverc -fandroid-kernel-driver-mode`, from any host
(macOS / Linux / Windows), **without** a full kernel source tree or the kernel
build system.

This is deliberately separate from the user-space Android runtime
(`runtime/android/arm64`, the bionic NDK sysroot). Nothing here is used for
app-layer builds.

## Why this can be "minimal + cross-version"

A `.ko` is a relocatable ELF (`ET_REL`) plus kernel-module metadata. Two pieces
are bound to a specific kernel build and cannot be unified in one insmod-loadable
binary across major versions:

1. **`vermagic`** — the `.modinfo` string the loader compares to the running
   kernel.
2. **`struct module` layout** — the byte offsets of `name` / `init` / `exit`
   inside `.gnu.linkonce.this_module`, read using the *kernel's* own layout.

Everything else (the kernel API the driver calls) is resolved **dynamically at
runtime** via `kallsyms_lookup_name()` (bootstrapped with a kprobe). So the
driver source stays version independent and the `.ko` imports almost no symbols
(typically only `register_kprobe` / `unregister_kprobe`, which are stable GKI
exports). NeverC's final module link emits an allocated, possibly empty
`__versions` section. On MODVERSIONS kernels this avoids the force-load path
used when the section is absent, while missing per-symbol entries remain the
kernel's accepted unversioned-import case. It also lets the loader compare the
vermagic feature flags independently of an OEM patch/local-version token.

Per-kernel data is one generated, atomic **profile contract** rather than a set
of independently overridable macros:

- `arm64/gki-profiles.json` names each profile and owns semantic identity plus
  ABI capabilities;
- `arm64/gki-manifests/<id>.json` is the checked layout/config evidence;
- `arm64/gki-layout-certificates.json` optionally overlays a measured runtime
  layout when the live release token matches byte-for-byte; a missing or
  non-matching certificate leaves the complete compile-family table active;
- `arm64/gki-release.json` pins exact release, vermagic, and KCFI evidence;
- `tools/generate-compat-table.py` validates those three sources together and
  generates the public profile configuration and private runtime tables.

Select a current profile with `-DNVK_KERNEL=<id>` (for example `612`). The
numeric values are compatibility handles, not ordered kernel versions; new
code should use the generated `NEVERC_KRT_PROFILE_*` names when it needs to
refer to a profile symbolically. The source-compatibility default remains the
android12 5.10 profile.

## Layout

```
runtime/android/kernel/
  include/                     # public NeverC kernel SDK headers
    nvkmod.h                   #   module entry point and NEVERC_KRT_BOOTSTRAP()
    nvkmod_version.h           #   stable profile-selection facade
    nvk_profile_ids.h          #   generated symbolic profile handles
    nvk_profile_config.h       #   generated exact compile-time contract
    nvk_profile_marker.h       #   re-entrant source-to-compiler contract marker
    nvk_profile_contract_asm.h #   native contract for profile-aware .S inputs
    nvk.h                      #   all-in-one include and subsystem initialization
    nvk_interpose.h                 #   arm64 inline-interpose engine v2 (simple + context + batch + chain + ftrace + kCFI)
    nvk_mem.h                  #   safe memory read/write, pattern scan (BMH), write-protected memory update
    nvk_syscall.h              #   sys_call_table operations + arm64 syscall number table
    nvk_process.h              #   process enumeration, PID lookup, task walking (6.12-safe ranges)
    nvk_cred.h                 #   credential wrappers (uid/gid/capabilities; struct cred)
    nvk_selinux.h              #   SELinux enforcement-state control + AVC/inode/capable interpose helpers
    nvk_vis.h                  #   module visibility management (list + sysfs + proc + dmesg + PID + mount + maps)
    nvk_log.h                  #   leveled logging (silent/error/warn/info/debug/trace) + ratelimit
    nvk_thread.h               #   kernel thread management (kthread create/stop/sleep)
    nvk_netlink.h              #   user↔kernel netlink IPC channel
    nvk_file.h                 #   kernel file I/O (filp_open/kernel_read/write)
    nvk_addr.h                 #   virtual↔physical address, page table walking, KASLR offset
    nvk_compat.h               #   runtime kernel version detection + feature probing + vermagic patching
    nvk_anti.h                 #   environment detection + integrity watchdog + HW CRC32
    nvk_vma.h                  #   VMA operations, process memory map inspection
    nvk_cred_sess.h            #   credential session helpers for SDK demos
    nvk_ksyms.h                #   extended symbol table operations (walk, prefix search, info)
    nvk_seccomp.h              #   seccomp filter inspection and mode control
    nvk_pmu.h                  #   ARM64 PMU counter access
    nvk_xmem.h                 #   cross-process memory operations (mmap + ELF loader + I-cache coherent)
    nvk_ns.h                   #   PID namespace operations
    nvk_binder.h               #   Binder transaction filter helpers (lazy interpose)
    nvk_crypto.h               #   SHA-256, HMAC-SHA256, ChaCha20, integrity verification
    nvk_timer.h                #   hrtimer, timestamps (ktime/arch counter), busy-wait
    nvk_power.h                #   PM notifier (suspend/resume) + reboot notifier
    nvk_cpu.h                  #   CPU topology, online enumeration, per-CPU data, SMP calls
    nvk_inode.h                #   opaque filename/path/inode access and path→inode refs
  arm64/
    gki-profiles.json          # semantic profile identity + named capabilities
    gki-manifests/*.json       # authoritative layout/config evidence
    gki-layout-certificates.json # compatible-identity private-field evidence
    gki-release.json           # pinned release/vermagic/KCFI evidence
    include/                   # minimal cross-version kernel compatibility headers
      linux/*.h                #   types, kernel, printk, list, slab, fs, ...
      asm/*.h                  #   barrier, current, page, ptrace, syscall, ...
    lib/                       # optional libclang_rt.builtins-aarch64.a
  src/
    nvk_internal.h             # private interfaces shared only by runtime C files
    *.c                        # runtime implementations and file-local helpers
  tools/
    gen_struct_module_offsets.c # regenerate exact struct module offsets per kernel
    gen_fops_offsets.c           # file_operations offsetof/size probe
    gen_layout_offsets.c       # proc_ops / sk_buff / nf_hook_ops layout probe
    extract-btf-layouts.py     # extract authoritative layouts from GKI BTF
    verify-gki-release.py      # verify a pinned release archive + complete manifest
    verify-android-module.py   # verify final .ko loader sections and symbols
    run-gki-qemu-smoke.sh      # boot Image and require module load + unload
    check-source-boundaries.py # enforce public/private source boundaries
    test-sdk-layouts.sh        # compile layout-sensitive headers for every GKI
    test-inode-metadata.py     # host behavior/evidence test for opaque filename/path/inode access
    test-runtime-linkage.sh    # focused multi-TU auto/full/no-LTO checks
    test-all.sh                # full demo × kernel-profile × linkage-mode matrix
```

`neverc -fandroid-kernel-driver-mode` automatically:

- swaps the bionic sysroot for the kernel include roots above
  (`include/` + `arm64/include/`),
- enables auto-LTO by default for multi-file modules; explicit `-flto=full` and
  `-fno-lto` builds are both supported and covered by the runtime matrix,
- carries the selected profile and KCFI mode as one opaque compiler contract:
  LLVM module flags enforce equality before every LTO merge, while compiler-
  produced native objects carry the same pair in
  `.neverc.android.kernel.profile`. Native and plugin-mediated links reject
  missing, malformed, or mixed contracts on those inputs, then the final
  Android `.ko` merge drops the section so delivered modules keep no NeverC
  tooling fingerprint,
- adds `-D__KERNEL__ -DMODULE -ffreestanding`, direct external-data access
  (the arm64 module loader has no GOT), reserved `x18`, and disables outline
  atomics and CFI checks,
- lowers any wider-than-64-bit integer div/rem inline (LLVM `ExpandLargeDivRem`,
  via `-expand-div-rem-bits=64`) so `__int128` division never needs the
  compiler-rt helpers the kernel doesn't export — the `.ko` stays fully
  self-contained with **zero** compiler-rt dependency,
- emits the empty `.plt` / `.init.plt` / `.text.ftrace_trampoline` sections the
  arm64 loader requires (`CONFIG_ARM64_MODULE_PLTS`) — no external `module.lds`.
- performs the module-script portion of the final `ET_REL` link once: it emits
  allocated `__versions`, collects compiler `alloc_tags` inputs into aligned
  `.codetag.alloc_tags`, and defines `__start_alloc_tags` / `__stop_alloc_tags`
  around the real range. This supports `CONFIG_MEM_ALLOC_PROFILING` without
  creating duplicate placeholders in every translation unit. An independent
  post-merge reader checks this loader contract before the linker commits the
  output.

Public headers contain declarations, macros, compatibility types, and only
small forced-inline helpers. Every header-defined function uses
`static __always_inline`; implementation-private cross-file declarations live
in `src/nvk_internal.h`, while file-local helpers remain `static` in their C
file.

You then pass `-r -nostdlib -o mod.ko mod.c` to relocatably link the module.
Preprocessed assembly (`.S`) receives the generated native contract
automatically. Raw `.s` and third-party/prebuilt objects cannot infer a source
profile and are rejected unless their producer emits the same documented
`.neverc.android.kernel.profile` record; this is an intentional fail-closed
boundary, not a one-profile-per-command assumption. A finished `.ko` no longer
carries that record and therefore cannot be fed back into another
contract-checked Android module link — re-link from compiler-produced objects
instead. The lowercase `.ko` output suffix is the explicit finalization
boundary: keep partial links named `.o`, and do not link a deliverable under a
temporary suffix and merely rename it afterward.

## GKI build producer vs runtime validation gate

The two GKI workflows have deliberately different jobs:

- `.github/workflows/build-gki-kernels.yml` is the expensive, manual producer.
  It syncs and builds complete Android kernel trees, verifies their direct
  `struct module` relocation evidence, and publishes release archives.
- `.github/workflows/validate-gki-runtime.yml` is the lightweight consumer and
  compatibility gate. It downloads every archive pinned in
  `arm64/gki-release.json` (eight families on `gki-build-20260701`: the
  six official GKI series plus android13-5.10 / android14-5.15). QEMU and
  smoke therefore cover `51013` and `51514` as well as the original six.
- `.github/workflows/watch-gki-updates.yml` polls AOSP `kernel/common` daily
  for a newer linux sublevel, a KMI generation bump, a `-kminext` preview, a
  NeverC-read struct-field shift, or a new `androidN-M.m` branch, and posts
  Discord via `GKI_WATCH_DISCORD_WEBHOOK_URL`.

The consumer checks each asset's name, byte size, and SHA-256 before safe
extraction; regenerates all 55 checked BTF/DWARF layouts from `vmlinux`; checks
every packaged config and `Module.symvers` occurrence; and independently derives
`init`, `exit`, `sizeof(struct module)`, and the module-entry KCFI type IDs from
the lock-pinned packaged `.ko`. It also runs the complete current runtime
SDK/layout/linkage/demo suites.

The 5.10 image carries DWARF rather than BTF. Its extractor releases each
pyelftools compilation-unit cache after producing plain layout data, and hashes
large ELF evidence sections in chunks; this preserves byte-for-byte evidence
while keeping the validation job within hosted-runner memory limits.

For the loader proof, the workflow compiles a dedicated zero-import module from
the exact same source SHA as the reused `linux-x64-neverc-compiler` artifact.
Before boot, `tools/verify-android-module.py` independently requires AArch64
`ET_REL`, a valid allocated `__versions`, and defined alloc-tag boundaries in
the correct allocated/writable section; the zero-import fixture must have an
empty range. A section-name-only artifact with undefined boundary symbols is
rejected.
For KCFI kernels, NeverC emits the independently release-derived
`init_module` and `cleanup_module` type-id words as function prefix data during
code generation. The build helper verifies those bytes without modifying the
`.ko`; non-KCFI profiles are required to retain zero prefixes.
It boots every released `dist/Image` under QEMU, calls `finit_module`, then
`delete_module`, and requires separate load/unload success markers. This smoke
test proves module format, loader entry-point offsets, and the pinned entry-call
ABI; runtime symbol bootstrap and API behavior remain covered by the
compile/link suites.

Automatic validation is a reusable job in the same Linux build run/check suite,
so a result cannot be attached to a different default-branch SHA. For diagnosis,
dispatch `validate-gki-runtime` with an exact matching compiler run ID and one
profile (or `all`). These archives describe pinned stock GKI builds only. The
gate certifies the pinned stock token and loader contract. Runtime
`NEVERC_KRT_VER_EXACT` is deliberately a numeric match class, not a claim that
the OEM token was measured: it means patch + Android generation + KMI + page
match, with token suffixes ignored. An explicitly selected profile may also
activate on an observed OEM kernel with the same Linux `major.minor`, Android
generation, and page size as `NEVERC_KRT_VER_COMPAT`. A missing/unparseable
banner, a different series, or a different page size remains fail-closed.
Linux patch, KMI, and release token do not reject the compatible path. Both
match classes start with the complete family layout; a byte-exact certificate
may overlay measured offsets but is not required to activate or use them. A
different Android generation that also changes
loader-visible `struct module` / vermagic is its own compile-time family
(`51013`, `51514`) and fail-closes on the older compile handle.

## SDK headers

| Header | Purpose |
|--------|---------|
| `nvkmod.h` | Module entry point, kprobe bootstrap, `NVK_BOOTSTRAP()`, `NVK_DEFINE_MODULE()` |
| `nvk_interpose.h` | arm64 inline-interpose engine v2 — simple/context/batch modes + absolute relocation (10 insn types) + BTI/PAC/kCFI-safe + SMP-safe DMB barriers + atomic stop_machine patch + D-cache→I-cache coherent + deep quiescence uninterpose + poison-on-free pool (32 pages) + ftrace fallback + interpose chain + pause/resume + 6.12+ execmem support |
| `nvk_mem.h` | `nvk_mem_read/write`, `nvk_mem_read_user`, `nvk_mem_scan`, `nvk_mem_scan_mask`, `nvk_mem_write_protected` — MTE-tag-aware, dynamic page size (4K/16K/64K) |
| `nvk_syscall.h` | `nvk_syscall_replace/restore`, `nvk_syscall_get`, arm64 syscall number definitions |
| `nvk_process.h` | `nvk_current_pid`, `nvk_find_task_by_name`, `nvk_for_each_task`, task comm/pid resolution |
| `nvk_cred.h` | `nvk_cred_set_uid0`, `nvk_cred_set_uid`, `nvk_cred_set_caps_full`, `nvk_cred_get_ids` — credential wrappers around `struct cred` |
| `nvk_selinux.h` | `nvk_selinux_set_permissive/enforcing`, `nvk_selinux_policy_install/remove` (AVC + inode interpose helpers for policy-control demos) |
| `nvk_vis.h` | `nvk_vis_filter/restore`, `nvk_vis_filter_full` — module visibility filters (list + sysfs + /proc/modules + /proc/vmallocinfo + dmesg + PID + mount + maps) |
| `nvk_log.h` | `nvk_log_err/warn/info/dbg/trace`, `nvk_log_once`, `nvk_log_ratelimit`, `nvk_log_hexdump` |
| `nvk_thread.h` | `nvk_thread_run`, `nvk_thread_stop`, `nvk_thread_sleep_ms`, `nvk_thread_stop_all` |
| `nvk_netlink.h` | `nvk_nl_open/close/send/reply` — bidirectional netlink IPC with dispatch callback |
| `nvk_addr.h` | `nvk_virt_to_phys`, `nvk_translate_user`, `nvk_walk_pgtable`, VA bits / page size detection |
| `nvk_compat.h` | `nvk_kernel_version()`, `NVK_KERNEL_GE(maj,min)`, `nvk_has_pac/bti/mte`, versioned symbol lookup helpers |
| `nvk_file.h` | `nvk_file_open/read/write/close`, `nvk_file_exists`, `nvk_file_read_all/write_all` |
| `nvk_anti.h` | Host environment probes (emulator, debugger, credential state, admin CLI / mgr path markers, SELinux policy state, interpose/kprobe integrity), integrity verification, sealed watchdog, ARM64 HW CRC32 — probe path strings use xorstr |
| `nvk_vma.h` | VMA operations (find_vma, walk, read/write remote), process memory map inspection |
| `nvk_cred_sess.h` | Credential session helpers (allow/query/apply) for SDK demos |
| `nvk_ksyms.h` | Extended symbol operations (`nvk_ksyms_walk`, `nvk_ksyms_for_each`, prefix search, function size) |
| `nvk_seccomp.h` | Seccomp filter inspection and mode control (per-process mode read/clear/set) |
| `nvk_pmu.h` | ARM64 PMU counter access (cycle/instruction/cache/branch counters) |
| `nvk_xmem.h` | Cross-process memory operations — `nvk_xmem_mmap/munmap`, `nvk_xmem_deploy_dyncode` (cross-process I-cache coherent via DC CIVAC + IC IALLU), `nvk_xmem_load_elf` (ELF PT_LOAD segment loader), thread-context transfer helpers |
| `nvk_ns.h` | PID namespace operations (cross-namespace PID translation, nsproxy) |
| `nvk_binder.h` | Binder transaction filter helpers (lazy interpose — only installed on first filter add) |
| `nvk_crypto.h` | `nvk_sha256`, `nvk_hmac_sha256`, `nvk_chacha20_encrypt`, `nvk_crypto_verify_region` — constant-time, pure C, zero kernel dependencies |
| `nvk_timer.h` | `nvk_timer_start_ms/us/ns`, `nvk_ktime_get_ns`, `nvk_arch_counter`, `nvk_udelay` — hrtimer wrapper + ARM64 generic timer |
| `nvk_power.h` | `nvk_pm_register/unregister`, `nvk_reboot_register/unregister` — suspend/resume/shutdown awareness |
| `nvk_cpu.h` | `nvk_cpu_id/cluster/midr`, `nvk_for_each_online_cpu`, `NVK_DEFINE_PER_CPU`, `nvk_smp_on_each`, CPU feature detection (CRC32/SHA/AES/LSE/SVE) |

All symbol lookups go through `NEVERC_KRT_LOOKUP()` which auto-encrypts
string literals via xorstr.

## Examples

| Example | Description |
|---------|-------------|
| `android-kernel-hello` | Zero-import minimal module (load test) |
| `android-kernel-driver` | Dynamic kallsyms template |
| `android-kernel-chardev` | misc device + ioctl + /proc status page |
| `android-kernel-inline-interpose` | High-level function interpose on `do_faccessat` |
| `android-kernel-syscall-interpose` | sys_call_table replacement + inline interpose (dual mode) |
| `android-kernel-lowvis` | Module visibility management (list / sysfs / proc + SELinux policy control + credential wrappers) |
| `android-kernel-netlink` | User↔kernel netlink IPC channel (ping/version/echo) |
| `android-kernel-full` | Full SDK demo — initializes all subsystems, exercises interpose/cred/vis/netlink |

## Profile evidence and `struct module` offsets

The checked manifest for each profile holds the `name`/`init`/`exit` offsets and
total size of `struct module`. Generated headers consume those facts; no
handwritten runtime or compiler source branch enumerates catalog IDs.
Official pinned families are **verified** against stock GKI `gki_defconfig`
evidence computed from each kernel's own prepared headers via
`tools/gen_struct_module_offsets.c`. Local Android-generation families
(`51013`, `51514`) are verified against the measured local `vmlinux`:

| preset | release | NAME | INIT | EXIT | sizeof |
|--------|---------|------|------|------|--------|
| `510` (android12-5.10) | 5.10.257| 24 | 400 (0x190) | 960 (0x3C0)  | 1024 (0x400) |
| `51013` (android13-5.10) | 5.10.223| 24 | 400 (0x190) | 936 (0x3A8)  | 1024 (0x400) |
| `515` (android13-5.15) | 5.15.208| 24 | 376 (0x178) | 888 (0x378)  | 960 (0x3C0)  |
| `51514` (android14-5.15) | 5.15.164| 24 | 376 (0x178) | 976 (0x3D0)  | 1024 (0x400) |
| `601` (android14-6.1)  | 6.1.174 | 24 | 368 (0x170) | 984 (0x3D8)  | 1088 (0x440) |
| `606` (android15-6.6)  | 6.6.139 | 24 | 392 (0x188) | 1464 (0x5B8) | 1536 (0x600) |
| `612` (android16-6.12) | 6.12.89 | 24 | 392 (0x188) | 1528 (0x5F8) | 1600 (0x640) |
| `618` (android17-6.18) | 6.18.24 | 24 | 376 (0x178) | 1536 (0x600) | 1664 (0x680) |

`name` (offset 24) is stable across current GKI builds; `init`/`exit`/sizeof
depend on the target kernel's `CONFIG_*` (CFI_CLANG, MODULE_UNLOAD, TRACEPOINTS,
DEBUG_INFO_BTF_MODULES, ...). The values above match a stock GKI kernel built
with `gki_defconfig` (BTF module debug info shifts `exit` and may grow
`sizeof(struct module)`). A different config or layout is a different certified
atomic profile. A same-series/same-page OEM patch or local-version may use the
explicitly selected layout in `COMPAT` mode, but that is an operator-selected
compatibility path rather than evidence-backed exact certification. To certify
it, collect evidence first:

```
# On a prepared tree (Linux: make ARCH=arm64 LLVM=1 gki_defconfig modules_prepare):
runtime/android/kernel/tools/gen-offsets.sh <path-to-GKI>/common
# prints measured layout facts for the new manifest
```

Then add a catalog record, matching manifest and release-lock record, run
`tools/generate-compat-table.py`, and execute the profile/runtime/smoke tests.
The generator rejects incomplete or inconsistent profile sets. Per-TU
`NEVERC_KRT_OFF_*`, `NEVERC_KRT_MODULE_SIZE`, `NEVERC_KRT_KCFI_MODE`, and
`NEVERC_KRT_VERMAGIC` overrides are intentionally rejected: mixing any one of
those facts with a different profile would recreate the ABI ambiguity this
contract prevents.

`tools/gen-offsets.sh` drives `tools/gen_struct_module_offsets.c` (a no-run
"asm-offsets" probe). It works fully on Linux / an already-prepared tree; on
macOS, where the kernel build system can't run, prepare the GKI trees in
a throwaway Linux container instead:

```
# from a host with Docker; computes offsets for the raw GKI trees at once
docker run --rm -v <repo>/local_docs:/work -v <repo>/runtime/android/kernel/tools:/tools:ro \
  ubuntu:24.04 bash -lc '
    apt-get update -qq && apt-get install -y -qq build-essential clang lld llvm \
      bc bison flex libssl-dev libelf-dev libdw-dev cpio kmod rsync >/dev/null
    for kit in GKI-android12-5.10-kit GKI-android13-5.15-kit GKI-android14-6.1-kit GKI-android15-6.6-kit GKI-android16-6.12-kit GKI-android17-6.18-kit; do
      KT=/work/$kit/common; O=/build/$kit
      make -C $KT O=$O ARCH=arm64 LLVM=1 -j"$(nproc)" gki_defconfig modules_prepare
      clang --target=aarch64-linux-gnu -fno-lto -nostdlibinc -std=gnu11 -D__KERNEL__ -DNVK_GEN_KSRC=1 \
        -I$KT/arch/arm64/include -I$O/arch/arm64/include/generated -I$KT/include -I$O/include \
        -I$O/include/generated -I$KT/arch/arm64/include/uapi -I$O/arch/arm64/include/generated/uapi \
        -I$KT/include/uapi -I$O/include/generated/uapi \
        -include $KT/include/linux/kconfig.h -include $O/include/generated/autoconf.h \
        -S -o - /tools/gen_struct_module_offsets.c | grep "==NVK=="
    done'
# NOTE: android16-6.12 needs libdw-dev (its gendwarfksyms host tool includes <dwarf.h>).
```

At runtime, bootstrap first tries the full release token, Android/KMI identity,
and page size. Without an explicitly selected profile, activation remains
token-exact. With the build-selected profile, the same Linux patch + Android
generation + KMI + page reports `EXACT`; the catalog token is measurement
evidence and is ignored. `COMPAT` is the same Linux `major.minor`, the same
Android generation, and the same page size; patch, KMI, and token are
ignored. A certificate overlays measured offsets only on a byte-for-byte
release token; it is not required. The observed OEM patch/Android/KMI
fields remain visible and are never overwritten with pinned values. There
is no cross-series, unknown-banner, nearest-version, or maximum-layout
fallback.

## Same Linux series, different Android generation

The Linux patch number (the `223` in `5.10.223`) does not select another
NeverC family table when Android generation and page size stay the same.
That table is the permissive runtime default; if a measured patch does move a
runtime-read field, a byte-exact certificate overlays the changed layout.
A new Android generation on the same `major.minor` is instead a distinct
compile family because it can also change loader-visible ABI.
Official AOSP `include/linux/module.h` keeps the same field order inside a
Linux series (`android12-5.10` matches `android13-5.10`; `android13-5.15`
matches `android14-5.15`, including the optional `build_id[]`). The loader-
visible size/`exit` deltas are that generation's GKI `CONFIG_*` / KMI, which
is why the compile IDs follow the measured `vmlinux`, not a source rewrite.
The loader also reads vermagic and `.gnu.linkonce.this_module` before our
init runs, so one `.ko` cannot carry two `struct module` images. That is why
android12-5.10 and android13-5.10 are different compile-time families, and
why android13-5.15 and android14-5.15 are too:

| Compile handle | Alias | Live banner family | Loader-visible `struct module` | Notes |
|----------------|-------|--------------------|--------------------------------|-------|
| `510` | `51012` | `5.10.*-android12-9` (pinned official) | size 1024, `init` 400, `exit` 960 | Official CI/release key |
| `51013` | — | `5.10.*-android13-4` | size 1024, `init` 400, `exit` 936 | Pinned CI/release key (`gki-android13-5.10-build.tar.gz`) |
| `515` | `51513` | `5.15.*-android13-8` (pinned official) | size 960, `init` 376, `exit` 888 | Official CI/release key |
| `51514` | — | `5.15.*-android14-11` | size 1024, `init` 376, `exit` 976 | Pinned CI/release key (`gki-android14-5.15-build.tar.gz`); `task_struct` 4608→4736, `comm` 1960→2064 |

`-DNVK_KERNEL=51012` is only a spelling for `510`. `-DNVK_KERNEL=51513` is
only a spelling for `515`. Compile a module for android13-5.10 with
`-DNVK_KERNEL=51013`, and for android14-5.15 with `-DNVK_KERNEL=51514`.
Do not ship a `510` android12 `.ko` onto android13-5.10, or a `515`
android13 `.ko` onto android14-5.15. GKI `CONFIG_MODVERSIONS` drops the
first vermagic token, so the `android12-9` / `android13-8` spelling is
**not** what stops the loader. The compile families differ in
loader-visible `struct module`: `510` writes `exit` at 960 on a 1024-byte
image that android13-5.10 reads at 936; `515` is 960 bytes on a kernel
whose `struct module` is 1024. After a successful load, activate also
fail-closes on the wrong Android generation.

Local trees used for the new families (do not treat these binaries as
drop-in replacements for the pinned 510/515 archives):

| Tree | Banner |
|------|--------|
| `local_docs/android12-5.10-bin` | `5.10.205-android12-9-dirty` |
| `local_docs/android13-5.10-bin` | `5.10.223-android13-4-00011-ga33040a671e2-dirty` |
| `local_docs/android13-5.15-bin` | `5.15.153-android13-8-00026-g06276351e9ff-dirty` |
| `local_docs/android14-5.15-bin` | `5.15.164-android14-11-maybe-dirty` |

On GKI (`CONFIG_MODVERSIONS=y`) the loader compares vermagic **flags**
(`SMP preempt mod_unload modversions aarch64`) after dropping the first
token. Sublevel, `-dirty`, and git suffix therefore do **not** block
`insmod`. A `-DNVK_KERNEL=51013` module loads on any same-page
`5.10.*-android13-*` GKI whose `struct module` matches this family
(size 1024, `init` 400, `exit` 936), including the local dirty tree and
other official/OEM tokens. The same rule applies to `51514` on
`5.15.*-android14-*`. Same-generation `COMPAT` is the post-load layout
policy (patch / KMI / token ignored). A certificate overlays fields only
when the live release token matches byte-for-byte; it is not required
to activate. Ship the compile family that matches the Android
generation; do not mix `510`/`51013` or `515`/`51514`.

`CONFIG_CFI_CLANG` on 5.10/5.15 is classic Clang CFI (`__cfi_check` /
`__cfi_slowpath`), not NeverC `kcfi_mode`. 51013/51514 keep
`kcfi_mode=disabled` and emit no KCFI type-id prefixes. KCFI entry
prefixes start at the 601 family.

Activation with the selected compile family:

1. Same Linux patch + Android generation + KMI + page → `EXACT`, that
   family's layout. The catalog token (often a local `-dirty` / git
   suffix) is ignored.
2. Same `major.minor` + same Android generation + page → `COMPAT`, family
   layout. Patch, KMI, and release token are ignored. `612` on
   `android16-5` therefore uses the same table as `android16-6`.
3. Different Android generation (`510` on android13, `51013` on
   android12, `515` on android14, `51514` on android13) → fail closed.
   That generation is its own compile family.
4. OEM banner with no `-androidN-KMI` → `COMPAT` on the selected series
   (historical path).

Certificates overlay only a byte-for-byte live release token. A leftover
`6.12.38-android16-5-…` certificate therefore does not paint a later
`6.12.50` or `6.12.89` kernel; those keep the family table. A later
`5.15.170-android14-11-…` without its own certificate likewise keeps the
`51514` family layout.

Inspect any generated module before deployment with:

```bash
python3 runtime/android/kernel/tools/verify-android-module.py path/to/module.ko
```

## Linux 6.18 (android17-6.18) notes

Verified against GKI `6.18.24` (`gki_defconfig`, `CONFIG_COMPAT=y`):

| Item | Status |
|------|--------|
| `struct module` offsets (`init`/`exit`/sizeof) | Verified — `376` / `1536` / `1664` |
| `file_operations` (`mmap_prepare`, size 272) | Compile-time layout in `arm64/include/linux/fs.h` |
| `hrtimer_setup` / `hrtimer_start_range_ns` | Runtime lookup with legacy fallbacks |
| `vfs_fstatat` (replaces removed `vfs_stat`) | Runtime lookup |
| `queue_delayed_work` / `mod_delayed_work` | 6.18+: only `*_on` variants exported; see `workqueue.h` |
| `task_pid` inlined | `nvk_ns.c` probes `thread_pid` via `pid_vnr` |
| `nlmsg_hdr` inlined | `nvk_netlink.c` probes `sk_buff->data` at init |
| `execmem_alloc` / `execmem_free` | Interpose pool allocator (`nvk_interpose.c`) |
| `prepare_creds` + `commit_creds` | Credential API (`override_creds`/`revert_creds` not exported) |

**Not available on 6.18** (kernel removed the symbols from the export table):

- **ftrace interpose fallback** — `register_ftrace_function`, `unregister_ftrace_function`,
  `ftrace_set_filter_ip` are gone. `neverc_krt_ftrace_init()` returns `-1`;
  use inline patching or kprobes via `neverc_krt_interpose_auto()`.
- **`override_creds` / `revert_creds`** — there is no exported drop-in
  replacement with the same temporary-override semantics. Use
  `neverc_krt_cred_set_*` helpers for explicit credential changes; they are
  implemented on top of exported `prepare_creds` + `commit_creds`.
- **`abort_creds` / `get_current_cred` / `put_cred`** — present as internal
  kernel symbols on 6.18 but not exported to modules. The runtime avoids
  `get_task_cred` paths unless a matching release helper is available.

OEM kernels with `CONFIG_LOCALVERSION` (for example `"-4k"`) may share a
`struct module` layout with stock GKI. Selecting the matching series/profile at
build time permits the same-page OEM identity as `COMPAT`; exact certification
still requires its own evidence-backed profile. That compatibility path
starts after a successful load. On GKI `CONFIG_MODVERSIONS`, the loader
compares vermagic **flags** only (`SMP preempt mod_unload modversions aarch64`);
the compile-time first token does not have to equal the running
`UTS_RELEASE`. A different Android generation or page size still fail-closes
after load, because `struct module` and the family field table would be wrong.

## Source-level debugging

NeverC embeds debug-prefix-mapped paths in the DWARF info of the kernel runtime
bitcode so that debuggers can locate the original `.c` sources in your install
tree. The DWARF paths are relative to the NeverC install root:

- Kernel sources: `runtime/android/kernel/src/nvk_interpose.c`, etc.
- Kernel headers: `runtime/android/kernel/include/nvk_interpose.h`, etc.

These source files are already included in the `android-kernel-arm64` runtime
package. Configure your debugger to map the relative prefix to your NeverC root:

```bash
# GDB
(gdb) set substitute-path runtime/android/kernel /path/to/neverc/runtime/android/kernel

# LLDB
(lldb) settings set target.source-map "runtime/android/kernel" "/path/to/neverc/runtime/android/kernel"
```
