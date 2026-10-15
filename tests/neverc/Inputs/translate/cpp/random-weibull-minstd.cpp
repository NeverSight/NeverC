#include <random>
struct G{using result_type=unsigned;unsigned state=17,calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R>unsigned long long bits(R v){const auto*p=static_cast<const unsigned char*>(static_cast<const void*>(&v));unsigned long long n=0;for(unsigned i=0;i<sizeof(R);++i)n|=static_cast<unsigned long long>(p[i])<<(8*i);return n;}
template<class R>R value(unsigned long long raw){R v{};auto*p=static_cast<unsigned char*>(static_cast<void*>(&v));for(unsigned i=0;i<sizeof(R);++i)p[i]=static_cast<unsigned char>(raw>>(8*i));return v;}
template<class R>R magnitude(R x){return x<R(0)?-x:x;}
template<class R,class Engine>int check(unsigned k,const unsigned long long* expected,unsigned long long ea,unsigned long long eb,unsigned long long minimum,unsigned long long maximum,unsigned long long next){using D=std::weibull_distribution<R>;using P=typename D::param_type;R a[]={R(1),R(.5),R(8)},b[]={R(1),R(.5),R(3)};
 P defaults;D initial;P one(R(2));D first(R(4));if(defaults.a()!=R(1)||defaults.b()!=R(1)||initial.a()!=R(1)||initial.b()!=R(1)||one.b()!=R(1)||first.b()!=R(1))return 1;
 P p(a[k],b[k]),pc(p),pm(static_cast<P&&>(p));D d(p),dc(d),dm(static_cast<D&&>(d));if(p!=pc||pm!=p||d!=dc||dm!=d)return 2;
 P assignedP;P*pr=&(assignedP=p);P*selfP=&(assignedP=assignedP);D assigned;D*dr=&(assigned=d);D*selfD=&(assigned=assigned);assigned.reset();assigned.param(p);if(pr!=&assignedP||selfP!=&assignedP||assignedP!=p||dr!=&assigned||selfD!=&assigned||assigned.param()!=p||assigned!=d)return 3;
 if(bits(d.a())!=ea||bits(d.b())!=eb||bits(d.min())!=minimum||bits(d.max())!=maximum)return 4;
 Engine e;for(unsigned i=0;i<10003;++i){R x=assigned(e,p);if(!(x==x))return 5;if(i<16){R reference=value<R>(expected[i]);R tolerance=(sizeof(R)==4?R(.0000005):R(.000000000000004))*(magnitude(reference)+magnitude(a[k])+magnitude(b[k])+R(1));if(magnitude(x-reference)>tolerance)return 6;}}
 if(static_cast<unsigned long long>(e())!=next)return 7;return 0;}
int main(){
const unsigned long long s0[]={935100555ULL,1035337662ULL,1064005637ULL,1074673091ULL,1079784290ULL,1045914334ULL,1060715265ULL,1057090633ULL,1050422840ULL,1068378779ULL,1036001655ULL,1062364643ULL,1063219548ULL,1070876721ULL,1063613235ULL,1060602764ULL};
if(int r=check<float,std::minstd_rand>(0,s0,1065353216ULL,1065353216ULL,0ULL,2139095040ULL,366270559ULL))return 0+r;
const unsigned long long s1[]={797625613ULL,998335394ULL,1054377690ULL,1075707743ULL,1086154843ULL,1018509111ULL,1048970837ULL,1040441335ULL,1027510457ULL,1064106980ULL,999305783ULL,1051519825ULL,1052968624ULL,1068500902ULL,1053665101ULL,1048808790ULL};
if(int r=check<float,std::minstd_rand>(1,s1,1056964608ULL,1056964608ULL,0ULL,2139095040ULL,366270559ULL))return 8+r;
const unsigned long long s2[]={1061782704ULL,1074650874ULL,1077805116ULL,1079256742ULL,1080037778ULL,1075708116ULL,1077437341ULL,1076913324ULL,1076200619ULL,1078429985ULL,1074714049ULL,1077631321ULL,1077723993ULL,1078757513ULL,1077765084ULL,1077423286ULL};
if(int r=check<float,std::minstd_rand>(2,s2,1090519040ULL,1077936128ULL,0ULL,2139095040ULL,366270559ULL))return 16+r;
const unsigned long long s3[]={4591067939627806255ULL,4612185988308757324ULL,4596746249252478941ULL,4602746479159889120ULL,4608806755314101244ULL,4605577940684125594ULL,4610147827631365516ULL,4604632039476886036ULL,4617670594848969781ULL,4614837501562828231ULL,4603176146600647218ULL,4607445549850719685ULL,4614518686658839294ULL,4611469120853952877ULL,4599943820601576772ULL,4602998899533906929ULL};
if(int r=check<double,std::minstd_rand>(0,s3,4607182418800017408ULL,4607182418800017408ULL,0ULL,9218868437227405312ULL,1308187679ULL))return 24+r;
const unsigned long long s4[]={4571202497786875790ULL,4612741462653879998ULL,4582033182745063750ULL,4593807956384586000ULL,4606513350032170375ULL,4599755673207913913ULL,4608872319036330590ULL,4598300197842091089ULL,4624142179985017071ULL,4618191957781914648ULL,4594721194080928904ULL,4603220455187270326ULL,4617661330461043000ULL,4611257446275594509ULL,4588896167130316322ULL,4594334529429426135ULL};
if(int r=check<double,std::minstd_rand>(1,s4,4602678819172646912ULL,4602678819172646912ULL,0ULL,9218868437227405312ULL,1308187679ULL))return 32+r;
const unsigned long long s5[]={4612174060469034566ULL,4614646817067277108ULL,4612741663346524186ULL,4613388704988004713ULL,4614202955930954918ULL,4613774176513018797ULL,4614378796194678477ULL,4613662488044677612ULL,4615506623240330462ULL,4615054300295352832ULL,4613458790092182674ULL,4613985938374220482ULL,4615012553058324120ULL,4614526822391300692ULL,4613103159698351496ULL,4613430542561101894ULL};
if(int r=check<double,std::minstd_rand>(2,s5,4620693217682128896ULL,4613937818241073152ULL,0ULL,9218868437227405312ULL,1308187679ULL))return 40+r;
return 0;}
