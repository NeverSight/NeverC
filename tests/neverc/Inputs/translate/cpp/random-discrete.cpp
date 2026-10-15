using Size=decltype(sizeof(0));
struct Arena {unsigned long long alignment;unsigned char bytes[65536];};
Arena arena{};Size offset;unsigned allocations,frees;
void* operator new(Size n){++allocations;auto p=arena.bytes+offset;offset+=(n+15)/16*16;return p;}
void operator delete(void*)noexcept{++frees;}
#include <random>
struct G{using result_type=unsigned;unsigned state,calls;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
struct N{using result_type=unsigned short;unsigned state,calls;static constexpr unsigned short min(){return 13;}static constexpr unsigned short max(){return 37;}unsigned short operator()(){++calls;state=(state*13+7)%25;return static_cast<unsigned short>(13+state);}};
template<class E> E make(){return E(17);}template<> G make<G>(){return G{17,0};}template<> N make<N>(){return N{17,0};}
template<class R> unsigned long long bits(R value){const auto *p=static_cast<const unsigned char*>(static_cast<const void*>(&value));unsigned long long result=0;for(unsigned i=0;i<sizeof(R);++i)result|=static_cast<unsigned long long>(p[i])<<(8*i);return result;}
template<class E,class I>int check(const double* w,unsigned n,unsigned long long ph,unsigned long long h,unsigned long long next){
 E e=make<E>();using D=std::discrete_distribution<I>;D d(w,w+n);
 if(d.min()!=0 || d.max()!=static_cast<I>(n>1?n-1:0))return 1;
 auto p=d.probabilities();if(p.size()!=(n>1?n:1))return 2;
 unsigned long long actual=0;for(double v:p)actual=actual*257+bits(v);if(actual!=ph)return 3;
 actual=0;for(unsigned i=0;i<10003;++i)actual=actual*257+static_cast<unsigned long long>(d(e));
 if(actual!=h || static_cast<unsigned long long>(e())!=next)return 4;
 return 0;
}
int api(){
 using D=std::discrete_distribution<int>;using P=D::param_type;
 D empty;P emptyParams;if(empty.max()!=0||empty.param()!=emptyParams)return 1;
 auto singleton=empty.probabilities();if(singleton.size()!=1||singleton[0]!=1.)return 2;
 P p{1.,0.,2.,4.};D d(p),copy(d);if(copy!=d||d.param()!=p)return 3;
 D moved(std::move(copy));if(moved!=d||copy.max()!=0)return 4;
 D assigned{1.,2.};assigned=d;if(assigned!=d)return 5;
 assigned=assigned;if(assigned!=d)return 6;
 assigned.param(assigned.param());if(assigned!=d)return 7;
 P copied(p);P movedParam(std::move(copied));if(movedParam!=p||copied!=emptyParams)return 8;
 P large{1.,2.,3.,4.,5.,6.,7.,8.};copied=large;copied=p;if(copied!=p)return 9;
 copied=std::move(movedParam);if(copied!=p||movedParam!=emptyParams)return 10;
 assigned=std::move(moved);if(assigned!=d||moved.max()!=0)return 11;
 std::mt19937 e(17),clone(e);for(unsigned i=0;i<33;++i)if(d(e,p)!=assigned.operator()(clone,p))return 12;
 if(e()!=clone())return 13;
 std::initializer_list<double> list{3.,1.,0.,2.};D fromList(list);P fromListParams(list);
 if(fromList.param()!=fromListParams)return 14;
 std::vector<double> weights{3.,1.,0.,2.};const auto& constWeights=weights;
 D wrapped(weights.begin(),weights.end());P wrappedConst(constWeights.begin(),constWeights.end());
 if(wrapped!=fromList||wrappedConst!=fromListParams)return 15;
 const auto& constant=wrapped;auto probabilities=constant.probabilities();if(probabilities.size()!=4)return 16;
 auto original=d;d.param(emptyParams);d.reset();if(d.max()!=0||d==original)return 17;
 d=original;if(d!=original)return 18;
 P equalA{1.,2.},equalB{2.,4.};if(equalA!=equalB)return 19;
 return 0;
}
int main(){
double weights[64]{};for(unsigned i=0;i<64;++i)weights[i]=(i%7==0)?0.:static_cast<double>((i*7)%19+1);
 offset=0;allocations=0;frees=0;if(int result=check<std::mt19937,int>(weights,0,4607182418800017408ULL,0ULL,2276022928ULL))return 0+result;
 if(allocations!=frees)return 0+5;
 offset=0;allocations=0;frees=0;if(int result=check<std::mt19937,int>(weights,1,4607182418800017408ULL,0ULL,2276022928ULL))return 8+result;
 if(allocations!=frees)return 8+5;
 offset=0;allocations=0;frees=0;if(int result=check<std::mt19937,int>(weights,2,4607182418800017408ULL,9252161054641443347ULL,2276022928ULL))return 16+result;
 if(allocations!=frees)return 16+5;
 offset=0;allocations=0;frees=0;if(int result=check<std::mt19937_64,long long>(weights,3,6196365661223449778ULL,491612887218272374ULL,16283156750775118513ULL))return 24+result;
 if(allocations!=frees)return 24+5;
 offset=0;allocations=0;frees=0;if(int result=check<std::minstd_rand,unsigned>(weights,6,10207960882800611415ULL,964731565859986686ULL,764354073ULL))return 32+result;
 if(allocations!=frees)return 32+5;
 offset=0;allocations=0;frees=0;if(int result=check<std::ranlux48,short>(weights,17,12154525072805394983ULL,5114545773069362257ULL,200217642443660ULL))return 40+result;
 if(allocations!=frees)return 40+5;
 offset=0;allocations=0;frees=0;if(int result=check<std::knuth_b,unsigned short>(weights,64,1483739306111270003ULL,16417574077882049798ULL,1729487110ULL))return 48+result;
 if(allocations!=frees)return 48+5;
 offset=0;allocations=0;frees=0;if(int result=check<G,int>(weights,17,12154525072805394983ULL,18443758949849471473ULL,2013693826ULL))return 56+result;
 if(allocations!=frees)return 56+5;
 offset=0;allocations=0;frees=0;if(int result=check<N,unsigned long long>(weights,64,1483739306111270003ULL,5333954510917858931ULL,18ULL))return 64+result;
 if(allocations!=frees)return 64+5;
 offset=0;allocations=0;frees=0;if(int result=check<std::mt19937,signed char>(weights,17,12154525072805394983ULL,5889745593186552294ULL,2276022928ULL))return 72+result;
 if(allocations!=frees)return 72+5;
 offset=0;allocations=0;frees=0;if(int result=check<std::mt19937,unsigned char>(weights,17,12154525072805394983ULL,5889745593186552294ULL,2276022928ULL))return 80+result;
 if(allocations!=frees)return 80+5;
offset=0;allocations=0;frees=0;if(int result=api())return 128+result;
if(allocations!=frees||allocations!=30u||offset!=944)return 150;
return 0;}
