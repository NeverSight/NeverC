using Size = decltype(sizeof(0));
int live = 0, sequence = 0, events[16]{}, count = 0;
struct Object {
  int value;
  Object(int n = ++sequence) : value(n) { ++live; }
  ~Object() { events[count++] = value; --live; }
};
int* startupValue = new int(61);
Object* create() { return new Object(7); }
int main() {
  if (!startupValue || *startupValue != 61) return 9;
  delete startupValue; startupValue = nullptr;
  int* scalar = new int(19);
  if (!scalar || *scalar != 19) return 1;
  delete scalar;
  int* first = new int[0];
  int* second = new int[0];
  if (!first || !second || first == second) return 2;
  delete[] first; delete[] second;
  constexpr int extent = 5;
  int* values = new int[extent]{};
  for (int i = 0; i < extent; ++i) if (values[i]) return 3;
  values[4] = 23;
  if (values[4] != 23) return 4;
  delete[] values;
  Object* object = create();
  if (live != 1 || object->value != 7) return 5;
  delete object;
  Object* array = new Object[3];
  if (live != 3 || array[0].value != 1 || array[2].value != 3) return 6;
  delete[] array;
  if (live || count != 4 || events[0] != 7 || events[1] != 3 || events[2] != 2 || events[3] != 1) return 7;
  int* null = nullptr; delete null; delete[] null;
  return 0;
}
