# Embedded C++ SDK sources and licenses

This directory supplies the immutable header inputs for NeverC's built-in C++
frontend. The distribution is
`neverc-embedded-clang20.1.8-libcxx200100-macos15.5`. It contains the 209 header
files admitted by the former
`clang20.1.8-libcxx200100-macos15.5` catalog: 127 libc++ headers, one Clang resource
header, and 81 Darwin platform headers. Their contents, including copyright and
license notices, are preserved byte for byte. This is a fixed input set for the
supported translation profiles, not a complete Apple SDK.

[catalog.json](catalog.json) pins the distribution's contents.
[provenance.json](provenance.json) lists each header's catalog root, relative path,
SHA-256, license basis, and public upstream source reference. Upstream references
identify the source or generator and its license provenance; they do not claim
that every exported SDK header is identical to the referenced source revision.
Generated availability headers, configured libc++ headers, and exported kernel
headers can differ from their upstream templates. The catalog hashes identify
the exact header bytes supplied here.

## License inventory

| Header group | Files | Applicable notices |
| --- | ---: | --- |
| libc++ and Clang resource headers | 128 | Apache-2.0 with LLVM exceptions |
| Darwin headers with an APSL notice | 63 | APSL-2.0 |
| Darwin headers with APSL and Berkeley notices | 12 | APSL-2.0 and BSD-4-Clause |
| `arm/endian.h`, `arm/types.h` | 2 | BSD-4-Clause |
| `math.h` | 1 | Original APSL-1.1 notice; APSL-2.0 selected as described below |
| Headers with a project-level license basis | 3 | APSL-2.0 from the public upstream distribution, as described below |

The license texts are copied from these upstream sources:

- [licenses/LLVM.txt](licenses/LLVM.txt): [LLVM 20.1.8 libc++ license](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.8/libcxx/LICENSE.TXT), including the LLVM exceptions and legacy notices.
- [licenses/APSL-1.1.txt](licenses/APSL-1.1.txt): [Apple's XNU 123.5 license](https://github.com/apple-oss-distributions/xnu/blob/xnu-123.5/APPLE_LICENSE).
- [licenses/APSL-2.0.txt](licenses/APSL-2.0.txt): [Apple's XNU license](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/APPLE_LICENSE).
- [licenses/BSD-4-NOTICES.txt](licenses/BSD-4-NOTICES.txt): the original Berkeley redistribution comments from all 14 applicable headers, grouped by catalog path without replacing the original authors or terms.

This product includes software developed by the University of California,
Berkeley and its contributors.

The original header notices, including the Apple OS reference notices and
Berkeley attribution conditions, remain applicable. The license texts in this
directory supplement those notices; they do not replace them.

## Files with project-level license provenance

These three SDK headers contain copyright notices without a separate per-file
license grant. Their license attribution follows their publication in Apple's
official source distributions under the respective root `APPLE_LICENSE`, rather
than treating the copyright notice itself as a grant:

| Catalog path | Public source | Correspondence |
| --- | --- | --- |
| `platform/usr/include/arm/_types.h` | [XNU `bsd/arm/_types.h`](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/arm/_types.h) | SDK export omits the upstream `KERNEL`-only conditional branch. |
| `platform/usr/include/arm/signal.h` | [XNU `bsd/arm/signal.h`](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/arm/signal.h) | Identical bytes. |
| `platform/usr/include/stdint.h` | [Libc `include/stdint.h`](https://github.com/apple-oss-distributions/Libc/blob/71bbe350ab79eef58113991d817ccc6165061a64/include/stdint.h) | Identical bytes. |

The corresponding project licenses are
[XNU's APSL-2.0](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/APPLE_LICENSE)
and [Libc's APSL-2.0](https://github.com/apple-oss-distributions/Libc/blob/71bbe350ab79eef58113991d817ccc6165061a64/APPLE_LICENSE).
The original Apple and NeXT notices are retained.

## License selection for `math.h`

`platform/usr/include/math.h` retains its original APSL-1.1 notice. For this
distribution, NeverC elects to use that covered code under APSL-2.0, as permitted
by section 7 of APSL-1.1. Both license versions are included above so that the
original notice and this selection can be traced to Apple's published terms.
The selection does not change the header's bytes or its declarations.

APSL-2.0 sections 2.1 through 2.3 require preserving notices, supplying the
license, and providing the required source-availability information with
executable distributions. Section 4 permits combination with other code in a
larger work. Keep the original header files and the embedding build scripts in
the corresponding source release. Binary packages must include these notices
and identify how to obtain their corresponding source release; a generated
resource array alone is not a substitute for that source distribution.

## Platform metadata

The original Apple `SDKSettings.json` is not redistributed. The embedded
`platform/SDKSettings.json` is a NeverC-authored declaration of two version facts:
`Version` is `15.5` and `MaximumDeploymentTarget` is `15.5.99`. Its own hash is in
the new catalog. The former metadata hash appears only as migration provenance
in `provenance.json`; the header license inventory does not claim to license the
former SDK configuration file.
