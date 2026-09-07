// The host NeverC instance owns the process's Darwin crash annotation.
// Applied only to the private LLVM PrettyStackTrace.cpp translation unit.
#include "llvm/Config/config.h"
#undef HAVE_CRASHREPORTER_INFO
#define HAVE_CRASHREPORTER_INFO 0
#undef HAVE_CRASHREPORTERCLIENT_H
