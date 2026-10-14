#include <random>
struct G{using result_type=unsigned;unsigned state,calls;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
struct N{using result_type=unsigned short;unsigned state,calls;static constexpr unsigned short min(){return 13;}static constexpr unsigned short max(){return 37;}unsigned short operator()(){++calls;state=(state*13+7)%25;return static_cast<unsigned short>(13+state);}};
template<class R> int check(unsigned long long expected,unsigned state,unsigned calls){
 R g{17,0};std::uniform_int_distribution<int> d(-1000,1000);unsigned long long hash=0;
 for(unsigned i=0;i<10003;++i)hash=hash*257+static_cast<unsigned long long>(d(g));
 if(hash!=expected||g.state!=state||g.calls!=calls)return 1;
 auto previous=g;std::uniform_int_distribution<int>::param_type one(-7,-7);
 if(d(g,one)!=-7||g.state!=previous.state||g.calls!=previous.calls)return 2;
 return 0;
}
int main(){
 if(int r=check<G>(14261825768442615493ULL,3954158663u,10238u))return 0+r;
 if(int r=check<N>(9979552179434332356ULL,10u,40015u))return 16+r;
 return 0;
}
