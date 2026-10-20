#include "Linker/Core/Driver/Dispatcher.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
#include <string>
#include <unistd.h>
#include <vector>

LINKER_HAS_DRIVER(macho)

int main(int Argc, char **Argv) {
  llvm::InitLLVM Init(Argc, Argv);
  if (Argc != 6)
    return 64;
  const std::string Mode = Argv[1];
  const std::string Policy = Argv[2];
  if ((Mode != "preload" && Mode != "dylinker" && Mode != "kext") ||
      (Policy != "default" && Policy != "data_const" && Policy != "no_data_const"))
    return 65;
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllAsmParsers();
  linker::LinkerDriverConfig Config;
  Config.archName = "arm64";
  Config.platformName = "macos";
  Config.platformMinVersion = "13.0.0";
  Config.platformSdkVersion = "13.0.0";
  Config.nostdlib = true;
  Config.bundle = Mode == "kext";
  Config.outputFile = Argv[4];
  std::vector<std::string> Storage{"actual-macho-image-backend", "-" + Mode,
                                   Argv[3], "-o", Argv[4]};
  if (Mode != "kext") {
    Storage.push_back("-e");
    Storage.push_back("_start");
  }
  if (Policy != "default")
    Storage.push_back("-" + Policy);
  if (Mode == "kext") {
    Storage.push_back("-kext_objects_dir");
    Storage.push_back(Argv[5]);
  }
  std::vector<const char *> Arguments;
  for (const auto &Value : Storage)
    Arguments.push_back(Value.c_str());
  ::alarm(60);
  return linker::macho::link(Arguments, llvm::outs(), llvm::errs(), false,
                              false, Config) ? 0 : 1;
}
