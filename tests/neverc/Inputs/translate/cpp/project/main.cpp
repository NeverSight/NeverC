#include "compute.hpp"
int main() {
  return translate_project(17u) == ((486u * 5u) ^ (17u ^ 165u)) ? 0 : 1;
}
