static int constant_condition(void) {
  if ((bool)0ull)
    goto then_branch;
  else
    goto else_branch;

then_branch:
  return 1;
else_branch:
  return 0;
}

static int jump_into_skipped_branch(bool jump) {
  if (jump)
    goto target;
  if (false) {
  target:
    return 2;
  } else {
    return 3;
  }
}

int main(void) {
  if (constant_condition() != 0)
    return 1;
  if (jump_into_skipped_branch(false) != 3)
    return 2;
  return jump_into_skipped_branch(true) != 2 ? 3 : 0;
}
