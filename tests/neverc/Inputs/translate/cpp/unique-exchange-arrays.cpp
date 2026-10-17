#include <memory>
#include <utility>
int main() {
  auto values = std::make_unique<int[]>(4);
  auto next = std::make_unique<int[]>(7);
  values[0] = 5; values[3] = 11; next[6] = 19;
  auto old = std::exchange(values, std::move(next));
  if (next || old[0] != 5 || old[3] != 11 || values[6] != 19) return 1;
  auto self = std::exchange(values, std::move(values));
  if (values || self[6] != 19) return 2;
  auto empty = std::exchange(values, std::move(old));
  if (empty || old || values[3] != 11) return 3;
  auto cleared = std::exchange(values, nullptr);
  if (values || cleared[0] != 5 || cleared[3] != 11) return 4;
  return 0;
}
