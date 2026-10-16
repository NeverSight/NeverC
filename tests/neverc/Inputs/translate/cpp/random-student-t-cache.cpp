#include <random>
struct Warm{using result_type=unsigned;unsigned calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){const unsigned values[]={0x40000000u,0xc0000000u,0x80000000u};return values[calls++%3];}};
struct Probe{using result_type=unsigned;unsigned calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){const unsigned values[]={0x80000000u,0x40000000u,0xc0000000u};return values[calls++%3];}};
struct Mutates{using result_type=unsigned;unsigned calls=0;std::student_t_distribution<float>::param_type*target;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;*target=std::student_t_distribution<float>::param_type(8.f);return 0x80000000u;}};
int main(){using D=std::student_t_distribution<float>;using P=D::param_type;D d(2.f);Warm w;float first=d(w);if(w.calls!=3||first>-.9999f||first< -1.0001f)return 1;
 D copied(d),moved(static_cast<D&&>(d)),assigned,resetter(d),changed(d),mutating(d),cold(2.f);assigned=d;if(d!=cold||copied!=cold||moved!=cold||assigned!=cold)return 2;
 Probe a,b,c;float ca=copied(a),cb=moved(b),cc=assigned(c);if(a.calls!=1||b.calls!=1||c.calls!=1||ca<.9999f||ca>1.0001f||ca!=cb||cb!=cc)return 3;
 resetter.reset();Warm fresh;float r=resetter(fresh);if(fresh.calls!=3||r!=first||resetter!=cold)return 4;
 changed.param(P(4.f));Probe afterChange;float replacement=changed(afterChange);if(afterChange.calls!=2||replacement<1.177f||replacement>1.178f||changed.n()!=4.f||changed==cold)return 5;
 P changing(2.f);Mutates g{0,&changing};float explicitSample=mutating(g,changing);bool allowed=(explicitSample>.9999f&&explicitSample<1.0001f)||(explicitSample>1.9999f&&explicitSample<2.0001f);if(g.calls!=1||!allowed||changing.n()!=8.f||mutating.n()!=2.f)return 6;
 return 0;}
