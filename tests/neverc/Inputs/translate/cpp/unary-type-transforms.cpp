enum class E:unsigned{one=1};
struct Forward;
using Size=decltype(sizeof(0));
template<class T>using Erased=int;
template<class T>using ThroughErased=__remove_cv(Erased<T>);
template<class T>using Plain=__remove_cvref(T);
template<class T>using Pointer=__add_pointer(Plain<T>);
extern "C" bool transformed_add_lvalue(){return __is_same(__add_lvalue_reference(int),int&);}
extern "C" bool transformed_add_pointer(){return __is_same(__add_pointer(const int&),const int*);}
extern "C" bool transformed_add_rvalue(){return __is_same(__add_rvalue_reference(int),int&&);}
extern "C" bool transformed_decay(){return __is_same(__decay(const int[3]),const int*);}
extern "C" bool transformed_make_signed(){return __is_same(__make_signed(unsigned),int);}
extern "C" bool transformed_make_unsigned(){return __is_same(__make_unsigned(int),unsigned);}
extern "C" bool transformed_remove_all_extents(){return __is_same(__remove_all_extents(const int[][3]),const int);}
extern "C" bool transformed_remove_const(){return __is_same(__remove_const(const int),int);}
extern "C" bool transformed_remove_cv(){return __is_same(__remove_cv(const int),int);}
extern "C" bool transformed_remove_cvref(){return __is_same(__remove_cvref(const int&),int);}
extern "C" bool transformed_remove_extent(){return __is_same(__remove_extent(int[][3]),int[3]);}
extern "C" bool transformed_remove_pointer(){return __is_same(__remove_pointer(const int*const),const int);}
extern "C" bool transformed_remove_reference(){return __is_same(__remove_reference_t(const int&&),const int);}
extern "C" bool transformed_remove_restrict(){return __is_same(__remove_restrict(int*),int*);}
extern "C" bool transformed_remove_volatile(){return __is_same(__remove_volatile(const int),const int);}
extern "C" bool transformed_underlying(){return __is_same(__underlying_type(E),unsigned);}
extern "C" bool transformed_erased_alias(){return __is_same(ThroughErased<double>,int);}
extern "C" bool transformed_nested_alias(){return __is_same(Pointer<const int&>,int*);}
extern "C" bool transformed_forward(){return __is_same(__remove_all_extents(Forward[][3]),Forward);}
extern "C" bool transformed_different(){return __is_same(__remove_cvref(const int&),double);}
extern "C" Size transformed_long_size(){return sizeof(__make_signed(unsigned long));}
extern "C" Size transformed_pointer_size(){return sizeof(__add_pointer(int));}

int effects;
int effect(){return ++effects;}
template<class T> Plain<T> copied(T value){return value;}
template<class... T> constexpr bool all_plain(){return (__is_same(Plain<T>,int)&&...);}
template<class T> struct Box{Plain<T> value;};
using Row=__remove_extent(int[2][3]);
using Result=__remove_cvref(decltype(effect()));
using Extent=__remove_all_extents(int[sizeof(effect())]);
int main() {
  if(!transformed_add_lvalue()) return 1;
  if(!transformed_add_pointer()) return 2;
  if(!transformed_add_rvalue()) return 3;
  if(!transformed_decay()) return 4;
  if(!transformed_make_signed()) return 5;
  if(!transformed_make_unsigned()) return 6;
  if(!transformed_remove_all_extents()) return 7;
  if(!transformed_remove_const()) return 8;
  if(!transformed_remove_cv()) return 9;
  if(!transformed_remove_cvref()) return 10;
  if(!transformed_remove_extent()) return 11;
  if(!transformed_remove_pointer()) return 12;
  if(!transformed_remove_reference()) return 13;
  if(!transformed_remove_restrict()) return 14;
  if(!transformed_remove_volatile()) return 15;
  if(!transformed_underlying()) return 16;
  if(!transformed_erased_alias()) return 17;
  if(!transformed_nested_alias()) return 18;
  if(!transformed_forward()) return 19;
  if(transformed_different()) return 20;
  if(transformed_long_size()!=sizeof(long)) return 21;
  if(transformed_pointer_size()!=sizeof(void*)) return 22;
  if(effects!=0) return 23;
  int original=7;
  __add_lvalue_reference(int) reference=original;
  reference=9;
  if(original!=9) return 24;
  __add_pointer(int) pointer=&original;
  if(*pointer!=9) return 25;
  Row row{1,2,3};
  __decay(Row) elements=row;
  if(elements[2]!=3) return 26;
  Box<const int&> box{copied<const int&>(original)};
  original=4;
  if(box.value!=9) return 27;
  Result result=3;
  Extent scalar=4;
  if(result+scalar!=7) return 28;
  if(!all_plain<>()||!all_plain<const int&,int&&>()||all_plain<int,double>()) return 29;
  if(effects!=0) return 30;
  return 0;
}
