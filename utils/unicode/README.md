# Unicode table generation

NeverC's private classification and simple-case tables are generated from the
official Go `unicode` package. The generator is pinned to Go 1.27.0 commit
`8af21751f066eced273ca3ce49506b366847c623`, whose `src/unicode/tables.go`
declares Unicode 17.0.0. It verifies the source SHA-256 before parsing, so a
moved tag, truncated download, or upstream format change cannot silently alter
the checked-in C data.

Regenerate from the official Go repository:

```sh
python3 utils/unicode/gen-unicode-tables.py
```

For an offline/reviewed input, download that exact `tables.go` separately and
pass it explicitly. The same pinned SHA-256 is still required:

```sh
python3 utils/unicode/gen-unicode-tables.py \
  --input /path/to/go1.27.0/src/unicode/tables.go
```

To compare the checked-in header without replacing it:

```sh
python3 utils/unicode/gen-unicode-tables.py --check
```

The generated file is written with deterministic UTF-8/LF bytes on every host.
Its ranges are semantic equivalents of Go's tables, normalized into sorted,
disjoint, stride-one intervals for NeverC's lookup code. Go's
`UpperLower` case ranges are expanded according to the documented alternating
upper/lower rule, and only `caseOrbit` entries not expressible by the ordinary
case-map fallback are retained.
