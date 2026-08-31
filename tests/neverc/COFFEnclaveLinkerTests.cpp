#include "Linker/COFF/Driver.h"
#include "Linker/Core/Runtime/LinkerExecutionContext.h"
#include "neverc/Invoke/InMemoryFileStore.h"
#include "neverc/Plugin/Host/BuiltinLLVMAsmParser.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ArchiveWriter.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace linker;

LINKER_HAS_DRIVER(coff)

namespace {

constexpr uint16_t PE32PlusMagic = 0x20b;
constexpr uint16_t DllCharacteristicsForceIntegrity = 0x0080;
constexpr uint16_t DllCharacteristicsGuardCF = 0x4000;
constexpr uint32_t GuardCFInstrumented = 0x100;
constexpr uint32_t GuardCFFunctionTablePresent = 0x400;
constexpr uint32_t GuardCFLongJumpTablePresent = 0x10000;
constexpr uint32_t GuardEHContinuationTablePresent = 0x400000;
constexpr uint32_t BaseRelocationDirectory = 5;
constexpr uint32_t LoadConfigDirectory = 10;
constexpr uint16_t BaseRelocationDir64 = 10;
constexpr size_t DataDirectoryOffset = 112;
constexpr size_t LoadConfigGuardTableOffset = 0x80;
constexpr size_t LoadConfigGuardCountOffset = 0x88;
constexpr size_t LoadConfigGuardFlagsOffset = 0x90;
constexpr size_t LoadConfigEnclavePointerOffset = 0xf8;
constexpr size_t EnclaveConfigImageIdMagicOffset = 44;
constexpr uint32_t EnclaveConfigImageIdMagic = 0xc0decafe;

void initializeAssemblyTargets() {
  static std::once_flag Once;
  std::call_once(Once, [] {
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
  });
}

llvm::Expected<llvm::SmallVector<char, 0>>
assembleCOFF(llvm::StringRef Triple, llvm::StringRef Assembly) {
  initializeAssemblyTargets();
  const neverc::plugin::BuiltinTargetRoute *Route =
      neverc::plugin::findBuiltinTargetRoute(Triple);
  if (!Route)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "missing builtin target route for %s",
                                   Triple.str().c_str());
  auto Target = neverc::plugin::lookupBuiltinLLVMTarget(*Route);
  if (!Target)
    return Target.takeError();

  llvm::SmallVector<char, 0> Object;
  llvm::raw_svector_ostream Output(Object);
  neverc::plugin::BuiltinLLVMAsmParserRequest Request;
  Request.Target = *Target;
  Request.TargetTriple = llvm::Triple(llvm::Triple::normalize(Triple));
  Request.CPU = Route->DefaultCPU;
  Request.Input = llvm::MemoryBufferRef(Assembly, "coff-enclave-test.s");
  Request.Output = &Output;
  if (llvm::Error E = neverc::plugin::runBuiltinLLVMAsmParser(Request))
    return std::move(E);
  return Object;
}

struct InMemoryInput {
  std::string Path;
  llvm::SmallVector<char, 0> Contents;
};

struct LinkResult {
  bool Succeeded = false;
  std::string Diagnostics;
  std::string Image;
  bool Crashed = false;
};

llvm::Expected<llvm::SmallVector<char, 0>>
archiveCOFF(llvm::StringRef MemberName, llvm::ArrayRef<char> Object) {
  llvm::SmallVector<llvm::NewArchiveMember, 1> Members;
  Members.emplace_back(llvm::MemoryBufferRef(
      llvm::StringRef(Object.data(), Object.size()), MemberName));
  auto Archive = llvm::writeArchiveToBuffer(
      Members, llvm::SymtabWritingMode::NormalSymtab,
      llvm::object::Archive::K_COFF, /*Deterministic=*/true, /*Thin=*/false);
  if (!Archive)
    return Archive.takeError();
  llvm::SmallVector<char, 0> Result;
  Result.append((*Archive)->getBuffer().begin(), (*Archive)->getBuffer().end());
  return Result;
}

LinkResult linkCOFF(llvm::ArrayRef<llvm::StringRef> Options,
                    llvm::ArrayRef<InMemoryInput> Inputs) {
  neverc::InMemoryFileStore &Store = neverc::InMemoryFileStore::instance();
  Store.clear();
  auto ClearStore = llvm::make_scope_exit([&] { Store.clear(); });
  for (const InMemoryInput &Input : Inputs) {
    llvm::SmallString<0> &Bytes =
        Store.create(Input.Path, Input.Contents.size());
    Bytes.append(Input.Contents.begin(), Input.Contents.end());
  }
  Store.freeze();

  llvm::SmallString<128> OutputPath;
  std::error_code EC = llvm::sys::fs::createTemporaryFile("neverc-coff-enclave",
                                                          "dll", OutputPath);
  if (EC)
    return {false, EC.message(), {}};
  llvm::FileRemover RemoveOutput(OutputPath);

  std::vector<std::string> Arguments = {"neverc-test-linker", "--machine=x64",
                                        "--noentry", "--nodefaultlib"};
  for (llvm::StringRef Option : Options)
    Arguments.push_back(Option.str());
  for (const InMemoryInput &Input : Inputs)
    Arguments.push_back(Input.Path);
  llvm::SmallVector<const char *, 16> Argv;
  for (const std::string &Argument : Arguments)
    Argv.push_back(Argument.c_str());

  LinkerExecutionContext Execution;
  LinkerDriverConfig Config;
  Config.executionContext = &Execution;
  Config.outputFile = OutputPath.str().str();
  Config.shared = true;
  Config.threadCount = 1;
  Config.repro = true;
  std::string Stdout;
  std::string Stderr;
  llvm::raw_string_ostream StdoutStream(Stdout);
  llvm::raw_string_ostream StderrStream(Stderr);
  bool Succeeded = false;
  llvm::CrashRecoveryContext::Enable();
  auto DisableCrashRecovery =
      llvm::make_scope_exit([] { llvm::CrashRecoveryContext::Disable(); });
  llvm::CrashRecoveryContext CRC;
  const bool Completed = CRC.RunSafely([&] {
    Succeeded = linker::coff::link(Argv, StdoutStream, StderrStream,
                                   /*exitEarly=*/false,
                                   /*disableOutput=*/false, Config);
  });
  if (!Completed)
    Succeeded = false;
  StdoutStream.flush();
  StderrStream.flush();

  LinkResult Result;
  Result.Succeeded = Succeeded;
  Result.Diagnostics = Stdout + Stderr;
  Result.Crashed = !Completed;
  if (Succeeded) {
    auto Image = llvm::MemoryBuffer::getFile(OutputPath);
    if (!Image) {
      Result.Succeeded = false;
      Result.Diagnostics += Image.getError().message();
    } else {
      Result.Image = (*Image)->getBuffer().str();
    }
  }
  return Result;
}

class PEImage {
public:
  explicit PEImage(llvm::StringRef Data) : Data(Data) {}

  bool parse() {
    if (Data.size() < 0x40 || Data[0] != 'M' || Data[1] != 'Z')
      return false;
    const uint32_t PEOffset = read32(0x3c);
    if (!contains(PEOffset, 24) ||
        Data.substr(PEOffset, 4) != llvm::StringRef("PE\0\0", 4))
      return false;
    NumberOfSections = read16(PEOffset + 6);
    const uint16_t OptionalHeaderSize = read16(PEOffset + 20);
    OptionalHeaderOffset = PEOffset + 24;
    constexpr size_t RequiredOptionalHeaderSize =
        DataDirectoryOffset + (LoadConfigDirectory + 1) * 8;
    if (NumberOfSections == 0 ||
        !contains(OptionalHeaderOffset, OptionalHeaderSize) ||
        OptionalHeaderSize < RequiredOptionalHeaderSize ||
        read16(OptionalHeaderOffset) != PE32PlusMagic)
      return false;
    if (read32(OptionalHeaderOffset + 108) <= LoadConfigDirectory)
      return false;
    ImageBase = read64(OptionalHeaderOffset + 24);
    DllCharacteristics = read16(OptionalHeaderOffset + 70);
    BaseRelocationRVA = read32(OptionalHeaderOffset + DataDirectoryOffset +
                               8 * BaseRelocationDirectory);
    BaseRelocationSize = read32(OptionalHeaderOffset + DataDirectoryOffset + 4 +
                                8 * BaseRelocationDirectory);
    LoadConfigRVA = read32(OptionalHeaderOffset + DataDirectoryOffset +
                           8 * LoadConfigDirectory);
    LoadConfigSize = read32(OptionalHeaderOffset + DataDirectoryOffset + 4 +
                            8 * LoadConfigDirectory);
    SectionTableOffset = OptionalHeaderOffset + OptionalHeaderSize;
    if (!contains(SectionTableOffset, size_t(NumberOfSections) * 40))
      return false;

    if ((BaseRelocationRVA == 0) != (BaseRelocationSize == 0) ||
        (BaseRelocationRVA != 0 &&
         !rvaToOffset(BaseRelocationRVA, BaseRelocationSize)))
      return false;

    if ((LoadConfigRVA == 0) != (LoadConfigSize == 0))
      return false;
    if (LoadConfigRVA != 0) {
      if (LoadConfigSize < sizeof(uint32_t))
        return false;
      auto Offset = rvaToOffset(LoadConfigRVA, LoadConfigSize);
      if (!Offset)
        return false;
      LoadConfigOffset = *Offset;
      DeclaredLoadConfigSize = read32(*Offset);
      if (DeclaredLoadConfigSize != LoadConfigSize)
        return false;
    }
    return true;
  }

  bool hasLoadConfigField(size_t FieldOffset, size_t FieldSize) const {
    if (FieldOffset > std::numeric_limits<size_t>::max() - FieldSize)
      return false;
    const size_t End = FieldOffset + FieldSize;
    return LoadConfigOffset && LoadConfigSize >= End &&
           DeclaredLoadConfigSize >= End &&
           rvaToOffset(LoadConfigRVA, End).has_value();
  }

  std::optional<uint64_t> loadConfig64(size_t FieldOffset) const {
    if (!hasLoadConfigField(FieldOffset, sizeof(uint64_t)))
      return std::nullopt;
    return read64(*LoadConfigOffset + FieldOffset);
  }

  std::optional<uint32_t> loadConfig32(size_t FieldOffset) const {
    if (!hasLoadConfigField(FieldOffset, sizeof(uint32_t)))
      return std::nullopt;
    return read32(*LoadConfigOffset + FieldOffset);
  }

  std::optional<uint32_t> rvaToOffset(uint32_t RVA, size_t Size) const {
    for (uint16_t I = 0; I != NumberOfSections; ++I) {
      const size_t Section = SectionTableOffset + size_t(I) * 40;
      const uint32_t VirtualSize = read32(Section + 8);
      const uint32_t VirtualAddress = read32(Section + 12);
      const uint32_t RawSize = read32(Section + 16);
      const uint32_t RawOffset = read32(Section + 20);
      const uint64_t Extent = std::max(VirtualSize, RawSize);
      if (RVA < VirtualAddress)
        continue;
      const uint64_t Delta = uint64_t(RVA) - VirtualAddress;
      if (Delta > Extent || Size > Extent - Delta)
        continue;
      // A PE section's virtual tail is zero-filled and has no corresponding
      // file bytes. Never let a malformed RVA range escape the section's raw
      // extent and accidentally read padding or the following section.
      if (Delta > RawSize || Size > uint64_t(RawSize) - Delta)
        return std::nullopt;
      const uint64_t Offset = uint64_t(RawOffset) + Delta;
      if (Offset > Data.size() || Size > Data.size() - Offset ||
          Offset > std::numeric_limits<uint32_t>::max())
        return std::nullopt;
      return static_cast<uint32_t>(Offset);
    }
    return std::nullopt;
  }

  uint64_t imageBase() const { return ImageBase; }
  uint16_t dllCharacteristics() const { return DllCharacteristics; }
  uint32_t loadConfigRVA() const { return LoadConfigRVA; }
  uint32_t loadConfigSize() const { return LoadConfigSize; }
  uint32_t declaredLoadConfigSize() const { return DeclaredLoadConfigSize; }

  std::optional<uint32_t> image32(uint64_t VA) const {
    if (VA < ImageBase || VA - ImageBase > std::numeric_limits<uint32_t>::max())
      return std::nullopt;
    auto Offset = rvaToOffset(static_cast<uint32_t>(VA - ImageBase), 4);
    if (!Offset)
      return std::nullopt;
    return read32(*Offset);
  }

  bool hasBaseRelocation(uint32_t TargetRVA, uint16_t Type) const {
    if (BaseRelocationRVA == 0 || BaseRelocationSize < 8)
      return false;
    auto DirectoryOffset = rvaToOffset(BaseRelocationRVA, BaseRelocationSize);
    if (!DirectoryOffset)
      return false;

    size_t Cursor = *DirectoryOffset;
    const size_t End = Cursor + BaseRelocationSize;
    while (Cursor < End) {
      if (End - Cursor < 8)
        return false;
      const uint32_t PageRVA = read32(Cursor);
      const uint32_t BlockSize = read32(Cursor + 4);
      if (BlockSize < 8 || BlockSize > End - Cursor ||
          (BlockSize - 8) % sizeof(uint16_t) != 0)
        return false;
      for (size_t EntryOffset = Cursor + 8; EntryOffset != Cursor + BlockSize;
           EntryOffset += sizeof(uint16_t)) {
        const uint16_t Entry = read16(EntryOffset);
        if ((Entry >> 12) == Type &&
            uint64_t(PageRVA) + (Entry & 0x0fff) == TargetRVA)
          return true;
      }
      Cursor += BlockSize;
    }
    return false;
  }

private:
  bool contains(size_t Offset, size_t Size) const {
    return Offset <= Data.size() && Size <= Data.size() - Offset;
  }
  uint16_t read16(size_t Offset) const {
    return llvm::support::endian::read16le(
        reinterpret_cast<const uint8_t *>(Data.data() + Offset));
  }
  uint32_t read32(size_t Offset) const {
    return llvm::support::endian::read32le(
        reinterpret_cast<const uint8_t *>(Data.data() + Offset));
  }
  uint64_t read64(size_t Offset) const {
    return llvm::support::endian::read64le(
        reinterpret_cast<const uint8_t *>(Data.data() + Offset));
  }

  llvm::StringRef Data;
  size_t OptionalHeaderOffset = 0;
  size_t SectionTableOffset = 0;
  uint16_t NumberOfSections = 0;
  uint16_t DllCharacteristics = 0;
  uint64_t ImageBase = 0;
  uint32_t BaseRelocationRVA = 0;
  uint32_t BaseRelocationSize = 0;
  uint32_t LoadConfigRVA = 0;
  uint32_t LoadConfigSize = 0;
  std::optional<uint32_t> LoadConfigOffset;
  uint32_t DeclaredLoadConfigSize = 0;
};

std::string baseAssembly(llvm::StringRef ReturnInstruction = "retq",
                         llvm::StringRef LoadConfigSize = "0x100",
                         llvm::StringRef ConfigDefinition =
                             ".globl __enclave_config\n__enclave_config:\n") {
  std::string Assembly = ".text\n"
                         ".def enclave_entry; .scl 2; .type 32; .endef\n"
                         ".globl enclave_entry\n"
                         "enclave_entry:\n  ";
  Assembly += ReturnInstruction.str();
  Assembly += "\n.section .rdata,\"dr\"\n.p2align 3\n";
  Assembly += ConfigDefinition.str();
  Assembly += "  .long 80\n  .long 76\n  .zero 36\n"
              "  .long 0xc0decafe\n  .zero 16\n  .quad 0x200000\n"
              "  .long 1\n  .long 1\n.p2align 3\n"
              ".globl _load_config_used\n_load_config_used:\n  .long ";
  Assembly += LoadConfigSize.str();
  Assembly +=
      "\n  .zero 0x7c\n  .quad __guard_fids_table\n"
      "  .quad __guard_fids_count\n  .quad __guard_flags\n  .zero 0x60\n"
      "  .quad __enclave_config\n.globl enclave_entry_address\n"
      "enclave_entry_address:\n  .quad enclave_entry\n";
  return Assembly;
}

InMemoryInput objectFor(llvm::StringRef Path, llvm::StringRef Triple,
                        llvm::StringRef Assembly) {
  auto Object = assembleCOFF(Triple, Assembly);
  if (!Object) {
    ADD_FAILURE() << llvm::toString(Object.takeError()).str().str();
    return {Path.str(), {}};
  }
  return {Path.str(), std::move(*Object)};
}

class COFFEnclaveLinkerTest : public ::testing::Test {
protected:
  static constexpr llvm::StringLiteral X64 = "x86_64-pc-windows-msvc";
  static constexpr llvm::StringLiteral Arm64 = "aarch64-pc-windows-msvc";

  LinkResult link(llvm::ArrayRef<llvm::StringRef> Options,
                  llvm::ArrayRef<InMemoryInput> Inputs) {
    return linkCOFF(Options, Inputs);
  }

  InMemoryInput baseObject(llvm::StringRef Triple = X64,
                           llvm::StringRef ReturnInstruction = "retq",
                           llvm::StringRef LoadConfigSize = "0x100",
                           llvm::StringRef ConfigDefinition =
                               ".globl __enclave_config\n__enclave_config:\n") {
    return objectFor(
        "/virtual/enclave-base.obj", Triple,
        baseAssembly(ReturnInstruction, LoadConfigSize, ConfigDefinition));
  }

  PEImage inspect(const LinkResult &Result) {
    EXPECT_TRUE(Result.Succeeded) << Result.Diagnostics;
    PEImage Image(Result.Image);
    EXPECT_TRUE(Image.parse()) << "not a well-formed PE32+ image";
    return Image;
  }
};

TEST_F(COFFEnclaveLinkerTest, AcceptsGuardMixed) {
  const InMemoryInput Object = baseObject();
  const LinkResult Result = link({"--guard=mixed"}, {Object});
  PEImage Image = inspect(Result);
  EXPECT_EQ(Image.loadConfigSize(), Image.declaredLoadConfigSize());
  EXPECT_NE(Image.dllCharacteristics() & DllCharacteristicsGuardCF, 0);
  ASSERT_TRUE(Image.hasLoadConfigField(LoadConfigGuardFlagsOffset, 4));
  ASSERT_TRUE(Image.hasLoadConfigField(LoadConfigGuardTableOffset, 8));
  EXPECT_NE(*Image.loadConfig64(LoadConfigGuardTableOffset), 0U);
  EXPECT_NE(*Image.loadConfig64(LoadConfigGuardCountOffset), 0U);
  const uint32_t Flags = *Image.loadConfig32(LoadConfigGuardFlagsOffset);
  EXPECT_NE(Flags & GuardCFInstrumented, 0U);
  EXPECT_NE(Flags & GuardCFFunctionTablePresent, 0U);
  EXPECT_EQ(Flags & GuardCFLongJumpTablePresent, 0U);
  EXPECT_TRUE(Image.hasBaseRelocation(
      Image.loadConfigRVA() + LoadConfigGuardTableOffset, BaseRelocationDir64));
}

TEST_F(COFFEnclaveLinkerTest, GuardMixedParserOrder) {
  const InMemoryInput Object = baseObject();
  const std::pair<llvm::StringRef, uint32_t> Cases[] = {
      {"mixed", GuardCFInstrumented | GuardCFFunctionTablePresent},
      {"mixed,no", 0},
      {"mixed,nolongjmp", GuardCFInstrumented | GuardCFFunctionTablePresent},
      {"mixed,ehcont", GuardCFInstrumented | GuardCFFunctionTablePresent |
                           GuardEHContinuationTablePresent},
      {"ehcont,mixed", GuardCFInstrumented | GuardCFFunctionTablePresent |
                           GuardEHContinuationTablePresent},
  };
  for (auto [Guard, ExpectedFlags] : Cases) {
    SCOPED_TRACE(Guard.str());
    const std::string Option = "--guard=" + Guard.str();
    const LinkResult Result = link({Option}, {Object});
    PEImage Image = inspect(Result);
    ASSERT_TRUE(Image.hasLoadConfigField(LoadConfigGuardFlagsOffset, 4));
    EXPECT_EQ(*Image.loadConfig32(LoadConfigGuardFlagsOffset), ExpectedFlags);
    EXPECT_EQ((Image.dllCharacteristics() & DllCharacteristicsGuardCF) != 0,
              ExpectedFlags != 0);
  }
}

TEST_F(COFFEnclaveLinkerTest, MixedIncludesGuardedAndUnguardedTargets) {
  const InMemoryInput Main = baseObject();
  const InMemoryInput Guarded =
      objectFor("/virtual/guarded.obj", X64,
                ".text\n.def guarded_target; .scl 2; .type 32; .endef\n"
                ".globl guarded_target\nguarded_target:\n  retq\n"
                ".def @feat.00; .scl 3; .type 0; .endef\n.globl @feat.00\n"
                ".set @feat.00, 0x800\n.section .gfids$y,\"dr\"\n"
                ".symidx guarded_target\n");
  const InMemoryInput Legacy =
      objectFor("/virtual/legacy.obj", X64,
                ".text\n.def legacy_target; .scl 2; .type 32; .endef\n"
                ".globl legacy_target\nlegacy_target:\n  retq\n"
                ".section .rdata,\"dr\"\n  .quad legacy_target\n");
  const LinkResult Result = link({"--guard=mixed"}, {Main, Guarded, Legacy});
  PEImage Image = inspect(Result);
  ASSERT_TRUE(Image.hasLoadConfigField(LoadConfigGuardTableOffset, 8));
  const uint64_t TableVA = *Image.loadConfig64(LoadConfigGuardTableOffset);
  const uint64_t Count = *Image.loadConfig64(LoadConfigGuardCountOffset);
  ASSERT_EQ(Count, 3U);
  ASSERT_GE(TableVA, Image.imageBase());
  auto TableOffset =
      Image.rvaToOffset(static_cast<uint32_t>(TableVA - Image.imageBase()),
                        static_cast<size_t>(Count) * 4);
  ASSERT_TRUE(TableOffset);
  std::vector<uint32_t> Entries;
  for (uint64_t I = 0; I != Count; ++I)
    Entries.push_back(
        llvm::support::endian::read32le(reinterpret_cast<const uint8_t *>(
            Result.Image.data() + *TableOffset + I * 4)));
  EXPECT_TRUE(std::is_sorted(Entries.begin(), Entries.end()));
  EXPECT_EQ(std::adjacent_find(Entries.begin(), Entries.end()), Entries.end());
}

TEST_F(COFFEnclaveLinkerTest, EnclaveRequiresConfig) {
  const InMemoryInput Object = objectFor(
      "/virtual/no-config.obj", X64,
      ".text\n.globl enclave_entry\nenclave_entry:\n retq\n"
      ".section .rdata,\"dr\"\n.globl _load_config_used\n_load_config_used:\n"
      ".long 0x100\n.zero 0xf4\n.quad 0\n");
  const LinkResult Result = link({"--enclave"}, {Object});
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_NE(Result.Diagnostics.find("__enclave_config"), std::string::npos)
      << Result.Diagnostics;
}

TEST_F(COFFEnclaveLinkerTest, EnclaveConfigArchiveMemberIsExtracted) {
  const InMemoryInput Main = objectFor(
      "/virtual/archive-main.obj", X64,
      ".text\n.globl enclave_entry\nenclave_entry:\n retq\n"
      ".section .rdata,\"dr\"\n.globl _load_config_used\n_load_config_used:\n"
      ".long 0x100\n.zero 0xf4\n.quad __enclave_config\n");
  const InMemoryInput ConfigObject = objectFor(
      "/virtual/archive-member.obj", X64,
      ".section .rdata,\"dr\"\n.globl __enclave_config\n__enclave_config:\n"
      ".long 80\n.long 76\n.zero 72\n");
  auto Archive = archiveCOFF("enclave-config.obj", ConfigObject.Contents);
  ASSERT_TRUE(static_cast<bool>(Archive))
      << llvm::toString(Archive.takeError()).str().str();
  InMemoryInput Library{"/virtual/enclave-config.lib", std::move(*Archive)};
  const LinkResult Result = link({"--enclave"}, {Main, Library});
  PEImage Image = inspect(Result);
  ASSERT_TRUE(Image.hasLoadConfigField(LoadConfigEnclavePointerOffset, 8));
  EXPECT_NE(*Image.loadConfig64(LoadConfigEnclavePointerOffset), 0U);
}

TEST_F(COFFEnclaveLinkerTest, EnclaveConfigSurvivesReferenceGC) {
  const InMemoryInput Object =
      objectFor("/virtual/enclave-gc.obj", X64,
                ".text\n.globl enclave_entry\nenclave_entry:\n retq\n"
                ".section .rdata$enclave,\"dr\",discard,__enclave_config\n"
                ".globl __enclave_config\n__enclave_config:\n"
                ".long 80\n.long 76\n.zero 36\n.long 0xc0decafe\n.zero 16\n"
                ".quad 0x200000\n"
                ".long 1\n.long 1\n"
                ".section .rdata$loadcfg,\"dr\"\n.p2align 3\n"
                ".globl _load_config_used\n_load_config_used:\n.long 0x100\n"
                ".zero 0x7c\n.quad __guard_fids_table\n"
                ".quad __guard_fids_count\n.quad __guard_flags\n.zero 0x60\n"
                ".quad __enclave_config\n");
  const LinkResult Result = link({"--enclave", "--opt=ref"}, {Object});
  PEImage Image = inspect(Result);
  ASSERT_TRUE(Image.hasLoadConfigField(LoadConfigEnclavePointerOffset, 8));
  const uint64_t ConfigVA = *Image.loadConfig64(LoadConfigEnclavePointerOffset);
  ASSERT_GE(ConfigVA, Image.imageBase());
  const auto Magic = Image.image32(ConfigVA + EnclaveConfigImageIdMagicOffset);
  ASSERT_TRUE(Magic);
  EXPECT_EQ(*Magic, EnclaveConfigImageIdMagic);
}

TEST_F(COFFEnclaveLinkerTest, EnclaveRequiresLoadConfig) {
  const InMemoryInput Object = objectFor(
      "/virtual/no-load-config.obj", X64,
      ".text\n.globl enclave_entry\nenclave_entry:\n retq\n"
      ".section .rdata,\"dr\"\n.globl __enclave_config\n__enclave_config:\n"
      ".long 80\n.long 76\n.zero 72\n");
  const LinkResult Result = link({"--enclave"}, {Object});
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_NE(Result.Diagnostics.find("_load_config_used"), std::string::npos)
      << Result.Diagnostics;
}

TEST_F(COFFEnclaveLinkerTest, EnclaveForceMissingLoadConfigDoesNotCrash) {
  const InMemoryInput Object = objectFor(
      "/virtual/no-load-config-force.obj", X64,
      ".text\n.globl enclave_entry\nenclave_entry:\n retq\n"
      ".section .rdata,\"dr\"\n.globl __enclave_config\n__enclave_config:\n"
      ".long 80\n.long 76\n.zero 72\n");
  const LinkResult Result = link({"--enclave", "--force"}, {Object});
  EXPECT_FALSE(Result.Crashed) << Result.Diagnostics;
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_NE(Result.Diagnostics.find("_load_config_used"), std::string::npos)
      << Result.Diagnostics;
}

TEST_F(COFFEnclaveLinkerTest, EnclaveRejectsAbsoluteLoadConfigWithoutCrash) {
  const InMemoryInput Object = objectFor(
      "/virtual/absolute-load-config.obj", X64,
      ".text\n.globl enclave_entry\nenclave_entry:\n retq\n"
      ".section .rdata,\"dr\"\n.globl __enclave_config\n__enclave_config:\n"
      ".long 80\n.long 76\n.zero 72\n"
      ".globl _load_config_used\n.set _load_config_used, 0x200000\n");
  const LinkResult Result = link({"--enclave"}, {Object});
  EXPECT_FALSE(Result.Crashed) << Result.Diagnostics;
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_NE(Result.Diagnostics.find("_load_config_used"), std::string::npos)
      << Result.Diagnostics;
}

TEST_F(COFFEnclaveLinkerTest, ExplicitIncrementalIsRejected) {
  const InMemoryInput Object = baseObject();
  const LinkResult Result = link({"--enclave", "--incremental"}, {Object});
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_NE(Result.Diagnostics.find("incremental"), std::string::npos)
      << Result.Diagnostics;
}

TEST_F(COFFEnclaveLinkerTest, IncrementalUsesEffectiveLastOption) {
  const InMemoryInput Object = baseObject();

  const LinkResult DisabledLast =
      link({"--enclave", "--incremental", "--no-incremental"}, {Object});
  EXPECT_TRUE(DisabledLast.Succeeded) << DisabledLast.Diagnostics;

  const LinkResult EnabledLast =
      link({"--enclave", "--no-incremental", "--incremental"}, {Object});
  EXPECT_FALSE(EnabledLast.Succeeded);
  EXPECT_NE(EnabledLast.Diagnostics.find("incremental"), std::string::npos)
      << EnabledLast.Diagnostics;
}

TEST_F(COFFEnclaveLinkerTest, EnclaveRejectsIncrementalDirective) {
  std::string Assembly = baseAssembly();
  Assembly += ".section .drectve,\"yn\"\n.ascii \" /incremental\"\n";
  const InMemoryInput Object =
      objectFor("/virtual/incremental-directive.obj", X64, Assembly);
  const LinkResult Result = link({"--enclave", "--no-incremental"}, {Object});
  EXPECT_FALSE(Result.Crashed) << Result.Diagnostics;
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_NE(Result.Diagnostics.find("incremental"), std::string::npos)
      << Result.Diagnostics;
}

TEST_F(COFFEnclaveLinkerTest, EnclaveDoesNotImplyGuardOrIntegrity) {
  const InMemoryInput Object = baseObject();
  const LinkResult Result = link({"--enclave"}, {Object});
  PEImage Image = inspect(Result);
  EXPECT_EQ(Image.dllCharacteristics() & DllCharacteristicsGuardCF, 0);
  EXPECT_EQ(Image.dllCharacteristics() & DllCharacteristicsForceIntegrity, 0);
}

TEST_F(COFFEnclaveLinkerTest, NormalLinkRetainsZeroConfigFallback) {
  const InMemoryInput Object = objectFor(
      "/virtual/normal-fallback.obj", X64,
      ".text\n.globl enclave_entry\nenclave_entry:\n retq\n"
      ".section .rdata,\"dr\"\n.globl _load_config_used\n_load_config_used:\n"
      ".long 0x100\n.zero 0xf4\n.quad __enclave_config\n");
  const LinkResult Result = link({}, {Object});
  PEImage Image = inspect(Result);
  ASSERT_TRUE(Image.hasLoadConfigField(LoadConfigEnclavePointerOffset, 8));
  EXPECT_EQ(*Image.loadConfig64(LoadConfigEnclavePointerOffset), 0U);
}

TEST_F(COFFEnclaveLinkerTest, EnclaveSetsLoadConfigPointerX64) {
  const InMemoryInput Object = baseObject();
  const LinkResult Result = link({"--enclave"}, {Object});
  PEImage Image = inspect(Result);
  ASSERT_TRUE(Image.hasLoadConfigField(LoadConfigEnclavePointerOffset, 8));
  const uint64_t ConfigVA = *Image.loadConfig64(LoadConfigEnclavePointerOffset);
  ASSERT_GE(ConfigVA, Image.imageBase());
  const auto Magic = Image.image32(ConfigVA + EnclaveConfigImageIdMagicOffset);
  ASSERT_TRUE(Magic);
  EXPECT_EQ(*Magic, EnclaveConfigImageIdMagic);
  EXPECT_TRUE(Image.hasBaseRelocation(Image.loadConfigRVA() +
                                          LoadConfigEnclavePointerOffset,
                                      BaseRelocationDir64));
}

TEST_F(COFFEnclaveLinkerTest, EnclaveSetsLoadConfigPointerArm64) {
  const InMemoryInput Object = baseObject(Arm64, "ret");
  const LinkResult Result = link({"--machine=arm64", "--enclave"}, {Object});
  PEImage Image = inspect(Result);
  ASSERT_TRUE(Image.hasLoadConfigField(LoadConfigEnclavePointerOffset, 8));
  const uint64_t ConfigVA = *Image.loadConfig64(LoadConfigEnclavePointerOffset);
  ASSERT_GE(ConfigVA, Image.imageBase());
  const auto Magic = Image.image32(ConfigVA + EnclaveConfigImageIdMagicOffset);
  ASSERT_TRUE(Magic);
  EXPECT_EQ(*Magic, EnclaveConfigImageIdMagic);
}

TEST_F(COFFEnclaveLinkerTest, EnclaveRejectsShortLoadConfig) {
  const InMemoryInput Object = baseObject(X64, "retq", "0xff");
  const LinkResult Result = link({"--enclave"}, {Object});
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_NE(Result.Diagnostics.find("_load_config_used"), std::string::npos)
      << Result.Diagnostics;
}

TEST_F(COFFEnclaveLinkerTest, EnclaveRejectsDeclaredSizeBeyondChunk) {
  const InMemoryInput Object =
      objectFor("/virtual/oversized-load-config.obj", X64,
                ".text\n.globl enclave_entry\nenclave_entry:\n retq\n"
                ".section .rdata,\"dr\"\n.globl __enclave_config\n"
                "__enclave_config:\n.long 80\n.long 76\n.zero 72\n"
                ".section .loadcfg,\"dr\"\n.p2align 3\n"
                ".globl _load_config_used\n_load_config_used:\n.long 0x108\n"
                ".zero 0xf4\n.quad __enclave_config\n");
  const LinkResult Result = link({"--enclave"}, {Object});
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_NE(Result.Diagnostics.find("_load_config_used"), std::string::npos)
      << Result.Diagnostics;
}

TEST_F(COFFEnclaveLinkerTest, EnclaveRejectsRelocatedLoadConfigSize) {
  const InMemoryInput Object =
      objectFor("/virtual/relocated-load-config-size.obj", X64,
                ".text\n.globl enclave_entry\nenclave_entry:\n retq\n"
                ".section .rdata$enclave,\"dr\"\n.globl __enclave_config\n"
                "__enclave_config:\n.long 80\n.long 76\n.zero 72\n"
                ".section .rdata$loadcfg,\"dr\"\n.globl _load_config_used\n"
                "_load_config_used:\n.rva enclave_entry\n");
  const LinkResult Result = link({"--enclave"}, {Object});
  EXPECT_FALSE(Result.Crashed) << Result.Diagnostics;
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_NE(Result.Diagnostics.find("changed after relocation"),
            std::string::npos)
      << Result.Diagnostics;
}

TEST_F(COFFEnclaveLinkerTest, EnclaveRejectsWrongPointer) {
  // Point at other live image data so the test proves that the field must
  // resolve to __enclave_config, not merely contain an in-image, non-null VA.
  const InMemoryInput Wrong = objectFor(
      "/virtual/wrong-pointer.obj", X64,
      ".text\n.globl enclave_entry\nenclave_entry:\n retq\n"
      ".section .rdata,\"dr\"\n.globl __enclave_config\n__enclave_config:\n"
      ".long 80\n.long 76\n.zero 72\n.p2align 3\n"
      ".globl some_other_live_data\nsome_other_live_data:\n"
      ".quad 0x12345678\n.p2align 3\n"
      ".globl _load_config_used\n_load_config_used:\n.long 0x100\n"
      ".zero 0xf4\n.quad some_other_live_data\n");
  const LinkResult Result = link({"--enclave"}, {Wrong});
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_NE(Result.Diagnostics.find("EnclaveConfigurationPointer"),
            std::string::npos)
      << Result.Diagnostics;
}

TEST_F(COFFEnclaveLinkerTest, EnclaveRejectsAbsoluteConfig) {
  const InMemoryInput Object =
      baseObject(X64, "retq", "0x100",
                 ".globl __enclave_config\n.set __enclave_config, 0x200000\n");
  const LinkResult Result = link({"--enclave"}, {Object});
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_NE(Result.Diagnostics.find("__enclave_config"), std::string::npos)
      << Result.Diagnostics;
}

} // namespace
