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
const unsigned long long s0[]={814407903ULL,1061195638ULL,1057482551ULL,1041075867ULL,1015243084ULL,1060249890ULL,1060718931ULL,1077427293ULL,1067732286ULL,1043646155ULL,1079578286ULL,1079802796ULL,1029587064ULL,1080419607ULL,1083557308ULL,1044569003ULL};
if(int r=check<float,std::minstd_rand>(0,s0,1065353216ULL,0ULL,2139095040ULL,552575312ULL))return 0+r;
const unsigned long long s1[]={943489163ULL,1043726270ULL,1072394245ULL,1083061699ULL,1088172898ULL,1054302942ULL,1069103873ULL,1065479241ULL,1058811448ULL,1076767387ULL,1044390263ULL,1070753251ULL,1071608156ULL,1079265329ULL,1072001843ULL,1068991372ULL};
if(int r=check<float,std::minstd_rand>(1,s1,1073741824ULL,0ULL,2139095040ULL,366270559ULL))return 16+r;
const unsigned long long s2[]={1086938638ULL,1101840173ULL,1084620029ULL,1086411193ULL,1086676997ULL,1090695378ULL,1074694991ULL,1096074097ULL,1096539530ULL,1098986698ULL,1075461672ULL,1080722747ULL,1089579548ULL,1086690476ULL,1087166688ULL,1086888391ULL};
if(int r=check<float,std::minstd_rand>(2,s2,1088421888ULL,0ULL,2139095040ULL,687050729ULL))return 32+r;
const unsigned long long s3[]={4579489840705872622ULL,4589850003561770379ULL,4608940285335275853ULL,4611376231777138100ULL,4625086875363679018ULL,4600211542205161770ULL,4618839914308573496ULL,4595397498476219742ULL,4602955300546806782ULL,4600403539294418075ULL,4609885606255803825ULL,4610159409695183731ULL,4604364579903274408ULL,4599786835016073558ULL,4587905731374362080ULL,4577851189738198164ULL};
if(int r=check<double,std::minstd_rand>(0,s3,4607182418800017408ULL,0ULL,9218868437227405312ULL,390948395ULL))return 48+r;
const unsigned long long s4[]={4595571539255176751ULL,4616689587936127820ULL,4601249848879849437ULL,4607250078787259616ULL,4613310354941471740ULL,4610081540311496090ULL,4614651427258736012ULL,4609135639104256532ULL,4622174194476340277ULL,4619341101190198727ULL,4607679746228017714ULL,4611949149478090181ULL,4619022286286209790ULL,4615972720481323373ULL,4604447420228947268ULL,4607502499161277425ULL};
if(int r=check<double,std::minstd_rand>(1,s4,4611686018427387904ULL,0ULL,9218868437227405312ULL,1308187679ULL))return 64+r;
const unsigned long long s5[]={4620964773707412950ULL,4621776145915377204ULL,4616264045308394867ULL,4612083910309400847ULL,4617525042187153005ULL,4617940558668914598ULL,4616354053784956694ULL,4621293727651213063ULL,4621385207781804138ULL,4618442653039322288ULL,4619927453904361493ULL,4615263379347256358ULL,4619902571238641486ULL,4610764041823551306ULL,4616829161133437822ULL,4618331197647865200ULL};
if(int r=check<double,std::minstd_rand>(2,s5,4619567317775286272ULL,0ULL,9218868437227405312ULL,1544010504ULL))return 80+r;
return 0;}
