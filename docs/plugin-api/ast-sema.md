# AST, parser, and semantic plugin APIs

`PluginAST.h` and `PluginSema.h` provide task-scoped, pure-C access to the
frontend tree and semantic pipeline. Stable node, property, and child-slot IDs
are generated from NeverC's concrete AST definitions; plugins never receive a
C++ `Decl`, `Stmt`, `Type`, or `Sema` pointer.

## Reading and building AST nodes

Use `NevercASTAPI` to query node information, schema properties, children,
parents, declaration contexts, types, attributes, and common concrete node
details. Batch APIs require explicit element count, capacity, and stride.

`NevercASTBuilder` constructs only schema-declared node kinds. Required
properties and child slots are verified at commit. A successful commit
publishes a task-owned node; a failed commit leaves no partially visible node.
Destroy every builder after commit or failure.

## Atomic mutation

AST changes use `BeginASTMutation`, staged operations, and
`CommitASTMutation`. The host validates ownership, slot compatibility,
cardinality, parent links, cycles, and semantic invariants before changing the
tree. `AbortASTMutation` discards all staged operations. Native
`TreeMutationListener` notifications are sent only after a successful commit.

The buildable
[`ASTRewritePlugin.c`](../../pluginsdk/examples/ASTRewritePlugin.c) shows a
parser interceptor that calls the built-in parser, constructs an integer
literal, and atomically replaces a variable initializer.

## Parser and Sema replacement

`neverc.syntax.parse` maps a verified token stream to an `ASTUnit`.
`neverc.sema.analyze` maps an AST product to a `SemanticUnit`. Both phases have
typed interceptors and Providers. Fine-grained declaration, statement,
expression, type-name, attribute, lookup, conversion, and keyword extension
phases remain available when replacing only part of the frontend.

The built-in fused parser/Sema path publishes the same artifact contracts as a
replacement. Semantic replay accepts only node kinds for which NeverC can
reconstruct scope, lookup, redeclaration, and type-checking state. Encountering
an unsupported concrete kind returns `NEVERC_STATUS_UNSUPPORTED_AST_KIND`; it
never marks a partially replayed tree semantically complete.

## Lifecycle and cleanup

AST and Sema lifecycle observers are delivered in source order through the
host `TreeConsumer` bridge. Begin/end events remain paired on syntax errors,
plugin errors, and cancellation. Task handles become invalid only after final
read-only end events and cleanup callbacks have run.

## Verification

```sh
python3 utils/plugin-api/gen-ast-schema.py --check
ctest --test-dir build-neverc \
  -R 'Plugin(AST|Parser|Sema|Frontend)' --output-on-failure
```

With `NEVERC_ENABLE_PLUGIN_FUZZERS=ON`,
`plugin-ast-mutation-fuzzer` covers property decoding, malformed builders,
forged handles, and mutation rollback.
