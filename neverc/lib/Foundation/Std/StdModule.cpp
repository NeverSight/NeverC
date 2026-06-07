#include "neverc/Foundation/Std/StdModule.h"
#include "llvm/ADT/Twine.h"

using namespace neverc;

static constexpr llvm::StringLiteral TopLevelModules[] = {
#define STD_MODULE(name) #name,
#include "neverc/Foundation/Std/StdTopLevelModules.def"
};

static constexpr llvm::StringLiteral Categories[] = {
#define STD_CATEGORY(name) #name,
#include "neverc/Foundation/Std/StdCategories.def"
};

struct SubModuleEntry {
  llvm::StringLiteral Category;
  llvm::StringLiteral Name;
};

static constexpr SubModuleEntry SubModules[] = {
#define STD_SUBMODULE(cat, name) {#cat, #name},
#include "neverc/Foundation/Std/StdSubModules.def"
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

bool StdModule::isRootType(llvm::StringRef TypeName) {
  return TypeName == RootTypeName;
}

std::string StdModule::getModuleVarName(llvm::StringRef ModuleName) {
  return (llvm::Twine(ModVarPrefix) + ModuleName).str();
}
