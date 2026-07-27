#ifndef NEVERC_PLUGIN_HOST_BUILTINOBJECTEXTENSION_H
#define NEVERC_PLUGIN_HOST_BUILTINOBJECTEXTENSION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <optional>

namespace neverc::plugin::builtinext {

// Native facts that the portable object graph has no field for -- an ELF
// sh_entsize, a COFF storage class, the relocation type number -- travel with
// each entity in an extension blob. The built-in reader writes them, and the
// built-in writer and the dyncode extractor read them back, so the layout is
// defined once here instead of being spelled out independently at each end.
//
// A blob is a four-byte tag, a four-byte little-endian version, then a run of
// little-endian u64 fields. Fields are fixed-width and never reordered, so a
// consumer that knows an older version still finds the fields it knows and
// stops where its knowledge ends.

inline constexpr char SectionTag[] = "NCSE";
inline constexpr char SymbolTag[] = "NCSY";
inline constexpr char RelocationTag[] = "NCRL";
// Reserved: COMDATs carry no native facts the graph cannot already hold, so the
// reader emits no blob for them and the writer only accepts an absent one.
inline constexpr char ComdatTag[] = "NCCO";

// Version 2 appends the ELF sh_entsize that a mergeable section needs to
// survive a read/rewrite/write round trip; version 1 still parses, it just
// carries no entry size.
inline constexpr uint32_t SectionVersion = 2;
inline constexpr uint32_t SymbolVersion = 1;
inline constexpr uint32_t RelocationVersion = 1;
inline constexpr uint32_t ComdatVersion = 1;

inline constexpr size_t TagSize = 4;
inline constexpr size_t HeaderSize = 8;

// Indices into the u64 run that follows the header.
enum SectionField : size_t {
  SectionIndex = 0,
  SectionAddress = 1,
  SectionType = 2,
  SectionFlags = 3,
  SectionOffset = 4,
  // Present from SectionVersion 2 on.
  SectionEntrySize = 5,
};

enum SymbolField : size_t {
  SymbolType = 0,
  SymbolBinding = 1,
  SymbolOther = 2,
  SymbolAuxiliary = 3,
};

enum RelocationField : size_t {
  RelocationNativeType = 0,
};

// The relocation blob ends with a counted name rather than a u64, because the
// name length is not known in advance.
inline constexpr size_t RelocationNameLengthOffset =
    HeaderSize + (RelocationNativeType + 1) * 8;

inline void appendU32(llvm::SmallVectorImpl<uint8_t> &Bytes, uint32_t Value) {
  for (unsigned I = 0; I != 4; ++I)
    Bytes.push_back(static_cast<uint8_t>(Value >> (I * 8)));
}

inline void appendU64(llvm::SmallVectorImpl<uint8_t> &Bytes, uint64_t Value) {
  for (unsigned I = 0; I != 8; ++I)
    Bytes.push_back(static_cast<uint8_t>(Value >> (I * 8)));
}

inline void appendHeader(llvm::SmallVectorImpl<uint8_t> &Bytes,
                         llvm::StringRef Tag, uint32_t Version) {
  Bytes.append(Tag.bytes_begin(), Tag.bytes_begin() + TagSize);
  appendU32(Bytes, Version);
}

inline void appendBytes(llvm::SmallVectorImpl<uint8_t> &Bytes,
                        llvm::StringRef Value) {
  Bytes.append(Value.bytes_begin(), Value.bytes_end());
}

inline bool hasTag(llvm::ArrayRef<uint8_t> Bytes, llvm::StringRef Tag) {
  return Bytes.size() >= HeaderSize &&
         llvm::StringRef(reinterpret_cast<const char *>(Bytes.data()),
                         TagSize) == Tag;
}

inline uint32_t version(llvm::ArrayRef<uint8_t> Bytes) {
  if (Bytes.size() < HeaderSize)
    return 0;
  uint32_t Value = 0;
  for (unsigned I = 0; I != 4; ++I)
    Value |= static_cast<uint32_t>(Bytes[TagSize + I]) << (I * 8);
  return Value;
}

// Reads the u64 field at \p Index, or nothing when the blob is too short to
// carry it -- which is how a version that predates the field reports itself.
inline std::optional<uint64_t> field(llvm::ArrayRef<uint8_t> Bytes,
                                     size_t Index) {
  const size_t Offset = HeaderSize + Index * 8;
  if (Bytes.size() < Offset + 8)
    return std::nullopt;
  uint64_t Value = 0;
  for (unsigned I = 0; I != 8; ++I)
    Value |= static_cast<uint64_t>(Bytes[Offset + I]) << (I * 8);
  return Value;
}

inline llvm::StringRef relocationName(llvm::ArrayRef<uint8_t> Bytes) {
  if (Bytes.size() < RelocationNameLengthOffset + 4)
    return llvm::StringRef();
  uint32_t Length = 0;
  for (unsigned I = 0; I != 4; ++I)
    Length |= static_cast<uint32_t>(Bytes[RelocationNameLengthOffset + I])
              << (I * 8);
  const size_t Offset = RelocationNameLengthOffset + 4;
  if (Length == 0 || Bytes.size() - Offset < Length)
    return llvm::StringRef();
  return llvm::StringRef(
      reinterpret_cast<const char *>(Bytes.data() + Offset), Length);
}

} // namespace neverc::plugin::builtinext

#endif
