#include "LibraryMappings.h"
#include "../ArtifactWriter.h"
#include "CppSdk.h"
#include "neverc/Foundation/Std/BuiltinStd.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <map>
#include <set>

namespace neverc::translate {
namespace {
using namespace llvm;
constexpr std::size_t MaxMathPayloadBytes = 4 * 1024 * 1024;
constexpr StringLiteral HeaderIdentity =
    "2124e02ddab13569fb310376913cda90c3c9effd11daf381b9dcf50c4a2d5093";

std::string digest(StringRef Bytes) {
  SHA256 Hash;
  Hash.update(Bytes);
  std::string Out;
  constexpr char Hex[] = "0123456789abcdef";
  for (uint8_t Byte : Hash.final()) {
    Out += Hex[Byte >> 4];
    Out += Hex[Byte & 15];
  }
  return Out;
}
bool error(Diagnostics &D, StringRef File, StringRef Why) {
  D.push_back(driverDiagnostic(
      "TR0403", File, "math runtime capability", Why,
      "Install matching NeverC math headers and rebuild the approved target "
      "runtime payload; keep builtin std enabled."));
  return false;
}
std::unique_ptr<llvm::Module> parse(StringRef Bytes, LLVMContext &Context,
                                    Diagnostics &D) {
  if (Bytes.empty() || Bytes.size() > MaxMathPayloadBytes) {
    error(D, "<runtime-bitcode>",
          "runtime payload is empty or exceeds its byte limit");
    return nullptr;
  }
  auto Buffer = MemoryBuffer::getMemBuffer(Bytes, "math runtime", false);
  auto Parsed = parseBitcodeFile(Buffer->getMemBufferRef(), Context);
  if (!Parsed) {
    std::string Why = toString(Parsed.takeError()).str().str();
    error(D, "<runtime-bitcode>", "malformed runtime bitcode: " + Why);
    return nullptr;
  }
  std::string Detail;
  raw_string_ostream OS(Detail);
  if (llvm::verifyModule(**Parsed, &OS)) {
    error(D, "<runtime-bitcode>", "invalid runtime IR: " + Detail);
    return nullptr;
  }
  return std::move(*Parsed);
}
std::string canonicalIdentity(llvm::Module &M) {
  StripDebugInfo(M);
  M.setModuleIdentifier("neverc.math.payload.v1");
  M.setSourceFileName("");
  if (auto *Ident = M.getNamedMetadata("llvm.ident"))
    Ident->eraseFromParent();
  if (auto *Flags = M.getModuleFlagsMetadata()) {
    SmallVector<MDNode *, 8> Kept;
    for (auto *Flag : Flags->operands()) {
      const auto *Key = Flag->getNumOperands() > 1
                            ? dyn_cast<MDString>(Flag->getOperand(1))
                            : nullptr;
      if (Key &&
          (Key->getString() == "Debug Info Version" ||
           Key->getString() == "Dwarf Version" ||
           Key->getString() == "DWARF64" || Key->getString() == "CodeView"))
        continue;
      Kept.push_back(Flag);
    }
    Flags->clearOperands();
    for (auto *Flag : Kept)
      Flags->addOperand(Flag);
  }
  // Retain target/layout/attributes and all non-debug metadata. This is a
  // versioned approval fingerprint, not a best-effort semantic equivalence
  // test.
  std::string Text;
  raw_string_ostream OS(Text);
  M.print(OS, nullptr);
  return digest(Text);
}
bool readHeader(StringRef Path, std::string &Bytes, std::string &Why) {
  auto Contents = neverc::translate::readFile(Path, MaxMathPayloadBytes);
  if (!Contents) {
    Why = toString(Contents.takeError()).str().str();
    return false;
  }
  Bytes = std::move(*Contents);
  return true;
}
bool inspectModule(StringRef Target, const RuntimeModuleData &Payload,
                   const MappingSpec &Spec, RuntimeModuleCapability &Capability,
                   Diagnostics &D) {
  LLVMContext C;
  auto M = parse(Payload.Bitcode, C, D);
  if (!M)
    return false;
  Triple Requested(Triple::normalize(Target)), Embedded(M->getTargetTriple());
  const auto &Layout = M->getDataLayout();
  auto *Double = llvm::Type::getDoubleTy(C);
  if (Embedded.getArch() != Requested.getArch() || !Embedded.isMacOSX() ||
      Embedded.getVendor() != Triple::Apple ||
      Embedded.getObjectFormat() != Requested.getObjectFormat() ||
      !Embedded.getEnvironmentName().empty() || M->getDataLayoutStr().empty() ||
      !Layout.isLittleEndian() || Layout.getPointerSizeInBits() != 64 ||
      Layout.getTypeAllocSize(Double).getFixedValue() != 8 ||
      Layout.getABITypeAlign(Double).value() != 8)
    return error(D, Payload.Name,
                 "runtime payload target or binary64 data layout does not "
                 "match the approved target");
  const auto *F = M->getFunction(Spec.RuntimeSymbol);
  if (!F || F->isDeclaration() || F->hasLocalLinkage() ||
      F->getCallingConv() != CallingConv::C || F->isVarArg() ||
      F->getReturnType() != Double || F->arg_size() != 1 ||
      F->getFunctionType()->getParamType(0) != Double)
    return error(D, Payload.Name,
                 "required runtime symbol is missing, declaration-only, or has "
                 "the wrong signature/calling convention");
  if (!M->alias_empty() || !M->ifunc_empty())
    return error(D, Payload.Name,
                 "runtime aliases or indirect functions are outside the "
                 "approved implementation");
  for (const auto &Function : *M) {
    if (Function.isIntrinsic()) {
      if (Function.getName() != "llvm.fabs.f64")
        return error(D, Payload.Name,
                     "runtime uses an unapproved LLVM intrinsic");
      continue;
    }
    if (Function.isDeclaration()) {
      if (!Function.use_empty())
        Capability.RequiredSymbols.push_back(Function.getName().str());
    } else {
      if (&Function != F)
        return error(D, Payload.Name,
                     "runtime has an unexpected function definition");
      Capability.DefinedSymbols.push_back(Function.getName().str());
    }
  }
  for (const auto &Global : M->globals()) {
    if (Global.isDeclaration() && !Global.use_empty())
      Capability.RequiredSymbols.push_back(Global.getName().str());
    else if (!Global.isDeclaration())
      return error(D, Payload.Name,
                   "runtime has an unexpected global definition");
  }
  if (!Capability.RequiredSymbols.empty())
    return error(D, Payload.Name,
                 "runtime has an unapproved external dependency: " +
                     Capability.RequiredSymbols.front());
  Capability.Name = Payload.Name;
  Capability.SHA256 = digest(Payload.Bitcode);
  Capability.ImplementationIdentity = canonicalIdentity(*M);
  StringRef Approved = approvedMathRuntimeIdentity(Target, Payload.Name);
  if (Approved.empty() || Capability.ImplementationIdentity != Approved)
    return error(D, Payload.Name,
                 "runtime implementation fingerprint is unapproved; expected " +
                     Approved.str() + ", observed " +
                     Capability.ImplementationIdentity);
  return true;
}
} // namespace

llvm::StringRef approvedMathRuntimeHeaderSHA256() { return HeaderIdentity; }
llvm::StringRef approvedMathRuntimeIdentity(llvm::StringRef Target,
                                            llvm::StringRef Module) {
  const auto Arch = llvm::Triple(llvm::Triple::normalize(Target)).getArch();
  if (Arch == llvm::Triple::x86_64) {
    if (Module == "math_abs")
      return "12a2074d0a47cfab43e3641b21d630ac3fdc3054bd42b48196f3f29f21c1ee66";
    if (Module == "math_floor")
      return "7ef3cbd4cdbbfa8290762500aa616a7d22a1bfc2db233ad7a4af755e30ddbf1d";
  }
  if (Arch == llvm::Triple::aarch64) {
    if (Module == "math_abs")
      return "53dede0961bd20a20ecb8829a2b7fff04994bf451aa1282736eabcc48c917d86";
    if (Module == "math_floor")
      return "38cfac156bacb0705392b44cc347ee10ce524368dd143c65bed1f54e5c6bdb38";
  }
  return {};
}

bool mathRuntimeImplementationIdentity(llvm::StringRef Bitcode,
                                       std::string &Identity, Diagnostics &D) {
  Identity.clear();
  llvm::LLVMContext C;
  auto M = parse(Bitcode, C, D);
  if (!M)
    return false;
  Identity = canonicalIdentity(*M);
  return true;
}

bool inspectMathRuntime(llvm::StringRef Target,
                        llvm::StringRef ResourceDirectory,
                        llvm::ArrayRef<std::string> RequiredIDs, bool Enabled,
                        MathRuntimeCapabilities &Result, Diagnostics &D,
                        const RuntimeCapabilityProviders *Providers) {
  Result = {};
  if (!validateCppMathTarget(Target, D))
    return false;
  MathRuntimeCapabilities Parsed;
  Parsed.Target = llvm::Triple::normalize(Target);
  std::set<std::string> MappingIDs(RequiredIDs.begin(), RequiredIDs.end());
  std::map<std::string, const MappingSpec *> Required;
  for (const auto &ID : MappingIDs) {
    const auto *Spec = findMappingSpec(ID);
    if (!Spec)
      return error(D, ID,
                   "unknown mapping operation requested a runtime capability");
    Required.emplace(Spec->RuntimeModule, Spec);
  }
  Parsed.MappingIDs.assign(MappingIDs.begin(), MappingIDs.end());
  if (!Required.empty()) {
    if (!Enabled)
      return error(D, "<runtime>", "builtin std runtime is disabled");
    Parsed.HeaderRelativePath = "neverc/std/math.h";
    llvm::SmallString<256> Header(ResourceDirectory);
    llvm::sys::path::append(Header, "include", Parsed.HeaderRelativePath);
    std::string Bytes, Why;
    bool Read = Providers && Providers->ReadHeader
                    ? Providers->ReadHeader(Header, Bytes, Why)
                    : readHeader(Header, Bytes, Why);
    if (!Read)
      return error(D, Parsed.HeaderRelativePath,
                   "required installed math header is unavailable: " + Why);
    Parsed.HeaderSHA256 = digest(Bytes);
    if (Parsed.HeaderSHA256 != HeaderIdentity)
      return error(
          D, Parsed.HeaderRelativePath,
          "installed math header does not match the approved header identity");
    std::vector<RuntimeModuleData> Modules;
    if (Providers && Providers->Modules)
      Modules = Providers->Modules(Target);
    else {
      llvm::Triple T(Parsed.Target);
      const auto Count = neverc::BuiltinStd::getEmbeddedModuleCount(T);
      for (unsigned I = 0; I < Count; ++I) {
        auto Entry = neverc::BuiltinStd::getEmbeddedModule(T, I);
        if (Required.count(Entry.first.str()))
          Modules.push_back({Entry.first.str(), Entry.second.str()});
      }
    }
    for (const auto &Entry : Required) {
      const RuntimeModuleData *Found = nullptr;
      for (const auto &Module : Modules)
        if (Module.Name == Entry.first) {
          if (Found)
            return error(D, Entry.first,
                         "duplicate embedded runtime module name");
          Found = &Module;
        }
      if (!Found)
        return error(D, Entry.first,
                     "required target runtime payload is absent");
      RuntimeModuleCapability Capability;
      if (!inspectModule(Parsed.Target, *Found, *Entry.second, Capability, D))
        return false;
      Parsed.Modules.push_back(std::move(Capability));
    }
  }
  std::string Identity;
  auto Frame = [&](llvm::StringRef Value) {
    Identity += std::to_string(Value.size()) + ":" + Value.str();
  };
  Frame(MathRuntimeFingerprintPolicy);
  Frame(Parsed.Target);
  Frame(Parsed.HeaderSHA256);
  for (const auto &ID : Parsed.MappingIDs)
    Frame(ID);
  for (const auto &Module : Parsed.Modules) {
    Frame(Module.Name);
    Frame(Module.ImplementationIdentity);
  }
  Parsed.ID = digest(Identity);
  Result = std::move(Parsed);
  return true;
}

} // namespace neverc::translate
