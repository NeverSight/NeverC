#include "NeverCTestFixture.h"
#include <filesystem>

class StdLibTest : public NeverCTest {
protected:
  static std::string stdTestDir() {
    return (testDir() / "std").string();
  }

  static std::string stdSrcDir() {
    return (testDir().parent_path().parent_path() / "std").string();
  }

  CmdResult compileAndRunStdTest(const std::string &testName,
                                  const std::vector<std::string> &srcs = {}) {
    fs::path testFile = fs::path(stdTestDir()) / ("test_" + testName + ".c");
    if (!fs::exists(testFile))
      return {1, "", "test file not found: " + testFile.string()};

    fs::path outBin = tmp() / ("test_" + testName);
    std::string sd = stdSrcDir();

    std::vector<std::string> args;
    args.push_back("-I" + sd + "/include");
    args.push_back("-Wall");
    args.push_back("-Wextra");
    args.push_back("-Wno-unused-parameter");
    args.push_back("-Wno-unused-function");
    args.push_back("-O1");

    if (!srcs.empty())
      args.push_back("-fno-builtin-std");

    args.push_back("-o");
    args.push_back(outBin.string());
    args.push_back(testFile.string());
    for (const auto &s : srcs)
      args.push_back(sd + "/" + s);
#ifndef _WIN32
    args.push_back("-lm");
    args.push_back("-lpthread");
#else
    args.push_back("-Wno-deprecated-declarations");
#endif

    CmdResult compile = ncc(args);
    if (!compile.ok())
      return compile;

    return exec(outBin.string(), {});
  }
};

#define STD_TEST(name, ...)                                     \
  TEST_F(StdLibTest, name) {                                    \
    auto r = compileAndRunStdTest(#name, {__VA_ARGS__});         \
    ASSERT_TRUE(r.ok()) << "stdout: " << r.out                 \
                        << "\nstderr: " << r.err;               \
    EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;  \
  }

// ===== Math =====
STD_TEST(math, "src/math/abs.c", "src/math/acos.c", "src/math/acosh.c", "src/math/asin.c", "src/math/asinh.c", "src/math/atan.c", "src/math/atan2.c", "src/math/atanh.c", "src/math/cbrt.c", "src/math/ceil.c", "src/math/copysign.c", "src/math/cos.c", "src/math/cosh.c", "src/math/dim.c", "src/math/erf.c", "src/math/erfc.c", "src/math/erfcinv.c", "src/math/erfinv.c", "src/math/exp.c", "src/math/exp2.c", "src/math/expm1.c", "src/math/float32bits.c", "src/math/float64bits.c", "src/math/floor.c", "src/math/fma.c", "src/math/fmod.c", "src/math/frexp.c", "src/math/gamma.c", "src/math/hypot.c", "src/math/ilogb.c", "src/math/inf.c", "src/math/isinf.c", "src/math/isnan.c", "src/math/j0.c", "src/math/j1.c", "src/math/jn.c", "src/math/ldexp.c", "src/math/lgamma.c", "src/math/log.c", "src/math/log10.c", "src/math/log1p.c", "src/math/log2.c", "src/math/logb.c", "src/math/max.c", "src/math/min.c", "src/math/modf.c", "src/math/nan.c", "src/math/nextafter.c", "src/math/nextafter32.c", "src/math/pow.c", "src/math/pow10.c", "src/math/remainder.c", "src/math/round.c", "src/math/roundtoeven.c", "src/math/signbit.c", "src/math/sin.c", "src/math/sincos.c", "src/math/sinh.c", "src/math/sqrt.c", "src/math/tan.c", "src/math/tanh.c", "src/math/trunc.c")
STD_TEST(strconv, "src/strconv/format_bool.c", "src/strconv/format_float.c", "src/strconv/format_int.c", "src/strconv/parse_bool.c", "src/strconv/parse_float.c", "src/strconv/parse_int.c", "src/strconv/quote.c", "src/unicode/utf8/utf8.c", "src/unicode/unicode.c")
STD_TEST(path, "src/path/base.c", "src/path/dir.c", "src/path/ext.c", "src/path/isabs.c", "src/path/clean.c", "src/path/join.c", "src/path/split.c", "src/path/match.c")
STD_TEST(sort, "src/sort/sort.c")
// Differential fuzz over the shared sort + substring-search engines (catches
// structural regressions like the Timsort merge-invariant overflow).
STD_TEST(sortsearch_fuzz, "src/sort/sort.c", "src/bytes/bytes.c")
STD_TEST(rand, "src/math/rand/rand.c", "src/math/log.c", "src/math/exp.c", "src/math/frexp.c", "src/math/ldexp.c", "src/math/floor.c", "src/math/modf.c", "src/math/trunc.c", "src/math/sqrt.c")
STD_TEST(bits, "src/math/bits/bits.c")
STD_TEST(cmplx, "src/math/cmplx/cmplx.c", "src/math/abs.c", "src/math/acos.c", "src/math/acosh.c", "src/math/asin.c", "src/math/asinh.c", "src/math/atan.c", "src/math/atan2.c", "src/math/atanh.c", "src/math/cbrt.c", "src/math/ceil.c", "src/math/copysign.c", "src/math/cos.c", "src/math/cosh.c", "src/math/dim.c", "src/math/erf.c", "src/math/erfc.c", "src/math/erfcinv.c", "src/math/erfinv.c", "src/math/exp.c", "src/math/exp2.c", "src/math/expm1.c", "src/math/float32bits.c", "src/math/float64bits.c", "src/math/floor.c", "src/math/fma.c", "src/math/fmod.c", "src/math/frexp.c", "src/math/gamma.c", "src/math/hypot.c", "src/math/ilogb.c", "src/math/inf.c", "src/math/isinf.c", "src/math/isnan.c", "src/math/j0.c", "src/math/j1.c", "src/math/jn.c", "src/math/ldexp.c", "src/math/lgamma.c", "src/math/log.c", "src/math/log10.c", "src/math/log1p.c", "src/math/log2.c", "src/math/logb.c", "src/math/max.c", "src/math/min.c", "src/math/modf.c", "src/math/nan.c", "src/math/nextafter.c", "src/math/nextafter32.c", "src/math/pow.c", "src/math/pow10.c", "src/math/remainder.c", "src/math/round.c", "src/math/roundtoeven.c", "src/math/signbit.c", "src/math/sin.c", "src/math/sincos.c", "src/math/sinh.c", "src/math/sqrt.c", "src/math/tan.c", "src/math/tanh.c", "src/math/trunc.c")
STD_TEST(big, "src/math/big/big.c")

// ===== Encoding =====
STD_TEST(hex, "src/encoding/hex/decode.c", "src/encoding/hex/encode.c")
STD_TEST(base64, "src/encoding/base64/base64.c")
STD_TEST(base32, "src/encoding/base32/base32.c")
STD_TEST(binary, "src/encoding/binary/binary.c")
STD_TEST(ascii85, "src/encoding/ascii85/ascii85.c")
STD_TEST(pem, "src/encoding/pem/pem.c", "src/encoding/base64/base64.c")
STD_TEST(json, "src/encoding/json/json.c", "src/strconv/format_float.c", "src/strconv/parse_float.c")
STD_TEST(csv, "src/encoding/csv/csv.c")
STD_TEST(xml, "src/encoding/xml/xml.c")
STD_TEST(asn1, "src/encoding/asn1/asn1.c")

// ===== Hash =====
STD_TEST(fnv, "src/hash/fnv/fnv.c")
STD_TEST(crc32, "src/hash/crc32/crc32.c")
STD_TEST(crc64, "src/hash/crc64/crc64.c")
#ifndef _WIN32
TEST_F(StdLibTest, crc_concurrency) {
  auto r = compileAndRunStdTest("crc_concurrency",
                                {"src/hash/crc32/crc32.c", "src/hash/crc64/crc64.c"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
#endif
STD_TEST(adler32, "src/hash/adler32/adler32.c")
STD_TEST(maphash, "src/hash/maphash/maphash.c")

// ===== Crypto =====
STD_TEST(sha256, "src/crypto/sha256/sha256.c")
STD_TEST(sha1, "src/crypto/sha1/sha1.c")
STD_TEST(sha512, "src/crypto/sha512/sha512.c")
STD_TEST(sha384, "src/crypto/sha384/sha384.c", "src/crypto/sha512/sha512.c")
STD_TEST(sha224, "src/crypto/sha224/sha224.c", "src/crypto/sha256/sha256.c")
STD_TEST(sha3, "src/crypto/sha3/sha3.c")
STD_TEST(sha512_variants, "src/crypto/sha512_224/sha512_224.c", "src/crypto/sha512_256/sha512_256.c", "src/crypto/sha512/sha512.c", "src/crypto/sha256/sha256.c")
STD_TEST(md5, "src/crypto/md5/md5.c")
STD_TEST(aes, "src/crypto/aes/aes.c")
STD_TEST(des, "src/crypto/des/des.c")
STD_TEST(rc4, "src/crypto/rc4/rc4.c")
STD_TEST(chacha20, "src/crypto/chacha20/chacha20.c")
STD_TEST(poly1305, "src/crypto/poly1305/poly1305.c")
STD_TEST(chacha20poly1305, "src/crypto/chacha20poly1305/chacha20poly1305.c", "src/crypto/chacha20/chacha20.c", "src/crypto/poly1305/poly1305.c")
STD_TEST(gcm, "src/crypto/gcm/gcm.c", "src/crypto/aes/aes.c")
STD_TEST(cipher, "src/crypto/cipher/cipher.c", "src/crypto/aes/aes.c")
STD_TEST(hmac, "src/crypto/hmac/hmac.c", "src/crypto/sha256/sha256.c", "src/crypto/sha512/sha512.c", "src/crypto/sha1/sha1.c", "src/crypto/md5/md5.c", "src/crypto/subtle/subtle.c")
STD_TEST(subtle, "src/crypto/subtle/subtle.c")
STD_TEST(hkdf, "src/crypto/hkdf/hkdf.c", "src/crypto/hmac/hmac.c", "src/crypto/sha256/sha256.c", "src/crypto/sha512/sha512.c", "src/crypto/sha1/sha1.c", "src/crypto/md5/md5.c", "src/crypto/subtle/subtle.c")
STD_TEST(pbkdf2, "src/crypto/pbkdf2/pbkdf2.c", "src/crypto/hmac/hmac.c", "src/crypto/sha256/sha256.c", "src/crypto/sha512/sha512.c", "src/crypto/sha1/sha1.c", "src/crypto/md5/md5.c", "src/crypto/subtle/subtle.c")
STD_TEST(crypto_rand, "src/crypto/rand/rand.c")
STD_TEST(elliptic, "src/crypto/elliptic/elliptic.c", "src/math/big/big.c")
STD_TEST(rsa, "src/crypto/rsa/rsa.c", "src/math/big/big.c", "src/crypto/rand/rand.c", "src/crypto/sha256/sha256.c")
STD_TEST(ecdsa, "src/crypto/ecdsa/ecdsa.c", "src/crypto/elliptic/elliptic.c", "src/math/big/big.c", "src/crypto/rand/rand.c", "src/crypto/sha256/sha256.c")
STD_TEST(dsa, "src/crypto/dsa/dsa.c", "src/math/big/big.c", "src/crypto/rand/rand.c", "src/crypto/sha256/sha256.c")
STD_TEST(ed25519, "src/crypto/ed25519/ed25519.c", "src/crypto/sha512/sha512.c", "src/crypto/rand/rand.c", "src/math/big/big.c")
STD_TEST(ecdh, "src/crypto/ecdh/ecdh.c", "src/crypto/elliptic/elliptic.c", "src/math/big/big.c", "src/crypto/rand/rand.c", "src/crypto/sha256/sha256.c")
STD_TEST(hpke, "src/crypto/hpke/hpke.c", "src/crypto/ecdh/ecdh.c",
    "src/crypto/elliptic/elliptic.c", "src/math/big/big.c",
    "src/crypto/hkdf/hkdf.c", "src/crypto/hmac/hmac.c",
    "src/crypto/sha256/sha256.c", "src/crypto/sha512/sha512.c",
    "src/crypto/sha1/sha1.c", "src/crypto/md5/md5.c",
    "src/crypto/aes/aes.c", "src/crypto/gcm/gcm.c",
    "src/crypto/chacha20/chacha20.c", "src/crypto/poly1305/poly1305.c",
    "src/crypto/chacha20poly1305/chacha20poly1305.c",
    "src/crypto/rand/rand.c", "src/crypto/subtle/subtle.c")
STD_TEST(mlkem, "src/crypto/mlkem/mlkem.c", "src/crypto/sha3/sha3.c",
    "src/crypto/rand/rand.c")
STD_TEST(mldsa, "src/crypto/mldsa/mldsa.c", "src/crypto/sha3/sha3.c",
    "src/crypto/rand/rand.c")

// ===== Unicode =====
STD_TEST(unicode, "src/unicode/unicode.c")
STD_TEST(utf8, "src/unicode/utf8/utf8.c")
STD_TEST(utf16, "src/unicode/utf16/utf16.c")

// ===== Core =====
STD_TEST(cmp, "src/cmp/cmp.c")
STD_TEST(bytes, "src/bytes/bytes.c")
STD_TEST(errors, "src/errors/errors.c")
STD_TEST(html, "src/html/html.c")
STD_TEST(fmt, "src/fmt/fmt.c", "src/strconv/format_float.c", "src/strconv/parse_float.c")
STD_TEST(io, "src/io/io.c")
STD_TEST(bufio, "src/bufio/bufio.c", "src/io/io.c")
STD_TEST(flag, "src/flag/flag.c", "src/strconv/parse_float.c")
STD_TEST(log, "src/log/log.c")
STD_TEST(slog, "src/log/slog/slog.c")
STD_TEST(time, "src/time/time.c")
STD_TEST(uuid, "src/uuid/uuid.c")
STD_TEST(regexp, "src/regexp/regexp.c")
STD_TEST(mime, "src/mime/mime.c")
STD_TEST(context, "src/context/context.c")
STD_TEST(maps, "src/maps/maps.c")
STD_TEST(slices, "src/slices/slices.c")

// ===== Container =====
STD_TEST(heap, "src/container/heap/heap.c")
STD_TEST(list, "src/container/list/list.c")
STD_TEST(ring, "src/container/ring/ring.c")
STD_TEST(vector, "src/container/vector/vector.c")

// ===== Compress =====
STD_TEST(lzw, "src/compress/lzw/lzw.c")
STD_TEST(flate, "src/compress/flate/flate.c")
STD_TEST(gzip, "src/compress/gzip/gzip.c", "src/compress/flate/flate.c", "src/hash/crc32/crc32.c")
STD_TEST(zlib, "src/compress/zlib/zlib.c", "src/compress/flate/flate.c", "src/hash/adler32/adler32.c")
STD_TEST(bzip2, "src/compress/bzip2/bzip2.c")

// ===== Archive =====
STD_TEST(tar, "src/archive/tar/tar.c")
STD_TEST(zip, "src/archive/zip/zip.c", "src/hash/crc32/crc32.c")

// ===== Text =====
// Note: 'template' is a C++ keyword, use template_ prefix
TEST_F(StdLibTest, text_template) {
  auto r = compileAndRunStdTest("template", {"src/text/template/template.c"});
  ASSERT_TRUE(r.ok()) << "stderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(scanner, "src/text/scanner/scanner.c")
STD_TEST(tabwriter, "src/text/tabwriter/tabwriter.c")

// ===== Index =====
STD_TEST(suffixarray, "src/index/suffixarray/suffixarray.c")

// ===== Sync =====
STD_TEST(sync, "src/sync/sync.c")
STD_TEST(atomic, "src/sync/atomic/atomic.c")

// ===== Net =====
STD_TEST(tcp, "src/net/tcp/tcp.c")
STD_TEST(udp, "src/net/udp/udp.c")

// http.c embeds HTTPS support; the COFF linker (Windows) requires all
// referenced TLS symbols to be present at link time even when the test
// only exercises plain HTTP.  Include the full TLS dependency chain for
// every target that compiles http.c.
#define HTTP_TLS_DEPS \
    "src/crypto/tls/tls.c", "src/crypto/ecdh/ecdh.c", \
    "src/crypto/aes/aes.c", "src/crypto/gcm/gcm.c", \
    "src/crypto/chacha20/chacha20.c", "src/crypto/poly1305/poly1305.c", \
    "src/crypto/chacha20poly1305/chacha20poly1305.c", \
    "src/crypto/sha256/sha256.c", "src/crypto/sha512/sha512.c", \
    "src/crypto/sha384/sha384.c", "src/crypto/sha1/sha1.c", "src/crypto/md5/md5.c", \
    "src/crypto/hmac/hmac.c", "src/crypto/hkdf/hkdf.c", \
    "src/crypto/rand/rand.c", "src/crypto/subtle/subtle.c", \
    "src/crypto/x509/x509.c", "src/crypto/elliptic/elliptic.c", \
    "src/math/big/big.c", "src/encoding/base64/base64.c"

STD_TEST(http, "src/net/http/http.c", "src/net/tcp/tcp.c", HTTP_TLS_DEPS)
STD_TEST(websocket, "src/net/websocket/websocket.c", "src/net/tcp/tcp.c",
    "src/net/http/http.c", HTTP_TLS_DEPS)
STD_TEST(url, "src/net/url/url.c")
STD_TEST(netip, "src/net/netip/netip.c")
STD_TEST(mail, "src/net/mail/mail.c")
STD_TEST(textproto, "src/net/textproto/textproto.c")
STD_TEST(httptest, "src/net/http/httptest/httptest.c",
    "src/net/http/http.c", "src/net/tcp/tcp.c", HTTP_TLS_DEPS)

// ===== io_uring =====
STD_TEST(io_uring, "src/net/tcp/tcp.c", "src/net/http/http.c", HTTP_TLS_DEPS)

// ===== HTTP/2 =====
STD_TEST(http2, "src/net/http/http2/http2.c", "src/net/http/http.c", "src/net/tcp/tcp.c",
    HTTP_TLS_DEPS)

// ===== HTTP Benchmark =====
STD_TEST(http_bench, "src/net/http/http.c", "src/net/tcp/tcp.c", HTTP_TLS_DEPS)

// ===== HTTP Util =====
STD_TEST(httputil, "src/net/http/httputil/httputil.c",
    "src/net/http/http.c", "src/net/tcp/tcp.c", HTTP_TLS_DEPS)

// ===== Cookie Jar =====
STD_TEST(cookiejar, "src/net/http/cookiejar/cookiejar.c")

// ===== SMTP =====
STD_TEST(smtp, "src/net/smtp/smtp.c", "src/net/tcp/tcp.c")

// ===== Net Core (DNS, Pipe, SplitHostPort) =====
STD_TEST(resolve, "src/net/resolve/resolve.c")
STD_TEST(net_interface, "src/net/interface/interface.c")

// ===== Net Internals (Timer Wheel, Buffer Pool, Poller, Event Loop) =====
STD_TEST(net_internals, "src/net/tcp/tcp.c")

// ===== Path =====
STD_TEST(filepath, "src/path/filepath/filepath.c")

// ===== Image =====
STD_TEST(color, "src/image/color/color.c")
STD_TEST(image, "src/image/image/image.c", "src/image/color/color.c")
STD_TEST(draw, "src/image/draw/draw.c", "src/image/image/image.c", "src/image/color/color.c")
STD_TEST(png, "src/image/png/png.c", "src/image/image/image.c", "src/image/color/color.c", "src/compress/flate/flate.c", "src/hash/crc32/crc32.c", "src/hash/adler32/adler32.c")
STD_TEST(jpeg, "src/image/jpeg/jpeg.c", "src/image/image/image.c", "src/image/color/color.c")
STD_TEST(gif, "src/image/gif/gif.c", "src/image/image/image.c", "src/image/color/color.c", "src/compress/lzw/lzw.c")
// Locks in the Wu-quantizer quality win in neverc_gif_from_rgba (lower error
// than the old 216-color web-safe cube; near-lossless on small off-grid palettes).
STD_TEST(gif_quant, "src/image/gif/gif.c", "src/image/image/image.c", "src/image/color/color.c", "src/compress/lzw/lzw.c")
STD_TEST(palette, "src/image/color/palette/palette.c")

// ===== OS =====
STD_TEST(os, "src/os/os.c")
STD_TEST(exec, "src/os/exec/exec.c")
STD_TEST(signal, "src/os/signal/signal.c")
STD_TEST(user, "src/os/user/user.c")

// ===== HTML =====
STD_TEST(html_template, "src/html/template/template.c", "src/html/html.c")

// ===== MIME =====
STD_TEST(quotedprintable, "src/mime/quotedprintable/quotedprintable.c")
STD_TEST(multipart, "src/mime/multipart/multipart.c")

// ===== IO =====
STD_TEST(fs, "src/io/fs/fs.c")

// ===== Log =====
STD_TEST(syslog, "src/log/syslog/syslog.c")

// ===== Debug =====
STD_TEST(elf, "src/debug/elf/elf.c")
STD_TEST(pe, "src/debug/pe/pe.c")
STD_TEST(macho, "src/debug/macho/macho.c")
STD_TEST(dwarf, "src/debug/dwarf/dwarf.c")
STD_TEST(plan9obj, "src/debug/plan9obj/plan9obj.c")

// ===== Crypto (x509) =====
STD_TEST(x509, "src/crypto/x509/x509.c")
STD_TEST(tls, HTTP_TLS_DEPS, "src/net/tcp/tcp.c")

// ===== Regexp =====
STD_TEST(regexp_syntax, "src/regexp/syntax/syntax.c")

// ===== Time =====
STD_TEST(tzdata, "src/time/tzdata/tzdata.c")

// ===== CString =====
STD_TEST(cstring, "src/cstring/cstring.c")

// ===== Arena =====
STD_TEST(arena, "src/arena/arena.c")

// ===== Unique =====
STD_TEST(unique, "src/unique/unique.c")

// ===== Weak =====
STD_TEST(weak, "src/weak/weak.c")

// ===== Dot-syntax (comprehensive) =====
STD_TEST(dot_syntax,
    "src/math/abs.c", "src/math/acos.c", "src/math/acosh.c", "src/math/asin.c",
    "src/math/asinh.c", "src/math/atan.c", "src/math/atan2.c", "src/math/atanh.c",
    "src/math/cbrt.c", "src/math/ceil.c", "src/math/copysign.c", "src/math/cos.c",
    "src/math/cosh.c", "src/math/dim.c", "src/math/erf.c", "src/math/erfc.c",
    "src/math/erfcinv.c", "src/math/erfinv.c", "src/math/exp.c", "src/math/exp2.c",
    "src/math/expm1.c", "src/math/float32bits.c", "src/math/float64bits.c",
    "src/math/floor.c", "src/math/fma.c", "src/math/fmod.c", "src/math/frexp.c",
    "src/math/gamma.c", "src/math/hypot.c", "src/math/ilogb.c", "src/math/inf.c",
    "src/math/isinf.c", "src/math/isnan.c", "src/math/j0.c", "src/math/j1.c",
    "src/math/jn.c", "src/math/ldexp.c", "src/math/lgamma.c", "src/math/log.c",
    "src/math/log10.c", "src/math/log1p.c", "src/math/log2.c", "src/math/logb.c",
    "src/math/max.c", "src/math/min.c", "src/math/modf.c", "src/math/nan.c",
    "src/math/nextafter.c", "src/math/nextafter32.c", "src/math/pow.c",
    "src/math/pow10.c", "src/math/remainder.c", "src/math/round.c",
    "src/math/roundtoeven.c", "src/math/signbit.c", "src/math/sin.c",
    "src/math/sincos.c", "src/math/sinh.c", "src/math/sqrt.c", "src/math/tan.c",
    "src/math/tanh.c", "src/math/trunc.c",
    "src/math/rand/rand.c",
    "src/math/bits/bits.c",
    "src/strconv/format_int.c", "src/strconv/parse_int.c", "src/strconv/format_bool.c",
    "src/strconv/format_float.c", "src/strconv/parse_float.c",
    "src/encoding/hex/encode.c", "src/encoding/hex/decode.c",
    "src/encoding/base64/base64.c",
    "src/hash/crc32/crc32.c",
    "src/hash/fnv/fnv.c",
    "src/hash/adler32/adler32.c",
    "src/container/vector/vector.c",
    "src/sort/sort.c",
    "src/cmp/cmp.c",
    "src/errors/errors.c",
    "src/crypto/sha256/sha256.c",
    "src/crypto/sha1/sha1.c",
    "src/crypto/md5/md5.c",
    "src/bytes/bytes.c",
    "src/unicode/unicode.c",
    "src/html/html.c",
    "src/path/base.c", "src/path/dir.c", "src/path/ext.c", "src/path/isabs.c", "src/path/clean.c",
    "src/slices/slices.c",
    "src/maps/maps.c",
    "src/regexp/regexp.c",
    "src/uuid/uuid.c",
    "src/fmt/fmt.c",
    "src/cstring/cstring.c",
    "src/context/context.c",
    "src/io/io.c",
    "src/time/time.c",
    "src/compress/gzip/gzip.c", "src/compress/flate/flate.c",
    "src/compress/zlib/zlib.c",
    "src/sync/sync.c",
    "src/sync/atomic/atomic.c",
    "src/os/os.c",
    "src/log/log.c",
    "src/encoding/json/json.c",
    "src/encoding/binary/binary.c",
    "src/container/list/list.c",
    "src/container/ring/ring.c",
    "src/path/filepath/filepath.c",
    "src/hash/crc64/crc64.c",
    "src/image/color/color.c",
    "src/flag/flag.c",
    "src/arena/arena.c",
    "src/unique/unique.c",
    "src/weak/weak.c",
    "src/net/tcp/tcp.c",
    "src/net/http/http.c",
    "src/net/url/url.c",
    HTTP_TLS_DEPS)
