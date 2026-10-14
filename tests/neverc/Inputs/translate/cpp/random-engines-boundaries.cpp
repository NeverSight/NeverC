#include <random>
#include <algorithm>
int evaluations;
std::minstd_rand make() { ++evaluations; return std::minstd_rand(17); }
std::minstd_rand &get(std::minstd_rand &engine) { ++evaluations; return engine; }
unsigned seedValue() { ++evaluations; return 0; }
unsigned long long countValue() { ++evaluations; return 3; }
struct Holder { std::minstd_rand engine; Holder() {} };
int main() {
  std::minstd_rand engine(17);
  get(engine).seed(seedValue());
  if(evaluations != 2 || engine()!=48271) return 1;
  get(engine).discard(countValue());
  if(evaluations != 4) return 2;
  std::minstd_rand expected; expected.discard(4);
  if(engine != expected) return 3;
  if(make().min()!=1 || evaluations != 5) return 4;
  if(std::minstd_rand(17)()!=820607) return 5;
  int values[3]{4,7,9},output[3]{};
  std::shuffle(values,values,make());
  if(evaluations != 6) return 6;
  std::sample(values,values+3,output,0,make());
  if(evaluations != 7) return 7;
  engine.seed(2147483647u); if(engine()!=48271) return 8;
  engine.seed(17); std::minstd_rand *pointer=&engine;
  pointer->seed(0); if((*pointer)()!=48271) return 9;
  Holder holder; if(holder.engine()!=48271) return 10;
  std::mt19937 mt(0); mt.discard(624); mt.seed(0);
  if(mt()!=2357136044u) return 11;
  const std::minstd_rand copy(engine);
  if(copy.min()!=1 || copy.max()!=2147483646) return 12;
  engine.operator=(std::minstd_rand(17));
  if(engine.operator()()!=820607) return 13;
  mt.operator=(std::mt19937(0));
  if(mt.operator()()!=2357136044u) return 14;
  return 0;
}
