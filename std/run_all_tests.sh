#!/bin/bash
set -e
cd "$(dirname "$0")"

CC="${CC:-cc}"
CFLAGS="-I include -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -O1"
OUTDIR="/tmp/neverc_tests"
mkdir -p "$OUTDIR"

TOTAL=0
PASS=0
FAIL=0
SKIP=0
FAIL_LIST=""

compile_and_run() {
    local name="$1"
    shift
    local test_file="tests/test_${name}.c"
    local out="$OUTDIR/test_${name}"
    
    if [ ! -f "$test_file" ]; then
        echo "SKIP: $name (no test file)"
        SKIP=$((SKIP + 1))
        return
    fi
    
    TOTAL=$((TOTAL + 1))
    
    if $CC $CFLAGS -o "$out" "$test_file" "$@" -lm -lpthread 2>/tmp/neverc_compile_err_${name}.txt; then
        if output=$("$out" 2>&1); then
            result=$(echo "$output" | tail -1)
            echo "PASS: $name  ($result)"
            PASS=$((PASS + 1))
        else
            echo "FAIL: $name (runtime error, exit=$?)"
            echo "$output" | tail -5
            FAIL=$((FAIL + 1))
            FAIL_LIST="$FAIL_LIST $name"
        fi
    else
        echo "FAIL: $name (compile error)"
        cat /tmp/neverc_compile_err_${name}.txt | head -10
        FAIL=$((FAIL + 1))
        FAIL_LIST="$FAIL_LIST $name"
    fi
}

echo "============================================"
echo "  NeverC Standard Library Test Runner"
echo "============================================"
echo ""

# Math module
MATH_SRCS=$(ls src/math/*.c 2>/dev/null | tr '\n' ' ')
compile_and_run "math" $MATH_SRCS

# Strconv
compile_and_run "strconv" src/strconv/*.c

# Path
compile_and_run "path" src/path/base.c src/path/dir.c src/path/ext.c src/path/isabs.c src/path/clean.c src/path/join.c src/path/split.c

# Sort
compile_and_run "sort" src/sort/sort.c

# Hex
compile_and_run "hex" src/encoding/hex/*.c

# FNV
compile_and_run "fnv" src/hash/fnv/fnv.c

# CRC32
compile_and_run "crc32" src/hash/crc32/crc32.c

# CRC64
compile_and_run "crc64" src/hash/crc64/crc64.c

# Adler32
compile_and_run "adler32" src/hash/adler32/adler32.c

# Base64
compile_and_run "base64" src/encoding/base64/base64.c

# Base32
compile_and_run "base32" src/encoding/base32/base32.c

# Rand
compile_and_run "rand" src/math/rand/rand.c

# Bits
compile_and_run "bits" src/math/bits/bits.c

# Binary
compile_and_run "binary" src/encoding/binary/binary.c

# ASCII85
compile_and_run "ascii85" src/encoding/ascii85/ascii85.c

# Unicode
compile_and_run "unicode" src/unicode/unicode.c

# UTF8
compile_and_run "utf8" src/unicode/utf8/utf8.c

# UTF16
compile_and_run "utf16" src/unicode/utf16/utf16.c

# PEM
compile_and_run "pem" src/encoding/pem/pem.c src/encoding/base64/base64.c

# SHA256
compile_and_run "sha256" src/crypto/sha256/sha256.c

# SHA1
compile_and_run "sha1" src/crypto/sha1/sha1.c

# SHA512
compile_and_run "sha512" src/crypto/sha512/sha512.c

# SHA384 (depends on sha512)
compile_and_run "sha384" src/crypto/sha384/sha384.c src/crypto/sha512/sha512.c

# SHA224 (depends on sha256)
compile_and_run "sha224" src/crypto/sha224/sha224.c src/crypto/sha256/sha256.c

# SHA3
compile_and_run "sha3" src/crypto/sha3/sha3.c

# SHA512 variants (depends on sha512 + sha256)
compile_and_run "sha512_variants" src/crypto/sha512_224/sha512_224.c src/crypto/sha512_256/sha512_256.c src/crypto/sha512/sha512.c src/crypto/sha256/sha256.c

# MD5
compile_and_run "md5" src/crypto/md5/md5.c

# AES
compile_and_run "aes" src/crypto/aes/aes.c

# DES
compile_and_run "des" src/crypto/des/des.c

# RC4
compile_and_run "rc4" src/crypto/rc4/rc4.c

# ChaCha20
compile_and_run "chacha20" src/crypto/chacha20/chacha20.c

# Poly1305
compile_and_run "poly1305" src/crypto/poly1305/poly1305.c

# ChaCha20-Poly1305
compile_and_run "chacha20poly1305" src/crypto/chacha20poly1305/chacha20poly1305.c src/crypto/chacha20/chacha20.c src/crypto/poly1305/poly1305.c

# GCM
compile_and_run "gcm" src/crypto/gcm/gcm.c src/crypto/aes/aes.c

# Cipher
compile_and_run "cipher" src/crypto/cipher/cipher.c src/crypto/aes/aes.c

# HMAC (depends on subtle)
compile_and_run "hmac" src/crypto/hmac/hmac.c src/crypto/sha256/sha256.c src/crypto/sha512/sha512.c src/crypto/sha1/sha1.c src/crypto/md5/md5.c src/crypto/subtle/subtle.c

# Subtle
compile_and_run "subtle" src/crypto/subtle/subtle.c

# HKDF (depends on subtle)
compile_and_run "hkdf" src/crypto/hkdf/hkdf.c src/crypto/hmac/hmac.c src/crypto/sha256/sha256.c src/crypto/sha512/sha512.c src/crypto/sha1/sha1.c src/crypto/md5/md5.c src/crypto/subtle/subtle.c

# PBKDF2 (depends on subtle)
compile_and_run "pbkdf2" src/crypto/pbkdf2/pbkdf2.c src/crypto/hmac/hmac.c src/crypto/sha256/sha256.c src/crypto/sha512/sha512.c src/crypto/sha1/sha1.c src/crypto/md5/md5.c src/crypto/subtle/subtle.c

# Crypto rand
compile_and_run "crypto_rand" src/crypto/rand/rand.c

# CMP
compile_and_run "cmp" src/cmp/cmp.c

# Bytes
compile_and_run "bytes" src/bytes/bytes.c

# Errors
compile_and_run "errors" src/errors/errors.c

# HTML
compile_and_run "html" src/html/html.c

# FMT
compile_and_run "fmt" src/fmt/fmt.c

# IO
compile_and_run "io" src/io/io.c

# Bufio
compile_and_run "bufio" src/bufio/bufio.c src/io/io.c

# Flag
compile_and_run "flag" src/flag/flag.c

# Log
compile_and_run "log" src/log/log.c

# Slog
compile_and_run "slog" src/log/slog/slog.c

# Time
compile_and_run "time" src/time/time.c

# UUID
compile_and_run "uuid" src/uuid/uuid.c

# Regexp
compile_and_run "regexp" src/regexp/regexp.c

# MIME
compile_and_run "mime" src/mime/mime.c

# Cmplx
compile_and_run "cmplx" src/math/cmplx/cmplx.c $MATH_SRCS

# Big
compile_and_run "big" src/math/big/big.c

# Container heap
compile_and_run "heap" src/container/heap/heap.c

# Container list
compile_and_run "list" src/container/list/list.c

# Container ring
compile_and_run "ring" src/container/ring/ring.c

# Maphash
compile_and_run "maphash" src/hash/maphash/maphash.c

# Compress LZW
compile_and_run "lzw" src/compress/lzw/lzw.c

# Compress flate
compile_and_run "flate" src/compress/flate/flate.c

# Compress gzip
compile_and_run "gzip" src/compress/gzip/gzip.c src/compress/flate/flate.c src/hash/crc32/crc32.c

# Compress zlib
compile_and_run "zlib" src/compress/zlib/zlib.c src/compress/flate/flate.c src/hash/adler32/adler32.c

# JSON
compile_and_run "json" src/encoding/json/json.c

# CSV
compile_and_run "csv" src/encoding/csv/csv.c

# XML
compile_and_run "xml" src/encoding/xml/xml.c

# ASN1
compile_and_run "asn1" src/encoding/asn1/asn1.c

# Filepath
compile_and_run "filepath" src/path/filepath/filepath.c

# Tar
compile_and_run "tar" src/archive/tar/tar.c

# Zip
compile_and_run "zip" src/archive/zip/zip.c src/hash/crc32/crc32.c

# Template
compile_and_run "template" src/text/template/template.c

# Scanner
compile_and_run "scanner" src/text/scanner/scanner.c

# Tabwriter
compile_and_run "tabwriter" src/text/tabwriter/tabwriter.c

# Suffixarray
compile_and_run "suffixarray" src/index/suffixarray/suffixarray.c

# Sync
compile_and_run "sync" src/sync/sync.c

# Atomic
compile_and_run "atomic" src/sync/atomic/atomic.c

# URL
compile_and_run "url" src/net/url/url.c

# Color
compile_and_run "color" src/image/color/color.c

# Elliptic (needs big)
compile_and_run "elliptic" src/crypto/elliptic/elliptic.c src/math/big/big.c

# RSA (needs big + crypto)
compile_and_run "rsa" src/crypto/rsa/rsa.c src/math/big/big.c src/crypto/rand/rand.c src/crypto/sha256/sha256.c

# ECDSA (needs elliptic + big + crypto)
compile_and_run "ecdsa" src/crypto/ecdsa/ecdsa.c src/crypto/elliptic/elliptic.c src/math/big/big.c src/crypto/rand/rand.c src/crypto/sha256/sha256.c

# DSA (needs big + crypto + sha256)
compile_and_run "dsa" src/crypto/dsa/dsa.c src/math/big/big.c src/crypto/rand/rand.c src/crypto/sha256/sha256.c

# Ed25519 (needs sha512 + big)
compile_and_run "ed25519" src/crypto/ed25519/ed25519.c src/crypto/sha512/sha512.c src/crypto/rand/rand.c src/math/big/big.c

# ECDH (needs elliptic + big + crypto)
compile_and_run "ecdh" src/crypto/ecdh/ecdh.c src/crypto/elliptic/elliptic.c src/math/big/big.c src/crypto/rand/rand.c src/crypto/sha256/sha256.c

# Compress bzip2
compile_and_run "bzip2" src/compress/bzip2/bzip2.c

# Context
compile_and_run "context" src/context/context.c

# OS
compile_and_run "os" src/os/os.c

# OS exec
compile_and_run "exec" src/os/exec/exec.c

# OS signal
compile_and_run "signal" src/os/signal/signal.c

# OS user
compile_and_run "user" src/os/user/user.c

# Maps
compile_and_run "maps" src/maps/maps.c

# Slices
compile_and_run "slices" src/slices/slices.c

# Image
compile_and_run "image" src/image/image/image.c src/image/color/color.c

# Image draw
compile_and_run "draw" src/image/draw/draw.c src/image/image/image.c src/image/color/color.c

# Image PNG
compile_and_run "png" src/image/png/png.c src/image/image/image.c src/image/color/color.c src/compress/flate/flate.c src/hash/crc32/crc32.c

# Image JPEG
compile_and_run "jpeg" src/image/jpeg/jpeg.c src/image/image/image.c src/image/color/color.c

# Image GIF
compile_and_run "gif" src/image/gif/gif.c src/image/image/image.c src/image/color/color.c src/compress/lzw/lzw.c

# Net mail
compile_and_run "mail" src/net/mail/mail.c

# Net netip
compile_and_run "netip" src/net/netip/netip.c

# Net textproto
compile_and_run "textproto" src/net/textproto/textproto.c

# HTML template
compile_and_run "html_template" src/html/template/template.c src/html/html.c

# MIME quotedprintable
compile_and_run "quotedprintable" src/mime/quotedprintable/quotedprintable.c

# MIME multipart
compile_and_run "multipart" src/mime/multipart/multipart.c

# IO FS
compile_and_run "fs" src/io/fs/fs.c

# Log syslog
compile_and_run "syslog" src/log/syslog/syslog.c

echo ""
echo "============================================"
echo "  TOTAL: $TOTAL  PASS: $PASS  FAIL: $FAIL  SKIP: $SKIP"
if [ -n "$FAIL_LIST" ]; then
    echo "  Failed tests:$FAIL_LIST"
fi
echo "============================================"
