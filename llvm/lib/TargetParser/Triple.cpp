//===--- Triple.cpp - Target triple helper class --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/TargetParser/Triple.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/TargetParser/Host.h"
#include <cassert>
#include <cstring>
using namespace llvm;

StringRef Triple::getArchTypeName(ArchType Kind) {
  switch (Kind) {
  case UnknownArch:
    return "unknown";

  case aarch64:
    return "aarch64";
  case x86:
    return "i386";
  case x86_64:
    return "x86_64";
  }

  llvm_unreachable("Invalid ArchType!");
}

StringRef Triple::getArchName(ArchType Kind, SubArchType SubArch) {
  return getArchTypeName(Kind);
}

StringRef Triple::getArchTypePrefix(ArchType Kind) {
  switch (Kind) {
  default:
    return StringRef();
  case aarch64:
    return "aarch64";
  case x86:
  case x86_64:
    return "x86";
  }
}

StringRef Triple::getVendorTypeName(VendorType Kind) {
  switch (Kind) {
  case UnknownVendor:
    return "unknown";
  case Apple:
    return "apple";
  case PC:
    return "pc";
  }

  llvm_unreachable("Invalid VendorType!");
}

StringRef Triple::getOSTypeName(OSType Kind) {
  switch (Kind) {
  case UnknownOS:
    return "unknown";

  case Darwin:
    return "darwin";
  case IOS:
    return "ios";
  case Linux:
    return "linux";
  case MacOSX:
    return "macosx";
  case UEFI:
    return "uefi";
  case Win32:
    return "windows";
  }

  llvm_unreachable("Invalid OSType");
}

StringRef Triple::getEnvironmentTypeName(EnvironmentType Kind) {
  switch (Kind) {
  case UnknownEnvironment:
    return "unknown";
  case Android:
    return "android";
  case CODE16:
    return "code16";
  case Cygnus:
    return "cygnus";
  case EABI:
    return "eabi";
  case EABIHF:
    return "eabihf";
  case GNU:
    return "gnu";
  case GNUABI64:
    return "gnuabi64";
  case GNUABIN32:
    return "gnuabin32";
  case GNUEABI:
    return "gnueabi";
  case GNUEABIHF:
    return "gnueabihf";
  case GNUF32:
    return "gnuf32";
  case GNUF64:
    return "gnuf64";
  case GNUSF:
    return "gnusf";
  case GNUX32:
    return "gnux32";
  case Itanium:
    return "itanium";
  case MSVC:
    return "msvc";
  case Musl:
    return "musl";
  case MuslEABI:
    return "musleabi";
  case MuslEABIHF:
    return "musleabihf";
  case MuslX32:
    return "muslx32";
  case Simulator:
    return "simulator";
  case OpenHOS:
    return "ohos";
  }

  llvm_unreachable("Invalid EnvironmentType!");
}

StringRef Triple::getObjectFormatTypeName(ObjectFormatType Kind) {
  switch (Kind) {
  case UnknownObjectFormat:
    return "";
  case COFF:
    return "coff";
  case ELF:
    return "elf";
  case MachO:
    return "macho";
  }
  llvm_unreachable("Invalid ObjectFormatType!");
}

Triple::ArchType Triple::getArchTypeForLLVMName(StringRef Name) {
  return StringSwitch<Triple::ArchType>(Name)
      .Case("aarch64", aarch64)
      .Case("arm64", aarch64)
      .Case("x86", x86)
      .Case("i386", x86)
      .Case("x86-64", x86_64)
      .Default(UnknownArch);
}

static Triple::ArchType parseArch(StringRef ArchName) {
  return StringSwitch<Triple::ArchType>(ArchName)
      .Cases("i386", "i486", "i586", "i686", Triple::x86)
      .Cases("amd64", "x86_64", Triple::x86_64)
      .Case("aarch64", Triple::aarch64)
      .Case("arm64", Triple::aarch64)
      .Default(Triple::UnknownArch);
}

static Triple::VendorType parseVendor(StringRef VendorName) {
  return StringSwitch<Triple::VendorType>(VendorName)
      .Case("apple", Triple::Apple)
      .Case("pc", Triple::PC)
      .Default(Triple::UnknownVendor);
}

static Triple::OSType parseOS(StringRef OSName) {
  return StringSwitch<Triple::OSType>(OSName)
      .StartsWith("darwin", Triple::Darwin)
      .StartsWith("ios", Triple::IOS)
      .StartsWith("linux", Triple::Linux)
      .StartsWith("macos", Triple::MacOSX)
      .StartsWith("uefi", Triple::UEFI)
      .StartsWith("win32", Triple::Win32)
      .StartsWith("windows", Triple::Win32)
      .Default(Triple::UnknownOS);
}

static Triple::EnvironmentType parseEnvironment(StringRef EnvironmentName) {
  return StringSwitch<Triple::EnvironmentType>(EnvironmentName)
      .StartsWith("eabihf", Triple::EABIHF)
      .StartsWith("eabi", Triple::EABI)
      .StartsWith("gnuabin32", Triple::GNUABIN32)
      .StartsWith("gnuabi64", Triple::GNUABI64)
      .StartsWith("gnueabihf", Triple::GNUEABIHF)
      .StartsWith("gnueabi", Triple::GNUEABI)
      .StartsWith("gnuf32", Triple::GNUF32)
      .StartsWith("gnuf64", Triple::GNUF64)
      .StartsWith("gnusf", Triple::GNUSF)
      .StartsWith("gnux32", Triple::GNUX32)
      .StartsWith("code16", Triple::CODE16)
      .StartsWith("gnu", Triple::GNU)
      .StartsWith("android", Triple::Android)
      .StartsWith("musleabihf", Triple::MuslEABIHF)
      .StartsWith("musleabi", Triple::MuslEABI)
      .StartsWith("muslx32", Triple::MuslX32)
      .StartsWith("musl", Triple::Musl)
      .StartsWith("msvc", Triple::MSVC)
      .StartsWith("itanium", Triple::Itanium)
      .StartsWith("cygnus", Triple::Cygnus)
      .StartsWith("simulator", Triple::Simulator)
      .StartsWith("ohos", Triple::OpenHOS)
      .Default(Triple::UnknownEnvironment);
}

static Triple::ObjectFormatType parseFormat(StringRef EnvironmentName) {
  return StringSwitch<Triple::ObjectFormatType>(EnvironmentName)
      .EndsWith("coff", Triple::COFF)
      .EndsWith("elf", Triple::ELF)
      .EndsWith("macho", Triple::MachO)
      .Default(Triple::UnknownObjectFormat);
}

static Triple::SubArchType parseSubArch(StringRef SubArchName) {
  (void)SubArchName;
  return Triple::NoSubArch;
}

static Triple::ObjectFormatType getDefaultFormat(const Triple &T) {
  switch (T.getArch()) {
  case Triple::UnknownArch:
  case Triple::aarch64:
  case Triple::x86:
  case Triple::x86_64:
    switch (T.getOS()) {
    case Triple::Win32:
    case Triple::UEFI:
      return Triple::COFF;
    default:
      return T.isOSDarwin() ? Triple::MachO : Triple::ELF;
    }
  default:
    // neverc only supports x86/x86_64/aarch64 targets.
    // Unsupported architectures are rejected by the driver; keep parser
    // behavior non-crashing here by returning an unknown format.
    return Triple::UnknownObjectFormat;
  }
}

/// Construct a triple from the string representation provided.
///
/// This stores the string representation and parses the various pieces into
/// enum members.
Triple::Triple(const Twine &Str)
    : Data(Str.str()), Arch(UnknownArch), SubArch(NoSubArch),
      Vendor(UnknownVendor), OS(UnknownOS), Environment(UnknownEnvironment),
      ObjectFormat(UnknownObjectFormat) {
  // Do minimal parsing by hand here.
  SmallVector<StringRef, 4> Components;
  StringRef(Data).split(Components, '-', /*MaxSplit*/ 3);
  if (Components.size() > 0) {
    Arch = parseArch(Components[0]);
    SubArch = parseSubArch(Components[0]);
    if (Components.size() > 1) {
      Vendor = parseVendor(Components[1]);
      if (Components.size() > 2) {
        OS = parseOS(Components[2]);
        if (Components.size() > 3) {
          Environment = parseEnvironment(Components[3]);
          ObjectFormat = parseFormat(Components[3]);
        }
      }
    } else {
      Environment = UnknownEnvironment;
    }
  }
  if (ObjectFormat == UnknownObjectFormat)
    ObjectFormat = getDefaultFormat(*this);
}

/// Construct a triple from string representations of the architecture,
/// vendor, and OS.
///
/// This joins each argument into a canonical string representation and parses
/// them into enum members. It leaves the environment unknown and omits it from
/// the string representation.
Triple::Triple(const Twine &ArchStr, const Twine &VendorStr, const Twine &OSStr)
    : Data((ArchStr + Twine('-') + VendorStr + Twine('-') + OSStr).str()),
      Arch(parseArch(ArchStr.str())), SubArch(parseSubArch(ArchStr.str())),
      Vendor(parseVendor(VendorStr.str())), OS(parseOS(OSStr.str())),
      Environment(), ObjectFormat(Triple::UnknownObjectFormat) {
  ObjectFormat = getDefaultFormat(*this);
}

/// Construct a triple from string representations of the architecture,
/// vendor, OS, and environment.
///
/// This joins each argument into a canonical string representation and parses
/// them into enum members.
Triple::Triple(const Twine &ArchStr, const Twine &VendorStr, const Twine &OSStr,
               const Twine &EnvironmentStr)
    : Data((ArchStr + Twine('-') + VendorStr + Twine('-') + OSStr + Twine('-') +
            EnvironmentStr)
               .str()),
      Arch(parseArch(ArchStr.str())), SubArch(parseSubArch(ArchStr.str())),
      Vendor(parseVendor(VendorStr.str())), OS(parseOS(OSStr.str())),
      Environment(parseEnvironment(EnvironmentStr.str())),
      ObjectFormat(parseFormat(EnvironmentStr.str())) {
  if (ObjectFormat == Triple::UnknownObjectFormat)
    ObjectFormat = getDefaultFormat(*this);
}

std::string Triple::normalize(StringRef Str) {
  bool IsMinGW32 = false;
  bool IsCygwin = false;

  // Parse into components.
  SmallVector<StringRef, 4> Components;
  Str.split(Components, '-');

  // If the first component corresponds to a known architecture, preferentially
  // use it for the architecture.  If the second component corresponds to a
  // known vendor, preferentially use it for the vendor, etc.  This avoids silly
  // component movement when a component parses as (eg) both a valid arch and a
  // valid os.
  ArchType Arch = UnknownArch;
  if (Components.size() > 0)
    Arch = parseArch(Components[0]);
  VendorType Vendor = UnknownVendor;
  if (Components.size() > 1)
    Vendor = parseVendor(Components[1]);
  OSType OS = UnknownOS;
  if (Components.size() > 2) {
    OS = parseOS(Components[2]);
    IsCygwin = Components[2].starts_with("cygwin");
    IsMinGW32 = Components[2].starts_with("mingw");
  }
  EnvironmentType Environment = UnknownEnvironment;
  if (Components.size() > 3)
    Environment = parseEnvironment(Components[3]);
  ObjectFormatType ObjectFormat = UnknownObjectFormat;
  if (Components.size() > 4)
    ObjectFormat = parseFormat(Components[4]);

  // Note which components are already in their final position.  These will not
  // be moved.
  bool Found[4];
  Found[0] = Arch != UnknownArch;
  Found[1] = Vendor != UnknownVendor;
  Found[2] = OS != UnknownOS;
  Found[3] = Environment != UnknownEnvironment;

  // If they are not there already, permute the components into their canonical
  // positions by seeing if they parse as a valid architecture, and if so moving
  // the component to the architecture position etc.
  for (unsigned Pos = 0; Pos != std::size(Found); ++Pos) {
    if (Found[Pos])
      continue; // Already in the canonical position.

    for (unsigned Idx = 0; Idx != Components.size(); ++Idx) {
      // Do not reparse any components that already matched.
      if (Idx < std::size(Found) && Found[Idx])
        continue;

      // Does this component parse as valid for the target position?
      bool Valid = false;
      StringRef Comp = Components[Idx];
      switch (Pos) {
      default:
        llvm_unreachable("unexpected component type!");
      case 0:
        Arch = parseArch(Comp);
        Valid = Arch != UnknownArch;
        break;
      case 1:
        Vendor = parseVendor(Comp);
        Valid = Vendor != UnknownVendor;
        break;
      case 2:
        OS = parseOS(Comp);
        IsCygwin = Comp.starts_with("cygwin");
        IsMinGW32 = Comp.starts_with("mingw");
        Valid = OS != UnknownOS || IsCygwin || IsMinGW32;
        break;
      case 3:
        Environment = parseEnvironment(Comp);
        Valid = Environment != UnknownEnvironment;
        if (!Valid) {
          ObjectFormat = parseFormat(Comp);
          Valid = ObjectFormat != UnknownObjectFormat;
        }
        break;
      }
      if (!Valid)
        continue; // Nope, try the next component.

      // Move the component to the target position, pushing any non-fixed
      // components that are in the way to the right.  This tends to give
      // good results in the common cases of a forgotten vendor component
      // or a wrongly positioned environment.
      if (Pos < Idx) {
        // Insert left, pushing the existing components to the right.  For
        // example, a-b-i386 -> i386-a-b when moving i386 to the front.
        StringRef CurrentComponent(""); // The empty component.
        // Replace the component we are moving with an empty component.
        std::swap(CurrentComponent, Components[Idx]);
        // Insert the component being moved at Pos, displacing any existing
        // components to the right.
        for (unsigned i = Pos; !CurrentComponent.empty(); ++i) {
          // Skip over any fixed components.
          while (i < std::size(Found) && Found[i])
            ++i;
          // Place the component at the new position, getting the component
          // that was at this position - it will be moved right.
          std::swap(CurrentComponent, Components[i]);
        }
      } else if (Pos > Idx) {
        // Push right by inserting empty components until the component at Idx
        // reaches the target position Pos.  For example, pc-a -> -pc-a when
        // moving pc to the second position.
        do {
          // Insert one empty component at Idx.
          StringRef CurrentComponent(""); // The empty component.
          for (unsigned i = Idx; i < Components.size();) {
            // Place the component at the new position, getting the component
            // that was at this position - it will be moved right.
            std::swap(CurrentComponent, Components[i]);
            // If it was placed on top of an empty component then we are done.
            if (CurrentComponent.empty())
              break;
            // Advance to the next component, skipping any fixed components.
            while (++i < std::size(Found) && Found[i])
              ;
          }
          // The last component was pushed off the end - append it.
          if (!CurrentComponent.empty())
            Components.push_back(CurrentComponent);

          // Advance Idx to the component's new position.
          while (++Idx < std::size(Found) && Found[Idx])
            ;
        } while (Idx < Pos); // Add more until the final position is reached.
      }
      assert(Pos < Components.size() && Components[Pos] == Comp &&
             "Component moved wrong!");
      Found[Pos] = true;
      break;
    }
  }

  // Replace empty components with "unknown" value.
  for (StringRef &C : Components)
    if (C.empty())
      C = "unknown";

  // Special case logic goes here.  At this point Arch, Vendor and OS have the
  // correct values for the computed components.
  std::string NormalizedEnvironment;
  if (Environment == Triple::Android &&
      Components[3].starts_with("androideabi")) {
    StringRef AndroidVersion = Components[3].drop_front(strlen("androideabi"));
    if (AndroidVersion.empty()) {
      Components[3] = "android";
    } else {
      NormalizedEnvironment = Twine("android", AndroidVersion).str();
      Components[3] = NormalizedEnvironment;
    }
  }

  if (OS == Triple::Win32) {
    Components.resize(4);
    Components[2] = "windows";
    if (Environment == UnknownEnvironment) {
      if (ObjectFormat == UnknownObjectFormat || ObjectFormat == Triple::COFF)
        Components[3] = "msvc";
      else
        Components[3] = getObjectFormatTypeName(ObjectFormat);
    }
  } else if (IsMinGW32) {
    Components.resize(4);
    Components[2] = "windows";
    Components[3] = "gnu";
  } else if (IsCygwin) {
    Components.resize(4);
    Components[2] = "windows";
    Components[3] = "cygnus";
  }
  if (IsMinGW32 || IsCygwin ||
      (OS == Triple::Win32 && Environment != UnknownEnvironment)) {
    if (ObjectFormat != UnknownObjectFormat && ObjectFormat != Triple::COFF) {
      Components.resize(5);
      Components[4] = getObjectFormatTypeName(ObjectFormat);
    }
  }

  // Stick the corrected components back together to form the normalized string.
  return join(Components, "-");
}

StringRef Triple::getArchName() const {
  return StringRef(Data).split('-').first; // Isolate first component
}

StringRef Triple::getVendorName() const {
  StringRef Tmp = StringRef(Data).split('-').second; // Strip first component
  return Tmp.split('-').first;                       // Isolate second component
}

StringRef Triple::getOSName() const {
  StringRef Tmp = StringRef(Data).split('-').second; // Strip first component
  Tmp = Tmp.split('-').second;                       // Strip second component
  return Tmp.split('-').first;                       // Isolate third component
}

StringRef Triple::getEnvironmentName() const {
  StringRef Tmp = StringRef(Data).split('-').second; // Strip first component
  Tmp = Tmp.split('-').second;                       // Strip second component
  return Tmp.split('-').second;                      // Strip third component
}

StringRef Triple::getOSAndEnvironmentName() const {
  StringRef Tmp = StringRef(Data).split('-').second; // Strip first component
  return Tmp.split('-').second;                      // Strip second component
}

static VersionTuple parseVersionFromName(StringRef Name) {
  VersionTuple Version;
  Version.tryParse(Name);
  return Version.withoutBuild();
}

VersionTuple Triple::getEnvironmentVersion() const {
  StringRef EnvironmentName = getEnvironmentName();
  StringRef EnvironmentTypeName = getEnvironmentTypeName(getEnvironment());
  EnvironmentName.consume_front(EnvironmentTypeName);

  return parseVersionFromName(EnvironmentName);
}

VersionTuple Triple::getOSVersion() const {
  StringRef OSName = getOSName();
  // Assume that the OS portion of the triple starts with the canonical name.
  StringRef OSTypeName = getOSTypeName(getOS());
  if (OSName.starts_with(OSTypeName))
    OSName = OSName.substr(OSTypeName.size());
  else if (getOS() == MacOSX)
    OSName.consume_front("macos");

  return parseVersionFromName(OSName);
}

bool Triple::getMacOSXVersion(VersionTuple &Version) const {
  Version = getOSVersion();

  switch (getOS()) {
  default:
    llvm_unreachable("unexpected OS for Darwin triple");
  case Darwin:
    // Default to darwin8, i.e., MacOSX 10.4.
    if (Version.getMajor() == 0)
      Version = VersionTuple(8);
    // Darwin version numbers are skewed from OS X versions.
    if (Version.getMajor() < 4) {
      return false;
    }
    if (Version.getMajor() <= 19) {
      Version = VersionTuple(10, Version.getMajor() - 4);
    } else {
      // darwin20+ corresponds to macOS 11+.
      Version = VersionTuple(11 + Version.getMajor() - 20);
    }
    break;
  case MacOSX:
    // Default to 10.4.
    if (Version.getMajor() == 0) {
      Version = VersionTuple(10, 4);
    } else if (Version.getMajor() < 10) {
      return false;
    }
    break;
  case IOS:
    // Ignore the version from the triple.  This is only handled because the
    // the clang driver combines OS X and IOS support into a common Darwin
    // toolchain that wants to know the OS X version number even when targeting
    // IOS.
    Version = VersionTuple(10, 4);
    break;
  }
  return true;
}

VersionTuple Triple::getiOSVersion() const {
  switch (getOS()) {
  default:
    llvm_unreachable("unexpected OS for Darwin triple");
  case Darwin:
  case MacOSX:
    // Ignore the version from the triple.  This is only handled because the
    // the clang driver combines OS X and IOS support into a common Darwin
    // toolchain that wants to know the iOS version number even when targeting
    // OS X.
    return VersionTuple(5);
  case IOS: {
    VersionTuple Version = getOSVersion();
    // Default to 5.0 (or 7.0 for arm64).
    if (Version.getMajor() == 0)
      return (getArch() == aarch64) ? VersionTuple(7) : VersionTuple(5);
    return Version;
  }
  }
}

void Triple::setTriple(const Twine &Str) { *this = Triple(Str); }

void Triple::setArch(ArchType Kind, SubArchType SubArch) {
  setArchName(getArchName(Kind, SubArch));
}

void Triple::setVendor(VendorType Kind) {
  setVendorName(getVendorTypeName(Kind));
}

void Triple::setOS(OSType Kind) { setOSName(getOSTypeName(Kind)); }

void Triple::setEnvironment(EnvironmentType Kind) {
  if (ObjectFormat == getDefaultFormat(*this))
    return setEnvironmentName(getEnvironmentTypeName(Kind));

  setEnvironmentName((getEnvironmentTypeName(Kind) + Twine("-") +
                      getObjectFormatTypeName(ObjectFormat))
                         .str());
}

void Triple::setObjectFormat(ObjectFormatType Kind) {
  if (Environment == UnknownEnvironment)
    return setEnvironmentName(getObjectFormatTypeName(Kind));

  setEnvironmentName((getEnvironmentTypeName(Environment) + Twine("-") +
                      getObjectFormatTypeName(Kind))
                         .str());
}

void Triple::setArchName(StringRef Str) {
  // Work around a miscompilation bug for Twines in gcc 4.0.3.
  SmallString<64> Triple;
  Triple += Str;
  Triple += "-";
  Triple += getVendorName();
  Triple += "-";
  Triple += getOSAndEnvironmentName();
  setTriple(Triple);
}

void Triple::setVendorName(StringRef Str) {
  setTriple(getArchName() + "-" + Str + "-" + getOSAndEnvironmentName());
}

void Triple::setOSName(StringRef Str) {
  if (hasEnvironment())
    setTriple(getArchName() + "-" + getVendorName() + "-" + Str + "-" +
              getEnvironmentName());
  else
    setTriple(getArchName() + "-" + getVendorName() + "-" + Str);
}

void Triple::setEnvironmentName(StringRef Str) {
  setTriple(getArchName() + "-" + getVendorName() + "-" + getOSName() + "-" +
            Str);
}

void Triple::setOSAndEnvironmentName(StringRef Str) {
  setTriple(getArchName() + "-" + getVendorName() + "-" + Str);
}

static unsigned getArchPointerBitWidth(llvm::Triple::ArchType Arch) {
  switch (Arch) {
  case llvm::Triple::UnknownArch:
    return 0;
  case llvm::Triple::x86:
    return 32;
  case llvm::Triple::aarch64:
  case llvm::Triple::x86_64:
    return 64;
  }
  llvm_unreachable("Invalid architecture value");
}

bool Triple::isArch64Bit() const {
  return getArchPointerBitWidth(getArch()) == 64;
}

Triple Triple::get64BitArchVariant() const {
  Triple T(*this);
  switch (getArch()) {
  case Triple::UnknownArch:
    T.setArch(UnknownArch);
    break;
  case Triple::aarch64:
  case Triple::x86_64:
    break;
  case Triple::x86:
    T.setArch(Triple::x86_64);
    break;
  }
  return T;
}

Triple Triple::getBigEndianArchVariant() const {
  Triple T(*this);
  if (!isLittleEndian())
    return T;
  T.setArch(UnknownArch);
  return T;
}

Triple Triple::getLittleEndianArchVariant() const {
  Triple T(*this);
  if (isLittleEndian())
    return T;
  T.setArch(UnknownArch);
  return T;
}

bool Triple::isLittleEndian() const {
  switch (getArch()) {
  case Triple::aarch64:
  case Triple::x86:
  case Triple::x86_64:
    return true;
  default:
    return false;
  }
}

bool Triple::isCompatibleWith(const Triple &Other) const {
  // If vendor is apple, ignore the version number.
  if (getVendor() == Triple::Apple)
    return getArch() == Other.getArch() && getSubArch() == Other.getSubArch() &&
           getVendor() == Other.getVendor() && getOS() == Other.getOS();

  return *this == Other;
}

std::string Triple::merge(const Triple &Other) const {
  // If vendor is apple, pick the triple with the larger version number.
  if (getVendor() == Triple::Apple)
    if (Other.isOSVersionLT(*this))
      return str();

  return Other.str();
}

bool Triple::isMacOSXVersionLT(unsigned Major, unsigned Minor,
                               unsigned Micro) const {
  assert(isMacOSX() && "Not an OS X triple!");

  // If this is OS X, expect a sane version number.
  if (getOS() == Triple::MacOSX)
    return isOSVersionLT(Major, Minor, Micro);

  // Otherwise, compare to the "Darwin" number.
  if (Major == 10) {
    return isOSVersionLT(Minor + 4, Micro, 0);
  } else {
    assert(Major >= 11 && "Unexpected major version");
    return isOSVersionLT(Major - 11 + 20, Minor, Micro);
  }
}

VersionTuple Triple::getMinimumSupportedOSVersion() const {
  if (getVendor() != Triple::Apple || getArch() != Triple::aarch64)
    return VersionTuple();
  switch (getOS()) {
  case Triple::MacOSX:
    // ARM64 slice is supported starting from macOS 11.0+.
    return VersionTuple(11, 0, 0);
  case Triple::IOS:
    // ARM64 simulators are supported for iOS 14+.
    if (isSimulatorEnvironment())
      return VersionTuple(14, 0, 0);
    break;
  default:
    break;
  }
  return VersionTuple();
}

VersionTuple Triple::getCanonicalVersionForOS(OSType OSKind,
                                              const VersionTuple &Version) {
  switch (OSKind) {
  case MacOSX:
    // macOS 10.16 is canonicalized to macOS 11.
    if (Version == VersionTuple(10, 16))
      return VersionTuple(11, 0);
    [[fallthrough]];
  default:
    return Version;
  }
}
