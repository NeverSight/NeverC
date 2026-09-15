using Observer = void (*)(int);
Observer observe = nullptr;
int constructed;
void mark(int n) { if (observe) observe(n); }
struct Item {
 int n;
 Item(int value) : n(value) { ++constructed; }
 ~Item() { mark(n); n = -1; }
};
struct Constant { int n; ~Constant() { mark(n); n = -1; } };
extern const Constant constant;
int read_constant() { return constant.n; }
int early = read_constant();
const Constant constant{21};
const Item root(22);
const Item &extended = Item(23);
const Item array[2] = {Item(24), Item(25)};
int nested() { static Item local(27); return 0; }
struct Parent { const Item &child; int n; ~Parent() { mark(28); } };
const Parent parent{Item(26), nested()};
void after_load() { static const Constant local{29}; }
extern "C" int lifetime_start(Observer callback) {
 observe = callback;
 after_load(); after_load();
 return early == 21 && constant.n == 21 && root.n == 22 && extended.n == 23 &&
        array[0].n == 24 && array[1].n == 25 && parent.child.n == 26 && constructed == 6;
}
