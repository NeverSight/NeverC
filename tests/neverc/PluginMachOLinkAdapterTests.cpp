#include "NeverCTestFixture.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/SHA256.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

class PluginMachOLinkAdapterTest : public NeverCTest {};

class ScopedMachOEnvironmentVariable {
public:
  ScopedMachOEnvironmentVariable(const char *Name, const char *Value)
      : Name(Name) {
    if (const char *Previous = std::getenv(Name)) {
      HadPrevious = true;
      PreviousValue = Previous;
    }
#ifdef _WIN32
    ::_putenv_s(Name, Value ? Value : "");
#else
    if (Value)
      ::setenv(Name, Value, 1);
    else
      ::unsetenv(Name);
#endif
  }

  ScopedMachOEnvironmentVariable(const ScopedMachOEnvironmentVariable &) =
      delete;
  ScopedMachOEnvironmentVariable &
  operator=(const ScopedMachOEnvironmentVariable &) = delete;

  ~ScopedMachOEnvironmentVariable() {
#ifdef _WIN32
    ::_putenv_s(Name.c_str(), HadPrevious ? PreviousValue.c_str() : "");
#else
    if (HadPrevious)
      ::setenv(Name.c_str(), PreviousValue.c_str(), 1);
    else
      ::unsetenv(Name.c_str());
#endif
  }

private:
  std::string Name;
  std::string PreviousValue;
  bool HadPrevious = false;
};

struct MachOIdentity {
  std::array<uint8_t, 16> Uuid{};
  uint32_t CodeSignatureOffset = 0;
  uint32_t CodeSignatureSize = 0;
  uint32_t CodeSlotCount = 0;
};

llvm::Error machoIdentityError(const llvm::Twine &Message) {
  return llvm::createStringError(llvm::inconvertibleErrorCode(), Message);
}

llvm::Expected<MachOIdentity> verifyMachOIdentity(llvm::StringRef Bytes,
                                                  llvm::StringRef BufferName) {
  llvm::Expected<std::unique_ptr<llvm::object::ObjectFile>> ObjectOrErr =
      llvm::object::ObjectFile::createObjectFile(
          llvm::MemoryBufferRef(Bytes, BufferName));
  if (!ObjectOrErr)
    return ObjectOrErr.takeError();
  const auto *Object =
      llvm::dyn_cast<llvm::object::MachOObjectFile>(ObjectOrErr->get());
  if (!Object)
    return machoIdentityError("expected a Mach-O output image");

  MachOIdentity Identity;
  bool FoundUuid = false;
  bool FoundCodeSignature = false;
  for (const llvm::object::MachOObjectFile::LoadCommandInfo &Load :
       Object->load_commands()) {
    if (Load.C.cmd == llvm::MachO::LC_UUID) {
      if (FoundUuid)
        return machoIdentityError("duplicate LC_UUID load command");
      const llvm::MachO::uuid_command Command = Object->getUuidCommand(Load);
      std::memcpy(Identity.Uuid.data(), Command.uuid, Identity.Uuid.size());
      FoundUuid = true;
      continue;
    }
    if (Load.C.cmd == llvm::MachO::LC_CODE_SIGNATURE) {
      if (FoundCodeSignature)
        return machoIdentityError("duplicate LC_CODE_SIGNATURE load command");
      const llvm::MachO::linkedit_data_command Command =
          Object->getLinkeditDataLoadCommand(Load);
      Identity.CodeSignatureOffset = Command.dataoff;
      Identity.CodeSignatureSize = Command.datasize;
      FoundCodeSignature = true;
    }
  }
  if (!FoundUuid)
    return machoIdentityError("LC_UUID load command not found");
  if (!FoundCodeSignature)
    return machoIdentityError("LC_CODE_SIGNATURE load command not found");
  if ((Identity.Uuid[6] & 0xf0U) != 0x30U ||
      (Identity.Uuid[8] & 0xc0U) != 0x80U)
    return machoIdentityError("LC_UUID has invalid version or variant bits");

  const uint64_t SignatureOffset = Identity.CodeSignatureOffset;
  const uint64_t SignatureSize = Identity.CodeSignatureSize;
  if (SignatureOffset > Bytes.size() ||
      SignatureSize > Bytes.size() - SignatureOffset)
    return machoIdentityError("LC_CODE_SIGNATURE range exceeds the image");
  if (SignatureSize <
      sizeof(llvm::MachO::CS_SuperBlob) + sizeof(llvm::MachO::CS_BlobIndex))
    return machoIdentityError("truncated code signature superblob");

  const auto *Signature = reinterpret_cast<const uint8_t *>(Bytes.data()) +
                          static_cast<size_t>(SignatureOffset);
  auto readSignature32 = [&](size_t Offset) {
    return llvm::support::endian::read32be(Signature + Offset);
  };
  if (readSignature32(offsetof(llvm::MachO::CS_SuperBlob, magic)) !=
      llvm::MachO::CSMAGIC_EMBEDDED_SIGNATURE)
    return machoIdentityError("invalid code signature superblob magic");
  const uint32_t SuperBlobLength =
      readSignature32(offsetof(llvm::MachO::CS_SuperBlob, length));
  if (SuperBlobLength > SignatureSize ||
      SuperBlobLength <
          sizeof(llvm::MachO::CS_SuperBlob) + sizeof(llvm::MachO::CS_BlobIndex))
    return machoIdentityError("invalid code signature superblob length");
  if (readSignature32(offsetof(llvm::MachO::CS_SuperBlob, count)) != 1)
    return machoIdentityError("unexpected code signature blob count");

  constexpr size_t BlobIndexOffset = sizeof(llvm::MachO::CS_SuperBlob);
  if (readSignature32(BlobIndexOffset +
                      offsetof(llvm::MachO::CS_BlobIndex, type)) !=
      llvm::MachO::CSSLOT_CODEDIRECTORY)
    return machoIdentityError("code directory blob index not found");
  const uint32_t CodeDirectoryOffset = readSignature32(
      BlobIndexOffset + offsetof(llvm::MachO::CS_BlobIndex, offset));
  if (CodeDirectoryOffset > SuperBlobLength ||
      sizeof(llvm::MachO::CS_CodeDirectory) >
          SuperBlobLength - CodeDirectoryOffset)
    return machoIdentityError("truncated code directory");

  const uint8_t *CodeDirectory = Signature + CodeDirectoryOffset;
  auto readCodeDirectory32 = [&](size_t Offset) {
    return llvm::support::endian::read32be(CodeDirectory + Offset);
  };
  auto readCodeDirectory64 = [&](size_t Offset) {
    return llvm::support::endian::read64be(CodeDirectory + Offset);
  };
  if (readCodeDirectory32(offsetof(llvm::MachO::CS_CodeDirectory, magic)) !=
      llvm::MachO::CSMAGIC_CODEDIRECTORY)
    return machoIdentityError("invalid code directory magic");
  const uint32_t CodeDirectoryLength =
      readCodeDirectory32(offsetof(llvm::MachO::CS_CodeDirectory, length));
  if (CodeDirectoryLength < sizeof(llvm::MachO::CS_CodeDirectory) ||
      CodeDirectoryLength > SuperBlobLength - CodeDirectoryOffset)
    return machoIdentityError("invalid code directory length");

  const uint32_t HashOffset =
      readCodeDirectory32(offsetof(llvm::MachO::CS_CodeDirectory, hashOffset));
  const uint32_t SpecialSlotCount = readCodeDirectory32(
      offsetof(llvm::MachO::CS_CodeDirectory, nSpecialSlots));
  const uint32_t CodeSlotCount =
      readCodeDirectory32(offsetof(llvm::MachO::CS_CodeDirectory, nCodeSlots));
  uint64_t CodeLimit =
      readCodeDirectory32(offsetof(llvm::MachO::CS_CodeDirectory, codeLimit));
  if (CodeLimit == 0)
    CodeLimit = readCodeDirectory64(
        offsetof(llvm::MachO::CS_CodeDirectory, codeLimit64));
  const uint8_t HashSize =
      CodeDirectory[offsetof(llvm::MachO::CS_CodeDirectory, hashSize)];
  const uint8_t HashType =
      CodeDirectory[offsetof(llvm::MachO::CS_CodeDirectory, hashType)];
  const uint8_t PageSizeShift =
      CodeDirectory[offsetof(llvm::MachO::CS_CodeDirectory, pageSize)];

  if (SpecialSlotCount != 0 || HashSize != llvm::MachO::CS_SHA256_LEN ||
      HashType != llvm::MachO::CS_HASHTYPE_SHA256 || PageSizeShift != 12)
    return machoIdentityError("unexpected code directory hash policy");
  if (CodeLimit != SignatureOffset)
    return machoIdentityError("code directory does not cover the image prefix");

  const uint64_t PageSize = uint64_t{1} << PageSizeShift;
  const uint64_t ExpectedCodeSlots = (CodeLimit + PageSize - 1) / PageSize;
  if (ExpectedCodeSlots != CodeSlotCount)
    return machoIdentityError("incorrect code directory page count");
  const uint64_t HashBytes = uint64_t{CodeSlotCount} * HashSize;
  if (HashOffset > CodeDirectoryLength ||
      HashBytes > CodeDirectoryLength - HashOffset)
    return machoIdentityError("code directory hash slots exceed the blob");

  const auto *Image = reinterpret_cast<const uint8_t *>(Bytes.data());
  const uint8_t *HashSlots = CodeDirectory + HashOffset;
  for (uint64_t Slot = 0; Slot != CodeSlotCount; ++Slot) {
    const uint64_t PageOffset = Slot * PageSize;
    const size_t PageBytes = static_cast<size_t>(
        std::min<uint64_t>(PageSize, CodeLimit - PageOffset));
    const std::array<uint8_t, llvm::MachO::CS_SHA256_LEN> ExpectedHash =
        llvm::SHA256::hash(llvm::ArrayRef<uint8_t>(
            Image + static_cast<size_t>(PageOffset), PageBytes));
    if (std::memcmp(HashSlots + Slot * HashSize, ExpectedHash.data(),
                    ExpectedHash.size()) != 0)
      return machoIdentityError("code directory page hash mismatch");
  }

  Identity.CodeSlotCount = CodeSlotCount;
  return Identity;
}

bool hasMachOMagic(const std::string &Bytes) {
  if (Bytes.size() < sizeof(uint32_t))
    return false;
  const auto B0 = static_cast<uint8_t>(Bytes[0]);
  const auto B1 = static_cast<uint8_t>(Bytes[1]);
  const auto B2 = static_cast<uint8_t>(Bytes[2]);
  const auto B3 = static_cast<uint8_t>(Bytes[3]);
  // MH_MAGIC_64 (0xfeedfacf) little-endian, and MH_MAGIC (0xfeedface) for the
  // 32-bit form; also accept the relocatable object magic emitted by -r.
  const bool Magic64 = B0 == 0xcf && B1 == 0xfa && B2 == 0xed && B3 == 0xfe;
  const bool Magic32 = B0 == 0xce && B1 == 0xfa && B2 == 0xed && B3 == 0xfe;
  return Magic64 || Magic32;
}

llvm::Expected<uint64_t> findMachOSymbolAddress(llvm::StringRef Bytes,
                                                llvm::StringRef Name) {
  llvm::Expected<std::unique_ptr<llvm::object::ObjectFile>> ObjectOrErr =
      llvm::object::ObjectFile::createObjectFile(
          llvm::MemoryBufferRef(Bytes, "macho-linker-test"));
  if (!ObjectOrErr)
    return ObjectOrErr.takeError();
  const auto *Object =
      llvm::dyn_cast<llvm::object::MachOObjectFile>(ObjectOrErr->get());
  if (!Object)
    return machoIdentityError("expected a Mach-O output image");

  for (const llvm::object::SymbolRef &Symbol : Object->symbols()) {
    llvm::Expected<llvm::StringRef> SymbolName = Symbol.getName();
    if (!SymbolName)
      return SymbolName.takeError();
    if (*SymbolName != Name)
      continue;
    llvm::Expected<uint32_t> Flags = Symbol.getFlags();
    if (!Flags)
      return Flags.takeError();
    if (*Flags & llvm::object::SymbolRef::SF_Undefined)
      continue;
    return Symbol.getAddress();
  }
  return machoIdentityError("Mach-O symbol not found: " + Name);
}

TEST_F(PluginMachOLinkAdapterTest,
       IcfDistinguishesReferentOffsetsWithinOneSection) {
  const fs::path DataSource = tmpFile("macho-icf-referent-data.s");
  const fs::path TextSource = tmpFile("macho-icf-referent-text.s");
  const fs::path DataObject = tmpFile("macho-icf-referent-data.o");
  const fs::path TextObject = tmpFile("macho-icf-referent-text.o");
  const fs::path Image = tmpFile("macho-icf-referent.dylib");

  // All four data symbols deliberately share one input section. The first and
  // second function reference swapped offsets whose sums collide in the ICF
  // hash; the full equality check must still distinguish their semantics.
  writeFile(DataSource, ".section __TEXT,__const\n"
                        ".globl _a, _b, _c, _d\n"
                        ".p2align 3\n"
                        "_a:\n"
                        "  .quad 1\n"
                        "_b:\n"
                        "  .quad 2\n"
                        "_c:\n"
                        "  .quad 3\n"
                        "_d:\n"
                        "  .quad 4\n");
  writeFile(TextSource,
            ".section __TEXT,__text,regular,pure_instructions\n"
            ".globl _f1, _f2, _f3\n"
            ".p2align 2\n"
            "_f1:\n"
            "  adrp x0, _a@PAGE\n"
            "  add x0, x0, _a@PAGEOFF\n"
            "  adrp x1, _d@PAGE\n"
            "  add x1, x1, _d@PAGEOFF\n"
            "  ret\n"
            "_f2:\n"
            "  adrp x0, _b@PAGE\n"
            "  add x0, x0, _b@PAGEOFF\n"
            "  adrp x1, _c@PAGE\n"
            "  add x1, x1, _c@PAGEOFF\n"
            "  ret\n"
            "_f3:\n"
            "  adrp x0, _a@PAGE\n"
            "  add x0, x0, _a@PAGEOFF\n"
            "  adrp x1, _d@PAGE\n"
            "  add x1, x1, _d@PAGEOFF\n"
            "  ret\n"
            ".subsections_via_symbols\n");

  constexpr const char *Target = "aarch64-apple-macosx13.0";
  CmdResult CompileData =
      ncc({"--no-default-config", std::string("--target=") + Target,
           "-fno-lto", "-c", DataSource.string(), "-o",
           DataObject.string()});
  ASSERT_EQ(CompileData.exitCode, 0) << CompileData.err;
  CmdResult CompileText =
      ncc({"--no-default-config", std::string("--target=") + Target,
           "-fno-lto", "-c", TextSource.string(), "-o",
           TextObject.string()});
  ASSERT_EQ(CompileText.exitCode, 0) << CompileText.err;

  CmdResult Link = ncc(
      {"--no-default-config", std::string("--target=") + Target, "-fno-lto",
       "-nostdlib", "-shared", "-ficf=all", "-Wl,--no-uuid",
       "-Wl,--no-adhoc-codesign", DataObject.string(), TextObject.string(),
       "-o", Image.string()});
  ASSERT_EQ(Link.exitCode, 0) << Link.err;

  const std::string Bytes = readFile(Image);
  ASSERT_TRUE(hasMachOMagic(Bytes));
  llvm::Expected<uint64_t> F1 = findMachOSymbolAddress(Bytes, "_f1");
  ASSERT_TRUE(static_cast<bool>(F1))
      << llvm::toString(F1.takeError()).str().str();
  llvm::Expected<uint64_t> F2 = findMachOSymbolAddress(Bytes, "_f2");
  ASSERT_TRUE(static_cast<bool>(F2))
      << llvm::toString(F2.takeError()).str().str();
  llvm::Expected<uint64_t> F3 = findMachOSymbolAddress(Bytes, "_f3");
  ASSERT_TRUE(static_cast<bool>(F3))
      << llvm::toString(F3.takeError()).str().str();

  EXPECT_EQ(*F1, *F3) << "identical functions were not folded";
  EXPECT_NE(*F1, *F2)
      << "ICF folded functions with different referent offsets";
}

// Activating a plugin session routes the built-in Mach-O linker through the
// LinkGraph adapter. With no user providers registered the projection must be
// a faithful no-op, so the emitted image stays byte-identical to the baseline.
TEST_F(PluginMachOLinkAdapterTest,
       ActivatedSessionPreservesBuiltinMachOLinkOutput) {
  const fs::path Source = tmpFile("plugin-macho-adapter.c");
  writeFile(Source, "__attribute__((visibility(\"default\"))) volatile int "
                    "plugin_macho_adapter_data = 42;\n"
                    "__attribute__((visibility(\"default\"))) int "
                    "plugin_macho_adapter_answer(void) {\n"
                    "  return plugin_macho_adapter_data;\n"
                    "}\n"
                    "void _start(void) {\n"
                    "  for (;;) plugin_macho_adapter_data = "
                    "plugin_macho_adapter_answer();\n"
                    "}\n");

  struct OutputCase {
    const char *Name;
    std::vector<std::string> Flags;
  };
  const std::vector<OutputCase> OutputCases = {
      {"executable", {"-Wl,-e,_start", "-Wl,-undefined,dynamic_lookup"}},
      {"dylib",
       {"-shared", "-Wl,-install-name,@rpath/libplugin-macho-adapter.dylib",
        "-Wl,-undefined,dynamic_lookup"}},
      {"bundle", {"-bundle", "-Wl,-undefined,dynamic_lookup"}},
      {"relocatable", {"-r"}},
  };
  const std::vector<std::string> Targets = {
      "x86_64-apple-macosx13.0",
      "aarch64-apple-macosx13.0",
  };

  for (const std::string &Target : Targets) {
    for (const OutputCase &Output : OutputCases) {
      SCOPED_TRACE(Target + "/" + Output.Name);
      const std::string Stem = "plugin-macho-" +
                               Target.substr(0, Target.find('-')) + "-" +
                               Output.Name;
      const fs::path Baseline = tmpFile(Stem + "-baseline");
      const fs::path WithPlugin = tmpFile(Stem + "-session");

      std::vector<std::string> Common = {"--no-default-config",
                                         "--target=" + Target,
                                         "-O0",
                                         "-fno-lto",
                                         "-nostdlib",
                                         "-Wl,--no-uuid",
                                         "-Wl,--no-adhoc-codesign"};
      Common.insert(Common.end(), Output.Flags.begin(), Output.Flags.end());
      Common.push_back(Source.string());

      std::vector<std::string> BaselineArguments = Common;
      BaselineArguments.insert(BaselineArguments.end(),
                               {"-o", Baseline.string()});
      CmdResult BaselineResult = ncc(BaselineArguments);
      ASSERT_EQ(BaselineResult.exitCode, 0) << BaselineResult.err;

      std::vector<std::string> PluginArguments = Common;
      PluginArguments.insert(PluginArguments.begin() + 1,
                             std::string("-fplugin=") +
                                 NEVERC_TEST_TARGET_VALID_PLUGIN);
      PluginArguments.insert(PluginArguments.end(),
                             {"-o", WithPlugin.string()});
      CmdResult PluginResult = ncc(PluginArguments);
      ASSERT_EQ(PluginResult.exitCode, 0) << PluginResult.err;

      const std::string BaselineBytes = readFile(Baseline);
      const std::string PluginBytes = readFile(WithPlugin);
      ASSERT_TRUE(hasMachOMagic(PluginBytes));
      EXPECT_EQ(PluginBytes, BaselineBytes);
    }
  }
}

TEST_F(PluginMachOLinkAdapterTest,
       UuidAndAdhocSignatureAreStableAcrossWorkerBudgets) {
  constexpr size_t PayloadBytes = 4 * 1024 * 1024 + 257;
  const fs::path Source = tmpFile("macho-output-identity.c");
  writeFile(Source, "__attribute__((used)) volatile unsigned char payload[" +
                        std::to_string(PayloadBytes) +
                        "] = {1};\n"
                        "void _start(void) {\n"
                        "  for (;;) payload[0] ^= 1;\n"
                        "}\n");

  const std::vector<std::string> Targets = {
      "x86_64-apple-macosx13.0",
      "aarch64-apple-macosx13.0",
  };
  ScopedMachOEnvironmentVariable BudgetEnabled("NEVERC_RESOURCE_BUDGET", "1");

  for (const std::string &Target : Targets) {
    SCOPED_TRACE(Target);
    const std::string Arch = Target.substr(0, Target.find('-'));
    const fs::path Object = tmpFile("macho-output-identity-" + Arch + ".o");
    CmdResult Compile =
        ncc({"--no-default-config", "--target=" + Target, "-O0", "-fno-lto",
             "-c", Source.string(), "-o", Object.string()});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.err;

    const fs::path SerialDirectory =
        tmpFile("macho-output-identity-" + Arch + "-serial-dir");
    const fs::path ParallelDirectory =
        tmpFile("macho-output-identity-" + Arch + "-parallel-dir");
    fs::create_directories(SerialDirectory);
    fs::create_directories(ParallelDirectory);
    // Both UUID generation and the ad-hoc CodeDirectory incorporate the output
    // basename. Separate directories avoid overwriting while preserving that
    // public identity input across the serial and parallel links.
    const fs::path SerialOutput = SerialDirectory / "macho-output-identity";
    const fs::path ParallelOutput = ParallelDirectory / "macho-output-identity";

    auto Link = [&](const fs::path &Output, const char *CpuTokens) {
      // The broker token budget bounds physical participants even though the
      // public compiler driver leaves the linker's logical thread count on
      // automatic selection.
      ScopedMachOEnvironmentVariable TokenBudget("NEVERC_RESOURCE_CPU_TOKENS",
                                                 CpuTokens);
      return ncc({"--no-default-config", "--target=" + Target, "-O0",
                  "-fno-lto", "-nostdlib", "-Wl,-e,__start",
                  "-Wl,--adhoc-codesign", Object.string(), "-o",
                  Output.string()});
    };

    CmdResult SerialLink = Link(SerialOutput, "1");
    ASSERT_EQ(SerialLink.exitCode, 0) << SerialLink.err;
    CmdResult ParallelLink = Link(ParallelOutput, "4");
    ASSERT_EQ(ParallelLink.exitCode, 0) << ParallelLink.err;

    const std::string SerialBytes = readFile(SerialOutput);
    const std::string ParallelBytes = readFile(ParallelOutput);
    ASSERT_GT(SerialBytes.size(), PayloadBytes);
    ASSERT_EQ(SerialBytes, ParallelBytes)
        << "Mach-O output identity changed with the physical worker budget";

    llvm::Expected<MachOIdentity> SerialIdentity =
        verifyMachOIdentity(SerialBytes, "serial-macho-output");
    ASSERT_TRUE(static_cast<bool>(SerialIdentity))
        << llvm::toString(SerialIdentity.takeError()).str().str();
    llvm::Expected<MachOIdentity> ParallelIdentity =
        verifyMachOIdentity(ParallelBytes, "parallel-macho-output");
    ASSERT_TRUE(static_cast<bool>(ParallelIdentity))
        << llvm::toString(ParallelIdentity.takeError()).str().str();
    EXPECT_EQ(SerialIdentity->Uuid, ParallelIdentity->Uuid);
    EXPECT_EQ(SerialIdentity->CodeSignatureOffset,
              ParallelIdentity->CodeSignatureOffset);
    EXPECT_EQ(SerialIdentity->CodeSignatureSize,
              ParallelIdentity->CodeSignatureSize);
    EXPECT_GT(SerialIdentity->CodeSlotCount, 1024U);

    ASSERT_GT(SerialIdentity->CodeSignatureOffset, 0U);
    std::string CorruptedBytes = SerialBytes;
    CorruptedBytes[SerialIdentity->CodeSignatureOffset - 1] ^= 1;
    llvm::Expected<MachOIdentity> CorruptedIdentity =
        verifyMachOIdentity(CorruptedBytes, "corrupted-macho-output");
    EXPECT_FALSE(static_cast<bool>(CorruptedIdentity));
    if (!CorruptedIdentity)
      llvm::consumeError(CorruptedIdentity.takeError());
  }
}

} // namespace
