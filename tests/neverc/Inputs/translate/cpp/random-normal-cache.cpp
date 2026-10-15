#include <random>
struct Steps{using result_type=unsigned;unsigned calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){const unsigned values[]={0,0,0x80000000u,0x80000000u,0x40000000u,0x80000000u};return values[calls++%6];}};
int main(){using D=std::normal_distribution<float>;using P=D::param_type;Steps g;D d;float first=d(g);if(g.calls!=6||first> -1.66f||first< -1.67f)return 1;
 D copied(d),moved(static_cast<D&&>(d)),assigned;assigned=d;if(copied!=moved||assigned!=copied||d!=copied)return 2;
 d.reset();float second=d(g);if(g.calls!=12||first!=second)return 3;d.param(P(3.f,2.f));if(d(g)!=3.f||g.calls!=12)return 4;
 if(copied(g)!=0.f||moved(g)!=0.f||assigned(g)!=0.f||g.calls!=12)return 5;
 D cold,hot;hot(g);if(g.calls!=18||hot==cold||!(hot!=cold))return 6;hot.reset();if(hot!=cold)return 7;
 D wrongMean(2.f,1.f),wrongDeviation(0.f,2.f);if(cold==wrongMean||cold==wrongDeviation)return 8;
 Steps parameterG;D supplied(5.f,3.f);P alternate(7.f,2.f);float external=supplied(parameterG,alternate);if(parameterG.calls!=6||external<3.66f||external>3.68f||supplied.mean()!=5.f||supplied.stddev()!=3.f)return 9;
 if(supplied(parameterG,alternate)!=7.f||parameterG.calls!=6||supplied.mean()!=5.f||supplied.stddev()!=3.f)return 10;
 return 0;}
