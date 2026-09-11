#include "TranslateIR.h"
#include "JSON.h"
#include "TranslateIRInternal.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <set>

namespace neverc::translate {

std::string typeName(const Type &T) {
  switch (T.Kind) {
  case TypeKind::Int:
    return "int";
  case TypeKind::UInt:
    return "uint";
  case TypeKind::Bool:
    return "bool";
  case TypeKind::Void:
    return "void";
  case TypeKind::Record:
    return T.RecordID;
  case TypeKind::Double:
    return "double";
  case TypeKind::Pointer:
    return std::string(T.PointeeConst ? "cptr:" : "ptr:") +
           (T.Elements.size() == 1 ? typeName(T.Elements[0]) : "?");
  case TypeKind::Array:
    return "arr:" + std::to_string(T.Count) + ":" +
           (T.Elements.size() == 1 ? typeName(T.Elements[0]) : "?");
  }
  return {};
}

const MappingSpec *findMappingSpec(llvm::StringRef ID) {
  static const MappingSpec Specs[] = {
      {"cpp.math.fabs.f64.v1", "neverc_math_abs", "math_abs",
       "neverc/std/math.h"},
      {"cpp.math.floor.f64.v1", "neverc_math_floor", "math_floor",
       "neverc/std/math.h"}};
  for (const auto &Spec : Specs)
    if (ID == Spec.ID)
      return &Spec;
  return nullptr;
}

namespace {
bool fail(Diagnostics &D, llvm::StringRef Code, const SourceLocation &Loc,
          llvm::StringRef Construct, llvm::StringRef Reason) {
  D.push_back(
      {Code.str(), Loc, Construct.str(), Reason.str(),
       "Use a compatible frontend and the documented translation profile."});
  return false;
}

class Parser {
  Diagnostics &D;
  std::size_t Nodes = 0;
  bool Math = false;
  SourceLocation Anchor{"<frontend>", 1, 1};

public:
  explicit Parser(Diagnostics &D) : D(D) {}
  bool error(llvm::StringRef Message) {
    return fail(D, "TR0103", Anchor, "frontend protocol", Message);
  }
  bool node() {
    return ++Nodes <= MaxProtocolNodes ||
           error("Protocol node limit exceeded.");
  }
  bool string(const llvm::json::Object &O, llvm::StringRef Key,
              std::string &Out) {
    auto S = O.getString(Key);
    if (!S.data() || S.contains('\0'))
      return error("Missing or invalid string field: " + Key.str());
    Out = S.str();
    return true;
  }
  bool boolean(const llvm::json::Object &O, llvm::StringRef Key, bool &Out) {
    auto B = O.getBoolean(Key);
    if (B < 0)
      return error("Missing or invalid boolean field: " + Key.str());
    Out = B != 0;
    return true;
  }
  bool integer(const llvm::json::Object &O, llvm::StringRef Key,
               uint32_t &Out) {
    int64_t I = 0;
    if (!O.getInteger(Key, I) || I < 0 || uint64_t(I) > UINT32_MAX)
      return error("Missing or invalid integer field: " + Key.str());
    Out = uint32_t(I);
    return true;
  }
  bool storageLayout(const llvm::json::Object &O, StorageLayout &Out) {
    return integer(O, "size_bits", Out.SizeBits) &&
           integer(O, "abi_align_bits", Out.ABIAlignBits);
  }
  bool carrierLayout(const llvm::json::Object &O, CarrierLayout &Out) {
    if (O.size() != CarrierNames.size() + 1 ||
        !integer(O, "char_bits", Out.CharBits))
      return error("Invalid carrier layout fields.");
    for (size_t I = 0; I < CarrierNames.size(); ++I) {
      const auto *C = O.getObject(CarrierNames[I]);
      if (!C || C->size() != 2 || !storageLayout(*C, Out.Carriers[I]))
        return error("Missing or invalid carrier layout entry.");
    }
    return true;
  }
  bool recordLayout(const llvm::json::Object &O, RecordLayout &Out) {
    const auto *Offsets = O.getArray("field_offsets_bits");
    if (O.size() != 3 || !storageLayout(O, Out.Storage) || !Offsets)
      return error("Missing or invalid record layout fields.");
    for (const auto &V : *Offsets) {
      int64_t Bits = 0;
      if (!node() || !V.getAsInteger(Bits) || Bits < 0 ||
          uint64_t(Bits) > UINT32_MAX)
        return error("Invalid record field offset.");
      Out.FieldOffsetsBits.push_back(uint32_t(Bits));
    }
    return true;
  }
  bool location(const llvm::json::Object &O, SourceLocation &Loc) {
    const auto *L = O.getObject("loc");
    if (!L)
      return error("Missing source location.");
    return string(*L, "file", Loc.File) && integer(*L, "line", Loc.Line) &&
           integer(*L, "column", Loc.Column);
  }
  bool type(const llvm::json::Object &O, llvm::StringRef Key, Type &T) {
    std::string S;
    if (!string(O, Key, S))
      return false;
    if (S.size() > 4096)
      return error("Type spelling limit exceeded.");
    return typeSpelling(S, T);
  }
  bool typeSpelling(llvm::StringRef S, Type &T, std::size_t Depth = 0) {
    bool Pointer = S.starts_with("ptr:") || S.starts_with("cptr:");
    bool Array = S.starts_with("arr:");
    if (Depth > MaxProtocolDepth || ((Depth || Pointer || Array) && !node()))
      return error("Type depth or node limit exceeded.");
    if (Pointer) {
      T.Kind = TypeKind::Pointer;
      T.PointeeConst = S.starts_with("cptr:");
      T.Elements.resize(1);
      return typeSpelling(S.drop_front(T.PointeeConst ? 5 : 4), T.Elements[0],
                          Depth + 1);
    }
    if (Array) {
      auto Parts = S.drop_front(4).split(':');
      if (Parts.first.empty() || Parts.first.front() == '0' ||
          !std::all_of(Parts.first.begin(), Parts.first.end(),
                       [](char C) { return C >= '0' && C <= '9'; }) ||
          Parts.first.getAsInteger(10, T.Count) || T.Count > 65536)
        return error("Array extent must be a canonical integer from 1 to 65536.");
      T.Kind = TypeKind::Array;
      T.Elements.resize(1);
      return typeSpelling(Parts.second, T.Elements[0], Depth + 1);
    }
    if (S.empty() || S.contains(':'))
      return error("Invalid canonical type spelling.");
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
      T.RecordID = S.str();
    }
    return true;
  }
  template <typename T, typename Fn>
  bool array(const llvm::json::Object &O, llvm::StringRef Key,
             std::vector<T> &Out, Fn Parse) {
    const auto *A = O.getArray(Key);
    if (!A)
      return error("Missing or invalid array field: " + Key.str());
    if (A->size() > MaxProtocolNodes)
      return error("Protocol array limit exceeded.");
    Out.reserve(A->size());
    for (const auto &V : *A) {
      const auto *E = V.getAsObject();
      if (!E || !node())
        return E ? false : error("Expected an object in " + Key.str());
      T Item;
      if (!Parse(*E, Item))
        return false;
      Out.push_back(std::move(Item));
    }
    return true;
  }
  bool expr(const llvm::json::Object &O, Expr &E, std::size_t Depth = 0) {
    if (Depth > MaxProtocolDepth || !node())
      return error("Expression depth or node limit exceeded.");
    std::string K;
    if (!string(O, "kind", K) || !type(O, "type", E.ValueType) ||
        !location(O, E.Loc))
      return false;
    if (K == "literal") {
      E.Kind = ExprKind::Literal;
      if (E.ValueType.Kind == TypeKind::Bool)
        return boolean(O, "value", E.Boolean);
      if (E.ValueType.Kind == TypeKind::Double) {
        std::string Bits;
        if (!Math || !string(O, "bits", Bits) || Bits.size() != 16 ||
            !std::all_of(Bits.begin(), Bits.end(),
                         [](char C) {
                           return (C >= '0' && C <= '9') ||
                                  (C >= 'a' && C <= 'f');
                         }) ||
            llvm::StringRef(Bits).getAsInteger(16, E.Binary64Bits))
          return error("A math double literal requires exactly 16 lowercase "
                       "hexadecimal bits.");
        return true;
      }
      return string(O, "value", E.Integer);
    }
    if (K == "null") {
      E.Kind = ExprKind::Null;
      return true;
    }
    if (K == "var") {
      E.Kind = ExprKind::Var;
      return string(O, "name", E.Name);
    }
    if (K == "unary") {
      E.Kind = ExprKind::Unary;
      std::string Op;
      if (!string(O, "operator", Op))
        return false;
      if (Op == "+")
        E.UnaryOp = UnaryOperator::Plus;
      else if (Op == "-")
        E.UnaryOp = UnaryOperator::Minus;
      else if (Op == "~")
        E.UnaryOp = UnaryOperator::BitNot;
      else if (Op == "!")
        E.UnaryOp = UnaryOperator::LogicalNot;
      else
        return error("Unknown unary operator.");
    } else if (K == "binary") {
      E.Kind = ExprKind::Binary;
      std::string Op;
      if (!string(O, "operator", Op))
        return false;
      const std::pair<const char *, BinaryOperator> Operators[] = {
          {"+", BinaryOperator::Add},
          {"-", BinaryOperator::Subtract},
          {"*", BinaryOperator::Multiply},
          {"/", BinaryOperator::Divide},
          {"%", BinaryOperator::Remainder},
          {"<<", BinaryOperator::ShiftLeft},
          {">>", BinaryOperator::ShiftRight},
          {"&", BinaryOperator::BitAnd},
          {"|", BinaryOperator::BitOr},
          {"^", BinaryOperator::BitXor},
          {"==", BinaryOperator::Equal},
          {"!=", BinaryOperator::NotEqual},
          {"<", BinaryOperator::Less},
          {"<=", BinaryOperator::LessEqual},
          {">", BinaryOperator::Greater},
          {">=", BinaryOperator::GreaterEqual}};
      auto I = std::find_if(std::begin(Operators), std::end(Operators),
                            [&](const auto &P) { return Op == P.first; });
      if (I == std::end(Operators))
        return error("Unknown binary operator.");
      E.BinaryOp = I->second;
    } else if (K == "cast")
      E.Kind = ExprKind::Cast;
    else if (K == "member") {
      E.Kind = ExprKind::Member;
      if (!string(O, "name", E.Name))
        return false;
    } else if (K == "aggregate")
      E.Kind = ExprKind::Aggregate;
    else if (K == "address")
      E.Kind = ExprKind::Address;
    else if (K == "dereference")
      E.Kind = ExprKind::Dereference;
    else if (K == "array_decay")
      E.Kind = ExprKind::ArrayDecay;
    else if (K == "index")
      E.Kind = ExprKind::Index;
    else
      return error("Unknown expression kind.");
    return array(O, "args", E.Args, [&](const auto &A, Expr &Arg) {
      return expr(A, Arg, Depth + 1);
    });
  }
  bool expressionField(const llvm::json::Object &O, llvm::StringRef Key,
                       std::optional<Expr> &Out, bool Required = true) {
    const auto *V = O.get(Key);
    if (!V)
      return !Required || error("Missing expression field: " + Key.str());
    const auto *E = V->getAsObject();
    if (!E)
      return error("Invalid expression field: " + Key.str());
    Expr Result;
    if (!expr(*E, Result))
      return false;
    Out = std::move(Result);
    return true;
  }
  bool instruction(const llvm::json::Object &O, Instruction &I) {
    std::string Op;
    if (!string(O, "op", Op) || !location(O, I.Loc))
      return false;
    if (Op == "assign") {
      I.Op = InstructionKind::Assign;
      return expressionField(O, "target", I.Target) &&
             expressionField(O, "value", I.Value);
    }
    if (Op == "call" || Op == "mapped_call") {
      I.Op = Op == "call" ? InstructionKind::Call : InstructionKind::MappedCall;
      if (I.Op == InstructionKind::MappedCall && !Math)
        return error("Mapped calls require cpp-math-v1.");
      return (I.Op == InstructionKind::Call
                  ? string(O, "callee", I.Callee)
                  : string(O, "mapping", I.MappingID)) &&
             expressionField(O, "target", I.Target, false) &&
             array(O, "args", I.Args,
                   [&](const auto &A, Expr &E) { return expr(A, E); });
    }
    if (Op == "label" || Op == "jump") {
      I.Op = Op == "label" ? InstructionKind::Label : InstructionKind::Jump;
      return string(O, "label", I.Label);
    }
    if (Op == "branch") {
      I.Op = InstructionKind::Branch;
      return expressionField(O, "condition", I.Condition) &&
             string(O, "true", I.TrueLabel) && string(O, "false", I.FalseLabel);
    }
    if (Op == "return") {
      I.Op = InstructionKind::Return;
      return expressionField(O, "value", I.Value, false);
    }
    return error("Unknown instruction operation.");
  }
  bool variable(const llvm::json::Object &O, Variable &V) {
    return string(O, "name", V.Name) && type(O, "type", V.ValueType) &&
           location(O, V.Loc);
  }
  bool mathMetadata(const llvm::json::Object &O, Module &M) {
    if (!string(O, "fp_contract", M.FPContractID) ||
        !string(O, "sdk_distribution_id", M.SDKDistributionID) ||
        !string(O, "sdk_catalog_sha256", M.SDKCatalogSHA256))
      return false;
    return array(O, "sdk_dependencies", M.SDKDependencies,
                 [&](const auto &V, SDKDependency &Dep) {
                   return string(V, "root", Dep.Root) &&
                          string(V, "path", Dep.Path) &&
                          string(V, "sha256", Dep.SHA256);
                 }) &&
           array(O, "mappings", M.Mappings,
                 [&](const auto &V, MappingEvidence &Mapping) {
                   if (!string(V, "id", Mapping.ID) ||
                       !string(V, "declaration_id", Mapping.DeclarationID) ||
                       !type(V, "result", Mapping.Result))
                     return false;
                   const auto *Params = V.getArray("parameters");
                   if (!Params || Params->size() > MaxProtocolNodes)
                     return error("Invalid mapping parameter types.");
                   for (const auto &P : *Params) {
                     auto T = P.getAsString();
                     if (!T.data() || T != "double")
                       return error(
                           "Math mappings require an exact double parameter.");
                     Mapping.Parameters.push_back({TypeKind::Double, {}});
                   }
                   const auto *L = V.getObject("origin");
                   if (!L)
                     return error("Missing SDK mapping declaration origin.");
                   return string(*L, "root", Mapping.Origin.Root) &&
                          string(*L, "path", Mapping.Origin.Path) &&
                          string(*L, "sha256", Mapping.Origin.SHA256) &&
                          integer(*L, "line", Mapping.Origin.Line) &&
                          integer(*L, "column", Mapping.Origin.Column);
                 });
  }
  bool module(const llvm::json::Object &O, Module &M) {
    if (!integer(O, "protocol", M.Protocol) || !string(O, "profile", M.Profile))
      return false;
    if (M.Protocol != FrontendProtocolMajor)
      return error("Incompatible frontend protocol major version.");
    Math = M.Profile == "cpp-math-v1";
    const auto *F = O.getObject("frontend");
    const auto *T = O.getObject("target");
    if (!F || !T)
      return error("Missing frontend or target metadata.");
    if (!string(*F, "name", M.Frontend.Name) ||
        !string(*F, "version", M.Frontend.Version) ||
        !string(*F, "build", M.Frontend.Build) ||
        !string(*T, "triple", M.Target.Triple) ||
        !integer(*T, "int_bits", M.Target.IntBits) ||
        !integer(*T, "pointer_bits", M.Target.PointerBits) ||
        !boolean(*T, "little_endian", M.Target.LittleEndian))
      return false;
    if (M.Profile == "cpp-core-v2") {
      const auto *Layout = T->getObject("carrier_layout");
      if (!Layout)
        return error("Core v2 requires carrier layout evidence.");
      M.Target.Carriers.emplace();
      if (!carrierLayout(*Layout, *M.Target.Carriers))
        return false;
    } else if (T->get("carrier_layout"))
      return error("Carrier layout evidence requires core v2.");
    const auto *Mappings = O.getArray("mappings");
    const auto *Diags = O.getArray("diagnostics");
    if (!Mappings || !Diags)
      return error("Missing mapping or diagnostic array.");
    if (!Math && (!Mappings->empty() || O.get("fp_contract") ||
                  O.get("sdk_distribution_id") || O.get("sdk_catalog_sha256") ||
                  O.get("sdk_dependencies")))
      return error(
          "The selected profile admits no math mappings or SDK evidence.");
    if (!Diags->empty())
      return error("A successful module cannot contain error diagnostics.");
    if (Math && !mathMetadata(O, M))
      return false;
    if (!array(O, "dependencies", M.Dependencies,
               [&](const auto &V, Dependency &Dep) {
                 return string(V, "path", Dep.Path) &&
                        string(V, "sha256", Dep.SHA256);
               }) ||
        !array(O, "records", M.Records,
               [&](const auto &V, Record &R) {
                 if (M.Profile == "cpp-core-v2") {
                   const auto *Layout = V.getObject("layout");
                   if (!Layout)
                     return error("Core v2 records require layout evidence.");
                   R.Layout.emplace();
                   if (!recordLayout(*Layout, *R.Layout))
                     return false;
                 } else if (V.get("layout"))
                   return error("Record layout evidence requires core v2.");
                 return string(V, "id", R.ID) && location(V, R.Loc) &&
                        array(V, "fields", R.Fields,
                              [&](const auto &J, Field &Field) {
                                return string(J, "name", Field.Name) &&
                                       type(J, "type", Field.ValueType);
                              });
               }) ||
        !array(O, "globals", M.Globals,
               [&](const auto &V, Global &G) {
                 if (!string(V, "name", G.Name) ||
                     !type(V, "type", G.ValueType) || !location(V, G.Loc))
                   return false;
                 const auto *E = V.getObject("value");
                 return E ? expr(*E, G.Value)
                          : error("Missing global initializer.");
               }) ||
        !array(O, "functions", M.Functions, [&](const auto &V, Function &Fn) {
          return string(V, "name", Fn.Name) && type(V, "result", Fn.Result) &&
                 boolean(V, "internal", Fn.Internal) &&
                 boolean(V, "c_export", Fn.CExport) && location(V, Fn.Loc) &&
                 array(V, "params", Fn.Params,
                       [&](const auto &J, Variable &P) {
                         return variable(J, P);
                       }) &&
                 array(V, "locals", Fn.Locals,
                       [&](const auto &J, Variable &L) {
                         return variable(J, L);
                       }) &&
                 array(V, "body", Fn.Body, [&](const auto &J, Instruction &I) {
                   return instruction(J, I);
                 });
        }))
      return false;
    for (const auto &Fn : M.Functions) {
      if (Fn.Internal)
        continue;
      Export E{Fn.Name, typeName(Fn.Result), {}, Fn.CExport};
      for (const auto &P : Fn.Params)
        E.Parameters.push_back(typeName(P.ValueType));
      M.Exports.push_back(std::move(E));
    }
    return true;
  }
};

bool relativePath(llvm::StringRef P) {
  if (P.empty() || P.starts_with("/") || P.ends_with("/") || P.contains('\\') ||
      P.contains('\0') || P.contains(':') || P.contains('\n') ||
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

bool identifier(llvm::StringRef N) {
  auto Alpha = [](unsigned char C) {
    return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || C == '_';
  };
  if (N.empty() || N.size() > MaxProtocolIdentifierBytes || !Alpha(N.front()))
    return false;
  for (unsigned char C : N)
    if (!Alpha(C) && !(C >= '0' && C <= '9'))
      return false;
  if (N.starts_with("_") || N.starts_with("neverc_") || N.starts_with("nc_") ||
      N.starts_with("nct_emit_"))
    return false;
  return !llvm::StringSwitch<bool>(N)
              .Cases("auto", "break", "case", "char", "const", "continue", true)
              .Cases("default", "do", "double", "else", "enum", "extern", true)
              .Cases("float", "for", "goto", "if", "inline", "int", true)
              .Cases("long", "register", "restrict", "return", "short",
                     "signed", true)
              .Cases("sizeof", "static", "struct", "switch", "typedef", "union",
                     true)
              .Cases("unsigned", "void", "volatile", "while", "alignas",
                     "alignof", true)
              .Cases("bool", "constexpr", "false", "nullptr", "static_assert",
                     true)
              .Cases("thread_local", "true", "typeof", "typeof_unqual", true)
              .Cases("string", "i8", "i16", "i32", "i64", "i128", true)
              .Cases("u8", "u16", "u32", "u64", "u128", "isize", "usize", true)
              .Default(false);
}

bool canonicalInteger(llvm::StringRef S, TypeKind K) {
  if (K != TypeKind::Int && K != TypeKind::UInt)
    return false;
  bool Negative = S.consume_front("-");
  if (S.empty() || (Negative && K == TypeKind::UInt) ||
      (S.size() > 1 && S.front() == '0') || (Negative && S == "0"))
    return false;
  for (char C : S)
    if (C < '0' || C > '9')
      return false;
  uint64_t V;
  if (S.getAsInteger(10, V))
    return false;
  return V <= (K == TypeKind::UInt ? UINT64_C(4294967295)
               : Negative          ? UINT64_C(2147483648)
                                   : UINT64_C(2147483647));
}

class Verifier {
  const Module &M;
  const VerificationContext &Context;
  Diagnostics &D;
  bool Project = false;
  bool Math = false;
  bool HasSignalingNaNLiteral = false;
  std::set<std::string> Mappings, UsedMappings;
  llvm::ArrayRef<Function> FunctionDeclarations;
  llvm::ArrayRef<Variable> GlobalDeclarations;
  std::map<std::string, const Record *> Records;
  std::set<std::string> KnownRecords;
  std::map<std::string, std::size_t> RecordStorageUnits;
  std::map<std::string, bool> RecordHasArray;
  std::map<std::string, Type> Globals;
  std::map<std::string, const Function *> Functions;
  std::set<std::string> Symbols;
  std::set<std::string> Paths;
  std::size_t Nodes = 0;

  bool error(const SourceLocation &L, llvm::StringRef Reason) {
    return fail(D, "TR0301", L, "typed translation IR", Reason);
  }
  bool loc(const SourceLocation &L) {
    return (L.Line && L.Column && relativePath(L.File) &&
            Paths.count(L.File)) ||
           error(L, "Source location must name a declared relative dependency "
                    "and positive line/column.");
  }
  bool name(llvm::StringRef N, const SourceLocation &L,
            bool Generated = false) {
    return (identifier(N) && (!Generated || N.starts_with("nct_"))) ||
           error(
               L,
               "Invalid, reserved, or non-deterministic emitted identifier: " +
                   N.str());
  }
  bool type(const Type &T, const SourceLocation &L, bool Void = false,
            std::size_t Depth = 0, bool Indirect = false) {
    if (Depth > MaxProtocolDepth ||
        ((Depth || T.Kind == TypeKind::Pointer || T.Kind == TypeKind::Array) &&
         ++Nodes > MaxProtocolNodes))
      return error(L, "IR type depth/node limit exceeded.");
    if (T.Kind == TypeKind::Pointer) {
      if (M.Profile != "cpp-core-v2" || !T.RecordID.empty() || T.Count ||
          T.Elements.size() != 1)
        return error(L, "Pointer types require core v2 and exactly one pointee.");
      return type(T.Elements[0], L, true, Depth + 1, true);
    }
    if (T.Kind == TypeKind::Array) {
      if (M.Profile != "cpp-core-v2" || !T.RecordID.empty() || T.PointeeConst ||
          T.Elements.size() != 1 || !T.Count || T.Count > 65536)
        return error(L, "Array types require core v2, one element and a bounded extent.");
      // An outer pointer does not make incomplete/void array elements valid.
      if (!type(T.Elements[0], L, false, Depth + 1, false))
        return false;
      return storageUnits(T) <= MaxProtocolNodes ||
             error(L, "Array storage expansion exceeds the protocol limit.");
    }
    if (!T.Elements.empty() || T.PointeeConst || T.Count)
      return error(L, "Non-pointer type has pointer components/qualifiers.");
    switch (T.Kind) {
    case TypeKind::Void:
      return (Void && T.RecordID.empty()) ||
             error(L, "Void is permitted only as a function result.");
    case TypeKind::Double:
      return (Math && T.RecordID.empty()) ||
             error(L, "Double requires the explicit math profile.");
    case TypeKind::Int:
    case TypeKind::UInt:
    case TypeKind::Bool:
      return T.RecordID.empty() ||
             error(L, "Scalar type has an invalid record identity.");
    case TypeKind::Record:
      return (Indirect ? KnownRecords.count(T.RecordID)
                       : Records.count(T.RecordID)) ||
             error(L, "Unknown or forward/cyclic record type: " + T.RecordID);
    case TypeKind::Pointer:
    case TypeKind::Array:
      break;
    }
    return error(L, "Unknown type kind.");
  }
  bool expr(const Expr &E, const std::map<std::string, Type> &Storage,
            std::size_t Depth = 0, bool Folded = false,
            bool InitializerElement = false) {
    if (++Nodes > MaxProtocolNodes || Depth > MaxProtocolDepth)
      return error(E.Loc, "IR expression/node limit exceeded.");
    if (!loc(E.Loc) || !type(E.ValueType, E.Loc))
      return false;
    if (Folded && E.Kind != ExprKind::Literal && E.Kind != ExprKind::Aggregate)
      return error(
          E.Loc, "Global initializer must be a folded literal/aggregate tree.");
    for (const auto &A : E.Args)
      if (!expr(A, Storage, Depth + 1, Folded, E.Kind == ExprKind::Aggregate))
        return false;
    auto Arity = [&](std::size_t N) {
      return E.Args.size() == N ||
             error(E.Loc, "Expression has incorrect operand count.");
    };
    switch (E.Kind) {
    case ExprKind::ArrayDecay:
      if (!Arity(1) || E.ValueType.Kind != TypeKind::Pointer ||
          E.Args[0].ValueType.Kind != TypeKind::Array ||
          E.ValueType.Elements[0] != E.Args[0].ValueType.Elements[0])
        return error(E.Loc, "Array decay requires matching addressable array/element types.");
      return lvalue(E.Args[0], Storage, !E.ValueType.PointeeConst);
    case ExprKind::Index:
      return (Arity(2) && E.Args[0].ValueType.Kind == TypeKind::Pointer &&
              E.Args[1].ValueType.isInteger() &&
              E.ValueType == E.Args[0].ValueType.Elements[0]) ||
             error(E.Loc, "Index requires a nonvoid element pointer and promoted integer.");
    case ExprKind::Null:
      return (Arity(0) && E.ValueType.Kind == TypeKind::Pointer) ||
             error(E.Loc, "Null requires a pointer type and no operands.");
    case ExprKind::Address:
      if (!Arity(1) || E.ValueType.Kind != TypeKind::Pointer ||
          E.ValueType.Elements[0] != E.Args[0].ValueType)
        return error(E.Loc, "Address requires a matching pointer/lvalue type.");
      return lvalue(E.Args[0], Storage, !E.ValueType.PointeeConst);
    case ExprKind::Dereference:
      return (Arity(1) && E.Args[0].ValueType.Kind == TypeKind::Pointer &&
              E.Args[0].ValueType.Elements[0] == E.ValueType) ||
             error(E.Loc, "Dereference requires a matching nonvoid pointee.");
    case ExprKind::Literal:
      if (!Arity(0))
        return false;
      if (E.ValueType.Kind == TypeKind::Double &&
          (E.Binary64Bits & UINT64_C(0x7ff8000000000000)) ==
              UINT64_C(0x7ff0000000000000) &&
          (E.Binary64Bits & UINT64_C(0x0007ffffffffffff)))
        HasSignalingNaNLiteral = true;
      return E.ValueType.Kind == TypeKind::Bool ||
             E.ValueType.Kind == TypeKind::Double ||
             canonicalInteger(E.Integer, E.ValueType.Kind) ||
             error(E.Loc, "Invalid scalar literal value or range.");
    case ExprKind::Var: {
      if (!Arity(0))
        return false;
      auto Local = Storage.find(E.Name);
      auto Global = Globals.find(E.Name);
      const Type *T = Local != Storage.end()    ? &Local->second
                      : Global != Globals.end() ? &Global->second
                                                : nullptr;
      return (T && *T == E.ValueType) ||
             error(E.Loc,
                   "Unknown variable or variable type mismatch: " + E.Name);
    }
    case ExprKind::Unary:
      if (!Arity(1))
        return false;
      if (E.UnaryOp == UnaryOperator::LogicalNot)
        return (E.Args[0].ValueType.isScalar() &&
                E.Args[0].ValueType.Kind != TypeKind::Double &&
                E.ValueType.Kind == TypeKind::Bool) ||
               error(E.Loc,
                     "Logical not requires a scalar operand and bool result.");
      if (E.UnaryOp != UnaryOperator::Plus &&
          E.UnaryOp != UnaryOperator::Minus &&
          E.UnaryOp != UnaryOperator::BitNot)
        return error(E.Loc, "Unknown unary operator.");
      if (E.ValueType.Kind == TypeKind::Double)
        return (Math && E.Args[0].ValueType == E.ValueType &&
                E.UnaryOp != UnaryOperator::BitNot) ||
               error(E.Loc,
                     "Math unary +/- requires a double operand and result.");
      return (E.Args[0].ValueType.isInteger() &&
              E.ValueType == E.Args[0].ValueType) ||
             error(E.Loc, "Unary arithmetic requires an explicitly promoted "
                          "integer operand.");
    case ExprKind::Binary: {
      if (!Arity(2))
        return false;
      const Type &A = E.Args[0].ValueType, &B = E.Args[1].ValueType;
      if (A.Kind == TypeKind::Pointer || B.Kind == TypeKind::Pointer)
        return (A == B && E.ValueType.Kind == TypeKind::Bool &&
                (E.BinaryOp == BinaryOperator::Equal ||
                 E.BinaryOp == BinaryOperator::NotEqual)) ||
               error(E.Loc, "Pointers permit only equally typed equality/inequality.");
      bool Compare = false, Shift = false;
      switch (E.BinaryOp) {
      case BinaryOperator::Equal:
      case BinaryOperator::NotEqual:
      case BinaryOperator::Less:
      case BinaryOperator::LessEqual:
      case BinaryOperator::Greater:
      case BinaryOperator::GreaterEqual:
        Compare = true;
        break;
      case BinaryOperator::ShiftLeft:
      case BinaryOperator::ShiftRight:
        Shift = true;
        break;
      case BinaryOperator::Add:
      case BinaryOperator::Subtract:
      case BinaryOperator::Multiply:
      case BinaryOperator::Divide:
      case BinaryOperator::Remainder:
      case BinaryOperator::BitAnd:
      case BinaryOperator::BitOr:
      case BinaryOperator::BitXor:
        break;
      default:
        return error(E.Loc, "Unknown binary operator.");
      }
      if (A.Kind == TypeKind::Double || B.Kind == TypeKind::Double)
        return (Math && Compare && A.Kind == TypeKind::Double &&
                B.Kind == TypeKind::Double &&
                E.ValueType.Kind == TypeKind::Bool) ||
               error(E.Loc, "The math profile permits double comparisons but "
                            "excludes binary floating arithmetic.");
      if (!A.isInteger() || !B.isInteger())
        return error(E.Loc,
                     "Binary operands must include source integer promotions.");
      if (!Shift && A != B)
        return error(E.Loc,
                     "Binary operands must have the same converted type.");
      return (Compare ? E.ValueType.Kind == TypeKind::Bool
                      : E.ValueType == A) ||
             error(E.Loc, "Binary expression result type mismatch.");
    }
    case ExprKind::Cast: {
      if (!Arity(1))
        return false;
      const Type &From = E.Args[0].ValueType, &To = E.ValueType;
      if (From.Kind == TypeKind::Pointer || To.Kind == TypeKind::Pointer) {
        if (From.Kind == TypeKind::Pointer && To.Kind == TypeKind::Bool)
          return true;
        if (From.Kind == TypeKind::Pointer && To.Kind == TypeKind::Pointer &&
            (From.Elements[0].Kind == TypeKind::Void ||
             To.Elements[0].Kind == TypeKind::Void || sameUnqualified(From, To)))
          return true;
        return error(E.Loc, "Unsupported pointer conversion.");
      }
      return (From.isScalar() && To.isScalar()) ||
             error(E.Loc, "Only documented scalar conversions are supported.");
    }
    case ExprKind::Member: {
      if (!Arity(1))
        return false;
      const auto &Base = E.Args[0].ValueType;
      if (Base.Kind != TypeKind::Record)
        return error(E.Loc, "Member base must be a record.");
      const auto &Fields = Records.at(Base.RecordID)->Fields;
      auto F = std::find_if(Fields.begin(), Fields.end(),
                            [&](const Field &F) { return F.Name == E.Name; });
      return (F != Fields.end() && F->ValueType == E.ValueType) ||
             error(E.Loc, "Unknown member or member type mismatch.");
    }
    case ExprKind::Aggregate: {
      for (const auto &A : E.Args)
        if (A.ValueType.Kind == TypeKind::Array && A.Kind != ExprKind::Aggregate)
          return error(E.Loc, "Array initializer children must be aggregate subtrees.");
      if (E.ValueType.Kind == TypeKind::Array) {
        if (!InitializerElement || !Arity(E.ValueType.Count))
          return error(E.Loc, "Array aggregate requires an initializer subtree with every element.");
        for (const auto &A : E.Args)
          if (A.ValueType != E.ValueType.Elements[0])
            return error(E.Loc, "Array element initializer type mismatch.");
        return true;
      }
      if (E.ValueType.Kind != TypeKind::Record)
        return error(E.Loc, "Aggregate expression must have record type.");
      const auto &Fields = Records.at(E.ValueType.RecordID)->Fields;
      if (!Arity(Fields.size()))
        return false;
      for (std::size_t I = 0; I < Fields.size(); ++I)
        if (E.Args[I].ValueType != Fields[I].ValueType)
          return error(E.Loc, "Aggregate field initializer type mismatch.");
      return true;
    }
    }
    return error(E.Loc, "Unknown expression kind.");
  }
  bool sameUnqualified(const Type &A, const Type &B) {
    return A.Kind == B.Kind && A.RecordID == B.RecordID && A.Count == B.Count &&
           ((A.Kind != TypeKind::Pointer && A.Kind != TypeKind::Array) ||
            sameUnqualified(A.Elements[0], B.Elements[0]));
  }
  std::size_t storageUnits(const Type &T) {
    if (T.Kind == TypeKind::Record) {
      auto I = RecordStorageUnits.find(T.RecordID);
      return I == RecordStorageUnits.end() ? MaxProtocolNodes + 1 : I->second;
    }
    if (T.Kind == TypeKind::Array) {
      auto Element = storageUnits(T.Elements[0]);
      return Element > MaxProtocolNodes / T.Count ? MaxProtocolNodes + 1
                                                 : Element * T.Count;
    }
    return 1;
  }
  bool containsArray(const Type &T) {
    if (T.Kind == TypeKind::Array)
      return true;
    if (T.Kind == TypeKind::Record)
      return RecordHasArray.at(T.RecordID);
    return false;
  }
  bool lvalue(const Expr &E, const std::map<std::string, Type> &Storage,
              bool Write = true) {
    if (E.Kind == ExprKind::Var)
      return Storage.count(E.Name) || (!Write && Globals.count(E.Name)) ||
             error(E.Loc, "Global constants are not writable.");
    if (E.Kind == ExprKind::Member && E.Args.size() == 1)
      return lvalue(E.Args[0], Storage, Write);
    if (((E.Kind == ExprKind::Dereference && E.Args.size() == 1) ||
         (E.Kind == ExprKind::Index && E.Args.size() == 2)) &&
        E.Args[0].ValueType.Kind == TypeKind::Pointer)
      return !Write || !E.Args[0].ValueType.PointeeConst ||
             error(E.Loc, "Const pointee storage is not writable.");
    return error(
        E.Loc,
        "Assignment target must be rooted in mutable local/parameter storage.");
  }
  bool function(const Function &F, bool Definition = true) {
    std::map<std::string, Type> Storage;
    std::set<std::string> Locals;
    for (const auto *Vars : {&F.Params, &F.Locals}) {
      for (const auto &V : *Vars) {
        if (!loc(V.Loc) || !name(V.Name, V.Loc, true) ||
            !type(V.ValueType, V.Loc))
          return false;
        if (Vars == &F.Params && V.ValueType.Kind == TypeKind::Array)
          return error(V.Loc, "Array parameters must use their adjusted pointer type.");
        if (Symbols.count(V.Name) ||
            !Storage.emplace(V.Name, V.ValueType).second)
          return error(V.Loc, "Duplicate or shadowing storage identifier.");
        if (Vars == &F.Locals)
          Locals.insert(V.Name);
      }
    }
    if (!Definition)
      return true;
    std::set<std::string> Labels;
    for (const auto &I : F.Body) {
      if (++Nodes > MaxProtocolNodes)
        return error(I.Loc, "IR instruction limit exceeded.");
      if (I.Op == InstructionKind::Label &&
          (!name(I.Label, I.Loc, true) || !Labels.insert(I.Label).second))
        return error(I.Loc, "Invalid or duplicate function-local label.");
    }
    if (F.Body.empty() || F.Body.front().Op != InstructionKind::Label)
      return error(F.Loc, "Function body must begin with a label.");
    bool Terminated = true;
    for (const auto &I : F.Body) {
      if (!loc(I.Loc))
        return false;
      if (I.Op != InstructionKind::MappedCall && !I.MappingID.empty())
        return error(
            I.Loc,
            "Mapping identity is valid only on an explicit mapped call.");
      if (I.Op == InstructionKind::Label) {
        if (!Terminated)
          return error(I.Loc, "Basic block has implicit fallthrough.");
        Terminated = false;
        continue;
      }
      if (Terminated)
        return error(I.Loc, "Instruction appears after a block terminator.");
      auto Check = [&](const std::optional<Expr> &E) {
        return !E || expr(*E, Storage);
      };
      if (!Check(I.Target) || !Check(I.Value) || !Check(I.Condition))
        return false;
      for (const auto &A : I.Args)
        if (!expr(A, Storage))
          return false;
      switch (I.Op) {
      case InstructionKind::Assign:
        if (!I.Target || !I.Value || I.Condition || !I.Args.empty() ||
            I.Target->ValueType != I.Value->ValueType ||
            I.Target->ValueType.Kind == TypeKind::Array)
          return error(I.Loc,
                       "Assignment requires equally typed target and value.");
        if (!lvalue(*I.Target, Storage))
          return false;
        break;
      case InstructionKind::MappedCall:
        if (!Math || !Mappings.count(I.MappingID) || !I.Callee.empty() ||
            I.Value || I.Condition || I.Args.size() != 1 ||
            I.Args[0].ValueType.Kind != TypeKind::Double || !I.Target ||
            I.Target->Kind != ExprKind::Var || !Locals.count(I.Target->Name) ||
            I.Target->ValueType.Kind != TypeKind::Double)
          return error(I.Loc, "Mapped call requires an authorized operation "
                              "and exact double argument/local result.");
        UsedMappings.insert(I.MappingID);
        break;
      case InstructionKind::Call: {
        if (!I.MappingID.empty())
          return error(I.Loc,
                       "Ordinary calls cannot carry mapping identities.");
        auto Callee = Functions.find(I.Callee);
        if (Callee == Functions.end())
          return error(I.Loc, "Call has no selected definition: " + I.Callee);
        const Function &CF = *Callee->second;
        if (I.Value || I.Condition || I.Args.size() != CF.Params.size())
          return error(I.Loc, "Call operand or argument count mismatch.");
        for (std::size_t N = 0; N < I.Args.size(); ++N)
          if (I.Args[N].ValueType != CF.Params[N].ValueType)
            return error(I.Loc, "Call argument type mismatch.");
        if (CF.Result.Kind == TypeKind::Void) {
          if (I.Target)
            return error(I.Loc, "Void call cannot have result storage.");
        } else if (!I.Target || I.Target->Kind != ExprKind::Var ||
                   !Locals.count(I.Target->Name) ||
                   I.Target->ValueType != CF.Result)
          return error(I.Loc,
                       "Nonvoid call requires matching local result storage.");
        break;
      }
      case InstructionKind::Jump:
        if (!Labels.count(I.Label) || I.Target || I.Value || I.Condition ||
            !I.Args.empty())
          return error(I.Loc, "Jump has an unknown label or invalid operands.");
        Terminated = true;
        break;
      case InstructionKind::Branch:
        if (!I.Condition || I.Condition->ValueType.Kind != TypeKind::Bool ||
            !Labels.count(I.TrueLabel) || !Labels.count(I.FalseLabel) ||
            I.Target || I.Value || !I.Args.empty())
          return error(
              I.Loc, "Branch requires bool condition and known destinations.");
        Terminated = true;
        break;
      case InstructionKind::Return:
        if (I.Target || I.Condition || !I.Args.empty() ||
            (F.Result.Kind == TypeKind::Void
                 ? bool(I.Value)
                 : !I.Value || I.Value->ValueType != F.Result))
          return error(I.Loc,
                       "Return value does not match the function result type.");
        Terminated = true;
        break;
      default:
        return error(I.Loc, "Unknown instruction operation.");
      }
    }
    return Terminated || error(F.Loc, "Final basic block lacks a terminator.");
  }

  bool mathMetadata(const SourceLocation &L, const llvm::Triple &T) {
    if (!Math)
      return (M.FPContractID.empty() && M.SDKDistributionID.empty() &&
              M.SDKCatalogSHA256.empty() && M.SDKDependencies.empty() &&
              M.Mappings.empty()) ||
             error(L, "Math metadata is forbidden outside cpp-math-v1.");
    auto Hash = [](llvm::StringRef S) {
      return S.size() == 64 && std::all_of(S.begin(), S.end(), [](char C) {
               return (C >= '0' && C <= '9') || (C >= 'a' && C <= 'f');
             });
    };
    auto Approved = [](const std::vector<std::string> &IDs,
                       const std::string &ID) {
      return std::find(IDs.begin(), IDs.end(), ID) != IDs.end();
    };
    if (M.FPContractID != CppMathFPContractID ||
        M.FPContractID != Context.FPContractID || M.SDKDistributionID.empty() ||
        !Approved(Context.ApprovedSDKIDs, M.SDKDistributionID) ||
        !Hash(M.SDKCatalogSHA256))
      return error(L, "Math requires the driver-approved SDK and explicit "
                      "binary64 floating-point contract.");
    if (!T.isMacOSX() || (T.getArch() != llvm::Triple::aarch64 &&
                          T.getArch() != llvm::Triple::x86_64))
      return fail(D, "TR0204", L, "math target",
                  "Initial binary64 mappings require a validated macOS "
                  "arm64/x86_64 target.");
    std::map<std::pair<std::string, std::string>, std::string> Dependencies;
    for (const auto &Dep : M.SDKDependencies) {
      if ((Dep.Root != "libcxx" && Dep.Root != "resource" &&
           Dep.Root != "platform") ||
          !relativePath(Dep.Path) || !Hash(Dep.SHA256) ||
          !Dependencies.emplace(std::make_pair(Dep.Root, Dep.Path), Dep.SHA256)
               .second)
        return error(L, "Invalid or duplicate named SDK dependency.");
    }
    if (!M.Mappings.empty() && !Dependencies.count({"libcxx", "cmath"}))
      return error(L, "Mapped std math operations require consumed "
                      "libcxx/cmath provenance.");
    for (const auto &Mapping : M.Mappings) {
      const auto &O = Mapping.Origin;
      auto Dep = Dependencies.find({O.Root, O.Path});
      if (!findMappingSpec(Mapping.ID) ||
          !Approved(Context.ApprovedMappingIDs, Mapping.ID) ||
          !Mappings.insert(Mapping.ID).second || !Hash(Mapping.DeclarationID) ||
          Mapping.Result != Type{TypeKind::Double, {}} ||
          Mapping.Parameters.size() != 1 ||
          Mapping.Parameters[0] != Type{TypeKind::Double, {}} || !O.Line ||
          !O.Column || Dep == Dependencies.end() || Dep->second != O.SHA256)
        return error(L, "Mapping evidence lacks an approved operation, exact "
                        "signature, or consumed SDK declaration origin.");
    }
    return true;
  }

  bool carrierLayout(const SourceLocation &L) {
    if (M.Profile != "cpp-core-v2")
      return !M.Target.Carriers ||
             error(L, "Carrier layout evidence requires core v2.");
    if (!M.Target.Carriers || !Context.ExpectedCarrierLayout)
      return error(L, "Core v2 requires independent carrier layout evidence.");
    const auto &Layout = *M.Target.Carriers;
    const uint32_t Widths[] = {0, 8, 8, 16, 16, 32, 32, 64, 64,
                               M.Target.PointerBits};
    if (Layout.CharBits != 8 ||
        Layout != *Context.ExpectedCarrierLayout)
      return error(L, "Source and NeverC carrier layouts disagree.");
    for (size_t I = 0; I < Layout.Carriers.size(); ++I) {
      const auto &C = Layout.Carriers[I];
      if (C.SizeBits < 8 || C.SizeBits > 64 ||
          (C.SizeBits & (C.SizeBits - 1)) ||
          (Widths[I] && C.SizeBits != Widths[I]) ||
          C.ABIAlignBits < 8 || C.ABIAlignBits > C.SizeBits ||
          (C.ABIAlignBits & (C.ABIAlignBits - 1)))
        return error(L, "Unsupported carrier size or ABI alignment.");
    }
    return true;
  }
  // Current records have only ordinary fields, with no bases, packing,
  // bitfields, custom alignment or lifetime-managed subobjects. Reconstruct
  // their natural layout from independently verified carriers. Pointer layout
  // never traverses its pointee, so recursive record pointers are bounded.
  static constexpr uint64_t MaxLayoutBits = uint64_t(MaxProtocolNodes) * 128;
  std::optional<StorageLayout> storageLayout(const Type &T) const {
    const auto &C = Context.ExpectedCarrierLayout->Carriers;
    switch (T.Kind) {
    case TypeKind::Bool: return C[0];
    case TypeKind::Int: return C[5];
    case TypeKind::UInt: return C[6];
    case TypeKind::Pointer: return C[9];
    case TypeKind::Record: {
      auto It = Records.find(T.RecordID);
      if (It != Records.end() && It->second->Layout)
        return It->second->Layout->Storage;
      return std::nullopt;
    }
    case TypeKind::Array: {
      auto Element = storageLayout(T.Elements[0]);
      if (!Element || uint64_t(Element->SizeBits) * T.Count > MaxLayoutBits)
        return std::nullopt;
      return StorageLayout{Element->SizeBits * T.Count, Element->ABIAlignBits};
    }
    default: return std::nullopt;
    }
  }
  bool recordLayout(const Record &R) {
    if (M.Profile != "cpp-core-v2")
      return !R.Layout || error(R.Loc, "Record layout evidence requires core v2.");
    if (!R.Layout || R.Layout->FieldOffsetsBits.size() != R.Fields.size())
      return error(R.Loc, "Missing record layout or mismatched field offsets.");
    uint64_t End = 0;
    uint32_t Align = 8;
    for (size_t I = 0; I < R.Fields.size(); ++I) {
      auto Field = storageLayout(R.Fields[I].ValueType);
      if (!Field)
        return error(R.Loc, "Record layout exceeds the storage budget.");
      Align = std::max(Align, Field->ABIAlignBits);
      uint64_t Offset = (End + Field->ABIAlignBits - 1) /
                        Field->ABIAlignBits * Field->ABIAlignBits;
      End = Offset + Field->SizeBits;
      if (End > MaxLayoutBits || R.Layout->FieldOffsetsBits[I] != Offset)
        return error(R.Loc, "Record field offset disagrees with target layout.");
    }
    End = (End + Align - 1) / Align * Align;
    if (End > MaxLayoutBits || R.Layout->Storage.SizeBits != End ||
        R.Layout->Storage.ABIAlignBits != Align)
      return error(R.Loc, "Record size or alignment disagrees with target layout.");
    return true;
  }

public:
  Verifier(const Module &M, const VerificationContext &C, Diagnostics &D)
      : M(M), Context(C), D(D) {}
  Verifier(const Module &M, const VerificationContext &C,
           llvm::ArrayRef<Function> F, llvm::ArrayRef<Variable> G,
           Diagnostics &D)
      : M(M), Context(C), D(D), Project(true), Math(M.Profile == "cpp-math-v1"),
        FunctionDeclarations(F), GlobalDeclarations(G) {}
  bool run() {
    SourceLocation Anchor{M.Dependencies.empty() ? "<frontend>"
                                                 : M.Dependencies.front().Path,
                          1, 1};
    if (M.Protocol != FrontendProtocolMajor ||
        M.Frontend.Name != CppFrontendName ||
        M.Frontend.Version != CppFrontendVersion || M.Frontend.Build.empty())
      return fail(D, "TR0103", Anchor, "frontend identity",
                  "Incompatible protocol or frontend identity/version.");
    const bool SupportedProfile =
        Project ? M.Profile == (Math ? "cpp-math-v1" : "cpp-project-v1")
                : (M.Profile == "cpp-core-v1" || M.Profile == "cpp-core-v2");
    if (!SupportedProfile || M.Profile != Context.Profile)
      return fail(D, "TR0003", Anchor, "translation profile",
                  "Unsupported or mismatched semantic profile.");
    llvm::Triple T(llvm::Triple::normalize(M.Target.Triple));
    llvm::Triple Requested(llvm::Triple::normalize(Context.TargetTriple));
    if (M.Target.Triple.empty() || Context.TargetTriple.empty() ||
        T != Requested || M.Target.IntBits != 32 ||
        M.Target.IntBits != Context.IntBits ||
        (M.Target.PointerBits != 32 && M.Target.PointerBits != 64) ||
        M.Target.PointerBits != Context.PointerBits ||
        M.Target.LittleEndian != Context.LittleEndian ||
        !M.Target.LittleEndian ||
        M.Target.PointerBits != (T.isArch64Bit() ? 64u : 32u) ||
        (T.getArch() != llvm::Triple::aarch64 &&
         T.getArch() != llvm::Triple::x86_64 &&
         T.getArch() != llvm::Triple::x86) ||
        (!T.isMacOSX() && !(T.isOSLinux() && !T.isAndroid()) &&
         !T.isOSWindows()))
      return fail(D, "TR0204", Anchor, "target data model",
                  "Frontend and requested native target/data model disagree or "
                  "are unsupported.");
    if (!carrierLayout(Anchor) || !mathMetadata(Anchor, T))
      return false;
    if (M.Dependencies.empty())
      return error(Anchor, "Module must record its input dependency.");
    for (const auto &Dep : M.Dependencies) {
      if (!relativePath(Dep.Path) || !Paths.insert(Dep.Path).second ||
          Dep.SHA256.size() != 64 ||
          !std::all_of(Dep.SHA256.begin(), Dep.SHA256.end(), [](char C) {
            return (C >= '0' && C <= '9') || (C >= 'a' && C <= 'f');
          }))
        return error(Anchor,
                     "Invalid/duplicate dependency path or SHA-256 digest.");
    }
    if (!Project && M.Dependencies.size() != 1)
      return error(Anchor,
                   "The core profile accepts exactly one source dependency.");
    for (const auto &R : M.Records)
      KnownRecords.insert(R.ID);
    for (const auto &R : M.Records) {
      if (!loc(R.Loc) || !name(R.ID, R.Loc, true) ||
          !Symbols.insert(R.ID).second)
        return error(R.Loc, "Invalid or duplicate record identifier.");
      if (R.Fields.empty())
        return error(R.Loc, "Empty records are outside the core profile.");
      std::set<std::string> Fields;
      for (const auto &F : R.Fields)
        if (!name(F.Name, R.Loc) || !Fields.insert(F.Name).second ||
            !type(F.ValueType, R.Loc))
          return error(R.Loc, "Invalid/duplicate field or forward/cyclic "
                              "by-value record dependency.");
      if (!recordLayout(R))
        return false;
      Records.emplace(R.ID, &R);
      std::size_t Units = 0;
      bool HasArray = false;
      for (const auto &F : R.Fields)
        HasArray |= containsArray(F.ValueType);
      for (const auto &F : R.Fields) {
        auto Added = storageUnits(F.ValueType);
        if (Added > MaxProtocolNodes - Units) {
          Units = MaxProtocolNodes + 1;
          break;
        }
        Units += Added;
      }
      RecordStorageUnits.emplace(R.ID, Units);
      RecordHasArray.emplace(R.ID, HasArray);
    }
    for (const auto &G : M.Globals) {
      if (!loc(G.Loc) || !name(G.Name, G.Loc, true) ||
          !type(G.ValueType, G.Loc) || G.ValueType.Kind == TypeKind::Pointer ||
          containsArray(G.ValueType) ||
          !Symbols.insert(G.Name).second)
        return error(G.Loc, "Invalid or duplicate global identifier/type.");
      Globals.emplace(G.Name, G.ValueType);
    }
    for (const auto &G : GlobalDeclarations) {
      if (Globals.count(G.Name))
        continue;
      if (!loc(G.Loc) || !name(G.Name, G.Loc, true) ||
          !type(G.ValueType, G.Loc) || !Symbols.insert(G.Name).second)
        return error(G.Loc, "Invalid global declaration.");
      Globals.emplace(G.Name, G.ValueType);
    }
    std::vector<const Function *> Signatures;
    std::set<std::string> DefinedFunctions;
    for (const auto &F : M.Functions) {
      Signatures.push_back(&F);
      DefinedFunctions.insert(F.Name);
    }
    for (const auto &F : FunctionDeclarations)
      if (!DefinedFunctions.count(F.Name))
        Signatures.push_back(&F);
    for (const auto *Signature : Signatures) {
      const auto &F = *Signature;
      if (!loc(F.Loc) || !name(F.Name, F.Loc, !F.CExport && F.Name != "main") ||
          !type(F.Result, F.Loc, true) || !Symbols.insert(F.Name).second)
        return error(F.Loc, "Invalid or duplicate function identifier/type.");
      if (F.Result.Kind == TypeKind::Array)
        return error(F.Loc, "Functions cannot return arrays by value.");
      if (F.CExport && llvm::StringRef(F.Name).starts_with("nct_"))
        return error(F.Loc, "C export collides with generated identifiers.");
      if (F.Name == "main" &&
          (F.Internal || F.Result.Kind != TypeKind::Int || !F.Params.empty()))
        return error(F.Loc, "Only external int main() is supported.");
      if (F.CExport) {
        if (F.Result.Kind != TypeKind::Void && !F.Result.isScalar())
          return error(F.Loc, "C exports require scalar result types.");
        for (const auto &P : F.Params)
          if (!P.ValueType.isScalar())
            return error(P.Loc, "C exports require scalar parameter types.");
      }
      Functions.emplace(F.Name, &F);
    }
    const std::map<std::string, Type> Empty;
    for (const auto &G : M.Globals)
      if (G.Value.ValueType != G.ValueType || !expr(G.Value, Empty, 0, true))
        return error(G.Loc, "Invalid folded global initializer.");
    for (const auto &F : FunctionDeclarations)
      if (!function(F, false))
        return false;
    for (const auto &F : M.Functions)
      if (!function(F))
        return false;
    // Source O2 folding of constant sNaN floor may omit INVALID, unlike a
    // runtime call. The admitted source subset cannot construct NaN constants.
    // Reject the whole combination, covering aliases and cross-unit merging.
    if (HasSignalingNaNLiteral && Mappings.count("cpp.math.floor.f64.v1"))
      return error(Anchor, "The initial math profile excludes floor mappings "
                           "in modules containing signaling-NaN literals.");
    return UsedMappings == Mappings ||
           error(Anchor,
                 "Mapping evidence must correspond to an emitted mapped call.");
  }
};
} // namespace

bool parseModule(llvm::StringRef JSON, Module &Out, Diagnostics &D) {
  Parser P(D);
  if (JSON.size() > MaxFrontendResponseBytes)
    return P.error("Frontend response exceeds 32 MiB.");
  if (!jsonWithinLimits(JSON, MaxFrontendResponseBytes, MaxProtocolDepth))
    return P.error("Frontend JSON is unbalanced or exceeds structural depth.");
  auto V = llvm::json::parse(JSON);
  if (!V)
    return P.error(llvm::toString(V.takeError()));
  const auto *O = V->getAsObject();
  if (!O)
    return P.error("Frontend response must be an object.");
  Module Parsed;
  if (!P.module(*O, Parsed))
    return false;
  Out = std::move(Parsed);
  return true;
}

bool verifyModule(const Module &M, const VerificationContext &Context,
                  Diagnostics &D) {
  return Verifier(M, Context, D).run();
}
bool detail::verifyProjectDefinitions(const Module &M,
                                      const VerificationContext &C,
                                      llvm::ArrayRef<Function> F,
                                      llvm::ArrayRef<Variable> G,
                                      Diagnostics &D) {
  return Verifier(M, C, F, G, D).run();
}
} // namespace neverc::translate
