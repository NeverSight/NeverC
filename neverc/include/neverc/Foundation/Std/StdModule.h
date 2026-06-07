#ifndef NEVERC_FOUNDATION_STDMODULE_H
#define NEVERC_FOUNDATION_STDMODULE_H

#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace StdModule {

/// The type-name prefix stamped on every std module marker struct.
/// E.g. math.h declares `struct __neverc_std_math_t {}; extern ... math;`
constexpr const char *TypePrefix = "__neverc_std_";
constexpr const char *TypeSuffix = "_t";

/// The function-name prefix for all std library functions.
/// E.g. neverc_math_sqrt, neverc_hex_encode, ...
constexpr const char *FuncPrefix = "neverc_";

/// Returns true if \p Name is a top-level module (e.g. "math", "sort").
bool isTopLevelModuleName(llvm::StringRef Name);

/// Returns true if \p Name is a category (e.g. "crypto", "hash", "encoding").
bool isCategoryName(llvm::StringRef Name);

/// Returns true if \p SubName is a submodule of category \p CatName.
/// E.g. isSubModule("crypto", "sha256") == true.
bool isSubModule(llvm::StringRef CatName, llvm::StringRef SubName);

/// Returns true for any known module name (top-level or submodule).
bool isModuleName(llvm::StringRef Name);

bool isModuleMethod(llvm::StringRef ModuleName, llvm::StringRef MethodName);

/// ("math", "sqrt") → "neverc_math_sqrt"
/// ("sha256", "sum") → "neverc_sha256_sum"
std::string getModuleFunctionName(llvm::StringRef ModuleName,
                                  llvm::StringRef MethodName);

/// "math" → "__neverc_std_math_t"
std::string getModuleTypeName(llvm::StringRef ModuleName);

/// Given a record type name like "__neverc_std_math_t", extract "math".
/// Returns empty StringRef if the name doesn't match the pattern.
llvm::StringRef extractModuleName(llvm::StringRef TypeName);

/// The root namespace type: __neverc_std_root_t
constexpr const char *RootTypeName = "__neverc_std_root_t";

/// Internal variable prefix for module variables: __neverc_mod_
constexpr const char *ModVarPrefix = "__neverc_mod_";

/// Returns true if \p TypeName is the root std namespace type.
bool isRootType(llvm::StringRef TypeName);

/// "math" → "__neverc_mod_math"
std::string getModuleVarName(llvm::StringRef ModuleName);

} // namespace StdModule
} // namespace neverc

#endif
