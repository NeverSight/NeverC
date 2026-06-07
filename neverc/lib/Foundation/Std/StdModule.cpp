#include "neverc/Foundation/Std/StdModule.h"
#include "llvm/ADT/Twine.h"

using namespace neverc;

static constexpr llvm::StringLiteral TopLevelModules[] = {
    "math",    "strconv", "path",    "sort",    "unicode",
    "cmp",     "bytes",   "errors",  "html",    "fmt",
    "io",      "bufio",   "flag",    "log",     "time",
    "uuid",    "regexp",  "mime",    "sync",    "os",
    "context", "slices",  "maps",    "image",
};

static constexpr llvm::StringLiteral Categories[] = {
    "crypto",    "hash",      "encoding",  "math",      "unicode",
    "container", "compress",  "archive",   "text",      "log",
    "index",     "sync",      "path",      "net",       "image",
    "mime",      "os",        "io",        "html",
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
    {"crypto", "pbkdf2"},  {"crypto", "rand"},
    {"crypto", "elliptic"},{"crypto", "rsa"},
    {"crypto", "ecdsa"},   {"crypto", "dsa"},
    {"crypto", "ed25519"},
    {"crypto", "ecdh"},
    // net
    {"net", "url"},
    {"net", "netip"},
    {"net", "mail"},
    // image
    {"image", "color"},
    // hash
    {"hash", "crc32"},     {"hash", "crc64"},
    {"hash", "fnv"},       {"hash", "adler32"},
    {"hash", "maphash"},
    // encoding
    {"encoding", "hex"},   {"encoding", "base64"},
    {"encoding", "base32"},{"encoding", "ascii85"},
    {"encoding", "binary"},{"encoding", "pem"},
    {"encoding", "csv"},   {"encoding", "json"},
    {"encoding", "xml"},   {"encoding", "asn1"},
    // math
    {"math", "rand"},      {"math", "bits"},
    {"math", "cmplx"},     {"math", "big"},
    // unicode
    {"unicode", "utf8"},   {"unicode", "utf16"},
    // container
    {"container", "heap"}, {"container", "list"},
    {"container", "ring"},
    // compress
    {"compress", "lzw"},   {"compress", "flate"},
    {"compress", "gzip"},  {"compress", "zlib"},
    {"compress", "bzip2"},
    // archive
    {"archive", "tar"},    {"archive", "zip"},
    // text
    {"text", "template"},  {"text", "scanner"},
    {"text", "tabwriter"},
    // log
    {"log", "slog"},
    {"log", "syslog"},
    // index
    {"index", "suffixarray"},
    // sync
    {"sync", "atomic"},
    // path
    {"path", "filepath"},
    // mime
    {"mime", "quotedprintable"},
    {"mime", "multipart"},
    // os
    {"os", "exec"},
    {"os", "signal"},
    {"os", "user"},
    // io
    {"io", "fs"},
    // image
    {"image", "draw"},
    {"image", "png"},
    {"image", "jpeg"},
    {"image", "gif"},
    // html
    {"html", "template"},
    // net (additional)
    {"net", "textproto"},
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
