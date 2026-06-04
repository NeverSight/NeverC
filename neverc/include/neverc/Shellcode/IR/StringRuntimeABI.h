#ifndef NEVERC_SHELLCODE_STRINGRUNTIMEABI_H
#define NEVERC_SHELLCODE_STRINGRUNTIMEABI_H

#include "neverc/Foundation/Builtin/BuiltinStringNames.h"
#include "neverc/Shellcode/Pipeline/SymbolNames.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace neverc {
namespace BuiltinString {
bool isRuntimeFunctionName(llvm::StringRef Name);
}
}

namespace neverc {
namespace shellcode {
namespace StringRuntimeABI {

inline constexpr uint64_t ArenaAlignment = 16;
inline constexpr uint64_t ArenaSizeCeiling = (uint64_t)1 << 30;

#ifndef NEVERC_STRING_USER_ARENA_SIZE
#define NEVERC_STRING_USER_ARENA_SIZE (64 * 1024)
#endif
#ifndef NEVERC_STRING_KERNEL_ARENA_SIZE
#define NEVERC_STRING_KERNEL_ARENA_SIZE (4 * 1024)
#endif

inline constexpr uint64_t UserArenaSize = NEVERC_STRING_USER_ARENA_SIZE;
inline constexpr uint64_t KernelArenaSize = NEVERC_STRING_KERNEL_ARENA_SIZE;

static_assert(UserArenaSize > 0,
              "NEVERC_STRING_USER_ARENA_SIZE must be > 0 -- a zero-byte "
              "arena would force every owned-string allocation to the "
              "empty sentinel.");
static_assert(KernelArenaSize > 0,
              "NEVERC_STRING_KERNEL_ARENA_SIZE must be > 0 -- a zero-byte "
              "arena would force every owned-string allocation to the "
              "empty sentinel.");
static_assert(UserArenaSize % ArenaAlignment == 0,
              "NEVERC_STRING_USER_ARENA_SIZE must be a multiple of "
              "ArenaAlignment so the OOM bound check rejects allocations "
              "consistently across rounding-induced fragments.");
static_assert(KernelArenaSize % ArenaAlignment == 0,
              "NEVERC_STRING_KERNEL_ARENA_SIZE must be a multiple of "
              "ArenaAlignment so the OOM bound check rejects allocations "
              "consistently across rounding-induced fragments.");
static_assert(UserArenaSize <= ArenaSizeCeiling,
              "NEVERC_STRING_USER_ARENA_SIZE must stay <= 1 GB so the "
              "stackified arena fits a realistic stack budget and the "
              "alloc helper's `Pos + N` arithmetic stays far from wrap.");
static_assert(KernelArenaSize <= ArenaSizeCeiling,
              "NEVERC_STRING_KERNEL_ARENA_SIZE must stay <= 1 GB so the "
              "stackified arena fits a realistic stack budget and the "
              "alloc helper's `Pos + N` arithmetic stays far from wrap.");

inline constexpr llvm::StringLiteral ArenaGlobalName = "__sc_string_arena";
inline constexpr llvm::StringLiteral ArenaOffsetGlobalName =
    "__sc_string_arena_pos";
inline constexpr llvm::StringLiteral ArenaFreeListGlobalName =
    "__sc_string_arena_free";
inline constexpr llvm::StringLiteral AllocFunctionName = "__sc_string_alloc";
inline constexpr llvm::StringLiteral FreeFunctionName = "__sc_string_free";

inline constexpr uint64_t ArenaMetadataAlignment = 8;

enum ArenaHeaderField : unsigned {
  HeaderSizeField = 0,
  HeaderNextField = 1,
  HeaderSelfField = 2,
  HeaderTagField = 3,
  HeaderFieldCount,
};

inline constexpr uint64_t ArenaBlockActiveTag = 0x5343525354524141ULL;
inline constexpr uint64_t ArenaBlockFreeTag = 0x5343525354524646ULL;

enum class AllocatorRole : unsigned {
  Alloc = 0,
  Free = 1,
};

namespace BasicBlockNames {

inline constexpr llvm::StringLiteral AllocEntry = "entry";
inline constexpr llvm::StringLiteral AllocReuseCheck = "reuse.check";
inline constexpr llvm::StringLiteral AllocReuseInspect = "reuse.inspect";
inline constexpr llvm::StringLiteral AllocReuseNext = "reuse.next";
inline constexpr llvm::StringLiteral AllocReuseUnlink = "reuse.unlink";
inline constexpr llvm::StringLiteral AllocReuseUpdateHead = "reuse.update.head";
inline constexpr llvm::StringLiteral AllocReuseUpdatePrev = "reuse.update.prev";
inline constexpr llvm::StringLiteral AllocReuseReturn = "reuse.return";
inline constexpr llvm::StringLiteral AllocBump = "bump";
inline constexpr llvm::StringLiteral AllocBumpCommit = "bump.commit";
inline constexpr llvm::StringLiteral AllocOOM = "oom";

inline constexpr llvm::StringLiteral FreeEntry = "entry";
inline constexpr llvm::StringLiteral FreeValidate = "validate";
inline constexpr llvm::StringLiteral FreeValidateHeader = "validate.header";
inline constexpr llvm::StringLiteral FreeRelease = "release";
inline constexpr llvm::StringLiteral FreeDone = "done";

}

namespace IRNames {

inline constexpr llvm::StringLiteral AlignmentRoundUpHeaderStart =
    "bump.aligned";
inline constexpr llvm::StringLiteral AlignmentRoundUpPayloadEnd =
    "bump.end.aligned";

inline constexpr llvm::StringLiteral AllocSizeArg = "n";
inline constexpr llvm::StringLiteral AllocSizeNonZero = "n.nonzero";
inline constexpr llvm::StringLiteral AllocFreeHead = "free.head";
inline constexpr llvm::StringLiteral AllocFreeCur = "free.cur";
inline constexpr llvm::StringLiteral AllocFreePrev = "free.prev";
inline constexpr llvm::StringLiteral AllocFreeCurSelfPtr = "free.cur.self.ptr";
inline constexpr llvm::StringLiteral AllocFreeCurSelf = "free.cur.self";
inline constexpr llvm::StringLiteral AllocFreeCurTagPtr = "free.cur.tag.ptr";
inline constexpr llvm::StringLiteral AllocFreeCurTag = "free.cur.tag";
inline constexpr llvm::StringLiteral AllocFreeCurSizePtr = "free.cur.size.ptr";
inline constexpr llvm::StringLiteral AllocFreeCurSize = "free.cur.size";
inline constexpr llvm::StringLiteral AllocFreeCurNextPtr = "free.cur.next.ptr";
inline constexpr llvm::StringLiteral AllocFreeNext = "free.next";
inline constexpr llvm::StringLiteral AllocReuseNextPtr = "reuse.next.ptr";
inline constexpr llvm::StringLiteral AllocReuseNextValue = "reuse.next";
inline constexpr llvm::StringLiteral AllocFreePrevNextPtr = "free.prev.next";
inline constexpr llvm::StringLiteral AllocReuseNextClearPtr =
    "reuse.next.clear";
inline constexpr llvm::StringLiteral AllocReuseTagPtr = "reuse.tag.ptr";
inline constexpr llvm::StringLiteral AllocReusePayload = "reuse.payload";
inline constexpr llvm::StringLiteral AllocBumpPos = "pos";
inline constexpr llvm::StringLiteral AllocBumpPayloadStart = "payload.start";
inline constexpr llvm::StringLiteral AllocBumpEndRaw = "end.raw";
inline constexpr llvm::StringLiteral AllocBlock = "block";
inline constexpr llvm::StringLiteral AllocBlockSizePtr = "block.size.ptr";
inline constexpr llvm::StringLiteral AllocPayloadCapacity = "payload.capacity";
inline constexpr llvm::StringLiteral AllocBlockNextPtr = "block.next.ptr";
inline constexpr llvm::StringLiteral AllocBlockSelfPtr = "block.self.ptr";
inline constexpr llvm::StringLiteral AllocBlockTagPtr = "block.tag.ptr";
inline constexpr llvm::StringLiteral AllocPayloadPtr = "ptr";

inline constexpr llvm::StringLiteral FreePtrArg = "ptr";
inline constexpr llvm::StringLiteral FreeArenaBegin = "arena.begin";
inline constexpr llvm::StringLiteral FreeArenaEnd = "arena.end";
inline constexpr llvm::StringLiteral FreePtrInt = "ptr.int";
inline constexpr llvm::StringLiteral FreeArenaBeginInt = "arena.begin.int";
inline constexpr llvm::StringLiteral FreeArenaEndInt = "arena.end.int";
inline constexpr llvm::StringLiteral FreeArenaFirstPayload =
    "arena.first.payload";
inline constexpr llvm::StringLiteral FreeBlock = "block";
inline constexpr llvm::StringLiteral FreeBlockSelfPtr = "block.self.ptr";
inline constexpr llvm::StringLiteral FreeBlockSelf = "block.self";
inline constexpr llvm::StringLiteral FreeBlockTagPtr = "block.tag.ptr";
inline constexpr llvm::StringLiteral FreeBlockTag = "block.tag";
inline constexpr llvm::StringLiteral FreeOldHead = "free.head";
inline constexpr llvm::StringLiteral FreeBlockNextPtr = "block.next.ptr";

}

inline llvm::StringRef canonicalSymbolName(llvm::StringRef Name) {
  return SymbolNames::canonicalRuntimeName(Name);
}

inline bool isRuntimeSymbolName(llvm::StringRef Name) {
  return BuiltinString::isRuntimeFunctionName(canonicalSymbolName(Name));
}

inline constexpr llvm::StringLiteral kRuntimeFnAttr =
    BuiltinStringNames::RuntimeFnAttr;

}
}
}

#endif
