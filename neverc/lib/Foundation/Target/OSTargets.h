#ifndef NEVERC_LIB_BASIC_TARGET_OSTARGETS_H
#define NEVERC_LIB_BASIC_TARGET_OSTARGETS_H

#include "Targets.h"

namespace neverc {
namespace targets {

template <typename TgtInfo>
class LLVM_LIBRARY_VISIBILITY OSTargetInfo : public TgtInfo {
protected:
  virtual void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                            MacroBuilder &Builder) const = 0;

public:
  OSTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : TgtInfo(Triple, Opts) {}

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override {
    TgtInfo::getTargetDefines(Opts, Builder);
    getOSDefines(Opts, TgtInfo::getTriple(), Builder);
  }
};

void getDarwinDefines(MacroBuilder &Builder, const LangOptions &Opts,
                      const llvm::Triple &Triple, llvm::StringRef &PlatformName,
                      llvm::VersionTuple &PlatformMinVersion);

template <typename Target>
class LLVM_LIBRARY_VISIBILITY DarwinTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    getDarwinDefines(Builder, Opts, Triple, this->PlatformName,
                     this->PlatformMinVersion);
  }

public:
  DarwinTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {
    // Darwin targets are aarch64/x86_64 only (see README). TLS is supported
    // on macOS 10.7+ and 64-bit iOS 8+; everything else stays at the default
    // false.
    this->TLSSupported = false;
    if (Triple.isMacOSX())
      this->TLSSupported = !Triple.isMacOSXVersionLT(10, 7);
    else if (Triple.isiOS() && Triple.isArch64Bit())
      this->TLSSupported = !Triple.isOSVersionLT(8);
  }

  const char *getStaticInitSectionSpecifier() const override {
    return "__TEXT,__StaticInit,regular,pure_instructions";
  }

  bool hasProtectedVisibility() const override { return false; }

  TargetInfo::IntType getLeastIntTypeByWidth(unsigned BitWidth,
                                             bool IsSigned) const final {
    // Darwin uses `long long` for `int_least64_t` and `int_fast64_t`.
    return BitWidth == 64
               ? (IsSigned ? TargetInfo::SignedLongLong
                           : TargetInfo::UnsignedLongLong)
               : TargetInfo::getLeastIntTypeByWidth(BitWidth, IsSigned);
  }
};

// Linux target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY LinuxTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    // Linux defines; list based off of gcc output
    DefineStd(Builder, "unix", Opts);
    DefineStd(Builder, "linux", Opts);
    Builder.defineMacro("__gnu_linux__");
    if (Opts.POSIXThreads)
      Builder.defineMacro("_REENTRANT");
    if (this->HasFloat128)
      Builder.defineMacro("__FLOAT128__");
  }

public:
  LinuxTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {
    this->WIntType = TargetInfo::UnsignedInt;

    switch (Triple.getArch()) {
    default:
      break;
    case llvm::Triple::x86_64:
      this->HasFloat128 = true;
      break;
    }
  }

  const char *getStaticInitSectionSpecifier() const override {
    return ".text.startup";
  }
};

void addWindowsDefines(const llvm::Triple &Triple, const LangOptions &Opts,
                       MacroBuilder &Builder);

// Windows target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY WindowsTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    addWindowsDefines(Triple, Opts, Builder);
  }

public:
  WindowsTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {
    this->WCharType = TargetInfo::UnsignedShort;
    this->WIntType = TargetInfo::UnsignedShort;
  }
};

} // namespace targets
} // namespace neverc
#endif // NEVERC_LIB_BASIC_TARGET_OSTARGETS_H
