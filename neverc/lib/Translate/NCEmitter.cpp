#include "TranslateIR.h"
#include "TranslateIRInternal.h"
#include "llvm/TargetParser/Triple.h"
#include <utility>

namespace neverc::translate {
namespace {
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
         (T.Kind == TypeKind::UInt ? "unsigned int" : typeName(T)) +
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
  bool NeedsUnsignedConversion = false;
  bool NeedsArithmeticShift = false;

  void line(const std::string &Text, const SourceLocation *Loc = nullptr) {
    Output.Text += Text;
    Output.Text += '\n';
    if (Loc)
      Output.Map.push_back({Line, Line, *Loc});
    ++Line;
  }
  void inspect(const Expr &E) {
    if ((E.Kind == ExprKind::Cast && E.ValueType.Kind == TypeKind::Int &&
         E.Args[0].ValueType.Kind == TypeKind::UInt) ||
        (E.Kind == ExprKind::Binary &&
         E.BinaryOp == BinaryOperator::ShiftLeft &&
         E.ValueType.Kind == TypeKind::Int))
      NeedsUnsignedConversion = true;
    if (E.Kind == ExprKind::Binary &&
        E.BinaryOp == BinaryOperator::ShiftRight &&
        E.ValueType.Kind == TypeKind::Int)
      NeedsArithmeticShift = true;
    for (const auto &A : E.Args)
      inspect(A);
  }
  std::string expression(const Expr &E, bool Initializer = false) {
    switch (E.Kind) {
    case ExprKind::Null:
      return "((" + cType(E.ValueType) + ")0)";
    case ExprKind::Address:
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
      std::string A = expression(E.Args[0]), B = expression(E.Args[1]);
      if (E.ValueType.Kind == TypeKind::Int &&
          E.BinaryOp == BinaryOperator::ShiftLeft)
        return "nct_emit_u32_to_i32(((unsigned int)(" + A + ")) << (" + B +
               "))";
      if (E.ValueType.Kind == TypeKind::Int &&
          E.BinaryOp == BinaryOperator::ShiftRight)
        return "nct_emit_i32_shr(" + A + ", (unsigned int)(" + B + "))";
      std::string Text =
          "((" + A + ") " + binarySpelling(E.BinaryOp) + " (" + B + "))";
      return E.ValueType.Kind == TypeKind::Bool ? "((bool)" + Text + ")" : Text;
    }
    case ExprKind::Cast:
      if (E.ValueType.Kind == TypeKind::Int &&
          E.Args[0].ValueType.Kind == TypeKind::UInt)
        return "nct_emit_u32_to_i32(" + expression(E.Args[0]) + ")";
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
    if (NeedsUnsignedConversion) {
      line("/* Pin the source frontend's two's-complement uint-to-int "
           "conversion. */");
      line("static int nct_emit_u32_to_i32(unsigned int nct_emit_u) {");
      line("  return nct_emit_u <= 2147483647u ? (int)nct_emit_u");
      line("      : -1 - (int)(4294967295u - nct_emit_u);");
      line("}");
      line("");
    }
    if (NeedsArithmeticShift) {
      line("/* Preserve arithmetic right shift without implementation-defined "
           "C behavior. */");
      line("static int nct_emit_i32_shr(int nct_emit_a, unsigned int "
           "nct_emit_b) {");
      line("  return nct_emit_a < 0 ? -1 - (int)((~(unsigned int)nct_emit_a) "
           ">> nct_emit_b)");
      line("      : (int)((unsigned int)nct_emit_a >> nct_emit_b);");
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
    line(ForwardDeclared ? "};" : "} " + R.ID + ";", &R.Loc);
    line("");
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
    for (const auto &G : M.Globals)
      line("static const " + cType(G.ValueType) + " " + G.Name + " = " +
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
