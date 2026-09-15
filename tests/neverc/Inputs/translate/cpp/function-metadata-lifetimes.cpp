// Names must survive function lowering until the complete response is written.
static int internal_function_with_a_name_longer_than_small_string_storage(int n) {
  return n + 7;
}
extern "C" int exported_integer_function_with_persistent_metadata(int n) {
  return internal_function_with_a_name_longer_than_small_string_storage(n);
}
extern "C" unsigned int exported_unsigned_function_with_persistent_metadata(unsigned int n) {
  return n + 1u;
}
extern "C" bool exported_boolean_function_with_persistent_metadata(bool n) {
  return !n;
}
extern "C" void exported_void_function_with_persistent_metadata() {}
int main() {
  exported_void_function_with_persistent_metadata();
  return exported_integer_function_with_persistent_metadata(3) != 10 ||
         exported_unsigned_function_with_persistent_metadata(8u) != 9u ||
         !exported_boolean_function_with_persistent_metadata(false);
}
