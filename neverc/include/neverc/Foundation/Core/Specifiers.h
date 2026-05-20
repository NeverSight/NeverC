#ifndef NEVERC_FOUNDATION_SPECIFIERS_H
#define NEVERC_FOUNDATION_SPECIFIERS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/ErrorHandling.h"

namespace llvm {
class raw_ostream;
} // namespace llvm
namespace neverc {

enum class ConstexprSpecKind { Unspecified, Constexpr };

enum class TypeSpecifierWidth { Unspecified, Short, Long, LongLong };

enum class TypeSpecifierSign { Unspecified, Signed, Unsigned };

enum TypeSpecifierType {
  TST_unspecified,
  TST_void,
  TST_char,
  TST_wchar,  // wchar_t
  TST_char8,  // char8_t
  TST_char16, // char16_t
  TST_char32, // char32_t
  TST_int,
  TST_int128,
  TST_bitint,  // Bit-precise integer types.
  TST_half,    // ARM NEON __fp16
  TST_Float16, // C11 extension ISO/IEC TS 18661-3
  TST_Accum,   // ISO/IEC JTC1 SC22 WG14 N1169 Extension
  TST_Fract,
  TST_BFloat16,
  TST_float,
  TST_double,
  TST_float128,
  TST_bool,       // _Bool
  TST_decimal32,  // _Decimal32
  TST_decimal64,  // _Decimal64
  TST_decimal128, // _Decimal128
  TST_enum,
  TST_union,
  TST_struct,
  TST_typename,          // typedef name, struct/union/enum name
  TST_typeofType,        // C23 (and GNU extension) typeof(type-name)
  TST_typeofExpr,        // C23 (and GNU extension) typeof(expression)
  TST_typeof_unqualType, // C23 typeof_unqual(type-name)
  TST_typeof_unqualExpr, // C23 typeof_unqual(expression)
  TST_auto,              // auto
  TST_auto_type,         // __auto_type extension
  TST_atomic,            // C11 _Atomic
  // NeverC Rust-style fixed-width integer types
  TST_i8,
  TST_i16,
  TST_i32,
  TST_i64,
  TST_i128,
  TST_u8,
  TST_u16,
  TST_u32,
  TST_u64,
  TST_u128,
  TST_isize,
  TST_usize,
  TST_error // erroneous type
};

struct WrittenBuiltinSpecs {
  static_assert(TST_error < 1 << 7, "Type bitfield not wide enough for TST");
  LLVM_PREFERRED_TYPE(TypeSpecifierType)
  unsigned Type : 7;
  LLVM_PREFERRED_TYPE(TypeSpecifierSign)
  unsigned Sign : 2;
  LLVM_PREFERRED_TYPE(TypeSpecifierWidth)
  unsigned Width : 2;
  LLVM_PREFERRED_TYPE(bool)
  unsigned ModeAttr : 1;
};

enum AccessSpecifier { AS_public, AS_none };

enum ExprValueKind {
  VK_PRValue,

  VK_LValue,
};

enum ExprObjectKind {
  OK_Ordinary,

  OK_BitField,

  OK_VectorComponent,

  OK_MatrixComponent
};

enum NonOdrUseReason {
  NOUR_None = 0,
  NOUR_Unevaluated,
  NOUR_Constant,
};

enum ThreadStorageClassSpecifier {
  TSCS_unspecified,
  TSCS___thread,
  TSCS_thread_local,
  TSCS__Thread_local
};

enum StorageClass {
  // These are legal on both functions and variables.
  SC_None,
  SC_Extern,
  SC_Static,
  SC_PrivateExtern,

  // These are only legal on variables.
  SC_Auto,
  SC_Register
};

enum CallingConv {
  CC_C,                 // __attribute__((cdecl))
  CC_X86StdCall,        // __attribute__((stdcall))
  CC_X86FastCall,       // __attribute__((fastcall))
  CC_X86VectorCall,     // __attribute__((vectorcall))
  CC_Win64,             // __attribute__((ms_abi))
  CC_X86_64SysV,        // __attribute__((sysv_abi))
  CC_X86RegCall,        // __attribute__((regcall))
  CC_PreserveMost,      // __attribute__((preserve_most))
  CC_PreserveAll,       // __attribute__((preserve_all))
  CC_AArch64VectorCall, // __attribute__((aarch64_vector_pcs))
  CC_AArch64SVEPCS,     // __attribute__((aarch64_sve_pcs))
};

inline bool supportsVariadicCall(CallingConv CC) {
  switch (CC) {
  case CC_X86StdCall:
  case CC_X86FastCall:
  case CC_X86RegCall:
  case CC_X86VectorCall:
    return false;
  default:
    return true;
  }
}

enum StorageDuration {
  SD_Automatic,
  SD_Thread,
  SD_Static,
};

enum class NullabilityKind : uint8_t {
  NonNull = 0,
  Nullable,
  Unspecified,
};
llvm::raw_ostream &operator<<(llvm::raw_ostream &, NullabilityKind);

llvm::StringRef getNullabilitySpelling(NullabilityKind kind,
                                       bool isContextSensitive = false);

} // end namespace neverc

#endif // NEVERC_FOUNDATION_SPECIFIERS_H
