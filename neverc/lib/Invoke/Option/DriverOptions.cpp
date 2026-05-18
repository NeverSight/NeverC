#include "neverc/Invoke/Options.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"

using namespace neverc::driver;
using namespace neverc::driver::options;
using namespace llvm::opt;

#define OPTTABLE_VALUES_CODE
#include "neverc/Invoke/Options.td.h"
#undef OPTTABLE_VALUES_CODE

#define PREFIX(NAME, VALUE)                                                    \
  static constexpr llvm::StringLiteral NAME##_init[] = VALUE;                  \
  static constexpr llvm::ArrayRef<llvm::StringLiteral> NAME(                   \
      NAME##_init, std::size(NAME##_init) - 1);
#include "neverc/Invoke/Options.td.h"
#undef PREFIX

namespace {
constexpr const llvm::StringLiteral PrefixTable_init[] =
#define PREFIX_UNION(VALUES) VALUES
#include "neverc/Invoke/Options.td.h"
#undef PREFIX_UNION
    ;
constexpr const llvm::ArrayRef<llvm::StringLiteral>
    PrefixTable(PrefixTable_init, std::size(PrefixTable_init) - 1);

constexpr OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "neverc/Invoke/Options.td.h"
#undef OPTION
};
} // namespace

namespace {

class DriverOptTable : public PrecomputedOptTable {
public:
  DriverOptTable() : PrecomputedOptTable(InfoTable, PrefixTable) {}
};
} // namespace

const llvm::opt::OptTable &neverc::driver::getDriverOptTable() {
  static DriverOptTable Table;
  return Table;
}
