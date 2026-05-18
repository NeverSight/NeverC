//===-- llvm/Support/TarWriter.h - Tar archive file creator -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TARWRITER_H
#define LLVM_SUPPORT_TARWRITER_H

#include "csupport/ltar_lwriter.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
class TarWriter {
public:
  static Expected<std::unique_ptr<TarWriter>> create(StringRef OutputPath,
                                                     StringRef BaseDir);

  void append(StringRef Path, StringRef Data);

private:
  TarWriter(int FD, StringRef BaseDir);
  raw_fd_ostream OS;
  SmallString<256> BaseDir;
  StringSet<> Files;
};
} // namespace llvm

// === Inline implementations (moved from cpp_bridge.cpp) ===

namespace llvm {

// Each file in an archive must be aligned to this block size.
inline static const int BlockSize = 512;

struct UstarHeader {
  char Name[100];
  char Mode[8];
  char Uid[8];
  char Gid[8];
  char Size[12];
  char Mtime[12];
  char Checksum[8];
  char TypeFlag;
  char Linkname[100];
  char Magic[6];
  char Version[2];
  char Uname[32];
  char Gname[32];
  char DevMajor[8];
  char DevMinor[8];
  char Prefix[155];
  char Pad[12];
};
static_assert(sizeof(UstarHeader) == BlockSize, "invalid Ustar header");

inline static UstarHeader makeUstarHeader() {
  UstarHeader Hdr = {};
  memcpy(Hdr.Magic, "ustar", 5); // Ustar magic
  memcpy(Hdr.Version, "00", 2);  // Ustar version
  return Hdr;
}

// A PAX attribute is in the form of "<length> <key>=<value>\n"
// where <length> is the length of the entire string including
// Headers in tar files must be aligned to 512 byte boundaries.
// This function forwards the current file position to the next boundary.
inline static void pad(raw_fd_ostream &OS) {
  uint64_t Pos = OS.tell();
  OS.seek(alignTo(Pos, BlockSize));
}

// Computes a checksum for a tar header.
inline static void computeChecksum(UstarHeader &Hdr) {
  csupport_tar_compute_checksum_buf((char *)(&Hdr), sizeof(Hdr));
}

// Create a tar header and write it to a given output stream.
inline static void writePaxHeader(raw_fd_ostream &OS, StringRef Path) {
  // A PAX header consists of a 512-byte header followed
  // by key-value strings. First, create key-value strings.
  char pax_buf[1024];
  size_t pax_len = csupport_tar_format_pax(pax_buf, sizeof(pax_buf), "path", 4,
                                           Path.data(), Path.size());
  StringRef PaxAttr(pax_buf, pax_len);

  // Create a 512-byte header.
  UstarHeader Hdr = makeUstarHeader();
  snprintf(Hdr.Size, sizeof(Hdr.Size), "%011zo", pax_len);
  Hdr.TypeFlag = 'x'; // PAX magic
  computeChecksum(Hdr);

  // Write them down.
  OS << StringRef((char *)(&Hdr), sizeof(Hdr));
  OS << PaxAttr;
  pad(OS);
}

// Path fits in a Ustar header if
//
// - Path is less than 100 characters long, or
// - Path is in the form of "<prefix>/<name>" where <prefix> is less
//   than or equal to 155 characters long and <name> is less than 100
//   characters long. Both <prefix> and <name> can contain extra '/'.
//
// If Path fits in a Ustar header, updates Prefix and Name and returns true.
// Otherwise, returns false.
inline static bool splitUstar(StringRef Path, StringRef &Prefix,
                              StringRef &Name) {
  size_t prefix_len, name_start;
  if (!csupport_tar_split_path(Path.data(), Path.size(), &prefix_len,
                               &name_start))
    return false;
  Prefix = Path.substr(0, prefix_len);
  Name = Path.substr(name_start);
  return true;
}

// The PAX header is an extended format, so a PAX header needs
// to be followed by a "real" header.
inline static void writeUstarHeader(raw_fd_ostream &OS, StringRef Prefix,
                                    StringRef Name, size_t Size) {
  UstarHeader Hdr = makeUstarHeader();
  memcpy(Hdr.Name, Name.data(), Name.size());
  memcpy(Hdr.Mode, "0000664", 8);
  snprintf(Hdr.Size, sizeof(Hdr.Size), "%011zo", Size);
  memcpy(Hdr.Prefix, Prefix.data(), Prefix.size());
  computeChecksum(Hdr);
  OS << StringRef((char *)(&Hdr), sizeof(Hdr));
}

// Creates a TarWriter instance and returns it.
inline Expected<uptr_t<TarWriter>> TarWriter::create(StringRef OutputPath,
                                                     StringRef BaseDir) {
  using namespace sys::fs;
  int FD;
  if (errc_t EC = openFileForWrite(OutputPath, FD, CD_CreateAlways, OF_None))
    return make_error<StringError>("cannot open " + OutputPath, EC);
  return uptr_t<TarWriter>(new TarWriter(FD, BaseDir));
}

inline TarWriter::TarWriter(int FD, StringRef BaseDir)
    : OS(FD, /*shouldClose=*/true, /*unbuffered=*/false), BaseDir(BaseDir) {}

// Append a given file to an archive.
inline void TarWriter::append(StringRef Path, StringRef Data) {
  // Write Path and Data.
  SmallString<512> FullpathStorage;
  FullpathStorage += BaseDir;
  FullpathStorage += "/";
  FullpathStorage += sys::path::convert_to_slash(Path);
  SmallString<512> Fullpath(FullpathStorage);

  // We do not want to include the same file more than once.
  if (!Files.insert(Fullpath).second)
    return;

  StringRef Prefix;
  StringRef Name;
  if (splitUstar(Fullpath, Prefix, Name)) {
    writeUstarHeader(OS, Prefix, Name, Data.size());
  } else {
    writePaxHeader(OS, Fullpath);
    writeUstarHeader(OS, "", "", Data.size());
  }

  OS << Data;
  pad(OS);

  // POSIX requires tar archives end with two null blocks.
  // Here, we write the terminator and then seek back, so that
  // the file being output is terminated correctly at any moment.
  uint64_t Pos = OS.tell();
  {
    char zeros[BlockSize * 2] = {};
    OS.write(zeros, sizeof(zeros));
  }
  OS.seek(Pos);
  OS.flush();
}

} // namespace llvm

#endif
