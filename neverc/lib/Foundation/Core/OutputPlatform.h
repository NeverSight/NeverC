#ifndef NEVERC_LIB_FOUNDATION_CORE_OUTPUTPLATFORM_H
#define NEVERC_LIB_FOUNDATION_CORE_OUTPUTPLATFORM_H

#include "llvm/ADT/StringRef.h"
#include <system_error>

namespace neverc::output_platform {

std::error_code syncFileDescriptor(int FileDescriptor);
std::error_code syncParentDirectory(llvm::StringRef Path);
std::error_code seekFileToEnd(int FileDescriptor);
std::error_code closeFileDescriptor(int FileDescriptor);
std::error_code isLinkLikePath(llvm::StringRef Path, bool &Result);
std::error_code renameLinkLikePath(llvm::StringRef From, llvm::StringRef To);
std::error_code renameStagingFile(llvm::StringRef From, int FileDescriptor,
                                  bool &DeleteDispositionActive,
                                  llvm::StringRef To);
std::error_code restrictFileToOwner(llvm::StringRef Path, int FileDescriptor,
                                    bool &DeleteDispositionActive);
std::error_code applyBackupFilePermissions(llvm::StringRef SourcePath,
                                           llvm::StringRef DestinationPath,
                                           int DestinationFileDescriptor,
                                           unsigned PosixPermissions);

} // namespace neverc::output_platform

#endif
