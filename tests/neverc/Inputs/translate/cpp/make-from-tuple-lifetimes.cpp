#include <tuple>
#include <utility>
int argumentLive = 0, argumentDrops = 0;
int defaultLive = 0, defaultDrops = 0, defaultsSeen = 0, calls = 0;
struct Argument {
  int value;
  explicit Argument(int n) noexcept : value(n) { ++argumentLive; }
  Argument(const Argument& other) noexcept : value(other.value) { ++argumentLive; }
  Argument(Argument&& other) noexcept : value(other.value) { ++argumentLive; other.value = -1; }
  ~Argument() { --argumentLive; ++argumentDrops; }
};
struct Default {
  Default() noexcept { ++defaultLive; }
  ~Default() { --defaultLive; ++defaultDrops; }
};
struct ConversionInput {
  int value;
  operator Argument() const noexcept { return Argument(value); }
};
struct Result {
  int value;
  explicit Result(const Argument& arg, const Default& = Default()) noexcept : value(arg.value) {
    defaultsSeen = defaultLive; ++calls;
  }
  Result(const Result&) = delete;
  Result(Result&&) = delete;
};
int observe() { return defaultLive == 0 && defaultDrops == 1 && argumentLive > 0 ? 0 : 1; }
Result create(std::tuple<Argument>& tuple) { return std::make_from_tuple<Result>(tuple); }
int main() {
  int bad = (std::make_from_tuple<Result>(std::tuple<Argument>{Argument(17)}), observe());
  if (bad || defaultLive || defaultDrops != 1 || defaultsSeen != 1 || calls != 1 || argumentLive) return 1;
  {
    std::tuple<Argument> tuple{Argument(23)};
    Result result = create(tuple);
    if (result.value != 23 || defaultLive || defaultDrops != 2 || calls != 2 || argumentLive != 1) return 2;
  }
  if (argumentLive || defaultLive || calls != 2) return 3;
  int conversionBad = (std::make_from_tuple<Result>(std::tuple<ConversionInput>{ConversionInput{29}}),
                       argumentLive != 0 || defaultLive != 0);
  if (conversionBad || defaultDrops != 3 || calls != 3 || defaultsSeen != 1) return 4;
  return 0;
}
