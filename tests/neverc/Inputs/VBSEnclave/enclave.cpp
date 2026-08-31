#include <windows.h>

#ifdef __cplusplus
extern "C"
#endif
    const IMAGE_ENCLAVE_CONFIG __enclave_config = {
        sizeof(IMAGE_ENCLAVE_CONFIG),
        IMAGE_ENCLAVE_MINIMUM_CONFIG_SIZE,
        IMAGE_ENCLAVE_POLICY_DEBUGGABLE,
        0,
        0,
        0,
        {0x91, 0x2d, 0x74, 0x18, 0xb6, 0x53, 0x4c, 0x2a, 0x8e, 0xa4, 0x57, 0x39,
         0xc1, 0x06, 0xfd, 0x22},
        {0x37, 0xa8, 0xc5, 0x40, 0x6f, 0xd1, 0x49, 0xbb, 0x9a, 0x0e, 0xe3, 0x15,
         0x72, 0xbc, 0x48, 0x9d},
        0x00010000,
        1,
        0x20000000ULL,
        1,
        IMAGE_ENCLAVE_FLAG_PRIMARY_IMAGE,
};

#ifdef __cplusplus
extern "C"
#endif
    BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
  return TRUE;
}
