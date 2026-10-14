#include <random>
#include <utility>
template<class E,class B> int check(unsigned initializationDraws) {
 B b(23), expectedBase(b); expectedBase.discard(initializationDraws);
 E fromBase(b), expected(23);
 if (fromBase != expected || fromBase.base() != expectedBase) return 1;
 const E &constant = fromBase;
 if (&constant.base() != &fromBase.base()) return 2;
 E fromMove(std::move(b));
 if (fromMove != expected) return 3;
 E copied(std::move(fromMove));
 if(copied != expected) return 4;
 E assigned; assigned.operator=(std::move(copied));
 if(assigned != expected || assigned.operator()() != expected())return 5;
 assigned.seed(); E defaulted;
 if(assigned != defaulted) return 6;
 assigned.seed(0); E zero(0);
 if(assigned != zero || assigned() != zero())return 7;
 auto *p=&assigned;p->discard(0);const auto &view=p->base();
 if(view != assigned.base())return 8;
 return 0;
}
int main(){
 if(int r=check<std::ranlux24,std::ranlux24_base>(0))return r;
 if(int r=check<std::ranlux48,std::ranlux48_base>(0))return 16+r;
 if(int r=check<std::knuth_b,std::minstd_rand0>(257))return 32+r;
 if(int r=check<std::shuffle_order_engine<std::mt19937_64,17>,std::mt19937_64>(18))return 48+r;
 if(int r=check<std::independent_bits_engine<std::ranlux24_base,16,unsigned short>,std::ranlux24_base>(0))return 64+r;
 return 0;
}
