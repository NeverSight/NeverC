// REQUIRES: system-windows
// RUN: %neverc --target=x86_64-windows-msvc -fsyntax-only %s
#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    MessageBoxA(NULL, "Hello from NeverC!", "NeverC Cross-Compile Test", MB_OK | MB_ICONINFORMATION);
    return 0;
}
