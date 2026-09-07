#ifndef NEVERC_TRANSLATE_FRONTENDPROTOCOL_H
#define NEVERC_TRANSLATE_FRONTENDPROTOCOL_H

#include <cstddef>
#include <cstdint>

namespace neverc::translate {
inline constexpr uint32_t FrontendProtocolMajor = 1;
inline constexpr const char *CppFrontendName = "neverc-cpp-frontend";
inline constexpr const char *CppFrontendVersion = "20.1.8";
inline constexpr std::size_t MaxFrontendResponseBytes = 32u * 1024u * 1024u;
inline constexpr std::size_t MaxProtocolDepth = 64;
inline constexpr std::size_t MaxProtocolNodes = 200000;
inline constexpr std::size_t MaxProtocolIdentifierBytes = 256;
} // namespace neverc::translate

#endif
