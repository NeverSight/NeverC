unsigned int combine(unsigned int first, unsigned int second) {
  return first * 10u + second;
}

extern "C" unsigned int translate_order() {
  unsigned int index = 1u;
  return combine(index++, index++);
}
