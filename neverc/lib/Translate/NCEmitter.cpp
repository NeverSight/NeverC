#include "TranslateIR.h"
#include "TranslateIRInternal.h"
#include "llvm/TargetParser/Triple.h"
#include <map>
#include <set>
#include <utility>

namespace neverc::translate {
namespace {
std::string integerCarrier(const Type &T) {
  unsigned Bits = T.integerBits();
  unsigned Slot = Bits == 8 ? 1 : Bits == 16 ? 3 : Bits == 32 ? 5 : 7;
  return CarrierSpellings[Slot + (T.Kind == TypeKind::UInt)];
}
std::string unsignedCarrier(unsigned Bits) {
  Type T{TypeKind::UInt, {}};
  T.IntegerBits = Bits == 32 ? 0 : Bits;
  return integerCarrier(T);
}
std::string signedCarrier(unsigned Bits) {
  Type T{TypeKind::Int, {}};
  T.IntegerBits = Bits == 32 ? 0 : Bits;
  return integerCarrier(T);
}
std::string conversionHelper(unsigned Bits) {
  return "nct_emit_u" + std::to_string(Bits) + "_to_i" + std::to_string(Bits);
}
std::string shiftHelper(unsigned Bits) {
  return "nct_emit_i" + std::to_string(Bits) + "_shr";
}
bool needsSignedConversion(const Type &From, const Type &To) {
  return To.isSignedInteger() && From.isInteger() &&
         (From.integerBits() > To.integerBits() ||
          (From.Kind == TypeKind::UInt && From.integerBits() == To.integerBits()));
}
std::string declaration(const Type &T, std::string Name, bool Const = false) {
  if (T.Kind == TypeKind::Pointer) {
    Name = std::string(Const ? "*const" : "*") +
           (Name.empty() ? "" : " " + Name);
    if (T.Elements[0].Kind == TypeKind::Array)
      Name = "(" + Name + ")";
    return declaration(T.Elements[0], std::move(Name), T.PointeeConst);
  }
  if (T.Kind == TypeKind::Array)
    return declaration(T.Elements[0],
                       Name + "[" + std::to_string(T.Count) + "]", Const);
  return std::string(Const ? "const " : "") +
         (T.isInteger() ? integerCarrier(T) : typeName(T)) +
         (Name.empty() ? "" : " " + Name);
}
std::string cType(const Type &T) {
  return declaration(T, {});
}
std::string hexadecimal(uint64_t Bits, size_t Digits) {
  static const char Hex[] = "0123456789abcdef";
  std::string S(Digits, '0');
  for (size_t I = Digits; I != 0; --I) {
    S[I - 1] = Hex[Bits & 15];
    Bits >>= 4;
  }
  return S;
}
std::string binary64Literal(uint64_t Bits) {
  const bool Negative = (Bits >> 63) != 0;
  const unsigned Exponent = unsigned((Bits >> 52) & 2047);
  const uint64_t Fraction = Bits & UINT64_C(0x000fffffffffffff);
  std::string S;
  if (Exponent == 2047) {
    if (!Fraction)
      S = "__builtin_huge_val()";
    else {
      const bool Quiet = (Fraction & UINT64_C(0x0008000000000000)) != 0;
      S = std::string(Quiet ? "__builtin_nan(\"0x" : "__builtin_nans(\"0x") +
          hexadecimal(Fraction & UINT64_C(0x0007ffffffffffff), 13) + "\")";
    }
  } else if (Exponent == 0 && Fraction == 0)
    S = "0x0p+0";
  else {
    int Power = Exponent ? int(Exponent) - 1023 : -1022;
    S = std::string(Exponent ? "0x1." : "0x0.") + hexadecimal(Fraction, 13) +
        "p" + (Power >= 0 ? "+" : "") + std::to_string(Power);
  }
  return Negative ? "(-" + S + ")" : S;
}
const char *binarySpelling(BinaryOperator Op) {
  switch (Op) {
  case BinaryOperator::Add:
    return "+";
  case BinaryOperator::Subtract:
    return "-";
  case BinaryOperator::Multiply:
    return "*";
  case BinaryOperator::Divide:
    return "/";
  case BinaryOperator::Remainder:
    return "%";
  case BinaryOperator::ShiftLeft:
    return "<<";
  case BinaryOperator::ShiftRight:
    return ">>";
  case BinaryOperator::BitAnd:
    return "&";
  case BinaryOperator::BitOr:
    return "|";
  case BinaryOperator::BitXor:
    return "^";
  case BinaryOperator::Equal:
    return "==";
  case BinaryOperator::NotEqual:
    return "!=";
  case BinaryOperator::Less:
    return "<";
  case BinaryOperator::LessEqual:
    return "<=";
  case BinaryOperator::Greater:
    return ">";
  case BinaryOperator::GreaterEqual:
    return ">=";
  }
  return "";
}

class Emitter {
  const Module &M;
  EmittedSource Output;
  uint32_t Line = 1;
  std::set<unsigned> SignedConversions, ArithmeticShifts;
  struct PointerHelper {
    Type Left, Right, Result;
    BinaryOperator Op;
    std::string Name;
  };
  std::vector<PointerHelper> PointerHelpers;
  std::map<std::string, size_t> PointerHelperIndices;
  bool HasPointerDifference = false;

  std::string pointerKey(BinaryOperator Op, const Type &Left, const Type &Right,
                         const Type &Result) {
    return typeName(Left) + " " + binarySpelling(Op) + " " + typeName(Right) +
           " " + typeName(Result);
  }
  void inspectPointer(BinaryOperator Op, const Type &Left, const Type &Right,
                      const Type &Result) {
    auto Key = pointerKey(Op, Left, Right, Result);
    if (!PointerHelperIndices.emplace(Key, PointerHelpers.size()).second)
      return;
    PointerHelpers.push_back(
        {Left, Right, Result, Op,
         "nct_emit_pointer_" + std::to_string(PointerHelpers.size())});
    HasPointerDifference |= Left.Kind == TypeKind::Pointer &&
                            Right.Kind == TypeKind::Pointer;
  }
  std::string pointerCall(BinaryOperator Op, const Expr &Left, const Expr &Right,
                          const Type &Result) {
    auto Index = PointerHelperIndices.at(
        pointerKey(Op, Left.ValueType, Right.ValueType, Result));
    return PointerHelpers[Index].Name + "(" + expression(Left) + ", " +
           expression(Right) + ")";
  }

  void line(const std::string &Text, const SourceLocation *Loc = nullptr) {
    Output.Text += Text;
    Output.Text += '\n';
    if (Loc)
      Output.Map.push_back({Line, Line, *Loc});
    ++Line;
  }
  void inspect(const Expr &E) {
    if (E.Kind == ExprKind::Binary &&
        (E.BinaryOp == BinaryOperator::Add ||
         E.BinaryOp == BinaryOperator::Subtract) &&
        (E.Args[0].ValueType.Kind == TypeKind::Pointer ||
         E.Args[1].ValueType.Kind == TypeKind::Pointer))
      inspectPointer(E.BinaryOp, E.Args[0].ValueType, E.Args[1].ValueType,
                     E.ValueType);
    if (E.Kind == ExprKind::Address && E.Args[0].Kind == ExprKind::Index) {
      const auto &Index = E.Args[0];
      inspectPointer(BinaryOperator::Add, Index.Args[0].ValueType,
                     Index.Args[1].ValueType, E.ValueType);
    }
    if ((E.Kind == ExprKind::Cast &&
         needsSignedConversion(E.Args[0].ValueType, E.ValueType)) ||
        (E.Kind == ExprKind::Binary &&
         E.BinaryOp == BinaryOperator::ShiftLeft && E.ValueType.isSignedInteger()))
      SignedConversions.insert(E.ValueType.integerBits());
    if (E.Kind == ExprKind::Binary &&
        E.BinaryOp == BinaryOperator::ShiftRight && E.ValueType.isSignedInteger())
      ArithmeticShifts.insert(E.ValueType.integerBits());
    for (const auto &A : E.Args)
      inspect(A);
  }
  std::string expression(const Expr &E, bool Initializer = false) {
    switch (E.Kind) {
    case ExprKind::Null:
      return "((" + cType(E.ValueType) + ")0)";
    case ExprKind::Address:
      if (E.Args[0].Kind == ExprKind::Index) {
        const auto &Index = E.Args[0];
        return pointerCall(BinaryOperator::Add, Index.Args[0], Index.Args[1],
                           E.ValueType);
      }
      if (E.Args[0].Kind == ExprKind::Dereference)
        return "((" + cType(E.ValueType) + ")(" +
               expression(E.Args[0].Args[0]) + "))";
      return "(&(" + expression(E.Args[0]) + "))";
    case ExprKind::Dereference:
      return "(*(" + expression(E.Args[0]) + "))";
    case ExprKind::ArrayDecay:
      return "(&((" + expression(E.Args[0]) + ")[0]))";
    case ExprKind::Index:
      return "((" + expression(E.Args[0]) + ")[" + expression(E.Args[1]) + "])";
    case ExprKind::Literal:
      if (E.ValueType.Kind == TypeKind::Bool)
        return E.Boolean ? "true" : "false";
      if (E.ValueType.Kind == TypeKind::Double)
        return binary64Literal(E.Binary64Bits);
      if (E.ValueType.integerBits() < 32)
        return "((" + cType(E.ValueType) + ")(" + E.Integer + "))";
      if (E.ValueType.integerBits() == 64) {
        if (E.ValueType.Kind == TypeKind::UInt)
          return E.Integer + "ull";
        if (E.Integer == "-9223372036854775808")
          return "(-9223372036854775807ll - 1ll)";
        return "(" + E.Integer + "ll)";
      }
      if (E.ValueType.Kind == TypeKind::UInt)
        return E.Integer + "u";
      if (E.Integer == "-2147483648")
        return "(-2147483647 - 1)";
      return "(" + E.Integer + ")";
    case ExprKind::Var:
      return E.Name;
    case ExprKind::Unary: {
      const char *Op = E.UnaryOp == UnaryOperator::Plus     ? "+"
                       : E.UnaryOp == UnaryOperator::Minus  ? "-"
                       : E.UnaryOp == UnaryOperator::BitNot ? "~"
                                                            : "!";
      std::string Text =
          "(" + std::string(Op) + "(" + expression(E.Args[0]) + "))";
      return E.UnaryOp == UnaryOperator::LogicalNot ? "((bool)" + Text + ")"
                                                    : Text;
    }
    case ExprKind::Binary: {
      if ((E.BinaryOp == BinaryOperator::Add ||
           E.BinaryOp == BinaryOperator::Subtract) &&
          (E.Args[0].ValueType.Kind == TypeKind::Pointer ||
           E.Args[1].ValueType.Kind == TypeKind::Pointer))
        return pointerCall(E.BinaryOp, E.Args[0], E.Args[1], E.ValueType);
      std::string A = expression(E.Args[0]), B = expression(E.Args[1]);
      auto Bits = E.ValueType.integerBits();
      if (E.ValueType.isSignedInteger() && E.BinaryOp == BinaryOperator::ShiftLeft)
        return conversionHelper(Bits) + "(((" + unsignedCarrier(Bits) + ")(" +
               A + ")) << (" + B + "))";
      if (E.ValueType.isSignedInteger() && E.BinaryOp == BinaryOperator::ShiftRight)
        return shiftHelper(Bits) + "(" + A + ", (" + unsignedCarrier(Bits) +
               ")(" + B + "))";
      std::string Text =
          "((" + A + ") " + binarySpelling(E.BinaryOp) + " (" + B + "))";
      return E.ValueType.Kind == TypeKind::Bool ? "((bool)" + Text + ")" : Text;
    }
    case ExprKind::Cast:
      if (needsSignedConversion(E.Args[0].ValueType, E.ValueType))
        return conversionHelper(E.ValueType.integerBits()) + "(" +
               expression(E.Args[0]) + ")";
      return "((" + cType(E.ValueType) + ")(" + expression(E.Args[0]) + "))";
    case ExprKind::Member:
      return "((" + expression(E.Args[0]) + ")." + E.Name + ")";
    case ExprKind::Aggregate: {
      Initializer |= E.ValueType.Kind == TypeKind::Array;
      std::string Text = Initializer ? "{" : "(" + cType(E.ValueType) + "){";
      for (std::size_t I = 0; I < E.Args.size(); ++I) {
        if (I)
          Text += ", ";
        Text += expression(E.Args[I], Initializer);
      }
      return Text + "}";
    }
    }
    return {};
  }
  std::string signature(const Function &F) {
    std::string S = F.Name + "(";
    if (F.Params.empty())
      S += "void";
    for (std::size_t I = 0; I < F.Params.size(); ++I) {
      if (I)
        S += ", ";
      S += declaration(F.Params[I].ValueType, F.Params[I].Name);
    }
    return std::string(F.Internal ? "static " : "") +
           declaration(F.Result, S + ")");
  }
  void guards() {
    llvm::Triple T(llvm::Triple::normalize(M.Target.Triple));
    line("/* Generated by neverc translate; profile " + M.Profile + ". */");
    const char *Arch = T.getArch() == llvm::Triple::aarch64  ? "__aarch64__"
                       : T.getArch() == llvm::Triple::x86_64 ? "__x86_64__"
                                                             : "__i386__";
    line("#if !defined(" + std::string(Arch) + ")");
    line("#error \"translated source requires its recorded target "
         "architecture\"");
    line("#endif");
    const char *OS = T.isMacOSX()      ? "__APPLE__"
                     : T.isOSWindows() ? "_WIN32"
                                       : "__linux__";
    line("#if !defined(" + std::string(OS) + ")");
    line("#error \"translated source requires its recorded target operating "
         "system\"");
    line("#endif");
    if (T.isMacOSX()) {
      line("#if !defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__) || "
           "defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__)");
      line("#error \"translated source requires a macOS hosted target\"");
      line("#endif");
    } else if (T.isOSLinux()) {
      line("#if defined(__ANDROID__)");
      line("#error \"translated source requires a Linux hosted target without "
           "Android ABI assumptions\"");
      line("#endif");
    }
    line("#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != "
         "__ORDER_LITTLE_ENDIAN__");
    line("#error \"translated source requires little-endian byte order\"");
    line("#endif");
    line("static_assert(__CHAR_BIT__ == 8, \"translated source requires 8-bit "
         "bytes\");");
    line("static_assert(sizeof(int) * __CHAR_BIT__ == 32, \"translated source "
         "requires 32-bit int\");");
    line("static_assert(sizeof(void *) * __CHAR_BIT__ == " +
         std::to_string(M.Target.PointerBits) +
         ", \"translated source requires its recorded pointer width\");");
    if (M.Profile == "cpp-core-v2")
      for (size_t I = 0; I < CarrierNames.size(); ++I) {
        const auto &C = M.Target.Carriers->Carriers[I];
        const std::string T = CarrierSpellings[I];
        line("static_assert(sizeof(" + T + ") * __CHAR_BIT__ == " +
             std::to_string(C.SizeBits) + ", \"translated carrier size mismatch\");");
        line("static_assert(alignof(" + T + ") * __CHAR_BIT__ == " +
             std::to_string(C.ABIAlignBits) +
             ", \"translated carrier alignment mismatch\");");
      }
    if (HasPointerDifference) {
      line("static_assert(sizeof(__typeof__((int *)0 - (int *)0)) * "
           "__CHAR_BIT__ == " +
           std::to_string(M.Target.PointerBits) +
           ", \"translated ptrdiff width mismatch\");");
      line("static_assert(((__typeof__((int *)0 - (int *)0))-1) < 0, "
           "\"translated ptrdiff must be signed\");");
    }
    if (M.Profile == "cpp-math-v1") {
      line("static_assert(sizeof(double) * __CHAR_BIT__ == 64, \"translated "
           "math requires binary64 storage\");");
      line("static_assert(__FLT_RADIX__ == 2 && __DBL_MANT_DIG__ == 53 && "
           "__DBL_MAX_EXP__ == 1024, \"translated math requires IEEE "
           "binary64\");");
      line("static_assert(__FLT_EVAL_METHOD__ == 0, \"translated math excludes "
           "excess floating precision\");");
      line("#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && "
           "__FINITE_MATH_ONLY__ != 0)");
      line("#error \"translated math requires the recorded strict "
           "floating-point policy\"");
      line("#endif");
      line("/* FP contract: masked traps, IEEE subnormals (no FTZ/DAZ); "
           "preserve mapped-call errno and flags. */");
    }
    line("");
  }
  void helpers() {
    for (unsigned Bits : SignedConversions) {
      const auto Signed = signedCarrier(Bits), Unsigned = unsignedCarrier(Bits);
      const uint64_t Max = Bits == 64 ? UINT64_MAX : (UINT64_C(1) << Bits) - 1;
      const std::string Suffix = Bits == 64 ? "ull" : "u";
      line("/* Pin the source frontend's two's-complement uint-to-int conversion. */");
      line("static " + Signed + " " + conversionHelper(Bits) + "(" + Unsigned +
           " nct_emit_u) {");
      line("  return nct_emit_u <= " + std::to_string(Max >> 1) + Suffix +
           " ? (" + Signed + ")nct_emit_u");
      line("      : -1 - (" + Signed + ")(" + std::to_string(Max) + Suffix +
           " - nct_emit_u);");
      line("}");
      line("");
    }
    for (unsigned Bits : ArithmeticShifts) {
      const auto Signed = signedCarrier(Bits), Unsigned = unsignedCarrier(Bits);
      line("/* Preserve arithmetic right shift without implementation-defined C behavior. */");
      line("static " + Signed + " " + shiftHelper(Bits) + "(" + Signed +
           " nct_emit_a, " + Unsigned + " nct_emit_b) {");
      line("  return nct_emit_a < 0 ? -1 - (" + Signed + ")((~(" + Unsigned +
           ")nct_emit_a) >> nct_emit_b)");
      line("      : (" + Signed + ")((" + Unsigned + ")nct_emit_a >> nct_emit_b);");
      line("}");
      line("");
    }
  }
  void record(const Record &R) {
    const bool ForwardDeclared = M.Profile == "cpp-core-v2";
    line(std::string(ForwardDeclared ? "struct " : "typedef struct ") +
             R.ID + " {", &R.Loc);
    for (const auto &F : R.Fields)
      line("  " + declaration(F.ValueType, F.Name) + ";", &R.Loc);
    if (ForwardDeclared && R.Fields.empty()) {
      line("  /* C++ empty-object storage and identity; not a source field. */", &R.Loc);
      line("  unsigned char nct_emit_empty_storage;", &R.Loc);
    }
    line(ForwardDeclared ? "};" : "} " + R.ID + ";", &R.Loc);
    if (R.Layout) {
      line("static_assert(sizeof(" + R.ID + ") * __CHAR_BIT__ == " +
           std::to_string(R.Layout->Storage.SizeBits) +
           ", \"translated record size mismatch\");", &R.Loc);
      line("static_assert(alignof(" + R.ID + ") * __CHAR_BIT__ == " +
           std::to_string(R.Layout->Storage.ABIAlignBits) +
           ", \"translated record alignment mismatch\");", &R.Loc);
      for (size_t I = 0; I < R.Fields.size(); ++I)
        line("static_assert(__builtin_offsetof(" + R.ID + ", " +
             R.Fields[I].Name + ") * __CHAR_BIT__ == " +
             std::to_string(R.Layout->FieldOffsetsBits[I]) +
             ", \"translated field offset mismatch\");", &R.Loc);
    }
    line("");
  }
  void pointerHelpers() {
    for (const auto &H : PointerHelpers) {
      const std::string Left = "nct_emit_left", Right = "nct_emit_right";
      auto Parameters = declaration(H.Left, Left) + ", " +
                        declaration(H.Right, Right);
      line("static " + declaration(H.Result, H.Name + "(" + Parameters + ")") +
           " {");
      if (H.Left.Kind == TypeKind::Pointer && H.Right.Kind == TypeKind::Pointer) {
        line("  return " + Left + " == " + Right + " ? (" + cType(H.Result) +
             ")0 : (" + cType(H.Result) + ")(" + Left + " - " + Right + ");");
      } else {
        const auto &Pointer = H.Left.Kind == TypeKind::Pointer ? Left : Right;
        const auto &Offset = H.Left.Kind == TypeKind::Pointer ? Right : Left;
        line("  return " + Offset + " == 0 ? " + Pointer + " : " + Pointer +
             " " + binarySpelling(H.Op) + " " + Offset + ";");
      }
      line("}");
      line("");
    }
  }
  void inspectModule() {
    for (const auto &G : M.Globals)
      inspect(G.Value);
    for (const auto &F : M.Functions)
      for (const auto &I : F.Body) {
        for (const auto *E : {&I.Target, &I.Value, &I.Condition})
          if (*E)
            inspect(**E);
        for (const auto &A : I.Args)
          inspect(A);
      }
  }
  void function(const Function &F) {
    line(signature(F) + " {", &F.Loc);
    for (const auto &L : F.Locals)
      line("  " + declaration(L.ValueType, L.Name) + ";", &L.Loc);
    for (const auto &I : F.Body) {
      std::string Text;
      switch (I.Op) {
      case InstructionKind::Assign:
        Text =
            "  " + expression(*I.Target) + " = " + expression(*I.Value) + ";";
        break;
      case InstructionKind::Call:
      case InstructionKind::MappedCall:
        Text = "  ";
        if (I.Target)
          Text += expression(*I.Target) + " = ";
        Text += (I.Op == InstructionKind::MappedCall
                     ? findMappingSpec(I.MappingID)->RuntimeSymbol
                     : I.Callee) +
                std::string("(");
        for (std::size_t N = 0; N < I.Args.size(); ++N) {
          if (N)
            Text += ", ";
          Text += expression(I.Args[N]);
        }
        Text += ");";
        break;
      case InstructionKind::Label:
        Text = I.Label + ":;";
        break;
      case InstructionKind::Jump:
        Text = "  goto " + I.Label + ";";
        break;
      case InstructionKind::Branch:
        Text = "  if (" + expression(*I.Condition) + ") goto " + I.TrueLabel +
               "; else goto " + I.FalseLabel + ";";
        break;
      case InstructionKind::Return:
        Text = I.Value ? "  return " + expression(*I.Value) + ";" : "  return;";
        break;
      }
      line(Text, &I.Loc);
    }
    line("}");
    line("");
  }

public:
  explicit Emitter(const Module &M) : M(M) {}
  EmittedSource run() {
    inspectModule();
    guards();
    helpers();
    if (M.Profile == "cpp-core-v2")
      for (const auto &R : M.Records)
        line("typedef struct " + R.ID + " " + R.ID + ";", &R.Loc);
    for (const auto &R : M.Records)
      record(R);
    pointerHelpers();
    for (const auto &G : M.Globals)
      line(std::string(G.Mutable ? "static " : "static const ") +
               cType(G.ValueType) + " " + G.Name + " = " +
               expression(G.Value, true) + ";",
           &G.Loc);
    if (!M.Globals.empty())
      line("");
    for (const auto &F : M.Functions)
      line(signature(F) + ";", &F.Loc);
    if (!M.Functions.empty())
      line("");
    for (const auto &F : M.Functions)
      function(F);
    return std::move(Output);
  }
  EmittedSource header(const std::set<std::string> &PublicRecords,
                       const std::set<std::string> &ExternalGlobals) {
    line("#ifndef NCT_EMIT_TRANSLATED_H");
    line("#define NCT_EMIT_TRANSLATED_H");
    guards();
    for (const auto &R : M.Records)
      if (PublicRecords.count(R.ID))
        record(R);
    for (const auto &G : M.Globals)
      if (ExternalGlobals.count(G.Name))
        line("extern const " + cType(G.ValueType) + " " + G.Name + ";", &G.Loc);
    for (const auto &F : M.Functions)
      if (!F.Internal)
        line(signature(F) + ";", &F.Loc);
    line("#endif");
    return std::move(Output);
  }
  EmittedSource source(const std::set<std::string> &PublicRecords,
                       const std::set<std::string> &ExternalGlobals) {
    line("/* Generated by neverc translate; profile " + M.Profile + ". */");
    line("#include \"translated.h\"");
    if (!M.Mappings.empty())
      line("#include <neverc/std/math.h>");
    line("");
    inspectModule();
    helpers();
    for (const auto &R : M.Records)
      if (!PublicRecords.count(R.ID))
        record(R);
    for (const auto &G : M.Globals)
      line(std::string(ExternalGlobals.count(G.Name) ? "const "
                                                     : "static const ") +
               cType(G.ValueType) + " " + G.Name + " = " +
               expression(G.Value, true) + ";",
           &G.Loc);
    if (!M.Globals.empty())
      line("");
    for (const auto &F : M.Functions)
      if (F.Internal)
        line(signature(F) + ";", &F.Loc);
    if (!M.Functions.empty())
      line("");
    for (const auto &F : M.Functions)
      function(F);
    return std::move(Output);
  }
};
} // namespace

bool emitNC(const Module &M, const VerificationContext &Context,
            EmittedSource &Out, Diagnostics &D) {
  if (!verifyModule(M, Context, D))
    return false;
  EmittedSource Result = Emitter(M).run();
  if (Result.Text.size() > 2 * MaxFrontendResponseBytes) {
    D.push_back({"TR0301",
                 {M.Dependencies.front().Path, 1, 1},
                 "NC emission",
                 "Generated source exceeds the 64 MiB emission limit.",
                 "Reduce the selected translation unit's size."});
    return false;
  }
  Out = std::move(Result);
  return true;
}
void detail::emitProjectFiles(const Module &M,
                              const std::set<std::string> &PublicRecords,
                              const std::set<std::string> &ExternalGlobals,
                              EmittedSource &Header, EmittedSource &Source) {
  Header = Emitter(M).header(PublicRecords, ExternalGlobals);
  Source = Emitter(M).source(PublicRecords, ExternalGlobals);
}
} // namespace neverc::translate
