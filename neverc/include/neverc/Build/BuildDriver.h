#ifndef NEVERC_BUILD_BUILDDRIVER_H
#define NEVERC_BUILD_BUILDDRIVER_H

namespace neverc {
namespace build {

int runBuild(int Argc, const char **Argv, const char *Argv0,
             const char *PrependArg = nullptr);

} // namespace build
} // namespace neverc

#endif // NEVERC_BUILD_BUILDDRIVER_H
