#include "neverc/Foundation/Std/StdModule.h"
#include "llvm/ADT/Twine.h"

using namespace neverc;

static constexpr llvm::StringLiteral TopLevelModules[] = {
    "math",    "strconv", "path",    "sort",    "unicode",
};

static constexpr llvm::StringLiteral Categories[] = {
    "crypto", "hash", "encoding", "math", "unicode",
};

struct SubModuleEntry {
  llvm::StringLiteral Category;
  llvm::StringLiteral Name;
};

static constexpr SubModuleEntry SubModules[] = {
    // crypto
    {"crypto", "sha256"},  {"crypto", "sha1"},
    {"crypto", "sha512"},  {"crypto", "sha384"},
    {"crypto", "sha224"},  {"crypto", "sha3"},
    {"crypto", "sha512_224"}, {"crypto", "sha512_256"},
    {"crypto", "md5"},     {"crypto", "aes"},
    {"crypto", "des"},     {"crypto", "rc4"},
    {"crypto", "chacha20"},{"crypto", "poly1305"},
    {"crypto", "chacha20poly1305"}, {"crypto", "gcm"},
    {"crypto", "cipher"},  {"crypto", "hmac"},
    {"crypto", "subtle"},  {"crypto", "hkdf"},
    {"crypto", "pbkdf2"},
    // hash
    {"hash", "crc32"},     {"hash", "crc64"},
    {"hash", "fnv"},       {"hash", "adler32"},
    // encoding
    {"encoding", "hex"},   {"encoding", "base64"},
    {"encoding", "base32"},{"encoding", "ascii85"},
    {"encoding", "binary"},{"encoding", "pem"},
    // math
    {"math", "rand"},      {"math", "bits"},
    {"math", "cmplx"},
    // unicode
    {"unicode", "utf8"},   {"unicode", "utf16"},
};

bool StdModule::isTopLevelModuleName(llvm::StringRef Name) {
  for (const auto &M : TopLevelModules)
    if (Name == M)
      return true;
  return false;
}

bool StdModule::isCategoryName(llvm::StringRef Name) {
  for (const auto &C : Categories)
    if (Name == C)
      return true;
  return false;
}

bool StdModule::isSubModule(llvm::StringRef CatName, llvm::StringRef SubName) {
  for (const auto &S : SubModules)
    if (S.Category == CatName && S.Name == SubName)
      return true;
  return false;
}

bool StdModule::isModuleName(llvm::StringRef Name) {
  if (isTopLevelModuleName(Name))
    return true;
  for (const auto &S : SubModules)
    if (S.Name == Name)
      return true;
  return false;
}

bool StdModule::isModuleMethod(llvm::StringRef ModuleName,
                               llvm::StringRef MethodName) {
  (void)MethodName;
  return isModuleName(ModuleName);
}

std::string StdModule::getModuleFunctionName(llvm::StringRef ModuleName,
                                             llvm::StringRef MethodName) {
  return (llvm::Twine(FuncPrefix) + ModuleName + "_" + MethodName).str();
}

std::string StdModule::getModuleTypeName(llvm::StringRef ModuleName) {
  return (llvm::Twine(TypePrefix) + ModuleName + TypeSuffix).str();
}

llvm::StringRef StdModule::extractModuleName(llvm::StringRef TypeName) {
  llvm::StringRef Prefix(TypePrefix);
  llvm::StringRef Suffix(TypeSuffix);
  if (!TypeName.starts_with(Prefix) || !TypeName.ends_with(Suffix))
    return {};
  return TypeName.drop_front(Prefix.size()).drop_back(Suffix.size());
}
