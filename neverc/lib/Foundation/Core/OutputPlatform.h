#ifndef NEVERC_LIB_FOUNDATION_CORE_OUTPUTPLATFORM_H
#define NEVERC_LIB_FOUNDATION_CORE_OUTPUTPLATFORM_H

#include "llvm/ADT/StringRef.h"
#include <system_error>

namespace neverc::output_platform {

std::error_code syncFileDescriptor(int FileDescriptor);
std::error_code syncParentDirectory(llvm::StringRef Path);

} // namespace neverc::output_platform

#endif
