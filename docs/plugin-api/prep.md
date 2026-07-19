# Preprocessor plugin API

`PluginPrep.h` exposes stable token, identifier, macro, pragma, and token-stream
schemas without leaking NeverC or LLVM C++ types. The generated schema in
`Schema/PluginPrepSchema.inc` is the source of truth for stable numeric kinds,
categories, spellings, and constructibility.

## Extension levels

Plugins can participate at three levels:

- read-only preprocessor events for include, macro expansion, conditional,
  pragma, and file transitions;
- typed interceptors for token, include, macro, pragma, and feature-query
  phases;
- a complete `neverc.prep.build_token_stream` Provider that publishes a
  verified `TokenStream`.

The token phase supports bounded replacement, deletion, and expansion. The
host enforces the expansion budget and verifies spelling, location, flags,
EOF placement, and token ownership before publishing a replacement.

## Token builders

Create synthesized tokens with `CreateTokenBuilder`, set exactly one token
payload, assign a valid task-owned location, and call `TokenBuilderCommit`.
Destroy the builder on every path. A committed builder is immutable and a
failed commit does not publish a token.

Token streams are contiguous, immutable task artifacts. A replacement stream
must contain exactly one final EOF token and may not exceed
`NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS`.

## Observer and interceptor rules

Observers receive read-only event data and cannot affect preprocessing.
Interceptors follow the common continuation contract:

- call `InvokeNext` at most once and then return `CONTINUE`; or
- do not call it and publish a verified replacement.

Continuation objects and all preprocessor handles are valid only during their
declared callback/task scope. A plugin-created thread must join before the
callback returns if it touches those values.

## Verification

Run the generated-schema and coverage checks after changing token definitions:

```sh
python3 utils/plugin-api/gen-prep-schema.py --check
python3 utils/plugin-api/check-coverage.py docs/plugin-api/coverage.json
```

With `NEVERC_ENABLE_PLUGIN_FUZZERS=ON`,
`plugin-prep-token-builder-fuzzer` exercises malformed token builders, task
handles, output capacities, and token-stream queries.
