#ifndef NEVERC_DRIVER_OPTIONS_H
#define NEVERC_DRIVER_OPTIONS_H

#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"

namespace neverc {
namespace driver {

namespace options {
enum NeverCFlags {
  NoXarchOption = (1 << 4),
  LinkerInput = (1 << 5),
  NoArgumentUnused = (1 << 6),
  Unsupported = (1 << 7),
  LinkOption = (1 << 8),
  Ignored = (1 << 9),
  TargetSpecific = (1 << 10),
};

enum NeverCVisibility {
  NeverCOption = llvm::opt::DefaultVis,
};

enum ID {
  OPT_INVALID = 0,
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "neverc/Invoke/Options.td.h"
  LastOption
#undef OPTION
};
} // namespace options

const llvm::opt::OptTable &getDriverOptTable();
} // namespace driver
} // namespace neverc

#endif
