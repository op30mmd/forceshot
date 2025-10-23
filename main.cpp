// Minimal test case to diagnose the build environment.

#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <dxgi1_2.h>
#include <iostream>

#pragma comment(lib, "dxgi.lib")

int main() {
    IDXGIDesktopDuplication* pDeskDupl = nullptr;
    if (pDeskDupl == nullptr) {
        std::cout << "Compilation test successful." << std::endl;
    }
    return 0;
}
