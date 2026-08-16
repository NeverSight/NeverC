# GKI feature compatibility audit

Hard matrix: `510` `51013` `515` `51514` `601` `606` `612` `618`.
Status: `supported` | `fallback` | `unsupported` | `unverified`.
Page size in every current profile is 4K / VA39 / 3-level. OEM, 16K, and 64K
remain fail-closed until they have their own image, manifest, and certificate.

This file is the honest support claim. Loader-green QEMU is not full API
compatibility.

## Evidence

Catalog: `arm64/gki-local-evidence.json`.
Auditor: `tools/audit-gki-evidence.py`.

| Profile | Artifacts | Source tree | Manifest-bound | Notes |
|---------|-----------|-------------|----------------|-------|
| 510 | config, image, symvers, System.map, vmlinux | `GKI-android12-5.10-kit/common` | none | DWARF layouts |
| 51013 | same | `GKI-android13-5.10-kit/common` | vmlinux | local tree is `5.10.223` |
| 515 | same | `GKI-android13-5.15-kit/common` | none | |
| 51514 | same | `GKI-android14-5.15-kit/common` | vmlinux | local tree is `5.15.164` |
| 601 | same | `GKI-android14-6.1-kit/common` | none | |
| 606 | same | `GKI-android15-6.6-kit/common` | none | split `module_memory[7]` |
| 612 | same | `GKI-android16-6.12-kit/common` | none | named `vmalloc_info_show` |
| 618 | image, System.map, vmlinux | `GKI-android17-6.18-kit/common` | vmlinux | local config/symvers missing |

Required System.map symbols now include binder, vmalloc backend, visibility
mutex/kobject/list-head, `modules_op`, `mounts_op`, `proc_pid_maps_op`,
`proc_pid_maps_operations`, file I/O, pid-namespace, and power notifiers.
`vfs_fstatat` or `vfs_stat` is accepted. The auditor now reports every
unbound `config`, `Module.symvers`, or `vmlinux` as a failure; the current local
catalog therefore remains red until the pinned artifacts replace the
same-family development builds.

## Capability matrix

| Capability | 510 | 51013 | 515 | 51514 | 601 | 606 | 612 | 618 |
|------------|-----|-------|-----|-------|-----|-----|-----|-----|
| `binder_filter_backend` | transaction | transaction | transaction | transaction | transaction | transaction | transaction | transaction |
| `vmalloc_visibility_backend` | seq_ops | seq_ops | seq_ops | seq_ops | seq_ops | seq_ops | named_show | named_show |
| `user_ptmap_backend` | legacy_510 | legacy_510 | legacy_515 | legacy_515 | classic_601 | classic_606 | normalized_612+ | normalized_612+ |
| `filldir_abi` | int | int | int | int | bool | bool | bool | bool |
| `do_mmap_abi` | without | without | without | without | without | with | with | with |
| `kallsyms_iter_abi` | with_module | with_module | with_module | with_module | with_module | address_only | address_only | address_only |
| `ftrace_callback_abi` | pt_regs | pt_regs | ftrace_regs | ftrace_regs | ftrace_regs | ftrace_regs | ftrace_regs | ftrace_regs |
| `ftrace_registration_api` | false | false | false | false | false | false | false | false |
| module memory ranges | 1 | 1 | 1 | 1 | 1 | 7 | 7 | 7 |

## Category status

| Category | Impl | Host | Compile | QEMU | Status |
|----------|------|------|---------|------|--------|
| bootstrap / kallsyms | real | policy + evidence | yes | loader smoke | supported |
| interpose | inline real; ftrace closed | yes | yes | loader smoke | inline supported; ftrace unsupported |
| cred / task | real, layout-gated | yes | yes | loader smoke | supported / guest unverified |
| VFS / inode / dir / file | real; file init records I/O symbols and fails at the API | dir/inode/file | yes | loader smoke | supported / guest unverified |
| binder | `binder_transaction` + overflow-checked allocation rejection | lifecycle + reject ordering | yes | none | supported; live rejection still guest-unverified |
| SELinux / seccomp | real, opt-in | no | partial | none | unverified |
| visibility | transactional list restore + multi-range vmalloc + scoped maps and rendered mounts hooks | list rollback + vmalloc + maps/mounts contracts | yes | none | supported / guest unverified |
| mem / VMA / ptmap | real; ptmap 4K-only via capability | yes | yes | loader smoke | ptmap 4K supported; mapping mutation guest-unverified |
| namespace / netlink | real; ns/netlink stay optional and fail at their APIs | no | yes | none | supported / guest unverified |
| crypto / anti | software crypto; heuristic anti | no | partial | none | crypto supported; anti unverified |
| timer / power / CPU / PMU | real; timer/cpu are core; power stays optional | notifier lifecycle | yes | none | timer/cpu supported; PMU unverified |

## Closed in this pass

- Binder no longer hooks `binder_ioctl` and no longer stats-only. It interposes
  `binder_transaction` and forces an overflow-checked buffer-allocation failure.
  This avoids `binder_user_error`. Bounded vendor-hook parsers see the
  oversized copied header and return before touching the userspace Parcel.
- Vmalloc hide no longer assumes one contiguous module allocation. 6.6+ uses
  seven `module_memory` ranges from the generated profile table.
- `vmallocinfo` resolution no longer depends on compiler-local `s_show.N`.
- `/proc/modules` now reads `modules_op.show` instead of ambiguous `m_show`.
- `/proc/pid/maps` interposes `proc_pid_maps_op.show` and skips VMAs that
  overlap recorded ranges. New callers can scope a range to one task's
  referenced `mm`; the legacy range API remains intentionally global.
  Linux 6.12+ `PROCMAP_QUERY` is rejected for a global rule or the queried
  maps file's matching `mm`.  Private `file` / `seq_file` /
  `proc_maps_private` offsets live in the family capability table.  An
  unrecognized COMPAT private layout leaves a task-scoped query alone
  instead of disabling every process.  `install()` on those kernels
  rejects any global-only rule and requires `add_task`.
- `/proc/mounts` reads `mounts_op.show`, parses the mountpoint field in mounts,
  mountinfo, and mountstats output, and matches kernel path escaping instead of
  guessing a `struct mount` field or scanning unrelated device/options text.
- `neverc_krt_init_all()` records every subsystem and pins `NEVERC_KRT_KERNEL`.
  Core helpers that every 5.10–6.18 GKI must export (mem/compat/process/cred/
  thread/interpose/ksyms/timer/cpu/vma) fail the load if missing. Feature
  backends (vis/file/ns/power/binder/SELinux/syscall/netlink/xmem/addr) stay
  recorded and fail at their APIs. Init/cleanup stay serialized, and partial
  cleanup never restores `ready`.
- User ptmap policy is selected by `user_ptmap_backend`, not Linux
  `major.minor`.
- Visibility, file, namespace, and power resolve symbols during init and
  fail at the call site when a helper is absent. Version-specific helpers
  such as `vfs_fstatat` vs `vfs_stat` stay alternative; `task_pid` stays a
  layout fallback.
- Module-list visibility locks `module_mutex` when present, rolls back
  partial writes, and prefers saved-neighbor restore for immediate rollback.
- Local evidence auditor and host contracts cover Binder, visibility, power
  notifier ownership, vmalloc, init-all, and strict manifest binding.
- Unknown OEM/COMPAT tokens keep the selected family's default runtime
  layout. A certificate overlays offsets only when the live token matches
  byte-for-byte; a missing token never disables the family table.

## Still not claimed

- Guest QEMU remains loader-only: vermagic, `struct module` init/exit, and
  load/unload. It does not prove runtime APIs.
- ftrace registration is closed on all eight profiles. Do not document an
  ftrace fallback as available.
- 16K / 64K / OEM vendor layouts without a certificate.
- SELinux, seccomp, anti, and PMU as production-tested guest features.
- `618` local `.config` and `Module.symvers`.
- Manifest-bound pinned config/symvers/vmlinux evidence for every profile.

## Test layers

| Layer | What it proves |
|-------|----------------|
| Host contracts | binder reject/lifecycle, transactional list restore, power notifier ownership, vmalloc ranges, scoped maps/mounts seq_ops, retained-hook ownership, ptmap backends, init-all status, evidence audit |
| Compile / ELF | demos × 8 profiles, smoke `.ko` |
| QEMU smoke | vermagic + `struct module` init/exit offsets + load/unload |
