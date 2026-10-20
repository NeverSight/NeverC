#include "Linker/Core/Runtime/Session.h"
#include <cstdio>

int main() {
  linker::CommonLinkerContext Context(neverc::ResourceSessionView{});
  auto Offset = [&](const auto &Member) {
    return reinterpret_cast<const unsigned char *>(&Member) -
           reinterpret_cast<const unsigned char *>(&Context);
  };
  std::printf("{\"size\":%zu,\"alignment\":%zu,\"guard_size\":%zu,"
              "\"guard_alignment\":%zu,\"backendHotState\":%zu,\"bAlloc\":%zu,"
              "\"saver\":%zu,\"instances\":%zu,\"instanceOrder\":%zu,"
              "\"instanceSequence\":%zu,\"allocatorSequence\":%zu,\"e\":%zu}\n",
              sizeof(Context), alignof(linker::CommonLinkerContext),
              sizeof(linker::LinkerContextGuard), alignof(linker::LinkerContextGuard),
              size_t(Offset(Context.backendHotState)), size_t(Offset(Context.bAlloc)),
              size_t(Offset(Context.saver)), size_t(Offset(Context.instances)),
              size_t(Offset(Context.instanceOrder)), size_t(Offset(Context.instanceSequence)),
              size_t(Offset(Context.allocatorSequence)), size_t(Offset(Context.e)));
}
