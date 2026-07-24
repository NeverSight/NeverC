#!/usr/bin/env python3
"""Measure the no-plugin / empty-plugin compilation overhead of the NeverC
first-version plugin runtime and enforce the design's <=1% regression budget.

Design requirement (spec section 16.5 / volume 6 completion item 10): with no
plugin selected, bootstrap only makes the single nullable activation-plan
decision and constructs no Session/task/domain tables/arena/locks, so the median
wall time and peak RSS of an ordinary compile must not regress relative to a
fully-disabled baseline.  The design mandates that the baseline be produced by
the *same* shipped binary running a test-only fully-disabled mode, so that the
comparison is free of build-to-build drift (a separately compiled baseline would
differ in inlining, layout and codegen for reasons unrelated to the plugin
seam).  This harness therefore measures one binary in several scenarios:

    fully-disabled -- the shipped binary with the test-only environment knob
                      ``NEVERC_TEST_PLUGIN_FULLY_DISABLED=1`` set, which makes
                      the driver skip the plugin bootstrap decision entirely
                      (behaving as if plugin support were compiled out).
    no-plugin      -- an ordinary compile, no ``-fplugin`` (the shipped default
                      path that still makes the nullable activation decision).
    empty-plugin   -- a registered but no-op plugin is loaded.
    observer       -- a plugin that only installs read-only observers.

The hard gate compares ``no-plugin`` against ``fully-disabled``; if either the
median wall time or the median peak RSS regresses by more than the configured
budget the gate fails.  ``empty-plugin`` / ``observer`` legitimately pay
``dlopen`` + session setup cost, so they are reported for information only and
are NOT subject to the <=1% no-plugin budget.

Machine noise is never absorbed by relaxing the threshold (design task 24 step
8): if the gate is over budget the whole measurement is repeated a fixed number
of times (``--retries``) and only a run that is still over budget on the final
attempt fails.  Raw per-scenario medians and samples can be persisted with
``--json`` for post-mortem.

An optional ``--baseline-compiler`` may point at a *separately* built,
plugin-compiled-out binary; when given, its ordinary compile is enforced as an
additional cross-binary baseline on top of the same-binary one.

This is intentionally dependency-free (standard library only) so it can run in
CI without extra packages.  Per-run peak RSS is read from ``os.wait4`` rusage;
``ru_maxrss`` is bytes on Darwin and kilobytes on Linux, normalised to bytes
here.
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import statistics
import sys
import tempfile
import time

# Normalise ru_maxrss (bytes on Darwin, KiB on Linux) to bytes.
_RU_MAXRSS_SCALE = 1 if sys.platform == "darwin" else 1024

# Test-only environment knob understood by the shipped driver: when set to a
# non-empty, non-"0" value the very same binary skips the plugin bootstrap
# decision entirely, i.e. behaves as if plugin support were compiled out.
# Running the fully-disabled baseline and the normal no-plugin compile from one
# binary removes the build-to-build drift a separately compiled baseline would
# introduce -- exactly what design completion item 10 requires.
FULLY_DISABLED_ENV = "NEVERC_TEST_PLUGIN_FULLY_DISABLED"


SAMPLE_SOURCE = r"""
/* Small but non-trivial translation unit: exercises the front end, IR and the
 * backend so the measured cost is representative of a real compile rather than
 * an empty file. */
#include <stdint.h>

static uint64_t mix(uint64_t x) {
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33;
  return x;
}

uint64_t sum_mixed(const uint64_t *data, unsigned n) {
  uint64_t acc = 0;
  for (unsigned i = 0; i < n; ++i)
    acc += mix(data[i] + i);
  return acc;
}

int compare_u64(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return (x > y) - (x < y);
}
"""


def run_once(argv: list[str], env: dict | None = None) -> tuple[float, int]:
    """Run one compile, returning (wall_seconds, peak_rss_bytes)."""
    devnull = os.open(os.devnull, os.O_WRONLY)
    try:
        start = time.perf_counter()
        pid = os.posix_spawn(argv[0], argv,
                             env if env is not None else os.environ,
                             file_actions=[
                                 (os.POSIX_SPAWN_DUP2, devnull, 1),
                                 (os.POSIX_SPAWN_DUP2, devnull, 2),
                             ])
        _, status, rusage = os.wait4(pid, 0)
        elapsed = time.perf_counter() - start
    finally:
        os.close(devnull)
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        raise RuntimeError(f"compile failed (status={status}): {' '.join(argv)}")
    return elapsed, int(rusage.ru_maxrss) * _RU_MAXRSS_SCALE


def measure(argv: list[str], iterations: int, warmup: int,
            env: dict | None = None) -> dict:
    for _ in range(warmup):
        run_once(argv, env)
    times: list[float] = []
    rss: list[int] = []
    for _ in range(iterations):
        t, r = run_once(argv, env)
        times.append(t)
        rss.append(r)
    return {
        "time_median": statistics.median(times),
        "time_min": min(times),
        "time_samples": times,
        "rss_median": statistics.median(rss),
        "rss_max": max(rss),
        "rss_samples": rss,
        "samples": iterations,
    }


def default_plugin(build_dir: pathlib.Path | None, name: str) -> pathlib.Path | None:
    if build_dir is None:
        return None
    suffix = ".dylib" if sys.platform == "darwin" else ".so"
    for rel in (
        pathlib.Path("neverc/pluginsdk/examples/host") / f"{name}{suffix}",
        pathlib.Path("neverc/pluginsdk/examples/host") / f"{name}.so",
        pathlib.Path("pluginsdk/examples/host") / f"{name}{suffix}",
    ):
        cand = build_dir / rel
        if cand.exists():
            return cand
    return None


def pct(new: float, base: float) -> float:
    return 0.0 if base == 0 else (new - base) / base


def summarise(result: dict) -> dict:
    """Drop the raw sample lists for a compact JSON/console view."""
    return {
        "time_median": result["time_median"],
        "time_min": result["time_min"],
        "rss_median": result["rss_median"],
        "rss_max": result["rss_max"],
        "samples": result["samples"],
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--compiler", required=True, type=pathlib.Path)
    ap.add_argument("--baseline-compiler", type=pathlib.Path, default=None,
                    help="an optional separately built, plugin-compiled-out "
                         "binary; when given its ordinary compile is enforced "
                         "as an ADDITIONAL cross-binary fully-disabled baseline "
                         "on top of the same-binary baseline.")
    ap.add_argument("--require-baseline", action="store_true",
                    help="fail if --baseline-compiler is not provided (for CI "
                         "configs that also want the cross-binary check).")
    ap.add_argument("--build-dir", type=pathlib.Path, default=None,
                    help="locate built example plugins when --empty/--observer "
                         "are not given explicitly")
    ap.add_argument("--empty-plugin", type=pathlib.Path, default=None,
                    help="registered no-op plugin (default: BenchPlugin)")
    ap.add_argument("--observer-plugin", type=pathlib.Path, default=None,
                    help="read-only observer plugin (default: DriverTracePlugin)")
    ap.add_argument("--iterations", type=int, default=31)
    ap.add_argument("--warmup", type=int, default=3)
    ap.add_argument("--max-time-regression", type=float, default=0.01)
    ap.add_argument("--max-rss-regression", type=float, default=0.01)
    ap.add_argument("--retries", type=int, default=3,
                    help="extra whole-measurement attempts if the gate is over "
                         "budget; the design forbids relaxing the threshold for "
                         "machine noise, so re-measure a fixed number of times "
                         "and fail only if still over on the final attempt.")
    ap.add_argument("--target", default=None,
                    help="optional --target triple for the sample compile")
    ap.add_argument("--json", type=pathlib.Path, default=None,
                    help="persist raw medians / gate deltas as a JSON artifact")
    args = ap.parse_args()

    compiler = args.compiler.resolve()
    if not compiler.exists():
        print(f"error: compiler not found: {compiler}", file=sys.stderr)
        return 2

    if args.require_baseline and args.baseline_compiler is None:
        print("FAIL: --require-baseline set but --baseline-compiler missing",
              file=sys.stderr)
        return 1

    separate_baseline: pathlib.Path | None = None
    if args.baseline_compiler is not None:
        separate_baseline = args.baseline_compiler.resolve()
        if not separate_baseline.exists():
            print(f"error: baseline compiler not found: {separate_baseline}",
                  file=sys.stderr)
            return 2

    build_dir = args.build_dir
    if build_dir is None:
        # Infer build dir from the compiler path (…/build-neverc/bin/neverc).
        maybe = compiler.parent.parent
        if (maybe / "CMakeCache.txt").exists():
            build_dir = maybe

    empty = args.empty_plugin or default_plugin(build_dir, "BenchPlugin")
    observer = args.observer_plugin or default_plugin(build_dir, "DriverTracePlugin")

    disabled_env = dict(os.environ)
    disabled_env[FULLY_DISABLED_ENV] = "1"

    # The gate must never see a stray knob leaking in from the caller.
    normal_env = dict(os.environ)
    normal_env.pop(FULLY_DISABLED_ENV, None)

    with tempfile.TemporaryDirectory(prefix="nvc-perf-") as tmp:
        src = pathlib.Path(tmp) / "sample.c"
        src.write_text(SAMPLE_SOURCE, encoding="utf-8")
        obj = pathlib.Path(tmp) / "sample.o"

        base_argv = [str(compiler), "-O2", "-c", str(src), "-o", str(obj)]
        if args.target:
            base_argv[1:1] = [f"--target={args.target}"]

        # Informational (dlopen-bearing) scenarios, measured once.
        info_scenarios: list[tuple[str, list[str]]] = []
        if empty and empty.exists():
            info_scenarios.append(
                ("empty-plugin", base_argv + [f"-fplugin={empty}"]))
        else:
            print("note: no empty plugin found; skipping empty-plugin overhead")
        if observer and observer.exists():
            info_scenarios.append(
                ("observer",
                 base_argv + [f"-fplugin={observer}", "--driver-trace"]))

        info_results: dict[str, dict] = {}
        for name, argv in info_scenarios:
            try:
                info_results[name] = measure(argv, args.iterations, args.warmup,
                                             env=normal_env)
            except RuntimeError as e:
                print(f"error: scenario {name}: {e}", file=sys.stderr)
                return 2

        # Hard gate scenarios, re-measured on noise.
        attempts: list[dict] = []
        gate_pass = False
        last_failures: list[str] = []
        last_no_plugin: dict = {}
        last_disabled: dict = {}
        last_separate: dict | None = None
        for attempt in range(args.retries + 1):
            try:
                disabled = measure(base_argv, args.iterations, args.warmup,
                                   env=disabled_env)
                no_plugin = measure(base_argv, args.iterations, args.warmup,
                                    env=normal_env)
                separate = None
                if separate_baseline is not None:
                    sep_argv = [str(separate_baseline)] + base_argv[1:]
                    separate = measure(sep_argv, args.iterations, args.warmup,
                                       env=normal_env)
            except RuntimeError as e:
                print(f"error: gate measurement: {e}", file=sys.stderr)
                return 2

            failures: list[str] = []
            dt = pct(no_plugin["time_median"], disabled["time_median"])
            dr = pct(no_plugin["rss_median"], disabled["rss_median"])
            if dt > args.max_time_regression:
                failures.append(
                    f"no-plugin time +{dt*100:.2f}% vs same-binary "
                    f"fully-disabled > {args.max_time_regression*100:.2f}%")
            if dr > args.max_rss_regression:
                failures.append(
                    f"no-plugin rss +{dr*100:.2f}% vs same-binary "
                    f"fully-disabled > {args.max_rss_regression*100:.2f}%")
            if separate is not None:
                st = pct(no_plugin["time_median"], separate["time_median"])
                sr = pct(no_plugin["rss_median"], separate["rss_median"])
                if st > args.max_time_regression:
                    failures.append(
                        f"no-plugin time +{st*100:.2f}% vs separate baseline "
                        f"> {args.max_time_regression*100:.2f}%")
                if sr > args.max_rss_regression:
                    failures.append(
                        f"no-plugin rss +{sr*100:.2f}% vs separate baseline "
                        f"> {args.max_rss_regression*100:.2f}%")

            print(f"--- attempt {attempt + 1}/{args.retries + 1} "
                  f"({args.iterations} iterations) ---")
            print(f"{'scenario':<24}{'time_median(ms)':>18}"
                  f"{'rss_median(MiB)':>18}{'d_time':>10}{'d_rss':>10}")
            print(f"{'fully-disabled':<24}{disabled['time_median']*1e3:>18.2f}"
                  f"{disabled['rss_median']/(1024*1024):>18.2f}{'--':>10}{'--':>10}")
            print(f"{'no-plugin':<24}{no_plugin['time_median']*1e3:>18.2f}"
                  f"{no_plugin['rss_median']/(1024*1024):>18.2f}"
                  f"{dt*100:>9.2f}%{dr*100:>9.2f}%")
            if separate is not None:
                st = pct(no_plugin["time_median"], separate["time_median"])
                sr = pct(no_plugin["rss_median"], separate["rss_median"])
                print(f"{'separate-baseline':<24}"
                      f"{separate['time_median']*1e3:>18.2f}"
                      f"{separate['rss_median']/(1024*1024):>18.2f}"
                      f"{st*100:>9.2f}%{sr*100:>9.2f}%")

            attempts.append({
                "attempt": attempt + 1,
                "fully_disabled": summarise(disabled),
                "no_plugin": summarise(no_plugin),
                "separate_baseline": summarise(separate) if separate else None,
                "d_time_same_binary": dt,
                "d_rss_same_binary": dr,
                "failures": failures,
            })
            last_failures = failures
            last_no_plugin, last_disabled, last_separate = \
                no_plugin, disabled, separate
            if not failures:
                gate_pass = True
                break
            if attempt < args.retries:
                print(f"note: gate over budget on attempt {attempt + 1}; "
                      f"re-measuring ({attempt + 1}/{args.retries} retries used)")

        # Informational overhead table.
        if info_results:
            print("--- informational plugin overhead (exempt from the "
                  "<=1% no-plugin budget) ---")
            base_time = last_no_plugin["time_median"]
            base_rss = last_no_plugin["rss_median"]
            print(f"{'scenario':<24}{'time_median(ms)':>18}"
                  f"{'rss_median(MiB)':>18}{'d_time':>10}{'d_rss':>10}")
            for name, r in info_results.items():
                it = pct(r["time_median"], base_time)
                ir = pct(r["rss_median"], base_rss)
                print(f"{name:<24}{r['time_median']*1e3:>18.2f}"
                      f"{r['rss_median']/(1024*1024):>18.2f}"
                      f"{it*100:>9.2f}%{ir*100:>9.2f}%")

        if args.json is not None:
            artifact = {
                "compiler": str(compiler),
                "separate_baseline": str(separate_baseline)
                if separate_baseline else None,
                "iterations": args.iterations,
                "max_time_regression": args.max_time_regression,
                "max_rss_regression": args.max_rss_regression,
                "gate_pass": gate_pass,
                "attempts": attempts,
                "informational": {n: summarise(r)
                                  for n, r in info_results.items()},
            }
            args.json.write_text(json.dumps(artifact, indent=2), encoding="utf-8")
            print(f"wrote {args.json}")

    if not gate_pass:
        for f in last_failures:
            print(f"FAIL: {f}", file=sys.stderr)
        print(f"FAIL: no-plugin overhead gate still over budget after "
              f"{args.retries + 1} attempts", file=sys.stderr)
        return 1
    print("measure-plugin-overhead: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
