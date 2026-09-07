// Header discovery only: no vector translation or lifetime support is implied.
#include <vector>

static_assert(_LIBCPP_VERSION == 200100, "probe requires pinned libc++ headers");

std::vector<int> probe_vector() {
  std::vector<int> result{1, 2};
  result.push_back(3);
  return result;
}
