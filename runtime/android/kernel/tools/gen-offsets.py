#!/usr/bin/env python3
"""Compute exact struct module offsets for a GKI kernel.

Usage:
    python3 gen-offsets.py <GKI-common> [generated-dir]

<GKI-common>    kernel source root (contains include/, arch/, ...)
[generated-dir] pre-built out-of-tree dir (defaults to auto-prepared temp)

If the tree is already prepared (has include/generated/autoconf.h),
pass the source root as both arguments.

Output: NEVERC_KRT_* evidence values for a profile layout manifest.
"""
import os
import re
import sys
import glob
import shutil
import subprocess
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROBE_C = os.path.join(SCRIPT_DIR, "gen_struct_module_offsets.c")
DEFAULT_NEVERC = os.path.join(SCRIPT_DIR, "..", "..", "..", "..",
                              "build-neverc", "bin", "neverc")


def find_neverc():
    neverc = os.environ.get("NEVERC", DEFAULT_NEVERC)
    if os.path.isfile(neverc) and os.access(neverc, os.X_OK):
        return neverc
    for name in ("neverc", "clang"):
        path = shutil.which(name)
        if path:
            return path
    return None


def prepare_tree(kt, gen):
    """Best-effort kernel prepare: defconfig + synthesized asm wrappers."""
    os.makedirs(gen, exist_ok=True)

    ld = os.environ.get("LD") or shutil.which("ld.lld") or "ld"

    make_env = dict(os.environ, ARCH="arm64", HOSTCC="cc", CC="cc", LD=ld)

    subprocess.run(
        ["make", "-C", kt, f"O={gen}", "gki_defconfig"],
        env=make_env, check=True,
    )
    subprocess.run(
        ["make", "-C", kt, f"O={gen}", "syncconfig"],
        env=make_env, check=True,
    )

    ga = os.path.join(gen, "arch", "arm64", "include", "generated", "asm")
    uga = os.path.join(gen, "arch", "arm64", "include", "generated",
                       "uapi", "asm")
    os.makedirs(ga, exist_ok=True)
    os.makedirs(uga, exist_ok=True)

    for h in glob.glob(os.path.join(kt, "include", "asm-generic", "*.h")):
        b = os.path.basename(h)
        if b == "Kbuild":
            continue
        if os.path.exists(os.path.join(kt, "arch", "arm64", "include",
                                       "asm", b)):
            continue
        dst = os.path.join(ga, b)
        if not os.path.exists(dst):
            with open(dst, "w") as f:
                f.write(f"#include <asm-generic/{b}>\n")

    for h in glob.glob(os.path.join(kt, "include", "uapi", "asm-generic",
                                    "*.h")):
        b = os.path.basename(h)
        if b == "Kbuild":
            continue
        if os.path.exists(os.path.join(kt, "arch", "arm64", "include",
                                       "uapi", "asm", b)):
            continue
        if os.path.exists(os.path.join(kt, "arch", "arm64", "include",
                                       "asm", b)):
            continue
        dst = os.path.join(uga, b)
        if not os.path.exists(dst):
            with open(dst, "w") as f:
                f.write(f"#include <asm-generic/{b}>\n")

    cpucaps_awk = os.path.join(kt, "arch", "arm64", "tools",
                               "gen-cpucaps.awk")
    cpucaps_in = os.path.join(kt, "arch", "arm64", "tools", "cpucaps")
    if os.path.isfile(cpucaps_awk) and os.path.isfile(cpucaps_in):
        try:
            out = subprocess.check_output(
                ["awk", "-f", cpucaps_awk, cpucaps_in],
                stderr=subprocess.DEVNULL,
            )
            with open(os.path.join(ga, "cpucaps.h"), "wb") as f:
                f.write(out)
        except (subprocess.CalledProcessError, FileNotFoundError):
            pass


def compile_probe(neverc, kt, gen):
    """Compile the probe to assembly and extract NVK offsets."""
    with tempfile.NamedTemporaryFile(suffix=".s", delete=False) as tmp:
        out_path = tmp.name

    autoconf = os.path.join(gen, "include", "generated", "autoconf.h")
    kconfig = os.path.join(kt, "include", "linux", "kconfig.h")

    cmd = [
        neverc, "--target=aarch64-linux-android",
        "-fno-lto", "-nostdlibinc", "-std=gnu11",
        "-D__KERNEL__", "-DNVK_GEN_KSRC=1",
        "-U__GNUC__", "-D__GNUC__=12",
        "-U__GNUC_MINOR__", "-D__GNUC_MINOR__=0",
        "-U__GNUC_PATCHLEVEL__", "-D__GNUC_PATCHLEVEL__=0",
        "-Wno-unknown-attributes", "-Wno-error",
        f"-I{kt}/arch/arm64/include",
        f"-I{gen}/arch/arm64/include/generated",
        f"-I{kt}/include",
        f"-I{gen}/include",
        f"-I{gen}/include/generated",
        f"-I{kt}/arch/arm64/include/uapi",
        f"-I{gen}/arch/arm64/include/generated/uapi",
        f"-I{kt}/include/uapi",
        f"-I{gen}/include/generated/uapi",
        f"-include", kconfig,
        f"-include", autoconf,
        "-S", "-o", out_path,
        PROBE_C,
    ]

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Probe compile failed (rc={result.returncode}):",
              file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        os.unlink(out_path)
        return None

    with open(out_path) as f:
        asm_text = f.read()
    os.unlink(out_path)

    offsets = {}
    for m in re.finditer(r'==NVK==\s+(\w+)\s+(\d+)\s+==', asm_text):
        offsets[m.group(1)] = int(m.group(2))

    return offsets


def main():
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} <GKI-common> [generated-dir]",
              file=sys.stderr)
        sys.exit(1)

    kt = os.path.abspath(sys.argv[1])
    gen = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else None

    neverc = find_neverc()
    if not neverc:
        print("neverc/clang not found; set NEVERC env var", file=sys.stderr)
        sys.exit(1)

    autoconf = os.path.join(kt, "include", "generated", "autoconf.h")
    if os.path.isfile(autoconf):
        if gen is None:
            gen = kt
    else:
        if gen is None:
            gen = os.path.join(tempfile.gettempdir(),
                               f"nvk-kbuild-{os.path.basename(kt)}")
        print(f"[*] preparing config in {gen} ...")
        prepare_tree(kt, gen)

    offsets = compile_probe(neverc, kt, gen)
    if not offsets:
        print("Failed to extract offsets; prepare the tree first.",
              file=sys.stderr)
        sys.exit(1)

    print()
    print("// record in the profile layout manifest, then regenerate:")
    for name in sorted(offsets):
        val = offsets[name]
        print(f"#  define {name} {val}  /* 0x{val:X} */")


if __name__ == "__main__":
    main()
