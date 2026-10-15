#include <random>
struct G{using result_type=unsigned;unsigned state,calls;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
struct N{using result_type=unsigned short;unsigned state,calls;static constexpr unsigned short min(){return 13;}static constexpr unsigned short max(){return 37;}unsigned short operator()(){++calls;state=(state*13+7)%25;return static_cast<unsigned short>(13+state);}};
template<class E> E make(){return E(17);}template<> G make<G>(){return G{17,0};}template<> N make<N>(){return N{17,0};}
template<class R> unsigned long long bits(R value){const auto *p=static_cast<const unsigned char*>(static_cast<const void*>(&value));unsigned long long result=0;for(unsigned i=0;i<sizeof(R);++i)result|=static_cast<unsigned long long>(p[i])<<(8*i);return result;}
template<class E> int check(double probability,unsigned long long expected,unsigned count,unsigned long long next){
 E e=make<E>();using D=std::bernoulli_distribution;using P=D::param_type;D d(probability);
 if(d.p()!=probability||d.min()!=false||d.max()!=true)return 1;
 unsigned long long hash=0;unsigned actual=0;for(unsigned i=0;i<10003;++i){bool value=d(e);hash=hash*257+value;actual+=value;}
 if(hash!=expected||actual!=count||static_cast<unsigned long long>(e())!=next)return 2;
 P p(probability);D from(p),copied(d);if(from!=d||copied!=d||d.param()!=p)return 3;
 D assigned;assigned=d;assigned.reset();if(assigned!=d)return 4;
 P always(1.);E clone=e;assigned.param(always);
 if(!d(e,always)||!assigned.operator()(clone,always)||e()!=clone()||d.param()!=p)return 5;
 D defaults;P defaultParams;if(defaults.p()!=0.5||defaultParams.p()!=0.5||defaults.param()!=defaultParams)return 6;
 P copiedParams(p),moveTarget;moveTarget=copiedParams;if(moveTarget!=p)return 7;
 assigned.param(p);if(assigned!=d)return 8;
 return 0;
}
int main(){
 if(int result=check<std::mt19937>(0.,0ULL,0u,2276022928ULL))return 0+result;
 if(int result=check<std::mt19937>(1.,9252161054641443347ULL,10003u,2276022928ULL))return 16+result;
 if(int result=check<std::mt19937>(0.5,3105946745952607147ULL,5035u,2276022928ULL))return 32+result;
 if(int result=check<std::mt19937_64>(0.123456789,15621003609418893775ULL,1231u,16283156750775118513ULL))return 48+result;
 if(int result=check<std::minstd_rand>(0.7,1591482081444628278ULL,6966u,764354073ULL))return 64+result;
 if(int result=check<std::ranlux48>(0.3,6237178607304621527ULL,3031u,200217642443660ULL))return 80+result;
 if(int result=check<std::knuth_b>(0.9,14523020706090954443ULL,8907u,1729487110ULL))return 96+result;
 if(int result=check<G>(0.37,400042517802361721ULL,3705u,2013693826ULL))return 112+result;
 if(int result=check<N>(0.37,17070953014364521122ULL,4002u,18ULL))return 128+result;
 return 0;
}
