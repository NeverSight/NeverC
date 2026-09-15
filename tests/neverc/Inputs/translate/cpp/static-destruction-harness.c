#include <stdlib.h>
extern void start_first(void);
extern void start_rest(void);
extern void event(int);
extern int event_count(void);
extern int event_at(int);
static void middle(void) { event(3); }
static void verify(void) {
 const int expected[] = {17, 21, 22, 21, 23, 25, 21, 23, 25,
                         26, 24, 22, 26, 24, 22, 27,
                         20, 19, 18, 15, 16, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 2, 3, 1};
 if (event_count() != sizeof(expected) / sizeof(expected[0])) _Exit(91);
 for (int n = 0; n < event_count(); ++n)
  if (event_at(n) != expected[n]) _Exit(100 + n);
}
int main(void) {
 if (atexit(verify)) return 1;
 start_first();
 if (atexit(middle)) return 2;
 start_rest();
 if (event_count() != 9 || event_at(0) != 17 || event_at(1) != 21 ||
     event_at(2) != 22 || event_at(3) != 21 || event_at(4) != 23 ||
     event_at(5) != 25 || event_at(6) != 21 || event_at(7) != 23 ||
     event_at(8) != 25) return 3;
 return 0;
}
