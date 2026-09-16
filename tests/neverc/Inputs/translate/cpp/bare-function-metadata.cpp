using Size=decltype(sizeof(0));
template<class F>using Decay=__decay(F);
template<class T>using Function=T(int);
template<class F>using Erased=int;
using RawFunction=int(int);
using FunctionLRef=RawFunction&;
using FunctionRRef=RawFunction&&;
extern "C" bool function_type(){return __is_function(Function<int>);}
extern "C" bool function_pointer(){return __is_same(Decay<int(int)>,int(*)(int));}
extern "C" bool function_noexcept(){return __is_same(Decay<int(int)noexcept>,int(*)(int)noexcept);}
extern "C" bool function_different(){return __is_same(Decay<int(int)noexcept>,int(*)(int));}
extern "C" bool function_erased(){return __is_same(Erased<int(int)>,int);}
extern "C" bool function_void(){return __is_same(Decay<void()>,void(*)());}
extern "C" bool function_construction(){return __is_constructible(Decay<int(int)>,int(int));}
extern "C" bool function_conversion(){return __is_convertible(int(int),Decay<int(int)>);}
extern "C" bool function_lvalue_reference(){return __is_lvalue_reference(FunctionLRef);}
extern "C" bool function_rvalue_reference(){return __is_rvalue_reference(FunctionRRef);}
extern "C" bool function_reference_remove(){return __is_same(__remove_reference_t(FunctionLRef),RawFunction);}
extern "C" bool function_reference_decay(){return __is_same(Decay<FunctionLRef>,RawFunction*);}
extern "C" bool function_reference_destructible(){return __is_nothrow_destructible(FunctionLRef);}
extern "C" bool function_reference_rank(){return __array_rank(FunctionLRef)==0;}
extern "C" Size function_pointer_size(){return sizeof(Decay<int(int)>);}

int calls;
int effects;
int bump(int n)noexcept{++calls;return n+2;}
int effect(){return ++effects;}
template<class F>Decay<F> pass(F* value){return value;}
template<class F=int(int)>struct Callback{Decay<F> value;};
template<class... F>constexpr bool all_functions(){return (__is_function(F)&&...);}
using Checked=Function<decltype(effect())>;
int main() {
  if(!function_type()) return 1;
  if(!function_pointer()) return 2;
  if(!function_noexcept()) return 3;
  if(function_different()) return 4;
  if(!function_erased()) return 5;
  if(!function_void()) return 6;
  if(!function_construction()) return 7;
  if(!function_conversion()) return 8;
  if(!function_lvalue_reference()) return 9;
  if(!function_rvalue_reference()) return 10;
  if(!function_reference_remove()) return 11;
  if(!function_reference_decay()) return 12;
  if(!function_reference_destructible()) return 13;
  if(function_pointer_size()!=sizeof(void*)) return 14;
  if(!function_reference_rank()) return 15;
  if(calls!=0||effects!=0) return 16;
  Decay<int(int)noexcept> pointer=&bump;
  if(pointer(3)!=5||calls!=1) return 17;
  Decay<int(int)> ordinary=pointer;
  if(ordinary(5)!=7||calls!=2) return 18;
  auto deduced=pass(&bump);
  if(deduced(7)!=9||calls!=3) return 19;
  Callback<> callback{ordinary};
  if(callback.value(9)!=11||calls!=4) return 20;
  Decay<Checked> checked=ordinary;
  if(checked(11)!=13||calls!=5) return 21;
  if(!all_functions<>()||!all_functions<int(int),void()>()||all_functions<int,int(int)>()) return 22;
  if(effects!=0) return 23;
  return 0;
}
