//===-- llvm/TargetParser/Triple.h - Target triple helper class--*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_TRIPLE_H
#define LLVM_TARGETPARSER_TRIPLE_H

#include "llvm/ADT/Twine.h"
#include "llvm/Support/VersionTuple.h"

// Some system headers or GCC predefined macros conflict with identifiers in
// this file.  Undefine them here.
#undef NetBSD
#undef mips
#undef sparc

namespace llvm {

/// Triple - Helper class for working with autoconf configuration names. For
/// historical reasons, we also call these 'triples' (they used to contain
/// exactly three fields).
///
/// Configuration names are strings in the canonical form:
///   ARCHITECTURE-VENDOR-OPERATING_SYSTEM
/// or
///   ARCHITECTURE-VENDOR-OPERATING_SYSTEM-ENVIRONMENT
///
/// This class is used for clients which want to support arbitrary
/// configuration names, but also want to implement certain special
/// behavior for particular configurations. This class isolates the mapping
/// from the components of the configuration name to well known IDs.
///
/// At its core the Triple class is designed to be a wrapper for a triple
/// string; the constructor does not change or normalize the triple string.
/// Clients that need to handle the non-canonical triples that users often
/// specify should use the normalize method.
///
/// See autoconf/config.guess for a glimpse into what configuration names
/// look like in practice.
class Triple {
public:
  enum ArchType {
    UnknownArch,

    aarch64, // AArch64 (little endian): aarch64
    x86,     // X86: i[3-9]86
    x86_64,  // X86-64: amd64, x86_64
    LastArchType = x86_64
  };
  enum SubArchType {
    NoSubArch,

    ARMSubArch_v9_5a,
    ARMSubArch_v9_4a,
    ARMSubArch_v9_3a,
    ARMSubArch_v9_2a,
    ARMSubArch_v9_1a,
    ARMSubArch_v9,
    ARMSubArch_v8_9a,
    ARMSubArch_v8_8a,
    ARMSubArch_v8_7a,
    ARMSubArch_v8_6a,
    ARMSubArch_v8_5a,
    ARMSubArch_v8_4a,
    ARMSubArch_v8_3a,
    ARMSubArch_v8_2a,
    ARMSubArch_v8_1a,
    ARMSubArch_v8,
    ARMSubArch_v8r,
  };
  enum VendorType {
    UnknownVendor,

    Apple,
    PC,
    LastVendorType = PC
  };
  enum OSType {
    UnknownOS,

    Darwin,
    IOS,
    Linux,
    MacOSX,
    UEFI,
    Win32,
    LastOSType = Win32
  };
  enum EnvironmentType {
    UnknownEnvironment,

    GNU,
    GNUABIN32,
    GNUABI64,
    GNUEABI,
    GNUEABIHF,
    GNUF32,
    GNUF64,
    GNUSF,
    GNUX32,
    CODE16,
    EABI,
    EABIHF,
    Android,
    Musl,
    MuslEABI,
    MuslEABIHF,
    MuslX32,

    MSVC,
    Itanium,
    Cygnus,
    Simulator, // Simulator variants of other systems, e.g., Apple's iOS

    OpenHOS,

    LastEnvironmentType = OpenHOS
  };
  enum ObjectFormatType {
    UnknownObjectFormat,

    COFF,
    ELF,
    MachO,
  };

private:
  std::string Data;

  /// The parsed arch type.
  ArchType Arch{};

  /// The parsed subarchitecture type.
  SubArchType SubArch{};

  /// The parsed vendor type.
  VendorType Vendor{};

  /// The parsed OS type.
  OSType OS{};

  /// The parsed Environment type.
  EnvironmentType Environment{};

  /// The object format type.
  ObjectFormatType ObjectFormat{};

public:
  /// @name Constructors
  /// @{

  /// Default constructor is the same as an empty string and leaves all
  /// triple fields unknown.
  Triple() = default;

  explicit Triple(const Twine &Str);
  Triple(const Twine &ArchStr, const Twine &VendorStr, const Twine &OSStr);
  Triple(const Twine &ArchStr, const Twine &VendorStr, const Twine &OSStr,
         const Twine &EnvironmentStr);

  bool operator==(const Triple &Other) const {
    return Arch == Other.Arch && SubArch == Other.SubArch &&
           Vendor == Other.Vendor && OS == Other.OS &&
           Environment == Other.Environment &&
           ObjectFormat == Other.ObjectFormat;
  }

  bool operator!=(const Triple &Other) const { return !(*this == Other); }

  /// @}
  /// @name Normalization
  /// @{

  /// Turn an arbitrary machine specification into the canonical triple form (or
  /// something sensible that the Triple class understands if nothing better can
  /// reasonably be done).  In particular, it handles the common case in which
  /// otherwise valid components are in the wrong order.
  static std::string normalize(StringRef Str);

  /// Return the normalized form of this triple's string.
  std::string normalize() const { return normalize(Data); }

  /// @}
  /// @name Typed Component Access
  /// @{

  /// Get the parsed architecture type of this triple.
  ArchType getArch() const { return Arch; }

  /// get the parsed subarchitecture type for this triple.
  SubArchType getSubArch() const { return SubArch; }

  /// Get the parsed vendor type of this triple.
  VendorType getVendor() const { return Vendor; }

  /// Get the parsed operating system type of this triple.
  OSType getOS() const { return OS; }

  /// Does this triple have the optional environment (fourth) component?
  bool hasEnvironment() const { return getEnvironmentName() != ""; }

  /// Get the parsed environment type of this triple.
  EnvironmentType getEnvironment() const { return Environment; }

  /// Parse the version number from the OS name component of the
  /// triple, if present.
  ///
  /// For example, "fooos1.2.3" would return (1, 2, 3).
  VersionTuple getEnvironmentVersion() const;

  /// Get the object format for this triple.
  ObjectFormatType getObjectFormat() const { return ObjectFormat; }

  /// Parse the version number from the OS name component of the triple, if
  /// present.
  ///
  /// For example, "fooos1.2.3" would return (1, 2, 3).
  VersionTuple getOSVersion() const;

  /// Return just the major version number, this is specialized because it is a
  /// common query.
  unsigned getOSMajorVersion() const { return getOSVersion().getMajor(); }

  /// Parse the version number as with getOSVersion and then translate generic
  /// "darwin" versions to the corresponding OS X versions.  This may also be
  /// called with IOS triples but the OS X version number is just set to a
  /// constant 10.4.0 in that case.  Returns true if successful.
  bool getMacOSXVersion(VersionTuple &Version) const;

  /// Parse the version number as with getOSVersion.  This should only be called
  /// with IOS or generic triples.
  VersionTuple getiOSVersion() const;

  /// @}
  /// @name Direct Component Access
  /// @{

  const std::string &str() const { return Data; }

  const std::string &getTriple() const { return Data; }

  /// Get the architecture (first) component of the triple.
  StringRef getArchName() const;

  /// Get the vendor (second) component of the triple.
  StringRef getVendorName() const;

  /// Get the operating system (third) component of the triple.
  StringRef getOSName() const;

  /// Get the optional environment (fourth) component of the triple, or "" if
  /// empty.
  StringRef getEnvironmentName() const;

  /// Get the operating system and optional environment components as a single
  /// string (separated by a '-' if the environment component is present).
  StringRef getOSAndEnvironmentName() const;

  /// @}
  /// @name Convenience Predicates
  /// @{

  /// Test whether the architecture is 64-bit
  ///
  /// Note that this tests for 64-bit pointer width, and nothing else. Note
  /// that we intentionally expose only three predicates, 64-bit, 32-bit, and
  /// 16-bit. The inner details of pointer width for particular architectures
  /// is not summed up in the triple, and so only a coarse grained predicate
  /// system is provided.
  bool isArch64Bit() const;

  /// Helper function for doing comparisons against version numbers included in
  /// the target triple.
  bool isOSVersionLT(unsigned Major, unsigned Minor = 0,
                     unsigned Micro = 0) const {
    if (Minor == 0) {
      return getOSVersion() < VersionTuple(Major);
    }
    if (Micro == 0) {
      return getOSVersion() < VersionTuple(Major, Minor);
    }
    return getOSVersion() < VersionTuple(Major, Minor, Micro);
  }

  bool isOSVersionLT(const Triple &Other) const {
    return getOSVersion() < Other.getOSVersion();
  }

  /// Comparison function for checking OS X version compatibility, which handles
  /// supporting skewed version numbering schemes used by the "darwin" triples.
  bool isMacOSXVersionLT(unsigned Major, unsigned Minor = 0,
                         unsigned Micro = 0) const;

  /// Is this a Mac OS X triple. For legacy reasons, we support both "darwin"
  /// and "osx" as OS X triples.
  bool isMacOSX() const {
    return getOS() == Triple::Darwin || getOS() == Triple::MacOSX;
  }

  /// Is this an iOS triple.
  bool isiOS() const { return getOS() == Triple::IOS; }

  /// Is this a "Darwin" OS (macOS or iOS).
  bool isOSDarwin() const {
    return isMacOSX() || isiOS();
  }

  bool isSimulatorEnvironment() const {
    return getEnvironment() == Triple::Simulator;
  }

  /// Returns true for targets that run on a macOS machine.
  bool isTargetMachineMac() const {
    return isMacOSX() || (isOSDarwin() && isSimulatorEnvironment());
  }

  bool isOSUnknown() const { return getOS() == Triple::UnknownOS; }

  bool isGNUEnvironment() const {
    EnvironmentType Env = getEnvironment();
    return Env == Triple::GNU || Env == Triple::GNUABIN32 ||
           Env == Triple::GNUABI64 || Env == Triple::GNUEABI ||
           Env == Triple::GNUEABIHF || Env == Triple::GNUF32 ||
           Env == Triple::GNUF64 || Env == Triple::GNUSF ||
           Env == Triple::GNUX32;
  }

  /// Tests whether the OS is UEFI.
  bool isUEFI() const { return getOS() == Triple::UEFI; }

  /// Tests whether the OS is Windows.
  bool isOSWindows() const { return getOS() == Triple::Win32; }

  /// Checks if the environment is MSVC.
  bool isKnownWindowsMSVCEnvironment() const {
    return isOSWindows() && getEnvironment() == Triple::MSVC;
  }

  /// Checks if the environment could be MSVC.
  bool isWindowsMSVCEnvironment() const {
    return isKnownWindowsMSVCEnvironment() ||
           (isOSWindows() && getEnvironment() == Triple::UnknownEnvironment);
  }

  bool isWindowsItaniumEnvironment() const {
    return isOSWindows() && getEnvironment() == Triple::Itanium;
  }

  bool isWindowsCygwinEnvironment() const {
    return isOSWindows() && getEnvironment() == Triple::Cygnus;
  }

  bool isWindowsGNUEnvironment() const {
    return isOSWindows() && getEnvironment() == Triple::GNU;
  }

  /// Tests for either Cygwin or MinGW OS
  bool isOSCygMing() const {
    return isWindowsCygwinEnvironment() || isWindowsGNUEnvironment();
  }

  /// Is this a "Windows" OS targeting a "MSVCRT.dll" environment.
  bool isOSMSVCRT() const {
    return isWindowsMSVCEnvironment() || isWindowsGNUEnvironment() ||
           isWindowsItaniumEnvironment();
  }

  /// Tests whether the OS is Linux.
  bool isOSLinux() const { return getOS() == Triple::Linux; }

  /// Tests whether the OS uses glibc.
  bool isOSGlibc() const {
    return getOS() == Triple::Linux && !isAndroid();
  }

  /// Tests whether the OS uses the ELF binary format.
  bool isOSBinFormatELF() const { return getObjectFormat() == Triple::ELF; }

  /// Tests whether the OS uses the COFF binary format.
  bool isOSBinFormatCOFF() const { return getObjectFormat() == Triple::COFF; }

  /// Tests whether the environment is MachO.
  bool isOSBinFormatMachO() const { return getObjectFormat() == Triple::MachO; }

  /// Tests whether the target is Android
  bool isAndroid() const { return getEnvironment() == Triple::Android; }

  bool isAndroidVersionLT(unsigned Major) const {
    assert(isAndroid() && "Not an Android triple!");

    VersionTuple Version = getEnvironmentVersion();

    // 64-bit targets did not exist before API level 21 (Lollipop).
    if (isArch64Bit() && Version.getMajor() < 21)
      return VersionTuple(21) < VersionTuple(Major);

    return Version < VersionTuple(Major);
  }

  /// Tests whether the environment is musl-libc
  bool isMusl() const {
    return getEnvironment() == Triple::Musl ||
           getEnvironment() == Triple::MuslEABI ||
           getEnvironment() == Triple::MuslEABIHF ||
           getEnvironment() == Triple::MuslX32;
  }

  /// Tests whether the target is AArch64 (little endian only).
  bool isAArch64() const { return getArch() == Triple::aarch64; }

  /// Tests whether the target is x86_64.
  bool isX86() const { return getArch() == Triple::x86_64; }

  /// Tests whether the target is X32.
  bool isX32() const {
    EnvironmentType Env = getEnvironment();
    return Env == Triple::GNUX32 || Env == Triple::MuslX32;
  }

  /// Tests whether the target supports comdat
  bool supportsCOMDAT() const { return !isOSBinFormatMachO(); }

  /// Tests whether the target uses emulated TLS as default.
  ///
  /// Note: Android API level 29 (10) introduced ELF TLS.
  bool hasDefaultEmulatedTLS() const {
    return (isAndroid() && isAndroidVersionLT(29)) ||
           isWindowsCygwinEnvironment();
  }

  /// Tests if the environment supports dllimport/export annotations.
  bool hasDLLImportExport() const { return isOSWindows(); }

  /// @}
  /// @name Mutators
  /// @{

  /// Set the architecture (first) component of the triple to a known type.
  void setArch(ArchType Kind, SubArchType SubArch = NoSubArch);

  /// Set the vendor (second) component of the triple to a known type.
  void setVendor(VendorType Kind);

  /// Set the operating system (third) component of the triple to a known type.
  void setOS(OSType Kind);

  /// Set the environment (fourth) component of the triple to a known type.
  void setEnvironment(EnvironmentType Kind);

  /// Set the object file format.
  void setObjectFormat(ObjectFormatType Kind);

  /// Set all components to the new triple \p Str.
  void setTriple(const Twine &Str);

  /// Set the architecture (first) component of the triple by name.
  void setArchName(StringRef Str);

  /// Set the vendor (second) component of the triple by name.
  void setVendorName(StringRef Str);

  /// Set the operating system (third) component of the triple by name.
  void setOSName(StringRef Str);

  /// Set the optional environment (fourth) component of the triple by name.
  void setEnvironmentName(StringRef Str);

  /// Set the operating system and optional environment components with a single
  /// string.
  void setOSAndEnvironmentName(StringRef Str);

  /// @}
  /// @name Helpers to build variants of a particular triple.
  /// @{

  /// Form a triple with a 32-bit variant of the current architecture.
  ///
  /// This can be used to move across "families" of architectures where useful.
  ///
  /// Form a triple with a 64-bit variant of the current architecture.
  ///
  /// This can be used to move across "families" of architectures where useful.
  ///
  /// \returns A new triple with a 64-bit architecture or an unknown
  ///          architecture if no such variant can be found.
  llvm::Triple get64BitArchVariant() const;

  /// Form a triple with a big endian variant of the current architecture.
  ///
  /// This can be used to move across "families" of architectures where useful.
  ///
  /// \returns A new triple with a big endian architecture or an unknown
  ///          architecture if no such variant can be found.
  llvm::Triple getBigEndianArchVariant() const;

  /// Form a triple with a little endian variant of the current architecture.
  ///
  /// This can be used to move across "families" of architectures where useful.
  ///
  /// \returns A new triple with a little endian architecture or an unknown
  ///          architecture if no such variant can be found.
  llvm::Triple getLittleEndianArchVariant() const;

  /// Tests whether the target triple is little endian.
  ///
  /// \returns true if the triple is little endian, false otherwise.
  bool isLittleEndian() const;

  /// Test whether target triples are compatible.
  bool isCompatibleWith(const Triple &Other) const;

  /// Merge target triples.
  std::string merge(const Triple &Other) const;

  /// Some platforms have different minimum supported OS versions that
  /// varies by the architecture specified in the triple. This function
  /// returns the minimum supported OS version for this triple if one an exists,
  /// or an invalid version tuple if this triple doesn't have one.
  VersionTuple getMinimumSupportedOSVersion() const;

  /// @}
  /// @name Static helpers for IDs.
  /// @{

  /// Get the canonical name for the \p Kind architecture.
  static StringRef getArchTypeName(ArchType Kind);

  /// Get the architecture name based on \p Kind and \p SubArch.
  static StringRef getArchName(ArchType Kind, SubArchType SubArch = NoSubArch);

  /// Get the "prefix" canonical name for the \p Kind architecture. This is the
  /// prefix used by the architecture specific builtins, and is suitable for
  /// passing to \see Intrinsic::getIntrinsicForClangBuiltin().
  ///
  /// \return - The architecture prefix, or 0 if none is defined.
  static StringRef getArchTypePrefix(ArchType Kind);

  /// Get the canonical name for the \p Kind vendor.
  static StringRef getVendorTypeName(VendorType Kind);

  /// Get the canonical name for the \p Kind operating system.
  static StringRef getOSTypeName(OSType Kind);

  /// Get the canonical name for the \p Kind environment.
  static StringRef getEnvironmentTypeName(EnvironmentType Kind);

  /// Get the name for the \p Object format.
  static StringRef getObjectFormatTypeName(ObjectFormatType ObjectFormat);

  /// @}
  /// @name Static helpers for converting alternate architecture names.
  /// @{

  /// The canonical type for the given LLVM architecture name (e.g., "x86").
  static ArchType getArchTypeForLLVMName(StringRef Str);

  /// @}

  /// Returns a canonicalized OS version number for the specified OS.
  static VersionTuple getCanonicalVersionForOS(OSType OSKind,
                                               const VersionTuple &Version);
};

} // namespace llvm

#endif
