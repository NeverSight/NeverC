int constructions, destructions;
struct R {
 int n; const R *self;
 R(int value) : n(value), self(this) { ++constructions; }
 ~R() { ++destructions; n = -1; }
};
const R &root() { static const R value(41); return value; }
const R &extended() { static const R &value = R(51); return value; }
extern "C" int read_value() {
 const R &a = root(); const R &b = extended();
 return a.n != 41 || b.n != 51 || a.self != &a || b.self != &b;
}
extern "C" int initialization_count() { return constructions; }
extern "C" int destruction_count() { return destructions; }
