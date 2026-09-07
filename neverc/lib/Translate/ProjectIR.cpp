#include "ProjectIR.h"
#include "TranslateIRInternal.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace neverc::translate {
namespace {
bool fail(Diagnostics &D, const SourceLocation &L, llvm::StringRef Reason,
          llvm::StringRef Code = "TR0301") {
  D.push_back({Code.str(), L, "project translation IR", Reason.str(),
               "Use complete, compatible owned project definitions and a "
               "supported compilation context."});
  return false;
}
bool digest(llvm::StringRef S) {
  return S.size() == 64 && std::all_of(S.begin(), S.end(), [](char C) {
           return (C >= '0' && C <= '9') || (C >= 'a' && C <= 'f');
         });
}
bool relativePath(llvm::StringRef P) {
  if (P.empty() || P.starts_with("/") || P.ends_with("/") || P.contains('\\') ||
      P.contains(':') || P.contains('\0') || P.contains('\n') ||
      P.contains('\r'))
    return false;
  while (!P.empty()) {
    auto Part = P.split('/');
    if (Part.first.empty() || Part.first == "." || Part.first == "..")
      return false;
    P = Part.second;
  }
  return true;
}
bool sameLocation(const SourceLocation &A, const SourceLocation &B) {
  return std::tie(A.File, A.Line, A.Column) ==
         std::tie(B.File, B.Line, B.Column);
}
std::string at(const SourceLocation &L) {
  return L.File + ":" + std::to_string(L.Line) + ":" + std::to_string(L.Column);
}
Function signature(const FunctionDeclaration &F) {
  Function R;
  R.Name = F.Name;
  R.Result = F.Result;
  R.Params = F.Params;
  R.Internal = F.Internal;
  R.CExport = F.CExport;
  R.Loc = F.Loc;
  return R;
}
bool sameSignature(const Function &A, const Function &B) {
  if (A.Result != B.Result || A.Internal != B.Internal ||
      A.CExport != B.CExport || A.Params.size() != B.Params.size())
    return false;
  for (size_t I = 0; I < A.Params.size(); ++I)
    if (A.Params[I].ValueType != B.Params[I].ValueType)
      return false;
  return true;
}

class ProjectParser {
  Diagnostics &D;
  SourceLocation Anchor{"<frontend>", 1, 1};

public:
  explicit ProjectParser(Diagnostics &D) : D(D) {}
  bool error(llvm::StringRef S) { return fail(D, Anchor, S, "TR0103"); }
  bool string(const llvm::json::Object &O, llvm::StringRef Key,
              std::string &Out) {
    auto S = O.getString(Key);
    if (!S.data() || S.contains('\0'))
      return error("Missing or invalid string: " + Key.str());
    Out = S.str();
    return true;
  }
  bool boolean(const llvm::json::Object &O, llvm::StringRef Key, bool &Out) {
    int B = O.getBoolean(Key);
    if (B < 0)
      return error("Missing or invalid boolean: " + Key.str());
    Out = B != 0;
    return true;
  }
  bool location(const llvm::json::Object &O, llvm::StringRef Key,
                SourceLocation &L) {
    const auto *V = O.getObject(Key);
    int64_t Line = 0, Column = 0;
    if (!V || !string(*V, "file", L.File) || !V->getInteger("line", Line) ||
        !V->getInteger("column", Column) || Line <= 0 || Line > UINT32_MAX ||
        Column <= 0 || Column > UINT32_MAX)
      return error("Invalid project source location.");
    L.Line = uint32_t(Line);
    L.Column = uint32_t(Column);
    return true;
  }
  bool type(const llvm::json::Object &O, llvm::StringRef Key, Type &T) {
    std::string S;
    if (!string(O, Key, S))
      return false;
    if (S == "int")
      T.Kind = TypeKind::Int;
    else if (S == "uint")
      T.Kind = TypeKind::UInt;
    else if (S == "bool")
      T.Kind = TypeKind::Bool;
    else if (S == "void")
      T.Kind = TypeKind::Void;
    else if (S == "double")
      T.Kind = TypeKind::Double;
    else {
      T.Kind = TypeKind::Record;
      T.RecordID = std::move(S);
    }
    return true;
  }
  template <class T, class F>
  bool array(const llvm::json::Object &O, llvm::StringRef Key,
             std::vector<T> &Out, F Read) {
    const auto *A = O.getArray(Key);
    if (!A || A->size() > MaxProtocolNodes)
      return error("Invalid or oversized project array: " + Key.str());
    for (const auto &V : *A) {
      const auto *J = V.getAsObject();
      if (!J)
        return error("Project array element must be an object.");
      T Item;
      if (!Read(*J, Item))
        return false;
      Out.push_back(std::move(Item));
    }
    return true;
  }
  bool run(const llvm::json::Object &O, ProjectUnit &U) {
    int64_t Schema = 0;
    if (!O.getInteger("project_schema", Schema) || Schema != 1)
      return error("Unsupported or missing project schema version.");
    if (!string(O, "translation_unit", U.TranslationUnit) ||
        !string(O, "configuration_id", U.ConfigurationID))
      return false;
    return array(O, "function_declarations", U.FunctionDeclarations,
                 [&](const auto &V, FunctionDeclaration &F) {
                   return string(V, "name", F.Name) &&
                          string(V, "semantic_id", F.SemanticID) &&
                          type(V, "result", F.Result) &&
                          boolean(V, "internal", F.Internal) &&
                          boolean(V, "c_export", F.CExport) &&
                          boolean(V, "inline", F.Inline) &&
                          location(V, "loc", F.Loc) &&
                          array(V, "params", F.Params,
                                [&](const auto &J, Variable &P) {
                                  return string(J, "name", P.Name) &&
                                         type(J, "type", P.ValueType) &&
                                         location(J, "loc", P.Loc);
                                });
                 }) &&
           array(O, "global_declarations", U.GlobalDeclarations,
                 [&](const auto &V, GlobalDeclaration &G) {
                   return string(V, "name", G.Name) &&
                          string(V, "semantic_id", G.SemanticID) &&
                          type(V, "type", G.ValueType) &&
                          boolean(V, "internal", G.Internal) &&
                          location(V, "loc", G.Loc);
                 }) &&
           array(O, "odr", U.ODR, [&](const auto &V, ODREvidence &E) {
             std::string Kind;
             if (!string(V, "kind", Kind))
               return false;
             if (Kind == "record")
               E.Kind = EntityKind::Record;
             else if (Kind == "function")
               E.Kind = EntityKind::Function;
             else if (Kind == "global")
               E.Kind = EntityKind::Global;
             else
               return error("Unknown ODR entity kind.");
             return string(V, "name", E.Name) &&
                    string(V, "semantic_id", E.SemanticID) &&
                    string(V, "owner_tu", E.OwnerTU) &&
                    boolean(V, "inline", E.Inline) &&
                    location(V, "origin", E.Origin) &&
                    string(V, "tokens_sha256", E.TokensSHA256) &&
                    string(V, "bindings_sha256", E.BindingsSHA256);
           });
  }
};

// The comparison deliberately excludes locations and parameter/local/label
// names. Length prefixes prevent ambiguities when values contain separators.
class Canonical {
  std::string Data;
  std::map<std::string, std::string> Local;
  void add(llvm::StringRef S) {
    Data += std::to_string(S.size()) + ":" + S.str();
  }
  void number(uint64_t N) { add(std::to_string(N)); }
  void name(const std::string &N) {
    auto I = Local.find(N);
    add(I == Local.end() ? N : I->second);
  }
  void type(const Type &T) {
    number(unsigned(T.Kind));
    add(T.RecordID);
  }
  void expr(const Expr &E) {
    number(unsigned(E.Kind));
    type(E.ValueType);
    switch (E.Kind) {
    case ExprKind::Literal:
      if (E.ValueType.Kind == TypeKind::Bool)
        number(E.Boolean);
      else if (E.ValueType.Kind == TypeKind::Double)
        number(E.Binary64Bits);
      else
        add(E.Integer);
      break;
    case ExprKind::Var:
      name(E.Name);
      break;
    case ExprKind::Member:
      add(E.Name);
      break;
    case ExprKind::Unary:
      number(unsigned(E.UnaryOp));
      break;
    case ExprKind::Binary:
      number(unsigned(E.BinaryOp));
      break;
    default:
      break;
    }
    number(E.Args.size());
    for (const auto &A : E.Args)
      expr(A);
  }
  void optional(const std::optional<Expr> &E) {
    number(bool(E));
    if (E)
      expr(*E);
  }

public:
  std::string record(const Record &R) {
    number(R.Fields.size());
    for (const auto &F : R.Fields) {
      add(F.Name);
      type(F.ValueType);
    }
    return std::move(Data);
  }
  std::string global(const Global &G) {
    type(G.ValueType);
    expr(G.Value);
    return std::move(Data);
  }
  std::string function(const Function &F) {
    type(F.Result);
    number(F.Internal);
    number(F.CExport);
    number(F.Params.size());
    size_t N = 0;
    for (const auto &P : F.Params) {
      type(P.ValueType);
      Local[P.Name] = "$p" + std::to_string(N++);
    }
    N = 0;
    number(F.Locals.size());
    for (const auto &L : F.Locals) {
      type(L.ValueType);
      Local[L.Name] = "$v" + std::to_string(N++);
    }
    // Labels occupy a separate C namespace, so canonicalize them separately
    // too.
    std::map<std::string, size_t> Labels;
    for (const auto &I : F.Body)
      if (I.Op == InstructionKind::Label)
        Labels.emplace(I.Label, Labels.size());
    number(F.Body.size());
    for (const auto &I : F.Body) {
      number(unsigned(I.Op));
      optional(I.Target);
      optional(I.Value);
      optional(I.Condition);
      add(I.Callee);
      add(I.MappingID);
      number(I.Args.size());
      for (const auto &A : I.Args)
        expr(A);
      if (I.Op == InstructionKind::Label || I.Op == InstructionKind::Jump)
        number(Labels.at(I.Label));
      if (I.Op == InstructionKind::Branch) {
        number(Labels.at(I.TrueLabel));
        number(Labels.at(I.FalseLabel));
      }
    }
    return std::move(Data);
  }
};

// Bound the whole project before deduplication. Repeated headers still consume
// budget.
struct Budget {
  size_t Nodes = 0, Bytes = 0;
  void text(const std::string &S) { Bytes += S.size(); }
  void location(const SourceLocation &L) { text(L.File); }
  void type(const Type &T) { text(T.RecordID); }
  bool expr(const Expr &E, size_t Depth = 0) {
    if (Depth > MaxProtocolDepth || ++Nodes > MaxProtocolNodes)
      return false;
    text(E.Name);
    text(E.Integer);
    type(E.ValueType);
    location(E.Loc);
    for (const auto &A : E.Args)
      if (!expr(A, Depth + 1))
        return false;
    return valid();
  }
  void variable(const Variable &V) {
    ++Nodes;
    text(V.Name);
    type(V.ValueType);
    location(V.Loc);
  }
  bool valid() const {
    return Nodes <= MaxProtocolNodes && Bytes <= 2 * MaxFrontendResponseBytes;
  }
  bool module(const Module &M) {
    text(M.Frontend.Name);
    text(M.Frontend.Version);
    text(M.Frontend.Build);
    text(M.Target.Triple);
    text(M.FPContractID);
    text(M.SDKDistributionID);
    text(M.SDKCatalogSHA256);
    for (const auto &D : M.SDKDependencies) {
      ++Nodes;
      text(D.Root);
      text(D.Path);
      text(D.SHA256);
    }
    for (const auto &Mapping : M.Mappings) {
      ++Nodes;
      text(Mapping.ID);
      text(Mapping.DeclarationID);
      type(Mapping.Result);
      for (const auto &T : Mapping.Parameters) {
        ++Nodes;
        type(T);
      }
      text(Mapping.Origin.Root);
      text(Mapping.Origin.Path);
      text(Mapping.Origin.SHA256);
    }
    for (const auto &D : M.Dependencies) {
      ++Nodes;
      text(D.Path);
      text(D.SHA256);
    }
    for (const auto &R : M.Records) {
      ++Nodes;
      text(R.ID);
      location(R.Loc);
      for (const auto &F : R.Fields) {
        ++Nodes;
        text(F.Name);
        type(F.ValueType);
      }
    }
    for (const auto &G : M.Globals) {
      ++Nodes;
      text(G.Name);
      type(G.ValueType);
      location(G.Loc);
      if (!expr(G.Value))
        return false;
    }
    for (const auto &F : M.Functions) {
      ++Nodes;
      text(F.Name);
      type(F.Result);
      location(F.Loc);
      for (const auto &P : F.Params)
        variable(P);
      for (const auto &L : F.Locals)
        variable(L);
      for (const auto &I : F.Body) {
        ++Nodes;
        location(I.Loc);
        text(I.Callee);
        text(I.MappingID);
        text(I.Label);
        text(I.TrueLabel);
        text(I.FalseLabel);
        for (const auto *E : {&I.Target, &I.Value, &I.Condition})
          if (*E && !expr(**E))
            return false;
        for (const auto &A : I.Args)
          if (!expr(A))
            return false;
      }
    }
    return valid();
  }
  bool unit(const ProjectUnit &U) {
    text(U.TranslationUnit);
    text(U.ConfigurationID);
    if (!module(U.Definitions))
      return false;
    for (const auto &F : U.FunctionDeclarations) {
      ++Nodes;
      text(F.Name);
      text(F.SemanticID);
      type(F.Result);
      location(F.Loc);
      for (const auto &P : F.Params)
        variable(P);
    }
    for (const auto &G : U.GlobalDeclarations) {
      ++Nodes;
      text(G.Name);
      text(G.SemanticID);
      type(G.ValueType);
      location(G.Loc);
    }
    for (const auto &E : U.ODR) {
      ++Nodes;
      text(E.Name);
      text(E.SemanticID);
      text(E.OwnerTU);
      location(E.Origin);
      text(E.TokensSHA256);
      text(E.BindingsSHA256);
    }
    return valid();
  }
};

bool evidence(const ODREvidence &E, const std::set<std::string> &Paths,
              Diagnostics &D) {
  if (!digest(E.SemanticID) || !digest(E.TokensSHA256) ||
      !digest(E.BindingsSHA256) || !E.Origin.Line || !E.Origin.Column ||
      !Paths.count(E.Origin.File) ||
      (!E.OwnerTU.empty() && !relativePath(E.OwnerTU)))
    return fail(D, E.Origin,
                "Invalid ODR identity, hash, owner, or original location.");
  if (E.Kind != EntityKind::Record && E.Kind != EntityKind::Function &&
      E.Kind != EntityKind::Global)
    return fail(D, E.Origin, "Invalid ODR entity kind.");
  if (E.Kind != EntityKind::Function && E.Inline)
    return fail(D, E.Origin,
                "Only function definitions may have inline ODR evidence.");
  return true;
}
bool declarationLocation(const SourceLocation &L,
                         const std::set<std::string> &Paths, Diagnostics &D) {
  return (L.Line && L.Column && Paths.count(L.File)) ||
         fail(D, L, "Declaration location is not an owned dependency.");
}

bool sortRecords(Module &M, Diagnostics &D) {
  std::map<std::string, Record> Pending;
  for (auto &R : M.Records)
    Pending.emplace(R.ID, std::move(R));
  std::map<std::string, size_t> Remaining;
  std::map<std::string, std::vector<std::string>> Users;
  std::set<std::string> Ready;
  for (const auto &Entry : Pending) {
    std::set<std::string> Dependencies;
    for (const auto &F : Entry.second.Fields)
      if (F.ValueType.Kind == TypeKind::Record) {
        if (!Pending.count(F.ValueType.RecordID))
          return fail(D, Entry.second.Loc, "Unknown merged record dependency.");
        Dependencies.insert(F.ValueType.RecordID);
      }
    Remaining.emplace(Entry.first, Dependencies.size());
    if (Dependencies.empty())
      Ready.insert(Entry.first);
    for (const auto &Dependency : Dependencies)
      Users[Dependency].push_back(Entry.first);
  }
  M.Records.clear();
  while (!Ready.empty()) {
    std::string Name = *Ready.begin();
    Ready.erase(Ready.begin());
    auto I = Pending.find(Name);
    M.Records.push_back(std::move(I->second));
    Pending.erase(I);
    for (const auto &User : Users[Name])
      if (--Remaining.at(User) == 0)
        Ready.insert(User);
  }
  return Pending.empty() || fail(D, Pending.begin()->second.Loc,
                                 "Cyclic merged record dependency.");
}

void exports(Module &M) {
  M.Exports.clear();
  for (const auto &F : M.Functions)
    if (!F.Internal) {
      Export E{F.Name, typeName(F.Result), {}, F.CExport};
      for (const auto &P : F.Params)
        E.Parameters.push_back(typeName(P.ValueType));
      M.Exports.push_back(std::move(E));
    }
}
} // namespace

bool parseProjectUnit(llvm::StringRef JSON, ProjectUnit &Out, Diagnostics &D) {
  ProjectUnit Parsed;
  // The common reader performs size/depth checks before either JSON parse.
  if (!parseModule(JSON, Parsed.Definitions, D))
    return false;
  if (Parsed.Definitions.Profile != "cpp-project-v1" &&
      Parsed.Definitions.Profile != "cpp-math-v1")
    return fail(D, {"<frontend>", 1, 1},
                "Project reader requires an explicit project or math profile.",
                "TR0003");
  auto Value = llvm::json::parse(JSON);
  if (!Value)
    return fail(D, {"<frontend>", 1, 1}, llvm::toString(Value.takeError()),
                "TR0103");
  if (!ProjectParser(D).run(*Value->getAsObject(), Parsed))
    return false;
  Budget B;
  if (!B.unit(Parsed))
    return fail(D, {Parsed.TranslationUnit, 1, 1},
                "Project unit exceeds the IR resource budget.");
  Out = std::move(Parsed);
  return true;
}

bool verifyProjectUnit(const ProjectUnit &U, const VerificationContext &Context,
                       Diagnostics &D) {
  const Module &M = U.Definitions;
  const SourceLocation Anchor{U.TranslationUnit, 1, 1};
  Budget B;
  if (!B.unit(U))
    return fail(D, Anchor, "Project unit exceeds the IR resource budget.");
  std::set<std::string> Paths;
  for (const auto &Dep : M.Dependencies)
    Paths.insert(Dep.Path);
  if (!relativePath(U.TranslationUnit) || !Paths.count(U.TranslationUnit) ||
      !digest(U.ConfigurationID))
    return fail(D, Anchor,
                "Invalid project translation unit or configuration identity.");
  std::map<std::string, const FunctionDeclaration *> FDecl;
  std::map<std::string, const GlobalDeclaration *> GDecl;
  std::map<std::string, std::string> IDs;
  std::vector<Function> Signatures;
  std::vector<Variable> Globals;
  auto Identity = [&](const std::string &ID, const std::string &Name,
                      const SourceLocation &L) {
    auto I = IDs.emplace(ID, Name);
    return (digest(ID) && (I.second || I.first->second == Name)) ||
           fail(D, L, "Semantic identity maps to conflicting emitted names.");
  };
  for (const auto &F : U.FunctionDeclarations) {
    if (!declarationLocation(F.Loc, Paths, D) ||
        !Identity(F.SemanticID, F.Name, F.Loc) ||
        !FDecl.emplace(F.Name, &F).second || GDecl.count(F.Name))
      return fail(D, F.Loc, "Duplicate or invalid function declaration.");
    Signatures.push_back(signature(F));
  }
  for (const auto &G : U.GlobalDeclarations) {
    if (!declarationLocation(G.Loc, Paths, D) ||
        !Identity(G.SemanticID, G.Name, G.Loc) ||
        !GDecl.emplace(G.Name, &G).second || FDecl.count(G.Name))
      return fail(D, G.Loc, "Duplicate or invalid global declaration.");
    Globals.push_back({G.Name, G.ValueType, G.Loc});
  }
  if (!detail::verifyProjectDefinitions(M, Context, Signatures, Globals, D))
    return false;
  std::map<std::string, std::pair<EntityKind, SourceLocation>> Definitions;
  for (const auto &R : M.Records)
    Definitions.emplace(R.ID, std::make_pair(EntityKind::Record, R.Loc));
  for (const auto &F : M.Functions) {
    auto Decl = FDecl.find(F.Name);
    if (Decl == FDecl.end() || !sameSignature(F, signature(*Decl->second)))
      return fail(D, F.Loc,
                  "Function definition lacks a matching complete declaration.");
    Definitions.emplace(F.Name, std::make_pair(EntityKind::Function, F.Loc));
  }
  for (const auto &G : M.Globals) {
    auto Decl = GDecl.find(G.Name);
    if (Decl == GDecl.end() || G.ValueType != Decl->second->ValueType)
      return fail(D, G.Loc,
                  "Global definition lacks a matching complete declaration.");
    Definitions.emplace(G.Name, std::make_pair(EntityKind::Global, G.Loc));
  }
  std::set<std::string> Seen;
  for (const auto &E : U.ODR) {
    auto Def = Definitions.find(E.Name);
    if (!evidence(E, Paths, D) || !Identity(E.SemanticID, E.Name, E.Origin) ||
        !Seen.insert(E.Name).second || Def == Definitions.end() ||
        E.Kind != Def->second.first ||
        !sameLocation(E.Origin, Def->second.second) ||
        (!E.OwnerTU.empty() && E.OwnerTU != U.TranslationUnit))
      return fail(D, E.Origin,
                  "ODR evidence does not match exactly one owned definition.");
    if (E.Kind == EntityKind::Record) {
      if (FDecl.count(E.Name) || GDecl.count(E.Name))
        return fail(D, E.Origin,
                    "Record identity conflicts with a declaration.");
    } else if (E.Kind == EntityKind::Function) {
      const auto &F = *FDecl.at(E.Name);
      if (F.SemanticID != E.SemanticID || F.Internal != !E.OwnerTU.empty() ||
          F.Inline != E.Inline)
        return fail(
            D, E.Origin,
            "Function ODR identity or linkage disagrees with its declaration.");
    } else {
      const auto &G = *GDecl.at(E.Name);
      if (G.SemanticID != E.SemanticID || G.Internal != !E.OwnerTU.empty())
        return fail(
            D, E.Origin,
            "Global ODR identity or linkage disagrees with its declaration.");
    }
  }
  if (Seen.size() != Definitions.size())
    return fail(D, Anchor,
                "Every definition requires exactly one ODR evidence entry.");
  for (const auto &F : U.FunctionDeclarations)
    if (F.Internal && !Definitions.count(F.Name))
      return fail(
          D, F.Loc,
          "Internal function has no definition in its owning translation unit.",
          "TR0203");
  for (const auto &G : U.GlobalDeclarations)
    if (G.Internal && !Definitions.count(G.Name))
      return fail(
          D, G.Loc,
          "Internal global has no definition in its owning translation unit.",
          "TR0203");
  for (const auto &F : M.Functions)
    for (const auto &I : F.Body)
      if (I.Op == InstructionKind::Call && FDecl.at(I.Callee)->Inline &&
          !Definitions.count(I.Callee))
        return fail(D, I.Loc,
                    "An inline function used in this translation unit requires "
                    "its local definition.",
                    "TR0203");
  return true;
}

bool mergeProjectUnits(llvm::ArrayRef<ProjectUnit> Input,
                       const VerificationContext &Context, MergedProject &Out,
                       Diagnostics &D) {
  if (Input.empty() || Input.size() > 4096)
    return fail(D, {"<project>", 1, 1},
                "A project must select between 1 and 4096 translation units.");
  Budget B;
  std::vector<const ProjectUnit *> Units;
  for (const auto &U : Input) {
    if (!B.unit(U))
      return fail(
          D, {U.TranslationUnit, 1, 1},
          "Combined project exceeds the pre-deduplication IR resource budget.");
    if (!verifyProjectUnit(U, Context, D))
      return false;
    Units.push_back(&U);
  }
  std::sort(Units.begin(), Units.end(), [](const auto *A, const auto *B) {
    return A->TranslationUnit < B->TranslationUnit;
  });
  MergedProject P;
  P.Definitions = Units.front()->Definitions;
  P.Definitions.Dependencies.clear();
  P.Definitions.Records.clear();
  P.Definitions.Globals.clear();
  P.Definitions.Functions.clear();
  P.Definitions.Exports.clear();
  P.Definitions.SDKDependencies.clear();
  P.Definitions.Mappings.clear();
  P.Definitions.Target.Triple = llvm::Triple::normalize(Context.TargetTriple);
  std::map<std::string, Dependency> Dependencies;
  std::map<std::pair<std::string, std::string>, SDKDependency> SDKDependencies;
  std::map<std::string, MappingEvidence> Mappings;
  std::map<std::string, FunctionDeclaration> FunctionDeclarations;
  std::map<std::string, GlobalDeclaration> GlobalDeclarations;
  std::map<std::string, std::pair<std::string, EntityKind>> Names;
  std::map<std::string, SourceLocation> NameOrigins;
  std::map<std::string, std::string> IDs;
  struct Definition {
    const ODREvidence *Evidence;
    const ProjectUnit *Unit;
    std::string Canonical;
    size_t EntityIndex;
  };
  std::map<std::string, Definition> Definitions;
  auto Identity = [&](const std::string &Name, const std::string &ID,
                      EntityKind Kind, const SourceLocation &L) {
    auto N = Names.emplace(Name, std::make_pair(ID, Kind));
    auto I = IDs.emplace(ID, Name);
    NameOrigins.emplace(Name, L);
    bool NameConflict =
        !N.second && N.first->second != std::make_pair(ID, Kind);
    bool IDConflict = !I.second && I.first->second != Name;
    if (NameConflict || IDConflict)
      return fail(
          D, L,
          "Project entity name, semantic identity, or declaration kind "
          "conflict; previous declaration at " +
              at(NameOrigins.at(NameConflict ? Name : I.first->second)) + ".");
    return true;
  };
  for (const ProjectUnit *UP : Units) {
    const auto &U = *UP;
    if (!P.Units.empty() && P.Units.back().TranslationUnit == U.TranslationUnit)
      return fail(D, {U.TranslationUnit, 1, 1},
                  "The same source cannot be selected more than once or under "
                  "multiple configurations.");
    P.Units.push_back({U.TranslationUnit, U.ConfigurationID});
    if (U.Definitions.Frontend.Build != P.Definitions.Frontend.Build)
      return fail(D, {U.TranslationUnit, 1, 1},
                  "All project units must use the same frontend build.",
                  "TR0103");
    if (U.Definitions.FPContractID != P.Definitions.FPContractID ||
        U.Definitions.SDKDistributionID != P.Definitions.SDKDistributionID ||
        U.Definitions.SDKCatalogSHA256 != P.Definitions.SDKCatalogSHA256)
      return fail(D, {U.TranslationUnit, 1, 1},
                  "Project units disagree on the approved SDK or "
                  "floating-point contract.");
    for (const auto &Dep : U.Definitions.SDKDependencies) {
      auto I = SDKDependencies.emplace(std::make_pair(Dep.Root, Dep.Path), Dep);
      if (!I.second && I.first->second.SHA256 != Dep.SHA256)
        return fail(
            D, {U.TranslationUnit, 1, 1},
            "Consumed SDK dependency bytes differ between selected units.");
    }
    for (const auto &Mapping : U.Definitions.Mappings) {
      auto I = Mappings.emplace(Mapping.ID, Mapping);
      const auto &Old = I.first->second;
      if (!I.second && (Mapping.DeclarationID != Old.DeclarationID ||
                        Mapping.Result != Old.Result ||
                        Mapping.Parameters != Old.Parameters ||
                        Mapping.Origin.Root != Old.Origin.Root ||
                        Mapping.Origin.Path != Old.Origin.Path ||
                        Mapping.Origin.SHA256 != Old.Origin.SHA256 ||
                        Mapping.Origin.Line != Old.Origin.Line ||
                        Mapping.Origin.Column != Old.Origin.Column))
        return fail(D, {U.TranslationUnit, 1, 1},
                    "Project mapping evidence disagrees on resolved SDK "
                    "declaration identity.");
    }
    for (const auto &Dep : U.Definitions.Dependencies) {
      auto I = Dependencies.emplace(Dep.Path, Dep);
      if (!I.second && I.first->second.SHA256 != Dep.SHA256)
        return fail(D, {Dep.Path, 1, 1},
                    "Owned dependency bytes differ between selected "
                    "translation units.");
    }
    for (const auto &F : U.FunctionDeclarations) {
      if (!Identity(F.Name, F.SemanticID, EntityKind::Function, F.Loc))
        return false;
      auto I = FunctionDeclarations.emplace(F.Name, F);
      if (!I.second &&
          (!sameSignature(signature(F), signature(I.first->second)) ||
           F.Inline != I.first->second.Inline))
        return fail(
            D, F.Loc,
            "Conflicting function declaration; previous declaration at " +
                at(I.first->second.Loc) + ".");
    }
    for (const auto &G : U.GlobalDeclarations) {
      if (!Identity(G.Name, G.SemanticID, EntityKind::Global, G.Loc))
        return false;
      auto I = GlobalDeclarations.emplace(G.Name, G);
      if (!I.second && (G.ValueType != I.first->second.ValueType ||
                        G.Internal != I.first->second.Internal))
        return fail(D, G.Loc,
                    "Conflicting global declaration; previous declaration at " +
                        at(I.first->second.Loc) + ".");
    }
    std::map<std::string, const ODREvidence *> Evidence;
    for (const auto &E : U.ODR)
      Evidence.emplace(E.Name, &E);
    auto DefinitionEntry = [&](const std::string &Name, std::string Body,
                               auto Append) {
      const ODREvidence &E = *Evidence.at(Name);
      if (!Identity(E.Name, E.SemanticID, E.Kind, E.Origin))
        return false;
      auto I = Definitions.find(Name);
      if (I == Definitions.end()) {
        Definitions.emplace(
            Name, Definition{&E, &U, std::move(Body), P.Entities.size()});
        P.Entities.push_back({E, {E.Origin}, {U.TranslationUnit}});
        Append();
        return true;
      }
      const auto &Previous = I->second;
      const auto &Old = *Previous.Evidence;
      std::string Reason;
      if (!E.OwnerTU.empty() || !Old.OwnerTU.empty())
        Reason = "TU-private definitions have a colliding project identity";
      else if (E.Kind != EntityKind::Record && (!E.Inline || !Old.Inline))
        Reason = "Duplicate strong external definition";
      else if (!sameLocation(E.Origin, Old.Origin) ||
               E.Origin.File == U.TranslationUnit ||
               Old.Origin.File == Previous.Unit->TranslationUnit)
        Reason = "Repeated definitions require the same owned-header origin";
      else if (E.TokensSHA256 != Old.TokensSHA256 ||
               E.BindingsSHA256 != Old.BindingsSHA256 ||
               Body != Previous.Canonical)
        Reason = "Repeated header definition has conflicting tokens, resolved "
                 "bindings, or typed body";
      if (!Reason.empty())
        return fail(D, E.Origin,
                    Reason + "; previous definition at " + at(Old.Origin) +
                        ".");
      auto &Entity = P.Entities[Previous.EntityIndex];
      Entity.Origins.push_back(E.Origin);
      Entity.TranslationUnits.push_back(U.TranslationUnit);
      return true;
    };
    for (const auto &R : U.Definitions.Records)
      if (!DefinitionEntry(R.ID, Canonical().record(R),
                           [&] { P.Definitions.Records.push_back(R); }))
        return false;
    for (const auto &G : U.Definitions.Globals)
      if (!DefinitionEntry(G.Name, Canonical().global(G),
                           [&] { P.Definitions.Globals.push_back(G); }))
        return false;
    for (const auto &F : U.Definitions.Functions)
      if (!DefinitionEntry(F.Name, Canonical().function(F),
                           [&] { P.Definitions.Functions.push_back(F); }))
        return false;
  }
  for (const auto &Entry : FunctionDeclarations)
    if (!Definitions.count(Entry.first))
      return fail(D, Entry.second.Loc,
                  "Selected project has no definition for declared function: " +
                      Entry.first,
                  "TR0203");
  for (const auto &Entry : GlobalDeclarations)
    if (!Definitions.count(Entry.first))
      return fail(D, Entry.second.Loc,
                  "Selected project has no definition for declared global: " +
                      Entry.first,
                  "TR0203");
  for (const auto &Dep : Dependencies)
    P.Definitions.Dependencies.push_back(Dep.second);
  for (const auto &Dep : SDKDependencies)
    P.Definitions.SDKDependencies.push_back(Dep.second);
  for (const auto &Mapping : Mappings)
    P.Definitions.Mappings.push_back(Mapping.second);
  if (!sortRecords(P.Definitions, D))
    return false;
  std::sort(P.Definitions.Globals.begin(), P.Definitions.Globals.end(),
            [](const auto &A, const auto &B) { return A.Name < B.Name; });
  std::sort(P.Definitions.Functions.begin(), P.Definitions.Functions.end(),
            [](const auto &A, const auto &B) { return A.Name < B.Name; });
  std::sort(P.Entities.begin(), P.Entities.end(),
            [](const auto &A, const auto &B) {
              return A.Representative.Name < B.Representative.Name;
            });
  exports(P.Definitions);
  if (!verifyMergedProject(P, Context, D))
    return false;
  Out = std::move(P);
  return true;
}

bool verifyMergedProject(const MergedProject &P,
                         const VerificationContext &Context, Diagnostics &D) {
  Budget B;
  if (!B.module(P.Definitions))
    return fail(D, {"<project>", 1, 1},
                "Merged project exceeds the IR resource budget.");
  for (const auto &U : P.Units) {
    ++B.Nodes;
    B.text(U.TranslationUnit);
    B.text(U.ConfigurationID);
  }
  for (const auto &Entity : P.Entities) {
    const auto &E = Entity.Representative;
    ++B.Nodes;
    B.text(E.Name);
    B.text(E.SemanticID);
    B.text(E.OwnerTU);
    B.location(E.Origin);
    B.text(E.TokensSHA256);
    B.text(E.BindingsSHA256);
    for (const auto &L : Entity.Origins) {
      ++B.Nodes;
      B.location(L);
    }
    for (const auto &TU : Entity.TranslationUnits) {
      ++B.Nodes;
      B.text(TU);
    }
  }
  if (!B.valid())
    return fail(D, {"<project>", 1, 1},
                "Merged project metadata exceeds the IR resource budget.");
  if (!detail::verifyProjectDefinitions(P.Definitions, Context, {}, {}, D))
    return false;
  std::set<std::string> Paths, Units, IDs;
  for (const auto &Dep : P.Definitions.Dependencies)
    Paths.insert(Dep.Path);
  if (P.Units.empty() || P.Units.size() > 4096)
    return fail(D, {"<project>", 1, 1},
                "Merged project has an invalid selected unit list.");
  for (const auto &U : P.Units)
    if (!Paths.count(U.TranslationUnit) || !digest(U.ConfigurationID) ||
        !Units.insert(U.TranslationUnit).second)
      return fail(
          D, {U.TranslationUnit, 1, 1},
          "Merged project has an invalid or duplicate selected unit identity.");
  std::map<std::string, std::pair<EntityKind, SourceLocation>> Definitions;
  std::map<std::string, bool> InternalFunctions;
  for (const auto &R : P.Definitions.Records)
    Definitions.emplace(R.ID, std::make_pair(EntityKind::Record, R.Loc));
  for (const auto &G : P.Definitions.Globals)
    Definitions.emplace(G.Name, std::make_pair(EntityKind::Global, G.Loc));
  for (const auto &F : P.Definitions.Functions) {
    Definitions.emplace(F.Name, std::make_pair(EntityKind::Function, F.Loc));
    InternalFunctions.emplace(F.Name, F.Internal);
  }
  std::set<std::string> Seen;
  for (const auto &Entity : P.Entities) {
    const auto &E = Entity.Representative;
    auto Def = Definitions.find(E.Name);
    if (!evidence(E, Paths, D) || !IDs.insert(E.SemanticID).second ||
        !Seen.insert(E.Name).second || Def == Definitions.end() ||
        Def->second.first != E.Kind ||
        !sameLocation(Def->second.second, E.Origin) ||
        (!E.OwnerTU.empty() && !Units.count(E.OwnerTU)) ||
        Entity.Origins.empty() ||
        Entity.Origins.size() != Entity.TranslationUnits.size())
      return fail(D, E.Origin,
                  "Merged entity metadata does not identify exactly one "
                  "selected definition.");
    if (E.Kind == EntityKind::Function &&
        InternalFunctions.at(E.Name) != !E.OwnerTU.empty())
      return fail(D, E.Origin,
                  "Merged function linkage disagrees with ODR ownership.");
    std::set<std::string> Contributors;
    for (size_t I = 0; I < Entity.Origins.size(); ++I)
      if (!sameLocation(Entity.Origins[I], E.Origin) ||
          !Units.count(Entity.TranslationUnits[I]) ||
          !Contributors.insert(Entity.TranslationUnits[I]).second ||
          (!E.OwnerTU.empty() && E.OwnerTU != Entity.TranslationUnits[I]))
        return fail(D, E.Origin,
                    "Invalid merged definition origin or contributing "
                    "translation unit.");
    if (Entity.Origins.size() > 1 &&
        (!E.OwnerTU.empty() || (E.Kind != EntityKind::Record && !E.Inline) ||
         Contributors.count(E.Origin.File)))
      return fail(
          D, E.Origin,
          "Merged duplicate definitions violate project linkage rules.");
  }
  if (Seen.size() != Definitions.size())
    return fail(D, {"<project>", 1, 1},
                "Merged definitions lack complete ODR metadata.");
  return true;
}

bool emitProject(const MergedProject &P, const VerificationContext &Context,
                 EmittedProject &Out, Diagnostics &D) {
  if (!verifyMergedProject(P, Context, D))
    return false;
  std::set<std::string> PublicRecords, ExternalGlobals;
  for (const auto &E : P.Entities)
    if (E.Representative.OwnerTU.empty()) {
      if (E.Representative.Kind == EntityKind::Record)
        PublicRecords.insert(E.Representative.Name);
      if (E.Representative.Kind == EntityKind::Global)
        ExternalGlobals.insert(E.Representative.Name);
    }
  auto PublicType = [&](const Type &T) {
    if (T.Kind == TypeKind::Record)
      PublicRecords.insert(T.RecordID);
  };
  for (const auto &F : P.Definitions.Functions)
    if (!F.Internal) {
      PublicType(F.Result);
      for (const auto &Param : F.Params)
        PublicType(Param.ValueType);
    }
  for (const auto &G : P.Definitions.Globals)
    if (ExternalGlobals.count(G.Name))
      PublicType(G.ValueType);
  // Verified record order places dependencies first; walk backwards to
  // propagate public visibility in one pass, including long by-value dependency
  // chains.
  for (auto I = P.Definitions.Records.rbegin();
       I != P.Definitions.Records.rend(); ++I)
    if (PublicRecords.count(I->ID))
      for (const auto &F : I->Fields)
        PublicType(F.ValueType);
  EmittedSource Header, Source;
  detail::emitProjectFiles(P.Definitions, PublicRecords, ExternalGlobals,
                           Header, Source);
  if (Header.Text.size() + Source.Text.size() > 2 * MaxFrontendResponseBytes)
    return fail(D, {"<project>", 1, 1},
                "Generated project exceeds the 64 MiB emission limit.");
  EmittedProject Result;
  Result.Files.push_back(
      {"translated.h", std::move(Header.Text), std::move(Header.Map)});
  Result.Files.push_back(
      {"translated.nc", std::move(Source.Text), std::move(Source.Map)});
  Out = std::move(Result);
  return true;
}
} // namespace neverc::translate
