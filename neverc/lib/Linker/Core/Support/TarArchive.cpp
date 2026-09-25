//===----------------------------------------------------------------------===//
//
//  TarArchive — POSIX ustar output with pax headers for long paths.
//
//===----------------------------------------------------------------------===//

#include "Linker/Core/Support/TarArchive.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include <cstring>

using namespace llvm;
using namespace linker;

namespace {
constexpr size_t BlockSize = 512;

// The ustar header; unused fields stay zero.
struct Header {
  char name[100];
  char mode[8];
  char uid[8];
  char gid[8];
  char size[12];
  char mtime[12];
  char checksum[8];
  char typeflag;
  char linkname[100];
  char magic[6];
  char version[2];
  char uname[32];
  char gname[32];
  char devmajor[8];
  char devminor[8];
  char prefix[155];
  char pad[12];
};
static_assert(sizeof(Header) == BlockSize, "ustar header is one block");

void writeOctal(char *field, size_t width, uint64_t value) {
  // width - 1 digits and a terminating NUL.
  snprintf(field, width, "%0*llo", int(width - 1), (unsigned long long)value);
}

void writeHeader(raw_fd_ostream &os, StringRef name, char type,
                 uint64_t size) {
  Header h;
  memset(&h, 0, sizeof h);
  memcpy(h.name, name.data(), std::min(name.size(), sizeof h.name));
  writeOctal(h.mode, sizeof h.mode, 0644);
  writeOctal(h.uid, sizeof h.uid, 0);
  writeOctal(h.gid, sizeof h.gid, 0);
  writeOctal(h.size, sizeof h.size, size);
  writeOctal(h.mtime, sizeof h.mtime, 0);
  h.typeflag = type;
  memcpy(h.magic, "ustar", 6);
  memcpy(h.version, "00", 2);
  // The checksum is computed with the field itself as spaces.
  memset(h.checksum, ' ', sizeof h.checksum);
  unsigned sum = 0;
  for (size_t i = 0; i < BlockSize; ++i)
    sum += reinterpret_cast<const unsigned char *>(&h)[i];
  snprintf(h.checksum, sizeof h.checksum, "%06o", sum);
  h.checksum[7] = ' ';
  os.write(reinterpret_cast<const char *>(&h), BlockSize);
}

void pad(raw_fd_ostream &os, uint64_t size) {
  static const char zeros[BlockSize] = {};
  if (size_t rest = size % BlockSize)
    os.write(zeros, BlockSize - rest);
}

// A pax "path" record: its length prefix counts itself.
std::string paxPathRecord(StringRef path) {
  const std::string body = " path=" + path.str() + "\n";
  size_t len = body.size() + 1;
  while (std::to_string(len).size() + body.size() != len)
    ++len;
  return std::to_string(len) + body;
}
} // namespace

Expected<std::unique_ptr<TarArchive>>
TarArchive::create(StringRef archivePath, StringRef baseDir) {
  std::error_code ec;
  auto os = std::make_unique<raw_fd_ostream>(archivePath, ec,
                                             sys::fs::OF_None);
  if (ec)
    return createStringError(ec, "cannot open " + archivePath + ": " +
                                     ec.message());
  return std::unique_ptr<TarArchive>(new TarArchive(std::move(os), baseDir));
}

void TarArchive::append(StringRef path, StringRef contents) {
  std::lock_guard<std::mutex> lock(mu);
  std::string name =
      baseDir + "/" + std::string(sys::path::convert_to_slash(path).str());
  if (!seen.insert(name).second)
    return;
  if (name.size() >= sizeof(Header::name)) {
    const std::string record = paxPathRecord(name);
    writeHeader(*os, "PaxHeader", 'x', record.size());
    *os << record;
    pad(*os, record.size());
  }
  writeHeader(*os, name, '0', contents.size());
  *os << contents;
  pad(*os, contents.size());
  // The two zero blocks that end an archive; each append rewrites them past
  // the previous end, so that the archive is complete at any time.
  static const char zeros[2 * BlockSize] = {};
  os->write(zeros, sizeof zeros);
  os->seek(os->tell() - sizeof zeros);
}

std::string linker::pathRelativeToRoot(StringRef path) {
  SmallString<128> abs(path);
  if (sys::fs::make_absolute(abs))
    return path.str();
  sys::path::remove_dots(abs, /*remove_dot_dot=*/true);
  // Drop the root name and directory, but keep a drive letter.
  std::string out;
  StringRef rootName = sys::path::root_name(abs);
  if (rootName.ends_with(":"))
    out = rootName.drop_back().str();
  if (!out.empty())
    out += "/";
  out += sys::path::relative_path(abs).str();
  return std::string(sys::path::convert_to_slash(out).str());
}
