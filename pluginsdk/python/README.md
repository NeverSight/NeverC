# NeverC Python Plugin SDK

This package is the pure-Python authoring layer for NeverC's optional embedded
Python plugin bridge. Install it in the Python environment used by a
Python-enabled NeverC build, or use the copy staged beside the compiler in the
NeverC Plugin SDK.

```sh
python3 -m pip install ./pluginsdk/python
```

The native `_neverc_plugin` module is supplied by NeverC while a plugin is
loading. Importing `neverc_plugin` in an editor, documentation tool, or test
process does not require that native module; operations that need a live host
raise a clear runtime error.

See `docs/plugin-api/python.md` in the NeverC source tree for the supported v1
API and examples.
