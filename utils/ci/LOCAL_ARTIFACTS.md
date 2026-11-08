# Local validation artifacts

Use `local-artifacts.py` for disposable local validation work on macOS/Linux.
The store owns only `build-neverc/local-artifacts`; it never scans or removes
existing build profiles, source packets, dirty files, or canonical SDKs.

For exact-commit Python documentation and registration checks:

```sh
python3 -B utils/ci/local-artifacts.py docs --repo . --commit COMMIT
```

This reads Git tree metadata and expands the checkers' declared inputs, public
plugin headers, SDK CMake declarations, documentation and linked/cited files.
Every expanded blob is checked against its Git object ID. It runs the original
three Python checkers, records their real exit codes and complete logs, then
removes the fixture. LLVM sources and compiler SDK trees are not cloned for
these checks. A partial clone can opt into `--fetch-missing`; this fetches only
missing selected blob IDs without updating refs or `FETCH_HEAD`.

For other probes and temporary build profiles, allocate work inside the attempt:

```sh
python3 -B utils/ci/local-artifacts.py run --purpose packet-analysis \
  --retain result.json -- python3 analysis.py --output '{work}/result.json'
python3 -B utils/ci/local-artifacts.py run --purpose temporary-configure \
  -- cmake -S llvm -B '{work}/build' -G Ninja -C neverc/cmake/caches/NeverC.cmake
```

`{work}` and `NEVERC_ARTIFACT_WORK` name the same owned scratch directory. Keep
reusable primary builds outside it. The supervisor waits for its command,
preserves its actual status, and terminates its owned process group before
retiring scratch. Failure, timeout and handled interruption use the same
cleanup path. SIGKILL or a host restart is recovered on the next invocation.
Kernel leases preserve active readers, including a child inheriting a lease;
an active reader is never guessed from a directory name or PID age.
Orphan recovery records an explicitly incomplete receipt and the final 64 KiB
of an interrupted console log, with the original log's full SHA-256 and length.
This diagnostic is subject to the same bounded retention as other closed work.

Reports are SHA-256 verified, deduplicated in `objects/`, and referenced by
immutable receipts in `receipts/`. The default retention is 16 receipts and
256 MiB total unpinned evidence. Old closed receipts and unreferenced objects
are evicted together. Raw Git source inputs are represented by commit/tree/
blob IDs rather than another source archive. Use stable purpose names.

Asynchronous consumers must explicitly register and release their dependency:

```sh
python3 -B utils/ci/local-artifacts.py pin --owner formal-qualifier --receipt RECEIPT.json
python3 -B utils/ci/local-artifacts.py unpin --owner formal-qualifier
```

Python callers can use `ArtifactStore.attempt`, `reader`, `pin`, `unpin` and
`publish(..., pending_owner=...)`. The latter publishes and pins atomically;
`receipt(identity)` verifies the receipt and every referenced object before
consumption. A pending run pin can be handed to a live `reader` before releasing
the pin. Pending readers may exceed the cache budget until explicitly released.
Do not pin already finished work merely because its historical packet exists.

Managed CPython staging has its own fixed, locked transaction slot in each
build profile. Successful publication removes staging immediately. A fatal
configure error or uncatchable interruption can leave one owned transaction;
the next configure recovers it before SDK reuse checks. If interrupted between
the two publication renames, recovery restores the previous installation.
The canonical input, installed-SDK fingerprint checks, and ABI probes remain
unchanged. Temporary profiles under `{work}` are also retired by the supervisor
when configure fails. Independent, deliberately retained build profiles each
retain their own installed runtime for relocation and packaging.

Do not invoke archived whole-repository snapshot generators for new checks,
copy the entire packet directory to analyze one report, or create another
timestamped build/SDK directory on each retry. Historical pinned validators
remain immutable; new work uses these entrypoints and explicit reader leases.

Run the small lifecycle regression suite with:

```sh
python3 -B utils/ci/test-local-artifacts.py -v
```

It uses tiny Git/SDK fixtures and real subprocess termination. It does not
compile C++, download SDKs or create full repository copies.
