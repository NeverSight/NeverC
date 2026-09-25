//===----------------------------------------------------------------------===//
//
//  FastLink pipeline: load, resolve, collect live sections, scan relocations,
//  lay out and write, each phase parallel over files, sections or names.
//  See FastLink.h for the scope of links it handles.
//
//===----------------------------------------------------------------------===//

#include "FastLink.h"

#if !defined(__linux__)

fastlink::Status fastlink::link(const Request &, std::string &Reason) {
  Reason = "the fast pipeline runs on Linux hosts";
  return Status::Declined;
}

#else

#include "FastLinkSupport.h"

#include <cerrno>
#include <fnmatch.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <map>
#include <memory>
#include <unordered_map>

using namespace fl;

namespace {

using Options = fastlink::Request;

// Section types not in every <elf.h>.
constexpr uint32_t SHT_LLVM_ADDRSIG_ = 0x6fff4c03;
constexpr uint32_t SHT_LLVM_CALL_GRAPH_PROFILE_ = 0x6fff4c09;

// ================================================================ inputs
// Per input section; kept small, since every section of every object in the
// link has one.
struct SectionState {
  // 1 live, 2 discarded with its COMDAT group, 3 folded into an identical
  // section, whose placement it shares.
  std::atomic<uint8_t> live{0};
  std::atomic<uint8_t> keepUnique{0}; // its address must stay distinct
  uint32_t osec = 0;      // output section index, 0 when not in the output
  uint32_t groupNext = 0; // next member of the section's group, cyclic
  uint32_t placed = 0;    // index of the PlacedSection, when in the output
};

// Per input section that is part of the output.
struct PlacedSection {
  uint64_t va = 0;        // output address, after layout
  uint32_t outOff = 0;    // offset inside the output section
  uint32_t padBefore = 0; // alignment padding preceding the section
  // Dynamic relocations against the contents: counts from scanning, then
  // the first slot of each kind in .rela.dyn.
  uint32_t numRel = 0, numSym = 0;
  uint32_t relBase = 0, symBase = 0;
  uint32_t kindBase = 0; // first entry in the relocation kind array
  uint32_t align = 0;    // alignment when raised by folding, else 0
  // Mergeable sections: the pieces, in a shared array, and the size of the
  // pieces this section holds in the output.
  uint32_t pieceBase = 0, numPieces = 0;
  uint32_t mergedSize = 0;
  uint16_t mergeGroup = 0;
  bool merged = false;
  uint32_t icfClass[2] = {0, 0}; // equivalence class, double-buffered
  bool icfCandidate = false;
};

struct EhPiece {
  uint32_t off, size;
  uint32_t relBegin, relEnd;
  uint32_t cie;    // FDE: index of its CIE piece; CIE: UINT32_MAX
  uint32_t target; // FDE: the section it describes, or 0
  uint32_t outOff = 0;
  bool live = false;
  bool inHdr = false; // an FDE listed in .eh_frame_hdr
};

struct ObjectFile {
  string name;
  uint32_t ehSec = 0;
  vector<EhPiece> eh;
  vector<std::pair<uint32_t, uint32_t>> fdeByTarget; // (section, piece)
  uint64_t ehOutOff = 0, ehSize = 0;
  uint32_t numLiveFdes = 0, fdeBase = 0;
  // .symtab contribution
  uint32_t symLocals = 0, symGlobals = 0, localBase = 0, globalBase = 0;
  uint64_t strBytes = 0, strBase = 0;
  const uint8_t *data = nullptr;
  size_t size = 0;
  uint32_t pos = 0;
  bool lazy = false;
  const Elf64_Shdr *shdrs = nullptr;
  uint32_t numShdrs = 0;
  const char *shstrtab = nullptr;
  const Elf64_Sym *syms = nullptr;
  uint32_t numSyms = 0, firstGlobal = 0;
  const char *strtab = nullptr;
  uint32_t *nameIds = nullptr; // per global
  uint32_t *relaOf = nullptr;  // section -> its SHT_RELA section
  vector<uint32_t> groups;     // SHT_GROUP sections
  vector<uint32_t> groupIds;   // per group: signature id
  uint32_t commentSec = 0;
  uint32_t addrsigSec = 0;
  vector<uint32_t> debugSecs; // .debug_* sections, in index order
  vector<uint32_t> roots; // GC root sections
  SectionState *secs = nullptr;
  std::atomic<uint8_t> live{0};

  const char *secName(uint32_t i) const {
    return shstrtab + shdrs[i].sh_name;
  }
  const uint8_t *secData(uint32_t i) const {
    return data + shdrs[i].sh_offset;
  }
  const char *symName(uint32_t i) const { return strtab + syms[i].st_name; }
  std::pair<const Elf64_Rela *, size_t> relas(uint32_t sec) const {
    uint32_t r = relaOf[sec];
    if (!r)
      return {nullptr, 0};
    return {reinterpret_cast<const Elf64_Rela *>(data + shdrs[r].sh_offset),
            shdrs[r].sh_size / sizeof(Elf64_Rela)};
  }
};

struct SharedFile {
  string path, soname;
  const uint8_t *data = nullptr;
  size_t size = 0;
  uint32_t pos = 0;
  bool asNeeded = false;
  const Elf64_Sym *dynsyms = nullptr;
  uint32_t numDyn = 0;
  const char *dynstr = nullptr;
  const uint16_t *versym = nullptr;
  const Elf64_Shdr *shdrs = nullptr;
  const uint8_t *verdef = nullptr;
  uint32_t numVerdef = 0;
  uint32_t *nameIds = nullptr; // per dynsym; UINT32_MAX if not interned
  std::atomic<uint8_t> needed{0};
};

struct MappedFile {
  const uint8_t *data;
  size_t size;
  bool fromDisk = false; // a mapping of a file, as opposed to caller memory
};

const Options *activeOptions; // the running link's options

MappedFile mapFile(const string &path) {
  if (activeOptions->openFile) {
    const unsigned char *data;
    size_t size;
    if (activeOptions->openFile(path, data, size))
      return {data, size};
  }
  int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0)
    fatal("cannot open " + path);
  struct stat st;
  fstat(fd, &st);
  void *p = st.st_size ? mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE,
                              fd, 0)
                       : nullptr;
  close(fd);
  if (p == MAP_FAILED)
    fatal("cannot map " + path);
  return {static_cast<const uint8_t *>(p), size_t(st.st_size), true};
}

// ================================================================ context
enum SymFlag : uint16_t {
  StrongRef = 1,
  WeakRef = 2,
  SharedRef = 4, // named by a shared library
  NeedsGot = 8,
  NeedsPlt = 16,
  NeedsDynsym = 32,
  NeedsGotTp = 64,
  CanonicalPlt = 128, // the import's address is its PLT entry
  NeedsCopy = 256,    // shared data copied into the executable
  HiddenRef = 512,    // referenced with non-default visibility
  StrongDef = 1024,   // has a global (non-weak) definition
  DupCandidate = 2048, // has more than one global definition
  Preemptible = 4096,  // bound by the dynamic loader
  NeedsTlsGd = 8192,   // needs a general-dynamic TLS GOT pair
};

constexpr uint64_t None64 = ~0ull;

struct Symbol {
  // DynUndef: an undefined weak reference that a shared library loaded at
  // run time may still satisfy; it is imported like a shared definition.
  enum Kind : uint8_t { Undefined, Object, Shared, Linker, DynUndef };
  Kind kind = Undefined;
  bool isImport() const { return kind == Shared || kind == DynUndef; }
  uint32_t file = 0; // index into objects/shared
  uint32_t index = 0;
  uint64_t va = 0;
  uint64_t copyOff = 0;
  uint32_t got = UINT32_MAX, plt = UINT32_MAX, dynsym = 0;
  uint32_t gotTp = UINT32_MAX;
  uint32_t tlsGd = UINT32_MAX; // first slot of the general-dynamic TLS pair
  bool copied = false; // shared data copied into the executable
  bool ifunc = false;  // defined as STT_GNU_IFUNC
  bool copyPrimary = false;
};

struct OutputSection {
  string name;
  uint32_t type = SHT_PROGBITS;
  uint64_t flags = 0;
  uint64_t align = 1;
  uint64_t entsize = 0;
  vector<std::pair<ObjectFile *, uint32_t>> members;
  uint64_t size = 0, addr = 0, offset = 0;
  int rank = 0;
  uint32_t index = 0; // creation index
  uint32_t shndx = 0; // section header index
  uint32_t nameOff = 0;
  // Synthetic contents writer, if any.
  std::function<void(uint8_t *)> write;
  bool relro = false;
};

struct Ctx {
  Options opt;
  std::unique_ptr<Pool> pool;
  vector<ObjectFile *> objects;
  vector<SharedFile *> shared;
  std::unique_ptr<NameTable> names;
  uint32_t numNames = 0;
  vector<string_view> nameStr;
  // per name
  std::atomic<uint64_t> *provider = nullptr, *objDef = nullptr,
                        *shDef = nullptr;
  std::atomic<uint16_t> *flags = nullptr;
  Symbol *syms = nullptr;
  uint16_t *versionIds = nullptr; // by name, with a version script
  PlacedSection *placed = nullptr; // indexed by SectionState::placed
  // Pieces of mergeable sections: input offset, piece id in the group's
  // table, and output offset in the output section (UINT32_MAX until the
  // owner's placement is known).
  struct Piece {
    uint32_t inOff, id, out;
  };
  struct FreeDeleter {
    void operator()(void *p) const { free(p); }
  };
  std::unique_ptr<Piece[], FreeDeleter> pieces;
  uint32_t numPlaced = 0;
  // Per name: the defining object and section, for the GC hot path
  // (file UINT32_MAX when not defined in an object section).
  std::pair<uint32_t, uint32_t> *defTarget = nullptr;
  vector<OutputSection *> osecs;
  // dynamic linking state
  vector<uint32_t> gotEntries;   // name ids with a GOT slot
  vector<uint32_t> gotTpEntries; // name ids with a TP-offset GOT slot
  vector<uint32_t> pltEntries;   // name ids with a PLT stub
  vector<uint32_t> dynsyms;      // name ids in .dynsym order (after null)
  vector<string> needed;
  bool isPic() const { return opt.pie || opt.shared; }
};

Ctx ctx;
std::atomic<bool> needsTlsLd{false}; // the module's TLS GOT pair is used

inline PlacedSection &placedOf(const ObjectFile *o, uint32_t sec) {
  return ctx.placed[o->secs[sec].placed];
}

// Whether a name's definition goes to .dynsym: those shared libraries
// refer to, or all with --export-dynamic, unless their visibility is
// restricted.
inline bool isExported(uint32_t id) {
  const Symbol &s = ctx.syms[id];
  if (s.kind != Symbol::Object)
    return false;
  const uint16_t f = ctx.flags[id].load(std::memory_order_relaxed);
  if (!(f & SharedRef) && !ctx.opt.exportDynamic && !ctx.opt.shared)
    return false;
  if ((f & HiddenRef) || (ctx.versionIds && ctx.versionIds[id] == 0))
    return false;
  const int vis =
      ELF64_ST_VISIBILITY(ctx.objects[s.file]->syms[s.index].st_other);
  return vis == STV_DEFAULT || vis == STV_PROTECTED;
}

// Assigns version script nodes to definitions: exact names first, then glob
// patterns with later nodes taking precedence, "*" last. Unassigned
// definitions get the global version.
void applyVersionScript() {
  const auto &nodes = ctx.opt.versions;
  if (nodes.empty())
    return;
  bool any = false;
  for (const auto &n : nodes)
    any |= !n.global.empty() || !n.local.empty();
  if (!any)
    return;
  ctx.versionIds = bigArray<uint16_t>(*ctx.pool, ctx.numNames);
  vector<uint8_t> assigned(ctx.numNames, 0);
  auto exact = [&](const fastlink::Request::VersionPattern &p, uint16_t id) {
    const uint32_t name = ctx.names->find(p.name.data(), p.name.size());
    if (name == UINT32_MAX || ctx.syms[name].kind != Symbol::Object) {
      if (!ctx.opt.undefinedVersion)
        fatal("version script names undefined symbol " + p.name);
      return;
    }
    if (assigned[name] && ctx.versionIds[name] != id)
      fatal("version script assigns " + p.name + " twice");
    assigned[name] = 1;
    ctx.versionIds[name] = id;
  };
  for (size_t v = 0; v < nodes.size(); ++v) {
    for (const auto &p : nodes[v].global)
      if (!p.wildcard)
        exact(p, uint16_t(v));
    for (const auto &p : nodes[v].local)
      if (!p.wildcard)
        exact(p, 0);
  }
  // Glob patterns in precedence order.
  vector<std::pair<const string *, uint16_t>> globs;
  for (int star = 0; star < 2; ++star)
    for (size_t v = nodes.size(); v-- > 0;) {
      for (const auto &p : nodes[v].global)
        if (p.wildcard && (p.name == "*") == bool(star))
          globs.push_back({&p.name, uint16_t(v)});
      for (const auto &p : nodes[v].local)
        if (p.wildcard && (p.name == "*") == bool(star))
          globs.push_back({&p.name, 0});
    }
  ctx.pool->forEach(ctx.numNames, [&](size_t id) {
    if (assigned[id])
      return;
    ctx.versionIds[id] = 1;
    if (ctx.syms[id].kind != Symbol::Object)
      return;
    const string name(ctx.nameStr[id]);
    for (auto [pattern, v] : globs)
      if (fnmatch(pattern->c_str(), name.c_str(), 0) == 0) {
        ctx.versionIds[id] = v;
        return;
      }
  });
}

// Marks the names the dynamic loader binds: imports, and in a shared
// library the exported definitions of default visibility that -Bsymbolic
// does not bind locally.
void markPreemptible() {
  ctx.pool->forEach(ctx.numNames, [&](size_t id) {
    const Symbol &s = ctx.syms[id];
    bool p = s.isImport();
    if (!p && ctx.opt.shared && isExported(uint32_t(id))) {
      const Elf64_Sym &d = ctx.objects[s.file]->syms[s.index];
      const bool func = ELF64_ST_TYPE(d.st_info) == STT_FUNC ||
                        ELF64_ST_TYPE(d.st_info) == STT_GNU_IFUNC;
      const bool weak = ELF64_ST_BIND(d.st_info) == STB_WEAK;
      switch (ctx.opt.bsymbolic) {
      case 0:
        p = ELF64_ST_VISIBILITY(d.st_other) == STV_DEFAULT;
        break;
      case 2:
        p = !func;
        break;
      case 3:
        p = weak;
        break;
      case 4:
        p = !func || weak;
        break;
      }
      p = p && ELF64_ST_VISIBILITY(d.st_other) == STV_DEFAULT;
    }
    if (p)
      atomicOr(ctx.flags[id], uint16_t(Preemptible));
  });
}

inline bool isPreemptible(uint32_t id) {
  return ctx.flags[id].load(std::memory_order_relaxed) & Preemptible;
}

// The bytes an input section occupies in its output section.
inline uint64_t outputSize(const ObjectFile *o, uint32_t sec,
                           const PlacedSection &ps) {
  return ps.merged ? ps.mergedSize : o->shdrs[sec].sh_size;
}

// The output address of offset `off` of a mergeable section.
uint64_t mergedAddress(const ObjectFile *o, uint32_t sec, uint64_t off);

// Runs fn(id, lists) for every name id in parallel, where lists holds the
// calling worker's K output vectors, then appends each worker's vectors in
// worker order. Callers order the results themselves.
template <size_t K, class F>
void collectNames(std::array<vector<uint32_t> *, K> outs, F fn) {
  vector<std::array<vector<uint32_t>, K>> local(ctx.pool->size());
  ctx.pool->forEach(ctx.numNames, [&](size_t id) {
    fn(uint32_t(id), local[Pool::self()]);
  });
  for (auto &w : local)
    for (size_t k = 0; k < K; ++k)
      outs[k]->insert(outs[k]->end(), w[k].begin(), w[k].end());
}

// Fine-grained wall-clock timers, printed with --time.
struct StepTimer {
  const char *name;
  Clock::time_point start = Clock::now();
  explicit StepTimer(const char *n) : name(n) {}
  ~StepTimer() {
    if (ctx.opt.timing)
      fprintf(stderr, "  %-22s %6.2f\n", name, msSince(start));
  }
};
#define STEP(n) StepTimer stepTimer##__LINE__(n)
Clock::time_point markTime = Clock::now();
void mark(const char *n) {
  if (ctx.opt.timing)
    fprintf(stderr, "    . %-20s %6.2f\n", n, msSince(markTime));
  markTime = Clock::now();
}

// ================================================================ phase 1: inputs
struct InputSpec {
  string path;
  bool whole = false;
  bool asNeeded = false;
};

string findLibrary(const vector<string> &paths, const string &name) {
  for (const string &d : paths) {
    string so = d + "/lib" + name + ".so";
    if (access(so.c_str(), R_OK) == 0)
      return so;
    string a = d + "/lib" + name + ".a";
    if (access(a.c_str(), R_OK) == 0)
      return a;
  }
  fatal("unable to find library -l" + name);
}

// Minimal GNU ld script support: GROUP / INPUT / AS_NEEDED of paths.
bool parseLinkerScript(const MappedFile &m, bool asNeeded,
                       vector<InputSpec> &out) {
  string text(reinterpret_cast<const char *>(m.data), m.size);
  // strip comments
  for (size_t p; (p = text.find("/*")) != string::npos;) {
    size_t e = text.find("*/", p);
    text.erase(p, e == string::npos ? string::npos : e + 2 - p);
  }
  vector<string> toks;
  string cur;
  for (char c : text) {
    if (isspace((unsigned char)c) || c == '(' || c == ')' || c == ',') {
      if (!cur.empty())
        toks.push_back(cur), cur.clear();
      if (c == '(' || c == ')')
        toks.push_back(string(1, c));
    } else {
      cur += c;
    }
  }
  if (!cur.empty())
    toks.push_back(cur);
  bool inNeeded = false;
  int depth = 0;
  bool any = false;
  for (size_t i = 0; i < toks.size(); ++i) {
    const string &t = toks[i];
    if (t == "OUTPUT_FORMAT" || t == "SEARCH_DIR") {
      while (i < toks.size() && toks[i] != ")")
        ++i;
      continue;
    }
    if (t == "GROUP" || t == "INPUT") {
      any = true;
      continue;
    }
    if (t == "AS_NEEDED") {
      inNeeded = true;
      continue;
    }
    if (t == "(") {
      ++depth;
      continue;
    }
    if (t == ")") {
      if (inNeeded)
        inNeeded = false;
      --depth;
      continue;
    }
    if (depth > 0) {
      string path = t;
      if (t.rfind("-l", 0) == 0)
        path = findLibrary(ctx.opt.libPaths, t.substr(2));
      else if (t[0] != '/' && access(t.c_str(), R_OK) != 0)
        for (const string &d : ctx.opt.libPaths)
          if (access((d + "/" + t).c_str(), R_OK) == 0) {
            path = d + "/" + t;
            break;
          }
      out.push_back({path, false, asNeeded || inNeeded});
    }
  }
  return any;
}

void loadInputs(const vector<InputSpec> &specs) {
  STEP("load");
  // Map every input once, expanding linker scripts (they are tiny and few).
  vector<InputSpec> flat;
  vector<MappedFile> maps;
  for (const InputSpec &s : specs) {
    MappedFile m = mapFile(s.path);
    if (m.size >= 4 && (memcmp(m.data, ELFMAG, 4) == 0 ||
                        memcmp(m.data, "!<ar", 4) == 0)) {
      flat.push_back(s);
      maps.push_back(m);
      continue;
    }
    vector<InputSpec> sub;
    if (!parseLinkerScript(m, s.asNeeded, sub))
      fatal("unknown file type: " + s.path);
    for (const InputSpec &x : sub) {
      flat.push_back(x);
      maps.push_back(mapFile(x.path));
    }
  }

  // The worker count follows from the size of the link.
  uint64_t bytes = 0;
  for (const MappedFile &m : maps)
    bytes += m.size;
  unsigned threads =
      ctx.opt.selectThreads ? ctx.opt.selectThreads(bytes, maps.size())
      : ctx.opt.threads     ? ctx.opt.threads
                            : std::min(16u, std::thread::hardware_concurrency());
  ctx.pool = std::make_unique<Pool>(std::max(1u, threads));

  // Build the page tables of the mappings up front, each 2 MiB block by one
  // worker. Workers faulting in neighboring pages of one file would contend
  // for the same page-table lock.
  if (!getenv("FL_NOPOP")) {
    constexpr uintptr_t Block = 2 << 20;
    const uintptr_t page = sysconf(_SC_PAGESIZE);
    vector<std::pair<uintptr_t, uintptr_t>> blocks;
    for (const MappedFile &m : maps) {
      if (!m.fromDisk || !m.size)
        continue;
      uintptr_t b = reinterpret_cast<uintptr_t>(m.data);
      uintptr_t e = (b + m.size + page - 1) & ~(page - 1);
      for (uintptr_t x = b; x < e;) {
        uintptr_t next = std::min(e, (x & ~(Block - 1)) + Block);
        blocks.push_back({x, next});
        x = next;
      }
    }
    ctx.pool->forEach(blocks.size(), [&](size_t i) {
      madvise(reinterpret_cast<void *>(blocks[i].first),
              blocks[i].second - blocks[i].first, 22 /* MADV_POPULATE_READ */);
    }, 1);
  }

  struct Loaded {
    vector<ObjectFile *> objs;
    SharedFile *so = nullptr;
  };
  vector<Loaded> loaded(flat.size());
  ctx.pool->forEach(
      flat.size(),
      [&](size_t i) {
        const InputSpec &s = flat[i];
        const MappedFile &m = maps[i];
        if (m.size >= 8 && memcmp(m.data, "!<arch>\n", 8) == 0) {
          size_t off = 8;
          while (off + 60 <= m.size) {
            const char *h = reinterpret_cast<const char *>(m.data + off);
            size_t sz = strtoull(string(h + 48, 10).c_str(), nullptr, 10);
            const uint8_t *body = m.data + off + 60;
            bool special = h[0] == '/' && (h[1] == ' ' || h[1] == '/' ||
                                           memcmp(h, "/SYM64/", 7) == 0);
            // The full backend diagnoses members that are not objects.
            if (!special && (sz < 4 || memcmp(body, ELFMAG, 4) != 0))
              fatal(s.path + ": archive member is not an ELF object");
            if (!special) {
              auto *o = new ObjectFile;
              o->data = body;
              o->size = sz;
              o->name = s.path;
              o->lazy = !s.whole;
              loaded[i].objs.push_back(o);
            }
            off += 60 + sz + (sz & 1);
          }
          return;
        }
        auto *eh = reinterpret_cast<const Elf64_Ehdr *>(m.data);
        if (eh->e_type == ET_DYN) {
          auto *so = new SharedFile;
          so->path = s.path;
          so->data = m.data;
          so->size = m.size;
          so->asNeeded = s.asNeeded;
          loaded[i].so = so;
          return;
        }
        auto *o = new ObjectFile;
        o->data = m.data;
        o->size = m.size;
        o->name = s.path;
        loaded[i].objs.push_back(o);
      },
      1);
  uint32_t pos = 0;
  for (Loaded &l : loaded) {
    for (ObjectFile *o : l.objs) {
      o->pos = pos++;
      ctx.objects.push_back(o);
    }
    if (l.so) {
      l.so->pos = pos++;
      ctx.shared.push_back(l.so);
    }
  }
}

// ================================================================ phase 2: resolution
void parseObjectHeader(ObjectFile *o) {
  auto *eh = reinterpret_cast<const Elf64_Ehdr *>(o->data);
  if (eh->e_machine != EM_X86_64 || eh->e_ident[EI_CLASS] != ELFCLASS64)
    fatal(o->name + ": not an x86-64 ELF object");
  o->shdrs = reinterpret_cast<const Elf64_Shdr *>(o->data + eh->e_shoff);
  o->numShdrs = eh->e_shnum;
  o->shstrtab =
      reinterpret_cast<const char *>(o->data + o->shdrs[eh->e_shstrndx].sh_offset);
  for (uint32_t i = 0; i < o->numShdrs; ++i) {
    const Elf64_Shdr &sh = o->shdrs[i];
    if (sh.sh_type == SHT_SYMTAB) {
      o->syms = reinterpret_cast<const Elf64_Sym *>(o->data + sh.sh_offset);
      o->numSyms = sh.sh_size / sizeof(Elf64_Sym);
      o->firstGlobal = sh.sh_info;
      o->strtab =
          reinterpret_cast<const char *>(o->data + o->shdrs[sh.sh_link].sh_offset);
      break;
    }
  }
  uint32_t ng = o->numSyms > o->firstGlobal ? o->numSyms - o->firstGlobal : 0;
  o->nameIds = static_cast<uint32_t *>(malloc(std::max<uint32_t>(ng, 1) * 4));
}

bool internObject(ObjectFile *o) {
  uint32_t ng = o->numSyms > o->firstGlobal ? o->numSyms - o->firstGlobal : 0;
  for (uint32_t k = 0; k < ng; ++k) {
    const char *n = o->symName(o->firstGlobal + k);
    size_t len = strlen(n);
    if (memchr(n, '@', len))
      fatal(o->name + ": versioned symbol names are not supported: " + n);
    if ((o->nameIds[k] = ctx.names->intern(n, len)) == UINT32_MAX)
      return false;
  }
  return true;
}

bool isRootSection(const ObjectFile *o, uint32_t i) {
  const Elf64_Shdr &sh = o->shdrs[i];
  if (!(sh.sh_flags & SHF_ALLOC))
    return false;
  if (sh.sh_flags & SHF_GNU_RETAIN)
    return true;
  switch (sh.sh_type) {
  case SHT_INIT_ARRAY:
  case SHT_FINI_ARRAY:
  case SHT_PREINIT_ARRAY:
    return true;
  case SHT_NOTE:
    return !(sh.sh_flags & SHF_GROUP);
  }
  const char *n = o->secName(i);
  if (n[0] != '.')
    return false;
  switch (n[1]) {
  case 'i':
    return strcmp(n, ".init") == 0 || strncmp(n, ".init_array", 11) == 0;
  case 'f':
    return strcmp(n, ".fini") == 0 || strncmp(n, ".fini_array", 11) == 0;
  case 'c':
    return strncmp(n, ".ctors", 6) == 0;
  case 'd':
    return strncmp(n, ".dtors", 6) == 0;
  case 'j':
    return strcmp(n, ".jcr") == 0;
  }
  return false;
}

// Section-level state, for objects that are part of the link. The arrays
// come from shared zeroed arenas.
void initSections(ObjectFile *o, SectionState *secs, uint32_t *relaOf) {
  o->relaOf = relaOf;
  o->secs = secs;
  for (uint32_t i = 0; i < o->numShdrs; ++i) {
    const Elf64_Shdr &sh = o->shdrs[i];
    if (sh.sh_type == SHT_RELA && sh.sh_info < o->numShdrs) {
      o->relaOf[sh.sh_info] = i;
    } else if (sh.sh_type == SHT_REL) {
      fatal(o->name + ": SHT_REL is not supported");
    } else if (sh.sh_type == SHT_GROUP) {
      o->groups.push_back(i);
    } else if (!(sh.sh_flags & SHF_ALLOC) && i) {
      // Non-allocated sections other than these would be copied to the
      // output, which the pipeline does not do.
      switch (sh.sh_type) {
      case SHT_SYMTAB:
      case SHT_STRTAB:
        break;
      case SHT_LLVM_ADDRSIG_:
        o->addrsigSec = i;
        break;
      case SHT_LLVM_CALL_GRAPH_PROFILE_:
        break;
      case SHT_PROGBITS:
        if (strcmp(o->secName(i), ".comment") == 0) {
          o->commentSec = i;
          break;
        }
        if (strcmp(o->secName(i), ".note.GNU-stack") == 0)
          break;
        [[fallthrough]];
      default:
        if (strncmp(o->secName(i), ".debug", 6) == 0) {
          if (ctx.opt.stripDebug)
            break;
          if (sh.sh_flags & SHF_COMPRESSED)
            fatal(o->name + ": compressed debug section " + o->secName(i));
          o->debugSecs.push_back(i);
          break;
        }
        fatal(o->name + ": non-allocated section " + o->secName(i));
      }
    } else if (sh.sh_type == SHT_X86_64_UNWIND ||
               (sh.sh_type == SHT_PROGBITS && !o->ehSec &&
                strcmp(o->secName(i), ".eh_frame") == 0)) {
      o->ehSec = i;
    }
    if ((sh.sh_flags & (SHF_ALLOC | SHF_LINK_ORDER)) ==
        (SHF_ALLOC | SHF_LINK_ORDER))
      fatal(o->name + ": SHF_LINK_ORDER section " + o->secName(i));
    // Symbol overrides change resolution; the full backend applies them.
    if ((sh.sh_flags & SHF_GNU_RETAIN) &&
        strcmp(o->secName(i), ".neverc.overrides") == 0)
      fatal(o->name + ": symbol overrides");
    if (ctx.opt.gcSections && i && isRootSection(o, i))
      o->roots.push_back(i);
  }
}

void parseShared(SharedFile *so) {
  auto *eh = reinterpret_cast<const Elf64_Ehdr *>(so->data);
  auto *shdrs = reinterpret_cast<const Elf64_Shdr *>(so->data + eh->e_shoff);
  so->shdrs = shdrs;
  const Elf64_Dyn *dyn = nullptr;
  size_t numDyn = 0;
  for (uint32_t i = 0; i < eh->e_shnum; ++i) {
    const Elf64_Shdr &sh = shdrs[i];
    if (sh.sh_type == SHT_DYNSYM) {
      so->dynsyms = reinterpret_cast<const Elf64_Sym *>(so->data + sh.sh_offset);
      so->numDyn = sh.sh_size / sizeof(Elf64_Sym);
      so->dynstr =
          reinterpret_cast<const char *>(so->data + shdrs[sh.sh_link].sh_offset);
    } else if (sh.sh_type == SHT_GNU_verdef) {
      so->verdef = so->data + sh.sh_offset;
      so->numVerdef = sh.sh_info;
    } else if (sh.sh_type == SHT_GNU_versym) {
      so->versym = reinterpret_cast<const uint16_t *>(so->data + sh.sh_offset);
    } else if (sh.sh_type == SHT_DYNAMIC) {
      dyn = reinterpret_cast<const Elf64_Dyn *>(so->data + sh.sh_offset);
      numDyn = sh.sh_size / sizeof(Elf64_Dyn);
    }
  }
  const char *base = so->path.c_str();
  const char *slash = strrchr(base, '/');
  so->soname = slash ? slash + 1 : base;
  for (size_t i = 0; i < numDyn && dyn[i].d_tag != DT_NULL; ++i)
    if (dyn[i].d_tag == DT_SONAME)
      so->soname = so->dynstr + dyn[i].d_un.d_val;
  so->nameIds = static_cast<uint32_t *>(malloc(std::max<uint32_t>(so->numDyn, 1) * 4));
}

bool internShared(SharedFile *so) {
  for (uint32_t k = 0; k < so->numDyn; ++k) {
    so->nameIds[k] = UINT32_MAX;
    const Elf64_Sym &s = so->dynsyms[k];
    if (k == 0 || ELF64_ST_BIND(s.st_info) == STB_LOCAL)
      continue;
    // Non-default versions of a definition are not visible to references
    // without a version.
    if (s.st_shndx != SHN_UNDEF && so->versym && (so->versym[k] & 0x8000))
      continue;
    const char *n = so->dynstr + s.st_name;
    if ((so->nameIds[k] = ctx.names->intern(n, strlen(n))) == UINT32_MAX)
      return false;
  }
  return true;
}

void resolve() {
  STEP("resolve");
  markTime = Clock::now();
  const size_t no = ctx.objects.size(), ns = ctx.shared.size();
  ctx.pool->forEach(no + ns, [&](size_t i) {
    if (i < no)
      parseObjectHeader(ctx.objects[i]);
    else
      parseShared(ctx.shared[i - no]);
  }, 8);
  mark("headers");
  // Size the name table from the global symbol counts; most names repeat,
  // so start at an eighth and retry with room for every entry if that overflows.
  size_t total = 0;
  for (ObjectFile *o : ctx.objects)
    total += o->numSyms - std::min(o->numSyms, o->firstGlobal);
  for (SharedFile *so : ctx.shared)
    total += so->numDyn;
  // Largest files first, so no big file starts last.
  vector<std::pair<uint32_t, uint32_t>> order(no + ns);
  for (size_t i = 0; i < no; ++i) {
    const ObjectFile *o = ctx.objects[i];
    order[i] = {o->numSyms - std::min(o->numSyms, o->firstGlobal), uint32_t(i)};
  }
  for (size_t i = 0; i < ns; ++i)
    order[no + i] = {ctx.shared[i]->numDyn, uint32_t(no + i)};
  std::sort(order.begin(), order.end(), [](const auto &a, const auto &b) {
    return a.first != b.first ? a.first > b.first : a.second < b.second;
  });
  for (size_t expected = total / 8;; expected = total) {
    ctx.names = std::make_unique<NameTable>(std::max<size_t>(expected, 1024));
    ctx.names->prefault(*ctx.pool);
    std::atomic<bool> ok{true};
    ctx.pool->forEach(no + ns, [&](size_t k) {
      if (!ok.load(std::memory_order_relaxed))
        return;
      size_t i = order[k].second;
      bool r = i < no ? internObject(ctx.objects[i])
                      : internShared(ctx.shared[i - no]);
      if (!r)
        ok = false;
    }, 1);
    if (ctx.opt.timing)
      fprintf(stderr, "    names: total %zu expected %zu ok %d unique %u\n", total,
              expected, int(ok.load()), ctx.names->size());
    if (ok || expected == total)
      break;
  }
  mark("intern");
  ctx.numNames = ctx.names->size();
  const uint32_t n = ctx.numNames;
  Pool &pool = *ctx.pool;
  ctx.provider = bigArray<std::atomic<uint64_t>>(pool, n);
  ctx.objDef = bigArray<std::atomic<uint64_t>>(pool, n);
  ctx.shDef = bigArray<std::atomic<uint64_t>>(pool, n);
  ctx.flags = bigArray<std::atomic<uint16_t>>(pool, n);
  ctx.pool->forEach(n, [&](size_t i) {
    ctx.provider[i].store(None64, std::memory_order_relaxed);
    ctx.objDef[i].store(None64, std::memory_order_relaxed);
    ctx.shDef[i].store(None64, std::memory_order_relaxed);
    ctx.flags[i].store(0, std::memory_order_relaxed);
  });

  mark("arrays");
  // Providers: a direct object defining the name, else the first archive
  // member or shared library by position.
  constexpr uint64_t Ranked = 1ull << 62;
  ctx.pool->forEach(no + ns, [&](size_t i) {
    if (i < no) {
      ObjectFile *o = ctx.objects[i];
      const uint64_t key = (o->lazy ? Ranked : 0) | (uint64_t(o->pos) << 32);
      for (uint32_t k = o->firstGlobal; k < o->numSyms; ++k) {
        const Elf64_Sym &s = o->syms[k];
        if (s.st_shndx == SHN_UNDEF)
          continue;
        if (s.st_shndx == SHN_COMMON)
          fatal(o->name + ": common symbols are not supported yet");
        atomicMin(ctx.provider[o->nameIds[k - o->firstGlobal]], key | k);
      }
    } else {
      SharedFile *so = ctx.shared[i - no];
      const uint64_t key = Ranked | (uint64_t(so->pos) << 32);
      for (uint32_t k = 1; k < so->numDyn; ++k)
        if (so->nameIds[k] != UINT32_MAX && so->dynsyms[k].st_shndx != SHN_UNDEF)
          atomicMin(ctx.provider[so->nameIds[k]], key | k);
    }
  }, 4);

  mark("providers");
  // Map position -> object for providers.
  vector<ObjectFile *> byPos(no + ns, nullptr);
  for (ObjectFile *o : ctx.objects)
    byPos[o->pos] = o;

  // Extract archive members reachable from strong undefined references.
  vector<ObjectFile *> frontier;
  for (ObjectFile *o : ctx.objects)
    if (!o->lazy) {
      o->live = 1;
      frontier.push_back(o);
    }
  vector<vector<ObjectFile *>> found(ctx.pool->size());
  auto extractFor = [&](uint32_t id) {
    uint64_t p = ctx.provider[id].load(std::memory_order_relaxed);
    if (p == None64 || !(p & Ranked))
      return;
    ObjectFile *def = byPos[(p >> 32) & 0x3fffffff];
    if (!def || !def->lazy)
      return;
    uint8_t zero = 0;
    if (def->live.compare_exchange_strong(zero, 1))
      found[Pool::self()].push_back(def);
  };
  // Shared libraries' undefined references extract members too.
  for (SharedFile *so : ctx.shared)
    for (uint32_t k = 1; k < so->numDyn; ++k)
      if (so->nameIds[k] != UINT32_MAX && so->dynsyms[k].st_shndx == SHN_UNDEF &&
          ELF64_ST_BIND(so->dynsyms[k].st_info) != STB_WEAK)
        extractFor(so->nameIds[k]);
  for (auto &f : found)
    frontier.insert(frontier.end(), f.begin(), f.end()), f.clear();
  while (!frontier.empty()) {
    ctx.pool->forEach(frontier.size(), [&](size_t i) {
      ObjectFile *o = frontier[i];
      for (uint32_t k = o->firstGlobal; k < o->numSyms; ++k) {
        const Elf64_Sym &s = o->syms[k];
        if (s.st_shndx == SHN_UNDEF && ELF64_ST_BIND(s.st_info) != STB_WEAK)
          extractFor(o->nameIds[k - o->firstGlobal]);
      }
    }, 2);
    frontier.clear();
    for (auto &f : found)
      frontier.insert(frontier.end(), f.begin(), f.end()), f.clear();
  }
  {
    vector<ObjectFile *> live;
    for (ObjectFile *o : ctx.objects)
      if (o->live.load())
        live.push_back(o);
    ctx.objects.swap(live);
  }
  mark("extract");
  {
    vector<size_t> base(ctx.objects.size() + 1, 0);
    for (size_t i = 0; i < ctx.objects.size(); ++i)
      base[i + 1] = base[i] + ctx.objects[i]->numShdrs;
    auto *secs = bigArray<SectionState>(*ctx.pool, base.back() + 1);
    auto *relaOf = bigArray<uint32_t>(*ctx.pool, base.back() + 1);
    ctx.pool->forEach(ctx.objects.size(), [&](size_t i) {
      initSections(ctx.objects[i], secs + base[i], relaOf + base[i]);
    }, 4);
  }
  mark("initSections");

  // Definitions: first global definition by position, else first weak one,
  // else the first shared library definition.
  ctx.pool->forEach(ctx.objects.size() + ns, [&](size_t i) {
    if (i < ctx.objects.size()) {
      ObjectFile *o = ctx.objects[i];
      for (uint32_t k = o->firstGlobal; k < o->numSyms; ++k) {
        const Elf64_Sym &s = o->syms[k];
        uint32_t id = o->nameIds[k - o->firstGlobal];
        if (s.st_shndx == SHN_UNDEF) {
          uint16_t f = ELF64_ST_BIND(s.st_info) == STB_WEAK ? WeakRef : StrongRef;
          if (ELF64_ST_VISIBILITY(s.st_other) != STV_DEFAULT)
            f |= HiddenRef;
          atomicOr(ctx.flags[id], f);
          continue;
        }
        uint64_t cls = ELF64_ST_BIND(s.st_info) == STB_WEAK ? 1 : 0;
        atomicMin(ctx.objDef[id], (cls << 62) | (uint64_t(i) << 32) | k);
        if (!cls && s.st_shndx != SHN_COMMON) {
          std::atomic<uint16_t> &f = ctx.flags[id];
          if ((f.load(std::memory_order_relaxed) & StrongDef) ||
              (f.fetch_or(StrongDef, std::memory_order_relaxed) & StrongDef))
            atomicOr(f, uint16_t(DupCandidate));
        }
      }
    } else {
      SharedFile *so = ctx.shared[i - ctx.objects.size()];
      for (uint32_t k = 1; k < so->numDyn; ++k) {
        uint32_t id = so->nameIds[k];
        if (id == UINT32_MAX)
          continue;
        // A definition an executable shares a name with a shared library
        // is exported, so that it takes precedence over the library's.
        atomicOr(ctx.flags[id], uint16_t(SharedRef));
        if (so->dynsyms[k].st_shndx != SHN_UNDEF)
          atomicMin(ctx.shDef[id],
                    (uint64_t(i - ctx.objects.size()) << 32) | k);
      }
    }
  }, 4);

  ctx.syms = bigArray<Symbol>(*ctx.pool, n);
  ctx.defTarget = bigArray<std::pair<uint32_t, uint32_t>>(*ctx.pool, n);
  ctx.pool->forEach(n, [&](size_t id) {
    Symbol &s = *new (&ctx.syms[id]) Symbol;
    uint64_t d = ctx.objDef[id].load(std::memory_order_relaxed);
    ctx.defTarget[id] = {UINT32_MAX, 0};
    if (d != None64) {
      s.kind = Symbol::Object;
      s.file = (d >> 32) & 0x3fffffff;
      s.index = uint32_t(d);
      const Elf64_Sym &def = ctx.objects[s.file]->syms[s.index];
      s.ifunc = ELF64_ST_TYPE(def.st_info) == STT_GNU_IFUNC;
      if (def.st_shndx != SHN_UNDEF && def.st_shndx < SHN_LORESERVE)
        ctx.defTarget[id] = {s.file, def.st_shndx};
      return;
    }
    d = ctx.shDef[id].load(std::memory_order_relaxed);
    if (d != None64) {
      s.kind = Symbol::Shared;
      s.file = uint32_t(d >> 32);
      s.index = uint32_t(d);
    }
  });
  mark("defs");
  ctx.names->names(ctx.nameStr);
  mark("nameStr");
}

// ================================================================ phase 3: GC

// COMDAT: the first live object by position keeps each group.
// Two global definitions of a name are an error unless all but one sit in
// discarded COMDAT groups; the full backend reports it.
void checkDuplicates() {
  STEP("duplicates");
  vector<vector<uint32_t>> found(ctx.pool->size());
  ctx.pool->forEach(ctx.objects.size(), [&](size_t fi) {
    ObjectFile *o = ctx.objects[fi];
    auto &out = found[Pool::self()];
    for (uint32_t k = o->firstGlobal; k < o->numSyms; ++k) {
      const Elf64_Sym &s = o->syms[k];
      if (s.st_shndx == SHN_UNDEF || s.st_shndx == SHN_COMMON ||
          ELF64_ST_BIND(s.st_info) == STB_WEAK)
        continue;
      uint32_t id = o->nameIds[k - o->firstGlobal];
      if (!(ctx.flags[id].load(std::memory_order_relaxed) & DupCandidate))
        continue;
      if (s.st_shndx < SHN_LORESERVE && o->secs[s.st_shndx].live.load() == 2)
        continue;
      out.push_back(id);
    }
  }, 8);
  vector<uint32_t> all;
  for (auto &v : found)
    all.insert(all.end(), v.begin(), v.end());
  std::sort(all.begin(), all.end());
  auto dup = std::adjacent_find(all.begin(), all.end());
  if (dup != all.end())
    fatal("duplicate symbol: " + string(ctx.nameStr[*dup]));
}

// Links the members of a kept group cyclically; a group is live or dead as
// a whole.
void linkGroupMembers(ObjectFile *o, uint32_t group) {
  auto *words = reinterpret_cast<const uint32_t *>(o->secData(group));
  size_t count = o->shdrs[group].sh_size / 4;
  uint32_t first = 0, prev = 0;
  for (size_t k = 1; k < count; ++k) {
    uint32_t m = words[k];
    if (m == 0 || m >= o->numShdrs)
      continue;
    if (!first)
      first = m;
    else
      o->secs[prev].groupNext = m;
    prev = m;
  }
  if (prev && prev != first)
    o->secs[prev].groupNext = first;
}

void selectComdats() {
  STEP("comdat");
  size_t total = 0;
  for (ObjectFile *o : ctx.objects)
    total += o->groups.size();
  if (!total)
    return;
  // Group ids: UINT32_MAX for a group that is not a COMDAT.
  // Group signatures get dense ids: a global signature symbol reuses its
  // name id, other signatures are interned after them. The first object by
  // position owns each group.
  const uint32_t n = ctx.numNames;
  std::atomic<size_t> localSigs{0};
  ctx.pool->forEach(ctx.objects.size(), [&](size_t fi) {
    ObjectFile *o = ctx.objects[fi];
    o->groupIds.resize(o->groups.size());
    size_t local = 0;
    for (size_t g = 0; g < o->groups.size(); ++g) {
      const Elf64_Shdr &sh = o->shdrs[o->groups[g]];
      auto *words = reinterpret_cast<const uint32_t *>(o->secData(o->groups[g]));
      if (sh.sh_size < 4 || !(words[0] & GRP_COMDAT))
        o->groupIds[g] = UINT32_MAX;
      else if (sh.sh_info >= o->firstGlobal)
        o->groupIds[g] = o->nameIds[sh.sh_info - o->firstGlobal];
      else
        o->groupIds[g] = UINT32_MAX - 1, ++local;
    }
    if (local)
      localSigs += local;
  }, 4);
  std::unique_ptr<NameTable> sigs;
  if (localSigs)
    sigs = std::make_unique<NameTable>(localSigs.load());
  const size_t numIds = n + localSigs.load();
  std::atomic<uint32_t> *owner = bigArray<std::atomic<uint32_t>>(*ctx.pool, numIds);
  ctx.pool->forEach(numIds, [&](size_t i) {
    owner[i].store(UINT32_MAX, std::memory_order_relaxed);
  });
  ctx.pool->forEach(ctx.objects.size(), [&](size_t fi) {
    ObjectFile *o = ctx.objects[fi];
    for (size_t g = 0; g < o->groups.size(); ++g) {
      uint32_t id = o->groupIds[g];
      if (id == UINT32_MAX)
        continue;
      if (id == UINT32_MAX - 1) {
        const char *sig = o->symName(o->shdrs[o->groups[g]].sh_info);
        id = o->groupIds[g] = n + sigs->intern(sig, strlen(sig));
      }
      atomicMin(owner[id], uint32_t(fi));
    }
  }, 4);
  ctx.pool->forEach(ctx.objects.size(), [&](size_t fi) {
    ObjectFile *o = ctx.objects[fi];
    for (size_t g = 0; g < o->groups.size(); ++g) {
      uint32_t id = o->groupIds[g];
      if (id == UINT32_MAX || owner[id].load(std::memory_order_relaxed) == fi) {
        linkGroupMembers(o, o->groups[g]);
        continue;
      }
      // Discard members: mark them dead forever (live = 2).
      const Elf64_Shdr &sh = o->shdrs[o->groups[g]];
      auto *words = reinterpret_cast<const uint32_t *>(o->secData(o->groups[g]));
      for (size_t k = 1; k < sh.sh_size / 4; ++k)
        if (words[k] < o->numShdrs)
          o->secs[words[k]].live.store(2, std::memory_order_relaxed);
    }
  }, 4);
}

// Section of a symbol reached through a relocation, or 0.
struct Target {
  ObjectFile *file;
  uint32_t sec;
};

Target symbolSection(ObjectFile *o, uint32_t symIdx) {
  if (symIdx < o->firstGlobal) {
    uint16_t shndx = o->syms[symIdx].st_shndx;
    if (shndx == SHN_UNDEF || shndx >= SHN_LORESERVE)
      return {nullptr, 0};
    return {o, shndx};
  }
  auto [file, sec] = ctx.defTarget[o->nameIds[symIdx - o->firstGlobal]];
  if (file == UINT32_MAX)
    return {nullptr, 0};
  return {ctx.objects[file], sec};
}

inline uint32_t rd32(const uint8_t *p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}

void parseEhFrame(ObjectFile *o) {
  if (!o->ehSec || o->secs[o->ehSec].live.load() == 2)
    return;
  const uint8_t *d = o->secData(o->ehSec);
  const size_t size = o->shdrs[o->ehSec].sh_size;
  auto [rels, n] = o->relas(o->ehSec);
  // Absolute pointers in .eh_frame would need dynamic relocations.
  for (size_t k = 0; k < n; ++k) {
    uint32_t t = ELF64_R_TYPE(rels[k].r_info);
    if (t != R_X86_64_NONE && t != R_X86_64_PC32 && t != R_X86_64_PC64)
      fatal(o->name + ": unsupported relocation in .eh_frame");
  }
  size_t ri = 0;
  vector<std::pair<uint32_t, uint32_t>> cies;
  for (size_t off = 0; off + 4 <= size;) {
    uint32_t len = rd32(d + off);
    if (len == 0) {
      off += 4;
      continue;
    }
    if (len == 0xffffffff)
      fatal(o->name + ": 64-bit .eh_frame records are not supported");
    EhPiece p;
    p.off = off;
    p.size = len + 4;
    p.relBegin = ri;
    while (ri < n && rels[ri].r_offset < off + p.size)
      ++ri;
    p.relEnd = ri;
    uint32_t id = rd32(d + off + 4);
    p.cie = UINT32_MAX;
    p.target = 0;
    if (id == 0) {
      cies.push_back({uint32_t(off), uint32_t(o->eh.size())});
    } else {
      uint32_t cieOff = off + 4 - id;
      for (auto &c : cies)
        if (c.first == cieOff)
          p.cie = c.second;
      if (p.cie == UINT32_MAX)
        fatal(o->name + ": bad CIE pointer in .eh_frame");
      if (p.relBegin < p.relEnd && rels[p.relBegin].r_offset == off + 8) {
        Target t = symbolSection(o, ELF64_R_SYM(rels[p.relBegin].r_info));
        if (t.file == o)
          p.target = t.sec;
      }
      if (p.target)
        o->fdeByTarget.push_back({p.target, uint32_t(o->eh.size())});
    }
    o->eh.push_back(p);
    off += p.size;
  }
  std::sort(o->fdeByTarget.begin(), o->fdeByTarget.end());
}

// Debug sections are kept without keeping what they describe alive. Their
// relocations are checked here, before any output exists.
void markDebugLive() {
  ctx.pool->forEach(ctx.objects.size(), [&](size_t fi) {
    ObjectFile *o = ctx.objects[fi];
    for (uint32_t i : o->debugSecs) {
      uint8_t zero = 0;
      o->secs[i].live.compare_exchange_strong(zero, 1);
      auto [rels, n] = o->relas(i);
      for (size_t k = 0; k < n; ++k)
        switch (ELF64_R_TYPE(rels[k].r_info)) {
        case R_X86_64_NONE:
        case R_X86_64_32:
        case R_X86_64_64:
        case R_X86_64_DTPOFF32:
        case R_X86_64_DTPOFF64:
          break;
        default:
          fatal(o->name + ": unsupported relocation type " +
                std::to_string(ELF64_R_TYPE(rels[k].r_info)) + " in " +
                o->secName(i));
        }
    }
  }, 8);
}

void markLive() {
  STEP("markLive");
  markTime = Clock::now();
  ctx.pool->forEach(ctx.objects.size(),
                    [&](size_t i) { parseEhFrame(ctx.objects[i]); });
  mark("parseEhFrame");
  vector<vector<std::pair<ObjectFile *, uint32_t>>> stacks(ctx.pool->size());
  auto push = [&](ObjectFile *o, uint32_t sec, unsigned w) {
    if (!o || sec == 0 || sec >= o->numShdrs)
      return;
    uint8_t zero = 0;
    if (o->secs[sec].live.load(std::memory_order_relaxed) == 0 &&
        o->secs[sec].live.compare_exchange_strong(zero, 1,
                                                  std::memory_order_relaxed))
      stacks[w].push_back({o, sec});
  };
  if (!ctx.opt.gcSections) {
    ctx.pool->forEach(ctx.objects.size(), [&](size_t fi) {
      ObjectFile *o = ctx.objects[fi];
      for (uint32_t i = 1; i < o->numShdrs; ++i) {
        uint8_t zero = 0;
        if (o->shdrs[i].sh_flags & SHF_ALLOC)
          o->secs[i].live.compare_exchange_strong(zero, 1);
      }
    });
    markDebugLive();
    return;
  }
  ctx.pool->forEach(ctx.objects.size(), [&](size_t fi) {
    ObjectFile *o = ctx.objects[fi];
    for (uint32_t i : o->roots)
      push(o, i, Pool::self());
  }, 16);
  auto rootName = [&](const char *name) {
    uint32_t id = ctx.names->find(name, strlen(name));
    if (id == UINT32_MAX)
      return;
    const Symbol &s = ctx.syms[id];
    if (s.kind == Symbol::Object) {
      ObjectFile *d = ctx.objects[s.file];
      push(d, d->syms[s.index].st_shndx, 0);
    }
  };
  // __start_/__stop_ references retain __libc_* sections, which only
  // static glibc links have.
  for (uint32_t id = 0; id < ctx.numNames; ++id)
    if (ctx.syms[id].kind == Symbol::Linker &&
        (ctx.nameStr[id].rfind("__start___libc_", 0) == 0 ||
         ctx.nameStr[id].rfind("__stop___libc_", 0) == 0))
      fatal("reference to " + string(ctx.nameStr[id]));
  rootName("_start");
  rootName("_init");
  rootName("_fini");
  // Exported definitions are live.
  ctx.pool->forEach(ctx.numNames, [&](size_t id) {
    if (isExported(uint32_t(id))) {
      ObjectFile *d = ctx.objects[ctx.syms[id].file];
      push(d, d->syms[ctx.syms[id].index].st_shndx, Pool::self());
    }
  });

  // Parallel traversal in rounds. Each task also follows a bounded number of
  // the sections it discovers right away, which keeps the rounds few.
  auto process = [&](ObjectFile *o, uint32_t sec, unsigned w) {
    auto [rels, n] = o->relas(sec);
    for (size_t k = 0; k < n; ++k) {
      uint32_t si = ELF64_R_SYM(rels[k].r_info);
      if (si == 0)
        continue;
      Target t = symbolSection(o, si);
      push(t.file, t.sec, w);
    }
    // A group is live or dead as a whole.
    if (uint32_t next = o->secs[sec].groupNext)
      push(o, next, w);
    // The FDEs of a live section keep their LSDA and personality alive.
    if (!o->fdeByTarget.empty()) {
      auto [ehRels, ehN] = o->relas(o->ehSec);
      (void)ehN;
      auto markRange = [&](uint32_t b, uint32_t e) {
        for (uint32_t k = b; k < e; ++k) {
          uint32_t si = ELF64_R_SYM(ehRels[k].r_info);
          if (si == 0)
            continue;
          Target t = symbolSection(o, si);
          push(t.file, t.sec, w);
        }
      };
      auto it = std::lower_bound(o->fdeByTarget.begin(), o->fdeByTarget.end(),
                                 std::make_pair(sec, 0u));
      for (; it != o->fdeByTarget.end() && it->first == sec; ++it) {
        const EhPiece &f = o->eh[it->second];
        markRange(f.relBegin + 1, f.relEnd);
        const EhPiece &c = o->eh[f.cie];
        markRange(c.relBegin, c.relEnd);
      }
    }
  };
  mark("roots");
  int rounds = 0;
  vector<std::pair<ObjectFile *, uint32_t>> frontier;
  for (auto &st : stacks)
    frontier.insert(frontier.end(), st.begin(), st.end()), st.clear();
  while (!frontier.empty()) {
    ctx.pool->forEach(frontier.size(), [&](size_t i) {
      const unsigned w = Pool::self();
      auto &local = stacks[w];
      const size_t keep = local.size();
      process(frontier[i].first, frontier[i].second, w);
      for (int budget = 0; budget < 256 && local.size() > keep; ++budget) {
        auto [o, sec] = local.back();
        local.pop_back();
        process(o, sec, w);
      }
    }, 16);
    frontier.clear();
    for (auto &st : stacks)
      frontier.insert(frontier.end(), st.begin(), st.end()), st.clear();
    ++rounds;
  }
  if (ctx.opt.timing)
    fprintf(stderr, "    rounds %d\n", rounds);
  mark("traverse");
  markDebugLive();
}

// ================================================================ phase 4: scan

enum Kind : uint8_t {
  K_Skip,
  K_Pc32,
  K_Pc64,
  K_Plt32,
  K_GotRelax,
  K_GotPc,
  K_GotTpPc,
  K_IeToLe,
  K_TpOff32,
  K_Abs64,
  K_Abs32,
  K_Size32,
  K_Size64,
  K_GotOff64,
  K_GotPc32,
  K_GdToLe,
  K_GdToIe,
  K_LdToLe,
  K_DtpOff32,
  K_DtpOff64,
  K_TlsGd, // shared library: general-dynamic GOT pair
  K_TlsLd, // shared library: the module's GOT pair
};

struct RelInfo {
  Kind kind = K_Skip;
  bool global = false;
  bool imp = false;
  uint32_t id = 0;
};

// The single relocation classifier used by both scanning and writing.
RelInfo classify(const ObjectFile *o, const uint8_t *secData,
                 const Elf64_Rela &r) {
  const uint32_t type = ELF64_R_TYPE(r.r_info), si = ELF64_R_SYM(r.r_info);
  RelInfo ri;
  ri.global = si >= o->firstGlobal;
  bool undef = false;
  if (ri.global) {
    ri.id = o->nameIds[si - o->firstGlobal];
    ri.imp = isPreemptible(ri.id);
    undef = ctx.syms[ri.id].kind == Symbol::Undefined;
  }
  switch (type) {
  case R_X86_64_NONE:
    return ri;
  case R_X86_64_PC32:
    ri.kind = K_Pc32;
    return ri;
  case R_X86_64_PC64:
    ri.kind = K_Pc64;
    return ri;
  case R_X86_64_PLT32:
    ri.kind = K_Plt32;
    return ri;
  case R_X86_64_GOTPCREL:
    ri.kind = K_GotPc;
    return ri;
  case R_X86_64_GOTPCRELX:
  case R_X86_64_REX_GOTPCRELX: {
    ri.kind = K_GotPc;
    if (ri.imp || undef || r.r_addend != -4 || r.r_offset < 2)
      return ri;
    if (ri.global && ctx.syms[ri.id].kind == Symbol::Object) {
      const Symbol &s = ctx.syms[ri.id];
      if (ELF64_ST_TYPE(ctx.objects[s.file]->syms[s.index].st_info) ==
          STT_GNU_IFUNC)
        return ri;
    }
    const uint8_t op = secData[r.r_offset - 2], modrm = secData[r.r_offset - 1];
    if (op == 0x8b || (op == 0xff && (modrm == 0x15 || modrm == 0x25)))
      ri.kind = K_GotRelax;
    return ri;
  }
  case R_X86_64_GOTTPOFF:
    ri.kind = ri.imp || ctx.opt.shared ? K_GotTpPc : K_IeToLe;
    return ri;
  case R_X86_64_TPOFF32:
    ri.kind = K_TpOff32;
    return ri;
  case R_X86_64_64:
    ri.kind = K_Abs64;
    return ri;
  case R_X86_64_32:
  case R_X86_64_32S:
    ri.kind = K_Abs32;
    return ri;
  case R_X86_64_SIZE32:
    ri.kind = K_Size32;
    return ri;
  case R_X86_64_SIZE64:
    ri.kind = K_Size64;
    return ri;
  case R_X86_64_GOTOFF64:
    ri.kind = K_GotOff64;
    return ri;
  case R_X86_64_GOTPC32:
    ri.kind = K_GotPc32;
    return ri;
  case R_X86_64_TLSGD:
    ri.kind = ctx.opt.shared ? K_TlsGd : ri.imp ? K_GdToIe : K_GdToLe;
    return ri;
  case R_X86_64_TLSLD:
    ri.kind = ctx.opt.shared ? K_TlsLd : K_LdToLe;
    return ri;
  case R_X86_64_DTPOFF32:
    ri.kind = K_DtpOff32;
    return ri;
  case R_X86_64_DTPOFF64:
    ri.kind = K_DtpOff64;
    return ri;
  }
  fatal(o->name + ": unsupported relocation type " + std::to_string(type));
}


uint8_t *relKinds; // classify() results, reused when writing

// General- and local-dynamic TLS sequences are rewritten in place, which is
// only valid for the exact code sequences compilers emit for them.
void checkTlsSequence(const ObjectFile *o, uint32_t sec, const Elf64_Rela *rels,
                      size_t n, size_t k, Kind kind) {
  const uint8_t *d = o->secData(sec);
  const uint64_t size = o->shdrs[sec].sh_size, off = rels[k].r_offset;
  bool ok;
  uint64_t call;
  if (kind == K_LdToLe) {
    ok = off >= 3 && off + 6 <= size && memcmp(d + off - 3, "\x48\x8d\x3d", 3) == 0 &&
         (d[off + 4] == 0xe8 || (d[off + 4] == 0xff && d[off + 5] == 0x15));
    call = d[off + 4] == 0xe8 ? off + 5 : off + 6;
  } else {
    ok = off >= 4 && off + 12 <= size &&
         memcmp(d + off - 4, "\x66\x48\x8d\x3d", 4) == 0 &&
         memcmp(d + off + 4, "\x66\x66\x48\xe8", 4) == 0;
    call = off + 8;
  }
  if (ok && k + 1 < n && rels[k + 1].r_offset == call) {
    uint32_t si = ELF64_R_SYM(rels[k + 1].r_info);
    ok = si >= o->firstGlobal && strcmp(o->symName(si), "__tls_get_addr") == 0;
  } else {
    ok = false;
  }
  if (!ok)
    fatal(o->name + ": unsupported dynamic TLS code sequence in " +
          o->secName(sec));
}

void scanRelocations() {
  STEP("scan");
  vector<uint64_t> base(ctx.objects.size() + 1, 0);
  ctx.pool->forEach(ctx.objects.size(), [&](size_t fi) {
    ObjectFile *o = ctx.objects[fi];
    uint64_t n = 0;
    for (uint32_t sec = 1; sec < o->numShdrs; ++sec)
      if (o->secs[sec].osec != 0 && o->secs[sec].live.load() == 1 &&
          o->relaOf[sec] && (o->shdrs[sec].sh_flags & SHF_ALLOC))
        n += o->shdrs[o->relaOf[sec]].sh_size / sizeof(Elf64_Rela);
    base[fi + 1] = n;
  }, 16);
  for (size_t i = 0; i < ctx.objects.size(); ++i)
    base[i + 1] += base[i];
  if (base.back() >= UINT32_MAX)
    fatal("too many relocations");
  relKinds = static_cast<uint8_t *>(malloc(base.back() + 1));
  ctx.pool->forEach(ctx.objects.size(), [&](size_t fi) {
    ObjectFile *o = ctx.objects[fi];
    uint32_t next = base[fi];
    for (uint32_t sec = 1; sec < o->numShdrs; ++sec) {
      // Debug sections need no dynamic relocations, GOT or PLT entries.
      if (o->secs[sec].osec == 0 || o->secs[sec].live.load() != 1 ||
          !(o->shdrs[sec].sh_flags & SHF_ALLOC))
        continue;
      PlacedSection &ss = placedOf(o, sec);
      auto [rels, n] = o->relas(sec);
      const uint8_t *data = o->secData(sec);
      ss.kindBase = next;
      uint8_t *kinds = relKinds + next;
      next += n;
      for (size_t k = 0; k < n; ++k) {
        const Elf64_Rela &r = rels[k];
        RelInfo ri = classify(o, data, r);
        kinds[k] = ri.kind;
        // Functions selected at load time need IRELATIVE relocations.
        if (ri.global ? ctx.syms[ri.id].ifunc
                      : ELF64_ST_TYPE(o->syms[ELF64_R_SYM(r.r_info)].st_info) ==
                            STT_GNU_IFUNC)
          fatal(o->name + ": reference to a GNU indirect function");
        if (ri.kind == K_GdToIe || ri.kind == K_GdToLe || ri.kind == K_LdToLe) {
          checkTlsSequence(o, sec, rels, n, k, ri.kind);
          kinds[k + 1] = K_Skip;
        } else if (ri.kind == K_IeToLe) {
          // movq or addq of a RIP-relative GOT entry to a register.
          const uint64_t off = r.r_offset;
          if (off < 3 || !(data[off - 3] == 0x48 || data[off - 3] == 0x4c) ||
              !(data[off - 2] == 0x03 || data[off - 2] == 0x8b) ||
              (data[off - 1] & 0xc7) != 0x05)
            fatal(o->name + ": unsupported initial-exec TLS instruction in " +
                  o->secName(sec));
        }
        const uint32_t id = ri.id;
        switch (ri.kind) {
        case K_Pc32:
        case K_Pc64:
          if (ri.imp) {
            const Symbol &s = ctx.syms[id];
            if (ctx.opt.shared)
              fatal(o->name + ": PC-relative reference to preemptible " +
                    string(ctx.nameStr[id]) + "; recompile with -fPIC");
            if (s.kind != Symbol::Shared)
              fatal(o->name + ": PC-relative reference to undefined weak " +
                    string(ctx.nameStr[id]));
            const Elf64_Sym &ds = ctx.shared[s.file]->dynsyms[s.index];
            if (ELF64_ST_TYPE(ds.st_info) == STT_FUNC)
              atomicOr(ctx.flags[id],
                       uint16_t(NeedsPlt | NeedsDynsym | CanonicalPlt));
            else
              atomicOr(ctx.flags[id], uint16_t(NeedsCopy | NeedsDynsym));
          }
          break;
        case K_Plt32:
          if (ri.imp)
            atomicOr(ctx.flags[id], uint16_t(NeedsPlt | NeedsDynsym));
          break;
        case K_GotPc:
          if (!ri.global)
            fatal(o->name + ": GOT reference to a local symbol in " +
                  o->secName(sec));
          atomicOr(ctx.flags[id], uint16_t(NeedsGot | (ri.imp ? NeedsDynsym : 0)));
          break;
        case K_GotTpPc:
          if (ctx.opt.shared) {
            if (!ri.global)
              fatal(o->name + ": initial-exec TLS reference to a local symbol");
            atomicOr(ctx.flags[id],
                     uint16_t(NeedsGotTp | (ri.imp ? NeedsDynsym : 0)));
            break;
          }
          if (ctx.syms[id].kind != Symbol::Shared)
            fatal(o->name + ": TLS reference to undefined weak " +
                  string(ctx.nameStr[id]));
          atomicOr(ctx.flags[id], uint16_t(NeedsGotTp | NeedsDynsym));
          break;
        case K_TlsGd:
          if (!ri.global)
            fatal(o->name + ": general-dynamic TLS reference to a local symbol");
          atomicOr(ctx.flags[id],
                   uint16_t(NeedsTlsGd | (ri.imp ? NeedsDynsym : 0)));
          break;
        case K_TlsLd:
          needsTlsLd.store(true, std::memory_order_relaxed);
          break;
        case K_GdToIe:
          if (ctx.syms[id].kind != Symbol::Shared)
            fatal(o->name + ": TLS reference to undefined weak " +
                  string(ctx.nameStr[id]));
          atomicOr(ctx.flags[id], uint16_t(NeedsGotTp | NeedsDynsym));
          ++k; // the __tls_get_addr call is rewritten away
          break;
        case K_GdToLe:
        case K_LdToLe:
          ++k;
          break;
        case K_Abs64: {
          const bool needsDyn =
              ri.imp || (ctx.isPic() &&
                         !(ri.global && ctx.syms[id].kind == Symbol::Undefined));
          // The dynamic loader only writes to writable segments.
          if (needsDyn && !(o->shdrs[sec].sh_flags & SHF_WRITE))
            fatal(o->name + ": dynamic relocation in read-only section " +
                  o->secName(sec));
          if (ri.imp) {
            atomicOr(ctx.flags[id], uint16_t(NeedsDynsym));
            ++ss.numSym;
          } else if (needsDyn) {
            ++ss.numRel;
          }
          break;
        }
        case K_Abs32:
        case K_TpOff32:
        case K_GotOff64:
          if (ri.kind == K_TpOff32 && ctx.opt.shared)
            fatal(o->name + ": local-exec TLS in a shared library");
          if (ri.imp && ri.kind == K_Abs32 && !ctx.isPic())
            atomicOr(ctx.flags[id], uint16_t(NeedsCopy | NeedsDynsym));
          else if (ri.imp)
            fatal(o->name + ": non-PIC reference to shared symbol " +
                  string(ctx.nameStr[id]));
          if (ri.kind == K_Abs32 && ctx.isPic())
            fatal(o->name + ": R_X86_64_32 in a position-independent output; "
                            "recompile with -fPIE");
          break;
        default:
          break;
        }
      }
    }
  });
}

// ================================================================ phase 5: layout
string_view outputName(string_view n) {
  static const char *const prefixes[] = {
      ".text",       ".rodata", ".data.rel.ro", ".data",
      ".bss.rel.ro", ".bss",    ".tdata",       ".tbss",
      ".init_array", ".fini_array", ".preinit_array", ".gcc_except_table",
      ".ldata",      ".lrodata", ".lbss",       ".ctors", ".dtors"};
  for (const char *p : prefixes) {
    size_t l = strlen(p);
    if (n.size() >= l && n.compare(0, l, p) == 0 &&
        (n.size() == l || n[l] == '.'))
      return string_view(p, l);
  }
  return n;
}

bool isRelroName(string_view n) {
  return n == ".data.rel.ro" || n == ".bss.rel.ro" || n == ".init_array" ||
         n == ".fini_array" || n == ".preinit_array" || n == ".dynamic" ||
         n == ".got" || n == ".ctors" || n == ".dtors" || n == ".jcr";
}

int segClass(const OutputSection *s) {
  if (s->flags & SHF_EXECINSTR)
    return 1;
  if (!(s->flags & SHF_WRITE))
    return 0;
  if (ctx.opt.zRelro && s->relro)
    return 2;
  return 3;
}

int rankOf(const OutputSection *s) {
  const string &n = s->name;
  if (!(s->flags & SHF_ALLOC))
    return 1000 + (s->type == SHT_SYMTAB ? 1 : s->type == SHT_STRTAB ? 2 : 0);
  int cls = segClass(s);
  if (cls == 0) {
    if (n == ".interp")
      return 0;
    if (s->type == SHT_NOTE)
      return 1;
    static const char *const synth[] = {".dynsym",   ".gnu.version",
                                        ".gnu.version_d", ".gnu.version_r",
                                        ".gnu.hash", ".dynstr",
                                        ".rela.dyn", ".rela.plt"};
    for (int i = 0; i < 8; ++i)
      if (n == synth[i])
        return 2 + i;
    if (n == ".eh_frame_hdr")
      return 11;
    if (n == ".eh_frame")
      return 12;
    if (n == ".gcc_except_table")
      return 13;
    return 10;
  }
  if (cls == 1)
    return 100 + (n == ".init" ? 0 : n == ".plt" ? 1 : n == ".fini" ? 3 : 2);
  if (cls == 2)
    return 200 + ((s->flags & SHF_TLS) ? (s->type == SHT_NOBITS ? 1 : 0)
                  : n == ".dynamic"    ? 4
                  : n == ".got"        ? 5
                  : s->type == SHT_NOBITS ? 6
                                          : 2);
  if (s->flags & SHF_TLS)
    return 300 + (s->type == SHT_NOBITS ? 1 : 0);
  return 310 + (n == ".got.plt" ? 0 : s->type == SHT_NOBITS ? 2 : 1);
}

uint32_t initPriority(string_view n) {
  // .init_array.NNNNN / .fini_array.NNNNN; plain sections sort last.
  size_t dot = n.rfind('.');
  if (dot == 0 || dot == string_view::npos)
    return 65536;
  string_view tail = n.substr(dot + 1);
  if (tail.empty() || tail.size() > 5)
    return 65536;
  uint32_t v = 0;
  for (char c : tail) {
    if (c < '0' || c > '9')
      return 65536;
    v = v * 10 + (c - '0');
  }
  return v;
}

uint32_t elfHash(string_view s) {
  uint32_t h = 0;
  for (unsigned char c : s) {
    h = (h << 4) + c;
    uint32_t g = h & 0xf0000000;
    if (g)
      h ^= g >> 24;
    h &= ~g;
  }
  return h;
}

uint32_t gnuHash(string_view s) {
  uint32_t h = 5381;
  for (unsigned char c : s)
    h = h * 33 + c;
  return h;
}

struct Layout {
  vector<OutputSection *> all;     // creation order (index = osec id)
  vector<OutputSection *> ordered; // output order
  std::unordered_map<string_view, OutputSection *> byName;
  OutputSection *interp = nullptr, *dynsym = nullptr, *dynstr = nullptr,
                *gnuHashSec = nullptr, *relaDyn = nullptr, *relaPlt = nullptr,
                *plt = nullptr, *got = nullptr, *gotPlt = nullptr,
                *dynamic = nullptr, *buildId = nullptr;
  vector<Elf64_Phdr> phdrs;
  uint64_t base = 0, fileSize = 0, shoff = 0;
  uint64_t tlsAddr = 0, tlsMemsz = 0, tlsAlign = 1;
  uint64_t textEnd = 0, dataEnd = 0, bssStart = 0, end = 0;
  // dynamic
  vector<uint32_t> imports, exports; // name ids, .dynsym order
  vector<std::pair<uint32_t, uint32_t>> dynstrOff; // unused
  vector<uint32_t> nameDynstr;                    // per dynsym entry
  string dynstrData;
  vector<SharedFile *> neededLibs;
  vector<uint32_t> neededOff;
  uint32_t rpathOff = 0;
  // .rela.dyn: [section RELATIVE][GOT RELATIVE][GOT symbolic, COPY]
  // [section symbolic]
  uint32_t numSecRel = 0, numGotRel = 0, numGotSym = 0, numSecSym = 0;
  uint32_t numRelative = 0;
  Elf64_Rela *relaOut = nullptr;
  uint32_t gnuBuckets = 1, gnuSymOffset = 1;
  uint64_t gnuBloom = 0;
  vector<uint32_t> gnuBucketVals, gnuChain;
  string shstrtab;
  uint32_t shstrtabName = 0;
  OutputSection *copyRel = nullptr;
  struct Verneed {
    uint32_t file;
    struct Aux {
      uint32_t hash;
      uint16_t idx;
      uint32_t name;
    };
    vector<Aux> aux;
  };
  vector<Verneed> verneeds;
  // .gnu.version_d entries: the base version, then the named ones.
  struct Verdef {
    uint32_t hash, name;
  };
  vector<Verdef> verdefs;
  OutputSection *verdef = nullptr;
  vector<uint16_t> versyms;
  OutputSection *versym = nullptr, *verneed = nullptr;
  OutputSection *ehFrame = nullptr, *ehHdr = nullptr;
  OutputSection *comment = nullptr, *symtab = nullptr, *strtab = nullptr;
  string commentData;
  vector<uint32_t> symHead, symTail; // linker-defined locals, trailing globals
  uint32_t numLocals = 0;
  uint64_t tailStrBase = 0;
  uint32_t numFdes = 0;
  uint32_t numCopies = 0;
  uint64_t shstrtabOff = 0;
  vector<uint32_t> gotIds, gotTpIds, gotPltIds, tlsGdIds;
  uint32_t tlsLdSlot = UINT32_MAX;
  bool staticTls = false;
  uint32_t sonameOff = 0;
};
Layout L;

uint64_t tpoff(uint64_t va) {
  uint64_t end = L.tlsAddr + L.tlsMemsz;
  return va - end - ((-end) & (L.tlsAlign - 1));
}

OutputSection *newSection(string name, uint32_t type, uint64_t flags,
                          uint64_t align) {
  auto *s = new OutputSection;
  s->name = std::move(name);
  s->type = type;
  s->flags = flags;
  s->align = align;
  s->relro = isRelroName(s->name) || (flags & SHF_TLS);
  s->index = L.all.size();
  L.all.push_back(s);
  L.byName[s->name] = s;
  return s;
}

uint64_t defVA(const ObjectFile *o, uint32_t si) {
  const Elf64_Sym &s = o->syms[si];
  if (s.st_shndx == SHN_ABS)
    return s.st_value;
  if (s.st_shndx == SHN_UNDEF || s.st_shndx >= SHN_LORESERVE)
    return 0;
  const SectionState &ss = o->secs[s.st_shndx];
  if (ss.osec == 0)
    return 0;
  const PlacedSection &ps = ctx.placed[ss.placed];
  if (ps.merged)
    return mergedAddress(o, s.st_shndx,
                         ELF64_ST_TYPE(s.st_info) == STT_SECTION ? 0 : s.st_value);
  return ELF64_ST_TYPE(s.st_info) == STT_SECTION ? ps.va : ps.va + s.st_value;
}

uint64_t pltVA(uint32_t i) { return L.plt->addr + 16 * (i + 1); }
uint64_t gotVA(uint32_t i) { return L.got->addr + 8 * i; }

bool isLinkerDefined(string_view n) {
  static const char *const names[] = {
      "__ehdr_start",       "__executable_start", "_DYNAMIC",
      "_GLOBAL_OFFSET_TABLE_", "__init_array_start", "__init_array_end",
      "__fini_array_start", "__fini_array_end",   "__preinit_array_start",
      "__preinit_array_end", "_end",              "end",
      "_etext",             "etext",              "_edata",
      "edata",              "__bss_start",        "__GNU_EH_FRAME_HDR",
      "__rela_iplt_start",  "__rela_iplt_end",    "__dso_handle"};
  for (const char *x : names)
    if (n == x)
      return true;
  auto cident = [](string_view s) {
    if (s.empty())
      return false;
    for (char c : s)
      if (!(isalnum((unsigned char)c) || c == '_'))
        return false;
    return true;
  };
  if (n.rfind("__start_", 0) == 0)
    return cident(n.substr(8));
  if (n.rfind("__stop_", 0) == 0)
    return cident(n.substr(7));
  return false;
}

bool isCIdentifier(const char *n) {
  if (!*n || isdigit((unsigned char)*n))
    return false;
  for (; *n; ++n)
    if (!(isalnum((unsigned char)*n) || *n == '_'))
      return false;
  return true;
}

// Mergeable sections by file, found while sections are assigned.
vector<vector<uint32_t>> mergeCandidates;

void assignInputSections() {
  STEP("assignInputSections");
  // Per file: the distinct output sections it feeds, in first-use order.
  struct Use {
    string_view name;
    uint32_t firstType;
    uint64_t flags = 0, align = 1;
    bool anyData = false;
    uint32_t count = 0;
    uint32_t global = 0, base = 0;
  };
  vector<vector<Use>> uses(ctx.objects.size());
  mergeCandidates.assign(ctx.objects.size(), {});
  ctx.pool->forEach(ctx.objects.size(), [&](size_t fi) {
    ObjectFile *o = ctx.objects[fi];
    auto &u = uses[fi];
    uint32_t last = UINT32_MAX;
    size_t nextDebug = 0;
    for (uint32_t i = 1; i < o->numShdrs; ++i) {
      if (o->secs[i].live.load(std::memory_order_relaxed) != 1)
        continue;
      const Elf64_Shdr &sh = o->shdrs[i];
      if (!(sh.sh_flags & SHF_ALLOC)) {
        while (nextDebug < o->debugSecs.size() && o->debugSecs[nextDebug] < i)
          ++nextDebug;
        if (nextDebug == o->debugSecs.size() || o->debugSecs[nextDebug] != i)
          continue;
      }
      if ((sh.sh_flags & SHF_EXCLUDE) || sh.sh_type == SHT_GROUP ||
          i == o->ehSec)
        continue;
      string_view in = o->secName(i);
      if (in == ".note.gnu.property")
        continue;
      // __start_/__stop_ symbols may enumerate sections with C identifier
      // names, which therefore keep distinct addresses.
      if (in[0] != '.' && isCIdentifier(in.data()))
        o->secs[i].keepUnique.store(1, std::memory_order_relaxed);
      string_view name = outputName(in);
      uint32_t k;
      if (last != UINT32_MAX && u[last].name == name) {
        k = last;
      } else {
        for (k = 0; k < u.size() && u[k].name != name; ++k)
          ;
        if (k == u.size()) {
          Use nu;
          nu.name = name;
          nu.firstType = sh.sh_type;
          u.push_back(nu);
        }
        last = k;
      }
      Use &x = u[k];
      x.flags |= sh.sh_flags & (SHF_ALLOC | SHF_WRITE | SHF_EXECINSTR | SHF_TLS |
                                (sh.sh_flags & SHF_ALLOC
                                     ? 0
                                     : SHF_MERGE | SHF_STRINGS));
      x.align = std::max<uint64_t>(x.align, sh.sh_addralign);
      x.anyData |= sh.sh_type != SHT_NOBITS;
      ++x.count;
      o->secs[i].osec = k + 1; // local index for now
    }
  }, 4);
  vector<uint32_t> counts;
  for (size_t fi = 0; fi < uses.size(); ++fi)
    for (Use &x : uses[fi]) {
      OutputSection *os;
      auto it = L.byName.find(x.name);
      if (it == L.byName.end()) {
        uint32_t type = x.firstType;
        if (type != SHT_INIT_ARRAY && type != SHT_FINI_ARRAY &&
            type != SHT_PREINIT_ARRAY && type != SHT_NOTE && type != SHT_NOBITS)
          type = SHT_PROGBITS;
        os = newSection(string(x.name), type, x.flags, 1);
        if (x.flags & SHF_STRINGS)
          os->entsize = 1;
        counts.resize(L.all.size(), 0);
      } else {
        os = it->second;
        os->flags |= x.flags & (SHF_WRITE | SHF_EXECINSTR | SHF_TLS);
      }
      if (os->type == SHT_NOBITS && x.anyData)
        os->type = SHT_PROGBITS;
      os->align = std::max(os->align, x.align);
      x.global = os->index;
      x.base = counts[os->index];
      counts[os->index] += x.count;
    }
  for (OutputSection *os : L.all)
    os->members.resize(counts[os->index]);
  // PlacedSection indices, by file position; 0 stays unused.
  vector<uint32_t> placedBase(ctx.objects.size());
  uint32_t numPlaced = 1;
  for (size_t fi = 0; fi < uses.size(); ++fi) {
    placedBase[fi] = numPlaced;
    for (const Use &x : uses[fi])
      numPlaced += x.count;
  }
  ctx.placed = bigArray<PlacedSection>(*ctx.pool, numPlaced);
  ctx.numPlaced = numPlaced;
  ctx.pool->forEach(ctx.objects.size(), [&](size_t fi) {
    ObjectFile *o = ctx.objects[fi];
    auto &u = uses[fi];
    if (u.empty())
      return;
    uint32_t next = placedBase[fi];
    for (uint32_t i = 1; i < o->numShdrs; ++i) {
      uint32_t k = o->secs[i].osec;
      if (k == 0)
        continue;
      Use &x = u[k - 1];
      o->secs[i].osec = x.global;
      o->secs[i].placed = next++;
      L.all[x.global]->members[x.base++] = {o, i};
      // Merging shrinks allocated data by about a percent at a cost that
      // is not worth it here; debug strings are merged, as they repeat
      // across every object.
      const Elf64_Shdr &sh = o->shdrs[i];
      if ((sh.sh_flags & SHF_MERGE) && !(sh.sh_flags & SHF_ALLOC) &&
          sh.sh_entsize && sh.sh_type != SHT_NOBITS)
        mergeCandidates[fi].push_back(i);
    }
  }, 4);
  for (OutputSection *os : L.all) {
    if (os->name != ".init_array" && os->name != ".fini_array")
      continue;
    std::stable_sort(os->members.begin(), os->members.end(),
                     [](const auto &a, const auto &b) {
                       return initPriority(a.first->secName(a.second)) <
                              initPriority(b.first->secName(b.second));
                     });
  }
}

void assignOffsets(OutputSection *os) {
  uint64_t off = 0;
  for (auto [o, i] : os->members) {
    const Elf64_Shdr &sh = o->shdrs[i];
    PlacedSection &ps = placedOf(o, i);
    uint64_t start = alignTo(off, ps.align ? ps.align : sh.sh_addralign);
    ps.padBefore = start - off;
    off = start;
    ps.outOff = off;
    off += outputSize(o, i, ps);
  }
  if (off > UINT32_MAX)
    fatal("output section " + os->name + " exceeds 4 GiB");
  os->size = off;
}

// ================================================================ ICF
// Identical code folding with the full backend's semantics: sections with
// equal contents, equal output sections and equivalent relocations are folded
// into one, found by refining equivalence classes until they are stable.
namespace icf {

struct Candidate {
  ObjectFile *file;
  uint32_t sec;
  uint32_t placed;
  uint64_t hash;
};

// Per candidate, the placed sections its relocations refer to, in relocation
// order, with 0 for targets that are not sections; filled by run().
vector<uint32_t> targetPlaced;
vector<uint64_t> targetBase; // by placed index

void markAddressSignificant(ObjectFile *o, uint32_t sec, bool codeToo) {
  if (!o || !sec || sec >= o->numShdrs)
    return;
  if (!codeToo && (o->shdrs[sec].sh_flags & SHF_EXECINSTR))
    return;
  // Popular sections are marked by many workers; read before writing.
  std::atomic<uint8_t> &k = o->secs[sec].keepUnique;
  if (!k.load(std::memory_order_relaxed))
    k.store(1, std::memory_order_relaxed);
}

// Sections whose address must stay distinct: those of address-significant
// symbols, of exported symbols, and code whose unwind information names an
// LSDA or a personality routine.
void markKeepUnique() {
  const bool safe = ctx.opt.icf == 1;
  ctx.pool->forEach(ctx.numNames, [&](size_t id) {
    if (!isExported(uint32_t(id)))
      return;
    auto [file, sec] = ctx.defTarget[id];
    if (file != UINT32_MAX)
      markAddressSignificant(ctx.objects[file], sec, safe);
  });
  ctx.pool->forEach(ctx.objects.size(), [&](size_t fi) {
    ObjectFile *o = ctx.objects[fi];
    auto markSymbol = [&](uint32_t si) {
      if (si == 0 || si >= o->numSyms)
        return;
      Target t = symbolSection(o, si);
      markAddressSignificant(t.file, t.sec, safe);
    };
    if (o->addrsigSec) {
      const uint8_t *p = o->secData(o->addrsigSec);
      const uint8_t *e = p + o->shdrs[o->addrsigSec].sh_size;
      while (p < e) {
        uint64_t v = 0;
        unsigned shift = 0;
        while (p < e) {
          uint8_t b = *p++;
          v |= uint64_t(b & 0x7f) << shift;
          shift += 7;
          if (!(b & 0x80))
            break;
        }
        markSymbol(uint32_t(v));
      }
    } else {
      // Without an address-significance table every symbol may have its
      // address taken: that covers every section of this file that a symbol
      // names, and the definitions its references resolve to.
      for (uint32_t i = 1; i < o->numShdrs; ++i)
        markAddressSignificant(o, i, safe);
      for (uint32_t k = o->firstGlobal; k < o->numSyms; ++k)
        if (o->syms[k].st_shndx == SHN_UNDEF)
          markSymbol(k);
    }
    for (const EhPiece &f : o->eh)
      if (f.cie != UINT32_MAX && f.target &&
          (f.relEnd - f.relBegin > 1 ||
           o->eh[f.cie].relEnd > o->eh[f.cie].relBegin))
        o->secs[f.target].keepUnique.store(1, std::memory_order_relaxed);
  }, 4);
}

// Per output section: 1 if its members may be folded, 2 if only its
// read-only members may be.
vector<uint8_t> outputFoldable;

bool eligible(const ObjectFile *o, uint32_t sec) {
  const SectionState &ss = o->secs[sec];
  if (ss.osec == 0 || ss.live.load(std::memory_order_relaxed) != 1 ||
      ss.keepUnique.load(std::memory_order_relaxed))
    return false;
  const Elf64_Shdr &sh = o->shdrs[sec];
  if (!(sh.sh_flags & SHF_ALLOC) || sh.sh_type == SHT_NOBITS || !sh.sh_size ||
      ctx.placed[ss.placed].merged)
    return false;
  const uint8_t foldable = outputFoldable[ss.osec];
  if (!foldable || ((sh.sh_flags & SHF_WRITE) && foldable != 1))
    return false;
  // Sections with C identifier names are marked when they are assigned to
  // output sections.
  return true;
}

// What a relocation refers to, for comparisons: the same global name, or a
// defined location (section or absolute value).
struct RelTarget {
  enum Kind : uint8_t { Name, Section, Absolute, Other } kind;
  uint32_t id = 0;              // Name
  ObjectFile *file = nullptr;   // Section
  uint32_t sec = 0;             // Section
  uint64_t value = 0;           // Section offset or absolute value
};

RelTarget relTarget(ObjectFile *o, uint32_t si) {
  RelTarget t{RelTarget::Other};
  const Elf64_Sym *sym;
  ObjectFile *def = o;
  if (si >= o->firstGlobal) {
    t.id = o->nameIds[si - o->firstGlobal];
    const Symbol &s = ctx.syms[t.id];
    if (s.kind != Symbol::Object || isPreemptible(t.id)) {
      t.kind = RelTarget::Name;
      return t;
    }
    def = ctx.objects[s.file];
    sym = &def->syms[s.index];
  } else {
    sym = &o->syms[si];
  }
  if (sym->st_shndx == SHN_ABS) {
    t.kind = RelTarget::Absolute;
    t.value = sym->st_value;
  } else if (sym->st_shndx != SHN_UNDEF && sym->st_shndx < SHN_LORESERVE &&
             def->secs[sym->st_shndx].osec != 0) {
    t.kind = RelTarget::Section;
    t.file = def;
    t.sec = sym->st_shndx;
    t.value = ELF64_ST_TYPE(sym->st_info) == STT_SECTION ? 0 : sym->st_value;
  }
  return t;
}

bool sameSymbol(ObjectFile *a, uint32_t sa, ObjectFile *b, uint32_t sb) {
  const bool ga = sa >= a->firstGlobal, gb = sb >= b->firstGlobal;
  if (ga && gb)
    return a->nameIds[sa - a->firstGlobal] == b->nameIds[sb - b->firstGlobal];
  return !ga && !gb && a == b && sa == sb;
}

bool equalsConstant(const Candidate &a, const Candidate &b) {
  const Elf64_Shdr &ha = a.file->shdrs[a.sec], &hb = b.file->shdrs[b.sec];
  if (ha.sh_flags != hb.sh_flags || ha.sh_size != hb.sh_size ||
      a.file->secs[a.sec].osec != b.file->secs[b.sec].osec ||
      memcmp(a.file->secData(a.sec), b.file->secData(b.sec), ha.sh_size))
    return false;
  auto [ra, na] = a.file->relas(a.sec);
  auto [rb, nb] = b.file->relas(b.sec);
  if (na != nb)
    return false;
  for (size_t k = 0; k < na; ++k) {
    if (ra[k].r_offset != rb[k].r_offset ||
        ELF64_R_TYPE(ra[k].r_info) != ELF64_R_TYPE(rb[k].r_info))
      return false;
    const uint32_t sa = ELF64_R_SYM(ra[k].r_info), sb = ELF64_R_SYM(rb[k].r_info);
    const int64_t aa = ra[k].r_addend, ab = rb[k].r_addend;
    if (sameSymbol(a.file, sa, b.file, sb)) {
      if (aa != ab)
        return false;
      continue;
    }
    RelTarget ta = relTarget(a.file, sa), tb = relTarget(b.file, sb);
    if (ta.kind != tb.kind || ta.kind == RelTarget::Name ||
        ta.kind == RelTarget::Other)
      return false;
    if (ta.value + aa != tb.value + ab)
      return false;
  }
  return true;
}


uint32_t classOfPlaced(uint32_t placed, int slot) {
  if (placed == 0)
    return 0;
  const PlacedSection &ps = ctx.placed[placed];
  // Sections outside the candidates are only equal to themselves.
  return ps.icfCandidate ? ps.icfClass[slot] : (1u << 31) | placed;
}

bool equalsVariable(const Candidate &a, const Candidate &b, int cur) {
  auto [ra, n] = a.file->relas(a.sec);
  (void)ra;
  const uint32_t *ta = targetPlaced.data() + targetBase[a.placed];
  const uint32_t *tb = targetPlaced.data() + targetBase[b.placed];
  for (size_t k = 0; k < n; ++k) {
    if (ta[k] == tb[k])
      continue;
    // equalsConstant made both refer to sections, or to the same symbol.
    const uint32_t ca = classOfPlaced(ta[k], cur);
    if (ca == 0 || ca != classOfPlaced(tb[k], cur))
      return false;
  }
  return true;
}

void run() {
  STEP("icf");
  markTime = Clock::now();
  markKeepUnique();
  // Writable data may not be shared; .data.rel.ro is only written by the
  // dynamic loader. .init and .fini run as a whole.
  outputFoldable.assign(L.all.size(), 2);
  for (const OutputSection *os : L.all)
    if (os->name == ".init" || os->name == ".fini")
      outputFoldable[os->index] = 0;
    else if (os->name == ".data.rel.ro")
      outputFoldable[os->index] = 1;
  mark("icf keepUnique");
  // Candidates in position order.
  vector<vector<Candidate>> perFile(ctx.objects.size());
  ctx.pool->forEach(ctx.objects.size(), [&](size_t fi) {
    ObjectFile *o = ctx.objects[fi];
    for (uint32_t sec = 1; sec < o->numShdrs; ++sec) {
      if (!eligible(o, sec))
        continue;
      const Elf64_Shdr &sh = o->shdrs[sec];
      auto [rels, n] = o->relas(sec);
      // The ends of the contents make a selective hash; groups compare the
      // whole contents.
      const uint8_t *d = o->secData(sec);
      uint64_t h = (sh.sh_size <= 128
                        ? hashBulk(d, sh.sh_size)
                        : hashBulk(d, 64) ^ (hashBulk(d + sh.sh_size - 64, 64) *
                                             0x9E3779B97F4A7C15ull)) ^
                   (sh.sh_size * 0xff51afd7ed558ccdull) ^
                   (sh.sh_flags * 0x9E3779B97F4A7C15ull) ^
                   (uint64_t(o->secs[sec].osec) << 40) ^ (uint64_t(n) << 20);
      for (size_t k = 0; k < n; ++k)
        h = (h ^ (rels[k].r_offset * 0xff51afd7ed558ccdull) ^
             ELF64_R_TYPE(rels[k].r_info)) *
            0xc4ceb9fe1a85ec53ull;
      const uint32_t placed = o->secs[sec].placed;
      ctx.placed[placed].icfCandidate = true;
      perFile[fi].push_back({o, sec, placed, h});
    }
  }, 4);
  vector<Candidate> cand;
  for (auto &v : perFile)
    cand.insert(cand.end(), v.begin(), v.end());
  if (cand.size() < 2)
    return;
  mark("icf candidates");
  // Sort by hash, then position, in parallel: bucket by the hash's top bits.
  {
    constexpr unsigned Bits = 10;
    vector<vector<uint32_t>> counts(ctx.pool->size(),
                                    vector<uint32_t>(1u << Bits, 0));
    const size_t chunk = (cand.size() + 63) / 64;
    vector<vector<uint32_t>> chunkCounts(64, vector<uint32_t>(1u << Bits, 0));
    ctx.pool->forEach(64, [&](size_t c) {
      for (size_t i = c * chunk; i < std::min(cand.size(), (c + 1) * chunk); ++i)
        ++chunkCounts[c][cand[i].hash >> (64 - Bits)];
    }, 1);
    vector<size_t> bucketStart((1u << Bits) + 1, 0);
    for (unsigned b = 0; b < (1u << Bits); ++b) {
      size_t sum = 0;
      for (size_t c = 0; c < 64; ++c)
        sum += chunkCounts[c][b];
      bucketStart[b + 1] = bucketStart[b] + sum;
    }
    vector<vector<size_t>> chunkPos(64, vector<size_t>(1u << Bits));
    for (unsigned b = 0; b < (1u << Bits); ++b) {
      size_t p = bucketStart[b];
      for (size_t c = 0; c < 64; ++c) {
        chunkPos[c][b] = p;
        p += chunkCounts[c][b];
      }
    }
    vector<Candidate> sorted(cand.size());
    ctx.pool->forEach(64, [&](size_t c) {
      for (size_t i = c * chunk; i < std::min(cand.size(), (c + 1) * chunk); ++i)
        sorted[chunkPos[c][cand[i].hash >> (64 - Bits)]++] = cand[i];
    }, 1);
    ctx.pool->forEach(1u << Bits, [&](size_t b) {
      std::sort(sorted.begin() + bucketStart[b], sorted.begin() + bucketStart[b + 1],
                [](const Candidate &x, const Candidate &y) {
                  if (x.hash != y.hash)
                    return x.hash < y.hash;
                  if (x.file->pos != y.file->pos)
                    return x.file->pos < y.file->pos;
                  return x.sec < y.sec;
                });
    }, 4);
    cand.swap(sorted);
  }

  // Splits [b, e) into runs equal to their first member, in place and in a
  // stable order. A run's class is its end index plus one, so a run that
  // does not split keeps its class. Runs of several members are reported for
  // the next round; a single member's class is final and set in both slots.
  vector<vector<Candidate>> scratch(ctx.pool->size());
  vector<vector<std::pair<uint32_t, uint32_t>>> found(ctx.pool->size());
  std::atomic<bool> split{false};
  auto segregate = [&](size_t b, size_t e, auto &&equal, int next) {
    auto &rest = scratch[Pool::self()];
    auto &runs = found[Pool::self()];
    while (b < e) {
      rest.clear();
      size_t m = b + 1;
      for (size_t i = b + 1; i < e; ++i) {
        if (equal(cand[b], cand[i]))
          cand[m++] = cand[i];
        else
          rest.push_back(cand[i]);
      }
      std::copy(rest.begin(), rest.end(), cand.begin() + m);
      const uint32_t cls = uint32_t(m + 1);
      if (m - b == 1) {
        PlacedSection &ps = ctx.placed[cand[b].placed];
        ps.icfClass[0] = ps.icfClass[1] = cls;
      } else {
        for (size_t i = b; i < m; ++i)
          ctx.placed[cand[i].placed].icfClass[next] = cls;
        runs.push_back({uint32_t(b), uint32_t(m)});
      }
      if (m != e)
        split.store(true, std::memory_order_relaxed);
      b = m;
    }
  };
  auto takeRuns = [&](vector<std::pair<size_t, size_t>> &out) {
    out.clear();
    for (auto &v : found) {
      out.insert(out.end(), v.begin(), v.end());
      v.clear();
    }
  };
  // Groups of equal hashes, then of equal constant parts.
  vector<std::pair<size_t, size_t>> groups;
  for (size_t b = 0; b < cand.size();) {
    size_t e = b + 1;
    while (e < cand.size() && cand[e].hash == cand[b].hash)
      ++e;
    groups.push_back({b, e});
    b = e;
  }
  mark("icf sort");
  ctx.pool->forEach(groups.size(), [&](size_t g) {
    segregate(groups[g].first, groups[g].second, equalsConstant, 0);
  }, 16);
  mark("icf constant");
  // Relocation targets of the members of the groups that remain.
  {
    vector<std::pair<size_t, size_t>> runs;
    for (auto &v : found)
      runs.insert(runs.end(), v.begin(), v.end());
    vector<uint64_t> offs(runs.size() + 1, 0);
    for (size_t g = 0; g < runs.size(); ++g) {
      uint64_t n = 0;
      for (size_t i = runs[g].first; i < runs[g].second; ++i)
        n += cand[i].file->relas(cand[i].sec).second;
      offs[g + 1] = offs[g] + n;
    }
    targetPlaced.assign(offs.back() + 1, 0);
    targetBase.assign(ctx.numPlaced, 0);
    ctx.pool->forEach(runs.size(), [&](size_t g) {
      uint64_t o = offs[g];
      for (size_t i = runs[g].first; i < runs[g].second; ++i) {
        const Candidate &c = cand[i];
        targetBase[c.placed] = o;
        auto [rels, n] = c.file->relas(c.sec);
        for (size_t k = 0; k < n; ++k) {
          RelTarget t = relTarget(c.file, ELF64_R_SYM(rels[k].r_info));
          if (t.kind == RelTarget::Section)
            targetPlaced[o + k] = t.file->secs[t.sec].placed;
        }
        o += n;
      }
    }, 8);
  }
  mark("icf targets");
  // Refine the groups of several members by relocation targets until no
  // group splits.
  int cur = 0;
  for (int round = 0;; ++round) {
    takeRuns(groups);
    split = false;
    const int next = cur ^ 1;
    ctx.pool->forEach(groups.size(), [&](size_t g) {
      segregate(groups[g].first, groups[g].second,
                [&](const Candidate &x, const Candidate &y) {
                  return equalsVariable(x, y, cur);
                },
                next);
    }, 8);
    cur = next;
    if (ctx.opt.timing)
      fprintf(stderr, "    icf round %d groups %zu\n", round, groups.size());
    if (!split.load())
      break;
    if (round > 100)
      fatal("identical code folding does not converge");
  }
  mark("icf refine");
  // Fold each class into its first member, the earliest by position: the
  // runs of the last round are the classes of several members.
  vector<std::pair<size_t, size_t>> classes;
  takeRuns(classes);
  std::atomic<size_t> folded{0};
  ctx.pool->forEach(classes.size(), [&](size_t g) {
    auto [b, e] = classes[g];
    const Candidate &leader = cand[b];
    PlacedSection &lp = ctx.placed[leader.placed];
    uint64_t align = leader.file->shdrs[leader.sec].sh_addralign;
    for (size_t i = b + 1; i < e; ++i) {
      const Candidate &c = cand[i];
      align = std::max<uint64_t>(align, c.file->shdrs[c.sec].sh_addralign);
      SectionState &ss = c.file->secs[c.sec];
      ss.live.store(3, std::memory_order_relaxed);
      ss.placed = leader.placed;
    }
    if (align > leader.file->shdrs[leader.sec].sh_addralign)
      lp.align = align;
    folded += e - b - 1;
  }, 16);
  if (ctx.opt.timing)
    fprintf(stderr, "    icf: %zu candidates, %zu folded\n", cand.size(),
            folded.load());
  if (!folded.load())
    return;
  // Drop the folded sections from their output sections.
  ctx.pool->forEach(L.all.size(), [&](size_t i) {
    auto &m = L.all[i]->members;
    m.erase(std::remove_if(m.begin(), m.end(),
                           [](const auto &x) {
                             return x.first->secs[x.second].live.load(
                                        std::memory_order_relaxed) == 3;
                           }),
            m.end());
  }, 1);
}

} // namespace icf

// ================================================================ merging
// Mergeable sections are split into pieces, strings or fixed-size records,
// and equal pieces of one group (output section, kind, entry size and
// alignment) share one copy: the piece that comes first by position. Each
// section holds, in order, the pieces it owns.
constexpr uint32_t MergeOwner = 1u << 31;

struct MergeGroup {
  uint64_t numPieces = 0;
  uint32_t align = 1;
};
vector<MergeGroup> mergeGroups;
// One table for the pieces of all groups, tagged with their group, and by
// piece id the lowest (placed index, offset) and the owner's output offset.
std::unique_ptr<NameTable> mergeTable;
std::atomic<uint64_t> *mergeRank;
uint32_t *mergeOut;

uint64_t mergedAddress(const ObjectFile *o, uint32_t sec, uint64_t off) {
  const SectionState &ss = o->secs[sec];
  const PlacedSection &ps = ctx.placed[ss.placed];
  const auto *b = ctx.pieces.get() + ps.pieceBase;
  const auto *e = b + ps.numPieces;
  // The last piece that starts at or before the offset.
  const auto *p = std::upper_bound(b, e, off, [](uint64_t v, const auto &q) {
                    return v < q.inOff;
                  }) - 1;
  if (p < b)
    p = b;
  return L.all[ss.osec]->addr + p->out + (off - p->inOff);
}

void mergeSections() {
  STEP("merge");
  markTime = Clock::now();
  // Mergeable sections by file, in position order.
  struct Item {
    ObjectFile *file;
    uint32_t sec;
  };
  vector<Item> items;
  for (size_t fi = 0; fi < ctx.objects.size(); ++fi)
    for (uint32_t sec : mergeCandidates[fi]) {
      ObjectFile *o = ctx.objects[fi];
      if (o->relaOf[sec])
        fatal(o->name + ": relocations in mergeable section " + o->secName(sec));
      items.push_back({o, sec});
    }
  if (items.empty())
    return;
  // Groups, in order of first appearance; there are only a few.
  struct Key {
    uint32_t osec;
    bool strings;
    uint64_t entsize, align;
    bool operator==(const Key &k) const {
      return osec == k.osec && strings == k.strings && entsize == k.entsize &&
             align == k.align;
    }
  };
  vector<Key> keys;
  size_t last = 0;
  for (const Item &it : items) {
    const Elf64_Shdr &sh = it.file->shdrs[it.sec];
    const Key key{it.file->secs[it.sec].osec, bool(sh.sh_flags & SHF_STRINGS),
                  sh.sh_entsize, std::max<uint64_t>(sh.sh_addralign, 1)};
    if (last >= keys.size() || !(keys[last] == key)) {
      last = std::find(keys.begin(), keys.end(), key) - keys.begin();
      if (last == keys.size()) {
        if (keys.size() == UINT16_MAX)
          fatal("too many kinds of mergeable sections");
        keys.push_back(key);
        mergeGroups.emplace_back();
        mergeGroups.back().align = uint32_t(key.align);
      }
    }
    PlacedSection &ps = placedOf(it.file, it.sec);
    ps.merged = true;
    ps.mergeGroup = uint16_t(last);
  }
  // Split into pieces: count, then fill a shared array.
  vector<uint64_t> base(items.size() + 1, 0);
  auto split = [](const ObjectFile *o, uint32_t sec, auto &&emit) {
    const Elf64_Shdr &sh = o->shdrs[sec];
    const uint8_t *d = o->secData(sec);
    const uint64_t size = sh.sh_size, es = sh.sh_entsize;
    if (!(sh.sh_flags & SHF_STRINGS)) {
      if (size % es)
        fatal(o->name + ": mergeable section size is not a multiple of its "
                        "entry size");
      for (uint64_t off = 0; off < size; off += es)
        emit(off);
      return;
    }
    for (uint64_t off = 0; off < size;) {
      uint64_t end = off;
      if (es == 1) {
        const void *z = memchr(d + off, 0, size - off);
        end = z ? static_cast<const uint8_t *>(z) - d : size;
      } else {
        while (end + es <= size) {
          bool zero = true;
          for (uint64_t b = 0; b < es; ++b)
            zero &= d[end + b] == 0;
          if (zero)
            break;
          end += es;
        }
      }
      if (end + es > size)
        fatal(o->name + ": unterminated string in " + o->secName(sec));
      emit(off);
      off = end + es;
    }
  };
  ctx.pool->forEach(items.size(), [&](size_t i) {
    uint64_t n = 0;
    split(items[i].file, items[i].sec, [&](uint64_t) { ++n; });
    base[i + 1] = n;
  }, 4);
  for (size_t i = 0; i < items.size(); ++i) {
    PlacedSection &ps = placedOf(items[i].file, items[i].sec);
    mergeGroups[ps.mergeGroup].numPieces += base[i + 1];
    base[i + 1] += base[i];
  }
  if (base.back() >= UINT32_MAX)
    fatal("too many mergeable pieces");
  ctx.pieces.reset(static_cast<Ctx::Piece *>(
      malloc(std::max<uint64_t>(base.back(), 1) * sizeof(Ctx::Piece))));
  if (ctx.opt.timing)
    fprintf(stderr, "    merge sections %zu\n", items.size());
  if (ctx.opt.timing)
    for (const MergeGroup &g : mergeGroups)
      fprintf(stderr, "    merge group align %u pieces %llu\n", g.align,
              (unsigned long long)g.numPieces);
  mark("merge split");
  {
    // Room for every piece and for the ids workers reserve in blocks.
    mergeTable = std::make_unique<NameTable>(base.back() + 256 * 64);
    mark("merge table alloc");
    mergeTable->prefault(*ctx.pool);
    mark("merge table prefault");
    const size_t ids = mergeTable->idLimit();
    mergeRank = bigArray<std::atomic<uint64_t>>(*ctx.pool, ids);
    mergeOut = bigArray<uint32_t>(*ctx.pool, ids);
    mark("merge arrays");
    ctx.pool->forEach(ids, [&](size_t k) {
      mergeRank[k].store(UINT64_MAX, std::memory_order_relaxed);
    });
  }
  mark("merge tables");
  // Intern the pieces; the lowest (placed index, offset) owns each.
  ctx.pool->forEach(items.size(), [&](size_t i) {
    ObjectFile *o = items[i].file;
    const uint32_t sec = items[i].sec;
    PlacedSection &ps = placedOf(o, sec);
    ps.pieceBase = base[i];
    ps.numPieces = base[i + 1] - base[i];
    auto *p = ctx.pieces.get() + base[i];
    uint32_t k = 0;
    split(o, sec, [&](uint64_t off) { p[k++].inOff = uint32_t(off); });
    const uint8_t *d = o->secData(sec);
    const uint64_t size = o->shdrs[sec].sh_size;
    const uint64_t rank0 = uint64_t(o->secs[sec].placed) << 32;
    for (k = 0; k < ps.numPieces; ++k) {
      const uint32_t end = k + 1 < ps.numPieces ? p[k + 1].inOff : size;
      const uint32_t id = mergeTable->intern(
          reinterpret_cast<const char *>(d + p[k].inOff), end - p[k].inOff,
          ps.mergeGroup);
      if (id == UINT32_MAX)
        fatal("internal error: mergeable piece table is full");
      p[k].id = id;
      atomicMin(mergeRank[id], rank0 | p[k].inOff);
    }
  }, 4);
  mark("merge intern");
  // Each section lays out the pieces it owns.
  ctx.pool->forEach(items.size(), [&](size_t i) {
    ObjectFile *o = items[i].file;
    const uint32_t sec = items[i].sec;
    PlacedSection &ps = placedOf(o, sec);
    MergeGroup &g = mergeGroups[ps.mergeGroup];
    auto *p = ctx.pieces.get() + ps.pieceBase;
    const uint64_t size = o->shdrs[sec].sh_size;
    const uint64_t rank0 = uint64_t(o->secs[sec].placed) << 32;
    uint64_t pos = 0;
    for (uint32_t k = 0; k < ps.numPieces; ++k) {
      if (mergeRank[p[k].id].load(std::memory_order_relaxed) != (rank0 | p[k].inOff))
        continue;
      const uint32_t end = k + 1 < ps.numPieces ? p[k + 1].inOff : size;
      pos = alignTo(pos, g.align);
      p[k].out = uint32_t(pos); // relative until the section is placed
      p[k].id |= MergeOwner;
      pos += end - p[k].inOff;
    }
    if (pos > UINT32_MAX)
      fatal("mergeable section too large");
    ps.mergedSize = uint32_t(pos);
  }, 4);
}

// Once sections are placed: every piece's output offset in its output
// section, the owner's.
void finalizeMerge() {
  if (mergeGroups.empty())
    return;
  vector<std::pair<ObjectFile *, uint32_t>> items;
  for (ObjectFile *o : ctx.objects)
    for (uint32_t sec = 1; sec < o->numShdrs; ++sec)
      if (o->secs[sec].osec && o->secs[sec].live.load() == 1 &&
          placedOf(o, sec).merged)
        items.push_back({o, sec});
  ctx.pool->forEach(items.size(), [&](size_t i) {
    PlacedSection &ps = placedOf(items[i].first, items[i].second);
    auto *p = ctx.pieces.get() + ps.pieceBase;
    for (uint32_t k = 0; k < ps.numPieces; ++k)
      if (p[k].id & MergeOwner) {
        p[k].out += ps.outOff;
        mergeOut[p[k].id & ~MergeOwner] = p[k].out;
      }
  }, 4);
  ctx.pool->forEach(items.size(), [&](size_t i) {
    PlacedSection &ps = placedOf(items[i].first, items[i].second);
    auto *p = ctx.pieces.get() + ps.pieceBase;
    for (uint32_t k = 0; k < ps.numPieces; ++k)
      if (!(p[k].id & MergeOwner))
        p[k].out = mergeOut[p[k].id];
  }, 4);
}

// Output sections with many members are laid out in fixed-size chunks, each
// starting at its strictest member alignment, so the chunks can be laid out
// in parallel. The chunk size does not depend on the thread count.
void assignAllOffsets() {
  constexpr size_t ChunkMembers = 4096, MinChunked = 4 * ChunkMembers;
  struct Chunk {
    OutputSection *os;
    size_t begin, end;
    uint64_t size = 0, align = 1, start = 0;
  };
  vector<Chunk> chunks;
  vector<OutputSection *> small;
  for (OutputSection *os : L.all) {
    if (os->members.size() < MinChunked) {
      if (!os->members.empty())
        small.push_back(os);
      continue;
    }
    for (size_t b = 0; b < os->members.size(); b += ChunkMembers)
      chunks.push_back({os, b, std::min(os->members.size(), b + ChunkMembers)});
  }
  ctx.pool->forEach(small.size() + chunks.size(), [&](size_t i) {
    if (i < small.size()) {
      assignOffsets(small[i]);
      return;
    }
    Chunk &c = chunks[i - small.size()];
    uint64_t off = 0;
    for (size_t m = c.begin; m < c.end; ++m) {
      auto [o, sec] = c.os->members[m];
      const Elf64_Shdr &sh = o->shdrs[sec];
      PlacedSection &ps = placedOf(o, sec);
      const uint64_t al = ps.align ? ps.align : sh.sh_addralign;
      uint64_t start = alignTo(off, al);
      ps.padBefore = start - off;
      ps.outOff = start;
      off = start + outputSize(o, sec, ps);
      c.align = std::max<uint64_t>(c.align, al);
    }
    c.size = off;
  }, 1);
  for (size_t i = 0; i < chunks.size(); ++i) {
    Chunk &c = chunks[i];
    uint64_t prevEnd =
        c.begin == 0 ? 0 : chunks[i - 1].start + chunks[i - 1].size;
    c.start = alignTo(prevEnd, c.align);
    // The first member's padding covers the gap to the previous chunk.
    auto [o, sec] = c.os->members[c.begin];
    placedOf(o, sec).padBefore += c.start - prevEnd;
    if (c.end == c.os->members.size()) {
      if (c.start + c.size > UINT32_MAX)
        fatal("output section " + c.os->name + " exceeds 4 GiB");
      c.os->size = c.start + c.size;
    }
  }
  ctx.pool->forEach(chunks.size(), [&](size_t i) {
    const Chunk &c = chunks[i];
    if (!c.start)
      return;
    for (size_t m = c.begin; m < c.end; ++m) {
      auto [o, sec] = c.os->members[m];
      placedOf(o, sec).outOff += c.start;
    }
  }, 1);
}

void assignCopies() {
  vector<uint32_t> primaries;
  for (uint32_t id = 0; id < ctx.numNames; ++id)
    if (ctx.flags[id].load(std::memory_order_relaxed) & NeedsCopy)
      primaries.push_back(id);
  if (primaries.empty())
    return;
  std::sort(primaries.begin(), primaries.end(), [](uint32_t a, uint32_t b) {
    return ctx.nameStr[a] < ctx.nameStr[b];
  });
  uint64_t off = 0, maxAlign = 1;
  for (uint32_t id : primaries) {
    Symbol &s = ctx.syms[id];
    if (s.copied)
      continue; // an alias of an earlier copy
    const SharedFile *so = ctx.shared[s.file];
    const Elf64_Sym &ds = so->dynsyms[s.index];
    uint64_t align = ds.st_value ? (ds.st_value & -ds.st_value) : 64;
    if (ds.st_shndx < SHN_LORESERVE)
      align = std::min<uint64_t>(align, so->shdrs[ds.st_shndx].sh_addralign);
    align = std::max<uint64_t>(align, 1);
    maxAlign = std::max(maxAlign, align);
    off = alignTo(off, align);
    s.copied = s.copyPrimary = true;
    s.copyOff = off;
    // Aliases in the same library share the copy.
    for (uint32_t k = 1; k < so->numDyn; ++k) {
      const Elf64_Sym &a = so->dynsyms[k];
      uint32_t aid = so->nameIds[k];
      if (aid == UINT32_MAX || aid == id || a.st_shndx != ds.st_shndx ||
          a.st_value != ds.st_value || ELF64_ST_TYPE(a.st_info) != STT_OBJECT)
        continue;
      Symbol &as = ctx.syms[aid];
      if (as.kind != Symbol::Shared || as.file != s.file || as.copied)
        continue;
      as.copied = true;
      as.copyOff = off;
      atomicOr(ctx.flags[aid], uint16_t(NeedsDynsym));
    }
    off += std::max<uint64_t>(ds.st_size, 1);
  }
  L.copyRel = newSection(".copyrel", SHT_NOBITS, SHF_ALLOC | SHF_WRITE,
                         maxAlign);
  L.copyRel->size = off;
}

void buildDynamic() {
  STEP("buildDynamic");
  const uint32_t n = ctx.numNames;
  assignCopies();
  (void)n;
  vector<uint32_t> got, gotTp, plt, tlsGd;
  collectNames<6>({&L.exports, &got, &gotTp, &plt, &L.imports, &tlsGd},
                  [](uint32_t id, auto &out) {
    uint16_t f = ctx.flags[id].load(std::memory_order_relaxed);
    const Symbol &s = ctx.syms[id];
    if (s.copied) {
      out[0].push_back(id);
      ctx.shared[s.file]->needed = 1;
      if (f & NeedsGot)
        out[1].push_back(id);
      return;
    }
    if (f & NeedsGot)
      out[1].push_back(id);
    if (f & NeedsGotTp)
      out[2].push_back(id);
    if (f & NeedsPlt)
      out[3].push_back(id);
    if (f & NeedsTlsGd)
      out[5].push_back(id);
    if (s.isImport() && (f & NeedsDynsym)) {
      out[4].push_back(id);
      if (s.kind == Symbol::Shared)
        ctx.shared[s.file]->needed = 1;
    }
    if (isExported(id))
      out[0].push_back(id);
  });
  for (uint32_t id : L.exports)
    L.numCopies += ctx.syms[id].copyPrimary;
  auto byName = [](uint32_t a, uint32_t b) {
    return ctx.nameStr[a] < ctx.nameStr[b];
  };
  std::sort(got.begin(), got.end(), byName);
  std::sort(gotTp.begin(), gotTp.end(), byName);
  std::sort(plt.begin(), plt.end(), byName);
  std::sort(tlsGd.begin(), tlsGd.end(), byName);
  std::sort(L.imports.begin(), L.imports.end(), byName);
  uint32_t slot = 0;
  for (uint32_t id : got)
    ctx.syms[id].got = slot++;
  for (uint32_t id : gotTp)
    ctx.syms[id].gotTp = slot++;
  for (uint32_t id : tlsGd) {
    ctx.syms[id].tlsGd = slot;
    slot += 2;
  }
  if (needsTlsLd.load()) {
    L.tlsLdSlot = slot;
    slot += 2;
  }
  for (uint32_t i = 0; i < plt.size(); ++i)
    ctx.syms[plt[i]].plt = i;

  // GNU hash: defined symbols follow undefined ones, grouped by bucket.
  L.gnuSymOffset = 1 + L.imports.size();
  L.gnuBuckets = std::max<uint32_t>(1, L.exports.size() / 4);
  vector<uint32_t> hashes;
  {
    vector<std::pair<uint64_t, uint32_t>> keyed;
    for (uint32_t id : L.exports) {
      uint32_t h = gnuHash(ctx.nameStr[id]);
      keyed.push_back({(uint64_t(h % L.gnuBuckets) << 32) | h, id});
    }
    std::sort(keyed.begin(), keyed.end(), [](const auto &a, const auto &b) {
      if (a.first != b.first)
        return a.first < b.first;
      return ctx.nameStr[a.second] < ctx.nameStr[b.second];
    });
    L.exports.clear();
    for (auto &[k, id] : keyed) {
      L.exports.push_back(id);
      hashes.push_back(uint32_t(k));
    }
  }
  L.gnuBucketVals.assign(L.gnuBuckets, 0);
  L.gnuChain.assign(L.exports.size(), 0);
  for (size_t i = 0; i < L.exports.size(); ++i) {
    uint32_t h = hashes[i], b = h % L.gnuBuckets;
    if (!L.gnuBucketVals[b])
      L.gnuBucketVals[b] = L.gnuSymOffset + i;
    bool last = i + 1 == L.exports.size() || hashes[i + 1] % L.gnuBuckets != b;
    L.gnuChain[i] = (h & ~1u) | (last ? 1 : 0);
    L.gnuBloom |= (1ull << (h % 64)) | (1ull << ((h >> 26) % 64));
  }
  uint32_t idx = 1;
  for (uint32_t id : L.imports)
    ctx.syms[id].dynsym = idx++;
  for (uint32_t id : L.exports)
    ctx.syms[id].dynsym = idx++;

  // .dynstr
  L.dynstrData.assign(1, '\0');
  auto addStr = [](string_view s) {
    uint32_t off = L.dynstrData.size();
    L.dynstrData.append(s);
    L.dynstrData.push_back('\0');
    return off;
  };
  for (SharedFile *so : ctx.shared)
    if (so->needed.load() || !so->asNeeded) {
      L.neededLibs.push_back(so);
      L.neededOff.push_back(addStr(so->soname));
    }
  if (ctx.opt.shared && !ctx.opt.soname.empty())
    L.sonameOff = addStr(ctx.opt.soname);
  if (!ctx.opt.rpaths.empty()) {
    string r;
    for (const string &p : ctx.opt.rpaths)
      r += (r.empty() ? "" : ":") + p;
    L.rpathOff = addStr(r);
  }
  L.nameDynstr.push_back(0);
  for (uint32_t id : L.imports)
    L.nameDynstr.push_back(addStr(ctx.nameStr[id]));
  for (uint32_t id : L.exports)
    L.nameDynstr.push_back(addStr(ctx.nameStr[id]));

  // Symbol versions: each import binds to the version its library defines.
  L.versyms.assign(1 + L.imports.size() + L.exports.size(), 1);
  L.versyms[0] = 0;
  // Definitions carry their version script node; named nodes are defined
  // in .gnu.version_d after the base version, the file itself.
  if (ctx.versionIds) {
    for (size_t i = 0; i < L.exports.size(); ++i)
      L.versyms[1 + L.imports.size() + i] = ctx.versionIds[L.exports[i]];
    if (ctx.opt.versions.size() > 2) {
      string base = ctx.opt.soname;
      if (base.empty()) {
        size_t slash = ctx.opt.output.rfind('/');
        base = slash == string::npos ? ctx.opt.output
                                     : ctx.opt.output.substr(slash + 1);
      }
      L.verdefs.push_back({elfHash(base), addStr(base)});
      for (size_t v = 2; v < ctx.opt.versions.size(); ++v)
        L.verdefs.push_back({elfHash(ctx.opt.versions[v].name),
                             addStr(ctx.opt.versions[v].name)});
    }
  }
  {
    std::unordered_map<const SharedFile *, vector<const char *>> verNames;
    std::map<std::pair<uint32_t, string_view>, uint16_t> assigned; // (lib pos, ver)
    vector<std::pair<SharedFile *, vector<std::pair<string_view, uint16_t>>>> libs;
    // Needed versions are numbered after the defined ones.
    uint16_t next = uint16_t(std::max<size_t>(2, L.verdefs.size() + 1));
    for (size_t i = 0; i < L.imports.size(); ++i) {
      const Symbol &sym = ctx.syms[L.imports[i]];
      if (sym.kind != Symbol::Shared)
        continue;
      SharedFile *so = ctx.shared[sym.file];
      if (!so->versym || !so->verdef)
        continue;
      uint16_t ndx = so->versym[sym.index] & 0x7fff;
      if (ndx <= 1)
        continue;
      auto &names = verNames[so];
      if (names.empty()) {
        const uint8_t *p = so->verdef;
        for (uint32_t k = 0; k < so->numVerdef; ++k) {
          auto *vd = reinterpret_cast<const Elf64_Verdef *>(p);
          auto *aux = reinterpret_cast<const Elf64_Verdaux *>(p + vd->vd_aux);
          if (names.size() <= vd->vd_ndx)
            names.resize(vd->vd_ndx + 1, nullptr);
          names[vd->vd_ndx] = so->dynstr + aux->vda_name;
          p += vd->vd_next;
        }
      }
      if (ndx >= names.size() || !names[ndx])
        continue;
      string_view vn = names[ndx];
      auto [it, inserted] = assigned.try_emplace({so->pos, vn}, 0);
      if (inserted) {
        it->second = next++;
        auto lit = std::find_if(libs.begin(), libs.end(),
                                [&](auto &l) { return l.first == so; });
        if (lit == libs.end()) {
          libs.push_back({so, {}});
          lit = libs.end() - 1;
        }
        lit->second.push_back({vn, it->second});
      }
      L.versyms[1 + i] = it->second;
    }
    std::sort(libs.begin(), libs.end(),
              [](auto &a, auto &b) { return a.first->pos < b.first->pos; });
    for (auto &[so, vers] : libs) {
      uint32_t file = 0;
      for (size_t k = 0; k < L.neededLibs.size(); ++k)
        if (L.neededLibs[k] == so)
          file = L.neededOff[k];
      Layout::Verneed vn{file, {}};
      for (auto &[name, idx] : vers)
        vn.aux.push_back({elfHash(name), idx, addStr(name)});
      L.verneeds.push_back(std::move(vn));
    }
  }

  // Dynamic relocation counts; slots are assigned after layout.
  {
    vector<uint64_t> rel(ctx.objects.size()), sym(ctx.objects.size());
    ctx.pool->forEach(ctx.objects.size(), [&](size_t fi) {
      const ObjectFile *o = ctx.objects[fi];
      for (uint32_t i = 1; i < o->numShdrs; ++i)
        if (o->secs[i].osec && o->secs[i].live.load() == 1) {
          rel[fi] += placedOf(o, i).numRel;
          sym[fi] += placedOf(o, i).numSym;
        }
    });
    for (size_t fi = 0; fi < rel.size(); ++fi) {
      L.numSecRel += rel[fi];
      L.numSecSym += sym[fi];
    }
  }
  for (uint32_t id : got) {
    Symbol::Kind k = ctx.syms[id].kind;
    if (isPreemptible(id))
      ++L.numGotSym;
    else if (ctx.isPic() && k != Symbol::Undefined)
      ++L.numGotRel;
  }
  L.numGotSym += gotTp.size() + L.numCopies;
  for (uint32_t id : tlsGd)
    L.numGotSym += isPreemptible(id) ? 2 : 1;
  L.numGotSym += needsTlsLd.load() ? 1 : 0;
  L.staticTls = ctx.opt.shared && !gotTp.empty();
  L.numRelative = L.numSecRel + L.numGotRel;

  // Synthetic sections.
  const uint64_t A = SHF_ALLOC;
  if (!ctx.opt.dynamicLinker.empty()) {
    L.interp = newSection(".interp", SHT_PROGBITS, A, 1);
    L.interp->size = ctx.opt.dynamicLinker.size() + 1;
  }
  if (!ctx.opt.buildIdBytes.empty())
    ctx.opt.buildIdSize = ctx.opt.buildIdBytes.size();
  if (ctx.opt.buildIdSize) {
    L.buildId = newSection(".note.gnu.build-id", SHT_NOTE, A, 4);
    L.buildId->size = 16 + alignTo(ctx.opt.buildIdSize, 4);
  }
  L.dynsym = newSection(".dynsym", SHT_DYNSYM, A, 8);
  L.dynsym->size = idx * sizeof(Elf64_Sym);
  L.dynsym->entsize = sizeof(Elf64_Sym);
  L.gnuHashSec = newSection(".gnu.hash", SHT_GNU_HASH, A, 8);
  L.gnuHashSec->size = 16 + 8 + 4 * L.gnuBuckets + 4 * L.exports.size();
  L.dynstr = newSection(".dynstr", SHT_STRTAB, A, 1);
  L.dynstr->size = L.dynstrData.size();
  if (!L.verneeds.empty() || !L.verdefs.empty()) {
    L.versym = newSection(".gnu.version", SHT_GNU_versym, A, 2);
    L.versym->size = 2 * L.versyms.size();
    L.versym->entsize = 2;
  }
  if (!L.verdefs.empty()) {
    L.verdef = newSection(".gnu.version_d", SHT_GNU_verdef, A, 4);
    L.verdef->size = 28 * L.verdefs.size();
  }
  if (!L.verneeds.empty()) {
    L.verneed = newSection(".gnu.version_r", SHT_GNU_verneed, A, 8);
    size_t n = 0;
    for (auto &v : L.verneeds)
      n += 1 + v.aux.size();
    L.verneed->size = 16 * n;
  }
  size_t numDyn = L.numRelative + L.numGotSym + L.numSecSym;
  if (numDyn) {
    L.relaDyn = newSection(".rela.dyn", SHT_RELA, A, 8);
    L.relaDyn->size = numDyn * sizeof(Elf64_Rela);
    L.relaDyn->entsize = sizeof(Elf64_Rela);
  }
  if (!plt.empty()) {
    L.relaPlt = newSection(".rela.plt", SHT_RELA, A | SHF_INFO_LINK, 8);
    L.relaPlt->size = plt.size() * sizeof(Elf64_Rela);
    L.relaPlt->entsize = sizeof(Elf64_Rela);
    L.plt = newSection(".plt", SHT_PROGBITS, A | SHF_EXECINSTR, 16);
    L.plt->size = 16 * (plt.size() + 1);
  }
  if (slot) {
    L.got = newSection(".got", SHT_PROGBITS, A | SHF_WRITE, 8);
    L.got->size = 8 * slot;
  }
  L.gotPlt = newSection(".got.plt", SHT_PROGBITS, A | SHF_WRITE, 8);
  L.gotPlt->size = 8 * (3 + plt.size());
  L.dynamic = newSection(".dynamic", SHT_DYNAMIC, A | SHF_WRITE, 8);
  L.dynamic->entsize = sizeof(Elf64_Dyn);
  L.gotPltIds = std::move(plt);
  L.gotIds = std::move(got);
  L.gotTpIds = std::move(gotTp);
  L.tlsGdIds = std::move(tlsGd);
}

vector<Elf64_Dyn> dynamicEntries() {
  vector<Elf64_Dyn> d;
  auto add = [&](int64_t tag, uint64_t val) {
    Elf64_Dyn e;
    e.d_tag = tag;
    e.d_un.d_val = val;
    d.push_back(e);
  };
  for (uint32_t off : L.neededOff)
    add(DT_NEEDED, off);
  if (L.sonameOff)
    add(DT_SONAME, L.sonameOff);
  if (!ctx.opt.rpaths.empty())
    add(DT_RUNPATH, L.rpathOff);
  auto init = L.byName.find(".init_array");
  auto fini = L.byName.find(".fini_array");
  auto pre = L.byName.find(".preinit_array");
  if (pre != L.byName.end()) {
    add(DT_PREINIT_ARRAY, pre->second->addr);
    add(DT_PREINIT_ARRAYSZ, pre->second->size);
  }
  if (init != L.byName.end()) {
    add(DT_INIT_ARRAY, init->second->addr);
    add(DT_INIT_ARRAYSZ, init->second->size);
  }
  if (fini != L.byName.end()) {
    add(DT_FINI_ARRAY, fini->second->addr);
    add(DT_FINI_ARRAYSZ, fini->second->size);
  }
  auto symVAOf = [](const char *name) -> uint64_t {
    uint32_t id = ctx.names ? ctx.names->find(name, strlen(name)) : UINT32_MAX;
    if (id == UINT32_MAX || ctx.syms[id].kind != Symbol::Object)
      return 0;
    return ctx.syms[id].va + 1; // +1: present marker
  };
  if (uint64_t v = symVAOf("_init"))
    add(DT_INIT, v - 1);
  if (uint64_t v = symVAOf("_fini"))
    add(DT_FINI, v - 1);
  if (L.versym)
    add(DT_VERSYM, L.versym->addr);
  if (L.verdef) {
    add(DT_VERDEF, L.verdef->addr);
    add(DT_VERDEFNUM, L.verdefs.size());
  }
  if (L.verneed) {
    add(DT_VERNEED, L.verneed->addr);
    add(DT_VERNEEDNUM, L.verneeds.size());
  }
  add(DT_GNU_HASH, L.gnuHashSec->addr);
  add(DT_STRTAB, L.dynstr->addr);
  add(DT_SYMTAB, L.dynsym->addr);
  add(DT_STRSZ, L.dynstr->size);
  add(DT_SYMENT, sizeof(Elf64_Sym));
  if (!ctx.opt.shared)
    add(DT_DEBUG, 0);
  if (L.relaDyn) {
    add(DT_RELA, L.relaDyn->addr);
    add(DT_RELASZ, L.relaDyn->size);
    add(DT_RELAENT, sizeof(Elf64_Rela));
    if (L.numRelative)
      add(DT_RELACOUNT, L.numRelative);
  }
  if (L.relaPlt) {
    add(DT_JMPREL, L.relaPlt->addr);
    add(DT_PLTRELSZ, L.relaPlt->size);
    add(DT_PLTREL, DT_RELA);
  }
  add(DT_PLTGOT, L.gotPlt->addr);
  if (uint64_t f = (ctx.opt.zNow ? DF_BIND_NOW : 0) |
                   (L.staticTls ? DF_STATIC_TLS : 0))
    add(DT_FLAGS, f);
  uint64_t f1 = (ctx.opt.zNow ? DF_1_NOW : 0) | (ctx.opt.pie ? DF_1_PIE : 0);
  if (f1)
    add(DT_FLAGS_1, f1);
  add(DT_NULL, 0);
  return d;
}

void layoutEhFrame() {
  STEP("layoutEhFrame");
  ctx.pool->forEach(ctx.objects.size(), [&](size_t fi) {
    ObjectFile *o = ctx.objects[fi];
    if (o->eh.empty())
      return;
    const uint8_t *d = o->secData(o->ehSec);
    for (EhPiece &p : o->eh)
      if (p.cie != UINT32_MAX && p.target &&
          o->secs[p.target].osec != 0 && o->secs[p.target].live.load() == 1) {
        p.live = true;
        o->eh[p.cie].live = true;
        // An FDE for no code stays in .eh_frame, but the search table must
        // not return it for the address that follows.
        p.inHdr = p.size >= 16 && rd32(d + p.off + 12) != 0;
        o->numLiveFdes += p.inHdr;
      }
    uint32_t off = 0;
    for (EhPiece &p : o->eh)
      if (p.live) {
        p.outOff = off;
        off += p.size;
      }
    o->ehSize = off;
  });
  uint64_t total = 0;
  uint32_t fdes = 0;
  for (ObjectFile *o : ctx.objects) {
    o->ehOutOff = total;
    o->fdeBase = fdes;
    total += o->ehSize;
    fdes += o->numLiveFdes;
  }
  if (!total)
    return;
  L.ehFrame = newSection(".eh_frame", SHT_PROGBITS, SHF_ALLOC, 8);
  L.ehFrame->size = total + 4; // and a zero terminator
  L.numFdes = fdes;
  if (ctx.opt.ehFrameHdr) {
    L.ehHdr = newSection(".eh_frame_hdr", SHT_PROGBITS, SHF_ALLOC, 4);
    L.ehHdr->size = 12 + 8 * uint64_t(fdes);
  }
}

enum class SymOut { Skip, Local, Global };

SymOut classifyLocal(const ObjectFile *o, uint32_t k) {
  if (ctx.opt.discardLocals)
    return SymOut::Skip;
  const Elf64_Sym &s = o->syms[k];
  const int type = ELF64_ST_TYPE(s.st_info);
  if (type == STT_SECTION || s.st_name == 0)
    return SymOut::Skip;
  const char *n = o->symName(k);
  if (n[0] == '.' && n[1] == 'L')
    return SymOut::Skip;
  // Externalized locals of parallel code generation, outside code.
  if (s.st_shndx < SHN_LORESERVE && s.st_shndx != SHN_UNDEF &&
      !(o->shdrs[s.st_shndx].sh_flags & SHF_EXECINSTR) &&
      strstr(n, ".__pcg"))
    return SymOut::Skip;
  if (s.st_shndx == SHN_ABS)
    return SymOut::Local;
  if (s.st_shndx == SHN_UNDEF || s.st_shndx >= SHN_LORESERVE)
    return SymOut::Skip;
  return o->secs[s.st_shndx].osec != 0 ? SymOut::Local : SymOut::Skip;
}

SymOut classifyGlobal(const ObjectFile *o, uint32_t fi, uint32_t k) {
  const Elf64_Sym &s = o->syms[k];
  if (s.st_shndx == SHN_UNDEF)
    return SymOut::Skip;
  const Symbol &g = ctx.syms[o->nameIds[k - o->firstGlobal]];
  if (g.kind != Symbol::Object || g.file != fi || g.index != k)
    return SymOut::Skip;
  if (s.st_shndx != SHN_ABS &&
      (s.st_shndx >= SHN_LORESERVE || o->secs[s.st_shndx].osec == 0))
    return SymOut::Skip;
  int vis = ELF64_ST_VISIBILITY(s.st_other);
  if (ctx.versionIds && ctx.versionIds[o->nameIds[k - o->firstGlobal]] == 0)
    return SymOut::Local;
  return vis == STV_HIDDEN || vis == STV_INTERNAL ? SymOut::Local
                                                  : SymOut::Global;
}

void prepareSymtab() {
  STEP("prepareSymtab");
  ctx.pool->forEach(ctx.objects.size(), [&](size_t fi) {
    ObjectFile *o = ctx.objects[fi];
    uint32_t loc = 0, glob = 0;
    uint64_t bytes = 0;
    for (uint32_t k = 1; k < o->numSyms; ++k) {
      SymOut c = k < o->firstGlobal ? classifyLocal(o, k)
                                    : classifyGlobal(o, fi, k);
      if (c == SymOut::Skip)
        continue;
      (c == SymOut::Local ? loc : glob)++;
      bytes += strlen(o->symName(k)) + 1;
    }
    o->symLocals = loc;
    o->symGlobals = glob;
    o->strBytes = bytes;
  });
  uint64_t str = 1;
  collectNames<2>({&L.symHead, &L.symTail}, [](uint32_t id, auto &out) {
    const Symbol &s = ctx.syms[id];
    uint16_t f = ctx.flags[id].load(std::memory_order_relaxed);
    if (s.kind == Symbol::Linker)
      out[0].push_back(id);
    else if (s.kind == Symbol::Shared && (f & (StrongRef | WeakRef)))
      out[1].push_back(id);
    else if ((s.kind == Symbol::Undefined || s.kind == Symbol::DynUndef) &&
             (f & WeakRef))
      out[1].push_back(id);
  });
  auto byName = [](uint32_t a, uint32_t b) {
    return ctx.nameStr[a] < ctx.nameStr[b];
  };
  std::sort(L.symHead.begin(), L.symHead.end(), byName);
  std::sort(L.symTail.begin(), L.symTail.end(), byName);
  for (uint32_t id : L.symHead)
    str += ctx.nameStr[id].size() + 1;
  uint32_t local = 1 + L.symHead.size();
  for (ObjectFile *o : ctx.objects) {
    o->localBase = local;
    o->strBase = str;
    local += o->symLocals;
    str += o->strBytes;
  }
  L.numLocals = local;
  uint32_t global = local;
  for (ObjectFile *o : ctx.objects) {
    o->globalBase = global;
    global += o->symGlobals;
  }
  L.tailStrBase = str;
  for (uint32_t id : L.symTail)
    str += ctx.nameStr[id].size() + 1;
  global += L.symTail.size();
  L.symtab = newSection(".symtab", SHT_SYMTAB, 0, 8);
  L.symtab->size = uint64_t(global) * sizeof(Elf64_Sym);
  L.symtab->entsize = sizeof(Elf64_Sym);
  L.strtab = newSection(".strtab", SHT_STRTAB, 0, 1);
  L.strtab->size = str;
}

void prepareComment() {
  STEP("prepareComment");
  vector<string_view> seen;
  auto add = [&](string_view v) {
    if (v.empty() || std::find(seen.begin(), seen.end(), v) != seen.end())
      return;
    seen.push_back(v);
    L.commentData.append(v);
    L.commentData.push_back('\0');
  };
  L.commentData.assign(1, '\0');
  for (ObjectFile *o : ctx.objects)
    if (uint32_t i = o->commentSec) {
      const char *p = reinterpret_cast<const char *>(o->secData(i));
      const char *e = p + o->shdrs[i].sh_size;
      while (p < e) {
        size_t l = strnlen(p, e - p);
        add(string_view(p, l));
        p += l + 1;
      }
    }
  L.comment = newSection(".comment", SHT_PROGBITS, SHF_MERGE | SHF_STRINGS, 1);
  L.comment->entsize = 1;
  L.comment->size = L.commentData.size();
}

void layout() {
  markTime = Clock::now();
  layoutEhFrame();
  prepareComment();
  if (!ctx.opt.stripSymbols)
    prepareSymtab();
  buildDynamic();
  L.dynamic->size = dynamicEntries().size() * sizeof(Elf64_Dyn);
  assignAllOffsets();

  mark("offsets");
  L.ordered.assign(L.all.begin() + 1, L.all.end());
  std::stable_sort(L.ordered.begin(), L.ordered.end(),
                   [](const OutputSection *a, const OutputSection *b) {
                     return rankOf(a) < rankOf(b);
                   });
  for (size_t i = 0; i < L.ordered.size(); ++i)
    L.ordered[i]->shndx = i + 1;

  // Program headers: count first.
  bool hasTls = false;
  int classes[4] = {0, 0, 0, 0};
  int notes = 0;
  bool relro = false;
  for (OutputSection *s : L.ordered) {
    classes[segClass(s)] = 1;
    hasTls |= (s->flags & SHF_TLS) != 0;
    notes += s->type == SHT_NOTE;
    relro |= segClass(s) == 2;
  }
  classes[0] = 1;
  const bool dyn = L.interp != nullptr;
  size_t phnum = (dyn ? 2 : 0) + classes[0] + classes[1] + classes[2] +
                 classes[3] + 1 /*DYNAMIC*/ + (relro ? 1 : 0) + 1 /*STACK*/ +
                 (hasTls ? 1 : 0) + notes + (L.ehHdr ? 1 : 0);
  L.base = ctx.isPic() ? 0 : 0x400000;
  const uint64_t page = 4096;
  const uint64_t hdrSize = sizeof(Elf64_Ehdr) + phnum * sizeof(Elf64_Phdr);

  struct Seg {
    int cls;
    uint64_t va, off, filesz = 0, memsz = 0;
  };
  vector<Seg> loads;
  uint64_t va = L.base, off = 0;
  int cur = -1;
  bool inTls = false;
  uint64_t tlsCursor = 0;
  for (OutputSection *s : L.ordered) {
    if (!(s->flags & SHF_ALLOC))
      continue;
    int cls = segClass(s);
    if (cls != cur) {
      if (cur == -1) {
        loads.push_back({0, va, off});
        va += hdrSize;
        off += hdrSize;
        cur = 0;
      }
      if (cls != cur) {
        const Seg &prev = loads.back();
        va = alignTo(va, page);
        off = alignTo(prev.off + prev.filesz, page);
        loads.push_back({cls, va, off});
      }
      cur = cls;
    }
    Seg &seg = loads.back();
    if (s->flags & SHF_TLS) {
      if (!inTls) {
        inTls = true;
        va = alignTo(va, s->align);
        L.tlsAddr = va;
        tlsCursor = va;
      }
      L.tlsAlign = std::max(L.tlsAlign, s->align);
      tlsCursor = alignTo(tlsCursor, s->align);
      s->addr = tlsCursor;
      tlsCursor += s->size;
      L.tlsMemsz = tlsCursor - L.tlsAddr;
      if (s->type != SHT_NOBITS)
        va = tlsCursor;
      s->offset = seg.off + (s->addr - seg.va);
      if (s->type != SHT_NOBITS)
        seg.filesz = s->offset + s->size - seg.off;
      seg.memsz = std::max(seg.memsz, va - seg.va);
      continue;
    }
    inTls = false;
    va = alignTo(va, s->align);
    s->addr = va;
    s->offset = seg.off + (va - seg.va);
    va += s->size;
    if (s->type != SHT_NOBITS)
      seg.filesz = va - seg.va;
    seg.memsz = va - seg.va;
  }
  if (loads.size() && loads[0].memsz == 0)
    loads[0].memsz = loads[0].filesz = hdrSize;
  for (const Seg &sg : loads)
    off = std::max(off, sg.off + sg.filesz);

  // Linker-defined and symbol addresses.
  L.end = va;
  for (const Seg &sg : loads) {
    if (sg.cls == 1)
      L.textEnd = sg.va + sg.memsz;
    if (sg.cls >= 2)
      L.dataEnd = sg.va + sg.filesz;
  }
  L.bssStart = L.dataEnd;
  if (auto it = L.byName.find(".bss"); it != L.byName.end())
    L.bssStart = it->second->addr;

  vector<Elf64_Phdr> &ph = L.phdrs;
  auto addPh = [&](uint32_t type, uint32_t flags, uint64_t o, uint64_t v,
                   uint64_t fs, uint64_t ms, uint64_t al) {
    Elf64_Phdr p{};
    p.p_type = type;
    p.p_flags = flags;
    p.p_offset = o;
    p.p_vaddr = p.p_paddr = v;
    p.p_filesz = fs;
    p.p_memsz = ms;
    p.p_align = al;
    ph.push_back(p);
  };
  if (dyn) {
    addPh(PT_PHDR, PF_R, sizeof(Elf64_Ehdr), L.base + sizeof(Elf64_Ehdr),
          phnum * sizeof(Elf64_Phdr), phnum * sizeof(Elf64_Phdr), 8);
    addPh(PT_INTERP, PF_R, L.interp->offset, L.interp->addr, L.interp->size,
          L.interp->size, 1);
  }
  static const uint32_t segFlags[4] = {PF_R, PF_R | PF_X, PF_R | PF_W,
                                       PF_R | PF_W};
  for (const Seg &sg : loads)
    addPh(PT_LOAD, segFlags[sg.cls], sg.off, sg.va, sg.filesz, sg.memsz, page);
  if (hasTls) {
    uint64_t fs = 0, o = 0;
    for (OutputSection *s : L.ordered)
      if ((s->flags & SHF_TLS)) {
        if (!o)
          o = s->offset;
        if (s->type != SHT_NOBITS)
          fs = s->addr + s->size - L.tlsAddr;
      }
    addPh(PT_TLS, PF_R, o, L.tlsAddr, fs, L.tlsMemsz, L.tlsAlign);
  }
  addPh(PT_DYNAMIC, PF_R | PF_W, L.dynamic->offset, L.dynamic->addr,
        L.dynamic->size, L.dynamic->size, 8);
  if (relro)
    for (const Seg &sg : loads)
      if (sg.cls == 2)
        addPh(PT_GNU_RELRO, PF_R, sg.off, sg.va, sg.filesz,
              alignTo(sg.memsz, page), 1);
  if (L.ehHdr)
    addPh(PT_GNU_EH_FRAME, PF_R, L.ehHdr->offset, L.ehHdr->addr,
          L.ehHdr->size, L.ehHdr->size, 4);
  addPh(PT_GNU_STACK, PF_R | PF_W, 0, 0, 0, 0, 0);
  for (OutputSection *s : L.ordered)
    if (s->type == SHT_NOTE)
      addPh(PT_NOTE, PF_R, s->offset, s->addr, s->size, s->size, s->align);
  if (ph.size() != phnum)
{ for (auto &p : ph) fprintf(stderr, "ph %u\n", p.p_type); for (auto *s : L.ordered) fprintf(stderr, "%s rank %d\n", s->name.c_str(), rankOf(s)); fatal("internal error: program header count mismatch " + std::to_string(ph.size()) + " vs " + std::to_string(phnum)); }

  // Non-allocated sections follow the loaded image.
  for (OutputSection *s : L.ordered)
    if (!(s->flags & SHF_ALLOC)) {
      off = alignTo(off, s->align);
      s->offset = off;
      s->addr = 0;
      off += s->size;
    }
  // Section header string table and table placement.
  L.shstrtab.assign(1, '\0');
  for (OutputSection *s : L.ordered) {
    s->nameOff = L.shstrtab.size();
    L.shstrtab += s->name;
    L.shstrtab.push_back('\0');
  }
  L.shstrtabName = L.shstrtab.size();
  L.shstrtab += ".shstrtab";
  L.shstrtab.push_back('\0');
  L.shstrtabOff = off;
  off += L.shstrtab.size();
  L.shoff = alignTo(off, 8);
  L.fileSize = L.shoff + (L.ordered.size() + 2) * sizeof(Elf64_Shdr);

  mark("addresses");
  ctx.pool->forEach(ctx.objects.size(), [&](size_t fi) {
    ObjectFile *o = ctx.objects[fi];
    for (uint32_t i = 1; i < o->numShdrs; ++i) {
      const SectionState &ss = o->secs[i];
      if (ss.osec != 0 && ss.live.load(std::memory_order_relaxed) == 1) {
        PlacedSection &ps = ctx.placed[ss.placed];
        ps.va = L.all[ss.osec]->addr + ps.outOff;
      }
    }
  }, 8);
  finalizeMerge();
  // Symbol addresses.
  ctx.pool->forEach(ctx.numNames, [&](size_t id) {
    Symbol &s = ctx.syms[id];
    if (s.kind == Symbol::Object)
      s.va = defVA(ctx.objects[s.file], s.index);
    else if (s.copied)
      s.va = L.copyRel->addr + s.copyOff;
  });
  auto secAddr = [](const char *n, bool endOf) -> uint64_t {
    auto it = L.byName.find(n);
    if (it == L.byName.end())
      return 0;
    return it->second->addr + (endOf ? it->second->size : 0);
  };
  for (uint32_t id = 0; id < ctx.numNames; ++id) {
    Symbol &s = ctx.syms[id];
    if (s.kind != Symbol::Linker)
      continue;
    string_view n = ctx.nameStr[id];
    if (n == "__ehdr_start" || n == "__executable_start" || n == "__dso_handle")
      s.va = L.base;
    else if (n == "_DYNAMIC")
      s.va = L.dynamic->addr;
    else if (n == "_GLOBAL_OFFSET_TABLE_")
      s.va = L.gotPlt->addr;
    else if (n == "__init_array_start")
      s.va = secAddr(".init_array", false);
    else if (n == "__init_array_end")
      s.va = secAddr(".init_array", true);
    else if (n == "__fini_array_start")
      s.va = secAddr(".fini_array", false);
    else if (n == "__fini_array_end")
      s.va = secAddr(".fini_array", true);
    else if (n == "__preinit_array_start")
      s.va = secAddr(".preinit_array", false);
    else if (n == "__preinit_array_end")
      s.va = secAddr(".preinit_array", true);
    else if (n == "_end" || n == "end")
      s.va = L.end;
    else if (n == "_etext" || n == "etext")
      s.va = L.textEnd;
    else if (n == "_edata" || n == "edata")
      s.va = L.dataEnd;
    else if (n == "__bss_start")
      s.va = L.bssStart;
    else if (n == "__GNU_EH_FRAME_HDR")
      s.va = L.ehHdr ? L.ehHdr->addr : 0;
    else if (n.rfind("__start_", 0) == 0)
      s.va = secAddr(string(n.substr(8)).c_str(), false);
    else if (n.rfind("__stop_", 0) == 0)
      s.va = secAddr(string(n.substr(7)).c_str(), true);
    else
      s.va = 0;
  }

  mark("symbolVAs");
  // .rela.dyn slots, in output address order.
  uint32_t rel = 0, sym = L.numRelative + L.numGotSym;
  for (OutputSection *os : L.ordered)
    for (auto [o, i] : os->members) {
      PlacedSection &ss = placedOf(o, i);
      ss.relBase = rel;
      ss.symBase = sym;
      rel += ss.numRel;
      sym += ss.numSym;
    }
}

// ================================================================ phase 6: write
inline void w32(uint8_t *p, uint64_t v) {
  uint32_t x = uint32_t(v);
  memcpy(p, &x, 4);
}
inline void w64(uint8_t *p, uint64_t v) { memcpy(p, &v, 8); }

uint64_t symbolVA(const ObjectFile *o, uint32_t si, const RelInfo &ri) {
  if (!ri.global)
    return defVA(o, si);
  const Symbol &s = ctx.syms[ri.id];
  // Calls to symbols the dynamic loader binds go through the PLT.
  if (ri.imp && !s.copied && s.plt != UINT32_MAX)
    return pltVA(s.plt);
  if (s.isImport() && !s.copied)
    return 0;
  return s.va;
}

uint64_t symbolSize(const ObjectFile *o, uint32_t si, const RelInfo &ri) {
  if (!ri.global)
    return o->syms[si].st_size;
  const Symbol &s = ctx.syms[ri.id];
  if (s.kind == Symbol::Object)
    return ctx.objects[s.file]->syms[s.index].st_size;
  if (s.kind == Symbol::Shared)
    return ctx.shared[s.file]->dynsyms[s.index].st_size;
  return 0;
}

void relaxIeToLe(uint8_t *loc, const string &where) {
  uint8_t *inst = loc - 3;
  uint8_t reg = (loc[-1] >> 3) & 7;
  if (memcmp(inst, "\x48\x03\x25", 3) == 0) {
    memcpy(inst, "\x48\x81\xc4", 3);
  } else if (memcmp(inst, "\x4c\x03\x25", 3) == 0) {
    memcpy(inst, "\x49\x81\xc4", 3);
  } else if (memcmp(inst, "\x4c\x03", 2) == 0) {
    memcpy(inst, "\x4d\x8d", 2);
    inst[2] = 0x80 | (reg << 3) | reg;
  } else if (memcmp(inst, "\x48\x03", 2) == 0) {
    memcpy(inst, "\x48\x8d", 2);
    inst[2] = 0x80 | (reg << 3) | reg;
  } else if (memcmp(inst, "\x4c\x8b", 2) == 0) {
    memcpy(inst, "\x49\xc7", 2);
    inst[2] = 0xc0 | reg;
  } else if (memcmp(inst, "\x48\x8b", 2) == 0) {
    memcpy(inst, "\x48\xc7", 2);
    inst[2] = 0xc0 | reg;
  } else {
    fatal(where + ": internal error: unchecked initial-exec TLS instruction");
  }
}

Elf64_Rela rela(uint64_t off, uint32_t sym, uint32_t type, int64_t addend) {
  Elf64_Rela r;
  r.r_offset = off;
  r.r_info = ELF64_R_INFO(uint64_t(sym), type);
  r.r_addend = addend;
  return r;
}

// Applies relocations; dst and base map input offset 0 of the section.
// Dynamic relocations go to relOut/symOut, which advance.
void applyRelocs(const ObjectFile *o, const uint8_t *src, const Elf64_Rela *rels,
                 size_t n, uint8_t *dst, uint64_t base,
                 Elf64_Rela *relOut = nullptr, Elf64_Rela *symOut = nullptr,
                 const uint8_t *kinds = nullptr) {
  const uint64_t gotBase = L.gotPlt->addr;
  for (size_t k = 0; k < n; ++k) {
    const Elf64_Rela &r = rels[k];
    RelInfo ri;
    if (kinds) {
      ri.kind = Kind(kinds[k]);
      const uint32_t si = ELF64_R_SYM(r.r_info);
      ri.global = si >= o->firstGlobal;
      if (ri.global) {
        ri.id = o->nameIds[si - o->firstGlobal];
        ri.imp = isPreemptible(ri.id);
      }
    } else {
      ri = classify(o, src, r);
    }
    if (ri.kind == K_Skip)
      continue;
    const uint32_t si = ELF64_R_SYM(r.r_info);
    uint8_t *loc = dst + r.r_offset;
    const uint64_t P = base + r.r_offset;
    const int64_t A = r.r_addend;
    uint64_t S = symbolVA(o, si, ri);
    // A section symbol locates the piece with its addend.
    if (!ri.global && o->syms[si].st_shndx < SHN_LORESERVE &&
        ELF64_ST_TYPE(o->syms[si].st_info) == STT_SECTION) {
      const SectionState &ts = o->secs[o->syms[si].st_shndx];
      if (ts.osec && ctx.placed[ts.placed].merged)
        S = mergedAddress(o, o->syms[si].st_shndx, A) - A;
    }
    switch (ri.kind) {
    case K_Pc32:
    case K_Plt32:
      w32(loc, S + A - P);
      break;
    case K_Pc64:
      w64(loc, S + A - P);
      break;
    case K_GotRelax: {
      uint64_t val = S + A - P;
      if (loc[-2] == 0x8b) {
        loc[-2] = 0x8d;
        w32(loc, val);
      } else if (loc[-1] == 0x15) {
        loc[-2] = 0x67;
        loc[-1] = 0xe8;
        w32(loc, val);
      } else {
        loc[-2] = 0xe9;
        loc[3] = 0x90;
        w32(loc - 1, val + 1);
      }
      break;
    }
    case K_GotPc:
      w32(loc, gotVA(ctx.syms[ri.id].got) + A - P);
      break;
    case K_GotTpPc:
      w32(loc, gotVA(ctx.syms[ri.id].gotTp) + A - P);
      break;
    case K_IeToLe:
      relaxIeToLe(loc, o->name);
      w32(loc, tpoff(S) + A + 4);
      break;
    case K_TpOff32:
      w32(loc, tpoff(S) + A);
      break;
    case K_Abs64:
      w64(loc, S + A);
      if (relOut) {
        if (ri.imp && !ctx.syms[ri.id].copied) {
          *symOut++ = rela(P, ctx.syms[ri.id].dynsym, R_X86_64_64, A);
        } else if (ri.imp) {
          *symOut++ = rela(P, ctx.syms[ri.id].dynsym, R_X86_64_64, A);
        } else if (ctx.isPic() &&
                   !(ri.global && ctx.syms[ri.id].kind == Symbol::Undefined)) {
          *relOut++ = rela(P, 0, R_X86_64_RELATIVE, S + A);
        }
      }
      break;
    case K_Abs32:
      w32(loc, S + A);
      break;
    case K_Size32:
      w32(loc, symbolSize(o, si, ri) + A);
      break;
    case K_Size64:
      w64(loc, symbolSize(o, si, ri) + A);
      break;
    case K_GotOff64:
      w64(loc, S + A - gotBase);
      break;
    case K_GotPc32:
      w32(loc, gotBase + A - P);
      break;
    case K_GdToLe: {
      static const uint8_t inst[] = {0x64, 0x48, 0x8b, 0x04, 0x25, 0, 0, 0,
                                     0,    0x48, 0x8d, 0x80, 0,    0, 0, 0};
      memcpy(loc - 4, inst, sizeof(inst));
      w32(loc + 8, tpoff(S) + A + 4);
      ++k;
      break;
    }
    case K_GdToIe: {
      static const uint8_t inst[] = {0x64, 0x48, 0x8b, 0x04, 0x25, 0, 0, 0,
                                     0,    0x48, 0x03, 0x05, 0,    0, 0, 0};
      memcpy(loc - 4, inst, sizeof(inst));
      w32(loc + 8, gotVA(ctx.syms[ri.id].gotTp) + A - P - 8);
      ++k;
      break;
    }
    case K_LdToLe: {
      if (loc[4] == 0xe8) {
        static const uint8_t inst[] = {0x66, 0x66, 0x66, 0x64, 0x48, 0x8b,
                                       0x04, 0x25, 0,    0,    0,    0};
        memcpy(loc - 3, inst, sizeof(inst));
      } else if (loc[4] == 0xff && loc[5] == 0x15) {
        static const uint8_t inst[] = {0x66, 0x66, 0x66, 0x66, 0x64,
                                       0x48, 0x8b, 0x04, 0x25, 0,
                                       0,    0,    0};
        memcpy(loc - 3, inst, sizeof(inst));
      } else {
        fatal(o->name + ": unsupported local-dynamic TLS sequence");
      }
      ++k;
      break;
    }
    case K_DtpOff32:
      w32(loc, (ctx.opt.shared ? S - L.tlsAddr : tpoff(S)) + A);
      break;
    case K_DtpOff64:
      w64(loc, (ctx.opt.shared ? S - L.tlsAddr : tpoff(S)) + A);
      break;
    case K_TlsGd:
      w32(loc, gotVA(ctx.syms[ri.id].tlsGd) + A - P);
      break;
    case K_TlsLd:
      w32(loc, gotVA(L.tlsLdSlot) + A - P);
      break;
    case K_Skip:
      break;
    }
  }
}

// Relocations in debug sections: addresses of code and data, offsets into
// other debug sections and offsets of TLS variables. References to discarded
// code get a tombstone, which location and range lists must not end on.
void applyDebugRelocs(const ObjectFile *o, uint32_t sec, uint8_t *dst) {
  auto [rels, n] = o->relas(sec);
  const char *name = o->secName(sec);
  const uint64_t tombstone =
      strcmp(name, ".debug_loc") == 0 || strcmp(name, ".debug_ranges") == 0;
  for (size_t k = 0; k < n; ++k) {
    const Elf64_Rela &r = rels[k];
    const uint32_t type = ELF64_R_TYPE(r.r_info), si = ELF64_R_SYM(r.r_info);
    if (type == R_X86_64_NONE)
      continue;
    const int64_t A = r.r_addend;
    uint64_t S;
    bool dead = false;
    if (si >= o->firstGlobal) {
      const uint32_t id = o->nameIds[si - o->firstGlobal];
      const Symbol &s = ctx.syms[id];
      if (s.kind == Symbol::Object) {
        auto [file, dsec] = ctx.defTarget[id];
        dead = file != UINT32_MAX && ctx.objects[file]->secs[dsec].osec == 0;
      }
      S = s.isImport() ? 0 : s.va;
    } else {
      const Elf64_Sym &ls = o->syms[si];
      if (ls.st_shndx != SHN_ABS && ls.st_shndx < SHN_LORESERVE &&
          ls.st_shndx != SHN_UNDEF && o->secs[ls.st_shndx].osec == 0)
        dead = true;
      S = defVA(o, si);
      if (!dead && ELF64_ST_TYPE(ls.st_info) == STT_SECTION &&
          ctx.placed[o->secs[ls.st_shndx].placed].merged)
        S = mergedAddress(o, ls.st_shndx, A) - A;
    }
    uint8_t *loc = dst + r.r_offset;
    switch (type) {
    case R_X86_64_32:
      w32(loc, dead ? tombstone : S + A);
      break;
    case R_X86_64_64:
      w64(loc, dead ? tombstone : S + A);
      break;
    case R_X86_64_DTPOFF32:
      w32(loc, dead ? tombstone : S + A - L.tlsAddr);
      break;
    case R_X86_64_DTPOFF64:
      w64(loc, dead ? tombstone : S + A - L.tlsAddr);
      break;
    }
  }
}

void writeInputSection(uint8_t *buf, const OutputSection *os, ObjectFile *o,
                       uint32_t sec) {
  const Elf64_Shdr &sh = o->shdrs[sec];
  if (os->type == SHT_NOBITS)
    return;
  const PlacedSection &ss = placedOf(o, sec);
  uint8_t *dst = buf + os->offset + ss.outOff;
  // The output file may hold stale bytes; clear what nothing else writes.
  memset(dst - ss.padBefore, 0, ss.padBefore);
  if (sh.sh_type == SHT_NOBITS) {
    memset(dst, 0, sh.sh_size);
    return;
  }
  const uint8_t *src = o->secData(sec);
  if (ss.merged) {
    // The pieces this section holds, with the padding between them cleared.
    uint8_t *const osecStart = buf + os->offset;
    uint64_t cur = ss.outOff;
    const auto *p = ctx.pieces.get() + ss.pieceBase;
    for (uint32_t k = 0; k < ss.numPieces; ++k) {
      if (!(p[k].id & MergeOwner))
        continue;
      const uint32_t end = k + 1 < ss.numPieces ? p[k + 1].inOff : sh.sh_size;
      memset(osecStart + cur, 0, p[k].out - cur);
      memcpy(osecStart + p[k].out, src + p[k].inOff, end - p[k].inOff);
      cur = p[k].out + (end - p[k].inOff);
    }
    memset(osecStart + cur, 0, ss.outOff + ss.mergedSize - cur);
    return;
  }
  memcpy(dst, src, sh.sh_size);
  if (!(sh.sh_flags & SHF_ALLOC)) {
    applyDebugRelocs(o, sec, dst);
    return;
  }
  auto [rels, n] = o->relas(sec);
  applyRelocs(o, src, rels, n, dst, ss.va, L.relaOut + ss.relBase,
              L.relaOut + ss.symBase, relKinds + ss.kindBase);
}


void writeSynthetic(uint8_t *buf) {
  STEP("synthetic");
  if (L.interp)
    memcpy(buf + L.interp->offset, ctx.opt.dynamicLinker.c_str(),
           L.interp->size);
  if (L.ehFrame)
    w32(buf + L.ehFrame->offset + L.ehFrame->size - 4, 0);
  if (L.buildId) {
    uint8_t *p = buf + L.buildId->offset;
    w32(p, 4);
    w32(p + 4, ctx.opt.buildIdSize);
    w32(p + 8, NT_GNU_BUILD_ID);
    memcpy(p + 12, "GNU", 4);
    memset(p + 16, 0, L.buildId->size - 16);
    if (!ctx.opt.buildIdBytes.empty())
      memcpy(p + 16, ctx.opt.buildIdBytes.data(), ctx.opt.buildIdSize);
  }
  // .dynsym
  {
    auto *d = reinterpret_cast<Elf64_Sym *>(buf + L.dynsym->offset);
    memset(d, 0, sizeof(Elf64_Sym));
    uint32_t i = 1;
    for (uint32_t id : L.imports) {
      const Symbol &s = ctx.syms[id];
      uint16_t f = ctx.flags[id].load();
      Elf64_Sym &e = d[i];
      e = {};
      e.st_name = L.nameDynstr[i];
      int bind = !(f & StrongRef) && (f & WeakRef) ? STB_WEAK : STB_GLOBAL;
      int type = STT_NOTYPE;
      if (s.kind == Symbol::Shared) {
        const Elf64_Sym &ds = ctx.shared[s.file]->dynsyms[s.index];
        type = ELF64_ST_TYPE(ds.st_info);
        e.st_size = ds.st_size;
      }
      e.st_info = ELF64_ST_INFO(bind, type);
      if (f & CanonicalPlt)
        e.st_value = pltVA(s.plt);
      ++i;
    }
    for (uint32_t id : L.exports) {
      const Symbol &s = ctx.syms[id];
      if (s.copied) {
        const Elf64_Sym &ds = ctx.shared[s.file]->dynsyms[s.index];
        Elf64_Sym &e = d[i];
        e = {};
        e.st_name = L.nameDynstr[i];
        e.st_info = ds.st_info;
        e.st_value = s.va;
        e.st_size = ds.st_size;
        e.st_shndx = L.copyRel->shndx;
        ++i;
        continue;
      }
      const ObjectFile *o = ctx.objects[s.file];
      const Elf64_Sym &os = o->syms[s.index];
      Elf64_Sym &e = d[i];
      e = {};
      e.st_name = L.nameDynstr[i];
      e.st_info = os.st_info;
      e.st_other = os.st_other;
      // TLS symbols' values are offsets in the TLS segment.
      e.st_value = ELF64_ST_TYPE(os.st_info) == STT_TLS ? s.va - L.tlsAddr : s.va;
      e.st_size = os.st_size;
      if (os.st_shndx == SHN_ABS)
        e.st_shndx = SHN_ABS;
      else
        e.st_shndx = L.all[o->secs[os.st_shndx].osec]->shndx;
      ++i;
    }
  }
  if (L.verdef) {
    uint8_t *p = buf + L.verdef->offset;
    for (size_t i = 0; i < L.verdefs.size(); ++i, p += 28) {
      auto *vd = reinterpret_cast<Elf64_Verdef *>(p);
      vd->vd_version = 1;
      vd->vd_flags = i == 0 ? VER_FLG_BASE : 0;
      vd->vd_ndx = uint16_t(i + 1);
      vd->vd_cnt = 1;
      vd->vd_hash = L.verdefs[i].hash;
      vd->vd_aux = 20;
      vd->vd_next = i + 1 < L.verdefs.size() ? 28 : 0;
      auto *aux = reinterpret_cast<Elf64_Verdaux *>(p + 20);
      aux->vda_name = L.verdefs[i].name;
      aux->vda_next = 0;
    }
  }
  if (L.versym)
    memcpy(buf + L.versym->offset, L.versyms.data(), 2 * L.versyms.size());
  if (L.verneed) {
    uint8_t *p = buf + L.verneed->offset;
    for (size_t i = 0; i < L.verneeds.size(); ++i) {
      const auto &v = L.verneeds[i];
      auto *vn = reinterpret_cast<Elf64_Verneed *>(p);
      vn->vn_version = 1;
      vn->vn_cnt = v.aux.size();
      vn->vn_file = v.file;
      vn->vn_aux = 16;
      vn->vn_next = i + 1 < L.verneeds.size() ? 16 * (1 + v.aux.size()) : 0;
      p += 16;
      for (size_t k = 0; k < v.aux.size(); ++k) {
        auto *a = reinterpret_cast<Elf64_Vernaux *>(p);
        a->vna_hash = v.aux[k].hash;
        a->vna_flags = 0;
        a->vna_other = v.aux[k].idx;
        a->vna_name = v.aux[k].name;
        a->vna_next = k + 1 < v.aux.size() ? 16 : 0;
        p += 16;
      }
    }
  }
  // .gnu.hash
  {
    uint8_t *p = buf + L.gnuHashSec->offset;
    w32(p, L.gnuBuckets);
    w32(p + 4, L.gnuSymOffset);
    w32(p + 8, 1);
    w32(p + 12, 26);
    w64(p + 16, L.gnuBloom);
    memcpy(p + 24, L.gnuBucketVals.data(), 4 * L.gnuBuckets);
    memcpy(p + 24 + 4 * L.gnuBuckets, L.gnuChain.data(), 4 * L.gnuChain.size());
  }
  memcpy(buf + L.dynstr->offset, L.dynstrData.data(), L.dynstrData.size());
  // .got and its dynamic relocations
  Elf64_Rela *relative = L.relaOut + L.numSecRel;
  Elf64_Rela *symbolic = L.relaOut + L.numRelative;
  if (L.got) {
    uint8_t *g = buf + L.got->offset;
    for (uint32_t id : L.gotIds) {
      const Symbol &s = ctx.syms[id];
      uint64_t slotVA = gotVA(s.got);
      if (isPreemptible(id)) {
        w64(g + 8 * s.got, s.copied || !s.isImport() ? s.va : 0);
        *symbolic++ = rela(slotVA, s.dynsym, R_X86_64_GLOB_DAT, 0);
      } else if (s.kind == Symbol::Undefined) {
        w64(g + 8 * s.got, 0);
      } else {
        w64(g + 8 * s.got, s.va);
        if (ctx.isPic())
          *relative++ = rela(slotVA, 0, R_X86_64_RELATIVE, s.va);
      }
    }
    for (uint32_t id : L.gotTpIds) {
      const Symbol &s = ctx.syms[id];
      w64(g + 8 * s.gotTp, 0);
      if (isPreemptible(id))
        *symbolic++ = rela(gotVA(s.gotTp), s.dynsym, R_X86_64_TPOFF64, 0);
      else
        *symbolic++ =
            rela(gotVA(s.gotTp), 0, R_X86_64_TPOFF64, s.va - L.tlsAddr);
    }
    // General-dynamic pairs: module and offset; the module's own pair has
    // offset 0.
    for (uint32_t id : L.tlsGdIds) {
      const Symbol &s = ctx.syms[id];
      const uint64_t va = gotVA(s.tlsGd);
      if (isPreemptible(id)) {
        w64(g + 8 * s.tlsGd, 0);
        w64(g + 8 * (s.tlsGd + 1), 0);
        *symbolic++ = rela(va, s.dynsym, R_X86_64_DTPMOD64, 0);
        *symbolic++ = rela(va + 8, s.dynsym, R_X86_64_DTPOFF64, 0);
      } else {
        w64(g + 8 * s.tlsGd, 0);
        w64(g + 8 * (s.tlsGd + 1), s.va - L.tlsAddr);
        *symbolic++ = rela(va, 0, R_X86_64_DTPMOD64, 0);
      }
    }
    if (L.tlsLdSlot != UINT32_MAX) {
      w64(g + 8 * L.tlsLdSlot, 0);
      w64(g + 8 * (L.tlsLdSlot + 1), 0);
      *symbolic++ = rela(gotVA(L.tlsLdSlot), 0, R_X86_64_DTPMOD64, 0);
    }
  }
  for (uint32_t id : L.exports) {
    const Symbol &s = ctx.syms[id];
    if (s.copyPrimary)
      *symbolic++ = rela(s.va, s.dynsym, R_X86_64_COPY, 0);
  }
  if (relative != L.relaOut + L.numSecRel + L.numGotRel ||
      symbolic != L.relaOut + L.numRelative + L.numGotSym)
    fatal("internal error: .rela.dyn count mismatch");
  // .plt, .got.plt, .rela.plt
  {
    uint8_t *gp = buf + L.gotPlt->offset;
    w64(gp, L.dynamic->addr);
    w64(gp + 8, 0);
    w64(gp + 16, 0);
    if (L.plt) {
      uint8_t *p = buf + L.plt->offset;
      const uint64_t pltAddr = L.plt->addr, gotPlt = L.gotPlt->addr;
      static const uint8_t header[16] = {0xff, 0x35, 0, 0, 0, 0, 0xff, 0x25,
                                         0,    0,    0, 0, 0x0f, 0x1f, 0x40, 0};
      memcpy(p, header, 16);
      w32(p + 2, gotPlt + 8 - (pltAddr + 6));
      w32(p + 8, gotPlt + 16 - (pltAddr + 12));
      auto *rp = reinterpret_cast<Elf64_Rela *>(buf + L.relaPlt->offset);
      for (uint32_t i = 0; i < L.gotPltIds.size(); ++i) {
        const Symbol &s = ctx.syms[L.gotPltIds[i]];
        uint8_t *e = p + 16 * (i + 1);
        const uint64_t ea = pltAddr + 16 * (i + 1);
        const uint64_t slot = gotPlt + 8 * (3 + i);
        static const uint8_t entry[16] = {0xff, 0x25, 0, 0, 0, 0, 0x68, 0,
                                          0,    0,    0, 0xe9, 0, 0, 0, 0};
        memcpy(e, entry, 16);
        w32(e + 2, slot - (ea + 6));
        w32(e + 7, i);
        w32(e + 12, pltAddr - (ea + 16));
        w64(gp + 8 * (3 + i), ea + 6);
        rp[i] = rela(slot, s.dynsym, R_X86_64_JUMP_SLOT, 0);
      }
    }
  }
  // .dynamic
  {
    vector<Elf64_Dyn> d = dynamicEntries();
    memcpy(buf + L.dynamic->offset, d.data(), d.size() * sizeof(Elf64_Dyn));
  }
}

void writeEhFrame(uint8_t *buf, ObjectFile *o) {
  uint8_t *out = buf + L.ehFrame->offset + o->ehOutOff;
  const uint64_t va = L.ehFrame->addr + o->ehOutOff;
  const uint8_t *d = o->secData(o->ehSec);
  auto [rels, n] = o->relas(o->ehSec);
  (void)n;
  for (const EhPiece &p : o->eh) {
    if (!p.live)
      continue;
    memcpy(out + p.outOff, d + p.off, p.size);
    if (p.cie != UINT32_MAX)
      w32(out + p.outOff + 4, p.outOff + 4 - o->eh[p.cie].outOff);
    int64_t delta = int64_t(p.outOff) - int64_t(p.off);
    applyRelocs(o, d, rels + p.relBegin, p.relEnd - p.relBegin, out + delta,
                va + delta);
  }
}

void writeEhFrameHdr(uint8_t *buf) {
  STEP("ehFrameHdr");
  vector<std::pair<uint64_t, uint64_t>> table(L.numFdes);
  ctx.pool->forEach(ctx.objects.size(), [&](size_t fi) {
    ObjectFile *o = ctx.objects[fi];
    if (!o->numLiveFdes)
      return;
    auto [rels, n] = o->relas(o->ehSec);
    (void)n;
    uint32_t j = o->fdeBase;
    for (const EhPiece &p : o->eh) {
      if (!p.inHdr)
        continue;
      const Elf64_Rela &r = rels[p.relBegin];
      RelInfo ri = classify(o, nullptr, r);
      uint64_t pc = symbolVA(o, ELF64_R_SYM(r.r_info), ri) + r.r_addend;
      table[j++] = {pc, L.ehFrame->addr + o->ehOutOff + p.outOff};
    }
  });
  std::sort(table.begin(), table.end());
  uint8_t *p = buf + L.ehHdr->offset;
  const uint64_t hdr = L.ehHdr->addr;
  p[0] = 1;
  p[1] = 0x1b; // pcrel | sdata4
  p[2] = 0x03; // udata4
  p[3] = 0x3b; // datarel | sdata4
  w32(p + 4, L.ehFrame->addr - (hdr + 4));
  w32(p + 8, table.size());
  for (size_t i = 0; i < table.size(); ++i) {
    w32(p + 12 + 8 * i, table[i].first - hdr);
    w32(p + 16 + 8 * i, table[i].second - hdr);
  }
}

uint64_t symtabValue(uint64_t va, int type) {
  return type == STT_TLS ? va - L.tlsAddr : va;
}

void writeSymtabFile(uint8_t *buf, uint32_t fi) {
  ObjectFile *o = ctx.objects[fi];
  auto *sym = reinterpret_cast<Elf64_Sym *>(buf + L.symtab->offset);
  char *str = reinterpret_cast<char *>(buf + L.strtab->offset);
  uint32_t li = o->localBase, gi = o->globalBase;
  uint64_t so = o->strBase;
  for (uint32_t k = 1; k < o->numSyms; ++k) {
    SymOut c = k < o->firstGlobal ? classifyLocal(o, k) : classifyGlobal(o, fi, k);
    if (c == SymOut::Skip)
      continue;
    const Elf64_Sym &in = o->syms[k];
    Elf64_Sym &e = sym[c == SymOut::Local ? li++ : gi++];
    const char *n = o->symName(k);
    size_t len = strlen(n);
    memcpy(str + so, n, len + 1);
    e.st_name = so;
    so += len + 1;
    int type = ELF64_ST_TYPE(in.st_info);
    int bind = c == SymOut::Local ? STB_LOCAL : ELF64_ST_BIND(in.st_info);
    e.st_info = ELF64_ST_INFO(bind, type);
    e.st_other = in.st_other;
    e.st_size = in.st_size;
    if (in.st_shndx == SHN_ABS) {
      e.st_shndx = SHN_ABS;
      e.st_value = in.st_value;
    } else {
      e.st_shndx = L.all[o->secs[in.st_shndx].osec]->shndx;
      e.st_value = symtabValue(defVA(o, k), type);
    }
  }
}

void writeSymtabEnds(uint8_t *buf) {
  auto *sym = reinterpret_cast<Elf64_Sym *>(buf + L.symtab->offset);
  char *str = reinterpret_cast<char *>(buf + L.strtab->offset);
  memset(sym, 0, sizeof(Elf64_Sym));
  str[0] = 0;
  uint64_t so = 1;
  uint32_t i = 1;
  auto name = [&](Elf64_Sym &e, uint32_t id) {
    string_view n = ctx.nameStr[id];
    memcpy(str + so, n.data(), n.size());
    str[so + n.size()] = 0;
    e.st_name = so;
    so += n.size() + 1;
  };
  for (uint32_t id : L.symHead) {
    Elf64_Sym &e = sym[i++];
    e = {};
    name(e, id);
    e.st_info = ELF64_ST_INFO(STB_LOCAL, STT_NOTYPE);
    e.st_other = STV_HIDDEN;
    e.st_shndx = SHN_ABS;
    e.st_value = ctx.syms[id].va;
  }
  so = L.tailStrBase;
  i = L.symtab->size / sizeof(Elf64_Sym) - L.symTail.size();
  for (uint32_t id : L.symTail) {
    const Symbol &s = ctx.syms[id];
    uint16_t f = ctx.flags[id].load(std::memory_order_relaxed);
    Elf64_Sym &e = sym[i++];
    e = {};
    name(e, id);
    int bind = !(f & StrongRef) && (f & WeakRef) ? STB_WEAK : STB_GLOBAL;
    int type = STT_NOTYPE;
    if (s.kind == Symbol::Shared) {
      const Elf64_Sym &ds = ctx.shared[s.file]->dynsyms[s.index];
      type = ELF64_ST_TYPE(ds.st_info);
      if (s.copied) {
        e.st_shndx = L.copyRel->shndx;
        e.st_value = s.va;
        e.st_size = ds.st_size;
      } else if (f & CanonicalPlt) {
        e.st_value = pltVA(s.plt);
      }
    }
    e.st_info = ELF64_ST_INFO(bind, type);
  }
}

void writeHeaders(uint8_t *buf) {
  auto *eh = reinterpret_cast<Elf64_Ehdr *>(buf);
  memset(eh, 0, sizeof(*eh));
  memcpy(eh->e_ident, ELFMAG, 4);
  eh->e_ident[EI_CLASS] = ELFCLASS64;
  eh->e_ident[EI_DATA] = ELFDATA2LSB;
  eh->e_ident[EI_VERSION] = EV_CURRENT;
  eh->e_type = ctx.isPic() ? ET_DYN : ET_EXEC;
  eh->e_machine = EM_X86_64;
  eh->e_version = EV_CURRENT;
  uint32_t id = ctx.names->find("_start", 6);
  eh->e_entry = id != UINT32_MAX && !ctx.opt.shared ? ctx.syms[id].va : 0;
  eh->e_phoff = sizeof(Elf64_Ehdr);
  eh->e_shoff = L.shoff;
  eh->e_ehsize = sizeof(Elf64_Ehdr);
  eh->e_phentsize = sizeof(Elf64_Phdr);
  eh->e_phnum = L.phdrs.size();
  eh->e_shentsize = sizeof(Elf64_Shdr);
  eh->e_shnum = L.ordered.size() + 2;
  eh->e_shstrndx = L.ordered.size() + 1;
  memcpy(buf + sizeof(Elf64_Ehdr), L.phdrs.data(),
         L.phdrs.size() * sizeof(Elf64_Phdr));
  memcpy(buf + L.shstrtabOff, L.shstrtab.data(), L.shstrtab.size());
  auto *sh = reinterpret_cast<Elf64_Shdr *>(buf + L.shoff);
  memset(sh, 0, sizeof(Elf64_Shdr));
  for (OutputSection *s : L.ordered) {
    Elf64_Shdr &h = sh[s->shndx];
    h = {};
    h.sh_name = s->nameOff;
    h.sh_type = s->type;
    h.sh_flags = s->flags;
    h.sh_addr = s->addr;
    h.sh_offset = s->offset;
    h.sh_size = s->size;
    h.sh_addralign = s->align;
    h.sh_entsize = s->entsize;
  }
  auto link = [&](OutputSection *s, OutputSection *to, uint32_t info) {
    if (s && to) {
      sh[s->shndx].sh_link = to->shndx;
      sh[s->shndx].sh_info = info;
    }
  };
  link(L.dynsym, L.dynstr, 1);
  link(L.gnuHashSec, L.dynsym, 0);
  link(L.versym, L.dynsym, 0);
  link(L.verneed, L.dynstr, L.verneeds.size());
  link(L.verdef, L.dynstr, L.verdefs.size());
  link(L.dynamic, L.dynstr, 0);
  link(L.relaDyn, L.dynsym, 0);
  link(L.relaPlt, L.dynsym, L.gotPlt->shndx);
  link(L.symtab, L.strtab, L.numLocals);
  Elf64_Shdr &st = sh[L.ordered.size() + 1];
  st = {};
  st.sh_name = L.shstrtabName;
  st.sh_type = SHT_STRTAB;
  st.sh_offset = L.shstrtabOff;
  st.sh_size = L.shstrtab.size();
  st.sh_addralign = 1;
}

void writeBuildId(uint8_t *buf) {
  STEP("buildId");
  if (!L.buildId || !ctx.opt.buildIdBytes.empty())
    return;
  const size_t chunk = 1 << 20;
  const size_t n = (L.fileSize + chunk - 1) / chunk;
  vector<uint64_t> h(n);
  ctx.pool->forEach(n, [&](size_t i) {
    size_t b = i * chunk, e = std::min<size_t>(L.fileSize, b + chunk);
    h[i] = hashBulk(buf + b, e - b);
  }, 1);
  uint8_t *p = buf + L.buildId->offset + 16;
  const uint64_t whole =
      hashBytes(reinterpret_cast<const char *>(h.data()), h.size() * 8);
  for (unsigned k = 0; k * 8 < ctx.opt.buildIdSize; ++k) {
    uint64_t v = whole ^ (0x9E3779B97F4A7C15ull * (k + 1));
    v = hashBytes(reinterpret_cast<const char *>(&v), 8);
    memcpy(p + 8 * k, &v, std::min(8u, ctx.opt.buildIdSize - 8 * k));
  }
}

// Clears the file bytes between sections and around the headers.
void zeroGaps(uint8_t *buf) {
  vector<std::pair<uint64_t, uint64_t>> used;
  used.push_back({0, sizeof(Elf64_Ehdr) + L.phdrs.size() * sizeof(Elf64_Phdr)});
  for (const OutputSection *s : L.ordered)
    if (s->type != SHT_NOBITS && s->size)
      used.push_back({s->offset, s->offset + s->size});
  used.push_back({L.shstrtabOff, L.shstrtabOff + L.shstrtab.size()});
  used.push_back({L.shoff, L.fileSize});
  std::sort(used.begin(), used.end());
  uint64_t cur = 0;
  for (auto [b, e] : used) {
    if (b > cur)
      memset(buf + cur, 0, b - cur);
    cur = std::max(cur, e);
  }
}

void writeOutput() {
  markTime = Clock::now();
  const string &path = ctx.opt.output;
  // Reuse an existing output file's pages; every byte is rewritten.
  int fd = open(path.c_str(), O_RDWR | O_CREAT, 0777);
  if (fd < 0 && errno == ETXTBSY) {
    unlink(path.c_str());
    fd = open(path.c_str(), O_RDWR | O_CREAT, 0777);
  }
  if (fd < 0)
    fatal("cannot create " + path);
  mode_t mask = umask(0);
  umask(mask);
  fchmod(fd, 0777 & ~mask);
  if (ftruncate(fd, L.fileSize) != 0)
    fatal("cannot size " + path);
  uint8_t *buf;
  if (ctx.opt.mmapOutput) {
    buf = static_cast<uint8_t *>(
        mmap(nullptr, L.fileSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (buf == MAP_FAILED)
      fatal("cannot map " + path);
    close(fd);
    prefault(*ctx.pool, buf, L.fileSize, true);
  } else {
    buf = static_cast<uint8_t *>(malloc(L.fileSize));
    if (!buf)
      fatal("out of memory for the output");
  }
  mark("mmap");
  if (L.relaDyn)
    L.relaOut = reinterpret_cast<Elf64_Rela *>(buf + L.relaDyn->offset);

  vector<std::pair<const OutputSection *, std::pair<ObjectFile *, uint32_t>>>
      chunks;
  for (const OutputSection *os : L.ordered)
    for (auto m : os->members)
      chunks.push_back({os, m});
  const size_t nc = chunks.size(), no = L.ehFrame ? ctx.objects.size() : 0;
  const size_t ns = L.symtab ? ctx.objects.size() : 0;
  ctx.pool->forEach(nc + 4 + no + ns, [&](size_t i) {
    if (i == nc + 3 + no + ns)
      zeroGaps(buf);
    else if (i == nc)
      writeSynthetic(buf);
    else if (i == nc + 1)
      writeHeaders(buf);
    else if (i == nc + 2) {
      if (L.symtab)
        writeSymtabEnds(buf);
      memcpy(buf + L.comment->offset, L.commentData.data(),
             L.commentData.size());
    } else if (i >= nc + 3 + no)
      writeSymtabFile(buf, i - nc - 3 - no);
    else if (i > nc + 2)
      writeEhFrame(buf, ctx.objects[i - nc - 3]);
    else
      writeInputSection(buf, chunks[i].first, chunks[i].second.first,
                        chunks[i].second.second);
  }, 16);
  mark("parallel write");
  if (L.ehHdr)
    writeEhFrameHdr(buf);
  writeBuildId(buf);
  markTime = Clock::now();
  if (ctx.opt.mmapOutput) {
    munmap(buf, L.fileSize);
  } else {
    for (size_t off = 0; off < L.fileSize;) {
      ssize_t n = pwrite(fd, buf + off, L.fileSize - off, off);
      if (n <= 0) {
        if (n < 0 && errno == EINTR)
          continue;
        fatal("cannot write " + path);
      }
      off += n;
    }
    close(fd);
  }
  mark("munmap");
}

void checkUndefined() {
  STEP("checkUndefined");
  const bool dynamic = !ctx.opt.dynamicLinker.empty();
  vector<uint32_t> undefined;
  collectNames<1>({&undefined}, [&](uint32_t id, auto &out) {
    Symbol &s = ctx.syms[id];
    if (s.kind != Symbol::Undefined)
      return;
    if (isLinkerDefined(ctx.nameStr[id])) {
      s.kind = Symbol::Linker;
      return;
    }
    const uint16_t f = ctx.flags[id].load(std::memory_order_relaxed);
    // Weak references stay dynamic in a dynamically linked output, and so
    // do all references a shared library leaves to the dynamic loader.
    if (!(f & HiddenRef) && (dynamic || ctx.opt.shared) &&
        (f & (StrongRef | WeakRef)) &&
        (!(f & StrongRef) || ctx.opt.allowUndefined)) {
      s.kind = Symbol::DynUndef;
      atomicOr(ctx.flags[id], uint16_t(NeedsDynsym));
      return;
    }
    if (f & StrongRef)
      out[0].push_back(id);
  });
  if (undefined.empty())
    return;
  // The full backend reports undefined symbols with their references.
  std::sort(undefined.begin(), undefined.end(), [](uint32_t a, uint32_t b) {
    return ctx.nameStr[a] < ctx.nameStr[b];
  });
  string names;
  for (size_t i = 0; i < undefined.size() && i < 3; ++i)
    names += (i ? ", " : "") + string(ctx.nameStr[undefined[i]]);
  fatal(std::to_string(undefined.size()) + " undefined symbols: " + names);
}

void runPipeline() {
  vector<InputSpec> specs;
  for (const fastlink::Input &in : ctx.opt.inputs)
    specs.push_back({in.isLibrary ? findLibrary(ctx.opt.libPaths, in.path)
                                  : in.path,
                     in.wholeArchive, in.asNeeded});
  auto t0 = Clock::now();
  auto t = t0;
  double times[8];
  int ti = 0;
  auto lap = [&] {
    times[ti++] = msSince(t);
    t = Clock::now();
  };
  loadInputs(specs);
  lap();
  resolve();
  checkUndefined();
  applyVersionScript();
  markPreemptible();
  lap();
  selectComdats();
  checkDuplicates();
  markLive();
  lap();
  assignInputSections();
  mergeSections();
  if (ctx.opt.icf)
    icf::run();
  scanRelocations();
  lap();
  layout();
  mark("relocation slots");
  lap();
  writeOutput();
  lap();
  if (ctx.opt.timing)
    fprintf(stderr,
            "load %.1f resolve %.1f gc %.1f scan %.1f layout %.1f write %.1f "
            "total %.1f\n",
            times[0], times[1], times[2], times[3], times[4], times[5],
            msSince(t0));
}

} // namespace

fastlink::Status fastlink::link(const Request &Req, std::string &Reason) {
  ctx = Ctx();
  needsTlsLd = false;
  L = Layout();
  L.all.push_back(new OutputSection); // index 0: not in the output
  relKinds = nullptr;
  ctx.opt = Req;
  activeOptions = &ctx.opt;
  try {
    runPipeline();
  } catch (const fl::Failure &F) {
    Reason = F.message;
    ctx.pool.reset();
    return Status::Declined;
  } catch (const std::bad_alloc &) {
    Reason = "out of memory";
    ctx.pool.reset();
    return Status::Declined;
  }
  // Workers are idle from here on; the process exits soon.
  return Status::Linked;
}

#endif // __linux__
