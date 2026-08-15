#!/usr/bin/env python3
"""Verify the embedded interpose runtime retains its live-profile KCFI path."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


KERNEL_ROOT = Path(__file__).resolve().parents[1]
SOURCE = KERNEL_ROOT / "src" / "nvk_interpose.c"


def function_body(ir: str, name: str) -> str:
    match = re.search(
        rf"^define\b[^\n]*@{re.escape(name)}\([^\n]*\)[^{{]*\{{\n(.*?)^\}}$",
        ir,
        re.MULTILINE | re.DOTALL,
    )
    if not match:
        raise RuntimeError(f"missing IR body for {name}")
    return match.group(1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--compiler",
        default=str(KERNEL_ROOT.parents[2] / "build-neverc" / "bin" / "neverc"),
    )
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="neverc-interpose-kcfi-") as temp:
        output = Path(temp) / "nvk_interpose.ll"
        subprocess.run(
            [
                args.compiler,
                "--target=aarch64-linux-android",
                "-fandroid-kernel-driver-mode",
                f"-I{KERNEL_ROOT / 'include'}",
                f"-I{KERNEL_ROOT / 'src'}",
                "-O0",
                "-fno-lto",
                "-S",
                "-emit-llvm",
                str(SOURCE),
                "-o",
                str(output),
            ],
            check=True,
        )
        ir = output.read_text()
        wrapper = function_body(ir, "neverc_krt_interpose_install_ctx")
        body = function_body(ir, "_neverc_krt_interpose_install_ctx_impl")
        patch_body = function_body(ir, "_neverc_krt_patch_multi")
        ll_install_body = function_body(ir, "_neverc_krt_ll_install")
        register_body = function_body(
            ir, "_neverc_krt_interpose_register_impl"
        )
        probe_register_body = function_body(
            ir, "_neverc_krt_probe_register_impl"
        )
        pause_body = function_body(ir, "neverc_krt_interpose_pause")

    if "call i32 @_neverc_krt_interpose_install_ctx_impl" not in wrapper:
        raise RuntimeError(
            "public install_ctx no longer delegates to the implementation"
        )
    if "call i32 @_neverc_krt_interpose_register_impl" not in function_body(
        ir, "neverc_krt_interpose_register"
    ):
        raise RuntimeError(
            "public register no longer delegates to the implementation"
        )
    if "call i32 @_neverc_krt_probe_register_impl" not in function_body(
        ir, "neverc_krt_probe_register"
    ):
        raise RuntimeError(
            "public probe_register no longer delegates to the implementation"
        )
    if "call i32 @_neverc_krt_current_kcfi_mode" not in body:
        raise RuntimeError(
            "default embedded-runtime IR folded away the live-profile KCFI branch"
        )
    if "store i32 -7" not in body:
        raise RuntimeError("missing fail-closed KCFI error path")

    llvm_local = r"%[-A-Za-z$._0-9]+"
    prefix_geps = re.findall(
        rf"^\s*({llvm_local}) = getelementptr inbounds i32, "
        rf"ptr {llvm_local}, i64 -1$",
        body,
        re.MULTILINE,
    )
    if len(prefix_geps) < 2:
        raise RuntimeError("missing target/trampoline KCFI prefix addressing")
    if not any(
        re.search(
            rf"@neverc_krt_mem_read\([^\n]*ptr noundef {re.escape(gep)}\b",
            body,
        )
        for gep in prefix_geps
    ):
        raise RuntimeError("target[-4] KCFI prefix is not read through safe memory API")
    if not any(
        re.search(rf"store i32 {llvm_local}, ptr {re.escape(gep)}\b", body)
        for gep in prefix_geps
    ):
        raise RuntimeError("target KCFI type ID is not written to trampoline[-4]")

    publish = body.find("store atomic i64")
    enable = body.find("call void @_neverc_krt_ctx_set_enabled", publish)
    patch = body.find("call i32 @_neverc_krt_patch_multi", enable)
    if min(publish, enable, patch) < 0 or not (publish < enable < patch):
        raise RuntimeError(
            "call_orig must be release-published before enabling and patching entry"
        )
    disable = body.find("call void @_neverc_krt_ctx_set_enabled", patch)
    restore = body.find(
        "call i32 @_neverc_krt_restore_ctx_text_unchecked", disable
    )
    drain = body.find("call i32 @_neverc_krt_wait_one_ctx_inflight", restore)
    clear_guard = body.find("call void @_neverc_krt_ctx_clear_guard", drain)
    if min(disable, restore, drain, clear_guard) < 0 or not (
        patch < disable < restore < drain < clear_guard
    ):
        raise RuntimeError(
            "post-publication failure must disable, restore, drain, then clear guard"
        )

    if "@_neverc_krt_patchtext" not in patch_body:
        raise RuntimeError("synchronized multi-patch backend is not called")
    for forbidden in (
        "@neverc_krt_mem_make_rw",
        "@neverc_krt_mem_make_ro",
        "@_neverc_krt_write_insn",
    ):
        if forbidden in patch_body:
            raise RuntimeError(
                f"live entry patch contains sequential fallback {forbidden}"
            )

    retained_sync = ll_install_body.find(
        "call void @_neverc_krt_ll_sync_from_ctx"
    )
    release_slot = ll_install_body.find(
        "call void @_neverc_krt_ll_release_slot", retained_sync
    )
    success_sync = ll_install_body.find(
        "call void @_neverc_krt_ll_sync_from_ctx", retained_sync + 1
    )
    if min(retained_sync, release_slot, success_sync) < 0 or not (
        retained_sync < release_slot < success_sync
    ):
        raise RuntimeError(
            "low-level install must retain and mirror an active failed ctx "
            "before its ordinary inactive-failure slot release"
        )

    retained_helper = "call i32 @_neverc_krt_ctx_install_retained"
    for name, install_body in (
        ("low-level", ll_install_body),
        ("registry", register_body),
        ("probe", probe_register_body),
    ):
        if retained_helper not in install_body:
            raise RuntimeError(
                f"{name} install does not preserve an active failed ctx"
            )

    pause_slot = pause_body.find("call i32 @_neverc_krt_ll_find_slot")
    pause_disable = pause_body.find(
        "call void @_neverc_krt_ctx_set_enabled", pause_slot
    )
    pause_drain = pause_body.find(
        "call i32 @_neverc_krt_wait_one_ctx_inflight", pause_disable
    )
    if min(pause_slot, pause_disable, pause_drain) < 0 or not (
        pause_slot < pause_disable < pause_drain
    ):
        raise RuntimeError(
            "low-level pause must disable its persistent ctx and drain exact inflight"
        )

    dispatch_body = function_body(ir, "_neverc_krt_ll_set_dispatch")
    dispatch_slot = dispatch_body.find("call i32 @_neverc_krt_ll_find_slot")
    dispatch_enable = dispatch_body.find(
        "call void @_neverc_krt_ctx_set_enabled", dispatch_slot
    )
    if min(dispatch_slot, dispatch_enable) < 0 or not (
        dispatch_slot < dispatch_enable
    ):
        raise RuntimeError(
            "low-level dispatch must mirror enabled onto the persistent ctx"
        )
    for name in (
        "neverc_krt_interpose_enable",
        "neverc_krt_interpose_disable",
    ):
        public_body = function_body(ir, name)
        if "call void @_neverc_krt_ll_set_dispatch" not in public_body:
            raise RuntimeError(
                f"{name} must publish through the low-level dispatch mirror"
            )

    print("interpose KCFI/publication/synchronized-patch IR test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
