#ifndef NEVERC_TRANSLATE_TRANSLATEIR_H
#define NEVERC_TRANSLATE_TRANSLATEIR_H

#include "Diagnostics.h"
#include "FrontendProtocol.h"
#include "llvm/ADT/StringRef.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace neverc::translate {

enum class TypeKind { Int, UInt, Bool, Void, Record, Double, Pointer, Array };
struct Type {
  TypeKind Kind = TypeKind::Void;
  std::string RecordID;
  // A pointer owns exactly one component; other types have none. Keeping the
  // tree value-owned prevents recursive record pointers from forming cycles.
  std::vector<Type> Elements;
  bool PointeeConst = false;
  uint32_t Count = 0;
  // Zero preserves the canonical legacy int/uint (32-bit) representation.
  uint32_t IntegerBits = 0;
  bool operator==(const Type &Other) const {
    return Kind == Other.Kind && RecordID == Other.RecordID &&
           Elements == Other.Elements && PointeeConst == Other.PointeeConst &&
           Count == Other.Count && IntegerBits == Other.IntegerBits;
  }
  bool operator!=(const Type &Other) const { return !(*this == Other); }
  bool isInteger() const {
    return Kind == TypeKind::Int || Kind == TypeKind::UInt;
  }
  unsigned integerBits() const { return IntegerBits ? IntegerBits : 32; }
  bool isSignedInteger() const { return Kind == TypeKind::Int; }
  bool isPromotedInteger() const { return isInteger() && integerBits() >= 32; }
  bool isScalar() const {
    return isInteger() || Kind == TypeKind::Bool || Kind == TypeKind::Double ||
           Kind == TypeKind::Pointer;
  }
};
std::string typeName(const Type &T);

enum class ExprKind {
  Literal, Var, Unary, Binary, Cast, Member, Aggregate, Null, Address, Dereference,
  ArrayDecay, Index
};
enum class UnaryOperator { Plus, Minus, BitNot, LogicalNot };
enum class BinaryOperator {
  Add,
  Subtract,
  Multiply,
  Divide,
  Remainder,
  ShiftLeft,
  ShiftRight,
  BitAnd,
  BitOr,
  BitXor,
  Equal,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual
};
struct Expr {
  ExprKind Kind = ExprKind::Literal;
  Type ValueType;
  SourceLocation Loc;
  std::string Name;
  std::string Integer;
  uint64_t Binary64Bits = 0;
  bool Boolean = false;
  UnaryOperator UnaryOp = UnaryOperator::Plus;
  BinaryOperator BinaryOp = BinaryOperator::Add;
  std::vector<Expr> Args;
};

enum class InstructionKind {
  Assign,
  Call,
  Label,
  Jump,
  Branch,
  Return,
  MappedCall
};
struct Instruction {
  InstructionKind Op = InstructionKind::Return;
  SourceLocation Loc;
  std::optional<Expr> Target;
  std::optional<Expr> Value;
  std::optional<Expr> Condition;
  std::string Callee;
  std::string MappingID;
  std::vector<Expr> Args;
  std::string Label;
  std::string TrueLabel;
  std::string FalseLabel;
};

struct Variable {
  std::string Name;
  Type ValueType;
  SourceLocation Loc;
};
struct Field {
  std::string Name;
  Type ValueType;
};
// Ordered carrier slots are shared by protocol validation and NC guards.
inline constexpr std::array<const char *, 10> CarrierNames{
    "bool", "i8", "u8", "i16", "u16", "int", "uint", "i64", "u64",
    "default-pointer"};
inline constexpr std::array<const char *, 10> CarrierSpellings{
    "bool", "signed char", "unsigned char", "short", "unsigned short", "int",
    "unsigned int", "long long", "unsigned long long", "void *"};
struct StorageLayout {
  uint32_t SizeBits = 0, ABIAlignBits = 0;
  bool operator==(const StorageLayout &Other) const {
    return SizeBits == Other.SizeBits && ABIAlignBits == Other.ABIAlignBits;
  }
  bool operator!=(const StorageLayout &Other) const { return !(*this == Other); }
};
struct CarrierLayout {
  uint32_t CharBits = 8;
  std::array<StorageLayout, CarrierNames.size()> Carriers{};
  bool operator==(const CarrierLayout &Other) const {
    return CharBits == Other.CharBits && Carriers == Other.Carriers;
  }
  bool operator!=(const CarrierLayout &Other) const { return !(*this == Other); }
};
struct RecordLayout {
  StorageLayout Storage;
  std::vector<uint32_t> FieldOffsetsBits;
};
struct Record {
  std::string ID;
  std::vector<Field> Fields;
  SourceLocation Loc;
  std::optional<RecordLayout> Layout;
};
struct Global {
  std::string Name;
  Type ValueType;
  Expr Value;
  SourceLocation Loc;
  bool Mutable = false;
};
struct Function {
  std::string Name;
  Type Result;
  bool Internal = false;
  bool CExport = false;
  std::vector<Variable> Params;
  std::vector<Variable> Locals;
  std::vector<Instruction> Body;
  SourceLocation Loc;
};

struct FrontendIdentity {
  std::string Name;
  std::string Version;
  std::string Build;
};
struct TargetInfo {
  std::string Triple;
  uint32_t IntBits = 32;
  uint32_t PointerBits = 0;
  bool LittleEndian = true;
  std::optional<CarrierLayout> Carriers;
};
struct Dependency {
  std::string Path;
  std::string SHA256;
};
struct Export {
  std::string Name;
  std::string Result;
  std::vector<std::string> Parameters;
  bool CExport = false;
};
inline constexpr const char *CppMathFPContractID =
    "cpp.math.binary64.masked.v1";
struct SDKDependency {
  std::string Root, Path, SHA256;
};
struct SDKSourceLocation {
  std::string Root, Path, SHA256;
  uint32_t Line = 1, Column = 1;
};
struct MappingEvidence {
  std::string ID, DeclarationID;
  Type Result;
  std::vector<Type> Parameters;
  SDKSourceLocation Origin;
};
struct MappingSpec {
  const char *ID, *RuntimeSymbol, *RuntimeModule, *RequiredHeader;
};
const MappingSpec *findMappingSpec(llvm::StringRef ID);
struct Module {
  uint32_t Protocol = FrontendProtocolMajor;
  std::string Profile;
  FrontendIdentity Frontend;
  TargetInfo Target;
  std::vector<Dependency> Dependencies;
  std::vector<Record> Records;
  std::vector<Global> Globals;
  std::vector<Function> Functions;
  std::vector<Export> Exports;
  std::string FPContractID, SDKDistributionID, SDKCatalogSHA256;
  std::vector<SDKDependency> SDKDependencies;
  std::vector<MappingEvidence> Mappings;
};

struct VerificationContext {
  std::string Profile = "cpp-core-v1";
  std::string TargetTriple;
  uint32_t IntBits = 32;
  uint32_t PointerBits = 0;
  bool LittleEndian = true;
  std::string FPContractID;
  std::vector<std::string> ApprovedSDKIDs, ApprovedMappingIDs;
  // Constructed independently from NeverC target options, never from Module.
  std::optional<CarrierLayout> ExpectedCarrierLayout;
  // Zero unless the consumer independently found a signed native ptrdiff type.
  uint32_t ExpectedPtrDiffBits = 0;
};
struct SourceMapEntry {
  uint32_t BeginLine = 1;
  uint32_t EndLine = 1;
  SourceLocation Original;
};
struct EmittedSource {
  std::string Text;
  std::vector<SourceMapEntry> Map;
};

// Outputs are replaced only on success. Parse failures have protocol
// diagnostics; semantic/target failures have verification diagnostics. No
// function touches disk.
bool parseModule(llvm::StringRef JSON, Module &Out, Diagnostics &D);
bool verifyModule(const Module &M, const VerificationContext &Context,
                  Diagnostics &D);
// Defensively verifies before producing any source, including synthetic
// modules.
bool emitNC(const Module &M, const VerificationContext &Context,
            EmittedSource &Out, Diagnostics &D);

} // namespace neverc::translate

#endif
