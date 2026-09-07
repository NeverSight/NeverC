# C++ project merge and emission contract

Status: accepted implementation contract for `cpp-project-v1`. The separate
project API preserves the accepted `cpp-core-v1` protocol.
The governing requirements are the [issue 16 roadmap](https://github.com/NeverSight/NeverC/issues/16),
especially command/artifact handling, frontend/lowering, and P3A acceptance.
Compilation database selection and option validation belong to the companion
compilation-context workstream.

## Delivery shape and boundaries

Run the current NeverC executable’s built-in frontend mode separately for every
explicitly selected translation unit, preserving its selected compilation directory and validated arguments.
Merge resolved semantic data after those independent source analyses succeed.
Emit one `translated.nc` containing the project's definitions and one
`translated.h` containing its shared type and external declaration interface.
The output directory also contains `translate.map.json` and
`translate-manifest.json`.

This meets the roadmap's generated sources/headers contract: it does not require
one output file per original translation unit. The compilation recipe builds the
combined source once. Do not combine C++ source text or reparse the original
project as a single translation unit: that would change preprocessing, name
lookup and internal linkage before semantic analysis.

The first project profile retains the core source-feature allowlist. It adds
owned headers, supported declarations across selected units, explicit build
context and project linkage. Standard-library boundaries and math mappings
remain disabled until `cpp-math-v1`. The profile uses `--out-dir` or `--check`;
the core single-file `-o` form remains unchanged. An omitted project `--target`
resolves to the validated native target; explicit targets require the same
capability checks and cannot conflict with source options.

Combined output preserves the declared computation and supported scalar C ABI
exports. It does not preserve original C++ object-file boundaries, mangled ABI,
archive-member extraction, class ABI, weak/interposable symbol behavior or
arbitrary original linker recipes. Unsupported linkage attributes remain
source diagnostics. The original program's `main` is retained only when
present; a selected library remains a library.

## Internal frontend request and response extension

The request keeps protocol major 1 and the existing `root`, `source`, `target`
and `arguments` fields. A project request adds:

```json
{
  "protocol": 1,
  "profile": "cpp-project-v1",
  "translation_unit": "src/a.cpp",
  "configuration_id": "<normalized selected-context SHA-256>",
  "working_directory": "<absolute selected compilation directory>"
}
```

The absolute root/source/working-directory strings are process context. They
never become semantic identifiers or deterministic artifact paths. The driver
matches each response to the expected source, configuration, target and frontend
build; worker completion order has no effect on merging.

The response keeps the existing typed `records`, `globals` and `functions`
arrays, containing definitions only. Their core expression and instruction
shapes do not change. It adds this profile-specific envelope:

```json
{
  "protocol": 1,
  "profile": "cpp-project-v1",
  "project_schema": 1,
  "translation_unit": "src/a.cpp",
  "configuration_id": "<64 lowercase hex digits>",
  "function_declarations": [
    {
      "name": "nct_example_function_id",
      "semantic_id": "<64 lowercase hex digits>",
      "result": "int",
      "params": [
        {"name": "nct_parameter_id", "type": "int",
         "loc": {"file": "include/project.h", "line": 5, "column": 15}}
      ],
      "internal": false,
      "c_export": false,
      "inline": false,
      "loc": {"file": "include/project.h", "line": 5, "column": 1}
    }
  ],
  "global_declarations": [
    {
      "name": "nct_example_constant_id",
      "semantic_id": "<64 lowercase hex digits>",
      "type": "int",
      "internal": false,
      "loc": {"file": "include/project.h", "line": 6, "column": 1}
    }
  ],
  "odr": [
    {
      "kind": "function",
      "name": "nct_example_function_id",
      "semantic_id": "<64 lowercase hex digits>",
      "owner_tu": "",
      "inline": false,
      "origin": {"file": "src/a.cpp", "line": 8, "column": 1},
      "tokens_sha256": "<expanded definition-token digest>",
      "bindings_sha256": "<resolved name-binding digest>"
    }
  ]
}
```

`kind` is exactly `record`, `function` or `global`. Every definition has exactly
one matching ODR entry. `semantic_id` is the full lowercase SHA-256 of the
normalized semantic identity, rather than raw Clang USR text; evidence and
corresponding declarations must match. Declaration arrays include the signatures of defined
entities as well as declaration-only entities; they are not exports. Parameter
names do not determine declaration compatibility. The merger compares result
and ordered parameter types, language linkage, internal/external linkage and
inline status. A declaration-only entry never acquires an empty or placeholder
function body.

Each unit records the exact bytes consumed for its source and every owned
header in the existing `dependencies` array. All dependency and location paths
are normalized relative to the declared project root. Hash header buffers
actually parsed, rather than rereading changed files afterward. A shared path
with different consumed byte hashes across frontend jobs fails the invocation.
Different supported macro contexts are recorded separately; they cannot erase
ODR conflicts caused by different expanded definitions.

Core responses omit these project fields and continue through the existing
strict parser/verifier. Project responses use a separate reader and require
`project_schema: 1`. No project capability is inferred from an include, an extra
JSON field or a source argument.

## Entity identities and linkage

Separate semantic identity, emitted name, declaration origin and owning
translation unit:

- External namespace-scope entities use canonical Clang identity with normalized
  file references. Overload identity includes resolved parameter types; return
  type and full signature are still checked independently for conflicts.
- Internal entities, including functions/constants/types in a shared header,
  include the owning translation unit's normalized source identity. Two units
  including the same `static` header function retain two distinct entities.
- Record fields derive their identity from the owning record identity and the
  corresponding field declaration. Do not salt fields of a shared external
  record by the including translation unit merely because a field has no
  independent language linkage.
- Local scalar storage and labels remain function-local. Emitted names cannot
  collide with merged module declarations. All references use typed traversal
  when a name must be rewritten; never replace names in emitted source text.
- C export names retain source spelling and are checked project-wide. Distinct
  semantic identities cannot both claim the same preserved C name. Internal
  functions retain `static` emission even if their language linkage is C.

`owner_tu` is empty for external entities and contains the selected relative
translation unit for private entities. Declaration origins identify the actual
source/header location. A configuration digest is evidence of the selected
build context, not a compilation-database array index or substitute for source
identity. Reject selection of the same source under multiple configurations in
one project until that use case has an explicit identity/linkage contract.

Corresponding local types inside repeated external inline definitions need
enclosing-entity identity, rather than independent TU-private identities. The
first implementation must either handle that correspondence explicitly or
reject this repeated-definition case; it must not structurally merge unrelated
local types. Single-definition local aggregates retain the core behavior.

## ODR checks and definition closure

Equal lowered behavior alone is insufficient to establish C++ ODR consistency:
repeated definitions also require matching definition tokens and consistent
name lookup. The C++17 rules permit a narrow exception for qualifying internal
constants with the same type/value that are not ODR-used. These are source
requirements, independently of the generated C compiler's acceptance.
[C++17 one-definition rules](https://timsong-cpp.github.io/cppwp/n4659/basic.def.odr).

The bounded merger applies the following rules:

1. Validate every unit's complete owned declarations, definitions, references
   and locations before merging. Calls may resolve to explicitly recorded
   external declarations during this stage; internal calls must resolve within
   their owning unit. Inline functions used by a unit require a definition in
   that unit. Unsupported owned code is never hidden by treating its header as
   an approved library boundary.
2. Build one entity table. Reject disagreement in declaration kind, semantic
   identity, linkage, complete signature or record identity. Matching printed
   names or matching record layouts are not grounds for merging distinct types.
3. A strong external function or global has exactly one definition. Duplicate
   strong definitions fail even if token/body hashes match. TU-private
   definitions are retained separately under their distinct identities.
4. Repeated external records and eligible inline definitions are admitted
   conservatively when they share a normalized owned-header origin, matching
   expanded-token digest, matching resolved-binding digest and matching typed
   shape/body. Definitions written independently at different origins are an
   explicit initial restriction, rather than a claim to prove arbitrary ODR
   equivalence. An external inline definition referring to TU-private state or
   functions is rejected when equivalent bindings cannot be established.
5. Token digests preserve preprocessing-token kinds/spellings and order, while
   ignoring whitespace/comments and source coordinates. Binding digests cover
   resolved identities before lowering discards unused expressions or folds
   constants. The frontend may normalize a proven non-ODR-used internal constant by
   its canonical type and folded value; otherwise different private bindings
   differ. The frontend must not erase a binding merely because its final IR has
   no effect.
6. The consumer computes canonical typed body/shape comparisons, excluding
   locations and consistently renaming function-local temporaries/labels.
   Entity references retain semantic identity. This is an additional check,
   not a replacement for token/binding evidence. Keep the complete ordered
   origin list when choosing a deterministic representative definition.
7. Finish with definition closure over all selected function bodies and global
   uses. Every emitted call/global reference must resolve to a selected
   definition or an explicitly validated mapping; P3A has no mappings. For the
   initial conservative profile, require closure for every admitted owned
   free-function/global declaration, including unused declarations. A later
   relaxation for unused declarations requires documented export rules.
8. Verify the merged module again with one target/data model and no unresolved
   declarations. Retain all selected definitions, including unused ones. Do
   not invent `main`, default results or source-runtime dependencies.

Expose conflicting original locations together in diagnostics. Existing stable
codes suffice: malformed evidence uses `TR0103`, conflicting typed/ODR data
uses `TR0301`, missing definitions use `TR0203`, unsupported bounded cases use
`TR0201`, and target disagreements use `TR0204`.

## Minimal shared C++ API

Use separate project containers, preserving the core API's closed-module
invariant. The implemented interface is declared in [neverc/lib/Translate/ProjectIR.h](../../../neverc/lib/Translate/ProjectIR.h):

```cpp
struct FunctionDeclaration {
  std::string Name, SemanticID;
  Type Result;
  std::vector<Variable> Params;
  bool Internal = false, CExport = false, Inline = false;
  SourceLocation Loc;
};
struct GlobalDeclaration {
  std::string Name, SemanticID;
  Type ValueType;
  bool Internal = true;
  SourceLocation Loc;
};
enum class EntityKind { Record, Function, Global };
struct ODREvidence {
  EntityKind Kind;
  std::string Name, SemanticID, OwnerTU;
  bool Inline = false;
  SourceLocation Origin;
  std::string TokensSHA256, BindingsSHA256;
};
struct ProjectUnit {
  Module Definitions;
  std::string TranslationUnit, ConfigurationID;
  std::vector<FunctionDeclaration> FunctionDeclarations;
  std::vector<GlobalDeclaration> GlobalDeclarations;
  std::vector<ODREvidence> ODR;
};
struct ProjectEntity {
  ODREvidence Representative;
  std::vector<SourceLocation> Origins;
  std::vector<std::string> TranslationUnits;
};
struct ProjectUnitIdentity {
  std::string TranslationUnit, ConfigurationID;
};
struct MergedProject {
  Module Definitions;
  std::vector<ProjectUnitIdentity> Units;
  std::vector<ProjectEntity> Entities;
};
struct EmittedFile {
  std::string RelativePath, Text;
  std::vector<SourceMapEntry> Map;
};
struct EmittedProject { std::vector<EmittedFile> Files; };

bool parseProjectUnit(llvm::StringRef JSON, ProjectUnit &, Diagnostics &);
bool verifyProjectUnit(const ProjectUnit &, const VerificationContext &,
                       Diagnostics &);
bool mergeProjectUnits(llvm::ArrayRef<ProjectUnit>, const VerificationContext &,
                       MergedProject &, Diagnostics &);
bool verifyMergedProject(const MergedProject &, const VerificationContext &,
                         Diagnostics &);
bool emitProject(const MergedProject &, const VerificationContext &,
                 EmittedProject &, Diagnostics &);
```

The driver matches `TranslationUnit`/`ConfigurationID` to the corresponding
selected context before acceptance. The merger owns cross-unit semantic checks;
the compilation-context parser does not infer definitions or header closure.
The emitter verifies its merged input defensively and returns only in-memory
files. The driver retains ownership of hashing, staging, compiler processes,
reports and publication. Outputs are published only on success; existing paths are never overwritten.

Linkage for project globals comes from their validated declaration/evidence
table; the existing core `Global` shape remains unchanged. The final merged
`Module.Exports` is derived from canonical definitions, never concatenated from
unverified per-unit export metadata. Shared verification internals can be reused
without permitting declaration-only functions through the core public API.

Apply aggregate limits to all selected units before and after deduplication.
Do not multiply the acceptable node/memory budget by allowing an unlimited
number of individually bounded responses. Structural-depth, identifier, path,
hash and total-generated-source checks still apply.

## Header, source maps and compilation recipe

`translated.h` has a deterministic include guard and target/data-model checks.
It contains shared record definitions in dependency order and external function
and constant declarations. Constant definitions belong in `translated.nc`:
external constants have one non-static definition; private constants remain
static. Internal function prototypes and all private definitions remain in the
source, avoiding header-local static declarations without corresponding bodies
in consumer translation units.

The public interface includes every external function and constant declaration.
Shared records and the transitive record dependencies of those declarations
are emitted in the header; private records used only by private definitions
remain in the source. No aggregate type is exposed as a preserved foreign C++
ABI. Scalar C-export library fixtures must compile a separate client using the
generated header.

`translated.nc` includes `translated.h`, emits private type declarations in
dependency order, private prototypes and globals, then all canonical function
definitions. Names remain distinct even though their original translation
units now share one generated source file. Emitter-private scalar conversion
helpers remain private and require no source-language runtime.

Each emitted file carries its own existing line-range/source-location entries.
The driver serializes a map keyed by generated-relative file path and exact
generated-byte SHA-256. Header maps point to original owned-header locations;
combined-source maps retain every original source identity. Deduplicated
definitions use the canonical origin for emitted lines and retain all contributing
origins in project metadata. Neither source locations nor origin sets depend on
frontend completion order.

Both normal mode and check mode stage all generated files together, run syntax
and object validation on `translated.nc` with the recorded target, and then
follow the existing publication contract. Check mode discards the files. The
reproducible recipe compiles `translated.nc` once; a program links that result,
while a library client links it with the client object. No compilation-database
linker command or undeclared flag is copied into the recipe.

Project manifests add the ordered selected-unit/configuration identities,
normalized source contexts, source/header and control-input provenance, canonical
entity origins, linkage and deduplication decisions. Source/header hashes are
over exact consumed bytes. The selected design also retains raw compilation
database/response-file digests alongside normalized context hashes: relocating
unchanged relative-path control files preserves the full manifest; regenerating
absolute-path control files changes their raw provenance hashes intentionally.
In the latter case semantic IDs, normalized context and generated source/header
bytes remain stable, while whole-manifest byte identity is not claimed for
changed declared control inputs.

## Required validation before accepting the profile

- Positive multi-unit program: shared owned record/header, external calls in both
  directions, overloads, header constants and distinct TU-private helpers with
  identical source spellings. Include a matching inline header definition and
  verify one canonical external emission plus independent private definitions.
- Positive library: translate selected units without `main`, compile a separate
  client against `translated.h`, and compare full values with the separately
  compiled original C++ library at `-O0`/`-O2` using fixed boundary inputs and
  logged seeds.
- Negative merge cases: missing definitions, conflicting return/parameter/record
  types, strong duplicates, mismatched inline tokens or resolved bindings,
  preserved C-name collisions, missing per-unit inline definitions, inconsistent
  dependency byte snapshots and unsupported repeated local-type cases.
- Identity tests: two TUs include one internal-linkage header helper; shared
  external records keep matching field IDs; distinct same-layout private types
  remain distinct. Reorder jobs/selections, relocate the project and regenerate
  supported contexts to test the stated raw/normalized provenance distinction.
- Artifact tests: header/source collisions, cancellation, syntax/object failure
  and report handling publish no incomplete directory. Validate target guards
  and generated-header use from a separate client.
- Retain the installed built-in frontend/package checks and existing compiler/subcommand
  regressions. Runtime coverage is advertised only for matching runners that
  execute both original and generated fixtures. Passing P3A does not enable
  `<cmath>`, arbitrary SDKs, string/vector support or subsequent languages.
