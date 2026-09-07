// Header discovery only: this is not an accepted translation fixture.
#include <cmath>

static_assert(_LIBCPP_VERSION == 200100, "probe requires pinned libc++ headers");

double probe_cmath(double value) {
  return std::floor(std::fabs(value));
}
