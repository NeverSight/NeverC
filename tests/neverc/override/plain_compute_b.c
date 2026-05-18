// Second plain `compute` definition; see plain_compute_a.c for the test
// rationale. Returns a distinct value so that the "last definition wins"
// behavior of `--override` can be observed at runtime.
int compute(void) { return 99; }
