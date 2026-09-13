# Triage

## Step 0 — Characterize the failure from the CI logs

Answer these from the failing run's logs before touching code. Use `gh`:

```bash
gh run list --workflow <wf>.yml -L 30                 # recent runs: which passed/failed
gh run view <run-id> --log | grep -iE "fail|segmentation|\*\*\*|error" | head -50
gh run view <run-id> --log-failed                     # only failed steps
```

Determine:
- **Which step fails** — building the tool? running the test? a link step? (decides "tool vs program" below)
- **The signal** — assertion? `Segmentation fault (core dumped)` (SIGSEGV)? exit 137 (SIGKILL/OOM)? timeout?
- **Deterministic or flaky** — compare the failed-test list across several failed runs. If *different*
  tests fail each run, the bug is **non-deterministic** (race / uninitialized memory / ASLR-dependent
  ordering), not a logic bug in those specific tests.
- **Platform scope** — diff the same commit across platform workflows (linux-x64 vs linux-arm64 vs
  macos vs windows). "x64 fails, arm64 passes" narrows it enormously.
- **First bad commit** — scan history for the last green run; correlate with what changed.

```bash
# Did the SAME tests fail, or different ones each run? (flaky signal)
for id in <id1> <id2> <id3>; do echo "== $id =="; \
  gh run view $id --log | grep -oE "Test #[0-9]+: [A-Za-z._]+ .*Failed"; done
```

## Step 3 — Is it the TOOL crashing or the PROGRAM it produced?

A "compile failed with segfault" can be either the compiler/linker crashing OR the produced binary
crashing when run. They need different fixes. Tells:

- **LLVM/Clang-based tools** install a crash handler (`InitLLVM` / `PrintStackTraceOnErrorSignal`).
  If the tool itself crashes you'll usually see `PLEASE submit a bug report ... Stack dump:`. Its
  **absence** (just bare `Segmentation fault (core dumped)`) suggests the *built program* crashed.
- The test harness often runs `tool ... && ./built_program`. Reproduce the two halves separately:
  compile to an artifact, then run the artifact, and see which one faults.
- Beware shell/`popen` output routing: `sh -c "prog 2>err"` may write the shell's
  "Segmentation fault" message to a different fd than the program's captured stderr. Don't over-read
  which stream the message landed in; verify with explicit exit codes (`echo $?`; signal = `128+N`).
