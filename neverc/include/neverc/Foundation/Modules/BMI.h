//===--- BMI.h - NeverC C++20 Binary Module Interface scaffold ---*- C++ -*-===//
#ifndef NEVERC_FOUNDATION_MODULES_BMI_H
#define NEVERC_FOUNDATION_MODULES_BMI_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace neverc {

/// NeverC-only BMI v0 scaffold. Not compatible with Clang PCM/BMI.
/// On-disk layout (little-endian host write, portable field widths):
///   [BMIHeader]
///   [u32 nameLen][name bytes]
///   [u32 exportCount]
///   repeated: [u32 len][bytes]
struct BMIHeader {
  static constexpr uint32_t Magic = 0x4E43424D; // 'NCBM'
  static constexpr uint16_t Version = 1;
  uint32_t MagicValue = Magic;
  uint16_t VersionValue = Version;
  uint16_t Flags = 0;
};

class BMIWriter {
public:
  void setModuleName(llvm::StringRef Name) { ModuleName = Name.str(); }
  void addExport(llvm::StringRef Sym) { Exports.emplace_back(Sym.str()); }
  const std::string &getModuleName() const { return ModuleName; }
  const std::vector<std::string> &getExports() const { return Exports; }

  /// Materialize a BMI blob into \p Out. Returns false on empty module name.
  bool writeTo(std::vector<uint8_t> &Out) const {
    if (ModuleName.empty())
      return false;
    Out.clear();
    BMIHeader H;
    appendBytes(Out, &H, sizeof(H));
    appendU32(Out, static_cast<uint32_t>(ModuleName.size()));
    appendBytes(Out, ModuleName.data(), ModuleName.size());
    appendU32(Out, static_cast<uint32_t>(Exports.size()));
    for (const std::string &E : Exports) {
      appendU32(Out, static_cast<uint32_t>(E.size()));
      appendBytes(Out, E.data(), E.size());
    }
    return true;
  }

  void writeTo(llvm::raw_ostream &OS) const {
    std::vector<uint8_t> Buf;
    if (!writeTo(Buf))
      return;
    OS.write(reinterpret_cast<const char *>(Buf.data()), Buf.size());
  }

private:
  static void appendU32(std::vector<uint8_t> &Out, uint32_t V) {
    for (int I = 0; I < 4; ++I)
      Out.push_back(static_cast<uint8_t>((V >> (8 * I)) & 0xFF));
  }
  static void appendBytes(std::vector<uint8_t> &Out, const void *P, size_t N) {
    const auto *B = static_cast<const uint8_t *>(P);
    Out.insert(Out.end(), B, B + N);
  }

  std::string ModuleName;
  std::vector<std::string> Exports;
};

class BMIReader {
public:
  bool empty() const { return ModuleName.empty(); }
  void setModuleName(llvm::StringRef Name) { ModuleName = Name.str(); }
  const std::string &getModuleName() const { return ModuleName; }
  const std::vector<std::string> &getExports() const { return Exports; }

  /// Parse a BMI blob produced by BMIWriter. Returns false on bad magic/trunc.
  bool readFrom(llvm::ArrayRef<uint8_t> Data) {
    ModuleName.clear();
    Exports.clear();
    size_t Off = 0;
    if (Data.size() < sizeof(BMIHeader))
      return false;
    BMIHeader H;
    std::memcpy(&H, Data.data(), sizeof(H));
    Off += sizeof(H);
    if (H.MagicValue != BMIHeader::Magic || H.VersionValue == 0)
      return false;
    uint32_t NameLen = 0;
    if (!readU32(Data, Off, NameLen))
      return false;
    if (Off + NameLen > Data.size())
      return false;
    ModuleName.assign(reinterpret_cast<const char *>(Data.data() + Off), NameLen);
    Off += NameLen;
    uint32_t Count = 0;
    if (!readU32(Data, Off, Count))
      return false;
    Exports.reserve(Count);
    for (uint32_t I = 0; I < Count; ++I) {
      uint32_t Len = 0;
      if (!readU32(Data, Off, Len))
        return false;
      if (Off + Len > Data.size())
        return false;
      Exports.emplace_back(reinterpret_cast<const char *>(Data.data() + Off), Len);
      Off += Len;
    }
    return true;
  }

private:
  static bool readU32(llvm::ArrayRef<uint8_t> Data, size_t &Off, uint32_t &V) {
    if (Off + 4 > Data.size())
      return false;
    V = 0;
    for (int I = 0; I < 4; ++I)
      V |= static_cast<uint32_t>(Data[Off + I]) << (8 * I);
    Off += 4;
    return true;
  }

  std::string ModuleName;
  std::vector<std::string> Exports;
};

} // namespace neverc

#endif
