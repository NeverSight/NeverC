#include <random>
struct G{using result_type=unsigned;unsigned state=17,calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R>unsigned long long bits(R v){const auto*p=static_cast<const unsigned char*>(static_cast<const void*>(&v));unsigned long long n=0;for(unsigned i=0;i<sizeof(R);++i)n|=static_cast<unsigned long long>(p[i])<<(8*i);return n;}
template<class R>R value(unsigned long long raw){R v{};auto*p=static_cast<unsigned char*>(static_cast<void*>(&v));for(unsigned i=0;i<sizeof(R);++i)p[i]=static_cast<unsigned char>(raw>>(8*i));return v;}
template<class R>R magnitude(R x){return x<R(0)?-x:x;}
template<class R,class Engine>int check(unsigned k,const unsigned long long* expected,unsigned long long ea,unsigned long long eb,unsigned long long minimum,unsigned long long maximum,unsigned long long next){using D=std::gamma_distribution<R>;using P=typename D::param_type;R a[]={R(1),R(.5),R(2.5)},b[]={R(1),R(.5),R(3)};
 P defaults;D initial;P one(R(2));D first(R(4));if(defaults.alpha()!=R(1)||defaults.beta()!=R(1)||initial.alpha()!=R(1)||initial.beta()!=R(1)||one.beta()!=R(1)||first.beta()!=R(1))return 1;
 P p(a[k],b[k]),pc(p),pm(static_cast<P&&>(p));D d(p),dc(d),dm(static_cast<D&&>(d));if(p!=pc||pm!=p||d!=dc||dm!=d)return 2;
 P assignedP;P*pr=&(assignedP=p);P*selfP=&(assignedP=assignedP);D assigned;D*dr=&(assigned=d);D*selfD=&(assigned=assigned);assigned.reset();assigned.param(p);if(pr!=&assignedP||selfP!=&assignedP||assignedP!=p||dr!=&assigned||selfD!=&assigned||assigned.param()!=p||assigned!=d)return 3;
 if(bits(d.alpha())!=ea||bits(d.beta())!=eb||bits(d.min())!=minimum||bits(d.max())!=maximum)return 4;
 Engine e;for(unsigned i=0;i<10003;++i){R x=assigned(e,p);if(!(x==x))return 5;if(i<16){R reference=value<R>(expected[i]);R tolerance=(sizeof(R)==4?R(.0000005):R(.000000000000004))*(magnitude(reference)+magnitude(a[k])+magnitude(b[k])+R(1));if(magnitude(x-reference)>tolerance)return 6;}}
 if(static_cast<unsigned long long>(e())!=next)return 7;return 0;}
int main(){
const unsigned long long s0[]={1069930232ULL,1049861898ULL,1067368090ULL,1077647622ULL,1017080030ULL,1057283868ULL,1049899412ULL,1018967231ULL,1060912455ULL,1054368059ULL,1050940740ULL,1062389169ULL,1041923415ULL,1061745224ULL,1060957053ULL,1073284237ULL};
if(int r=check<float,std::mt19937_64>(0,s0,1065353216ULL,1065353216ULL,0ULL,2139095040ULL,3657840786615683573ULL))return 0+r;
const unsigned long long s1[]={1054580842ULL,1050225816ULL,960673008ULL,1040909681ULL,1025112491ULL,1008772954ULL,1040956218ULL,1040172253ULL,1051892270ULL,1022001776ULL,1064095889ULL,1022815949ULL,1050463550ULL,1053388089ULL,1037605774ULL,1053892550ULL};
if(int r=check<float,std::mt19937_64>(1,s1,1056964608ULL,1056964608ULL,0ULL,2139095040ULL,14012012891671831484ULL))return 8+r;
const unsigned long long s2[]={1092572683ULL,1090646090ULL,1083854438ULL,1083896153ULL,1083171632ULL,1091425294ULL,1098601718ULL,1090761448ULL,1092077038ULL,1081598906ULL,1092288950ULL,1101345480ULL,1069851522ULL,1083341701ULL,1065968100ULL,1082556401ULL};
if(int r=check<float,std::mt19937_64>(2,s2,1075838976ULL,1077936128ULL,0ULL,2139095040ULL,16061261972519288336ULL))return 16+r;
const unsigned long long s3[]={4609639685794086531ULL,4598865580509351323ULL,4608264145696983550ULL,4613782928237412756ULL,4581265944042058253ULL,4602850220990507513ULL,4598885720185994049ULL,4582279123855602893ULL,4604798303001805401ULL,4601284807738974831ULL,4599444779360669108ULL,4605591107807809626ULL,4594603640598554064ULL,4605245392478329082ULL,4604822246375977386ULL,4611440352648756912ULL};
if(int r=check<double,std::mt19937_64>(0,s3,4607182418800017408ULL,4607182418800017408ULL,0ULL,9218868437227405312ULL,3657840786615683573ULL))return 24+r;
const unsigned long long s4[]={4601399045256780156ULL,4599060957125558385ULL,4550982659321039031ULL,4594059394660792294ULL,4585578343632068972ULL,4576806121922356986ULL,4594084379378241183ULL,4593663492214676904ULL,4599955628886682664ULL,4583908291688258294ULL,4606507398867695680ULL,4584345397432121936ULL,4599188590129143487ULL,4600758690419713241ULL,4592285623923119261ULL,4601029520223594538ULL};
if(int r=check<double,std::mt19937_64>(1,s4,4602678819172646912ULL,4602678819172646912ULL,0ULL,9218868437227405312ULL,14012012891671831484ULL))return 32+r;
const unsigned long long s5[]={4621795758623435184ULL,4620761426939787620ULL,4617115186041833043ULL,4617137581589531487ULL,4616748607790615780ULL,4621179759193888080ULL,4625032573258575980ULL,4620823359426530210ULL,4621529661831291911ULL,4615904256912751472ULL,4621643429998958374ULL,4626505618413292594ULL,4609597429368761189ULL,4616839912927181568ULL,4607512532382093497ULL,4616418308463958758ULL};
if(int r=check<double,std::mt19937_64>(2,s5,4612811918334230528ULL,4613937818241073152ULL,0ULL,9218868437227405312ULL,16061261972519288336ULL))return 40+r;
return 0;}
