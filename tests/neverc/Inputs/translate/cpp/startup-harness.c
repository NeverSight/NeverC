extern int startup_state(void);
extern int startup_calls(void);
int main(void) {
  if (startup_calls() != 4) return 1;
  if (!startup_state() || !startup_state() || startup_calls() != 4) return 2;
  return 0;
}
