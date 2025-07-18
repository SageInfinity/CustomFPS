#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <gdiplus.h>
#include <commctrl.h>
#include <vector>
#include <numeric>
#include <olectl.h>
#include "resource.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "Ole32.lib")

#define IDC_CLOSE_BUTTON 101
#define IDC_LOGO_STATIC 102
#define IDC_START_BUTTON 104
#define IDC_SAGE_TEXT 105
#define IDC_RESOLUTION_LABEL 106
#define IDC_FPS_LABEL 107
#define IDC_GPU_COMBO 108 
#define IDC_CLOSE_SETTINGS_BUTTON 109 // New ID for the close button on the settings screen

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pRenderTargetView = nullptr;
IDXGIAdapter* g_pSelectedAdapter = nullptr;
std::vector<IDXGIAdapter*> g_vAdapters;


HWND g_hRenderWnd;
HWND g_hInputContainer, g_hCloseButton, g_hLogo;
HWND g_hWidthEdit, g_hHeightEdit, g_hFpsEdit;
HFONT g_hUiFont = nullptr;
HFONT g_hAuthorFont = nullptr;
HWND g_hGpuCombo = nullptr;

ULONG_PTR g_gdiplusToken;
Gdiplus::Bitmap* g_pLogoBitmap = nullptr;
WNDPROC g_pOriginalEditProc = nullptr;
HBRUSH g_hbrGlow = nullptr;
HBRUSH g_hbrWhite = nullptr;

int g_currentWidth = 800;
int g_currentHeight = 600;
int g_targetFPS = 60;
bool g_resizeRequested = false;
bool g_settingsConfirmed = false;

void InitRenderWindow(HINSTANCE hInstance);
LRESULT CALLBACK RenderWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void UpdateUIPositions();
void InitD3D();
void RenderFrame();
void CleanupD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
void UpdateWindowSize(int width, int height);

void InitInputWindow(HINSTANCE hInstance);
LRESULT CALLBACK InputWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool LoadLogoImage();
LRESULT CALLBACK EditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void EnumerateAdapters();


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);

    if (!LoadLogoImage()) {
        MessageBox(NULL, L"Could not load logo from resources.", L"Error", MB_ICONERROR | MB_OK);
        return 1;
    }
    EnumerateAdapters();
    InitInputWindow(hInstance);
    MSG msg = { 0 };

    while (!g_settingsConfirmed && GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (!g_settingsConfirmed)
    {
        if (g_pLogoBitmap) delete g_pLogoBitmap;
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 0;
    }

    InitRenderWindow(hInstance);
    InitD3D();

    LARGE_INTEGER frequency, startTime, currentTime;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&startTime);

    bool bGotMsg;
    while (msg.message != WM_QUIT)
    {
        bGotMsg = PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE);

        if (bGotMsg)
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            if (g_resizeRequested)
            {
                UpdateWindowSize(g_currentWidth, g_currentHeight);
                g_resizeRequested = false;
            }

            QueryPerformanceCounter(&currentTime);
            double elapsed = static_cast<double>(currentTime.QuadPart - startTime.QuadPart) / frequency.QuadPart;
            double targetFrameTime = 1.0 / g_targetFPS;

            if (elapsed >= targetFrameTime)
            {
                RenderFrame();
                startTime = currentTime;
            }
        }
    }


    CleanupD3D();
    if (g_pLogoBitmap) delete g_pLogoBitmap;
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    UnregisterClass(L"D3DRenderWindowClass", hInstance);
    UnregisterClass(L"SageInputWindow", hInstance);
    for (auto& adapter : g_vAdapters) {
        adapter->Release();
    }
    g_vAdapters.clear();

    return 0;
}
void EnumerateAdapters()
{
    IDXGIFactory* pFactory = nullptr;
    CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory);

    IDXGIAdapter* pAdapter;
    for (UINT i = 0; pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        g_vAdapters.push_back(pAdapter);
    }

    pFactory->Release();
}

void InitInputWindow(HINSTANCE hInstance) {
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, InputWndProc, 0, 0, hInstance, nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"SageInputWindow", nullptr };
    RegisterClassEx(&wc);

    HWND hInputWnd = CreateWindowEx(
        WS_EX_LAYERED,
        wc.lpszClassName, L"Settings", WS_POPUP | WS_VISIBLE,
        (GetSystemMetrics(SM_CXSCREEN) - 400) / 2, (GetSystemMetrics(SM_CYSCREEN) - 600) / 2,
        400, 600, nullptr, nullptr, hInstance, nullptr
    );

    SetLayeredWindowAttributes(hInputWnd, 0, 255, LWA_ALPHA);
    HRGN hRgn = CreateRoundRectRgn(0, 0, 400, 600, 20, 20);
    SetWindowRgn(hInputWnd, hRgn, TRUE);

    ShowWindow(hInputWnd, SW_SHOW);
    UpdateWindow(hInputWnd);
}

LRESULT CALLBACK InputWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HFONT hTitleFont, hSageFont_Input;
    static HWND hWidthEdit_Input, hHeightEdit_Input, hFpsEdit_Input;

    switch (msg) {
    case WM_CREATE: {
        hTitleFont = CreateFontA(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
        hSageFont_Input = CreateFontA(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
        g_hUiFont = CreateFontA(30, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

        g_hbrGlow = CreateSolidBrush(RGB(240, 248, 255));
        g_hbrWhite = CreateSolidBrush(RGB(255, 255, 255));

        HINSTANCE hInstance = (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE);

        CreateWindowA("STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 150, 15, 100, 100, hWnd, (HMENU)IDC_LOGO_STATIC, hInstance, nullptr);

    
        g_hGpuCombo = CreateWindowA("COMBOBOX", "", CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_OVERLAPPED | WS_VISIBLE, 50, 135, 300, 250, hWnd, (HMENU)IDC_GPU_COMBO, hInstance, nullptr);

        for (const auto& adapter : g_vAdapters)
        {
            DXGI_ADAPTER_DESC desc;
            adapter->GetDesc(&desc);
            SendMessage(g_hGpuCombo, (UINT)CB_ADDSTRING, (WPARAM)0, (LPARAM)desc.Description);
        }
        SendMessage(g_hGpuCombo, CB_SETCURSEL, (WPARAM)0, (LPARAM)0);


        CreateWindowA("STATIC", "Resolution (W x H)", WS_CHILD | WS_VISIBLE | SS_CENTER, 75, 200, 250, 35, hWnd, (HMENU)IDC_RESOLUTION_LABEL, hInstance, nullptr);
        hWidthEdit_Input = CreateWindowA("EDIT", "800", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_CENTER, 100, 240, 80, 40, hWnd, nullptr, hInstance, nullptr);
        hHeightEdit_Input = CreateWindowA("EDIT", "600", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_CENTER, 220, 240, 80, 40, hWnd, nullptr, hInstance, nullptr);
        CreateWindowA("STATIC", "Target FPS", WS_CHILD | WS_VISIBLE | SS_CENTER, 75, 300, 250, 35, hWnd, (HMENU)IDC_FPS_LABEL, hInstance, nullptr);
        hFpsEdit_Input = CreateWindowA("EDIT", "60", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_CENTER, 150, 340, 100, 40, hWnd, nullptr, hInstance, nullptr);

        HWND hStartButton = CreateWindowA("BUTTON", "Start Render", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 125, 415, 150, 50, hWnd, (HMENU)IDC_START_BUTTON, hInstance, nullptr);

        HWND hCloseSettingsButton = CreateWindowA("BUTTON", "Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 125, 475, 150, 40, hWnd, (HMENU)IDC_CLOSE_SETTINGS_BUTTON, hInstance, nullptr);

    
        HWND hSageText = CreateWindowA("STATIC", "- by Sage", WS_CHILD | WS_VISIBLE | SS_CENTER, 150, 540, 100, 25, hWnd, (HMENU)IDC_SAGE_TEXT, hInstance, nullptr);


        EnumChildWindows(hWnd, [](HWND child, LPARAM font) -> BOOL { SendMessage(child, WM_SETFONT, (WPARAM)font, TRUE); return TRUE; }, (LPARAM)g_hUiFont);

        
        SendMessage(hStartButton, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
        SendMessage(hCloseSettingsButton, WM_SETFONT, (WPARAM)hTitleFont, TRUE); // Use same font as start button for consistency
        SendMessage(hSageText, WM_SETFONT, (WPARAM)hSageFont_Input, TRUE);
        SendMessage(g_hGpuCombo, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);


        g_pOriginalEditProc = (WNDPROC)SetWindowLongPtr(hWidthEdit_Input, GWLP_WNDPROC, (LONG_PTR)EditProc);
        SetWindowLongPtr(hHeightEdit_Input, GWLP_WNDPROC, (LONG_PTR)EditProc);
        SetWindowLongPtr(hFpsEdit_Input, GWLP_WNDPROC, (LONG_PTR)EditProc);
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        Gdiplus::Graphics graphics(hdc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        Gdiplus::Rect rc(0, 0, 400, 600);
        Gdiplus::LinearGradientBrush linGrBrush(rc, Gdiplus::Color(255, 33, 150, 243), Gdiplus::Color(255, 13, 71, 161), Gdiplus::LinearGradientModeVertical);
        graphics.FillRectangle(&linGrBrush, rc);

        EndPaint(hWnd, &ps);
        return 1;
    }
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProc(hWnd, msg, wParam, lParam);
        if (hit == HTCLIENT) return HTCAPTION;
        return hit;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
        if (pdis->CtlID == IDC_LOGO_STATIC && g_pLogoBitmap) {
            Gdiplus::Graphics graphics(pdis->hDC);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.DrawImage(g_pLogoBitmap, 0, 0, pdis->rcItem.right, pdis->rcItem.bottom);
        }
        return TRUE;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        SetBkMode(hdcStatic, TRANSPARENT);

        int ctrlId = GetDlgCtrlID((HWND)lParam);
        if (ctrlId == IDC_SAGE_TEXT || ctrlId == IDC_RESOLUTION_LABEL || ctrlId == IDC_FPS_LABEL) {
            SetTextColor(hdcStatic, RGB(255, 255, 255));
        }

        return (LRESULT)GetStockObject(NULL_BRUSH);
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == IDC_START_BUTTON) {
            char widthBuf[32], heightBuf[32], fpsBuf[32];
            GetWindowTextA(hWidthEdit_Input, widthBuf, sizeof(widthBuf));
            GetWindowTextA(hHeightEdit_Input, heightBuf, sizeof(heightBuf));
            GetWindowTextA(hFpsEdit_Input, fpsBuf, sizeof(fpsBuf));
            int selectedGpuIndex = SendMessage(g_hGpuCombo, CB_GETCURSEL, 0, 0);
            if (selectedGpuIndex != CB_ERR) {
                g_pSelectedAdapter = g_vAdapters[selectedGpuIndex];
            }
            else {
                g_pSelectedAdapter = nullptr;
            }


            try {
                g_currentWidth = std::stoi(widthBuf);
                g_currentHeight = std::stoi(heightBuf);
                g_targetFPS = std::stoi(fpsBuf);
                g_settingsConfirmed = true;
                DestroyWindow(hWnd);
            }
            catch (...) {
                MessageBox(hWnd, L"Please enter valid numbers.", L"Error", MB_OK | MB_ICONERROR);
            }
        }
        if (LOWORD(wParam) == IDC_CLOSE_SETTINGS_BUTTON) {
            DestroyWindow(hWnd); 
        }
        break;
    }
    case WM_DESTROY: {
        DeleteObject(hTitleFont);
        DeleteObject(hSageFont_Input);
        DeleteObject(g_hUiFont);
        DeleteObject(g_hbrGlow);
        DeleteObject(g_hbrWhite);
        if (!g_settingsConfirmed) PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void InitRenderWindow(HINSTANCE hInstance) {
    g_hUiFont = CreateFontA(22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Montserrat");
    g_hAuthorFont = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Montserrat");
    g_hbrGlow = CreateSolidBrush(RGB(230, 245, 255));
    g_hbrWhite = CreateSolidBrush(RGB(255, 255, 255));

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, RenderWndProc, 0L, 0L, hInstance, nullptr, nullptr, nullptr, nullptr, L"D3DRenderWindowClass", nullptr };
    RegisterClassEx(&wc);

    g_hRenderWnd = CreateWindow(wc.lpszClassName, L"CustomFPS", WS_POPUP | WS_CLIPCHILDREN, 100, 100, g_currentWidth, g_currentHeight, nullptr, nullptr, wc.hInstance, nullptr);

    g_hInputContainer = CreateWindowEx(0, L"STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, 0, 0, 0, g_hRenderWnd, NULL, hInstance, nullptr);
    SetWindowLongPtr(g_hInputContainer, GWLP_WNDPROC, (LONG_PTR)RenderWndProc);

    g_hLogo = CreateWindowA("STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, 0, 0, 0, g_hInputContainer, (HMENU)IDC_LOGO_STATIC, hInstance, nullptr);
    g_hWidthEdit = CreateWindowA("EDIT", std::to_string(g_currentWidth).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_CENTER, 0, 0, 0, 0, g_hInputContainer, nullptr, hInstance, nullptr);
    g_hHeightEdit = CreateWindowA("EDIT", std::to_string(g_currentHeight).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_CENTER, 0, 0, 0, 0, g_hInputContainer, nullptr, hInstance, nullptr);
    g_hFpsEdit = CreateWindowA("EDIT", std::to_string(g_targetFPS).c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_CENTER, 0, 0, 0, 0, g_hInputContainer, nullptr, hInstance, nullptr);
    g_hCloseButton = CreateWindowA("BUTTON", "Close Application", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, g_hInputContainer, (HMENU)IDC_CLOSE_BUTTON, hInstance, nullptr);

    g_pOriginalEditProc = (WNDPROC)SetWindowLongPtr(g_hWidthEdit, GWLP_WNDPROC, (LONG_PTR)EditProc);
    SetWindowLongPtr(g_hHeightEdit, GWLP_WNDPROC, (LONG_PTR)EditProc);
    SetWindowLongPtr(g_hFpsEdit, GWLP_WNDPROC, (LONG_PTR)EditProc);

    SendMessage(g_hWidthEdit, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);
    SendMessage(g_hHeightEdit, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);
    SendMessage(g_hFpsEdit, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);
    SendMessage(g_hCloseButton, WM_SETFONT, (WPARAM)g_hUiFont, TRUE);

    UpdateUIPositions();
    ShowWindow(g_hRenderWnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hRenderWnd);
}

LRESULT CALLBACK RenderWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProc(hWnd, msg, wParam, lParam);
        if (hit == HTCLIENT) return HTCAPTION;
        return hit;
    }
    case WM_ERASEBKGND: {
        if (hWnd == g_hInputContainer) {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hWnd, &rc);
            Gdiplus::Graphics graphics(hdc);
            Gdiplus::GraphicsPath path;
            path.AddRectangle(Gdiplus::Rect(0, 0, rc.right, rc.bottom));
            Gdiplus::PathGradientBrush pthGrBrush(&path);

            Gdiplus::Color centerColor(255, 33, 150, 243);
            Gdiplus::Color edgeColor(255, 13, 71, 161);

            pthGrBrush.SetCenterColor(centerColor);
            int count = 1;
            pthGrBrush.SetSurroundColors(&edgeColor, &count);
            graphics.FillRectangle(&pthGrBrush, 0, 0, rc.right, rc.bottom);
            return 1;
        }
        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
        if (pdis->CtlID == IDC_LOGO_STATIC) {
            Gdiplus::Graphics graphics(pdis->hDC);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            if (g_pLogoBitmap) {
                graphics.DrawImage(g_pLogoBitmap, 0, 0, pdis->rcItem.right, pdis->rcItem.bottom);
            }
        }
        return TRUE;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == IDC_CLOSE_BUTTON) { PostQuitMessage(0); return 0; }
        if (HIWORD(wParam) == EN_CHANGE) {
            char widthBuf[32] = {}, heightBuf[32] = {}, fpsBuf[32] = {};
            GetWindowTextA(g_hWidthEdit, widthBuf, sizeof(widthBuf));
            GetWindowTextA(g_hHeightEdit, heightBuf, sizeof(heightBuf));
            GetWindowTextA(g_hFpsEdit, fpsBuf, sizeof(fpsBuf));
            try {
                int newWidth = std::stoi(widthBuf), newHeight = std::stoi(heightBuf), newFPS = std::stoi(fpsBuf);
                if (newWidth >= 100 && newWidth <= 3840 && newHeight >= 100 && newHeight <= 2160 && newFPS >= 1 && newFPS <= 240) {
                    g_targetFPS = newFPS;
                    if (newWidth != g_currentWidth || newHeight != g_currentHeight) {
                        g_currentWidth = newWidth;
                        g_currentHeight = newHeight;
                        g_resizeRequested = true;
                    }
                }
            }
            catch (...) {}
        }
        break;
    }
    case WM_DESTROY:
        if (g_hUiFont) DeleteObject(g_hUiFont);
        if (g_hAuthorFont) DeleteObject(g_hAuthorFont);
        if (g_hbrGlow) DeleteObject(g_hbrGlow);
        if (g_hbrWhite) DeleteObject(g_hbrWhite);
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        if (g_pSwapChain && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
            UpdateUIPositions();
        }
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void UpdateUIPositions() {
    RECT rcClient;
    GetClientRect(g_hRenderWnd, &rcClient);

    const int padding = 20, spacing = 10, logoSize = 64;
    const int controlHeight = 40, editWidth = 120, closeButtonWidth = 170;

    std::vector<HWND> elements = { g_hLogo, g_hWidthEdit, g_hHeightEdit, g_hFpsEdit, g_hCloseButton };
    std::vector<int> elementWidths = { logoSize, editWidth, editWidth, editWidth, closeButtonWidth };
    int totalWidth = std::accumulate(elementWidths.begin(), elementWidths.end(), 0) + (elements.size() - 1) * spacing;

    int containerWidth = totalWidth + 2 * padding;
    int containerHeight = padding + logoSize + padding;
    int containerX = (rcClient.right - containerWidth) / 2;
    int containerY = (rcClient.bottom - containerHeight) / 2;
    SetWindowPos(g_hInputContainer, nullptr, containerX, containerY, containerWidth, containerHeight, SWP_NOZORDER);

    int currentX = padding;
    for (size_t i = 0; i < elements.size(); ++i) {
        int yPos = (elements[i] == g_hLogo) ? padding : padding + (logoSize - controlHeight) / 2;
        int height = (elements[i] == g_hLogo) ? logoSize : controlHeight;
        SetWindowPos(elements[i], nullptr, currentX, yPos, elementWidths[i], height, SWP_NOZORDER);
        currentX += elementWidths[i] + spacing;
    }
}

LRESULT CALLBACK EditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hHoveredEdit = nullptr;
    switch (msg) {
    case WM_MOUSEMOVE:
        if (hHoveredEdit != hWnd) {
            hHoveredEdit = hWnd;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;
    case WM_MOUSELEAVE:
        hHoveredEdit = nullptr;
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    case WM_CTLCOLOREDIT:
        return (LRESULT)(hWnd == hHoveredEdit ? g_hbrGlow : g_hbrWhite);
    }
    return CallWindowProc(g_pOriginalEditProc, hWnd, msg, wParam, lParam);
}

bool LoadLogoImage() {
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    HRSRC hResource = FindResource(hInstance, MAKEINTRESOURCE(IDB_LOGO1), L"PNG");
    if (!hResource) {
        return false;
    }

    DWORD imageSize = SizeofResource(hInstance, hResource);
    if (imageSize == 0) {
        return false;
    }

    HGLOBAL hGlobal = LoadResource(hInstance, hResource);
    if (!hGlobal) {
        return false;
    }

    LPVOID pSource = LockResource(hGlobal);
    if (!pSource) {
        return false;
    }

    HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, imageSize);
    if (hBuffer) {
        void* pBuffer = GlobalLock(hBuffer);
        if (pBuffer) {
            CopyMemory(pBuffer, pSource, imageSize);

            IStream* pStream = nullptr;
            if (CreateStreamOnHGlobal(hBuffer, TRUE, &pStream) == S_OK) {
                g_pLogoBitmap = Gdiplus::Bitmap::FromStream(pStream);
                pStream->Release();
            }
        }
        GlobalUnlock(hBuffer);
    }

    return g_pLogoBitmap && g_pLogoBitmap->GetLastStatus() == Gdiplus::Ok;
}

void InitD3D() {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = g_currentWidth;
    sd.BufferDesc.Height = g_currentHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_hRenderWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL featureLevelArray[1] = { D3D_FEATURE_LEVEL_11_0 };
    D3D11CreateDeviceAndSwapChain(g_pSelectedAdapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, featureLevelArray, 1, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, nullptr, &g_pd3dDeviceContext);
    CreateRenderTarget();
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_pRenderTargetView) { g_pRenderTargetView->Release(); g_pRenderTargetView = nullptr; }
}

void CleanupD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) g_pSwapChain->Release();
    if (g_pd3dDeviceContext) g_pd3dDeviceContext->Release();
    if (g_pd3dDevice) g_pd3dDevice->Release();
}

void UpdateWindowSize(int width, int height) {
    ShowWindow(g_hInputContainer, SW_HIDE);
    CleanupRenderTarget();
    if (g_pSwapChain) g_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
    SetWindowPos(g_hRenderWnd, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
    UpdateUIPositions();
    ShowWindow(g_hInputContainer, SW_SHOW);
}

void RenderFrame() {
    const float clearColor[4] = { 13.0f / 255.0f, 71.0f / 255.0f, 161.0f / 255.0f, 1.0f };

    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_pRenderTargetView, nullptr);
    g_pd3dDeviceContext->ClearRenderTargetView(g_pRenderTargetView, clearColor);
    g_pSwapChain->Present(0, 0);
}
