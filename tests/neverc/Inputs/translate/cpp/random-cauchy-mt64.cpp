#include <random>
struct G{using result_type=unsigned;unsigned state=17,calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R>unsigned long long bits(R v){const auto*p=static_cast<const unsigned char*>(static_cast<const void*>(&v));unsigned long long n=0;for(unsigned i=0;i<sizeof(R);++i)n|=static_cast<unsigned long long>(p[i])<<(8*i);return n;}
template<class R>R value(unsigned long long raw){R v{};auto*p=static_cast<unsigned char*>(static_cast<void*>(&v));for(unsigned i=0;i<sizeof(R);++i)p[i]=static_cast<unsigned char>(raw>>(8*i));return v;}
template<class R>R magnitude(R x){return x<R(0)?-x:x;}
template<class R,class Engine>int check(unsigned k,const unsigned long long* expected,unsigned long long ea,unsigned long long eb,unsigned long long minimum,unsigned long long maximum,unsigned long long next){using D=std::cauchy_distribution<R>;using P=typename D::param_type;R a[]={R(0),R(-2.5),R(8)},b[]={R(1),R(.5),R(3)};
 P defaults;D initial;P one(R(-2));D first(R(4));if(defaults.a()!=R(0)||defaults.b()!=R(1)||initial.a()!=R(0)||initial.b()!=R(1)||one.b()!=R(1)||first.b()!=R(1))return 1;
 P p(a[k],b[k]),pc(p),pm(static_cast<P&&>(p));D d(p),dc(d),dm(static_cast<D&&>(d));if(p!=pc||pm!=p||d!=dc||dm!=d)return 2;
 P assignedP;P*pr=&(assignedP=p);P*selfP=&(assignedP=assignedP);D assigned;D*dr=&(assigned=d);D*selfD=&(assigned=assigned);assigned.reset();assigned.param(p);if(pr!=&assignedP||selfP!=&assignedP||assignedP!=p||dr!=&assigned||selfD!=&assigned||assigned.param()!=p||assigned!=d)return 3;
 if(bits(d.a())!=ea||bits(d.b())!=eb||bits(d.min())!=minimum||bits(d.max())!=maximum)return 4;
 Engine e;for(unsigned i=0;i<10003;++i){R x=assigned(e,p);if(!(x==x))return 5;if(i<16){R reference=value<R>(expected[i]);R tolerance=(sizeof(R)==4?R(.0000005):R(.000000000000004))*(magnitude(reference)+magnitude(a[k])+magnitude(b[k])+R(1));if(magnitude(x-reference)>tolerance)return 6;}}
 if(static_cast<unsigned long long>(e())!=next)return 7;return 0;}
int main(){
const unsigned long long s0[]={3209343871ULL,1065378572ULL,3215215821ULL,3190632803ULL,1031293023ULL,1078972113ULL,1065422964ULL,1033003331ULL,3245760048ULL,1072768326ULL,1066736295ULL,3232028697ULL,1055981990ULL,3236350281ULL,3244819801ULL,3203838517ULL};
if(int r=check<float,std::mt19937_64>(0,s0,0ULL,1065353216ULL,4286578688ULL,2139095040ULL,3657840786615683573ULL))return 0+r;
const unsigned long long s1[]={3224983152ULL,3221212794ULL,3226014515ULL,3223677323ULL,3223195503ULL,3210764893ULL,3221190598ULL,3223172731ULL,3240308504ULL,3217517917ULL,3220533933ULL,3231869964ULL,3222335462ULL,3234030757ULL,3239838381ULL,3224333091ULL};
if(int r=check<float,std::mt19937_64>(1,s1,3223322624ULL,1056964608ULL,4286578688ULL,2139095040ULL,3657840786615683573ULL))return 8+r;
const unsigned long long s2[]={1085537457ULL,1093674277ULL,1082443366ULL,1089454943ULL,1090709721ULL,1099820431ULL,1093690924ULL,1090743880ULL,3256403620ULL,1096445434ULL,1094183423ULL,3236857931ULL,1091999784ULL,3243912686ULL,3255698435ULL,1087487638ULL};
if(int r=check<float,std::mt19937_64>(2,s2,1090519040ULL,1077936128ULL,4286578688ULL,2139095040ULL,3657840786615683573ULL))return 16+r;
const unsigned long long s3[]={13828679168757339782ULL,4607196031529348633ULL,13831831649368740169ULL,13818633739966501167ULL,4588896491663177314ULL,4614494009852547859ULL,4607219864269310629ULL,4589814706257657243ULL,13848229964978564429ULL,4611163376407335555ULL,4607924953344700686ULL,13840857994058618212ULL,4602151280016956980ULL,13843178128628499686ULL,13847725173230486198ULL,13825723506035343494ULL};
if(int r=check<double,std::mt19937_64>(0,s3,0ULL,4607182418800017408ULL,18442240474082181120ULL,9218868437227405312ULL,3657840786615683573ULL))return 24+r;
const unsigned long long s4[]={13837075444233667281ULL,13835051248917498100ULL,13837629153524335698ULL,13836374382777168521ULL,13836115707762229907ULL,13829442072431843802ULL,13835039332547517102ULL,13836103481976592394ULL,13845303184827602598ULL,13833067576478504638ULL,13834686788009822073ULL,13840772774437497522ULL,13835653976432815645ULL,13841932841722438259ULL,13845050788953563483ULL,13836726445767922696ULL};
if(int r=check<double,std::mt19937_64>(1,s4,13836183955189006336ULL,4602678819172646912ULL,18442240474082181120ULL,9218868437227405312ULL,3657840786615683573ULL))return 32+r;
const unsigned long long s5[]={4618018750548146062ULL,4622387172315892042ULL,4616357622676140809ULL,4620121934917642340ULL,4620795588822293539ULL,4625686864140513063ULL,4622396109593377790ULL,4620813927500749810ULL,13853944186809205242ULL,4623874926645137137ULL,4622660517996649061ULL,13843450672356786220ULL,4621488185816414933ULL,13847238165301667672ULL,13853565592998146568ULL,4619065745945379815ULL};
if(int r=check<double,std::mt19937_64>(2,s5,4620693217682128896ULL,4613937818241073152ULL,18442240474082181120ULL,9218868437227405312ULL,3657840786615683573ULL))return 40+r;
return 0;}
