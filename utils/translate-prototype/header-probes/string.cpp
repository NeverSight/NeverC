// Header discovery only: no string translation or ownership support is implied.
#include <string>

static_assert(_LIBCPP_VERSION == 200100, "probe requires pinned libc++ headers");

std::string probe_string(const char *bytes, std::size_t count) {
  std::string result(bytes, count);
  result.push_back('x');
  return result;
}
