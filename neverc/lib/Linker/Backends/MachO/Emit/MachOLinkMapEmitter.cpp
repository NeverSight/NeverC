#include "Linker/Core/Runtime/Diagnostic.h"
#include "Linker/MachO/ConcatOutputSection.h"
#include "Linker/MachO/Config.h"
#include "Linker/MachO/InputFiles.h"
#include "Linker/MachO/InputSection.h"
#include "Linker/MachO/MapFile.h"
#include "Linker/MachO/OutputSegment.h"
#include "Linker/MachO/Symbols.h"
#include "Linker/MachO/SyntheticSections.h"
#include "Linker/MachO/Target.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/TimeProfiler.h"

using namespace llvm;
using namespace llvm::sys;
using namespace linker;
using namespace linker::macho;

// ===----------------------------------------------------------------------===
// Map info collection types
// ===----------------------------------------------------------------------===

struct CStringInfo {
  uint32_t fileIndex;
  StringRef str;
};

struct MapInfo {
  SmallVector<InputFile *> files;
  SmallVector<Defined *> deadSymbols;
  DenseMap<const OutputSection *,
           SmallVector<std::pair<uint64_t /*addr*/, CStringInfo>>>
      liveCStringsForSection;
  SmallVector<CStringInfo> deadCStrings;
};

// ===----------------------------------------------------------------------===
// Symbol & cstring gathering
// ===----------------------------------------------------------------------===

namespace {
MapInfo gatherMapInfo() {
  MapInfo info;
  for (InputFile *file : inputFiles) {
    bool isReferencedFile = false;

    if (isa<ObjFile>(file) || isa<BitcodeFile>(file)) {
      uint32_t fileIndex = info.files.size() + 1;

      // Gather the dead symbols. We don't have to bother with the live ones
      // because we will pick them up as we iterate over the OutputSections
      // later.
      for (Symbol *sym : file->symbols) {
        if (auto *d = dyn_cast_or_null<Defined>(sym))
          // Only emit the prevailing definition; cstring members surface
          // as the literal text in the dedicated cstring pass instead.
          if (d->isec && d->getFile() == file &&
              !isa<CStringInputSection>(d->isec)) {
            isReferencedFile = true;
            if (!d->isLive())
              info.deadSymbols.push_back(d);
          }
      }

      // Gather all the cstrings (both live and dead). A CString(Output)Section
      // doesn't provide us a way of figuring out which InputSections its
      // cstring contents came from, so we need to build up that mapping here.
      for (const Section *sec : file->sections) {
        for (const Subsection &subsec : sec->subsections) {
          if (auto isec = dyn_cast<CStringInputSection>(subsec.isec)) {
            auto &liveCStrings = info.liveCStringsForSection[isec->parent];
            for (const auto &[i, piece] : llvm::enumerate(isec->pieces)) {
              if (piece.live)
                liveCStrings.push_back({isec->parent->addr + piece.outSecOff,
                                        {fileIndex, isec->getStringRef(i)}});
              else
                info.deadCStrings.push_back({fileIndex, isec->getStringRef(i)});
              isReferencedFile = true;
            }
          } else {
            break;
          }
        }
      }
    } else if (const auto *dylibFile = dyn_cast<DylibFile>(file)) {
      isReferencedFile = dylibFile->isReferenced();
    }

    if (isReferencedFile)
      info.files.push_back(file);
  }

  // cstrings are not stored in sorted order in their OutputSections, so we sort
  // them here.
  for (auto &liveCStrings : info.liveCStringsForSection)
    parallelSort(liveCStrings.second, [](const auto &p1, const auto &p2) {
      return p1.first < p2.first;
    });
  return info;
}
} // namespace

// ===----------------------------------------------------------------------===
// Per-section pretty printers
// ===----------------------------------------------------------------------===

namespace {

// We use this instead of `toString(const InputFile *)` as we don't want to
// include the dylib install name in our output.
void printFileName(raw_fd_ostream &os, const InputFile *f) {
  if (f->archiveName.empty())
    os << f->getName();
  else
    os << f->archiveName << "(" << path::filename(f->getName()) + ")";
}

// For printing the contents of the __stubs and __la_symbol_ptr sections.
void printStubsEntries(
    raw_fd_ostream &os,
    const DenseMap<linker::macho::InputFile *, uint32_t> &readerToFileOrdinal,
    const OutputSection *osec, size_t entrySize) {
  for (const Symbol *sym : in.stubs->getEntries())
    os << format("0x%08llX\t0x%08zX\t[%3u] %s\n",
                 osec->addr + sym->stubsIndex * entrySize, entrySize,
                 readerToFileOrdinal.lookup(sym->getFile()),
                 sym->getName().str().data());
}

void printNonLazyPointerSection(raw_fd_ostream &os,
                                NonLazyPointerSectionBase *osec) {
  // GOT entries are reported as linker-synthesised (no owning file); only
  // stub slots carry a file ordinal. Matches the convention crashlog
  // symbolicators expect.
  for (const Symbol *sym : osec->getEntries())
    os << format("0x%08llX\t0x%08zX\t[  0] non-lazy-pointer-to-local: %s\n",
                 osec->addr + sym->gotIndex * target->wordSize,
                 target->wordSize, sym->getName().str().data());
}
} // namespace

// ===----------------------------------------------------------------------===
// Map file emission
// ===----------------------------------------------------------------------===

void macho::writeMapFile() {
  if (config->mapFile.empty())
    return;

  TimeTraceScope timeScope("Write map file");

  // Open a map file for writing.
  std::error_code ec;
  raw_fd_ostream os(config->mapFile, ec, sys::fs::OF_None);
  if (ec) {
    error("cannot open " + config->mapFile + ": " + ec.message());
    return;
  }

  os << format("# Path: %s\n", config->outputFile.str().c_str());
  os << format("# Arch: %s\n",
               getArchitectureName(config->arch()).str().c_str());

  MapInfo info = gatherMapInfo();

  os << "# Object files:\n";
  os << format("[%3u] %s\n", 0, (const char *)"linker synthesized");
  uint32_t fileIndex = 1;
  DenseMap<linker::macho::InputFile *, uint32_t> readerToFileOrdinal;
  for (InputFile *file : info.files) {
    os << format("[%3u] ", fileIndex);
    printFileName(os, file);
    os << "\n";
    readerToFileOrdinal[file] = fileIndex++;
  }

  os << "# Sections:\n";
  os << "# Address\tSize    \tSegment\tSection\n";
  for (OutputSegment *seg : outputSegments)
    for (OutputSection *osec : seg->getSections()) {
      if (osec->isHidden())
        continue;

      os << format("0x%08llX\t0x%08llX\t%s\t%s\n", osec->addr, osec->getSize(),
                   seg->name.str().c_str(), osec->name.str().c_str());
    }

  os << "# Symbols:\n";
  os << "# Address\tSize    \tFile  Name\n";
  for (const OutputSegment *seg : outputSegments) {
    for (const OutputSection *osec : seg->getSections()) {
      if (auto *concatOsec = dyn_cast<ConcatOutputSection>(osec)) {
        for (const InputSection *isec : concatOsec->inputs) {
          for (Defined *sym : isec->symbols)
            if (!(isPrivateLabel(sym->getName()) && sym->size == 0))
              os << format("0x%08llX\t0x%08llX\t[%3u] %s\n", sym->getVA(),
                           sym->size, readerToFileOrdinal[sym->getFile()],
                           sym->getName().str().data());
        }
      } else if (osec == in.cStringSection) {
        const auto &liveCStrings = info.liveCStringsForSection.lookup(osec);
        uint64_t lastAddr = 0; // strings will never start at address 0, so this
                               // is a sentinel value
        for (const auto &[addr, info] : liveCStrings) {
          uint64_t size = 0;
          if (addr != lastAddr)
            size = info.str.size() + 1; // include null terminator
          lastAddr = addr;
          os << format("0x%08llX\t0x%08llX\t[%3u] literal string: ", addr, size,
                       info.fileIndex);
          os.write_escaped(info.str) << "\n";
        }
      } else if (osec == (void *)in.unwindInfo) {
        os << format("0x%08llX\t0x%08llX\t[  0] compact unwind info\n",
                     osec->addr, osec->getSize());
      } else if (osec == in.stubs) {
        printStubsEntries(os, readerToFileOrdinal, osec, target->stubSize);
      } else if (osec == in.lazyPointers) {
        printStubsEntries(os, readerToFileOrdinal, osec, target->wordSize);
      } else if (osec == in.stubHelper) {
        // Apple-style label "helper helper" kept for tool compatibility.
        os << format("0x%08llX\t0x%08llX\t[  0] helper helper\n", osec->addr,
                     osec->getSize());
      } else if (osec == in.got) {
        printNonLazyPointerSection(os, in.got);
      } else if (osec == in.tlvPointers) {
        printNonLazyPointerSection(os, in.tlvPointers);
      }
    }
  }

  if (config->deadStrip) {
    os << "# Dead Stripped Symbols:\n";
    os << "#        \tSize    \tFile  Name\n";
    for (Defined *sym : info.deadSymbols) {
      assert(!sym->isLive());
      os << format("<<dead>>\t0x%08llX\t[%3u] %s\n", sym->size,
                   readerToFileOrdinal[sym->getFile()],
                   sym->getName().str().data());
    }
    for (CStringInfo &cstrInfo : info.deadCStrings) {
      os << format("<<dead>>\t0x%08zX\t[%3u] literal string: ",
                   cstrInfo.str.size() + 1, cstrInfo.fileIndex);
      os.write_escaped(cstrInfo.str) << "\n";
    }
  }
}
