#include <algorithm>

int first_calls, last_calls, output_calls, count_calls, generator_calls, factories;
int *first(int *p) { ++first_calls; return p; }
int *last(int *p) { ++last_calls; return p; }
int *output(int *p) { ++output_calls; return p; }
short count(short n) { ++count_calls; return n; }
struct G {
  using result_type = unsigned;
  unsigned state, calls;
  static constexpr unsigned min() { return 0; }
  static constexpr unsigned max() { return ~0u; }
  unsigned operator()() & {
    ++calls;
    state = state * 1664525u + 1013904223u;
    return state;
  }
  unsigned operator()() && { return 0; }
};
G &generator(G &g) { ++generator_calls; return g; }
G make() { ++factories; return G{17, 0}; }
struct Noncopyable {
  using result_type = unsigned;
  unsigned state, calls;
  Noncopyable(const Noncopyable &) = delete;
  static constexpr unsigned min() { return 0; }
  static constexpr unsigned max() { return ~0u; }
  unsigned operator()() { ++calls; return ++state; }
};

int main() {
  int values[3]{4, 7, 9}, sampled[3]{-1, -1, -1};
  G g{17, 0};
  std::shuffle(first(values), last(values), generator(g));
  std::shuffle(first(values), last(values + 1), generator(g));
  if (g.state != 17 || g.calls || values[0] != 4 ||
      first_calls != 2 || last_calls != 2 || generator_calls != 2) return 1;
  auto empty = std::sample(first(values), last(values + 3), output(sampled),
                           count(0), generator(g));
  if (empty != sampled || g.calls || sampled[0] != -1 ||
      first_calls != 3 || last_calls != 3 || output_calls != 1 ||
      count_calls != 1 || generator_calls != 3) return 2;
  auto no_input = std::sample(values, values, sampled, ~0ULL, g);
  if (no_input != sampled || g.calls || sampled[0] != -1) return 3;
  auto all = std::sample(values, values + 3, sampled, ~0ULL, g);
  if (all != sampled + 3 || sampled[0] != 4 || sampled[1] != 7 ||
      sampled[2] != 9 || g.state != 3524153451u || g.calls != 2) return 4;
  std::shuffle(values, values, make());
  if (factories != 1) return 5;
  auto one = std::sample(values, values + 1, sampled, 1, make());
  if (factories != 2 || one != sampled + 1 || sampled[0] != 4) return 6;
  Noncopyable nc{0, 0};
  std::shuffle(values, values + 2, nc);
  if (nc.state != 1 || nc.calls != 1 || values[0] != 7 || values[1] != 4)
    return 7;
  return 0;
}
