using Size=decltype(sizeof(0));
template<class F>using Decay=__decay(F);
template<class T>using Function=T(int);
template<class F>using Erased=int;
extern "C" bool function_type(){return __is_function(Function<int>);}
extern "C" bool function_pointer(){return __is_same(Decay<int(int)>,int(*)(int));}
extern "C" bool function_noexcept(){return __is_same(Decay<int(int)noexcept>,int(*)(int)noexcept);}
extern "C" bool function_different(){return __is_same(Decay<int(int)noexcept>,int(*)(int));}
extern "C" bool function_erased(){return __is_same(Erased<int(int)>,int);}
extern "C" bool function_void(){return __is_same(Decay<void()>,void(*)());}
extern "C" bool function_construction(){return __is_constructible(Decay<int(int)>,int(int));}
extern "C" bool function_conversion(){return __is_convertible(int(int),Decay<int(int)>);}
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
  if(function_pointer_size()!=sizeof(void*)) return 9;
  if(calls!=0||effects!=0) return 10;
  Decay<int(int)noexcept> pointer=&bump;
  if(pointer(3)!=5||calls!=1) return 11;
  Decay<int(int)> ordinary=pointer;
  if(ordinary(5)!=7||calls!=2) return 12;
  auto deduced=pass(&bump);
  if(deduced(7)!=9||calls!=3) return 13;
  Callback<> callback{ordinary};
  if(callback.value(9)!=11||calls!=4) return 14;
  Decay<Checked> checked=ordinary;
  if(checked(11)!=13||calls!=5) return 15;
  if(!all_functions<>()||!all_functions<int(int),void()>()||all_functions<int,int(int)>()) return 16;
  if(effects!=0) return 17;
  return 0;
}
