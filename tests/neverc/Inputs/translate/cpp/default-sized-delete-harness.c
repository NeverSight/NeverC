// Keep allocation addresses observable to the separately compiled C client.
void *volatile default_sized_delete_observed;
void default_sized_delete_observe(void *pointer) { default_sized_delete_observed = pointer; }
extern int default_sized_delete_check(void);
int main(void) { return default_sized_delete_check(); }
