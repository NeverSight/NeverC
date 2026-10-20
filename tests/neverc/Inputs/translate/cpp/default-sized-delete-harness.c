extern int default_sized_delete_check(void);
extern void *default_sized_delete_observed[2];
static void *volatile observed_cookie_addresses[2];
int main(void) {
  int result = default_sized_delete_check();
  observed_cookie_addresses[0] = default_sized_delete_observed[0];
  observed_cookie_addresses[1] = default_sized_delete_observed[1];
  return result;
}
