//===-- llvm/Support/DynamicLibrary.h - Portable Dynamic Library -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the sys::DynamicLibrary class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_DYNAMICLIBRARY_H
#define LLVM_SUPPORT_DYNAMICLIBRARY_H

#include "csupport/ldynamic_llibrary.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Mutex.h"

#if defined(HAVE_DLFCN_H) && defined(HAVE_DLOPEN)
#include <dlfcn.h>
#endif

namespace llvm {

class StringRef;

namespace sys {

/// This class provides a portable interface to dynamic libraries which also
/// might be known as shared libraries, shared objects, dynamic shared
/// objects, or dynamic link libraries. Regardless of the terminology or the
/// operating system interface, this class provides a portable interface that
/// allows dynamic libraries to be loaded and searched for externally
/// defined symbols. This is typically used to provide "plug-in" support.
/// It also allows for symbols to be defined which don't live in any library,
/// but rather the main program itself, useful on Windows where the main
/// executable cannot be searched.
class DynamicLibrary {
  // Placeholder whose address represents an invalid library.
  // We use this instead of NULL or a pointer-int pair because the OS library
  // might define 0 or 1 to be "special" handles, such as "search all".
  static char Invalid;

  // Opaque data used to interface with OS-specific dynamic library handling.
  void *Data;

public:
  explicit DynamicLibrary(void *data = &Invalid) : Data(data) {}

  /// Return the OS specific handle value.
  void *getOSSpecificHandle() const { return Data; }

  /// Returns true if the object refers to a valid library.
  bool isValid() const { return Data != &Invalid; }

  /// Searches through the library for the symbol \p symbolName. If it is
  /// found, the address of that symbol is returned. If not, NULL is returned.
  /// Note that NULL will also be returned if the library failed to load.
  /// Use isValid() to distinguish these cases if it is important.
  /// Note that this will \e not search symbols explicitly registered by
  /// AddSymbol().
  void *getAddressOfSymbol(const char *symbolName);

  /// This function permanently loads the dynamic library at the given path
  /// using the library load operation from the host operating system. The
  /// library instance will only be closed when global destructors run, and
  /// there is no guarantee when the library will be unloaded.
  ///
  /// This returns a valid DynamicLibrary instance on success and an invalid
  /// instance on failure (see isValid()). \p *errMsg will only be modified if
  /// the library fails to load.
  ///
  /// It is safe to call this function multiple times for the same library.
  /// Open a dynamic library permanently.
  static DynamicLibrary
  getPermanentLibrary(const char *filename,
                      SmallVectorImpl<char> *errMsg = nullptr);

  /// Registers an externally loaded library. The library will be unloaded
  /// when the program terminates.
  ///
  /// It is safe to call this function multiple times for the same library,
  /// though ownership is only taken if there was no error.
  static DynamicLibrary
  addPermanentLibrary(void *handle, SmallVectorImpl<char> *errMsg = nullptr);

  /// This function permanently loads the dynamic library at the given path.
  /// Use this instead of getPermanentLibrary() when you won't need to get
  /// symbols from the library itself.
  ///
  /// It is safe to call this function multiple times for the same library.
  static bool LoadLibraryPermanently(const char *Filename,
                                     SmallVectorImpl<char> *ErrMsg = nullptr) {
    return !getPermanentLibrary(Filename, ErrMsg).isValid();
  }

  /// This function loads the dynamic library at the given path, using the
  /// library load operation from the host operating system. The library
  /// instance will be closed when closeLibrary is called or global destructors
  /// are run, but there is no guarantee when the library will be unloaded.
  ///
  /// This returns a valid DynamicLibrary instance on success and an invalid
  /// instance on failure (see isValid()). \p *Err will only be modified if the
  /// library fails to load.
  ///
  /// It is safe to call this function multiple times for the same library.
  static DynamicLibrary getLibrary(const char *FileName,
                                   SmallVectorImpl<char> *Err = nullptr);

  /// This function closes the dynamic library at the given path, using the
  /// library close operation of the host operating system, and there is no
  /// guarantee if or when this will cause the library to be unloaded.
  ///
  /// This function should be called only if the library was loaded using the
  /// getLibrary() function.
  static void closeLibrary(DynamicLibrary &Lib);

  enum SearchOrdering {
    /// SO_Linker - Search as a call to dlsym(dlopen(NULL)) would when
    /// DynamicLibrary::getPermanentLibrary(NULL) has been called or
    /// search the list of explcitly loaded symbols if not.
    SO_Linker,
    /// SO_LoadedFirst - Search all loaded libraries, then as SO_Linker would.
    SO_LoadedFirst,
    /// SO_LoadedLast - Search as SO_Linker would, then loaded libraries.
    /// Only useful to search if libraries with RTLD_LOCAL have been added.
    SO_LoadedLast,
    /// SO_LoadOrder - Or this in to search libraries in the ordered loaded.
    /// The default bahaviour is to search loaded libraries in reverse.
    SO_LoadOrder = 4
  };
  static SearchOrdering SearchOrder; // = SO_Linker

  /// This function will search through all previously loaded dynamic
  /// libraries for the symbol \p symbolName. If it is found, the address of
  /// that symbol is returned. If not, null is returned. Note that this will
  /// search permanently loaded libraries (getPermanentLibrary()) as well
  /// as explicitly registered symbols (AddSymbol()).
  /// @throws std::string on error.
  /// Search through libraries for address of a symbol
  static void *SearchForAddressOfSymbol(const char *symbolName);

  /// Convenience function for callers with StringRef.
  static void *SearchForAddressOfSymbol(StringRef symbolName) {
    SmallString<256> Buf(symbolName);
    return SearchForAddressOfSymbol(Buf.c_str());
  }

  /// This functions permanently adds the symbol \p symbolName with the
  /// value \p symbolValue.  These symbols are searched before any
  /// libraries.
  /// Add searchable symbol/value pair.
  static void AddSymbol(StringRef symbolName, void *symbolValue);

  class HandleSet;
};

} // namespace sys
} // namespace llvm

/*== Inline implementations (moved from cpp_bridge.cpp) ==*/

namespace llvm {
namespace sys {

class DynamicLibrary::HandleSet {
  typedef SmallVector<void *, 4> HandleList;
  HandleList Handles;
  void *Process = 0;

public:
  static void *DLOpen(const char *Filename, SmallVectorImpl<char> *Err);
  static void DLClose(void *Handle);
  static void *DLSym(void *Handle, const char *Symbol);

  HandleSet() = default;
  ~HandleSet();

  HandleList::iterator Find(void *Handle) { return find(Handles, Handle); }

  bool Contains(void *Handle) {
    return Handle == Process || Find(Handle) != Handles.end();
  }

  bool AddLibrary(void *Handle, bool IsProcess = false, bool CanClose = true,
                  bool AllowDuplicates = false) {
#ifdef _WIN32
    assert((Handle == this ? IsProcess : !IsProcess) && "Bad Handle.");
#endif
    assert((!AllowDuplicates || !CanClose) &&
           "CanClose must be false if AllowDuplicates is true.");

    if (LLVM_LIKELY(!IsProcess)) {
      if (!AllowDuplicates && Find(Handle) != Handles.end()) {
        if (CanClose)
          DLClose(Handle);
        return false;
      }
      Handles.push_back(Handle);
    } else {
#ifndef _WIN32
      if (Process) {
        if (CanClose)
          DLClose(Process);
        if (Process == Handle)
          return false;
      }
#endif
      Process = Handle;
    }
    return true;
  }

  void CloseLibrary(void *Handle) {
    DLClose(Handle);
    HandleList::iterator it = Find(Handle);
    if (it != Handles.end()) {
      Handles.erase(it);
    }
  }

  void *LibLookup(const char *Symbol, DynamicLibrary::SearchOrdering Order) {
    if (Order & SO_LoadOrder) {
      for (void *Handle : Handles) {
        if (void *Ptr = DLSym(Handle, Symbol))
          return Ptr;
      }
    } else {
      for (void *Handle : llvm::reverse(Handles)) {
        if (void *Ptr = DLSym(Handle, Symbol))
          return Ptr;
      }
    }
    return 0;
  }

  void *Lookup(const char *Symbol, DynamicLibrary::SearchOrdering Order) {
    assert(!((Order & SO_LoadedFirst) && (Order & SO_LoadedLast)) &&
           "Invalid Ordering");

    if (!Process || (Order & SO_LoadedFirst)) {
      if (void *Ptr = LibLookup(Symbol, Order))
        return Ptr;
    }
    if (Process) {
      if (void *Ptr = DLSym(Process, Symbol))
        return Ptr;

      if (Order & SO_LoadedLast) {
        if (void *Ptr = LibLookup(Symbol, Order))
          return Ptr;
      }
    }
    return 0;
  }
};

inline DynamicLibrary::HandleSet::~HandleSet() {
  for (void *Handle : llvm::reverse(Handles))
    csupport_dlclose(Handle);
  if (Process)
    csupport_dlclose(Process);
  DynamicLibrary::SearchOrder = DynamicLibrary::SO_Linker;
}

inline void *DynamicLibrary::HandleSet::DLOpen(const char *File,
                                               SmallVectorImpl<char> *Err) {
  char errbuf[256] = {};
  void *Handle = csupport_dlopen(File, errbuf, sizeof(errbuf));
  if (!Handle) {
    if (Err && errbuf[0]) {
      StringRef s(errbuf);
      Err->assign(s.begin(), s.end());
    }
    return &DynamicLibrary::Invalid;
  }
#ifdef __CYGWIN__
  if (!File)
    Handle = RTLD_DEFAULT;
#endif
  return Handle;
}

inline void DynamicLibrary::HandleSet::DLClose(void *Handle) {
  csupport_dlclose(Handle);
}

inline void *DynamicLibrary::HandleSet::DLSym(void *Handle,
                                              const char *Symbol) {
  return csupport_dlsym(Handle, Symbol);
}

inline void *DoSearch(const char *SymbolName) {
  return csupport_dl_search_special(SymbolName);
}

} // namespace sys
} // namespace llvm

namespace {

struct Globals {
  llvm::StringMap<void *> ExplicitSymbols;
  llvm::sys::DynamicLibrary::HandleSet OpenedHandles;
  llvm::sys::DynamicLibrary::HandleSet OpenedTemporaryHandles;
  llvm::sys::SmartMutex<true> SymbolsMutex;
};

inline Globals &getGlobals() {
  static Globals G;
  return G;
}

} // namespace

namespace llvm {
namespace sys {

inline char DynamicLibrary::Invalid;
inline DynamicLibrary::SearchOrdering DynamicLibrary::SearchOrder =
    DynamicLibrary::SO_Linker;

} // namespace sys

inline void *SearchForAddressOfSpecialSymbol(const char *SymbolName) {
  return sys::DoSearch(SymbolName);
}

namespace sys {

inline void DynamicLibrary::AddSymbol(StringRef SymbolName, void *SymbolValue) {
  auto &G = getGlobals();
  SmartScopedLock<true> Lock(G.SymbolsMutex);
  G.ExplicitSymbols[SymbolName] = SymbolValue;
}

inline DynamicLibrary
DynamicLibrary::getPermanentLibrary(const char *FileName,
                                    SmallVectorImpl<char> *Err) {
  auto &G = getGlobals();
  void *Handle = HandleSet::DLOpen(FileName, Err);
  if (Handle != &Invalid) {
    SmartScopedLock<true> Lock(G.SymbolsMutex);
    G.OpenedHandles.AddLibrary(Handle, /*IsProcess*/ FileName == 0);
  }

  return DynamicLibrary(Handle);
}

inline DynamicLibrary
DynamicLibrary::addPermanentLibrary(void *Handle, SmallVectorImpl<char> *Err) {
  auto &G = getGlobals();
  SmartScopedLock<true> Lock(G.SymbolsMutex);
  if (!G.OpenedHandles.AddLibrary(Handle, /*IsProcess*/ false,
                                  /*CanClose*/ false)) {
    StringRef Msg = "Library already loaded";
    Err->assign(Msg.begin(), Msg.end());
  }

  return DynamicLibrary(Handle);
}

inline DynamicLibrary DynamicLibrary::getLibrary(const char *FileName,
                                                 SmallVectorImpl<char> *Err) {
  assert(FileName && "Use getPermanentLibrary() for opening process handle");
  void *Handle = HandleSet::DLOpen(FileName, Err);
  if (Handle != &Invalid) {
    auto &G = getGlobals();
    SmartScopedLock<true> Lock(G.SymbolsMutex);
    G.OpenedTemporaryHandles.AddLibrary(Handle, /*IsProcess*/ false,
                                        /*CanClose*/ false,
                                        /*AllowDuplicates*/ true);
  }
  return DynamicLibrary(Handle);
}

inline void DynamicLibrary::closeLibrary(DynamicLibrary &Lib) {
  auto &G = getGlobals();
  SmartScopedLock<true> Lock(G.SymbolsMutex);
  if (Lib.isValid()) {
    G.OpenedTemporaryHandles.CloseLibrary(Lib.Data);
    Lib.Data = &Invalid;
  }
}

inline void *DynamicLibrary::getAddressOfSymbol(const char *SymbolName) {
  if (!isValid())
    return 0;
  return HandleSet::DLSym(Data, SymbolName);
}

inline void *DynamicLibrary::SearchForAddressOfSymbol(const char *SymbolName) {
  {
    auto &G = getGlobals();
    SmartScopedLock<true> Lock(G.SymbolsMutex);

    StringMap<void *>::iterator i = G.ExplicitSymbols.find(SymbolName);

    if (i != G.ExplicitSymbols.end())
      return i->second;

    if (void *Ptr = G.OpenedHandles.Lookup(SymbolName, SearchOrder))
      return Ptr;
    if (void *Ptr = G.OpenedTemporaryHandles.Lookup(SymbolName, SearchOrder))
      return Ptr;
  }

  return llvm::SearchForAddressOfSpecialSymbol(SymbolName);
}

} // namespace sys
} // namespace llvm

#endif
