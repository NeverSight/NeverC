#include <random>
struct Steps{using result_type=unsigned long long;unsigned calls=0;static constexpr result_type min(){return 0;}static constexpr result_type max(){return ~0ULL;}result_type operator()(){const result_type values[]={0,0x8000000000000000ULL,0};return values[calls++%3];}};
struct Quarter{using result_type=unsigned long long;unsigned calls=0;static constexpr result_type min(){return 0;}static constexpr result_type max(){return ~0ULL;}result_type operator()(){++calls;return 0x4000000000000000ULL;}};
struct Mutates{using result_type=unsigned long long;unsigned calls=0;std::negative_binomial_distribution<int>::param_type*p;static constexpr result_type min(){return 0;}static constexpr result_type max(){return ~0ULL;}result_type operator()(){++calls;*p=std::negative_binomial_distribution<int>::param_type(20,.01);return 0x4000000000000000ULL;}};
struct MutatesGeometric{using result_type=unsigned long long;unsigned calls=0;std::geometric_distribution<int>::param_type*p;static constexpr result_type min(){return 0;}static constexpr result_type max(){return ~0ULL;}result_type operator()(){++calls;*p=std::geometric_distribution<int>::param_type(.01);return 0x4000000000000000ULL;}};
unsigned effects;int count(){++effects;return 2;}double probability(){++effects;return .5;}
int main(){using D=std::negative_binomial_distribution<int>;using P=D::param_type;D constructed(count(),probability());if(effects!=2||constructed.k()!=2||constructed.p()!=.5)return 1;D first(2);P firstP(2);if(first.p()!=.5||firstP.p()!=.5)return 2;
D d(2,.5);Steps sequence;if(d(sequence)!=1||sequence.calls!=3)return 3;
D unit(2,1.);Quarter q;if(unit(q)!=0||q.calls!=2)return 4;D zero(0,.5);if(zero(q)!=0||q.calls!=2)return 5;
D boundary(21,1.);Quarter b;if(boundary(b)!=0||b.calls!=21)return 6;D mixture(22,1.);Quarter m;if(mixture(m)!=0||m.calls!=3)return 7;
std::geometric_distribution<int>geometric(.5);Steps one;if(geometric(one)!=0||one.calls!=1)return 8;std::geometric_distribution<int>certain(1.);Quarter certainG;if(certain(certainG)!=0||certainG.calls!=1)return 9;
P changing(2,.5);Mutates mutator{0,&changing};if(d(mutator,changing)!=0||mutator.calls!=2||changing.k()!=20||changing.p()!=.01||d.k()!=2||d.p()!=.5)return 10;
std::geometric_distribution<int>::param_type gp(.5);MutatesGeometric gm{0,&gp};if(geometric(gm,gp)!=0||gm.calls!=1||gp.p()!=.01||geometric.p()!=.5)return 11;return 0;}
