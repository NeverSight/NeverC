// The external-reference ledger records every symbol a dyncode
// unit still refers to but does not define, and tracks how each is dispositioned
// by an ImportProvider.  These tests exercise the scan and claim logic on real
// LLVM modules (this LLVM tree ships no AsmParser, so modules are built with the
// IR API) and confirm the conflict/idempotency rules the host relies on at route
// freeze and final verification.

#include "neverc/DynCode/Import/KernelImportABI.h"
#include "neverc/DynCode/Pipeline/ExternalReferenceLedger.h"

#include "DynCodeImportRegistry.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace neverc::dyncode;

namespace {

TEST(KernelResolverABITest, HashNameUsesFNV1a64) {
  EXPECT_EQ(KernelResolverABI::hashName(""), UINT64_C(0xcbf29ce484222325));
  EXPECT_EQ(KernelResolverABI::hashName("a"), UINT64_C(0xaf63dc4c8601ec8c));
  EXPECT_EQ(KernelResolverABI::hashName("foobar"),
            UINT64_C(0x85944171f73967e8));

  const char HighByte[] = {static_cast<char>(0xff)};
  EXPECT_EQ(KernelResolverABI::hashName(StringRef(HighByte, 1)),
            UINT64_C(0xaf64724c8602eb6e));
}

FunctionType *i32Fn(LLVMContext &C) {
  return FunctionType::get(Type::getInt32Ty(C), false);
}

// `declare i32 @<name>()`
Function *addDecl(Module &M, StringRef Name) {
  return Function::Create(i32Fn(M.getContext()), GlobalValue::ExternalLinkage,
                          Name, M);
}

// `define i32 @<name>() { ret i32 0 }`
Function *addDef(Module &M, StringRef Name) {
  Function *F = addDecl(M, Name);
  BasicBlock *BB = BasicBlock::Create(M.getContext(), "", F);
  IRBuilder<> B(BB);
  B.CreateRet(B.getInt32(0));
  return F;
}

TEST(ExternalReferenceLedgerTest, RecordsOnlyUndefinedNamedSymbols) {
  LLVMContext C;
  Module M("t", C);
  addDef(M, "defined");       // has a body -> not external
  addDecl(M, "ext_fn");       // declaration -> external function
  new GlobalVariable(M, Type::getInt32Ty(C), /*isConstant=*/false,
                     GlobalValue::ExternalLinkage, /*Init=*/nullptr, "ext_data");
  new GlobalVariable(M, Type::getInt32Ty(C), /*isConstant=*/true,
                     GlobalValue::ExternalLinkage,
                     ConstantInt::get(Type::getInt32Ty(C), 7), "def_data");

  ExternalReferenceLedger Ledger = ExternalReferenceLedger::scan(M);
  EXPECT_EQ(Ledger.size(), 2u);

  const ExternalReference *Fn = Ledger.find("ext_fn");
  ASSERT_NE(Fn, nullptr);
  EXPECT_EQ(Fn->Kind, ExternalReferenceKind::Function);
  EXPECT_EQ(Fn->Disposition, ExternalDisposition::Unresolved);

  const ExternalReference *Data = Ledger.find("ext_data");
  ASSERT_NE(Data, nullptr);
  EXPECT_EQ(Data->Kind, ExternalReferenceKind::Data);

  EXPECT_EQ(Ledger.find("defined"), nullptr);
  EXPECT_EQ(Ledger.find("def_data"), nullptr);
}

TEST(ExternalReferenceLedgerTest, CountsUseSitesAndExcludesIntrinsics) {
  LLVMContext C;
  Module M("t", C);
  Function *Ext = addDecl(M, "ext_fn");
  Function *Caller =
      Function::Create(FunctionType::get(Type::getVoidTy(C), false),
                       GlobalValue::ExternalLinkage, "caller", M);
  BasicBlock *BB = BasicBlock::Create(C, "", Caller);
  IRBuilder<> B(BB);
  B.CreateCall(Ext);
  B.CreateCall(Ext); // two call sites
  B.CreateRetVoid();

  // An intrinsic declaration must never be treated as an ImportProvider target.
  Function *Memcpy = Intrinsic::getDeclaration(
      &M, Intrinsic::memcpy,
      {PointerType::getUnqual(C), PointerType::getUnqual(C),
       Type::getInt64Ty(C)});
  ASSERT_TRUE(Memcpy->isIntrinsic());

  ExternalReferenceLedger Ledger = ExternalReferenceLedger::scan(M);
  const ExternalReference *Fn = Ledger.find("ext_fn");
  ASSERT_NE(Fn, nullptr);
  EXPECT_EQ(Fn->UseSites, 2u);
  EXPECT_EQ(Ledger.find(Memcpy->getName()), nullptr);
}

TEST(ExternalReferenceLedgerTest, ClaimResolvesAndIsProviderIdempotent) {
  LLVMContext C;
  Module M("t", C);
  addDecl(M, "sc_write");

  ExternalReferenceLedger Ledger = ExternalReferenceLedger::scan(M);
  ASSERT_FALSE(Ledger.allResolved());

  EXPECT_TRUE(Ledger.claim("sc_write", "com.neverc.syscall",
                           ExternalDisposition::RuntimeContract));
  const ExternalReference *Ref = Ledger.find("sc_write");
  ASSERT_NE(Ref, nullptr);
  EXPECT_EQ(Ref->Disposition, ExternalDisposition::RuntimeContract);
  EXPECT_EQ(Ref->ProviderID, "com.neverc.syscall");

  // Same provider re-claiming its own symbol is idempotent.
  EXPECT_TRUE(Ledger.claim("sc_write", "com.neverc.syscall",
                           ExternalDisposition::ResolvedInternal));
  EXPECT_TRUE(Ledger.allResolved());
  EXPECT_TRUE(Ledger.unresolved().empty());
}

TEST(ExternalReferenceLedgerTest, RejectsConflictUnknownAndUnresolvedClaims) {
  LLVMContext C;
  Module M("t", C);
  addDecl(M, "helper");

  ExternalReferenceLedger Ledger = ExternalReferenceLedger::scan(M);
  ASSERT_TRUE(Ledger.claim("helper", "provider.a",
                           ExternalDisposition::EliminatedInIR));

  // A different provider claiming the same symbol is a conflict.
  EXPECT_FALSE(Ledger.claim("helper", "provider.b",
                            ExternalDisposition::ResolvedInternal));
  EXPECT_EQ(Ledger.find("helper")->ProviderID, "provider.a");

  // Unknown symbol and an Unresolved disposition are both rejected.
  EXPECT_FALSE(Ledger.claim("missing", "provider.a",
                            ExternalDisposition::ResolvedInternal));
  EXPECT_FALSE(
      Ledger.claim("helper", "provider.a", ExternalDisposition::Unresolved));
}

TEST(ExternalReferenceLedgerTest, UnresolvedListTracksOutstandingExternals) {
  LLVMContext C;
  Module M("t", C);
  addDecl(M, "a");
  addDecl(M, "b");
  addDecl(M, "c");

  ExternalReferenceLedger Ledger = ExternalReferenceLedger::scan(M);
  EXPECT_EQ(Ledger.unresolved().size(), 3u);

  Ledger.claim("b", "p", ExternalDisposition::RuntimeContract);
  std::vector<StringRef> Left = Ledger.unresolved();
  ASSERT_EQ(Left.size(), 2u);
  EXPECT_EQ(Left[0], "a");
  EXPECT_EQ(Left[1], "c");
  EXPECT_FALSE(Ledger.allResolved());
}

// --- DynCodeImportRegistry ------------------------------------------------

OwnedDynCodeImportProvider mkProvider(uint64_t IDLow,
                                      NevercDynCodeImportKind Kind,
                                      std::vector<std::string> Matchers) {
  OwnedDynCodeImportProvider P;
  P.ProviderID = {0, IDLow};
  P.CanonicalName = "provider." + std::to_string(IDLow);
  P.Kind = Kind;
  P.TargetID = {0, 0};      // any target
  P.AnyLevel = true;        // any execution level
  P.SymbolMatchers = std::move(Matchers);
  return P;
}

TEST(DynCodeImportRegistryTest, ExactMatchBeatsWildcard) {
  DynCodeImportRegistry Reg;
  ASSERT_FALSE(errorToBool(Reg.registerImportProvider(
      mkProvider(1, NEVERC_DYNCODE_IMPORT_CUSTOM, {"*"}))));
  ASSERT_FALSE(errorToBool(Reg.registerImportProvider(
      mkProvider(2, NEVERC_DYNCODE_IMPORT_SYSCALL, {"write"}))));

  ImportResolution Hit =
      Reg.resolve("write", {0, 0}, NEVERC_DYNCODE_LEVEL_USER);
  EXPECT_EQ(Hit.Status, ImportResolveStatus::Resolved);
  ASSERT_NE(Hit.Provider, nullptr);
  EXPECT_EQ(Hit.Provider->ProviderID.Low, 2u); // the exact provider

  ImportResolution Fallback =
      Reg.resolve("anything_else", {0, 0}, NEVERC_DYNCODE_LEVEL_USER);
  EXPECT_EQ(Fallback.Status, ImportResolveStatus::Resolved);
  ASSERT_NE(Fallback.Provider, nullptr);
  EXPECT_EQ(Fallback.Provider->ProviderID.Low, 1u); // the wildcard provider
}

TEST(DynCodeImportRegistryTest, TwoExactMatchersConflict) {
  DynCodeImportRegistry Reg;
  ASSERT_FALSE(errorToBool(Reg.registerImportProvider(
      mkProvider(10, NEVERC_DYNCODE_IMPORT_SYSCALL, {"write"}))));
  ASSERT_FALSE(errorToBool(Reg.registerImportProvider(
      mkProvider(11, NEVERC_DYNCODE_IMPORT_CUSTOM, {"write"}))));

  ImportResolution R = Reg.resolve("write", {0, 0}, NEVERC_DYNCODE_LEVEL_USER);
  EXPECT_EQ(R.Status, ImportResolveStatus::Conflict);
  EXPECT_EQ(R.Provider, nullptr);
  EXPECT_TRUE((R.ConflictA.Low == 10u && R.ConflictB.Low == 11u) ||
              (R.ConflictA.Low == 11u && R.ConflictB.Low == 10u));
}

TEST(DynCodeImportRegistryTest, NoProviderWhenNothingMatches) {
  DynCodeImportRegistry Reg;
  ASSERT_FALSE(errorToBool(Reg.registerImportProvider(
      mkProvider(20, NEVERC_DYNCODE_IMPORT_SYSCALL, {"read"}))));

  ImportResolution R = Reg.resolve("write", {0, 0}, NEVERC_DYNCODE_LEVEL_USER);
  EXPECT_EQ(R.Status, ImportResolveStatus::NoProvider);
}

TEST(DynCodeImportRegistryTest, TargetAndLevelFilterProviders) {
  DynCodeImportRegistry Reg;
  OwnedDynCodeImportProvider Kern =
      mkProvider(30, NEVERC_DYNCODE_IMPORT_KERNEL, {"kalloc"});
  Kern.TargetID = {7, 7};
  Kern.AnyLevel = false;
  Kern.Level = NEVERC_DYNCODE_LEVEL_KERNEL;
  ASSERT_FALSE(errorToBool(Reg.registerImportProvider(Kern)));

  // Wrong target and wrong level both fail to match.
  EXPECT_EQ(Reg.resolve("kalloc", {1, 1}, NEVERC_DYNCODE_LEVEL_KERNEL).Status,
            ImportResolveStatus::NoProvider);
  EXPECT_EQ(Reg.resolve("kalloc", {7, 7}, NEVERC_DYNCODE_LEVEL_USER).Status,
            ImportResolveStatus::NoProvider);
  // Matching target and level resolve.
  EXPECT_EQ(Reg.resolve("kalloc", {7, 7}, NEVERC_DYNCODE_LEVEL_KERNEL).Status,
            ImportResolveStatus::Resolved);
}

TEST(DynCodeImportRegistryTest, RejectsInvalidRegistrations) {
  DynCodeImportRegistry Reg;
  // Zero provider ID.
  EXPECT_TRUE(errorToBool(Reg.registerImportProvider(
      mkProvider(0, NEVERC_DYNCODE_IMPORT_SYSCALL, {"x"}))));
  // Out-of-range kind.
  EXPECT_TRUE(errorToBool(
      Reg.registerImportProvider(mkProvider(1, /*kind=*/99, {"x"}))));
  // Empty matchers.
  EXPECT_TRUE(errorToBool(
      Reg.registerImportProvider(mkProvider(1, NEVERC_DYNCODE_IMPORT_SYSCALL, {}))));
  // Wildcard mixed with a named matcher.
  EXPECT_TRUE(errorToBool(Reg.registerImportProvider(
      mkProvider(1, NEVERC_DYNCODE_IMPORT_SYSCALL, {"*", "y"}))));
  // Valid registration, then duplicate ID.
  ASSERT_FALSE(errorToBool(Reg.registerImportProvider(
      mkProvider(1, NEVERC_DYNCODE_IMPORT_SYSCALL, {"x"}))));
  EXPECT_TRUE(errorToBool(Reg.registerImportProvider(
      mkProvider(1, NEVERC_DYNCODE_IMPORT_CUSTOM, {"y"}))));
}

} // namespace
