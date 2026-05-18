#ifndef NEVERC_BASIC_CLWARNINGS_H
#define NEVERC_BASIC_CLWARNINGS_H

#include <optional>

namespace neverc {

namespace diag {
enum class Group;
}

std::optional<diag::Group> diagGroupFromCLWarningID(unsigned);

} // end namespace neverc

#endif // NEVERC_BASIC_CLWARNINGS_H
