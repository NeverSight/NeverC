#ifndef NEVERC_LIB_SHELLCODE_EXTRACTORCOMMON_H
#define NEVERC_LIB_SHELLCODE_EXTRACTORCOMMON_H

#include "neverc/Shellcode/Pipeline/ShellcodeOptions.h"
#include "neverc/Shellcode/Pipeline/TargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <string>

namespace neverc {
namespace shellcode {

int extractMachO(llvm::StringRef InputObj, llvm::StringRef OutputBin,
                 const ShellcodeOptions &Opts);

int extractELF(llvm::StringRef InputObj, llvm::StringRef OutputBin,
               const ShellcodeOptions &Opts);

int extractCOFF(llvm::StringRef InputObj, llvm::StringRef OutputBin,
                const ShellcodeOptions &Opts);

bool patchArm64Branch26(llvm::MutableArrayRef<uint8_t> Text, uint64_t Off,
                        int64_t PCDisp);

bool patchArm64Page21(llvm::MutableArrayRef<uint8_t> Text, uint64_t Off,
                      int64_t SymAddr, uint64_t SiteAddr);

bool patchArm64Lo12WithShift(llvm::MutableArrayRef<uint8_t> Text, uint64_t Off,
                             uint64_t TargetAddr, unsigned Shift);

bool patchArm64Lo12AutoShift(llvm::MutableArrayRef<uint8_t> Text, uint64_t Off,
                             uint64_t TargetAddr, bool IsLdSt);

bool patchRel32(llvm::MutableArrayRef<uint8_t> Text, uint64_t Off,
                int64_t PCDisp);

bool patchRel64(llvm::MutableArrayRef<uint8_t> Text, uint64_t Off,
                int64_t PCDisp);

llvm::StringRef stripLeadingUnderscore(llvm::StringRef S);

bool isShellcodeInternalRuntimeName(llvm::StringRef Name);

bool isDefaultEntryName(llvm::StringRef Bare);

std::string defaultEntryNameList();

bool isShellcodeEntryCandidate(llvm::StringRef Name, llvm::StringRef UserEntry);

void printExternHint(llvm::raw_ostream &Os, const TargetDesc &Target,
                     llvm::StringRef Name);

struct ExternalSymbolHintContext {
  bool FunctionHasGeneralRegsOnly = false;
};

std::string getExternalSymbolHint(
    llvm::StringRef Name, const TargetDesc &Target,
    ExternalSymbolHintContext Context = ExternalSymbolHintContext());

bool isTextSection(const TargetDesc &Target, llvm::StringRef Name);

bool isForbiddenDataSection(const TargetDesc &Target, llvm::StringRef Name);

bool auditFinalBadBytes(llvm::ArrayRef<uint8_t> Bytes,
                        const ShellcodeOptions &Opts);

int finalizeShellcodeBytes(llvm::SmallVectorImpl<uint8_t> &Bytes,
                           const ShellcodeOptions &Opts);

llvm::StringRef lookupKernelHelperOS(llvm::StringRef Bare);

bool isReservedMemStdlibName(llvm::StringRef Bare);
bool isLibmTranscendentalName(llvm::StringRef Bare);
bool isStdioCallName(llvm::StringRef Bare);
bool isHeapAllocatorName(llvm::StringRef Bare);
bool isBuiltinStringRuntimeName(llvm::StringRef Bare);
bool isSetjmpName(llvm::StringRef Bare);
bool isScalarSoftFloatHelperName(llvm::StringRef Bare);
bool isLongIntegerCompilerRtHelperName(llvm::StringRef Bare);
bool isBinary128HelperName(llvm::StringRef Bare);
bool isCompilerRtRuntimeHelperName(llvm::StringRef Bare);

} // namespace shellcode
} // namespace neverc

#endif
