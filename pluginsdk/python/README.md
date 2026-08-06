# NeverC Python Plugin SDK

This package is the pure-Python authoring layer for NeverC's embedded Python
plugin bridge. It contains generated bindings for the complete public C plugin
ABI: every official interface table, record layout, function slot, constant,
and callback family. Install it in the Python environment used for authoring,
or use the copy staged beside the compiler in the NeverC Plugin SDK.

```sh
python3 -m pip install ./pluginsdk/python
```

The native `_neverc_plugin` module is supplied by NeverC while a plugin is
loading. Importing `neverc_plugin` in an editor, documentation tool, or test
process does not require that native module; operations that need a live host
raise a clear runtime error. Use `context.ffi`, `neverc_plugin.abi`, and the
modules under `neverc_plugin.domains` for raw parity access. Callback
descriptors are connected through `neverc_plugin.ffi.bind_callbacks`, which
adds checked lifetimes, native trampolines, GIL handling, and structured
exception propagation.

See `docs/plugin-api/python.md` for the full contract. A complete Python-only
classic OLLVM example implementing SUB, BCF, and FLA is in
`examples/ollvm/`.
