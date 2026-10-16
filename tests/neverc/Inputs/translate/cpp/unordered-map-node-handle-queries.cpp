#include <unordered_map>
using Map = std::unordered_map<int, int>;
using Node = Map::node_type;
using Result = Map::insert_return_type;
static_assert(sizeof(Node) == 2 * sizeof(void *));
static_assert(alignof(Node) == alignof(void *));
static_assert(sizeof(Result) == 4 * sizeof(void *));
int observe_node(const Node &node) {
  return node.empty() ? 0 : node.key() + node.mapped();
}
void modify_node(const Node &node) {
  node.key() = 5;
  node.mapped() = 7;
}
int observe_result(const Result &result) {
  return result.inserted ? result.position->second : int(result.node.empty());
}
