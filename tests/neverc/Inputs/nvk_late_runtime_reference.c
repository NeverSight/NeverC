#include <nvkmod.h>

extern int plugin_late_nvk_runtime(void);

static int m_init(void) { return plugin_late_nvk_runtime(); }
static void m_exit(void) {}

module_init(m_init);
module_exit(m_exit);
MODULE_LICENSE("GPL v2");
NEVERC_KRT_DEFINE_MODULE("neverc_test_late_runtime");
