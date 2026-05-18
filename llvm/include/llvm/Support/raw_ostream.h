//===--- raw_ostream.h - Raw output stream ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the raw_ostream class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RAW_OSTREAM_H
#define LLVM_SUPPORT_RAW_OSTREAM_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DataTypes.h"
#include <assert.h>
#include <optional>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "llvm/ADT/Twine.h"

namespace llvm {

class Duration;
class formatv_object_base;
class format_object_base;
class FormattedString;
class FormattedNumber;
class FormattedBytes;
template <class T> class [[nodiscard]] Expected;

namespace sys {
namespace fs {
enum FileAccess : unsigned;
enum OpenFlags : unsigned;
enum CreationDisposition : unsigned;
class FileLocker;
} // end namespace fs
} // end namespace sys

/// This class implements an extremely fast bulk output stream that can *only*
/// output to a stream.  It does not support seeking, reopening, rewinding, line
/// buffered disciplines etc. It is a simple buffer that outputs
/// a chunk at a time.
class raw_ostream {
public:
  // Class kinds to support LLVM-style RTTI.
  enum class OStreamKind {
    OK_OStream,
    OK_FDStream,
  };

public:
  OStreamKind Kind;

  /// The buffer is handled in such a way that the buffer is
  /// uninitialized, unbuffered, or out of space when OutBufCur >=
  /// OutBufEnd. Thus a single comparison suffices to determine if we
  /// need to take the slow path to write a single character.
  ///
  /// The buffer is in one of three states:
  ///  1. Unbuffered (BufferMode == Unbuffered)
  ///  1. Uninitialized (BufferMode != Unbuffered && OutBufStart == 0).
  ///  2. Buffered (BufferMode != Unbuffered && OutBufStart != 0 &&
  ///               OutBufEnd - OutBufStart >= 1).
  ///
  /// If buffered, then the raw_ostream owns the buffer if (BufferMode ==
  /// InternalBuffer); otherwise the buffer has been set via SetBuffer and is
  /// managed by the subclass.
  ///
  /// If a subclass installs an external buffer using SetBuffer then it can wait
  /// for a \see write_impl() call to handle the data which has been put into
  /// this buffer.
  char *OutBufStart, *OutBufEnd, *OutBufCur;
  bool ColorEnabled = false;

  /// Optional stream this stream is tied to. If this stream is written to, the
  /// tied-to stream will be flushed first.
  raw_ostream *TiedStream = nullptr;

  enum class BufferKind {
    Unbuffered = 0,
    InternalBuffer,
    ExternalBuffer
  } BufferMode;

public:
  // color order matches ANSI escape sequence, don't change
  enum class Colors {
    BLACK = 0,
    RED,
    GREEN,
    YELLOW,
    BLUE,
    MAGENTA,
    CYAN,
    WHITE,
    SAVEDCOLOR,
    RESET,
  };

  static constexpr Colors BLACK = Colors::BLACK;
  static constexpr Colors RED = Colors::RED;
  static constexpr Colors GREEN = Colors::GREEN;
  static constexpr Colors YELLOW = Colors::YELLOW;
  static constexpr Colors BLUE = Colors::BLUE;
  static constexpr Colors MAGENTA = Colors::MAGENTA;
  static constexpr Colors CYAN = Colors::CYAN;
  static constexpr Colors WHITE = Colors::WHITE;
  static constexpr Colors SAVEDCOLOR = Colors::SAVEDCOLOR;
  static constexpr Colors RESET = Colors::RESET;

  explicit raw_ostream(bool unbuffered = false,
                       OStreamKind K = OStreamKind::OK_OStream)
      : Kind(K), BufferMode(unbuffered ? BufferKind::Unbuffered
                                       : BufferKind::InternalBuffer) {
    // Start out ready to flush.
    OutBufStart = OutBufEnd = OutBufCur = nullptr;
  }

  raw_ostream(const raw_ostream &) = delete;
  void operator=(const raw_ostream &) = delete;

  virtual ~raw_ostream();

  /// tell - Return the current offset with the file.
  uint64_t tell() const { return current_pos() + GetNumBytesInBuffer(); }

  OStreamKind get_kind() const { return Kind; }

  std::string get_str() {
    return StringRef(OutBufStart, OutBufEnd - OutBufStart).str();
  }

  //===--------------------------------------------------------------------===//
  // Configuration Interface
  //===--------------------------------------------------------------------===//

  /// If possible, pre-allocate \p ExtraSize bytes for stream data.
  /// i.e. it extends internal buffers to keep additional ExtraSize bytes.
  /// So that the stream could keep at least tell() + ExtraSize bytes
  /// without re-allocations. reserveExtraSpace() does not change
  /// the size/data of the stream.
  virtual void reserveExtraSpace(uint64_t ExtraSize) {}

  /// Set the stream to be buffered, with an automatically determined buffer
  /// size.
  void SetBuffered();

  /// Set the stream to be buffered, using the specified buffer size.
  void SetBufferSize(size_t Size) {
    flush();
    SetBufferAndMode(new char[Size], Size, BufferKind::InternalBuffer);
  }

  size_t GetBufferSize() const {
    // If we're supposed to be buffered but haven't actually gotten around
    // to allocating the buffer yet, return the value that would be used.
    if (BufferMode != BufferKind::Unbuffered && OutBufStart == nullptr)
      return preferred_buffer_size();

    // Otherwise just return the size of the allocated buffer.
    return OutBufEnd - OutBufStart;
  }

  /// Set the stream to be unbuffered. When unbuffered, the stream will flush
  /// after every write. This routine will also flush the buffer immediately
  /// when the stream is being set to unbuffered.
  void SetUnbuffered() {
    flush();
    SetBufferAndMode(nullptr, 0, BufferKind::Unbuffered);
  }

  size_t GetNumBytesInBuffer() const { return OutBufCur - OutBufStart; }

  //===--------------------------------------------------------------------===//
  // Data Output Interface
  //===--------------------------------------------------------------------===//

  void flush() {
    if (OutBufCur != OutBufStart)
      flush_nonempty();
  }

  raw_ostream &operator<<(char C) {
    if (OutBufCur >= OutBufEnd)
      return write(C);
    *OutBufCur++ = C;
    return *this;
  }

  raw_ostream &operator<<(unsigned char C) {
    if (OutBufCur >= OutBufEnd)
      return write(C);
    *OutBufCur++ = C;
    return *this;
  }

  raw_ostream &operator<<(signed char C) {
    if (OutBufCur >= OutBufEnd)
      return write(C);
    *OutBufCur++ = C;
    return *this;
  }

  raw_ostream &operator<<(StringRef Str) {
    // Inline fast path, particularly for strings with a known length.
    size_t Size = Str.size();

    // Make sure we can use the fast path.
    if (Size > (size_t)(OutBufEnd - OutBufCur))
      return write(Str.data(), Size);

    if (Size) {
      memcpy(OutBufCur, Str.data(), Size);
      OutBufCur += Size;
    }
    return *this;
  }

#if defined(__cpp_char8_t)
  // When using `char8_t *` integers or pointers are written to the ostream
  // instead of UTF-8 code as one might expect. This might lead to unexpected
  // behavior, especially as `u8""` literals are of type `char8_t*` instead of
  // type `char_t*` from C++20 onwards. Thus we disallow using them with
  // raw_ostreams.
  // If you have u8"" literals to stream, you can rewrite them as ordinary
  // literals with escape sequences
  // e.g.  replace `u8"\u00a0"` by `"\xc2\xa0"`
  // or use `reinterpret_cast`:
  // e.g. replace `u8"\u00a0"` by `reinterpret_cast<const char *>(u8"\u00a0")`
  raw_ostream &operator<<(const char8_t *Str) = delete;
#endif

  raw_ostream &operator<<(const char *Str) {
    // Inline fast path, particularly for constant strings where a sufficiently
    // smart compiler will simplify strlen.

    return this->operator<<(StringRef(Str));
  }

  raw_ostream &operator<<(const std::string &Str) {
    // Avoid the fast path, it would only increase code size for a marginal win.
    return write(Str.data(), Str.length());
  }

  raw_ostream &operator<<(const std::string_view &Str) {
    return write(Str.data(), Str.length());
  }

  raw_ostream &operator<<(const SmallVectorImpl<char> &Str) {
    return write(Str.data(), Str.size());
  }

  raw_ostream &operator<<(unsigned long N);
  raw_ostream &operator<<(long N);
  raw_ostream &operator<<(unsigned long long N);
  raw_ostream &operator<<(long long N);
  raw_ostream &operator<<(const void *P);

  raw_ostream &operator<<(unsigned int N) {
    return this->operator<<(static_cast<unsigned long>(N));
  }

  raw_ostream &operator<<(int N) {
    return this->operator<<(static_cast<long>(N));
  }

  raw_ostream &operator<<(double N);

  /// Output \p N in hexadecimal, without any prefix or padding.
  raw_ostream &write_hex(unsigned long long N);

  // Change the foreground color of text.
  raw_ostream &operator<<(Colors C);

  /// Output a formatted UUID with dash separators.
  using uuid_t = uint8_t[16];
  raw_ostream &write_uuid(const uuid_t UUID);

  /// Output \p Str, turning '\\', '\t', '\n', '"', and anything that doesn't
  /// satisfy llvm::isPrint into an escape sequence.
  raw_ostream &write_escaped(StringRef Str, bool UseHexEscapes = false);

  raw_ostream &write(unsigned char C);
  raw_ostream &write(const char *Ptr, size_t Size);

  // Formatted output, see the format() function in Support/Format.h.
  raw_ostream &operator<<(const format_object_base &Fmt);

  // Formatted output, see the leftJustify() function in Support/Format.h.
  raw_ostream &operator<<(const FormattedString &);

  // Formatted output, see the formatHex() function in Support/Format.h.
  raw_ostream &operator<<(const FormattedNumber &);

  // Formatted output, see the formatv() function in Support/FormatVariadic.h.
  raw_ostream &operator<<(const formatv_object_base &);

  // Formatted output, see the format_bytes() function in Support/Format.h.
  raw_ostream &operator<<(const FormattedBytes &);

  /// indent - Insert 'NumSpaces' spaces.
  raw_ostream &indent(unsigned NumSpaces);

  /// write_zeros - Insert 'NumZeros' nulls.
  raw_ostream &write_zeros(unsigned NumZeros);

  /// Changes the foreground color of text that will be output from this point
  /// forward.
  /// @param Color ANSI color to use, the special SAVEDCOLOR can be used to
  /// change only the bold attribute, and keep colors untouched
  /// @param Bold bold/brighter text, default false
  /// @param BG if true change the background, default: change foreground
  /// @returns itself so it can be used within << invocations
  virtual raw_ostream &changeColor(enum Colors Color, bool Bold = false,
                                   bool BG = false);

  /// Resets the colors to terminal defaults. Call this when you are done
  /// outputting colored text, or before program exit.
  virtual raw_ostream &resetColor();

  /// Reverses the foreground and background colors.
  virtual raw_ostream &reverseColor();

  /// This function determines if this stream is connected to a "tty" or
  /// "console" window. That is, the output would be displayed to the user
  /// rather than being put on a pipe or stored in a file.
  virtual bool is_displayed() const { return false; }

  /// This function determines if this stream is displayed and supports colors.
  /// The result is unaffected by calls to enable_color().
  virtual bool has_colors() const { return is_displayed(); }

  // Enable or disable colors. Once enable_colors(false) is called,
  // changeColor() has no effect until enable_colors(true) is called.
  virtual void enable_colors(bool enable) { ColorEnabled = enable; }

  bool colors_enabled() const { return ColorEnabled; }

  /// Tie this stream to the specified stream. Replaces any existing tied-to
  /// stream. Specifying a nullptr unties the stream.
  void tie(raw_ostream *TieTo) { TiedStream = TieTo; }

  //===--------------------------------------------------------------------===//
  // Subclass Interface
  //===--------------------------------------------------------------------===//

private:
  /// The is the piece of the class that is implemented by subclasses.  This
  /// writes the \p Size bytes starting at
  /// \p Ptr to the underlying stream.
  ///
  /// This function is guaranteed to only be called at a point at which it is
  /// safe for the subclass to install a new buffer via SetBuffer.
  ///
  /// \param Ptr The start of the data to be written. For buffered streams this
  /// is guaranteed to be the start of the buffer.
  ///
  /// \param Size The number of bytes to be written.
  ///
  /// \invariant { Size > 0 }
  virtual void write_impl(const char *Ptr, size_t Size) = 0;

  /// Return the current position within the stream, not counting the bytes
  /// currently in the buffer.
  virtual uint64_t current_pos() const = 0;

protected:
  /// Use the provided buffer as the raw_ostream buffer. This is intended for
  /// use only by subclasses which can arrange for the output to go directly
  /// into the desired output buffer, instead of being copied on each flush.
  void SetBuffer(char *BufferStart, size_t Size) {
    SetBufferAndMode(BufferStart, Size, BufferKind::ExternalBuffer);
  }

  /// Return an efficient buffer size for the underlying output mechanism.
  virtual size_t preferred_buffer_size() const;

  /// Return the beginning of the current stream buffer, or 0 if the stream is
  /// unbuffered.
  const char *getBufferStart() const { return OutBufStart; }

  //===--------------------------------------------------------------------===//
  // Private Interface
  //===--------------------------------------------------------------------===//
private:
  /// Install the given buffer and mode.
  void SetBufferAndMode(char *BufferStart, size_t Size, BufferKind Mode);

  /// Flush the current buffer, which is known to be non-empty. This outputs the
  /// currently buffered data and resets the buffer to empty.
  void flush_nonempty();

  /// Copy data into the buffer. Size must not be greater than the number of
  /// unused bytes in the buffer.
  void copy_to_buffer(const char *Ptr, size_t Size);

  /// Compute whether colors should be used and do the necessary work such as
  /// flushing. The result is affected by calls to enable_color().
  bool prepare_colors();

  /// Flush the tied-to stream (if present) and then write the required data.
  void flush_tied_then_write(const char *Ptr, size_t Size);

  virtual void anchor();
};

/// Call the appropriate insertion operator, given an rvalue reference to a
/// raw_ostream object and return a stream of the same type as the argument.
template <typename OStream, typename T>
std::enable_if_t<!std::is_reference_v<OStream> &&
                     std::is_base_of_v<raw_ostream, OStream>,
                 OStream &&>
operator<<(OStream &&OS, const T &Value) {
  OS << Value;
  return std::move(OS);
}

/// An abstract base class for streams implementations that also support a
/// pwrite operation. This is useful for code that can mostly stream out data,
/// but needs to patch in a header that needs to know the output size.
class raw_pwrite_stream : public raw_ostream {
  virtual void pwrite_impl(const char *Ptr, size_t Size, uint64_t Offset) = 0;
  void anchor() override;

public:
  explicit raw_pwrite_stream(bool Unbuffered = false,
                             OStreamKind K = OStreamKind::OK_OStream)
      : raw_ostream(Unbuffered, K) {}
  void pwrite(const char *Ptr, size_t Size, uint64_t Offset) {
#ifndef NDEBUG
    uint64_t Pos = tell();
    // /dev/null always reports a pos of 0, so we cannot perform this check
    // in that case.
    if (Pos)
      assert(Size + Offset <= Pos && "We don't support extending the stream");
#endif
    pwrite_impl(Ptr, Size, Offset);
  }
};

//===----------------------------------------------------------------------===//
// File Output Streams
//===----------------------------------------------------------------------===//

/// A raw_ostream that writes to a file descriptor.
///
class raw_fd_ostream : public raw_pwrite_stream {
  int FD;
  bool ShouldClose;
  bool SupportsSeeking = false;
  bool IsRegularFile = false;
  mutable int HasColors = -1;

#ifdef _WIN32
  /// True if this fd refers to a Windows console device. Mintty and other
  /// terminal emulators are TTYs, but they are not consoles.
  bool IsWindowsConsole = false;
#endif

  std::error_code EC;

  uint64_t pos = 0;

  /// See raw_ostream::write_impl.
  void write_impl(const char *Ptr, size_t Size) override;

  void pwrite_impl(const char *Ptr, size_t Size, uint64_t Offset) override;

  /// Return the current position within the stream, not counting the bytes
  /// currently in the buffer.
  uint64_t current_pos() const override { return pos; }

  /// Determine an efficient buffer size.
  size_t preferred_buffer_size() const override;

  void anchor() override;

protected:
  /// Set the flag indicating that an output error has been encountered.
  void error_detected(std::error_code EC) { this->EC = EC; }

  /// Return the file descriptor.
  int get_fd() const { return FD; }

  // Update the file position by increasing \p Delta.
  void inc_pos(uint64_t Delta) { pos += Delta; }

public:
  /// Open the specified file for writing. If an error occurs, information
  /// about the error is put into EC, and the stream should be immediately
  /// destroyed;
  /// \p Flags allows optional flags to control how the file will be opened.
  ///
  /// As a special case, if Filename is "-", then the stream will use
  /// STDOUT_FILENO instead of opening a file. This will not close the stdout
  /// descriptor.
  raw_fd_ostream(StringRef Filename, std::error_code &EC);
  raw_fd_ostream(StringRef Filename, std::error_code &EC,
                 sys::fs::CreationDisposition Disp);
  raw_fd_ostream(StringRef Filename, std::error_code &EC,
                 sys::fs::FileAccess Access);
  raw_fd_ostream(StringRef Filename, std::error_code &EC,
                 sys::fs::OpenFlags Flags);
  raw_fd_ostream(StringRef Filename, std::error_code &EC,
                 sys::fs::CreationDisposition Disp, sys::fs::FileAccess Access,
                 sys::fs::OpenFlags Flags);

  /// FD is the file descriptor that this writes to.  If ShouldClose is true,
  /// this closes the file when the stream is destroyed. If FD is for stdout or
  /// stderr, it will not be closed.
  raw_fd_ostream(int fd, bool shouldClose, bool unbuffered = false,
                 OStreamKind K = OStreamKind::OK_OStream);

  ~raw_fd_ostream() override;

  /// Manually flush the stream and close the file. Note that this does not call
  /// fsync.
  void close();

  bool supportsSeeking() const { return SupportsSeeking; }

  bool isRegularFile() const { return IsRegularFile; }

  /// Flushes the stream and repositions the underlying file descriptor position
  /// to the offset specified from the beginning of the file.
  uint64_t seek(uint64_t off);

  bool is_displayed() const override;

  bool has_colors() const override;

  std::error_code error() const { return EC; }

  /// Return the value of the flag in this raw_fd_ostream indicating whether an
  /// output error has been encountered.
  /// This doesn't implicitly flush any pending output.  Also, it doesn't
  /// guarantee to detect all errors unless the stream has been closed.
  bool has_error() const { return bool(EC); }

  /// Set the flag read by has_error() to false. If the error flag is set at the
  /// time when this raw_ostream's destructor is called, report_fatal_error is
  /// called to report the error. Use clear_error() after handling the error to
  /// avoid this behavior.
  ///
  ///   "Errors should never pass silently.
  ///    Unless explicitly silenced."
  ///      - from The Zen of Python, by Tim Peters
  ///
  void clear_error() { EC = std::error_code(); }

  /// Locks the underlying file.
  ///
  /// @returns RAII object that releases the lock upon leaving the scope, if the
  ///          locking was successful. Otherwise returns corresponding
  ///          error code.
  ///
  /// The function blocks the current thread until the lock become available or
  /// error occurs.
  ///
  /// Possible use of this function may be as follows:
  ///
  ///   @code{.cpp}
  ///   if (auto L = stream.lock()) {
  ///     // ... do action that require file to be locked.
  ///   } else {
  ///     handleAllErrors(std::move(L.takeError()), [&](ErrorInfoBase &EIB) {
  ///       // ... handle lock error.
  ///     });
  ///   }
  ///   @endcode
  [[nodiscard]] Expected<sys::fs::FileLocker> lock();

  /// Tries to lock the underlying file within the specified period.
  ///
  /// @returns RAII object that releases the lock upon leaving the scope, if the
  ///          locking was successful. Otherwise returns corresponding
  ///          error code.
  ///
  /// It is used as @ref lock.
  [[nodiscard]] Expected<sys::fs::FileLocker>
  tryLockFor(Duration const &Timeout);
};

/// This returns a reference to a raw_fd_ostream for standard output.
raw_fd_ostream &outs();
/// This returns a reference to a raw_ostream for standard error.
raw_fd_ostream &errs();
/// This returns a reference to a raw_ostream which simply discards output.
raw_ostream &nulls();

//===----------------------------------------------------------------------===//
// File Streams
//===----------------------------------------------------------------------===//

/// A raw_ostream of a file for reading/writing/seeking.
///
class raw_fd_stream : public raw_fd_ostream {
public:
  /// Open the specified file for reading/writing/seeking. If an error occurs,
  /// information about the error is put into EC, and the stream should be
  /// immediately destroyed.
  raw_fd_stream(StringRef Filename, std::error_code &EC);

  raw_fd_stream(int fd, bool shouldClose);

  /// This reads the \p Size bytes into a buffer pointed by \p Ptr.
  ///
  /// \param Ptr The start of the buffer to hold data to be read.
  ///
  /// \param Size The number of bytes to be read.
  ///
  /// On success, the number of bytes read is returned, and the file position is
  /// advanced by this number. On error, -1 is returned, use error() to get the
  /// error code.
  ssize_t read(char *Ptr, size_t Size);

  /// Check if \p OS is a pointer of type raw_fd_stream*.
  static bool classof(const raw_ostream *OS);
};

//===----------------------------------------------------------------------===//
// Output Stream Adaptors
//===----------------------------------------------------------------------===//

/// A raw_ostream that writes to an std::string.  This is a simple adaptor
/// class. This class does not encounter output errors.
/// raw_string_ostream operates without a buffer, delegating all memory
/// management to the std::string. Thus the std::string is always up-to-date,
/// may be used directly and there is no need to call flush().
class raw_string_ostream : public raw_ostream {
  std::string &OS;

  /// See raw_ostream::write_impl.
  void write_impl(const char *Ptr, size_t Size) override;

  /// Return the current position within the stream, not counting the bytes
  /// currently in the buffer.
  uint64_t current_pos() const override { return OS.size(); }

public:
  explicit raw_string_ostream(std::string &O) : OS(O) { SetUnbuffered(); }

  /// Returns the string's reference. In most cases it is better to simply use
  /// the underlying std::string directly.
  /// TODO: Consider removing this API.
  std::string &str() { return OS; }

  void reserveExtraSpace(uint64_t ExtraSize) override {
    OS.reserve(tell() + ExtraSize);
  }
};

/// A raw_ostream that writes to an SmallVector or SmallString.  This is a
/// simple adaptor class. This class does not encounter output errors.
/// raw_svector_ostream operates without a buffer, delegating all memory
/// management to the SmallString. Thus the SmallString is always up-to-date,
/// may be used directly and there is no need to call flush().
class raw_svector_ostream : public raw_pwrite_stream {
  SmallVectorImpl<char> &OS;

  /// See raw_ostream::write_impl.
  void write_impl(const char *Ptr, size_t Size) override;

  void pwrite_impl(const char *Ptr, size_t Size, uint64_t Offset) override;

  /// Return the current position within the stream.
  uint64_t current_pos() const override;

public:
  /// Construct a new raw_svector_ostream.
  ///
  /// \param O The vector to write to; this should generally have at least 128
  /// bytes free to avoid any extraneous memory overhead.
  explicit raw_svector_ostream(SmallVectorImpl<char> &O) : OS(O) {
    SetUnbuffered();
  }

  ~raw_svector_ostream() override = default;

  void flush() = delete;

  /// Return a StringRef for the vector contents.
  StringRef str() const { return StringRef(OS.data(), OS.size()); }

  void reserveExtraSpace(uint64_t ExtraSize) override {
    OS.reserve(tell() + ExtraSize);
  }
};

/// A raw_ostream that discards all output.
class raw_null_ostream : public raw_pwrite_stream {
  /// See raw_ostream::write_impl.
  void write_impl(const char *Ptr, size_t size) override;
  void pwrite_impl(const char *Ptr, size_t Size, uint64_t Offset) override;

  /// Return the current position within the stream, not counting the bytes
  /// currently in the buffer.
  uint64_t current_pos() const override;

public:
  explicit raw_null_ostream() = default;
  ~raw_null_ostream() override;
};

class buffer_ostream : public raw_svector_ostream {
  raw_ostream &OS;
  SmallVector<char, 0> Buffer;

  void anchor() override;

public:
  buffer_ostream(raw_ostream &OS) : raw_svector_ostream(Buffer), OS(OS) {}
  ~buffer_ostream() override { OS << str(); }
};

class buffer_unique_ostream : public raw_svector_ostream {
  std::unique_ptr<raw_ostream> OS;
  SmallVector<char, 0> Buffer;

  void anchor() override;

public:
  buffer_unique_ostream(std::unique_ptr<raw_ostream> OS)
      : raw_svector_ostream(Buffer), OS(std::move(OS)) {
    // Turn off buffering on OS, which we now own, to avoid allocating a buffer
    // when the destructor writes only to be immediately flushed again.
    this->OS->SetUnbuffered();
  }
  ~buffer_unique_ostream() override { *OS << str(); }
};

class Error;

template <typename T, typename = decltype(std::declval<raw_ostream &>()
                                          << std::declval<const T &>())>
raw_ostream &operator<<(raw_ostream &OS, const std::optional<T> &O) {
  if (O)
    OS << *O;
  else
    OS << "None";
  return OS;
}

} // end namespace llvm

// ======================================================================
// Inline implementations - placed after class definitions to allow
// inclusion of headers that may themselves use raw_ostream declarations.
// ======================================================================

#include "csupport/lnative_lformatting.h"
#include "csupport/lprocess.h"
#include "csupport/raw_ostream.h"
#include "llvm/Support/Duration.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/NativeFormatting.h"
#include <stdlib.h>

#if defined(HAVE_UNISTD_H)
#include <unistd.h>
#endif
#if defined(HAVE_FCNTL_H)
#include <fcntl.h>
#endif
#include <sys/stat.h>

#if defined(__CYGWIN__)
#include <io.h>
#endif

#if defined(_MSC_VER)
#include <io.h>
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif
#endif

#ifdef _WIN32
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/Windows/WindowsSupport.h"
#endif

namespace llvm {

//===----------------------------------------------------------------------===//
//  raw_ostream
//===----------------------------------------------------------------------===//

inline raw_ostream::~raw_ostream() {
  assert(OutBufCur == OutBufStart &&
         "raw_ostream destructor called with non-empty buffer!");
  if (BufferMode == BufferKind::InternalBuffer)
    free(OutBufStart);
}

inline size_t raw_ostream::preferred_buffer_size() const {
#ifdef _WIN32
  return (16 * 1024);
#else
  return BUFSIZ;
#endif
}

inline void raw_ostream::SetBuffered() {
  if (size_t Size = preferred_buffer_size())
    SetBufferSize(Size);
  else
    SetUnbuffered();
}

inline void raw_ostream::SetBufferAndMode(char *BufferStart, size_t Size,
                                          BufferKind Mode) {
  assert(((Mode == BufferKind::Unbuffered && !BufferStart && Size == 0) ||
          (Mode != BufferKind::Unbuffered && BufferStart && Size != 0)) &&
         "stream must be unbuffered or have at least one byte");
  assert(GetNumBytesInBuffer() == 0 && "Current buffer is non-empty!");

  if (BufferMode == BufferKind::InternalBuffer)
    free(OutBufStart);
  OutBufStart = BufferStart;
  OutBufEnd = OutBufStart + Size;
  OutBufCur = OutBufStart;
  BufferMode = Mode;

  assert(OutBufStart <= OutBufEnd && "Invalid size!");
}

inline raw_ostream &raw_ostream::operator<<(unsigned long N) {
  write_integer(*this, (uint64_t)(N), 0, IntegerStyle::Integer);
  return *this;
}

inline raw_ostream &raw_ostream::operator<<(long N) {
  write_integer(*this, (int64_t)(N), 0, IntegerStyle::Integer);
  return *this;
}

inline raw_ostream &raw_ostream::operator<<(unsigned long long N) {
  write_integer(*this, (uint64_t)(N), 0, IntegerStyle::Integer);
  return *this;
}

inline raw_ostream &raw_ostream::operator<<(long long N) {
  write_integer(*this, (int64_t)(N), 0, IntegerStyle::Integer);
  return *this;
}

inline raw_ostream &raw_ostream::write_hex(unsigned long long N) {
  llvm::write_hex(*this, N, HexPrintStyle::Lower);
  return *this;
}

inline raw_ostream &raw_ostream::operator<<(Colors C) {
  if (C == Colors::RESET)
    resetColor();
  else
    changeColor(C);
  return *this;
}

inline raw_ostream &raw_ostream::write_uuid(const uuid_t UUID) {
  char buf[37];
  csupport_format_uuid_to_buf(UUID, buf, sizeof(buf));
  return *this << buf;
}

inline raw_ostream &raw_ostream::write_escaped(StringRef Str,
                                               bool UseHexEscapes) {
  char buf[1024];
  size_t len = Str.size();
  size_t off = 0;
  while (off < len) {
    size_t chunk = len - off;
    if (chunk > 200)
      chunk = 200;
    int n = csupport_write_escaped_to_buf(Str.data() + off, chunk, buf,
                                          sizeof(buf), UseHexEscapes);
    if (n > 0)
      this->write(buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
    off += chunk;
  }
  return *this;
}

inline raw_ostream &raw_ostream::operator<<(const void *P) {
  llvm::write_hex(*this, (uintptr_t)P, HexPrintStyle::PrefixLower);
  return *this;
}

inline raw_ostream &raw_ostream::operator<<(double N) {
  llvm::write_double(*this, N, FloatStyle::Exponent);
  return *this;
}

inline void raw_ostream::flush_nonempty() {
  assert(OutBufCur > OutBufStart && "Invalid call to flush_nonempty.");
  size_t Length = OutBufCur - OutBufStart;
  OutBufCur = OutBufStart;
  flush_tied_then_write(OutBufStart, Length);
}

inline raw_ostream &raw_ostream::write(unsigned char C) {
  if (LLVM_UNLIKELY(OutBufCur >= OutBufEnd)) {
    if (LLVM_UNLIKELY(!OutBufStart)) {
      if (BufferMode == BufferKind::Unbuffered) {
        flush_tied_then_write((char *)(&C), 1);
        return *this;
      }
      SetBuffered();
      return write(C);
    }
    flush_nonempty();
  }
  *OutBufCur++ = C;
  return *this;
}

inline raw_ostream &raw_ostream::write(const char *Ptr, size_t Size) {
  if (LLVM_UNLIKELY(size_t(OutBufEnd - OutBufCur) < Size)) {
    if (LLVM_UNLIKELY(!OutBufStart)) {
      if (BufferMode == BufferKind::Unbuffered) {
        flush_tied_then_write(Ptr, Size);
        return *this;
      }
      SetBuffered();
      return write(Ptr, Size);
    }

    size_t NumBytes = OutBufEnd - OutBufCur;

    if (LLVM_UNLIKELY(OutBufCur == OutBufStart)) {
      assert(NumBytes != 0 && "undefined behavior");
      size_t BytesToWrite = Size - (Size % NumBytes);
      flush_tied_then_write(Ptr, BytesToWrite);
      size_t BytesRemaining = Size - BytesToWrite;
      if (BytesRemaining > size_t(OutBufEnd - OutBufCur)) {
        return write(Ptr + BytesToWrite, BytesRemaining);
      }
      copy_to_buffer(Ptr + BytesToWrite, BytesRemaining);
      return *this;
    }

    copy_to_buffer(Ptr, NumBytes);
    flush_nonempty();
    return write(Ptr + NumBytes, Size - NumBytes);
  }

  copy_to_buffer(Ptr, Size);
  return *this;
}

inline void raw_ostream::copy_to_buffer(const char *Ptr, size_t Size) {
  assert(Size <= size_t(OutBufEnd - OutBufCur) && "Buffer overrun!");
  csupport_ostream_copy_to_buffer_small(Ptr, Size, OutBufCur);
  OutBufCur += Size;
}

inline void raw_ostream::flush_tied_then_write(const char *Ptr, size_t Size) {
  if (TiedStream)
    TiedStream->flush();
  write_impl(Ptr, Size);
}

/* Format-dependent operator<< implementations are in Format.h and
 * FormatVariadic.h */

static inline raw_ostream &write_padding_char(raw_ostream &OS, char C,
                                              unsigned NumChars) {
  char Buf[80];
  while (NumChars) {
    unsigned n = NumChars < sizeof(Buf) ? NumChars : (unsigned)sizeof(Buf);
    csupport_ostream_write_padding(Buf, sizeof(Buf), C, n);
    OS.write(Buf, n);
    NumChars -= n;
  }
  return OS;
}

inline raw_ostream &raw_ostream::indent(unsigned NumSpaces) {
  return write_padding_char(*this, ' ', NumSpaces);
}

inline raw_ostream &raw_ostream::write_zeros(unsigned NumZeros) {
  return write_padding_char(*this, '\0', NumZeros);
}

inline void raw_ostream::anchor() {}
inline void raw_pwrite_stream::anchor() {}
inline void buffer_ostream::anchor() {}
inline void buffer_unique_ostream::anchor() {}

inline bool raw_ostream::prepare_colors() {
  if (!ColorEnabled)
    return false;
  if (csupport_color_needs_flush() && !is_displayed())
    return false;
  if (csupport_color_needs_flush())
    flush();
  return true;
}

inline raw_ostream &raw_ostream::changeColor(enum Colors colors, bool bold,
                                             bool bg) {
  if (!prepare_colors())
    return *this;
  const char *colorcode =
      (colors == SAVEDCOLOR)
          ? csupport_output_bold(bg)
          : csupport_output_color(static_cast<char>(colors), bold, bg);
  if (colorcode)
    write(colorcode, strlen(colorcode));
  return *this;
}

inline raw_ostream &raw_ostream::resetColor() {
  if (!prepare_colors())
    return *this;
  if (const char *colorcode = csupport_reset_color())
    write(colorcode, strlen(colorcode));
  return *this;
}

inline raw_ostream &raw_ostream::reverseColor() {
  if (!prepare_colors())
    return *this;
  if (const char *colorcode = csupport_output_reverse())
    write(colorcode, strlen(colorcode));
  return *this;
}

inline raw_fd_ostream::raw_fd_ostream(int fd, bool shouldClose, bool unbuffered,
                                      OStreamKind K)
    : raw_pwrite_stream(unbuffered, K), FD(fd), ShouldClose(shouldClose) {
  if (FD < 0) {
    ShouldClose = false;
    return;
  }
  enable_colors(true);
  if (FD <= csupport_stderr_fileno())
    ShouldClose = false;
  int64_t loc = csupport_fd_tell(FD);
  IsRegularFile = csupport_fd_is_regular_file(FD);
#ifdef _WIN32
  SupportsSeeking = IsRegularFile;
  IsWindowsConsole =
      ::GetFileType((HANDLE)::_get_osfhandle(fd)) == FILE_TYPE_CHAR;
#else
  SupportsSeeking = loc != -1;
#endif
  if (!SupportsSeeking)
    pos = 0;
  else
    pos = static_cast<uint64_t>(loc);
}

namespace detail {
inline int getFDForRawOstream(StringRef Filename, std::error_code &EC,
                              unsigned Disp, unsigned Access, unsigned Flags) {
  if (Filename == "-") {
    EC = std::error_code();
    csupport_change_stdout_mode(Flags);
    return csupport_stdout_fileno();
  }
  char namebuf[4096];
  size_t len = Filename.size();
  if (len >= sizeof(namebuf)) {
    EC = std::make_error_code(std::errc::filename_too_long);
    return -1;
  }
  memcpy(namebuf, Filename.data(), len);
  namebuf[len] = '\0';
  int err = 0;
  int fd =
      csupport_fd_open(namebuf, len, (int)Disp, (int)Access, (int)Flags, &err);
  if (fd < 0) {
    EC = std::error_code(err, std::generic_category());
    return -1;
  }
  EC = std::error_code();
  return fd;
}
} // namespace detail

inline raw_fd_ostream::raw_fd_ostream(StringRef Filename, std::error_code &EC)
    : raw_fd_ostream(Filename, EC, static_cast<sys::fs::CreationDisposition>(0),
                     static_cast<sys::fs::FileAccess>(2),
                     static_cast<sys::fs::OpenFlags>(0)) {}

inline raw_fd_ostream::raw_fd_ostream(StringRef Filename, std::error_code &EC,
                                      sys::fs::CreationDisposition Disp)
    : raw_fd_ostream(Filename, EC, Disp, static_cast<sys::fs::FileAccess>(2),
                     static_cast<sys::fs::OpenFlags>(0)) {}

inline raw_fd_ostream::raw_fd_ostream(StringRef Filename, std::error_code &EC,
                                      sys::fs::FileAccess Access)
    : raw_fd_ostream(Filename, EC, static_cast<sys::fs::CreationDisposition>(0),
                     Access, static_cast<sys::fs::OpenFlags>(0)) {}

inline raw_fd_ostream::raw_fd_ostream(StringRef Filename, std::error_code &EC,
                                      sys::fs::OpenFlags Flags)
    : raw_fd_ostream(Filename, EC, static_cast<sys::fs::CreationDisposition>(0),
                     static_cast<sys::fs::FileAccess>(2), Flags) {}

inline raw_fd_ostream::raw_fd_ostream(StringRef Filename, std::error_code &EC,
                                      sys::fs::CreationDisposition Disp,
                                      sys::fs::FileAccess Access,
                                      sys::fs::OpenFlags Flags)
    : raw_fd_ostream(
          detail::getFDForRawOstream(Filename, EC, Disp, Access, Flags), true) {
}

inline raw_fd_ostream::~raw_fd_ostream() {
  if (FD >= 0) {
    flush();
    if (ShouldClose) {
      if (int Err = csupport_safely_close_fd(FD))
        error_detected(std::error_code(Err, std::generic_category()));
    }
  }
#ifdef __MINGW32__
  if (FD == csupport_stderr_fileno())
    return;
#endif
  if (has_error())
    report_fatal_error(
        Twine("IO failure on output stream: ") + error().message(), false);
}

inline void raw_fd_ostream::close() {
  assert(ShouldClose);
  ShouldClose = false;
  flush();
  if (int Err = csupport_safely_close_fd(FD))
    error_detected(std::error_code(Err, std::generic_category()));
  FD = -1;
}

inline uint64_t raw_fd_ostream::seek(uint64_t off) {
  assert(SupportsSeeking && "Stream does not support seeking!");
  flush();
  pos = csupport_fd_seek(FD, off);
  if (pos == (uint64_t)-1)
    error_detected(std::error_code(errno, std::generic_category()));
  return pos;
}

inline void raw_fd_ostream::pwrite_impl(const char *Ptr, size_t Size,
                                        uint64_t Offset) {
  uint64_t Pos = tell();
  seek(Offset);
  write(Ptr, Size);
  seek(Pos);
}

inline size_t raw_fd_ostream::preferred_buffer_size() const {
#if defined(_WIN32)
  if (IsWindowsConsole)
    return 0;
  return raw_ostream::preferred_buffer_size();
#else
  assert(FD >= 0 && "File not yet open!");
  return csupport_fd_preferred_buffer_size(FD, is_displayed());
#endif
}

inline bool raw_fd_ostream::is_displayed() const {
  return csupport_fd_is_displayed(FD);
}

inline bool raw_fd_ostream::has_colors() const {
  if (HasColors < 0)
    HasColors = csupport_fd_has_colors(FD) ? 1 : 0;
  return HasColors != 0;
}

inline void raw_fd_ostream::write_impl(const char *Ptr, size_t Size) {
  assert(FD >= 0 && "File already closed.");
  pos += Size;
#if defined(_WIN32)
  if (IsWindowsConsole)
    if (csupport_fd_write_console(FD, Ptr, Size))
      return;
#endif
  int rc = csupport_fd_write(FD, Ptr, Size);
  if (rc < 0)
    error_detected(std::error_code(-rc, std::generic_category()));
}

inline void raw_fd_ostream::anchor() {}

inline raw_fd_stream::raw_fd_stream(int fd, bool shouldClose)
    : raw_fd_ostream(fd, shouldClose, false, OStreamKind::OK_FDStream) {}

inline raw_fd_stream::raw_fd_stream(StringRef Filename, std::error_code &EC)
    : raw_fd_ostream(detail::getFDForRawOstream(
                         Filename, EC,
                         static_cast<sys::fs::CreationDisposition>(0),
                         static_cast<sys::fs::FileAccess>(3),
                         static_cast<sys::fs::OpenFlags>(0)),
                     true, false, OStreamKind::OK_FDStream) {
  if (EC)
    return;
  if (!isRegularFile())
    EC = std::make_error_code(std::errc::invalid_argument);
}

inline ssize_t raw_fd_stream::read(char *Ptr, size_t Size) {
  assert(get_fd() >= 0 && "File already closed.");
  ssize_t Ret = ::read(get_fd(), (void *)Ptr, Size);
  if (Ret >= 0)
    inc_pos(Ret);
  else
    error_detected(std::error_code(errno, std::generic_category()));
  return Ret;
}

inline bool raw_fd_stream::classof(const raw_ostream *OS) {
  return OS->get_kind() == OStreamKind::OK_FDStream;
}

inline void raw_string_ostream::write_impl(const char *Ptr, size_t Size) {
  OS.append(Ptr, Size);
}
inline uint64_t raw_svector_ostream::current_pos() const { return OS.size(); }
inline void raw_svector_ostream::write_impl(const char *Ptr, size_t Size) {
  OS.append(Ptr, Ptr + Size);
}
inline void raw_svector_ostream::pwrite_impl(const char *Ptr, size_t Size,
                                             uint64_t Offset) {
  memcpy(OS.data() + Offset, Ptr, Size);
}
inline raw_null_ostream::~raw_null_ostream() {
#ifndef NDEBUG
  flush();
#endif
}
inline void raw_null_ostream::write_impl(const char *, size_t) {}
inline uint64_t raw_null_ostream::current_pos() const { return 0; }
inline void raw_null_ostream::pwrite_impl(const char *, size_t, uint64_t) {}

// Twine methods that depend on raw_ostream (Twine.h included at file scope
// above) Definition in CSupportHost/raw_ostream_extra.inc (one TU for all
// callers).
inline void Twine::printOneChild(raw_ostream &OS, Child Ptr,
                                 NodeKind Kind) const {
  switch (Kind) {
  case NullKind:
  case EmptyKind:
    break;
  case TwineKind:
    Ptr.twine->print(OS);
    break;
  case CStringKind:
    OS << Ptr.cString;
    break;
  case StdStringKind:
    OS << *Ptr.stdString;
    break;
  case PtrAndLengthKind:
  case StringLiteralKind:
    OS << StringRef(Ptr.ptrAndLength.ptr, Ptr.ptrAndLength.length);
    break;
  case FormatvObjectKind:
    twine_print_formatv_to_stream(OS, Ptr.formatvObject);
    break;
  case CharKind:
    OS << Ptr.character;
    break;
  case DecUIKind:
    OS << Ptr.decUI;
    break;
  case DecIKind:
    OS << Ptr.decI;
    break;
  case DecULKind:
    OS << *Ptr.decUL;
    break;
  case DecLKind:
    OS << *Ptr.decL;
    break;
  case DecULLKind:
    OS << *Ptr.decULL;
    break;
  case DecLLKind:
    OS << *Ptr.decLL;
    break;
  case UHexKind:
    OS.write_hex(*Ptr.uHex);
    break;
  }
}
inline void Twine::printOneChildRepr(raw_ostream &OS, Child Ptr,
                                     NodeKind Kind) const {
  switch (Kind) {
  case NullKind:
    OS << "null";
    break;
  case EmptyKind:
    OS << "empty";
    break;
  case TwineKind:
    OS << "rope:";
    Ptr.twine->printRepr(OS);
    break;
  case CStringKind:
    OS << "cstring:\"" << Ptr.cString << "\"";
    break;
  case StdStringKind:
    OS << "stdstring:\"" << Ptr.stdString << "\"";
    break;
  case PtrAndLengthKind:
    OS << "ptrAndLength:\""
       << StringRef(Ptr.ptrAndLength.ptr, Ptr.ptrAndLength.length) << "\"";
    break;
  case StringLiteralKind:
    OS << "constexprPtrAndLength:\""
       << StringRef(Ptr.ptrAndLength.ptr, Ptr.ptrAndLength.length) << "\"";
    break;
  case FormatvObjectKind:
    twine_print_formatv_repr_to_stream(OS, Ptr.formatvObject);
    break;
  case CharKind:
    OS << "char:\"" << Ptr.character << "\"";
    break;
  case DecUIKind:
    OS << "decUI:\"" << Ptr.decUI << "\"";
    break;
  case DecIKind:
    OS << "decI:\"" << Ptr.decI << "\"";
    break;
  case DecULKind:
    OS << "decUL:\"" << *Ptr.decUL << "\"";
    break;
  case DecLKind:
    OS << "decL:\"" << *Ptr.decL << "\"";
    break;
  case DecULLKind:
    OS << "decULL:\"" << *Ptr.decULL << "\"";
    break;
  case DecLLKind:
    OS << "decLL:\"" << *Ptr.decLL << "\"";
    break;
  case UHexKind:
    OS << "uhex:\"" << Ptr.uHex << "\"";
    break;
  }
}
inline void Twine::print(raw_ostream &OS) const {
  printOneChild(OS, LHS, getLHSKind());
  printOneChild(OS, RHS, getRHSKind());
}
inline void Twine::printRepr(raw_ostream &OS) const {
  OS << "(Twine ";
  printOneChildRepr(OS, LHS, getLHSKind());
  OS << " ";
  printOneChildRepr(OS, RHS, getRHSKind());
  OS << ")";
}
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
inline LLVM_DUMP_METHOD void Twine::dump() const { print(errs()); }
inline LLVM_DUMP_METHOD void Twine::dumpRepr() const { printRepr(errs()); }
#endif

inline raw_fd_ostream &outs() {
  static raw_fd_ostream S(csupport_stdout_fileno(), false, false);
  return S;
}
inline raw_fd_ostream &errs() {
  static raw_fd_ostream S(csupport_stderr_fileno(), false, true);
  return S;
}
inline raw_ostream &nulls() {
  static raw_null_ostream S;
  return S;
}

} // namespace llvm

#endif // LLVM_SUPPORT_RAW_OSTREAM_H
