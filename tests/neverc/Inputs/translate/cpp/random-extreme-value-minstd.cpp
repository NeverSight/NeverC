#include <random>
struct G{using result_type=unsigned;unsigned state=17,calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R>unsigned long long bits(R v){const auto*p=static_cast<const unsigned char*>(static_cast<const void*>(&v));unsigned long long n=0;for(unsigned i=0;i<sizeof(R);++i)n|=static_cast<unsigned long long>(p[i])<<(8*i);return n;}
template<class R>R value(unsigned long long raw){R v{};auto*p=static_cast<unsigned char*>(static_cast<void*>(&v));for(unsigned i=0;i<sizeof(R);++i)p[i]=static_cast<unsigned char>(raw>>(8*i));return v;}
template<class R>R magnitude(R x){return x<R(0)?-x:x;}
template<class R,class Engine>int check(unsigned k,const unsigned long long* expected,unsigned long long ea,unsigned long long eb,unsigned long long minimum,unsigned long long maximum,unsigned long long next){using D=std::extreme_value_distribution<R>;using P=typename D::param_type;R a[]={R(0),R(-2.5),R(8)},b[]={R(1),R(.5),R(3)};
 P defaults;D initial;P one(R(-2));D first(R(4));if(defaults.a()!=R(0)||defaults.b()!=R(1)||initial.a()!=R(0)||initial.b()!=R(1)||one.b()!=R(1)||first.b()!=R(1))return 1;
 P p(a[k],b[k]),pc(p),pm(static_cast<P&&>(p));D d(p),dc(d),dm(static_cast<D&&>(d));if(p!=pc||pm!=p||d!=dc||dm!=d)return 2;
 P assignedP;P*pr=&(assignedP=p);P*selfP=&(assignedP=assignedP);D assigned;D*dr=&(assigned=d);D*selfD=&(assigned=assigned);assigned.reset();assigned.param(p);if(pr!=&assignedP||selfP!=&assignedP||assignedP!=p||dr!=&assigned||selfD!=&assigned||assigned.param()!=p||assigned!=d)return 3;
 if(bits(d.a())!=ea||bits(d.b())!=eb||bits(d.min())!=minimum||bits(d.max())!=maximum)return 4;
 Engine e;for(unsigned i=0;i<10003;++i){R x=assigned(e,p);if(!(x==x))return 5;if(i<16){R reference=value<R>(expected[i]);R tolerance=(sizeof(R)==4?R(.0000005):R(.000000000000004))*(magnitude(reference)+magnitude(a[k])+magnitude(b[k])+R(1));if(magnitude(x-reference)>tolerance)return 6;}}
 if(static_cast<unsigned long long>(e())!=next)return 7;return 0;}
int main(){
const unsigned long long s0[]={1093353630ULL,1075506023ULL,1034648446ULL,3209454943ULL,3214813693ULL,1070042776ULL,1051044819ULL,1059954905ULL,1066924503ULL,3198005169ULL,1075278797ULL,1044963966ULL,1040927062ULL,3204546995ULL,1038105986ULL,1051357235ULL};
if(int r=check<float,std::minstd_rand>(0,s0,0ULL,1065353216ULL,4286578688ULL,2139095040ULL,366270559ULL))return 0+r;
const unsigned long long s1[]={1077313852ULL,3215266969ULL,3223147026ULL,3224997036ULL,3225913983ULL,3218880692ULL,3222644035ULL,3221900261ULL,3220439828ULL,3223968507ULL,3215494195ULL,3222911212ULL,3223037365ULL,3224383542ULL,3223093002ULL,3222624509ULL};
if(int r=check<float,std::minstd_rand>(1,s1,3223322624ULL,1056964608ULL,4286578688ULL,2139095040ULL,366270559ULL))return 8+r;
const unsigned long long s2[]={1109422198ULL,1098133645ULL,1090782437ULL,1085495804ULL,1082744962ULL,1095423353ULL,1091536924ULL,1092652585ULL,1094254000ULL,1088581391ULL,1097963226ULL,1091136158ULL,1090946928ULL,1087336285ULL,1090863473ULL,1091566213ULL};
if(int r=check<float,std::minstd_rand>(2,s2,1090519040ULL,1077936128ULL,4286578688ULL,2139095040ULL,366270559ULL))return 16+r;
const unsigned long long s3[]={4612633166049153093ULL,13828738800154051489ULL,4609700106675847085ULL,4604284220763635022ULL,13822591749553419272ULL,4596236024334115420ULL,13826103865734008420ULL,4599668383476974390ULL,13833574582760826074ULL,13831561640797084149ULL,4603475063409020148ULL,13811716249084295648ULL,13831370059499920457ULL,13827571019401140652ULL,4607430315280321350ULL,4603800098880681687ULL};
if(int r=check<double,std::minstd_rand>(0,s3,0ULL,4607182418800017408ULL,18442240474082181120ULL,9218868437227405312ULL,1308187679ULL))return 24+r;
const unsigned long long s4[]={13831859107846713275ULL,13837082898158256244ULL,13833799211344248874ULL,13835420330036711510ULL,13836530710987802432ULL,13835963080062644461ULL,13836753531355750860ULL,13835809157466564557ULL,13838064886872357174ULL,13837561651381421693ULL,13835521474706038370ULL,13836247887932707316ULL,13837513756057130770ULL,13836936925564142390ULL,13834934107042011741ULL,13835480845272080677ULL};
if(int r=check<double,std::minstd_rand>(1,s4,13836183955189006336ULL,4602678819172646912ULL,18442240474082181120ULL,9218868437227405312ULL,1308187679ULL))return 32+r;
const unsigned long long s5[]={4624781278118980660ULL,4617996388774379172ULL,4623326200495828961ULL,4621838655410571134ULL,4619652950285740606ULL,4621024530371671708ULL,4618984489181895322ULL,4621255414265791565ULL,4613911227209394360ULL,4616560129104882824ULL,4621686938406580846ULL,4620501419451025957ULL,4616703815077755593ULL,4618434306556720736ULL,4622475028722506810ULL,4621747882557517384ULL};
if(int r=check<double,std::minstd_rand>(2,s5,4620693217682128896ULL,4613937818241073152ULL,18442240474082181120ULL,9218868437227405312ULL,1308187679ULL))return 40+r;
return 0;}
