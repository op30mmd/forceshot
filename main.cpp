#include "config.h"

#include <windows.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <iostream>
#include <d3d11.h>
#include <dxgi1_2.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "gdiplus.lib")

#define ID_TRAY_APP_ICON 1001
#define ID_TAKE_SCREENSHOT 1002
#define ID_EXIT 1003

// Forward declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void TakeScreenshot();
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);

// Globals
ULONG_PTR gdiplusToken;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    const char CLASS_NAME[] = "ScreenshotAppClass";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        "Screenshot App",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) {
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return 0;
    }

    // Register for Raw Input from the keyboard
    RAWINPUTDEVICE rid;
    rid.usUsagePage = 0x01; // Generic Desktop Controls
    rid.usUsage = 0x06;     // Keyboard
    rid.dwFlags = RIDEV_INPUTSINK; // Receive input even when not in foreground
    rid.hwndTarget = hwnd;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));

    ShowWindow(hwnd, SW_HIDE);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_INPUT: {
            UINT dwSize = 0;
            GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
            LPBYTE lpb = new BYTE[dwSize];
            if (lpb == NULL) {
                return 0;
            }

            if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) != dwSize) {
                delete[] lpb;
                return 0;
            }

            RAWINPUT* raw = (RAWINPUT*)lpb;

            if (raw->header.dwType == RIM_TYPEKEYBOARD) {
                // Check for 'S' key press (virtual key code for 'S' is 0x53)
                if (raw->data.keyboard.VKey == 0x53 && raw->data.keyboard.Message == WM_KEYDOWN) {
                    // Check if Ctrl and Shift are also pressed
                    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
                        TakeScreenshot();
                    }
                }
            }

            delete[] lpb;
            return 0;
        }
        case WM_CREATE: {
            NOTIFYICONDATA nid = {};
            nid.cbSize = sizeof(NOTIFYICONDATA);
            nid.hWnd = hwnd;
            nid.uID = ID_TRAY_APP_ICON;
            nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            nid.uCallbackMessage = WM_USER + 1;
            nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
            lstrcpy(nid.szTip, "Screenshot App");
            Shell_NotifyIcon(NIM_ADD, &nid);
            break;
        }
        case WM_USER + 1: {
            if (lParam == WM_RBUTTONUP) {
                POINT curPoint;
                GetCursorPos(&curPoint);
                HMENU hPopupMenu = CreatePopupMenu();
                InsertMenu(hPopupMenu, 0, MF_BYPOSITION | MF_STRING, ID_TAKE_SCREENSHOT, "Take Screenshot");
                InsertMenu(hPopupMenu, 1, MF_BYPOSITION | MF_STRING, ID_EXIT, "Exit");
                SetForegroundWindow(hwnd);
                TrackPopupMenu(hPopupMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, curPoint.x, curPoint.y, 0, hwnd, NULL);
            }
            break;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case ID_TAKE_SCREENSHOT:
                    TakeScreenshot();
                    break;
                case ID_EXIT:
                    PostQuitMessage(0);
                    break;
            }
            break;
        }
        case WM_DESTROY: {
            NOTIFYICONDATA nid = {};
            nid.cbSize = sizeof(NOTIFYICONDATA);
            nid.hWnd = hwnd;
            nid.uID = ID_TRAY_APP_ICON;
            Shell_NotifyIcon(NIM_DELETE, &nid);
            PostQuitMessage(0);
            break;
        }
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

void TakeScreenshot() {
    HRESULT hr;

    // Declare COM interfaces
    IDXGIFactory1* pFactory = nullptr;
    IDXGIAdapter1* pAdapter = nullptr;
    ID3D11Device* pDevice = nullptr;
    ID3D11DeviceContext* pContext = nullptr;
    IDXGIDevice* pDxgiDevice = nullptr;
    IDXGIAdapter* pDxgiAdapter = nullptr;
    IDXGIOutput* pDxgiOutput = nullptr;
    IDXGIOutput1* pDxgiOutput1 = nullptr;
    IDXGIDesktopDuplication* pDeskDupl = nullptr;
    ID3D11Texture2D* pStagingTexture = nullptr;
    IDXGIResource* pDesktopResource = nullptr;
    ID3D11Texture2D* pDesktopTexture = nullptr;

    // Create DXGI factory
    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)(&pFactory));
    if (FAILED(hr)) goto cleanup;

    // Enumerate adapters
    hr = pFactory->EnumAdapters1(0, &pAdapter);
    if (FAILED(hr)) goto cleanup;

    // Create D3D11 device
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    hr = D3D11CreateDevice(pAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, &featureLevel, 1, D3D11_SDK_VERSION, &pDevice, NULL, &pContext);
    if (FAILED(hr)) goto cleanup;

    // Get DXGI device
    hr = pDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)(&pDxgiDevice));
    if (FAILED(hr)) goto cleanup;

    // Get DXGI adapter
    hr = pDxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**)(&pDxgiAdapter));
    if (FAILED(hr)) goto cleanup;

    // Get DXGI output
    hr = pDxgiAdapter->EnumOutputs(0, &pDxgiOutput);
    if (FAILED(hr)) goto cleanup;

    // Get output 1 interface
    hr = pDxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)(&pDxgiOutput1));
    if (FAILED(hr)) goto cleanup;

    // Create desktop duplication
    hr = pDxgiOutput1->DuplicateOutput(pDevice, &pDeskDupl);
    if (FAILED(hr)) goto cleanup;

    // Get output description for texture size
    DXGI_OUTPUT_DESC desc;
    pDxgiOutput->GetDesc(&desc);

    // Create a texture to hold the screen image
    D3D11_TEXTURE2D_DESC texDesc;
    texDesc.Width = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
    texDesc.Height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_STAGING;
    texDesc.BindFlags = 0;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    texDesc.MiscFlags = 0;
    hr = pDevice->CreateTexture2D(&texDesc, NULL, &pStagingTexture);
    if (FAILED(hr)) goto cleanup;

    // Acquire the next frame
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    hr = pDeskDupl->AcquireNextFrame(500, &frameInfo, &pDesktopResource);
    if (FAILED(hr) || pDesktopResource == nullptr) goto cleanup;

    // Copy the frame to the staging texture
    hr = pDesktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)(&pDesktopTexture));
    if(FAILED(hr)) goto cleanup;

    pContext->CopyResource(pStagingTexture, pDesktopTexture);

    // Map the staging texture to access the raw pixel data
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = pContext->Map(pStagingTexture, 0, D3D11_MAP_READ, 0, &mappedResource);
    if (FAILED(hr)) goto cleanup;

    // Create GDI+ bitmap from the raw data and save to file
    {
        Gdiplus::Bitmap bitmap(texDesc.Width, texDesc.Height, mappedResource.RowPitch, PixelFormat32bppARGB, (BYTE*)mappedResource.pData);
        CLSID pngClsid;
        GetEncoderClsid(L"image/png", &pngClsid);
        bitmap.Save(L"screenshot.png", &pngClsid, NULL);
    }

    // Unmap the texture
    pContext->Unmap(pStagingTexture, 0);

cleanup:
    // Release all COM objects
    if (pDesktopTexture) pDesktopTexture->Release();
    if (pDesktopResource) pDesktopResource->Release();
    if (pStagingTexture) pStagingTexture->Release();
    if (pDeskDupl) {
        // Must release the frame before releasing the duplication interface
        pDeskDupl->ReleaseFrame();
        pDeskDupl->Release();
    }
    if (pDxgiOutput1) pDxgiOutput1->Release();
    if (pDxgiOutput) pDxgiOutput->Release();
    if (pDxgiAdapter) pDxgiAdapter->Release();
    if (pDxgiDevice) pDxgiDevice->Release();
    if (pContext) pContext->Release();
    if (pDevice) pDevice->Release();
    if (pAdapter) pAdapter->Release();
    if (pFactory) pFactory->Release();
}

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;

    Gdiplus::ImageCodecInfo* pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL) return -1;

    GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    free(pImageCodecInfo);
    return -1;
}
