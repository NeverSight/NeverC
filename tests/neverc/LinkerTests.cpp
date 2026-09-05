#include "NeverCTestFixture.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include <cstdlib>
#include <map>

namespace {

llvm::Expected<uint64_t> findELFSymbolAddress(llvm::StringRef Bytes,
                                              llvm::StringRef Name) {
  auto Object = llvm::object::ObjectFile::createObjectFile(
      llvm::MemoryBufferRef(Bytes, "elf-linker-test"));
  if (!Object)
    return Object.takeError();
  if (!(*Object)->isELF())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "expected an ELF image");

  for (const llvm::object::SymbolRef &Symbol : (*Object)->symbols()) {
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
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "ELF symbol not found: " + Name);
}

llvm::Expected<bool> hasELFSection(llvm::StringRef Bytes,
                                   llvm::StringRef Name) {
  auto Object = llvm::object::ObjectFile::createObjectFile(
      llvm::MemoryBufferRef(Bytes, "elf-linker-test"));
  if (!Object)
    return Object.takeError();
  if (!(*Object)->isELF())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "expected an ELF image");

  for (const llvm::object::SectionRef &Section : (*Object)->sections()) {
    llvm::Expected<llvm::StringRef> SectionName = Section.getName();
    if (!SectionName)
      return SectionName.takeError();
    if (*SectionName == Name)
      return true;
  }
  return false;
}

llvm::Expected<std::string> findELFBuildId(llvm::StringRef Bytes) {
  auto Object = llvm::object::ObjectFile::createObjectFile(
      llvm::MemoryBufferRef(Bytes, "elf-linker-test"));
  if (!Object)
    return Object.takeError();
  if (!(*Object)->isELF())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "expected an ELF image");

  for (const llvm::object::SectionRef &Section : (*Object)->sections()) {
    llvm::Expected<llvm::StringRef> SectionName = Section.getName();
    if (!SectionName)
      return SectionName.takeError();
    if (*SectionName != ".note.gnu.build-id")
      continue;

    llvm::Expected<llvm::StringRef> Contents = Section.getContents();
    if (!Contents)
      return Contents.takeError();
    if (Contents->size() < 16)
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "truncated GNU build-id note");
    auto read32 = [&](const char *Data) {
      return (*Object)->isLittleEndian()
                 ? llvm::support::endian::read32le(Data)
                 : llvm::support::endian::read32be(Data);
    };
    const uint32_t NameSize = read32(Contents->data());
    const uint32_t DescSize = read32(Contents->data() + 4);
    const uint32_t Type = read32(Contents->data() + 8);
    const uint64_t DescOffset = (12ULL + NameSize + 3) & ~3ULL;
    if (NameSize != 4 || Type != llvm::ELF::NT_GNU_BUILD_ID ||
        Contents->substr(12, 4) != llvm::StringRef("GNU\0", 4) ||
        DescOffset > Contents->size() ||
        DescSize > Contents->size() - DescOffset)
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "malformed GNU build-id note");
    return Contents->substr(DescOffset, DescSize).str();
  }
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "GNU build-id note not found");
}

using ELFDynamicSymbolVersions =
    std::map<std::string, std::pair<std::string, bool>>;

llvm::Expected<ELFDynamicSymbolVersions>
readELFDynamicSymbolVersions(llvm::StringRef Bytes) {
  auto Object = llvm::object::ObjectFile::createObjectFile(
      llvm::MemoryBufferRef(Bytes, "elf-version-script-test"));
  if (!Object)
    return Object.takeError();
  const auto *ELFObject =
      llvm::dyn_cast<llvm::object::ELFObjectFileBase>(Object->get());
  if (!ELFObject)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "expected an ELF image");

  llvm::Expected<std::vector<llvm::object::VersionEntry>> Versions =
      ELFObject->readDynsymVersions();
  if (!Versions)
    return Versions.takeError();

  ELFDynamicSymbolVersions Result;
  size_t Index = 0;
  for (llvm::object::ELFSymbolRef Symbol :
       ELFObject->getDynamicSymbolIterators()) {
    if (Index == Versions->size())
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "missing dynamic symbol version entry");
    llvm::Expected<llvm::StringRef> Name = Symbol.getName();
    if (!Name)
      return Name.takeError();
    Result[Name->str()] = {(*Versions)[Index].Name,
                           (*Versions)[Index].IsVerDef};
    ++Index;
  }
  if (Index != Versions->size())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "extra dynamic symbol version entry");
  return Result;
}

class ScopedEnvironmentVariable {
public:
  ScopedEnvironmentVariable(const char *Name, const char *Value) : Name(Name) {
    if (const char *Previous = ::getenv(Name)) {
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

  ScopedEnvironmentVariable(const ScopedEnvironmentVariable &) = delete;
  ScopedEnvironmentVariable &
  operator=(const ScopedEnvironmentVariable &) = delete;

  ~ScopedEnvironmentVariable() {
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

} // namespace

class LinkerTest : public NeverCTest {
protected:
  uint64_t requireELFSymbolAddress(llvm::StringRef Bytes,
                                   llvm::StringRef Name) const {
    llvm::Expected<uint64_t> Address = findELFSymbolAddress(Bytes, Name);
    if (!Address) {
      ADD_FAILURE() << llvm::toString(Address.takeError()).str().str();
      return 0;
    }
    return *Address;
  }

  CmdResult compileObject(const fs::path &source,
                          const fs::path &object) const {
    std::vector<std::string> args;
    for (const std::string &flag : sysrootFlags())
      args.push_back(flag);
    for (const std::string &flag : archFlags())
      args.push_back(flag);
    args.insert(args.end(),
                {"-fno-lto", "-c", source.string(), "-o", object.string()});
    return ncc(args);
  }

  CmdResult assembleELFObject(const fs::path &source,
                              const fs::path &object) const {
    return ncc({"--target=x86_64-linux-gnu", "-fno-lto", "-x", "assembler",
                "-c", source.string(), "-o", object.string()});
  }

  std::vector<std::string> baseLinkArgs() const {
    std::vector<std::string> args;
    for (const std::string &flag : sysrootFlags())
      args.push_back(flag);
    for (const std::string &flag : archFlags())
      args.push_back(flag);
    for (const std::string &flag : linkFlags())
      args.push_back(flag);
    args.push_back("-fno-lto");
    return args;
  }
};

TEST_F(LinkerTest, EmbeddedLinkerDefault) {
  auto src = tmpFile("fallback.c");
  writeFile(src, "int main(void){return 0;}");
  auto r = ncc({"-###"} );
  // The -### output should reference neverc and (in-process)
  auto args = std::vector<std::string>();
  for (auto &f : sysrootFlags()) args.push_back(f);
  for (auto &f : archFlags()) args.push_back(f);
  args.push_back("-###");
  args.push_back(src.string());
  args.push_back("-o");
  args.push_back(tmpFile("fallback").string());
  auto dr = ncc(args);
  auto all = dr.err + dr.out;
  EXPECT_TRUE(all.find("(in-process)") != std::string::npos)
      << "embedded linker: missing (in-process) marker\n" << all;
}

TEST_F(LinkerTest, ELFVersionedDefinitionsUseOnlyOwnVersionNodePatterns) {
  const fs::path Source = tmpFile("versioned_own_node.s");
  const fs::path Object = tmpFile("versioned_own_node.o");
  const fs::path Script = tmpFile("versioned_own_node.map");
  const fs::path Image = tmpFile("versioned_own_node.so");

  writeFile(Source, R"(
.data
.globl foreign_exact
.symver foreign_exact, foreign_exact@@NEW
foreign_exact:
  .byte 1

.globl own_global_exact
.symver own_global_exact, own_global_exact@@NEW
own_global_exact:
  .byte 2

.globl hidden_single
.symver hidden_single, hidden_single@NEW
hidden_single:
  .byte 3

.globl own_local_wild
.symver own_local_wild, own_local_wild@@NEW
own_local_wild:
  .byte 4
)");
  writeFile(Script, R"(
OLD {
  local: foreign_exact;
};
NEW {
  global: own_global_*; hidden_*;
  local: own_global_exact; hidden_single; own_local_*;
};
)");

  CmdResult Assemble = assembleELFObject(Source, Object);
  ASSERT_EQ(Assemble.exitCode, 0) << Assemble.err;
  CmdResult Link = ncc({"--target=x86_64-linux-gnu", "-nostdlib", "-shared",
                        "-fno-lto", "-Wl,--version-script=" + Script.string(),
                        Object.string(), "-o", Image.string()});
  ASSERT_EQ(Link.exitCode, 0) << Link.err;

  llvm::Expected<ELFDynamicSymbolVersions> Versions =
      readELFDynamicSymbolVersions(readFile(Image));
  ASSERT_TRUE(static_cast<bool>(Versions))
      << llvm::toString(Versions.takeError()).str().str();
  auto ExpectVersion = [&](llvm::StringRef Name, llvm::StringRef Version,
                           bool IsDefault) {
    SCOPED_TRACE(Name.str());
    auto It = Versions->find(Name.str());
    ASSERT_NE(It, Versions->end());
    EXPECT_EQ(It->second.first, Version);
    EXPECT_EQ(It->second.second, IsDefault);
  };
  ExpectVersion("foreign_exact", "NEW", true);
  ExpectVersion("own_global_exact", "NEW", true);
  ExpectVersion("hidden_single", "NEW", false);
  EXPECT_EQ(Versions->count("own_local_wild"), 0U);
}

TEST_F(LinkerTest, ELFQuotedVersionScriptNamesAreLiteral) {
  const fs::path Source = tmpFile("version_quoted_literal.s");
  const fs::path Object = tmpFile("version_quoted_literal.o");
  const fs::path Script = tmpFile("version_quoted_literal.map");
  const fs::path Image = tmpFile("version_quoted_literal.so");

  writeFile(Source, R"(
.data
.globl "literal[abc]"
"literal[abc]":
  .byte 1
.globl literala
literala:
  .byte 2
)");
  writeFile(Script, R"(
LITERAL {
  global: "literal[abc]";
  local: *;
};
)");

  CmdResult Assemble = assembleELFObject(Source, Object);
  ASSERT_EQ(Assemble.exitCode, 0) << Assemble.err;
  CmdResult Link = ncc({"--target=x86_64-linux-gnu", "-nostdlib", "-shared",
                        "-fno-lto", "-Wl,--version-script=" + Script.string(),
                        Object.string(), "-o", Image.string()});
  ASSERT_EQ(Link.exitCode, 0) << Link.err;

  llvm::Expected<ELFDynamicSymbolVersions> Versions =
      readELFDynamicSymbolVersions(readFile(Image));
  ASSERT_TRUE(static_cast<bool>(Versions))
      << llvm::toString(Versions.takeError()).str().str();
  auto It = Versions->find("literal[abc]");
  ASSERT_NE(It, Versions->end());
  EXPECT_EQ(It->second.first, "LITERAL");
  EXPECT_TRUE(It->second.second);
  EXPECT_EQ(Versions->count("literala"), 0U);
}

TEST_F(LinkerTest,
       ELFLargeVersionScriptWildcardAssignmentIsDeterministicAcrossBudgets) {
  const fs::path Source = tmpFile("large_version_wildcards.s");
  const fs::path Object = tmpFile("large_version_wildcards.o");
  const fs::path Script = tmpFile("large_version_wildcards.map");
  const fs::path SerialImage = tmpFile("large_version_serial.so");
  const fs::path ParallelImage = tmpFile("large_version_parallel.so");

  std::string Assembly = ".data\n.space 17825792, 0\n";
  for (unsigned Group = 0; Group != 64; ++Group) {
    for (unsigned Index = 0; Index != 64; ++Index) {
      const std::string Name =
          "group" + std::to_string(Group) + "_symbol" + std::to_string(Index);
      Assembly += ".globl " + Name + "\n" + Name + ":\n  .byte 0\n";
    }
  }
  Assembly += R"(
.globl exact_wins
exact_wins:
  .byte 1
.globl overlap_item
overlap_item:
  .byte 2
.globl suffix_tail
suffix_tail:
  .byte 3
.globl bracket_7
bracket_7:
  .byte 4
.globl local_only_symbol
local_only_symbol:
  .byte 5
)";
  writeFile(Source, Assembly);

  std::string VersionScript;
  for (unsigned Group = 0; Group != 64; ++Group)
    VersionScript += "V" + std::to_string(Group) + " { global: group" +
                     std::to_string(Group) + "_*; };\n";
  VersionScript += R"(
EXACT { global: exact_wins; };
WILD_LATE { global: exact_*; };
OVER_OLD { global: overlap_*; };
OVER_NEW { global: overlap_*; };
SUFFIX { global: *_tail; };
BRACKET { global: bracket_[0-9]; };
LOCAL { local: local_only_*; *; };
)";
  writeFile(Script, VersionScript);

  CmdResult Assemble = assembleELFObject(Source, Object);
  ASSERT_EQ(Assemble.exitCode, 0) << Assemble.err;
  ASSERT_GT(fileSize(Object), 16U * 1024U * 1024U);

  auto Link = [&](const fs::path &Output) {
    return ncc({"--target=x86_64-linux-gnu", "-nostdlib", "-shared", "-fno-lto",
                "-Wl,--version-script=" + Script.string(), Object.string(),
                "-o", Output.string()});
  };
  ScopedEnvironmentVariable Budget("NEVERC_RESOURCE_BUDGET", "1");
  {
    ScopedEnvironmentVariable Tokens("NEVERC_RESOURCE_CPU_TOKENS", "1");
    CmdResult SerialLink = Link(SerialImage);
    ASSERT_EQ(SerialLink.exitCode, 0) << SerialLink.err;
  }
  {
    ScopedEnvironmentVariable Tokens("NEVERC_RESOURCE_CPU_TOKENS", "4");
    CmdResult ParallelLink = Link(ParallelImage);
    ASSERT_EQ(ParallelLink.exitCode, 0) << ParallelLink.err;
  }

  const std::string SerialBytes = readFile(SerialImage);
  const std::string ParallelBytes = readFile(ParallelImage);
  EXPECT_TRUE(SerialBytes == ParallelBytes)
      << "version-script output changed under a larger worker grant";
  llvm::Expected<ELFDynamicSymbolVersions> SerialVersions =
      readELFDynamicSymbolVersions(SerialBytes);
  ASSERT_TRUE(static_cast<bool>(SerialVersions))
      << llvm::toString(SerialVersions.takeError()).str().str();
  llvm::Expected<ELFDynamicSymbolVersions> ParallelVersions =
      readELFDynamicSymbolVersions(ParallelBytes);
  ASSERT_TRUE(static_cast<bool>(ParallelVersions))
      << llvm::toString(ParallelVersions.takeError()).str().str();
  EXPECT_TRUE(*SerialVersions == *ParallelVersions)
      << "dynamic symbol versions changed under a larger worker grant";

  auto ExpectVersion = [&](llvm::StringRef Name, llvm::StringRef Version) {
    SCOPED_TRACE(Name.str());
    auto It = SerialVersions->find(Name.str());
    ASSERT_NE(It, SerialVersions->end());
    EXPECT_EQ(It->second.first, Version);
    EXPECT_TRUE(It->second.second);
  };
  ExpectVersion("group0_symbol0", "V0");
  ExpectVersion("group63_symbol63", "V63");
  ExpectVersion("exact_wins", "EXACT");
  ExpectVersion("overlap_item", "OVER_NEW");
  ExpectVersion("suffix_tail", "SUFFIX");
  ExpectVersion("bracket_7", "BRACKET");
  EXPECT_EQ(SerialVersions->count("local_only_symbol"), 0U);
}

TEST_F(LinkerTest, ElfRelocatableDropsUnusedFatLTOSections) {
  const fs::path firstSource = tmpFile("fat_lto_first.s");
  const fs::path secondSource = tmpFile("fat_lto_second.s");
  const fs::path firstObject = tmpFile("fat_lto_first.o");
  const fs::path secondObject = tmpFile("fat_lto_second.o");
  const fs::path output = tmpFile("fat_lto_combined.o");

  writeFile(firstSource, R"(
.section .text.first,"ax",@progbits
.globl fat_lto_first
.type fat_lto_first,@function
fat_lto_first:
  ret
.section .llvm.lto,"e",@llvm_lto
  .byte 0x42, 0x43, 0xc0, 0xde
)");
  writeFile(secondSource, R"(
.section .text.second,"ax",@progbits
.globl fat_lto_second
.type fat_lto_second,@function
fat_lto_second:
  ret
.section .llvm.lto,"e",@llvm_lto
  .byte 0xde, 0xc0, 0x43, 0x42
)");

  for (const std::pair<fs::path, fs::path> &input :
       {std::pair{firstSource, firstObject},
        std::pair{secondSource, secondObject}}) {
    CmdResult assemble =
        ncc({"--target=x86_64-linux-gnu", "-x", "assembler", "-c",
             input.first.string(), "-o", input.second.string()});
    ASSERT_EQ(assemble.exitCode, 0) << assemble.err;
  }

  CmdResult link =
      ncc({"--target=x86_64-linux-gnu", "-nostdlib", "-fno-lto", "-r",
           firstObject.string(), secondObject.string(), "-o", output.string()});
  ASSERT_EQ(link.exitCode, 0) << link.err;

  const std::string bytes = readFile(output);
  llvm::Expected<bool> hasText = hasELFSection(bytes, ".text");
  ASSERT_TRUE(static_cast<bool>(hasText))
      << llvm::toString(hasText.takeError()).str().str();
  EXPECT_TRUE(*hasText);
  llvm::Expected<uint64_t> firstAddress =
      findELFSymbolAddress(bytes, "fat_lto_first");
  ASSERT_TRUE(static_cast<bool>(firstAddress))
      << llvm::toString(firstAddress.takeError()).str().str();
  llvm::Expected<uint64_t> secondAddress =
      findELFSymbolAddress(bytes, "fat_lto_second");
  ASSERT_TRUE(static_cast<bool>(secondAddress))
      << llvm::toString(secondAddress.takeError()).str().str();
  llvm::Expected<bool> hasFatLTO = hasELFSection(bytes, ".llvm.lto");
  ASSERT_TRUE(static_cast<bool>(hasFatLTO))
      << llvm::toString(hasFatLTO.takeError()).str().str();
  EXPECT_FALSE(*hasFatLTO)
      << "unused raw FatLTO payloads must not be concatenated by -r";
}

TEST_F(LinkerTest, AutorouteObjectInput) {
  auto src = tmpFile("autoroute.c");
  writeFile(src, "int main(void){return 0;}");
  auto obj = tmpFile("autoroute.o");
  auto exe = tmpFile("autoroute");

  std::vector<std::string> c;
  for (auto &f : sysrootFlags()) c.push_back(f);
  for (auto &f : archFlags()) c.push_back(f);
  c.insert(c.end(), {"-c", src.string(), "-o", obj.string()});
  ASSERT_EQ(ncc(c).exitCode, 0);

  std::vector<std::string> l;
  for (auto &f : sysrootFlags()) l.push_back(f);
  for (auto &f : archFlags()) l.push_back(f);
  for (auto &f : linkFlags()) l.push_back(f);
  l.insert(l.end(), {obj.string(), "-o", exe.string()});
  ASSERT_EQ(ncc(l).exitCode, 0);

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0);
}

TEST_F(LinkerTest, IcfPreservesDistinctExceptionPersonalities) {
  const fs::path source = tmpFile("icf_personality.s");
  const fs::path object = tmpFile("icf_personality.o");
  const fs::path image = tmpFile("icf_personality.elf");

  writeFile(source, R"(
.text
.hidden personality_a
.type personality_a,@function
personality_a:
  ret
.hidden personality_b
.type personality_b,@function
personality_b:
  ret

.section .text.exception_a,"ax",@progbits
.globl exception_a
.type exception_a,@function
exception_a:
.cfi_startproc
.cfi_personality 0x1b, personality_a
  ret
.cfi_endproc

.section .text.exception_b,"ax",@progbits
.globl exception_b
.type exception_b,@function
exception_b:
.cfi_startproc
.cfi_personality 0x1b, personality_b
  ret
.cfi_endproc

.section .text.plain_a,"ax",@progbits
.globl plain_a
.type plain_a,@function
plain_a:
.cfi_startproc
  ret
.cfi_endproc

.section .text.plain_b,"ax",@progbits
.globl plain_b
.type plain_b,@function
plain_b:
.cfi_startproc
  ret
.cfi_endproc
)");

  CmdResult assemble = ncc({"--target=aarch64-linux-gnu", "-x", "assembler",
                            "-c", source.string(), "-o", object.string()});
  ASSERT_EQ(assemble.exitCode, 0) << assemble.err;

  CmdResult link =
      ncc({"--target=aarch64-linux-gnu", "-nostdlib", "-fno-lto", "-ficf=all",
           "-Wl,-e,exception_a", object.string(), "-o", image.string()});
  ASSERT_EQ(link.exitCode, 0) << link.err;

  const std::string bytes = readFile(image);
  const uint64_t exceptionA = requireELFSymbolAddress(bytes, "exception_a");
  const uint64_t exceptionB = requireELFSymbolAddress(bytes, "exception_b");
  const uint64_t plainA = requireELFSymbolAddress(bytes, "plain_a");
  const uint64_t plainB = requireELFSymbolAddress(bytes, "plain_b");

  EXPECT_NE(exceptionA, exceptionB)
      << "functions with distinct unwind personalities must not be folded";
  EXPECT_EQ(plainA, plainB)
      << "ordinary FDEs must remain eligible for identical-code folding";
}

TEST_F(LinkerTest, IcfPreservesAbsoluteExceptionPersonalities) {
  const fs::path source = tmpFile("icf_absolute_personality.s");
  const fs::path object = tmpFile("icf_absolute_personality.o");
  const fs::path image = tmpFile("icf_absolute_personality.elf");

  writeFile(source, R"(
.set personality_absolute_a, 1
.set personality_absolute_b, 2

.section .text.absolute_a,"ax",@progbits
.globl absolute_a
.type absolute_a,@function
absolute_a:
.cfi_startproc
.cfi_personality 0x00, personality_absolute_a
  ret
.cfi_endproc

.section .text.absolute_b,"ax",@progbits
.globl absolute_b
.type absolute_b,@function
absolute_b:
.cfi_startproc
.cfi_personality 0x00, personality_absolute_b
  ret
.cfi_endproc

.section .text.absolute_plain_a,"ax",@progbits
.globl absolute_plain_a
.type absolute_plain_a,@function
absolute_plain_a:
.cfi_startproc
  ret
.cfi_endproc

.section .text.absolute_plain_b,"ax",@progbits
.globl absolute_plain_b
.type absolute_plain_b,@function
absolute_plain_b:
.cfi_startproc
  ret
.cfi_endproc
)");

  CmdResult assemble = ncc({"--target=x86_64-linux-gnu", "-x", "assembler",
                            "-c", source.string(), "-o", object.string()});
  ASSERT_EQ(assemble.exitCode, 0) << assemble.err;

  CmdResult link =
      ncc({"--target=x86_64-linux-gnu", "-nostdlib", "-fno-lto", "-ficf=all",
           "-Wl,-e,absolute_a", object.string(), "-o", image.string()});
  ASSERT_EQ(link.exitCode, 0) << link.err;

  const std::string bytes = readFile(image);
  const uint64_t absoluteA = requireELFSymbolAddress(bytes, "absolute_a");
  const uint64_t absoluteB = requireELFSymbolAddress(bytes, "absolute_b");
  const uint64_t plainA = requireELFSymbolAddress(bytes, "absolute_plain_a");
  const uint64_t plainB = requireELFSymbolAddress(bytes, "absolute_plain_b");

  EXPECT_NE(absoluteA, absoluteB)
      << "absolute unwind personalities must not be folded";
  EXPECT_EQ(plainA, plainB)
      << "ordinary FDEs must remain eligible for identical-code folding";
}

TEST_F(LinkerTest, IcfPreservesDistinctLSDAs) {
  const fs::path source = tmpFile("icf_lsda.s");
  const fs::path object = tmpFile("icf_lsda.o");
  const fs::path image = tmpFile("icf_lsda.elf");

  writeFile(source, R"(
.section .gcc_except_table.lsda_a,"a",@progbits
lsda_a:
  .byte 0

.section .gcc_except_table.lsda_b,"a",@progbits
lsda_b:
  .byte 1

.section .text.lsda_function_a,"ax",@progbits
.globl lsda_function_a
.type lsda_function_a,@function
lsda_function_a:
.cfi_startproc
.cfi_lsda 0x1b, lsda_a
  ret
.cfi_endproc

.section .text.lsda_function_b,"ax",@progbits
.globl lsda_function_b
.type lsda_function_b,@function
lsda_function_b:
.cfi_startproc
.cfi_lsda 0x1b, lsda_b
  ret
.cfi_endproc

.section .text.lsda_plain_a,"ax",@progbits
.globl lsda_plain_a
.type lsda_plain_a,@function
lsda_plain_a:
.cfi_startproc
  ret
.cfi_endproc

.section .text.lsda_plain_b,"ax",@progbits
.globl lsda_plain_b
.type lsda_plain_b,@function
lsda_plain_b:
.cfi_startproc
  ret
.cfi_endproc
)");

  CmdResult assemble = ncc({"--target=x86_64-linux-gnu", "-x", "assembler",
                            "-c", source.string(), "-o", object.string()});
  ASSERT_EQ(assemble.exitCode, 0) << assemble.err;

  CmdResult link =
      ncc({"--target=x86_64-linux-gnu", "-nostdlib", "-fno-lto", "-ficf=all",
           "-Wl,-e,lsda_function_a", object.string(), "-o", image.string()});
  ASSERT_EQ(link.exitCode, 0) << link.err;

  const std::string bytes = readFile(image);
  const uint64_t functionA = requireELFSymbolAddress(bytes, "lsda_function_a");
  const uint64_t functionB = requireELFSymbolAddress(bytes, "lsda_function_b");
  const uint64_t plainA = requireELFSymbolAddress(bytes, "lsda_plain_a");
  const uint64_t plainB = requireELFSymbolAddress(bytes, "lsda_plain_b");

  EXPECT_NE(functionA, functionB)
      << "functions with distinct exception tables must not be folded";
  EXPECT_EQ(plainA, plainB)
      << "ordinary FDEs must remain eligible for identical-code folding";
}

TEST_F(LinkerTest, IcfKeepsStrictestFoldedAlignment) {
  const fs::path source = tmpFile("icf_alignment.s");
  const fs::path object = tmpFile("icf_alignment.o");
  const fs::path image = tmpFile("icf_alignment.elf");

  writeFile(source, R"(
.section .text.00_prefix,"ax",@progbits
.globl prefix
prefix:
  nop

.section .text.10_low_alignment,"ax",@progbits
.p2align 2
.globl low_alignment
.type low_alignment,@function
low_alignment:
  ret

.section .text.20_strict_alignment,"ax",@progbits
.p2align 12
.globl strict_alignment
.type strict_alignment,@function
strict_alignment:
  ret
)");

  CmdResult assemble = ncc({"--target=aarch64-linux-gnu", "-x", "assembler",
                            "-c", source.string(), "-o", object.string()});
  ASSERT_EQ(assemble.exitCode, 0) << assemble.err;

  CmdResult link =
      ncc({"--target=aarch64-linux-gnu", "-nostdlib", "-fno-lto", "-ficf=all",
           "-Wl,-e,prefix", object.string(), "-o", image.string()});
  ASSERT_EQ(link.exitCode, 0) << link.err;

  const std::string bytes = readFile(image);
  const uint64_t low = requireELFSymbolAddress(bytes, "low_alignment");
  const uint64_t strict = requireELFSymbolAddress(bytes, "strict_alignment");
  const uint64_t prefix = requireELFSymbolAddress(bytes, "prefix");

  ASSERT_EQ(low, strict) << "the inputs must form one ICF class";
  EXPECT_EQ(low % 4096, 0u)
      << "the folded address must retain the strictest input alignment";
  EXPECT_LT(prefix, low);
  EXPECT_GE(low - prefix, 4096u)
      << "the strict alignment must affect placement after prior text";
}

TEST_F(LinkerTest, IcfDistinguishesPreemptibleRelocations) {
  const fs::path source = tmpFile("icf_preemptible.s");
  const fs::path object = tmpFile("icf_preemptible.o");
  const fs::path image = tmpFile("icf_preemptible.so");

  writeFile(source, R"(
.section .text.targets,"ax",@progbits
.hidden fixed_target
.globl fixed_target
.type fixed_target,@function
.globl dynamic_target
.type dynamic_target,@function
fixed_target:
dynamic_target:
  ret

.section .text.call_fixed,"ax",@progbits
.globl call_fixed
.type call_fixed,@function
call_fixed:
  b fixed_target

.section .text.call_dynamic,"ax",@progbits
.globl call_dynamic
.type call_dynamic,@function
call_dynamic:
  b dynamic_target

.section .text.control_targets,"ax",@progbits
.hidden control_target_a
.globl control_target_a
.type control_target_a,@function
.hidden control_target_b
.globl control_target_b
.type control_target_b,@function
control_target_a:
control_target_b:
  ret

.section .text.call_control_a,"ax",@progbits
.globl call_control_a
.type call_control_a,@function
call_control_a:
  b control_target_a

.section .text.call_control_b,"ax",@progbits
.globl call_control_b
.type call_control_b,@function
call_control_b:
  b control_target_b
)");

  CmdResult assemble = ncc({"--target=aarch64-linux-gnu", "-x", "assembler",
                            "-c", source.string(), "-o", object.string()});
  ASSERT_EQ(assemble.exitCode, 0) << assemble.err;

  CmdResult link =
      ncc({"--target=aarch64-linux-gnu", "-nostdlib", "-shared", "-fno-lto",
           "-ficf=all", object.string(), "-o", image.string()});
  ASSERT_EQ(link.exitCode, 0) << link.err;

  const std::string bytes = readFile(image);
  const uint64_t fixedTarget = requireELFSymbolAddress(bytes, "fixed_target");
  const uint64_t dynamicTarget =
      requireELFSymbolAddress(bytes, "dynamic_target");
  const uint64_t callFixed = requireELFSymbolAddress(bytes, "call_fixed");
  const uint64_t callDynamic = requireELFSymbolAddress(bytes, "call_dynamic");
  const uint64_t controlTargetA =
      requireELFSymbolAddress(bytes, "control_target_a");
  const uint64_t controlTargetB =
      requireELFSymbolAddress(bytes, "control_target_b");
  const uint64_t callControlA =
      requireELFSymbolAddress(bytes, "call_control_a");
  const uint64_t callControlB =
      requireELFSymbolAddress(bytes, "call_control_b");

  ASSERT_EQ(fixedTarget, dynamicTarget)
      << "the targets must differ only in preemptibility";
  EXPECT_NE(callFixed, callDynamic)
      << "a preemptible target must keep relocation identity distinct";
  ASSERT_EQ(controlTargetA, controlTargetB)
      << "the control targets must differ only by hidden symbol identity";
  EXPECT_EQ(callControlA, callControlB)
      << "equivalent non-preemptible relocations must remain foldable";
}

TEST_F(LinkerTest, DuplicateLazyLibraryIsLoadedOnce) {
  if (!isLinux())
    GTEST_SKIP() << "duplicate -l coalescing is an ELF linker behavior";

  const fs::path dir = tmpFile("duplicate_lazy_library");
  fs::create_directories(dir);
  const fs::path librarySource = dir / "repeat.c";
  const fs::path libraryObject = dir / "repeat.o";
  const fs::path archive = dir / "librepeat.a";
  const fs::path mainSource = dir / "main.c";
  const fs::path mainObject = dir / "main.o";
  const fs::path executable = dir / "repeat";

  writeFile(librarySource, "int repeated_value(void) { return 29; }");
  writeFile(mainSource,
            "int repeated_value(void); "
            "int main(void) { return repeated_value() == 29 ? 0 : 1; }");

  CmdResult libraryCompile = compileObject(librarySource, libraryObject);
  ASSERT_EQ(libraryCompile.exitCode, 0) << libraryCompile.err;
  CmdResult mainCompile = compileObject(mainSource, mainObject);
  ASSERT_EQ(mainCompile.exitCode, 0) << mainCompile.err;

  CmdResult archiveBuild = ncc(
      {"--emit-static-lib", libraryObject.string(), "-o", archive.string()});
  ASSERT_EQ(archiveBuild.exitCode, 0) << archiveBuild.err;

  std::vector<std::string> linkArgs = baseLinkArgs();
  linkArgs.insert(linkArgs.end(),
                  {"-ftime-trace", "-ftime-trace-granularity=1",
                   mainObject.string(), "-L" + dir.string(), "-lrepeat",
                   "-lrepeat", "-o", executable.string()});
  CmdResult link = ncc(linkArgs);
  ASSERT_EQ(link.exitCode, 0) << link.err;
  EXPECT_EQ(exec(executable.string(), {}).exitCode, 0);

  const fs::path timeTrace(executable.string() + ".time-trace");
  ASSERT_TRUE(fs::exists(timeTrace));
  auto parsed = llvm::json::parse(readFile(timeTrace));
  ASSERT_TRUE(static_cast<bool>(parsed));
  const llvm::json::Object *root = parsed->getAsObject();
  ASSERT_NE(root, nullptr);
  const llvm::json::Array *events = root->getArray("traceEvents");
  ASSERT_NE(events, nullptr);

  size_t archiveLoads = 0;
  for (const llvm::json::Value &value : *events) {
    const llvm::json::Object *event = value.getAsObject();
    if (!event || event->getString("name") != "Load input files")
      continue;
    const llvm::json::Object *eventArgs = event->getObject("args");
    if (eventArgs && eventArgs->getString("detail") == archive.string())
      ++archiveLoads;
  }
  EXPECT_EQ(archiveLoads, 1U)
      << "a repeated normal -l archive must be loaded at most once";
}

TEST_F(LinkerTest, LibraryScriptOccurrencesAreNotCoalesced) {
  if (!isLinux())
    GTEST_SKIP() << "GNU linker scripts are an ELF linker behavior";

  const fs::path dir = tmpFile("library_script_occurrences");
  fs::create_directories(dir);
  const fs::path memberSource = dir / "member.c";
  const fs::path memberObject = dir / "member.o";
  const fs::path libraryScript = dir / "libscript.a";
  const fs::path mainSource = dir / "main.c";
  const fs::path mainObject = dir / "main.o";

  writeFile(memberSource,
            "int library_script_side_effect(void) { return 43; }");
  writeFile(mainSource, "int main(void) { return 0; }");
  CmdResult memberCompile = compileObject(memberSource, memberObject);
  ASSERT_EQ(memberCompile.exitCode, 0) << memberCompile.err;
  CmdResult mainCompile = compileObject(mainSource, mainObject);
  ASSERT_EQ(mainCompile.exitCode, 0) << mainCompile.err;

  // A file found through -l may itself be a linker script.  INPUT(object)
  // has positional effects, so two occurrences must be parsed twice rather
  // than treated like repeated lazy archives.
  writeFile(libraryScript, "INPUT(\"" + memberObject.string() + "\")\n");

  std::vector<std::string> linkArgs = baseLinkArgs();
  linkArgs.insert(linkArgs.end(),
                  {mainObject.string(), "-L" + dir.string(), "-lscript",
                   "-lscript", "-o", (dir / "main").string()});
  CmdResult link = ncc(linkArgs);
  EXPECT_NE(link.exitCode, 0)
      << "two library-script occurrences must retain positional INPUT "
         "semantics";
  EXPECT_TRUE(link.stderrContains("duplicate symbol")) << link.err;
}

TEST_F(LinkerTest, ArchiveWarningsPreserveDuplicateLibraryOccurrences) {
  if (!isLinux())
    GTEST_SKIP() << "ELF archive diagnostics are an ELF linker behavior";

  const fs::path dir = tmpFile("archive_warning_occurrences");
  fs::create_directories(dir);
  const fs::path payload = dir / "payload.txt";
  const fs::path archive = dir / "libwarning.a";
  const fs::path mainSource = dir / "main.c";
  const fs::path mainObject = dir / "main.o";

  writeFile(payload, "not an ELF relocatable object\n");
  writeFile(mainSource, "int main(void) { return 0; }");
  CmdResult mainCompile = compileObject(mainSource, mainObject);
  ASSERT_EQ(mainCompile.exitCode, 0) << mainCompile.err;
  CmdResult archiveBuild =
      ncc({"--emit-static-lib", payload.string(), "-o", archive.string()});
  ASSERT_EQ(archiveBuild.exitCode, 0) << archiveBuild.err;

  std::vector<std::string> linkArgs = baseLinkArgs();
  linkArgs.insert(linkArgs.end(),
                  {mainObject.string(), "-L" + dir.string(), "-lwarning",
                   "-lwarning", "-o", (dir / "main").string()});
  CmdResult link = ncc(linkArgs);
  ASSERT_EQ(link.exitCode, 0) << link.err;

  const std::string diagnostics = link.out + link.err;
  constexpr llvm::StringLiteral warningText =
      "is neither ET_REL nor LLVM bitcode";
  size_t warningCount = 0;
  for (size_t offset = 0;
       (offset = diagnostics.find(warningText.str(), offset)) !=
       std::string::npos;
       offset += warningText.size())
    ++warningCount;
  EXPECT_EQ(warningCount, 2U)
      << "occurrence-oriented archive warnings must not be suppressed";
}

TEST_F(LinkerTest, SuppressWarningsOverridesFatalWarningsButNotErrors) {
  const fs::path source = tmpFile("warning_policy.c");
  writeFile(source, "int main(void) { return 0; }");

  auto linkWithPolicies = [&](const std::vector<std::string> &policies,
                              const fs::path &output) {
    std::vector<std::string> args = {"--target=x86_64-linux-gnu", "-nostdlib",
                                     "-fno-lto", "-Wl,-e,main",
                                     "-Wl,-z,neverc-test-unknown"};
    args.insert(args.end(), policies.begin(), policies.end());
    args.insert(args.end(), {source.string(), "-o", output.string()});
    return ncc(args);
  };

  const fs::path fatalOutput = tmpFile("warning_policy_fatal");
  const CmdResult fatal = linkWithPolicies({"-Werror"}, fatalOutput);
  EXPECT_NE(fatal.exitCode, 0) << fatal.err;
  EXPECT_TRUE(fatal.stderrContains("unknown -z value: neverc-test-unknown"))
      << fatal.err;
  EXPECT_FALSE(fs::exists(fatalOutput));

  for (const std::vector<std::string> &policies :
       {std::vector<std::string>{"-w", "-Werror"},
        std::vector<std::string>{"-Werror", "-w"}}) {
    const fs::path output =
        tmpFile(policies.front() == "-w" ? "warning_policy_suppress_first"
                                         : "warning_policy_suppress_last");
    const CmdResult suppressed = linkWithPolicies(policies, output);
    ASSERT_EQ(suppressed.exitCode, 0) << suppressed.err;
    EXPECT_FALSE(
        suppressed.stderrContains("unknown -z value: neverc-test-unknown"))
        << suppressed.err;
    EXPECT_TRUE(fs::exists(output));
  }

  const fs::path errorOutput = tmpFile("warning_policy_real_error");
  const CmdResult error =
      ncc({"--target=x86_64-linux-gnu", "-nostdlib", "-fno-lto", "-w",
           "-Wl,-e,main", source.string(), "-Wl,-lneverc_diagnostic_missing",
           "-o", errorOutput.string()});
  EXPECT_NE(error.exitCode, 0) << error.err;
  EXPECT_TRUE(error.stderrContains(
      "unable to find library -lneverc_diagnostic_missing"))
      << error.err;
  EXPECT_FALSE(fs::exists(errorOutput));
}

TEST_F(LinkerTest, WholeArchiveDuplicateLibraryIsNotCoalesced) {
  if (!isLinux())
    GTEST_SKIP() << "--whole-archive is an ELF linker behavior";

  const fs::path dir = tmpFile("whole_archive_duplicate");
  fs::create_directories(dir);
  const fs::path librarySource = dir / "repeat.c";
  const fs::path libraryObject = dir / "repeat.o";
  const fs::path archive = dir / "librepeat.a";
  const fs::path mainSource = dir / "main.c";
  const fs::path mainObject = dir / "main.o";

  writeFile(librarySource, "int whole_archive_value(void) { return 31; }");
  writeFile(mainSource,
            "int whole_archive_value(void); "
            "int main(void) { return whole_archive_value() == 31 ? 0 : 1; }");
  CmdResult libraryCompile = compileObject(librarySource, libraryObject);
  ASSERT_EQ(libraryCompile.exitCode, 0) << libraryCompile.err;
  CmdResult mainCompile = compileObject(mainSource, mainObject);
  ASSERT_EQ(mainCompile.exitCode, 0) << mainCompile.err;
  CmdResult archiveBuild = ncc(
      {"--emit-static-lib", libraryObject.string(), "-o", archive.string()});
  ASSERT_EQ(archiveBuild.exitCode, 0) << archiveBuild.err;

  std::vector<std::string> linkArgs = baseLinkArgs();
  linkArgs.insert(linkArgs.end(),
                  {mainObject.string(), "-L" + dir.string(),
                   "-Wl,--whole-archive", "-lrepeat", "-lrepeat",
                   "-Wl,--no-whole-archive", "-o", (dir / "repeat").string()});
  CmdResult link = ncc(linkArgs);
  EXPECT_NE(link.exitCode, 0)
      << "two --whole-archive occurrences must retain duplicate-definition "
         "semantics";
  EXPECT_TRUE(link.stderrContains("duplicate symbol")) << link.err;
}

TEST_F(LinkerTest, AsNeededStateChangeIsNotCoalesced) {
  if (!isLinux())
    GTEST_SKIP() << "--as-needed is an ELF linker behavior";

  const fs::path dir = tmpFile("as_needed_state_change");
  fs::create_directories(dir);
  const fs::path marker = dir / "loaded";
  const fs::path librarySource = dir / "needed.c";
  const fs::path library = dir / "libneeded.so";
  const fs::path mainSource = dir / "main.c";
  const fs::path mainObject = dir / "main.o";
  const fs::path executable = dir / "main";

  writeFile(librarySource,
            "#include <stdio.h>\n"
            "__attribute__((constructor)) static void mark_loaded(void) {\n"
            "  FILE *file = fopen(\"" +
                marker.string() +
                "\", \"wb\");\n"
                "  if (file) { fputc(1, file); fclose(file); }\n"
                "}\n");
  writeFile(mainSource, "int main(void) { return 0; }");

  std::vector<std::string> sharedLinkArgs = baseLinkArgs();
  sharedLinkArgs.insert(
      sharedLinkArgs.end(),
      {"-fPIC", "-shared", librarySource.string(), "-o", library.string()});
  CmdResult sharedLink = ncc(sharedLinkArgs);
  ASSERT_EQ(sharedLink.exitCode, 0) << sharedLink.err;
  CmdResult mainCompile = compileObject(mainSource, mainObject);
  ASSERT_EQ(mainCompile.exitCode, 0) << mainCompile.err;

  std::vector<std::string> linkArgs = baseLinkArgs();
  linkArgs.insert(linkArgs.end(),
                  {mainObject.string(), "-L" + dir.string(), "-Wl,--as-needed",
                   "-lneeded", "-Wl,--no-as-needed", "-lneeded", "-o",
                   executable.string()});
  CmdResult link = ncc(linkArgs);
  ASSERT_EQ(link.exitCode, 0) << link.err;

  CmdResult run = exec(
      "/usr/bin/env", {"LD_LIBRARY_PATH=" + dir.string(), executable.string()});
  ASSERT_EQ(run.exitCode, 0) << run.err;
  EXPECT_TRUE(fs::exists(marker))
      << "the later --no-as-needed occurrence must force a DT_NEEDED entry";
}

TEST_F(LinkerTest, WarnBackrefsLibrarySandwichIsNotCoalesced) {
  if (!isLinux())
    GTEST_SKIP() << "--warn-backrefs is an ELF linker behavior";

  const fs::path dir = tmpFile("warn_backrefs_sandwich");
  fs::create_directories(dir);
  const fs::path librarySource = dir / "definition.c";
  const fs::path libraryObject = dir / "definition.o";
  const fs::path archive = dir / "libdefinition.a";
  const fs::path referenceSource = dir / "reference.c";
  const fs::path referenceObject = dir / "reference.o";
  const fs::path mainSource = dir / "main.c";
  const fs::path mainObject = dir / "main.o";
  const fs::path executable = dir / "main";

  writeFile(librarySource, "int backref_value(void) { return 37; }");
  writeFile(referenceSource,
            "int backref_value(void); "
            "int reference_value(void) { return backref_value(); }");
  writeFile(mainSource,
            "int reference_value(void); "
            "int main(void) { return reference_value() == 37 ? 0 : 1; }");
  CmdResult libraryCompile = compileObject(librarySource, libraryObject);
  ASSERT_EQ(libraryCompile.exitCode, 0) << libraryCompile.err;
  CmdResult referenceCompile = compileObject(referenceSource, referenceObject);
  ASSERT_EQ(referenceCompile.exitCode, 0) << referenceCompile.err;
  CmdResult mainCompile = compileObject(mainSource, mainObject);
  ASSERT_EQ(mainCompile.exitCode, 0) << mainCompile.err;
  CmdResult archiveBuild = ncc(
      {"--emit-static-lib", libraryObject.string(), "-o", archive.string()});
  ASSERT_EQ(archiveBuild.exitCode, 0) << archiveBuild.err;

  std::vector<std::string> linkArgs = baseLinkArgs();
  linkArgs.insert(linkArgs.end(),
                  {"-Wl,--warn-backrefs", "-L" + dir.string(), "-ldefinition",
                   referenceObject.string(), "-ldefinition",
                   mainObject.string(), "-o", executable.string()});
  CmdResult link = ncc(linkArgs);
  ASSERT_EQ(link.exitCode, 0) << link.err;
  EXPECT_EQ((link.out + link.err).find("backward reference detected"),
            std::string::npos)
      << "a later lazy definition in a library sandwich must retain the "
         "existing --warn-backrefs behavior";
  EXPECT_EQ(exec(executable.string(), {}).exitCode, 0);
}

TEST_F(LinkerTest, BinaryFormatDuplicateLibraryIsNotCoalesced) {
  if (!isLinux())
    GTEST_SKIP() << "--format=binary is an ELF linker behavior";

  const fs::path dir = tmpFile("binary_format_duplicate");
  fs::create_directories(dir);
  const fs::path payload = dir / "libpayload.a";
  const fs::path mainSource = dir / "main.c";
  const fs::path mainObject = dir / "main.o";
  writeFile(payload, "opaque archive-shaped library payload");
  writeFile(mainSource, "int main(void) { return 0; }");
  CmdResult mainCompile = compileObject(mainSource, mainObject);
  ASSERT_EQ(mainCompile.exitCode, 0) << mainCompile.err;

  std::vector<std::string> linkArgs = baseLinkArgs();
  linkArgs.insert(linkArgs.end(),
                  {mainObject.string(), "-L" + dir.string(),
                   "-Wl,--format=binary", "-lpayload", "-lpayload",
                   "-Wl,--format=elf", "-o", (dir / "main").string()});
  CmdResult link = ncc(linkArgs);
  EXPECT_NE(link.exitCode, 0)
      << "binary-format occurrences define input-specific symbols and must "
         "not be coalesced";
  EXPECT_TRUE(link.stderrContains("duplicate symbol")) << link.err;
}

TEST_F(LinkerTest, ArchiveStatsPreserveDuplicateLibraryOccurrences) {
  if (!isLinux())
    GTEST_SKIP() << "--print-archive-stats is an ELF linker behavior";

  const fs::path dir = tmpFile("archive_stats_duplicate");
  fs::create_directories(dir);
  const fs::path librarySource = dir / "repeat.c";
  const fs::path libraryObject = dir / "repeat.o";
  const fs::path archive = dir / "librepeat.a";
  const fs::path mainSource = dir / "main.c";
  const fs::path mainObject = dir / "main.o";
  const fs::path stats = dir / "archive-stats.tsv";
  writeFile(librarySource, "int stats_value(void) { return 41; }");
  writeFile(mainSource,
            "int stats_value(void); "
            "int main(void) { return stats_value() == 41 ? 0 : 1; }");
  CmdResult libraryCompile = compileObject(librarySource, libraryObject);
  ASSERT_EQ(libraryCompile.exitCode, 0) << libraryCompile.err;
  CmdResult mainCompile = compileObject(mainSource, mainObject);
  ASSERT_EQ(mainCompile.exitCode, 0) << mainCompile.err;
  CmdResult archiveBuild = ncc(
      {"--emit-static-lib", libraryObject.string(), "-o", archive.string()});
  ASSERT_EQ(archiveBuild.exitCode, 0) << archiveBuild.err;

  std::vector<std::string> linkArgs = baseLinkArgs();
  linkArgs.insert(linkArgs.end(),
                  {"-Wl,--print-archive-stats=" + stats.string(),
                   mainObject.string(), "-L" + dir.string(), "-lrepeat",
                   "-lrepeat", "-o", (dir / "main").string()});
  CmdResult link = ncc(linkArgs);
  ASSERT_EQ(link.exitCode, 0) << link.err;
  ASSERT_TRUE(fs::exists(stats));

  const std::string contents = readFile(stats);
  size_t occurrences = 0;
  for (size_t offset = 0;
       (offset = contents.find(archive.string(), offset)) != std::string::npos;
       offset += archive.string().size())
    ++occurrences;
  EXPECT_EQ(occurrences, 2U)
      << "archive statistics are occurrence-oriented diagnostics";
}

TEST_F(LinkerTest, TraceSymbolSessionPreservesDuplicateLibraryLoads) {
  if (!isLinux())
    GTEST_SKIP() << "--trace-symbol is an ELF linker behavior";

  const fs::path dir = tmpFile("trace_symbol_duplicate");
  fs::create_directories(dir);
  const fs::path librarySource = dir / "repeat.c";
  const fs::path libraryObject = dir / "repeat.o";
  const fs::path archive = dir / "librepeat.a";
  const fs::path mainSource = dir / "main.c";
  const fs::path mainObject = dir / "main.o";
  const fs::path executable = dir / "main";
  writeFile(librarySource, "int traced_value(void) { return 43; }");
  writeFile(mainSource,
            "int traced_value(void); "
            "int main(void) { return traced_value() == 43 ? 0 : 1; }");
  CmdResult libraryCompile = compileObject(librarySource, libraryObject);
  ASSERT_EQ(libraryCompile.exitCode, 0) << libraryCompile.err;
  CmdResult mainCompile = compileObject(mainSource, mainObject);
  ASSERT_EQ(mainCompile.exitCode, 0) << mainCompile.err;
  CmdResult archiveBuild = ncc(
      {"--emit-static-lib", libraryObject.string(), "-o", archive.string()});
  ASSERT_EQ(archiveBuild.exitCode, 0) << archiveBuild.err;

  std::vector<std::string> linkArgs = baseLinkArgs();
  linkArgs.insert(linkArgs.end(),
                  {"-ftime-trace", "-ftime-trace-granularity=1",
                   "-Wl,--trace-symbol=traced_value", mainObject.string(),
                   "-L" + dir.string(), "-lrepeat", "-lrepeat", "-o",
                   executable.string()});
  CmdResult link = ncc(linkArgs);
  ASSERT_EQ(link.exitCode, 0) << link.err;
  EXPECT_NE((link.out + link.err).find("traced_value"), std::string::npos)
      << "the trace-symbol session was not active";

  const fs::path timeTrace(executable.string() + ".time-trace");
  ASSERT_TRUE(fs::exists(timeTrace));
  auto parsed = llvm::json::parse(readFile(timeTrace));
  ASSERT_TRUE(static_cast<bool>(parsed));
  const llvm::json::Object *root = parsed->getAsObject();
  ASSERT_NE(root, nullptr);
  const llvm::json::Array *events = root->getArray("traceEvents");
  ASSERT_NE(events, nullptr);
  size_t archiveLoads = 0;
  for (const llvm::json::Value &value : *events) {
    const llvm::json::Object *event = value.getAsObject();
    if (!event || event->getString("name") != "Load input files")
      continue;
    const llvm::json::Object *eventArgs = event->getObject("args");
    if (eventArgs && eventArgs->getString("detail") == archive.string())
      ++archiveLoads;
  }
  EXPECT_EQ(archiveLoads, 2U)
      << "trace-symbol diagnostics must observe the uncoalesced input stream";
}

TEST_F(LinkerTest, InputListingSessionsPreserveDuplicateLibraryLoads) {
  if (!isLinux())
    GTEST_SKIP() << "ELF input-listing diagnostics are Linux-only";

  const fs::path dir = tmpFile("input_listing_duplicate");
  fs::create_directories(dir);
  const fs::path librarySource = dir / "repeat.c";
  const fs::path libraryObject = dir / "repeat.o";
  const fs::path archive = dir / "librepeat.a";
  const fs::path mainSource = dir / "main.c";
  const fs::path mainObject = dir / "main.o";
  writeFile(librarySource, "int listed_value(void) { return 47; }");
  writeFile(mainSource,
            "int listed_value(void); "
            "int main(void) { return listed_value() == 47 ? 0 : 1; }");
  CmdResult libraryCompile = compileObject(librarySource, libraryObject);
  ASSERT_EQ(libraryCompile.exitCode, 0) << libraryCompile.err;
  CmdResult mainCompile = compileObject(mainSource, mainObject);
  ASSERT_EQ(mainCompile.exitCode, 0) << mainCompile.err;
  CmdResult archiveBuild = ncc(
      {"--emit-static-lib", libraryObject.string(), "-o", archive.string()});
  ASSERT_EQ(archiveBuild.exitCode, 0) << archiveBuild.err;

  for (const std::pair<std::string, std::string> &mode :
       {std::pair<std::string, std::string>{"verbose", "-v"},
        {"trace", "-t"}}) {
    SCOPED_TRACE(mode.first);
    const fs::path executable = dir / ("main-" + mode.first);
    std::vector<std::string> linkArgs = baseLinkArgs();
    linkArgs.insert(linkArgs.end(),
                    {"-ftime-trace", "-ftime-trace-granularity=1", mode.second,
                     mainObject.string(), "-L" + dir.string(), "-lrepeat",
                     "-lrepeat", "-o", executable.string()});
    CmdResult link = ncc(linkArgs);
    ASSERT_EQ(link.exitCode, 0) << link.err;

    const fs::path timeTrace(executable.string() + ".time-trace");
    ASSERT_TRUE(fs::exists(timeTrace));
    auto parsed = llvm::json::parse(readFile(timeTrace));
    ASSERT_TRUE(static_cast<bool>(parsed));
    const llvm::json::Object *root = parsed->getAsObject();
    ASSERT_NE(root, nullptr);
    const llvm::json::Array *events = root->getArray("traceEvents");
    ASSERT_NE(events, nullptr);
    size_t archiveLoads = 0;
    for (const llvm::json::Value &value : *events) {
      const llvm::json::Object *event = value.getAsObject();
      if (!event || event->getString("name") != "Load input files")
        continue;
      const llvm::json::Object *eventArgs = event->getObject("args");
      if (eventArgs && eventArgs->getString("detail") == archive.string())
        ++archiveLoads;
    }
    EXPECT_EQ(archiveLoads, 2U)
        << "input-listing diagnostics must observe both occurrences";
  }
}

TEST_F(LinkerTest, NoMmapOutputFilePreservesExecutableContents) {
  if (!isLinux())
    GTEST_SKIP() << "--no-mmap-output-file is an ELF linker option";

  auto src = tmpFile("no_mmap_output.c");
  auto obj = tmpFile("no_mmap_output.o");
  auto mappedExe = tmpFile("mapped_output");
  auto bufferedExe = tmpFile("buffered_output");
  writeFile(src, R"(
volatile unsigned char payload[2 * 1024 * 1024 + 257] = {1};
int main(void) {
  return payload[sizeof(payload) - 1] == 0 ? 23 : 1;
}
)");

  std::vector<std::string> compileArgs;
  for (const std::string &flag : sysrootFlags())
    compileArgs.push_back(flag);
  for (const std::string &flag : archFlags())
    compileArgs.push_back(flag);
  compileArgs.insert(compileArgs.end(),
                     {"-fno-lto", "-c", src.string(), "-o", obj.string()});
  CmdResult compile = ncc(compileArgs);
  ASSERT_EQ(compile.exitCode, 0) << compile.err;

  auto link = [&](const fs::path &output, bool mmapOutput) {
    std::vector<std::string> args;
    for (const std::string &flag : sysrootFlags())
      args.push_back(flag);
    for (const std::string &flag : archFlags())
      args.push_back(flag);
    for (const std::string &flag : linkFlags())
      args.push_back(flag);
    args.push_back("-fno-lto");
    args.push_back("-fbuild-id=fast");
    if (!mmapOutput)
      args.push_back("-Wl,--no-mmap-output-file");
    args.insert(args.end(), {obj.string(), "-o", output.string()});
    return ncc(args);
  };

  CmdResult mappedLink = link(mappedExe, true);
  ASSERT_EQ(mappedLink.exitCode, 0) << mappedLink.err;
  CmdResult bufferedLink = link(bufferedExe, false);
  ASSERT_EQ(bufferedLink.exitCode, 0) << bufferedLink.err;

  ASSERT_GT(fileSize(mappedExe), 2U * 1024U * 1024U);
  EXPECT_TRUE(readFile(bufferedExe) == readFile(mappedExe))
      << "buffered and mmap output bytes differ";
  EXPECT_EQ(exec(bufferedExe.string(), {}).exitCode, 23);
}

TEST_F(LinkerTest, BuildIdHashIsDeterministicAcrossResourceBudgets) {
  if (!isLinux())
    GTEST_SKIP() << "ELF build-id execution is Linux-only";

  const fs::path source = tmpFile("build_id_parallel.c");
  const fs::path object = tmpFile("build_id_parallel.o");
  const fs::path serialExe = tmpFile("build_id_serial");
  const fs::path parallelExe = tmpFile("build_id_parallel");
  writeFile(source, R"(
volatile unsigned char payload[4 * 1024 * 1024 + 257] = {1};
int main(void) {
  return payload[0] == 1 && payload[sizeof(payload) / 2] == 0 &&
                 payload[sizeof(payload) - 1] == 0
             ? 23
             : 1;
}
)");

  CmdResult compile = compileObject(source, object);
  ASSERT_EQ(compile.exitCode, 0) << compile.err;

  auto link = [&](const fs::path &output) {
    std::vector<std::string> args = baseLinkArgs();
    args.insert(args.end(),
                {"-fbuild-id=sha1", object.string(), "-o", output.string()});
    return ncc(args);
  };

  ScopedEnvironmentVariable budget("NEVERC_RESOURCE_BUDGET", "1");
  {
    ScopedEnvironmentVariable tokens("NEVERC_RESOURCE_CPU_TOKENS", "1");
    CmdResult serialLink = link(serialExe);
    ASSERT_EQ(serialLink.exitCode, 0) << serialLink.err;
  }
  {
    ScopedEnvironmentVariable tokens("NEVERC_RESOURCE_CPU_TOKENS", "4");
    CmdResult parallelLink = link(parallelExe);
    ASSERT_EQ(parallelLink.exitCode, 0) << parallelLink.err;
  }

  const std::string serialBytes = readFile(serialExe);
  const std::string parallelBytes = readFile(parallelExe);
  ASSERT_GT(serialBytes.size(), 4U * 1024U * 1024U);
  EXPECT_TRUE(serialBytes == parallelBytes)
      << "build-id hashing changed output bytes under a larger worker grant";

  llvm::Expected<std::string> serialBuildId = findELFBuildId(serialBytes);
  ASSERT_TRUE(static_cast<bool>(serialBuildId))
      << llvm::toString(serialBuildId.takeError()).str().str();
  llvm::Expected<std::string> parallelBuildId = findELFBuildId(parallelBytes);
  ASSERT_TRUE(static_cast<bool>(parallelBuildId))
      << llvm::toString(parallelBuildId.takeError()).str().str();
  EXPECT_EQ(serialBuildId->size(), 20U)
      << "sha1-style GNU build-id must retain its 20-byte descriptor";
  EXPECT_EQ(*serialBuildId, *parallelBuildId)
      << "build-id descriptor changed under a larger worker grant";

  EXPECT_EQ(exec(serialExe.string(), {}).exitCode, 23);
  EXPECT_EQ(exec(parallelExe.string(), {}).exitCode, 23);
}

TEST_F(LinkerTest, EmitStaticLib) {
  auto dir = tmpFile("eslib");
  fs::create_directories(dir);

  writeFile(dir / "add.c", "int eslib_add(int a, int b) { return a + b; }");
  writeFile(dir / "mul.c", "int eslib_mul(int a, int b) { return a * b; }");
  writeFile(dir / "neg.c", "int eslib_neg(int a) { return -a; }");
  writeFile(dir / "main.c", R"(
extern int eslib_add(int, int);
extern int eslib_mul(int, int);
extern int eslib_neg(int);
int main(void) {
    int r = 0;
    if (eslib_add(3, 4) != 7)  r = 1;
    if (eslib_mul(5, 6) != 30) r = 1;
    if (eslib_neg(9)    != -9) r = 1;
    if (eslib_add(eslib_neg(2), eslib_mul(3, 3)) != 7) r = 1;
    return r;
})");

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  // Compile all members
  for (auto *unit : {"add", "mul", "neg", "main"}) {
    auto c = base;
    c.insert(c.end(),
             {"-c", (dir / (std::string(unit) + ".c")).string(), "-o",
              (dir / (std::string(unit) + ".o")).string()});
    ASSERT_EQ(ncc(c).exitCode, 0) << "compile " << unit;
  }

  auto ar = dir / "ops.a";

  // -### must show in-process archive marker
  {
    auto dr = ncc({"--emit-static-lib", (dir / "add.o").string(),
                   (dir / "mul.o").string(), (dir / "neg.o").string(), "-o",
                   ar.string(), "-###"});
    auto all = dr.err + dr.out;
    EXPECT_TRUE(all.find("(in-process archive)") != std::string::npos)
        << "missing in-process archive marker";
  }

  // Build the archive
  ASSERT_EQ(ncc({"--emit-static-lib", (dir / "add.o").string(),
                 (dir / "mul.o").string(), (dir / "neg.o").string(), "-o",
                 ar.string()})
                .exitCode,
            0);

  EXPECT_GT(fileSize(ar), 0u);

  // Check magic header
  auto content = readFile(ar);
  EXPECT_TRUE(content.substr(0, 7) == "!<arch>") << "bad archive magic";

  // Link and run
  auto exe = dir / "main";
  std::vector<std::string> link;
  for (auto &f : sysrootFlags()) link.push_back(f);
  for (auto &f : archFlags()) link.push_back(f);
  link.insert(link.end(),
              {(dir / "main.o").string(), ar.string(), "-o", exe.string()});
  ASSERT_EQ(ncc(link).exitCode, 0);

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0);

  // Deterministic: build twice, compare
  auto ar1 = dir / "det1.a";
  auto ar2 = dir / "det2.a";
  ncc({"--emit-static-lib", (dir / "add.o").string(), (dir / "mul.o").string(),
       (dir / "neg.o").string(), "-o", ar1.string()});
  ncc({"--emit-static-lib", (dir / "add.o").string(), (dir / "mul.o").string(),
       (dir / "neg.o").string(), "-o", ar2.string()});
  EXPECT_EQ(readFile(ar1), readFile(ar2)) << "archive not deterministic";
}

TEST_F(LinkerTest, EmitStaticLibSingleFile) {
  auto src = (testDir() / "codegen/test_emit_static_lib.c").string();
  auto memberObj = tmpFile("eslib_sf_member.o");
  auto ar = tmpFile("eslib_sf.a");
  auto exe = tmpFile("eslib_sf");

  std::vector<std::string> base;
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  auto c = base;
  c.insert(c.end(), {"-DSTATIC_LIB_MEMBER", "-c", src, "-o",
                     memberObj.string()});
  ASSERT_EQ(ncc(c).exitCode, 0);

  ASSERT_EQ(
      ncc({"--emit-static-lib", memberObj.string(), "-o", ar.string()})
          .exitCode,
      0);

  auto l = base;
  l.insert(l.end(), {src, ar.string(), "-o", exe.string()});
  ASSERT_EQ(ncc(l).exitCode, 0);

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0);
}
