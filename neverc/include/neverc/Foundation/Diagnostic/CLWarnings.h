#ifndef NEVERC_FOUNDATION_CLWARNINGS_H
#define NEVERC_FOUNDATION_CLWARNINGS_H

#include <optional>

namespace neverc {

namespace diag {
enum class Group;
}

std::optional<diag::Group> diagGroupFromCLWarningID(unsigned);

} // end namespace neverc

#endif // NEVERC_FOUNDATION_CLWARNINGS_H
