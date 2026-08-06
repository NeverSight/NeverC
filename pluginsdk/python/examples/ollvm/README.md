# Classic OLLVM in Python

`ollvm_plugin.py` is a real NeverC compiler plugin written entirely against the
public Python binding. It queries the generated `IR_CORE`, `IR_BUILDER`, and
`IR_PASS` tables and registers normal native pass descriptors through generated
callback trampolines. There is no private compiler hook or C/C++ transform
behind the example.

It implements the traditional OLLVM family:

- SUB replaces integer add/sub/and/or/xor operations with equivalent instruction
  sequences.
- BCF inserts deterministic conditional gates and bogus blocks without changing
  the selected real edge.
- FLA rewrites supported function CFGs around a state variable and dispatcher
  chain.

```sh
neverc -fplugin=/path/to/ollvm_plugin.py \
  --ollvm-sub --ollvm-bcf --ollvm-fla \
  --ollvm-seed 42 --ollvm-probability 80 --ollvm-iterations 2 \
  input.c -o output
```

Options can be enabled independently. `--ollvm-include` and
`--ollvm-exclude` accept repeatable shell-style function globs. The same seed,
source, target, and option set produce the same transformed IR.

| Option | Meaning | Default |
|---|---|---|
| `--ollvm-sub` | Enable instruction substitution | off |
| `--ollvm-bcf` | Enable bogus control flow | off |
| `--ollvm-fla` | Enable control-flow flattening | off |
| `--ollvm-probability 0..100` | Per-candidate transformation probability | `100` |
| `--ollvm-iterations 0..8` | SUB and BCF rounds | `1` |
| `--ollvm-seed UINT64` | Reproducible seed | `0` |
| `--ollvm-include GLOB` | Include function names; repeatable | `*` |
| `--ollvm-exclude GLOB` | Exclude function names; repeatable | none |

The plugin always excludes `llvm.*` intrinsics and NeverC's `__neverc_*`
runtime functions. It is valid at both `-O0` and optimized levels and emits the
same transformed module for repeated runs with identical inputs. NeverC's pass
transaction verifier validates every committed mutation.

The example deliberately skips CFGs with PHI nodes, exceptional terminators,
or entry backedges when a transformation cannot preserve them through the
generic stable IR API. SUB also skips arithmetic carrying `nuw`/`nsw`, so it
does not weaken LLVM poison semantics.

This is an SDK demonstration, not a claim that classic OLLVM provides strong
protection against a determined reverse engineer. Audit and tune the passes for
your threat model before using them in production.
