#ifndef NEVERC_BUILD_FUNCTION_H
#define NEVERC_BUILD_FUNCTION_H

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace neverc {
namespace build {

class VariableEnv;

class FunctionRegistry {
public:
  using FuncImpl = std::function<std::string(
      const std::vector<std::string> &, VariableEnv &)>;

  FunctionRegistry();

  void registerBuiltins();
  void registerFunction(const std::string &Name, FuncImpl Impl);

  std::string call(const std::string &Name,
                   const std::vector<std::string> &Args,
                   VariableEnv &Env) const;

  bool hasFunction(const std::string &Name) const;

  static std::vector<std::string> splitArgs(const std::string &ArgStr,
                                             unsigned MaxArgs = 0);

private:
  std::unordered_map<std::string, FuncImpl> Registry;
};

} // namespace build
} // namespace neverc

#endif // NEVERC_BUILD_FUNCTION_H
