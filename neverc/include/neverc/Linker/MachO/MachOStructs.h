#ifndef LINKER_MACHO_MACHO_STRUCTS_H
#define LINKER_MACHO_MACHO_STRUCTS_H

#include "llvm/Support/Endian.h"

namespace linker::structs {

struct nlist_64 {
  llvm::support::ulittle32_t n_strx;
  uint8_t n_type;
  uint8_t n_sect;
  llvm::support::ulittle16_t n_desc;
  llvm::support::ulittle64_t n_value;
};

struct nlist {
  llvm::support::ulittle32_t n_strx;
  uint8_t n_type;
  uint8_t n_sect;
  llvm::support::ulittle16_t n_desc;
  llvm::support::ulittle32_t n_value;
};

struct entry_point_command {
  llvm::support::ulittle32_t cmd;
  llvm::support::ulittle32_t cmdsize;
  llvm::support::ulittle64_t entryoff;
  llvm::support::ulittle64_t stacksize;
};

} // namespace linker::structs

#endif
