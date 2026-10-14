#include <random>
struct G{using result_type=unsigned;unsigned state,calls;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
struct N{using result_type=unsigned short;unsigned state,calls;static constexpr unsigned short min(){return 13;}static constexpr unsigned short max(){return 37;}unsigned short operator()(){++calls;state=(state*13+7)%25;return static_cast<unsigned short>(13+state);}};
template<class E> E make(){return E(17);}template<> G make<G>(){return G{17,0};}template<> N make<N>(){return N{17,0};}
template<class R> unsigned long long bits(R value){const auto *p=static_cast<const unsigned char*>(static_cast<const void*>(&value));unsigned long long result=0;for(unsigned i=0;i<sizeof(R);++i)result|=static_cast<unsigned long long>(p[i])<<(8*i);return result;}
template<class E,class R> int real(R a,R b,unsigned long long expected,unsigned long long next){
 E e=make<E>();using D=std::uniform_real_distribution<R>;using P=typename D::param_type;D d(a,b);
 if(d.a()!=a||d.b()!=b||d.min()!=a||d.max()!=b)return 1;
 unsigned long long digest=0;for(unsigned i=0;i<10003;++i)digest=digest*257+bits(d(e));
 if(digest!=expected||static_cast<unsigned long long>(e())!=next)return 2;
 P p(a,b);D from(p),copied(d);if(from!=d||copied!=d||d.param()!=p)return 3;
 D assigned;assigned=d;assigned.reset();if(assigned!=d)return 4;
 P single(b,b);assigned.param(single);E saved=e;
 if(d(e,single)!=b||d.param()!=p)return 5;
 if(assigned.operator()(saved,single)!=b||saved()!=e())return 6;
 D defaults;P defaultParams;if(defaults.param()!=defaultParams||defaults.a()!=0||defaults.b()!=1)return 7;
 D lowerOnly(R(0));P lowerParam(R(0));if(lowerOnly.param()!=lowerParam)return 8;
 return 0;
}
template<class E,class R,unsigned B> int canonical(unsigned long long expected,unsigned long long next){E e=make<E>();unsigned long long digest=0;for(unsigned i=0;i<10003;++i)digest=digest*257+bits(std::generate_canonical<R,B>(e));return digest==expected&&static_cast<unsigned long long>(e())==next?0:1;}
int main(){
 if(int result=real<std::mt19937,float>(0.f,1.f,3441993831697317595ULL,1619580843ULL))return 0+result;
 if(int result=real<std::mt19937,double>(0.,1.,13458063077320042130ULL,2276022928ULL))return 16+result;
 if(int result=real<std::mt19937_64,double>(-4.,16.,569693248250327443ULL,16283156750775118513ULL))return 32+result;
 if(int result=real<std::ranlux24_base,float>(2.f,10.f,14646276807910074743ULL,10370261ULL))return 48+result;
 if(int result=real<std::knuth_b,double>(-1.,1.,2479582915646053616ULL,1729487110ULL))return 64+result;
 if(int result=real<G,float>(0.f,1.f,16862645768230927621ULL,1860345957ULL))return 80+result;
 if(int result=real<G,double>(-4.,16.,13026747347162718847ULL,2013693826ULL))return 96+result;
 if(int result=real<N,double>(0.,1.,13824857535027729209ULL,18ULL))return 112+result;
 if(int result=real<std::mt19937,double>(5.25,5.25,8903334988334759936ULL,2276022928ULL))return 128+result;
 return 0;
}
