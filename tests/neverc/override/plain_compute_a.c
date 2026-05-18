// Plain `compute` definition with no override attribute. Paired with
// `plain_compute_b.c` to exercise: (1) the failure mode when two strong
// definitions collide without --override, and (2) the success mode when
// the linker flag `--override=_compute` allows them to coexist.
int compute(void) { return 1; }
