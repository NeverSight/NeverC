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
#include "llvm/Object/COFF.h"
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
constexpr uint32_t GuardCFProtectDelayLoadIAT = 0x1000;
constexpr uint32_t GuardCFDelayLoadIATInOwnSection = 0x2000;
constexpr uint32_t GuardCFLongJumpTablePresent = 0x10000;
constexpr uint32_t GuardEHContinuationTablePresent = 0x400000;
constexpr uint32_t GuardCFFunctionTableSize5Bytes = 0x10000000;
constexpr uint32_t GuardMixedFlags =
    GuardCFInstrumented | GuardCFFunctionTablePresent |
    GuardCFProtectDelayLoadIAT | GuardCFDelayLoadIATInOwnSection |
    GuardCFFunctionTableSize5Bytes;
constexpr uint32_t BaseRelocationDirectory = 5;
constexpr uint32_t LoadConfigDirectory = 10;
constexpr uint16_t BaseRelocationDir64 = 10;
constexpr size_t DataDirectoryOffset = 112;
constexpr size_t LoadConfigGuardTableOffset = 0x80;
constexpr size_t LoadConfigGuardCountOffset = 0x88;
constexpr size_t LoadConfigGuardFlagsOffset = 0x90;
constexpr size_t LoadConfigGuardIATTableOffset = 0xa0;
constexpr size_t LoadConfigGuardIATCountOffset = 0xa8;
constexpr size_t LoadConfigEnclavePointerOffset = 0xf8;
constexpr size_t LoadConfigGuardEHContTableOffset = 0x108;
constexpr size_t LoadConfigGuardEHContCountOffset = 0x110;
constexpr size_t EnclaveConfigNumberOfImportsOffset = 12;
constexpr size_t EnclaveConfigImportListOffset = 16;
constexpr size_t EnclaveConfigImportEntrySizeOffset = 20;
constexpr size_t EnclaveConfigImageIdMagicOffset = 44;
constexpr uint32_t EnclaveConfigImageIdMagic = 0xc0decafe;
constexpr size_t EnclaveImportSize = 80;
constexpr size_t EnclaveImportNameOffset = 72;

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

InMemoryInput objectFor(llvm::StringRef Path, llvm::StringRef Triple,
                        llvm::StringRef Assembly);

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

llvm::Expected<llvm::SmallVector<char, 0>>
archiveCOFF(llvm::ArrayRef<InMemoryInput> Objects) {
  llvm::SmallVector<llvm::NewArchiveMember, 4> Members;
  for (const InMemoryInput &Object : Objects)
    Members.emplace_back(llvm::MemoryBufferRef(
        llvm::StringRef(Object.Contents.data(), Object.Contents.size()),
        Object.Path));
  auto Archive = llvm::writeArchiveToBuffer(
      Members, llvm::SymtabWritingMode::NormalSymtab,
      llvm::object::Archive::K_COFF, /*Deterministic=*/true, /*Thin=*/false);
  if (!Archive)
    return Archive.takeError();
  llvm::SmallVector<char, 0> Result;
  Result.append((*Archive)->getBuffer().begin(), (*Archive)->getBuffer().end());
  return Result;
}

InMemoryInput importLibraryFor(
    llvm::StringRef Path, llvm::StringRef Symbol, llvm::StringRef DLL,
    llvm::COFF::MachineTypes Machine = llvm::COFF::IMAGE_FILE_MACHINE_AMD64) {
  llvm::object::coff_import_header Header{};
  Header.Sig1 = 0;
  Header.Sig2 = UINT16_MAX;
  Header.Version = 0;
  Header.Machine = Machine;
  Header.SizeOfData = Symbol.size() + 1 + DLL.size() + 1;
  Header.TypeInfo = llvm::COFF::IMPORT_CODE | (llvm::COFF::IMPORT_NAME << 2);

  llvm::SmallVector<char, 0> Member;
  Member.append(reinterpret_cast<const char *>(&Header),
                reinterpret_cast<const char *>(&Header) + sizeof(Header));
  Member.append(Symbol.begin(), Symbol.end());
  Member.push_back('\0');
  Member.append(DLL.begin(), DLL.end());
  Member.push_back('\0');
  auto Archive = archiveCOFF("import.obj", Member);
  if (!Archive) {
    ADD_FAILURE() << llvm::toString(Archive.takeError()).str().str();
    return {Path.str(), {}};
  }
  return {Path.str(), std::move(*Archive)};
}

InMemoryInput fullImportLibraryFor(llvm::StringRef Path, llvm::StringRef Triple,
                                   llvm::StringRef ReturnInstruction) {
  const InMemoryInput Head =
      objectFor("a_head.obj", Triple,
                ".section .idata$2,\"dr\"\n"
                ".globl __full_alpha_head\n__full_alpha_head:\n"
                "  .rva .Lfull_alpha_lookup\n  .long 0\n  .long 0\n"
                "  .rva __full_alpha_iname\n  .rva .Lfull_alpha_iat\n"
                ".section .idata$4,\"dr\"\n.Lfull_alpha_lookup:\n"
                ".section .idata$5,\"dr\"\n.Lfull_alpha_iat:\n");
  std::string SymbolAssembly =
      ".text\n.def imported_full_alpha; .scl 2; .type 32; .endef\n"
      ".globl imported_full_alpha\nimported_full_alpha:\n  ";
  SymbolAssembly += ReturnInstruction.str();
  SymbolAssembly +=
      "\n.section .idata$7,\"dr\"\n  .rva __full_alpha_head\n"
      ".section .idata$5,\"dr\"\n.globl __imp_imported_full_alpha\n"
      "__imp_imported_full_alpha:\n  .rva .Lfull_alpha_hint\n  .long 0\n"
      ".section .idata$4,\"dr\"\n  .rva .Lfull_alpha_hint\n  .long 0\n"
      ".section .idata$6,\"dr\"\n.Lfull_alpha_hint:\n  .short 0\n"
      "  .asciz \"imported_full_alpha\"\n";
  const InMemoryInput Symbol =
      objectFor("m_symbol.obj", Triple, SymbolAssembly);
  const InMemoryInput Tail =
      objectFor("z_tail.obj", Triple,
                ".section .idata$4,\"dr\"\n  .quad 0\n"
                ".section .idata$5,\"dr\"\n  .quad 0\n"
                ".section .idata$7,\"dr\"\n.globl __full_alpha_iname\n"
                "__full_alpha_iname:\n  .asciz \"full-alpha.dll\"\n");
  const InMemoryInput Objects[] = {Head, Symbol, Tail};
  auto Archive = archiveCOFF(Objects);
  if (!Archive) {
    ADD_FAILURE() << llvm::toString(Archive.takeError()).str().str();
    return {Path.str(), {}};
  }
  return {Path.str(), std::move(*Archive)};
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

  std::optional<uint32_t> imageRVA32(uint32_t RVA) const {
    auto Offset = rvaToOffset(RVA, sizeof(uint32_t));
    if (!Offset)
      return std::nullopt;
    return read32(*Offset);
  }

  std::optional<std::string> imageCString(uint32_t RVA) const {
    constexpr size_t MaxNameSize = 4096;
    for (size_t Size = 1; Size <= MaxNameSize; ++Size) {
      auto Offset = rvaToOffset(RVA, Size);
      if (!Offset)
        return std::nullopt;
      if (Data[*Offset + Size - 1] == '\0')
        return Data.substr(*Offset, Size - 1).str();
    }
    return std::nullopt;
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
                             ".globl __enclave_config\n__enclave_config:\n",
                         llvm::StringRef LoadConfigGuardTail = "  .zero 0x60\n",
                         llvm::StringRef LoadConfigExtension = "") {
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
  Assembly += "\n  .zero 0x7c\n  .quad __guard_fids_table\n"
              "  .quad __guard_fids_count\n  .quad __guard_flags\n";
  Assembly += LoadConfigGuardTail.str();
  Assembly += "  .quad __enclave_config\n";
  Assembly += LoadConfigExtension.str();
  Assembly += ".globl enclave_entry_address\n"
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

  InMemoryInput
  baseObject(llvm::StringRef Triple = X64,
             llvm::StringRef ReturnInstruction = "retq",
             llvm::StringRef LoadConfigSize = "0x100",
             llvm::StringRef ConfigDefinition =
                 ".globl __enclave_config\n__enclave_config:\n",
             llvm::StringRef LoadConfigGuardTail = "  .zero 0x60\n",
             llvm::StringRef LoadConfigExtension = "") {
    return objectFor("/virtual/enclave-base.obj", Triple,
                     baseAssembly(ReturnInstruction, LoadConfigSize,
                                  ConfigDefinition, LoadConfigGuardTail,
                                  LoadConfigExtension));
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
  EXPECT_EQ(Flags, GuardMixedFlags);
  EXPECT_TRUE(Image.hasBaseRelocation(
      Image.loadConfigRVA() + LoadConfigGuardTableOffset, BaseRelocationDir64));
}

TEST_F(COFFEnclaveLinkerTest, GuardMixedParserOrder) {
  const InMemoryInput Object = baseObject();
  const std::pair<llvm::StringRef, uint32_t> Cases[] = {
      {"mixed", GuardMixedFlags},
      {"mixed,no", 0},
      {"mixed,nolongjmp", GuardMixedFlags},
      {"mixed,ehcont", GuardMixedFlags | GuardEHContinuationTablePresent},
      {"ehcont,mixed", GuardMixedFlags | GuardEHContinuationTablePresent},
      {"mixed,longjmp", GuardCFInstrumented | GuardCFFunctionTablePresent |
                            GuardCFLongJumpTablePresent},
      {"longjmp,mixed", GuardMixedFlags},
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

TEST_F(COFFEnclaveLinkerTest, MixedDoesNotClaimWritableDelayIATProtection) {
  const InMemoryInput Object =
      baseObject(X64, "callq imported_alpha\n  retq\n"
                      ".def __delayLoadHelper2; .scl 2; .type 32; .endef\n"
                      ".globl __delayLoadHelper2\n__delayLoadHelper2:\n  retq");
  const InMemoryInput Alpha = importLibraryFor("/virtual/delay-alpha.lib",
                                               "imported_alpha", "alpha.dll");
  const LinkResult Result =
      link({"--guard=mixed", "--delayload=alpha.dll"}, {Object, Alpha});
  PEImage Image = inspect(Result);
  ASSERT_TRUE(Image.hasLoadConfigField(LoadConfigGuardFlagsOffset, 4));
  EXPECT_EQ(*Image.loadConfig32(LoadConfigGuardFlagsOffset),
            GuardMixedFlags & ~(GuardCFProtectDelayLoadIAT |
                                GuardCFDelayLoadIATInOwnSection));
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
                        static_cast<size_t>(Count) * 5);
  ASSERT_TRUE(TableOffset);
  std::vector<uint32_t> Entries;
  for (uint64_t I = 0; I != Count; ++I) {
    Entries.push_back(
        llvm::support::endian::read32le(reinterpret_cast<const uint8_t *>(
            Result.Image.data() + *TableOffset + I * 5)));
    EXPECT_EQ(static_cast<uint8_t>(Result.Image[*TableOffset + I * 5 + 4]), 0U);
  }
  EXPECT_TRUE(std::is_sorted(Entries.begin(), Entries.end()));
  EXPECT_EQ(std::adjacent_find(Entries.begin(), Entries.end()), Entries.end());
}

TEST_F(COFFEnclaveLinkerTest, MixedUsesFiveByteAddressTakenIATEntries) {
  struct Case {
    llvm::StringRef Triple;
    llvm::StringRef ReturnInstruction;
    llvm::StringRef MachineOption;
  };
  const Case Cases[] = {
      {X64, "retq", "--machine=x64"},
      {Arm64, "ret", "--machine=arm64"},
  };
  constexpr llvm::StringLiteral GuardTail = "  .zero 8\n"
                                            "  .quad __guard_iat_table\n"
                                            "  .quad __guard_iat_count\n"
                                            "  .zero 0x48\n";
  constexpr llvm::StringLiteral GIAT =
      ".def @feat.00; .scl 3; .type 0; .endef\n"
      ".globl @feat.00\n"
      ".set @feat.00, 0x800\n"
      ".section .idata$5,\"dr\"\n"
      ".globl __imp_guarded_import\n"
      "__imp_guarded_import:\n  .quad 0\n"
      ".globl __imp_guarded_import_two\n"
      "__imp_guarded_import_two:\n  .quad 0\n"
      ".section .giats$y,\"dr\"\n"
      "  .symidx __imp_guarded_import\n"
      "  .symidx __imp_guarded_import_two\n";

  for (const Case &C : Cases) {
    SCOPED_TRACE(C.Triple.str());
    const InMemoryInput Main =
        baseObject(C.Triple, C.ReturnInstruction, "0x100",
                   ".globl __enclave_config\n__enclave_config:\n", GuardTail);
    const InMemoryInput AddressTakenIAT =
        objectFor("/virtual/address-taken-iat.obj", C.Triple, GIAT);
    const LinkResult Result =
        link({C.MachineOption, "--guard=mixed"}, {Main, AddressTakenIAT});
    PEImage Image = inspect(Result);

    ASSERT_TRUE(Image.hasLoadConfigField(LoadConfigGuardIATTableOffset, 8));
    ASSERT_TRUE(Image.hasLoadConfigField(LoadConfigGuardIATCountOffset, 8));
    const uint64_t TableVA = *Image.loadConfig64(LoadConfigGuardIATTableOffset);
    const uint64_t Count = *Image.loadConfig64(LoadConfigGuardIATCountOffset);
    ASSERT_EQ(Count, 2U);
    ASSERT_GE(TableVA, Image.imageBase());
    auto TableOffset = Image.rvaToOffset(
        static_cast<uint32_t>(TableVA - Image.imageBase()), Count * 5);
    ASSERT_TRUE(TableOffset);
    std::vector<uint32_t> Entries;
    for (uint64_t I = 0; I != Count; ++I) {
      Entries.push_back(
          llvm::support::endian::read32le(reinterpret_cast<const uint8_t *>(
              Result.Image.data() + *TableOffset + I * 5)));
      EXPECT_EQ(static_cast<uint8_t>(Result.Image[*TableOffset + I * 5 + 4]),
                0U);
    }
    EXPECT_TRUE(std::is_sorted(Entries.begin(), Entries.end()));
    EXPECT_EQ(std::adjacent_find(Entries.begin(), Entries.end()),
              Entries.end());
    EXPECT_TRUE(Image.hasBaseRelocation(Image.loadConfigRVA() +
                                            LoadConfigGuardIATTableOffset,
                                        BaseRelocationDir64));
  }
}

TEST_F(COFFEnclaveLinkerTest, MixedUsesFiveByteEHContinuationEntries) {
  struct Case {
    llvm::StringRef Triple;
    llvm::StringRef ReturnInstructions;
    llvm::StringRef MachineOption;
  };
  const Case Cases[] = {
      {X64, "retq", "--machine=x64"},
      {Arm64, "ret", "--machine=arm64"},
  };
  constexpr llvm::StringLiteral LoadConfigExtension =
      "  .quad 0\n"
      "  .quad __guard_eh_cont_table\n"
      "  .quad __guard_eh_cont_count\n";

  for (const Case &C : Cases) {
    SCOPED_TRACE(C.Triple.str());
    const InMemoryInput Main =
        baseObject(C.Triple, C.ReturnInstructions, "0x118",
                   ".globl __enclave_config\n__enclave_config:\n",
                   "  .zero 0x60\n", LoadConfigExtension);
    std::string EHAssembly = ".def @feat.00; .scl 3; .type 0; .endef\n"
                             ".globl @feat.00\n.set @feat.00, 0x4800\n"
                             ".text\n"
                             ".def eh_one; .scl 2; .type 32; .endef\n"
                             ".globl eh_one\neh_one:\n  ";
    EHAssembly += C.ReturnInstructions.str();
    EHAssembly += "\n.def eh_two; .scl 2; .type 32; .endef\n"
                  ".globl eh_two\neh_two:\n  ";
    EHAssembly += C.ReturnInstructions.str();
    EHAssembly += "\n.section .gehcont$y,\"dr\"\n"
                  "  .symidx eh_one\n"
                  "  .symidx eh_two\n";
    const InMemoryInput EHCont =
        objectFor("/virtual/eh-continuations.obj", C.Triple, EHAssembly);
    const LinkResult Result =
        link({C.MachineOption, "--guard=mixed,ehcont"}, {Main, EHCont});
    PEImage Image = inspect(Result);

    ASSERT_TRUE(Image.hasLoadConfigField(LoadConfigGuardEHContTableOffset, 8));
    ASSERT_TRUE(Image.hasLoadConfigField(LoadConfigGuardEHContCountOffset, 8));
    const uint64_t TableVA =
        *Image.loadConfig64(LoadConfigGuardEHContTableOffset);
    const uint64_t Count =
        *Image.loadConfig64(LoadConfigGuardEHContCountOffset);
    ASSERT_EQ(Count, 2U);
    ASSERT_GE(TableVA, Image.imageBase());
    auto TableOffset = Image.rvaToOffset(
        static_cast<uint32_t>(TableVA - Image.imageBase()), Count * 5);
    ASSERT_TRUE(TableOffset);
    std::vector<uint32_t> Entries;
    for (uint64_t I = 0; I != Count; ++I) {
      Entries.push_back(
          llvm::support::endian::read32le(reinterpret_cast<const uint8_t *>(
              Result.Image.data() + *TableOffset + I * 5)));
      EXPECT_EQ(static_cast<uint8_t>(Result.Image[*TableOffset + I * 5 + 4]),
                0U);
    }
    EXPECT_TRUE(std::is_sorted(Entries.begin(), Entries.end()));
    EXPECT_EQ(std::adjacent_find(Entries.begin(), Entries.end()),
              Entries.end());
  }
}

TEST_F(COFFEnclaveLinkerTest, MixedValidatesEHContinuationLoadConfigFields) {
  constexpr llvm::StringLiteral MissingEHFields = "  .quad 0\n"
                                                  "  .quad 0\n"
                                                  "  .quad 0\n";
  const InMemoryInput Main = baseObject(
      X64, "retq", "0x118", ".globl __enclave_config\n__enclave_config:\n",
      "  .zero 0x60\n", MissingEHFields);
  const InMemoryInput EHCont =
      objectFor("/virtual/unchecked-eh-continuation.obj", X64,
                ".def @feat.00; .scl 3; .type 0; .endef\n"
                ".globl @feat.00\n.set @feat.00, 0x4800\n"
                ".text\n.def eh_target; .scl 2; .type 32; .endef\n"
                ".globl eh_target\neh_target:\n  retq\n"
                ".section .gehcont$y,\"dr\"\n  .symidx eh_target\n");
  const LinkResult Result = link({"--guard=mixed,ehcont"}, {Main, EHCont});
  EXPECT_TRUE(Result.Succeeded) << Result.Diagnostics;
  EXPECT_NE(
      Result.Diagnostics.find("GuardEHContinuationTable not set correctly"),
      std::string::npos)
      << Result.Diagnostics;
  EXPECT_NE(
      Result.Diagnostics.find("GuardEHContinuationCount not set correctly"),
      std::string::npos)
      << Result.Diagnostics;
}

TEST_F(COFFEnclaveLinkerTest, MixedIgnoresUnwindMetadataRelocations) {
  const InMemoryInput Main = baseObject();
  const InMemoryInput Legacy =
      objectFor("/virtual/unwind-only.obj", X64,
                ".text\n"
                ".def unwind_only; .scl 2; .type 32; .endef\n"
                ".globl unwind_only\nunwind_only:\n  retq\n"
                ".def data_target; .scl 2; .type 32; .endef\n"
                ".globl data_target\ndata_target:\n  retq\n"
                ".section .pdata,\"dr\"\n"
                "  .rva unwind_only\n  .rva unwind_only\n  .rva unwind_only\n"
                ".section .rdata,\"dr\"\n  .quad data_target\n");
  const LinkResult Result = link({"--guard=mixed"}, {Main, Legacy});
  PEImage Image = inspect(Result);
  ASSERT_TRUE(Image.hasLoadConfigField(LoadConfigGuardCountOffset, 8));
  EXPECT_EQ(*Image.loadConfig64(LoadConfigGuardCountOffset), 2U);
}

TEST_F(COFFEnclaveLinkerTest, MixedDoesNotTreatExportsAsAddressTaken) {
  const InMemoryInput Main = baseObject();
  const InMemoryInput Export =
      objectFor("/virtual/export-only.obj", X64,
                ".text\n.def export_only; .scl 2; .type 32; .endef\n"
                ".globl export_only\nexport_only:\n  retq\n");
  const LinkResult Result =
      link({"--guard=mixed", "--export=export_only"}, {Main, Export});
  PEImage Image = inspect(Result);
  ASSERT_TRUE(Image.hasLoadConfigField(LoadConfigGuardCountOffset, 8));
  EXPECT_EQ(*Image.loadConfig64(LoadConfigGuardCountOffset), 1U);
}

TEST_F(COFFEnclaveLinkerTest, EnclaveSynthesizesImportIdentityList) {
  struct Case {
    llvm::StringRef Triple;
    llvm::StringRef ReturnInstructions;
    llvm::COFF::MachineTypes Machine;
    llvm::StringRef MachineOption;
  };
  const Case Cases[] = {
      {X64,
       "callq imported_alpha\n  callq imported_alpha_extra\n  callq "
       "imported_beta\n  retq",
       llvm::COFF::IMAGE_FILE_MACHINE_AMD64, "--machine=x64"},
      {Arm64,
       "bl imported_alpha\n  bl imported_alpha_extra\n  bl imported_beta\n  "
       "ret",
       llvm::COFF::IMAGE_FILE_MACHINE_ARM64, "--machine=arm64"},
  };

  for (const Case &C : Cases) {
    SCOPED_TRACE(C.Triple.str());
    const InMemoryInput Object = baseObject(C.Triple, C.ReturnInstructions);
    const InMemoryInput Alpha = importLibraryFor(
        "/virtual/alpha.lib", "imported_alpha", "alpha.dll", C.Machine);
    const InMemoryInput AlphaExtra =
        importLibraryFor("/virtual/alpha-extra.lib", "imported_alpha_extra",
                         "alpha.dll", C.Machine);
    const InMemoryInput Beta = importLibraryFor(
        "/virtual/beta.lib", "imported_beta", "beta.dll", C.Machine);
    const InMemoryInput Unused = importLibraryFor(
        "/virtual/unused.lib", "unused_import", "unused.dll", C.Machine);
    const LinkResult Result = link({C.MachineOption, "--enclave"},
                                   {Object, Alpha, AlphaExtra, Beta, Unused});
    PEImage Image = inspect(Result);
    const uint64_t ConfigVA =
        *Image.loadConfig64(LoadConfigEnclavePointerOffset);
    const auto Count =
        Image.image32(ConfigVA + EnclaveConfigNumberOfImportsOffset);
    const auto List = Image.image32(ConfigVA + EnclaveConfigImportListOffset);
    const auto EntrySize =
        Image.image32(ConfigVA + EnclaveConfigImportEntrySizeOffset);
    ASSERT_TRUE(Count);
    ASSERT_TRUE(List);
    ASSERT_TRUE(EntrySize);
    EXPECT_EQ(*Count, 2U);
    EXPECT_NE(*List, 0U);
    EXPECT_EQ(*EntrySize, EnclaveImportSize);

    auto ImportOffset = Image.rvaToOffset(*List, 2 * EnclaveImportSize);
    ASSERT_TRUE(ImportOffset);
    const llvm::StringRef ExpectedNames[] = {"alpha.dll", "beta.dll"};
    for (size_t I = 0; I != 2; ++I) {
      const size_t Offset = *ImportOffset + I * EnclaveImportSize;
      for (size_t Byte = 0; Byte != EnclaveImportNameOffset; ++Byte)
        EXPECT_EQ(static_cast<uint8_t>(Result.Image[Offset + Byte]), 0U);
      for (size_t Byte = EnclaveImportNameOffset + 4; Byte != EnclaveImportSize;
           ++Byte)
        EXPECT_EQ(static_cast<uint8_t>(Result.Image[Offset + Byte]), 0U);
      const uint32_t NameRVA =
          llvm::support::endian::read32le(reinterpret_cast<const uint8_t *>(
              Result.Image.data() + Offset + EnclaveImportNameOffset));
      const auto Name = Image.imageCString(NameRVA);
      ASSERT_TRUE(Name);
      EXPECT_EQ(*Name, ExpectedNames[I]);
    }
  }
}

TEST_F(COFFEnclaveLinkerTest, EnclaveSynthesizesFullFormatImportIdentityList) {
  struct Case {
    llvm::StringRef Triple;
    llvm::StringRef MainInstructions;
    llvm::StringRef ReturnInstruction;
    llvm::StringRef MachineOption;
  };
  const Case Cases[] = {
      {X64, "callq imported_full_alpha\n  retq", "retq", "--machine=x64"},
      {Arm64, "bl imported_full_alpha\n  ret", "ret", "--machine=arm64"},
  };

  for (const Case &C : Cases) {
    SCOPED_TRACE(C.Triple.str());
    const InMemoryInput Object = baseObject(C.Triple, C.MainInstructions);
    const InMemoryInput Import = fullImportLibraryFor(
        "/virtual/full-alpha.lib", C.Triple, C.ReturnInstruction);
    const LinkResult Result =
        link({C.MachineOption, "--enclave"}, {Object, Import});
    PEImage Image = inspect(Result);
    const uint64_t ConfigVA =
        *Image.loadConfig64(LoadConfigEnclavePointerOffset);
    const auto Count =
        Image.image32(ConfigVA + EnclaveConfigNumberOfImportsOffset);
    const auto List = Image.image32(ConfigVA + EnclaveConfigImportListOffset);
    const auto EntrySize =
        Image.image32(ConfigVA + EnclaveConfigImportEntrySizeOffset);
    ASSERT_TRUE(Count);
    ASSERT_TRUE(List);
    ASSERT_TRUE(EntrySize);
    EXPECT_EQ(*Count, 1U);
    EXPECT_NE(*List, 0U);
    EXPECT_EQ(*EntrySize, EnclaveImportSize);

    auto ImportOffset = Image.rvaToOffset(*List, EnclaveImportSize);
    ASSERT_TRUE(ImportOffset);
    const uint32_t NameRVA =
        llvm::support::endian::read32le(reinterpret_cast<const uint8_t *>(
            Result.Image.data() + *ImportOffset + EnclaveImportNameOffset));
    ASSERT_TRUE(Image.imageCString(NameRVA));
    EXPECT_EQ(*Image.imageCString(NameRVA), "full-alpha.dll");
  }
}

TEST_F(COFFEnclaveLinkerTest, EnclaveReadsFullFormatImportNameAddends) {
  constexpr llvm::StringLiteral RawImports =
      ".section .idata$2,\"dr\"\n"
      "  .rva .Llookup_one\n  .long 0\n  .long 0\n"
      "  .rva .Ldll_names\n  .rva .Liat_one\n"
      "  .rva .Llookup_two\n  .long 0\n  .long 0\n"
      "  .rva .Ldll_names+8\n  .rva .Liat_two\n  .zero 20\n"
      ".section .idata$4,\"dr\"\n.Llookup_one:\n"
      "  .rva .Lhint_one\n  .long 0\n  .quad 0\n.Llookup_two:\n"
      "  .rva .Lhint_two\n  .long 0\n  .quad 0\n"
      ".section .idata$5,\"dr\"\n.Liat_one:\n"
      "  .rva .Lhint_one\n  .long 0\n  .quad 0\n.Liat_two:\n"
      "  .rva .Lhint_two\n  .long 0\n  .quad 0\n"
      ".section .idata$6,\"dr\"\n.Lhint_one:\n"
      "  .short 0\n  .asciz \"one_func\"\n.p2align 1\n.Lhint_two:\n"
      "  .short 0\n  .asciz \"two_func\"\n"
      ".section .idata$7,\"dr\"\n.Ldll_names:\n"
      "  .asciz \"one.dll\"\n  .asciz \"two.dll\"\n";
  struct Case {
    llvm::StringRef Triple;
    llvm::StringRef ReturnInstruction;
    llvm::StringRef MachineOption;
  };
  const Case Cases[] = {
      {X64, "retq", "--machine=x64"},
      {Arm64, "ret", "--machine=arm64"},
  };

  for (const Case &C : Cases) {
    SCOPED_TRACE(C.Triple.str());
    const InMemoryInput Object = baseObject(C.Triple, C.ReturnInstruction);
    const InMemoryInput Imports =
        objectFor("/virtual/raw-imports.obj", C.Triple, RawImports);
    const LinkResult Result =
        link({C.MachineOption, "--enclave"}, {Object, Imports});
    PEImage Image = inspect(Result);
    const uint64_t ConfigVA =
        *Image.loadConfig64(LoadConfigEnclavePointerOffset);
    const uint32_t Count =
        *Image.image32(ConfigVA + EnclaveConfigNumberOfImportsOffset);
    const uint32_t List =
        *Image.image32(ConfigVA + EnclaveConfigImportListOffset);
    EXPECT_EQ(Count, 2U);
    auto ImportOffset = Image.rvaToOffset(List, Count * EnclaveImportSize);
    ASSERT_TRUE(ImportOffset);
    const llvm::StringRef ExpectedNames[] = {"one.dll", "two.dll"};
    for (size_t I = 0; I != Count; ++I) {
      const uint32_t NameRVA =
          llvm::support::endian::read32le(reinterpret_cast<const uint8_t *>(
              Result.Image.data() + *ImportOffset + I * EnclaveImportSize +
              EnclaveImportNameOffset));
      ASSERT_TRUE(Image.imageCString(NameRVA));
      EXPECT_EQ(*Image.imageCString(NameRVA), ExpectedNames[I]);
    }
  }
}

TEST_F(COFFEnclaveLinkerTest, EnclaveRejectsPartialImportDirectoryEntry) {
  const InMemoryInput Object = baseObject();
  const InMemoryInput Imports =
      objectFor("/virtual/partial-import-directory.obj", X64,
                ".section .idata$2,\"dr\"\n.zero 21\n");
  const LinkResult Result = link({"--enclave"}, {Object, Imports});
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_NE(Result.Diagnostics.find("whole number of entries"),
            std::string::npos)
      << Result.Diagnostics;
}

TEST_F(COFFEnclaveLinkerTest,
       EnclaveRejectsRelocationBackedZeroImportDirectoryEntry) {
  const InMemoryInput Object = baseObject();
  const InMemoryInput Imports =
      objectFor("/virtual/relocated-zero-import-directory.obj", X64,
                ".section .idata$2,\"dr\"\n"
                "  .rva .Llookup\n  .zero 16\n"
                ".section .idata$4,\"dr\"\n.Llookup:\n  .quad 0\n");
  const LinkResult Result = link({"--enclave"}, {Object, Imports});
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_NE(Result.Diagnostics.find("cannot derive a DLL name"),
            std::string::npos)
      << Result.Diagnostics;
}

TEST_F(COFFEnclaveLinkerTest, EnclaveRejectsImportAfterTerminator) {
  const InMemoryInput Object = baseObject();
  const InMemoryInput Imports =
      objectFor("/virtual/import-after-terminator.obj", X64,
                ".section .idata$2,\"dr\"\n.zero 20\n"
                "  .rva .Llookup\n  .long 0\n  .long 0\n"
                "  .rva .Ldll_name\n  .rva .Liat\n"
                ".section .idata$4,\"dr\"\n.Llookup:\n  .quad 0\n"
                ".section .idata$5,\"dr\"\n.Liat:\n  .quad 0\n"
                ".section .idata$7,\"dr\"\n.Ldll_name:\n"
                "  .asciz \"after-terminator.dll\"\n");
  const LinkResult Result = link({"--enclave"}, {Object, Imports});
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_NE(Result.Diagnostics.find("after its terminator"), std::string::npos)
      << Result.Diagnostics;
}

TEST_F(COFFEnclaveLinkerTest, EnclaveRejectsShortImportAfterTerminator) {
  const InMemoryInput Object = baseObject(X64, "callq imported_alpha\n  retq");
  const InMemoryInput Terminator =
      objectFor("/virtual/full-format-terminator.obj", X64,
                ".section .idata$2,\"dr\"\n.zero 20\n");
  const InMemoryInput Alpha = importLibraryFor(
      "/virtual/short-after-terminator.lib", "imported_alpha", "alpha.dll");
  const LinkResult Result = link({"--enclave"}, {Object, Terminator, Alpha});
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_NE(Result.Diagnostics.find("short imports after its terminator"),
            std::string::npos)
      << Result.Diagnostics;
}

TEST_F(COFFEnclaveLinkerTest, EnclaveWithoutImportsClearsImportMetadata) {
  const InMemoryInput Object = baseObject();
  const LinkResult Result = link({"--enclave"}, {Object});
  PEImage Image = inspect(Result);
  const uint64_t ConfigVA = *Image.loadConfig64(LoadConfigEnclavePointerOffset);
  ASSERT_TRUE(Image.image32(ConfigVA + EnclaveConfigNumberOfImportsOffset));
  ASSERT_TRUE(Image.image32(ConfigVA + EnclaveConfigImportListOffset));
  ASSERT_TRUE(Image.image32(ConfigVA + EnclaveConfigImportEntrySizeOffset));
  EXPECT_EQ(*Image.image32(ConfigVA + EnclaveConfigNumberOfImportsOffset), 0U);
  EXPECT_EQ(*Image.image32(ConfigVA + EnclaveConfigImportListOffset), 0U);
  EXPECT_EQ(*Image.image32(ConfigVA + EnclaveConfigImportEntrySizeOffset), 0U);
}

TEST_F(COFFEnclaveLinkerTest, EnclaveRejectsPrefilledImportMetadata) {
  const InMemoryInput Object =
      baseObject(X64, "retq", "0x100",
                 ".globl __enclave_config\n__enclave_config:\n"
                 ".long 80\n.long 76\n.long 1\n.long 1\n.long 1\n");
  const LinkResult Result = link({"--enclave"}, {Object});
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_NE(Result.Diagnostics.find("import fields"), std::string::npos)
      << Result.Diagnostics;
}

TEST_F(COFFEnclaveLinkerTest, EnclaveRejectsShortImportMetadataPrefix) {
  const InMemoryInput Object =
      baseObject(X64, "retq", "0x100",
                 ".globl __enclave_config\n__enclave_config:\n.long 16\n");
  const LinkResult Result = link({"--enclave"}, {Object});
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_NE(Result.Diagnostics.find("import metadata"), std::string::npos)
      << Result.Diagnostics;
}

TEST_F(COFFEnclaveLinkerTest, EnclaveRejectsOversizedConfiguration) {
  const InMemoryInput Object =
      baseObject(X64, "retq", "0x100",
                 ".globl __enclave_config\n__enclave_config:\n.long 0x1000\n");
  const LinkResult Result = link({"--enclave"}, {Object});
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_NE(Result.Diagnostics.find("too large"), std::string::npos)
      << Result.Diagnostics;
}

TEST_F(COFFEnclaveLinkerTest, EnclaveRejectsDelayLoadedImports) {
  const InMemoryInput Object = baseObject(X64, "callq imported_alpha\n  retq");
  const InMemoryInput Alpha =
      importLibraryFor("/virtual/alpha.lib", "imported_alpha", "alpha.dll");
  const LinkResult Result =
      link({"--enclave", "--delayload=alpha.dll"}, {Object, Alpha});
  EXPECT_FALSE(Result.Succeeded);
  EXPECT_NE(Result.Diagnostics.find("delay-loaded imports"), std::string::npos)
      << Result.Diagnostics;
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
