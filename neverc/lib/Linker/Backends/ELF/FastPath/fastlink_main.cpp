//===----------------------------------------------------------------------===//
//
//  Standalone driver for the FastLink pipeline, used for development and
//  benchmarking. It accepts the GNU-style options of the links FastLink
//  handles. It is not part of the NeverC build.
//
//===----------------------------------------------------------------------===//

#include "FastLink.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>

using std::string;
using std::vector;

namespace {

[[noreturn]] void die(const string &msg) {
  fprintf(stderr, "fastlink: error: %s\n", msg.c_str());
  exit(1);
}

fastlink::Request parseArgs(int argc, char **argv) {
  fastlink::Request req;
  vector<string> args;
  for (int i = 1; i < argc; ++i) {
    string a = argv[i];
    if (a.size() > 1 && a[0] == '@') {
      FILE *f = fopen(a.c_str() + 1, "r");
      if (!f)
        die("cannot open " + a.substr(1));
      char buf[4096];
      while (fscanf(f, "%4095s", buf) == 1)
        args.push_back(buf);
      fclose(f);
    } else {
      args.push_back(a);
    }
  }
  struct State {
    bool whole = false, asNeeded = false;
  } st;
  vector<State> stack;
  auto value = [&](size_t &i, const string &a, const string &opt) {
    if (a.size() > opt.size() && a[opt.size()] == '=')
      return a.substr(opt.size() + 1);
    if (++i >= args.size())
      die("missing argument to " + opt);
    return args[i];
  };
  for (size_t i = 0; i < args.size(); ++i) {
    const string &a = args[i];
    if (a == "-o")
      req.output = args[++i];
    else if (a.rfind("-L", 0) == 0)
      req.libPaths.push_back(a.size() > 2 ? a.substr(2) : args[++i]);
    else if (a.rfind("-l", 0) == 0) {
      string n = a.size() > 2 ? a.substr(2) : args[++i];
      req.inputs.push_back({n, true, st.whole, st.asNeeded});
    } else if (a == "--whole-archive")
      st.whole = true;
    else if (a == "--no-whole-archive")
      st.whole = false;
    else if (a == "--as-needed")
      st.asNeeded = true;
    else if (a == "--no-as-needed")
      st.asNeeded = false;
    else if (a == "--push-state")
      stack.push_back(st);
    else if (a == "--pop-state") {
      if (!stack.empty()) {
        st = stack.back();
        stack.pop_back();
      }
    } else if (a == "-pie" || a == "--pie")
      req.pie = true;
    else if (a == "-no-pie")
      req.pie = false;
    else if (a == "--gc-sections")
      req.gcSections = true;
    else if (a == "--eh-frame-hdr")
      req.ehFrameHdr = true;
    else if (a == "--build-id" || a == "--build-id=sha1" || a == "--build-id=tree")
      req.buildIdSize = 20;
    else if (a == "-shared" || a == "--shared") {
      req.shared = true;
      req.allowUndefined = true;
    } else if (a == "-soname" || a == "-h" || a.rfind("--soname", 0) == 0)
      req.soname = value(i, a, a.rfind("--", 0) == 0 ? "--soname" : a);
    else if (a == "-Bsymbolic" || a == "--Bsymbolic")
      req.bsymbolic = 1;
    else if (a == "-Bsymbolic-functions" || a == "--Bsymbolic-functions")
      req.bsymbolic = 2;
    else if (a == "--no-undefined")
      req.allowUndefined = false;
    else if (a == "-E" || a == "--export-dynamic")
      req.exportDynamic = true;
    else if (a == "-s" || a == "--strip-all")
      req.stripSymbols = req.stripDebug = true;
    else if (a == "-S" || a == "--strip-debug")
      req.stripDebug = true;
    else if (a == "--icf=safe")
      req.icf = 1;
    else if (a == "--icf=all")
      req.icf = 2;
    else if (a == "--icf=none")
      req.icf = 0;
    else if (a == "--build-id=fast")
      req.buildIdSize = 8;
    else if (a == "--build-id=md5")
      req.buildIdSize = 16;
    else if (a == "--build-id=none")
      req.buildIdSize = 0;
    else if (a.rfind("--build-id=0x", 0) == 0) {
      string hex = a.substr(13);
      req.buildIdBytes.clear();
      for (size_t k = 0; k + 1 < hex.size(); k += 2)
        req.buildIdBytes.push_back(strtoul(hex.substr(k, 2).c_str(), nullptr, 16));
    }
    else if (a == "-z") {
      string z = args[++i];
      if (z == "now")
        req.zNow = true;
      else if (z == "defs")
        req.allowUndefined = false;
      else if (z == "relro")
        req.zRelro = true;
    } else if (a == "-dynamic-linker" || a.rfind("--dynamic-linker", 0) == 0)
      req.dynamicLinker = value(i, a, a.rfind("--", 0) == 0
                                              ? "--dynamic-linker"
                                              : "-dynamic-linker");
    else if (a == "-rpath" || a.rfind("--rpath", 0) == 0)
      req.rpaths.push_back(
          value(i, a, a.rfind("--", 0) == 0 ? "--rpath" : "-rpath"));
    else if (a.rfind("--threads", 0) == 0)
      req.threads = atoi(value(i, a, "--threads").c_str());
    else if (a == "--time")
      req.timing = true;
    else if (a == "-m" || a == "--hash-style")
      ++i;
    else if (a.rfind("--hash-style", 0) == 0 || a.rfind("--color", 0) == 0 ||
             a == "--eh-frame-hdr" || a == "--no-fork" || a == "--fork")
      ;
    else if (a[0] == '-')
      fprintf(stderr, "fastlink: warning: ignoring option %s\n", a.c_str());
    else
      req.inputs.push_back({a, false, st.whole, st.asNeeded});
  }
  return req;
}

} // namespace

int main(int argc, char **argv) {
  fastlink::Request req = parseArgs(argc, argv);
  string reason;
  if (fastlink::link(req, reason) != fastlink::Status::Linked)
    die("declined: " + reason);
  fflush(stderr);
  _exit(0);
}
