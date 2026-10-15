#include <random>
struct Steps{using result_type=unsigned;unsigned calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){const unsigned values[]={0,0,0x80000000u,0x80000000u,0x40000000u,0x80000000u};return values[calls++%6];}};
struct Mutates{using result_type=unsigned;unsigned calls=0;std::lognormal_distribution<float>::param_type* target;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){*target=std::lognormal_distribution<float>::param_type(9.f,4.f);const unsigned values[]={0,0,0x80000000u,0x80000000u,0x40000000u,0x80000000u};return values[calls++%6];}};
int main(){using D=std::lognormal_distribution<float>;using P=D::param_type;Steps g;D d;float first=d(g);if(g.calls!=6||first<.188f||first>.190f)return 1;
 D copied(d),moved(static_cast<D&&>(d)),assigned;assigned=d;if(copied!=moved||assigned!=copied||d!=copied)return 2;
 d.reset();float second=d(g);if(g.calls!=12||first!=second)return 3;d.param(P(3.f,2.f));float changed=d(g);if(changed<20.08f||changed>20.09f||g.calls!=12)return 4;
 if(copied(g)!=1.f||moved(g)!=1.f||assigned(g)!=1.f||g.calls!=12)return 5;
 D cold,hot;hot(g);if(g.calls!=18||hot==cold||!(hot!=cold))return 6;hot.reset();if(hot!=cold)return 7;
 D wrongMean(2.f,1.f),wrongDeviation(0.f,2.f);if(cold==wrongMean||cold==wrongDeviation)return 8;
 Steps parameterG;D supplied(5.f,3.f);P alternate(7.f,2.f);float external=supplied(parameterG,alternate);if(parameterG.calls!=6||external<38.f||external>40.f||supplied.m()!=5.f||supplied.s()!=3.f)return 9;
 float cachedExternal=supplied(parameterG,alternate);if(cachedExternal<1096.f||cachedExternal>1097.f||parameterG.calls!=6||supplied.m()!=5.f||supplied.s()!=3.f)return 10;
 P changing(2.f,1.f);Mutates mutator{0,&changing};D protectedParameters;float protectedSample=protectedParameters(mutator,changing);if(mutator.calls!=6||protectedSample<1.39f||protectedSample>1.41f||changing.m()!=9.f||changing.s()!=4.f||protectedParameters.m()!=0.f||protectedParameters.s()!=1.f)return 12;
 float changedSample=protectedParameters(mutator,changing);if(mutator.calls!=6||changedSample<8103.f||changedSample>8104.f)return 13;
 return 0;}
