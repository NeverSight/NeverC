using Size=decltype(sizeof(0));
struct Arena {unsigned long long alignment;unsigned char bytes[1024];};
Arena arena{};Size offset;unsigned allocations,frees;
void* operator new(Size n){++allocations;auto p=arena.bytes+offset;offset+=(n+15)/16*16;return p;}
void operator delete(void*)noexcept{++frees;}
#include <random>
struct G{using result_type=unsigned;unsigned state,calls;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R> unsigned long long bits(R value){const auto *p=static_cast<const unsigned char*>(static_cast<const void*>(&value));unsigned long long result=0;for(unsigned i=0;i<sizeof(R);++i)result|=static_cast<unsigned long long>(p[i])<<(8*i);return result;}
template<class R>int api(unsigned long long expectedB,unsigned long long expectedD,unsigned long long expectedH,unsigned long long expectedNext,unsigned expectedCount,Size expectedBytes){using D=std::piecewise_linear_distribution<R>;using P=typename D::param_type;offset=0;allocations=frees=0;unsigned flags=0;R b[]={R(-3),R(-1),R(2),R(5)};R w[]={R(1),R(0),R(4),R(2)};unsigned long long bh=0,dh=0,h=0,next;
 {P p(b,b+4,w);P q(p);P moved(static_cast<P&&>(p));if(p==q&&moved==q)flags|=1;D d(p);D copied(d);D movedD(static_cast<D&&>(d));if(d==copied&&movedD==copied)flags|=2;
 P assigned;P* result=&(assigned=p);if(result==&assigned&&assigned==p)flags|=4;P* self=&(assigned=assigned);if(self==&assigned&&assigned==p)flags|=8;
 D target;target=d;target=target;target.reset();target.param(p);auto extracted=target.param();if(extracted==p&&target==d&&target.min()==R(-3)&&target.max()==R(5))flags|=16;
 auto boundaries=target.intervals();auto density=target.densities();for(R v:boundaries)bh=bh*257+bits(v);for(R v:density)dh=dh*257+bits(v);G e{17,0};for(unsigned i=0;i<10003;++i)h=h*257+bits(target(e,p));next=e();}
 if(flags!=31)return 1;if(bh!=expectedB||dh!=expectedD)return 2;if(h!=expectedH||next!=expectedNext)return 3;if(allocations!=expectedCount||frees!=expectedCount||offset!=expectedBytes)return 4;return 0;}
int main(){return api<float>(54962669649657856ULL,17514634940514304ULL,1182111340672980190ULL,1860345957ULL,35,560);}
