int events[64]{};
int count;
void mark(int n) { events[count++] = n; }
struct Item {
 int n;
 Item(int value) : n(value) {}
 ~Item() { mark(n); n = -1; }
};
void first() { static const Item value(1); }
void second() { static Item value(2); }
void nested() { static Item value(5); }
struct Argument { ~Argument() { nested(); } };
struct Root { Root(const Argument&) {} ~Root() { mark(4); } };
void full_expression() { static Root value{Argument{}}; }
int middle() { static Item value(7); return 0; }
struct Parent { const Item &child; int n; ~Parent() { mark(8); } };
void extended() { static const Parent value{Item(6), middle()}; }
struct Bundle { Item a; Item b[2]; ~Bundle() { mark(12); } };
void array_members() { static const Bundle value{Item(9), {Item(10), Item(11)}}; }
struct Constant { int n; ~Constant() { mark(n); n = -1; } };
void constant() { static const Constant value{13}; }
void selected(bool b) { static const Item &value = b ? Item(14) : Item(99); }
void never_called() { static Item value(98); }
void created_at_exit() { static Item value(16); }
struct Reentrant { ~Reentrant() { mark(15); created_at_exit(); } };
void reentrant() { static Reentrant value; }
using Size = decltype(sizeof(0));
struct PlacementTag {};
void *operator new(Size, PlacementTag, void *p) { return p; }
void reuse() { static Item value(17); value.~Item(); new(PlacementTag{}, &value) Item(18); }
template<int N> const Constant &templated() { static const Constant value{N}; return value; }
extern "C" void start_first() { first(); }
extern "C" void start_rest() {
 second(); full_expression(); extended(); array_members(); constant();
 selected(true); selected(false); reentrant(); reuse();
 templated<19>(); templated<19>(); templated<20>();
}
extern "C" void event(int n) { mark(n); }
extern "C" int event_count() { return count; }
extern "C" int event_at(int n) { return events[n]; }
