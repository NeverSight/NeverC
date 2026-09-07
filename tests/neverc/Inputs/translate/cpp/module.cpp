namespace computation {
struct Pair { unsigned int first; unsigned int second; };

unsigned int adjust(unsigned int value) { return value + 17u; }
int adjust(int value) { return value - 3; }

unsigned int mix(unsigned int first, unsigned int second) {
  return first * 37u + second;
}

int recurse(int value) {
  if (value == 0)
    return 7;
  return recurse(value - 1) + value;
}
}

extern "C" unsigned int translate_probe(unsigned int first, unsigned int second) {
  computation::Pair source{first, second};
  computation::Pair copied = source;
  copied.first = computation::adjust(copied.first);

  unsigned int index = first & 31u;
  index = index++ + 4u;
  // Assignment RHS is sequenced before LHS evaluation in C++17.
  index += index++;
  unsigned int shifted = index++ << (index & 7u);

  bool gate = (second & 1u) != 0u;
  bool took = gate && (++index != 0u);
  bool skipped = gate || (++index != 0u);
  unsigned int accumulator = copied.first + source.second + shifted;
  for (int outer = 0; outer < 4; ++outer) {
    for (int inner = 0; inner < 4; ++inner) {
      if (inner == 1)
        continue;
      if (outer == 3 && inner == 3)
        break;
      accumulator = accumulator * 33u + (unsigned int)(outer + inner);
    }
  }

  int signed_value = computation::adjust((int)(first & 255u));
  bool promotion = signed_value < second;
  unsigned int conditional = gate ? index++ : ++index;
  return computation::mix(accumulator, index) + (unsigned int)signed_value
      + (unsigned int)took + (unsigned int)skipped + (unsigned int)promotion
      + conditional + (unsigned int)computation::recurse(5);
}

// These implementation-defined results are fixed by the pinned Clang profile.
// The signed left shifts are permitted by C++17's unsigned-representability
// rule; naively printing the same shifts in C would introduce undefined behavior.
extern "C" int translate_signed_boundary(unsigned int selector) {
  int one = 1;
  int three = 3;
  int negative = -7;
  if (selector == 0u)
    return one << 31;
  if (selector == 1u)
    return three << 30;
  if (selector == 2u)
    return negative >> 1;
  if (selector == 3u)
    return (int)4294967295u;
  if (selector == 4u)
    return (int)2147483648u;
  return 0;
}
