// Main TU used by the --override=_compute scenario where the two plain
// `compute` definitions disagree and the link order picks the b-variant
// (returns 99) as the last definition.
extern int compute(void);
int main(void) { return compute() == 99 ? 0 : 1; }
