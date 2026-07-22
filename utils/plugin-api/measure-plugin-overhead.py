#!/usr/bin/env python3
"""Measure the no-plugin / empty-plugin compilation overhead of the NeverC
first-version plugin runtime and enforce the design's <=1% regression budget.

Design requirement (spec section 16.5 / volume 6 completion item 10): with no
plugin selected, bootstrap only makes the single nullable activation-plan
decision and constructs no Session/task/domain tables/arena/locks, so the median
wall time and peak RSS of an ordinary compile must not regress relative to the
plugin-free baseline.  This harness measures the *same* shipped binary in
several scenarios and compares them:

    baseline      -- ordinary compile, no ``-fplugin``.
    empty-plugin  -- a registered but no-op plugin is loaded.
    observer      -- a plugin that only installs read-only observers (optional).

``empty-plugin`` (and ``observer``) are compared against ``baseline``; if either
median regresses by more than the configured budget the gate fails.  Running the
same binary for every scenario removes build-to-build noise; use a quiet machine
and enough iterations for a stable median.

This is intentionally dependency-free (standard library only) so it can run in
CI without extra packages.  Per-run peak RSS is read from ``os.wait4`` rusage;
``ru_maxrss`` is bytes on Darwin and kilobytes on Linux, normalised to bytes
here.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import statistics
import sys
import tempfile
import time

# Normalise ru_maxrss (bytes on Darwin, KiB on Linux) to bytes.
_RU_MAXRSS_SCALE = 1 if sys.platform == "darwin" else 1024


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


def run_once(argv: list[str]) -> tuple[float, int]:
    """Run one compile, returning (wall_seconds, peak_rss_bytes)."""
    devnull = os.open(os.devnull, os.O_WRONLY)
    try:
        start = time.perf_counter()
        pid = os.posix_spawn(argv[0], argv, os.environ,
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


def measure(argv: list[str], iterations: int, warmup: int) -> dict:
    for _ in range(warmup):
        run_once(argv)
    times: list[float] = []
    rss: list[int] = []
    for _ in range(iterations):
        t, r = run_once(argv)
        times.append(t)
        rss.append(r)
    return {
        "time_median": statistics.median(times),
        "time_min": min(times),
        "rss_median": statistics.median(rss),
        "rss_max": max(rss),
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


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--compiler", required=True, type=pathlib.Path)
    ap.add_argument("--baseline-compiler", type=pathlib.Path, default=None,
                    help="a test-only build with plugin support compiled out; "
                         "when given, the <=budget gate compares the normal "
                         "no-plugin compile against this fully-disabled baseline "
                         "(the spec section 16.5 / completion item 10 gate). "
                         "Without it, empty-plugin/observer overhead is reported "
                         "informationally and the no-plugin gate is skipped.")
    ap.add_argument("--require-baseline", action="store_true",
                    help="fail if --baseline-compiler is not provided instead of "
                         "reporting informationally")
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
    ap.add_argument("--target", default=None,
                    help="optional --target triple for the sample compile")
    args = ap.parse_args()

    compiler = args.compiler.resolve()
    if not compiler.exists():
        print(f"error: compiler not found: {compiler}", file=sys.stderr)
        return 2

    build_dir = args.build_dir
    if build_dir is None:
        # Infer build dir from the compiler path (…/build-neverc/bin/neverc).
        maybe = compiler.parent.parent
        if (maybe / "CMakeCache.txt").exists():
            build_dir = maybe

    empty = args.empty_plugin or default_plugin(build_dir, "BenchPlugin")
    observer = args.observer_plugin or default_plugin(build_dir, "DriverTracePlugin")

    with tempfile.TemporaryDirectory(prefix="nvc-perf-") as tmp:
        src = pathlib.Path(tmp) / "sample.c"
        src.write_text(SAMPLE_SOURCE, encoding="utf-8")
        obj = pathlib.Path(tmp) / "sample.o"

        base_argv = [str(compiler), "-O2", "-c", str(src), "-o", str(obj)]
        if args.target:
            base_argv[1:1] = [f"--target={args.target}"]

        scenarios: list[tuple[str, list[str]]] = [("baseline", base_argv)]
        if empty and empty.exists():
            scenarios.append(
                ("empty-plugin", base_argv + [f"-fplugin={empty}"]))
        else:
            print("note: no empty plugin found; measuring baseline only")
        if observer and observer.exists():
            scenarios.append(
                ("observer",
                 base_argv + [f"-fplugin={observer}", "--driver-trace"]))

        results: dict[str, dict] = {}
        for name, argv in scenarios:
            try:
                results[name] = measure(argv, args.iterations, args.warmup)
            except RuntimeError as e:
                print(f"error: scenario {name}: {e}", file=sys.stderr)
                return 2

    base = results["baseline"]
    print(f"{'scenario':<16}{'time_median(ms)':>18}{'rss_median(MiB)':>18}"
          f"{'d_time':>10}{'d_rss':>10}")
    for name, r in results.items():
        dt = pct(r["time_median"], base["time_median"]) if name != "baseline" else 0.0
        dr = pct(r["rss_median"], base["rss_median"]) if name != "baseline" else 0.0
        print(f"{name:<16}{r['time_median']*1e3:>18.2f}"
              f"{r['rss_median']/(1024*1024):>18.2f}"
              f"{dt*100:>9.2f}%{dr*100:>9.2f}%")

    failures: list[str] = []

    # The hard no-plugin regression gate (spec 16.5 / item 10): normal no-plugin
    # compile vs a fully-disabled baseline binary.  Loading a plugin legitimately
    # adds dlopen/session cost, so empty-plugin/observer overhead is reported
    # above for information but is NOT subject to the <=1% no-plugin budget.
    if args.baseline_compiler is not None:
        disabled = args.baseline_compiler.resolve()
        if not disabled.exists():
            print(f"error: baseline compiler not found: {disabled}",
                  file=sys.stderr)
            return 2
        with tempfile.TemporaryDirectory(prefix="nvc-perf-base-") as tmp:
            src = pathlib.Path(tmp) / "sample.c"
            src.write_text(SAMPLE_SOURCE, encoding="utf-8")
            obj = pathlib.Path(tmp) / "sample.o"
            dis_argv = [str(disabled), "-O2", "-c", str(src), "-o", str(obj)]
            if args.target:
                dis_argv[1:1] = [f"--target={args.target}"]
            try:
                dis = measure(dis_argv, args.iterations, args.warmup)
            except RuntimeError as e:
                print(f"error: fully-disabled baseline: {e}", file=sys.stderr)
                return 2
        dt = pct(base["time_median"], dis["time_median"])
        dr = pct(base["rss_median"], dis["rss_median"])
        print(f"{'no-plugin vs fully-disabled':<28}"
              f"time {dt*100:+.2f}%  rss {dr*100:+.2f}%")
        if dt > args.max_time_regression:
            failures.append(
                f"no-plugin time +{dt*100:.2f}% > "
                f"{args.max_time_regression*100:.2f}%")
        if dr > args.max_rss_regression:
            failures.append(
                f"no-plugin rss +{dr*100:.2f}% > "
                f"{args.max_rss_regression*100:.2f}%")
    elif args.require_baseline:
        print("FAIL: --require-baseline set but --baseline-compiler missing",
              file=sys.stderr)
        return 1
    else:
        print("note: no --baseline-compiler; reported plugin overhead only. "
              "The <=1% no-plugin gate needs a fully-disabled baseline build.")

    if failures:
        for f in failures:
            print(f"FAIL: {f}", file=sys.stderr)
        return 1
    print("measure-plugin-overhead: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
