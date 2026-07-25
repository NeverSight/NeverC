# NeverC plugin fuzzer corpus

Seed inputs and regression cases for the `neverc-plugin-fuzzers` umbrella.
Each fuzzer keeps its corpus in a sibling subdirectory
named after its libFuzzer target, e.g. `dyncode-request/` for
`plugin-dyncode-request-fuzzer`.

Guidelines:

- **Seeds** come from the plugin negative tests, the Merge fuzz regression
  corpus, and any input that previously reproduced a crash.  Prefer small,
  human-meaningful inputs so a minimized crash stays readable.
- **Regression cases** are committed after a crash is fixed so the smoke test
  (`ctest -R PluginFuzzSmoke`) keeps exercising the exact byte sequence.
- Hostile bytes only ever drive the host parser/builder/verifier.  No native
  plugin code is generated from fuzz input, so corpus files are inert data.
- Crash artifacts produced by CI are uploaded and minimized; do not commit raw,
  unminimized `crash-*` files here.

The CTest smoke targets run each fuzzer for a fixed, small `-runs` budget on
every touched PR; the nightly `plugin-fuzz` workflow runs longer soak sessions
and feeds new coverage back into these directories.
