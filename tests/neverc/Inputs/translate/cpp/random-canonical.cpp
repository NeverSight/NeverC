#include <random>
struct G{using result_type=unsigned;unsigned state,calls;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
struct N{using result_type=unsigned short;unsigned state,calls;static constexpr unsigned short min(){return 13;}static constexpr unsigned short max(){return 37;}unsigned short operator()(){++calls;state=(state*13+7)%25;return static_cast<unsigned short>(13+state);}};
template<class E> E make(){return E(17);}template<> G make<G>(){return G{17,0};}template<> N make<N>(){return N{17,0};}
template<class R> unsigned long long bits(R value){const auto *p=static_cast<const unsigned char*>(static_cast<const void*>(&value));unsigned long long result=0;for(unsigned i=0;i<sizeof(R);++i)result|=static_cast<unsigned long long>(p[i])<<(8*i);return result;}
template<class E,class R,unsigned B> int canonical(unsigned long long expected,unsigned long long next){E e=make<E>();unsigned long long digest=0;for(unsigned i=0;i<10003;++i)digest=digest*257+bits(std::generate_canonical<R,B>(e));return digest==expected&&static_cast<unsigned long long>(e())==next?0:1;}
int main(){
 if(int result=canonical<std::mt19937,float,0>(3441993831697317595ULL,1619580843ULL))return 0+result;
 if(int result=canonical<std::mt19937,double,0>(13571920500682129408ULL,1619580843ULL))return 16+result;
 if(int result=canonical<std::minstd_rand,float,12>(1575490615084748343ULL,1931632209ULL))return 32+result;
 if(int result=canonical<std::mt19937_64,float,1000>(9178717641559672545ULL,16283156750775118513ULL))return 48+result;
 if(int result=canonical<std::mt19937,double,53>(13458063077320042130ULL,2276022928ULL))return 64+result;
 if(int result=canonical<std::ranlux48,double,1000>(12802588576361624742ULL,200217642443660ULL))return 80+result;
 if(int result=canonical<N,double,53>(13824857535027729209ULL,18ULL))return 96+result;
 return 0;
}
