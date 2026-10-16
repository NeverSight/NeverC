#include <random>
struct G{using result_type=unsigned;unsigned state=17,calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R>unsigned long long bits(R v){const auto*p=static_cast<const unsigned char*>(static_cast<const void*>(&v));unsigned long long n=0;for(unsigned i=0;i<sizeof(R);++i)n|=static_cast<unsigned long long>(p[i])<<(8*i);return n;}
template<class R>R value(unsigned long long raw){R v{};auto*p=static_cast<unsigned char*>(static_cast<void*>(&v));for(unsigned i=0;i<sizeof(R);++i)p[i]=static_cast<unsigned char>(raw>>(8*i));return v;}
template<class R>R magnitude(R x){return x<R(0)?-x:x;}

template<class R,class Engine>int check(unsigned k,const unsigned long long* expected,unsigned long long degree,unsigned long long minimum,unsigned long long maximum,unsigned long long next){using D=std::chi_squared_distribution<R>;using P=typename D::param_type;R ns[]={R(1),R(2),R(7)};
 P defaults;D initial;if(defaults.n()!=R(1)||initial.n()!=R(1)||initial==D(R(2))||!(defaults!=P(R(2))))return 1;
 P p(ns[k]),pc(p),pm(static_cast<P&&>(p));D d(p),dc(d),dm(static_cast<D&&>(d));if(p!=pc||pm!=p||d!=dc||dm!=d)return 2;
 P assignedP;P*pr=&(assignedP=p);P*selfP=&(assignedP=assignedP);D assigned;D*dr=&(assigned=d);D*selfD=&(assigned=assigned);assigned.reset();assigned.param(p);if(pr!=&assignedP||selfP!=&assignedP||assignedP!=p||dr!=&assigned||selfD!=&assigned||assigned.param()!=p||assigned!=d)return 3;
 if(bits(d.n())!=degree||bits(d.min())!=minimum||bits(d.max())!=maximum)return 4;
 Engine e;for(unsigned i=0;i<10003;++i){R x=assigned(e,p);if(!(x==x))return 5;if(i<16){R reference=value<R>(expected[i]);R tolerance=(sizeof(R)==4?R(.0000005):R(.000000000000004))*(magnitude(reference)+magnitude(ns[k])+R(1));if(magnitude(x-reference)>tolerance)return 6;}}
 if(static_cast<unsigned long long>(e())!=next)return 7;return 0;}
int main(){
const unsigned long long s0[]={1039216222ULL,1054361363ULL,1067153938ULL,1065417613ULL,1028076036ULL,1057309127ULL,1036458023ULL,1078448009ULL,1044810068ULL,1054384548ULL,1041826371ULL,1071915939ULL,957106002ULL,1030926346ULL,1036473002ULL,1046625001ULL};
if(int r=check<float,G>(0,s0,1065353216ULL,0ULL,2139095040ULL,1128622408ULL))return 0+r;
const unsigned long long s1[]={1057902057ULL,1079762766ULL,1067289278ULL,1060407455ULL,1075853879ULL,1062158958ULL,1074687880ULL,1056401426ULL,1051564106ULL,1070678311ULL,1068934739ULL,1064582275ULL,1056901802ULL,1040965257ULL,1082935586ULL,1066643571ULL};
if(int r=check<float,G>(1,s1,1073741824ULL,0ULL,2139095040ULL,1860345957ULL))return 16+r;
const unsigned long long s2[]={1083165221ULL,1090429890ULL,1089005305ULL,1084491183ULL,1061815776ULL,1095445682ULL,1075653536ULL,1083175159ULL,1072263926ULL,1092140269ULL,1077003124ULL,1091096860ULL,1091085285ULL,1068693630ULL,1089208940ULL,1078031553ULL};
if(int r=check<float,G>(2,s2,1088421888ULL,0ULL,2139095040ULL,2565220004ULL))return 32+r;
const unsigned long long s3[]={4611797511767778683ULL,4597153243648786773ULL,4603867523747151656ULL,4576178125499208198ULL,4604141697423469550ULL,4582606501138602427ULL,4610006801304677652ULL,4605733062782212544ULL,4607503050086167376ULL,4586335234782159135ULL,4590962236120132599ULL,4586584222695852329ULL,4603402709509206836ULL,4608754520333180355ULL,4578406192275770452ULL,4601472221795104804ULL};
if(int r=check<double,G>(0,s3,4607182418800017408ULL,0ULL,9218868437227405312ULL,4166708416ULL))return 48+r;
const unsigned long long s4[]={4614918487517435851ULL,4604527183572205533ULL,4605467514151058222ULL,4602376462718065622ULL,4610041307991938457ULL,4606768523418114250ULL,4594089234476614815ULL,4607875173376306121ULL,4610282311895171932ULL,4612914554609737071ULL,4597548621466296666ULL,4608333474125490152ULL,4613924797771353160ULL,4611942879276154143ULL,4611583267897904093ULL,4612030771253832749ULL};
if(int r=check<double,G>(1,s4,4611686018427387904ULL,0ULL,9218868437227405312ULL,2013693826ULL))return 64+r;
const unsigned long long s5[]={4621940865886578989ULL,4613454767680642683ULL,4618142741827344676ULL,4618310848975269677ULL,4621334404635758964ULL,4619176007467755474ULL,4620124949255624727ULL,4617840499602383596ULL,4620897291193853266ULL,4613920811823848980ULL,4616826463270817131ULL,4617832014384119548ULL,4618907791842861516ULL,4622302333648246628ULL,4619096710465376184ULL,4618143949819449651ULL};
if(int r=check<double,G>(2,s5,4619567317775286272ULL,0ULL,9218868437227405312ULL,4031179692ULL))return 80+r;
return 0;}
