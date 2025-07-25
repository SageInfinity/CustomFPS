#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <gdiplus.h>
#include <commctrl.h>
#include <vector>
#include <numeric>
#include <olectl.h>
#include <timeapi.h>
#include "resource.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "winmm.lib")

#define IDC_CLOSE_BUTTON 101
#define IDC_LOGO_STATIC 102
#define IDC_START_BUTTON 104
#define IDC_SAGE_TEXT 105
#define IDC_RESOLUTION_LABEL 106
#define IDC_FPS_LABEL 107
#define IDC_GPU_COMBO 108
#define IDC_OUTPUT_COMBO 110
#define IDC_CLOSE_SETTINGS_BUTTON 109
#define IDC_FULLSCREEN_CHECKBOX 111
#define IDI_APPICON 112

struct AdapterOutputPair {
	IDXGIAdapter* pAdapter;
	IDXGIOutput* pOutput;
};

ID3D11Device* g_pDevice = nullptr;
ID3D11DeviceContext* g_pDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pRenderTargetView = nullptr;

IDXGIAdapter* g_pSelectedAdapter = nullptr;
IDXGIAdapter* g_pDisplayAdapter = nullptr;
IDXGIOutput* g_pSelectedOutput = nullptr;
std::vector<IDXGIAdapter*> g_vAdapters;
std::vector<AdapterOutputPair> g_vOutputs;

bool g_isMultiGpu = false;
ID3D11Device* g_pProcessingDevice = nullptr;
ID3D11DeviceContext* g_pProcessingDeviceContext = nullptr;
ID3D11Texture2D* g_pSharedTexture = nullptr;
ID3D11RenderTargetView* g_pSharedRTV = nullptr;

HWND g_hRenderWnd;
HFONT g_hUiFont = nullptr;
HFONT g_hAuthorFont = nullptr;
HWND g_hGpuCombo = nullptr;
HWND g_hOutputCombo = nullptr;

ULONG_PTR g_gdiplusToken;
Gdiplus::Bitmap* g_pLogoBitmap = nullptr;
WNDPROC g_pOriginalEditProc = nullptr;
HBRUSH g_hbrGlow = nullptr;
HBRUSH g_hbrWhite = nullptr;
HBRUSH g_hbrBackground = nullptr;

int g_currentWidth = 800;
int g_currentHeight = 600;
int g_targetFPS = 60;
bool g_settingsConfirmed = false;
bool g_borderlessFullscreen = true;
bool g_resizeRequested = false;

void InitRenderWindow(HINSTANCE hInstance);
LRESULT CALLBACK RenderWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void InitD3D();
void RenderFrame();
void CleanupD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
void UpdateWindowSize(int width, int height);
void CleanupSharedResources();
bool CreateSharedResources(int width, int height);

void InitInputWindow(HINSTANCE hInstance);
LRESULT CALLBACK InputWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool LoadLogoImage();
LRESULT CALLBACK EditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void EnumerateAdapters();
void EnumerateOutputs(HWND hWidthEdit, HWND hHeightEdit);
void UpdateResolutionFields(HWND hWidthEdit, HWND hHeightEdit, int outputIndex);
void ToggleResolutionControls(HWND hWnd, bool show);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);

	if (!LoadLogoImage()) {
		MessageBox(NULL, L"Could not load logo from resources.", L"Error", MB_ICONERROR | MB_OK);
		return 1;
	}
	EnumerateAdapters();

	timeBeginPeriod(1);

	while (true)
	{
		g_settingsConfirmed = false;
		InitInputWindow(hInstance);

		MSG inputMsg = { 0 };
		while (GetMessage(&inputMsg, nullptr, 0, 0))
		{
			TranslateMessage(&inputMsg);
			DispatchMessage(&inputMsg);
		}

		if (!g_settingsConfirmed) {
			break;
		}

		InitRenderWindow(hInstance);
		InitD3D();

		LARGE_INTEGER frequency, startTime, currentTime;
		QueryPerformanceFrequency(&frequency);
		QueryPerformanceCounter(&startTime);

		double targetFrameTime = 1.0 / g_targetFPS;

		MSG renderMsg = { 0 };
		while (renderMsg.message != WM_QUIT)
		{
			if (PeekMessage(&renderMsg, NULL, 0U, 0U, PM_REMOVE))
			{
				TranslateMessage(&renderMsg);
				DispatchMessage(&renderMsg);
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

				if (elapsed >= targetFrameTime)
				{
					RenderFrame();
					startTime = currentTime;
				}
				else
				{
					Sleep(0);
				}
			}
		}
		CleanupD3D();
	}

	if (g_pLogoBitmap) delete g_pLogoBitmap;
	Gdiplus::GdiplusShutdown(g_gdiplusToken);
	UnregisterClass(L"D3DRenderWindowClass", hInstance);
	UnregisterClass(L"SageInputWindow", hInstance);
	for (auto& adapter : g_vAdapters) {
		adapter->Release();
	}
	g_vAdapters.clear();

	for (auto& pair : g_vOutputs) {
		pair.pOutput->Release();
	}
	g_vOutputs.clear();

	timeEndPeriod(1);

	return 0;
}

void EnumerateAdapters()
{
	IDXGIFactory* pFactory = nullptr;
	CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory);
	if (!pFactory) return;

	IDXGIAdapter* pAdapter;
	for (UINT i = 0; pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
	{
		g_vAdapters.push_back(pAdapter);
	}

	pFactory->Release();
}

void UpdateResolutionFields(HWND hWidthEdit, HWND hHeightEdit, int outputIndex) {
	if (g_borderlessFullscreen && outputIndex >= 0 && static_cast<size_t>(outputIndex) < g_vOutputs.size()) {
		IDXGIOutput* pOutput = g_vOutputs[outputIndex].pOutput;
		DXGI_OUTPUT_DESC outputDesc;
		pOutput->GetDesc(&outputDesc);
		int width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
		int height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
		SetWindowTextA(hWidthEdit, std::to_string(width).c_str());
		SetWindowTextA(hHeightEdit, std::to_string(height).c_str());
	}
}

void EnumerateOutputs(HWND hWidthEdit, HWND hHeightEdit) {
	for (auto& pair : g_vOutputs) {
		pair.pOutput->Release();
	}
	g_vOutputs.clear();
	SendMessage(g_hOutputCombo, CB_RESETCONTENT, 0, 0);

	for (IDXGIAdapter* pAdapter : g_vAdapters) {
		DXGI_ADAPTER_DESC adapterDesc;
		pAdapter->GetDesc(&adapterDesc);
		IDXGIOutput* pOutput;
		for (UINT i = 0; pAdapter->EnumOutputs(i, &pOutput) != DXGI_ERROR_NOT_FOUND; ++i) {
			g_vOutputs.push_back({ pAdapter, pOutput });
			DXGI_OUTPUT_DESC outputDesc;
			pOutput->GetDesc(&outputDesc);
			std::wstring displayText = L"Display: ";
			displayText += outputDesc.DeviceName;
			SendMessage(g_hOutputCombo, CB_ADDSTRING, 0, (LPARAM)displayText.c_str());
		}
	}
	if (!g_vOutputs.empty()) {
		SendMessage(g_hOutputCombo, CB_SETCURSEL, (WPARAM)0, (LPARAM)0);
		UpdateResolutionFields(hWidthEdit, hHeightEdit, 0);
	}
}

void ToggleResolutionControls(HWND hWnd, bool show) {
	ShowWindow(GetDlgItem(hWnd, IDC_RESOLUTION_LABEL), show ? SW_SHOW : SW_HIDE);
	ShowWindow(GetDlgItem(hWnd, 1), show ? SW_SHOW : SW_HIDE);
	ShowWindow(GetDlgItem(hWnd, 2), show ? SW_SHOW : SW_HIDE);
}

void InitInputWindow(HINSTANCE hInstance) {
	WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, InputWndProc, 0, 0, hInstance, nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"SageInputWindow", nullptr };
	RegisterClassEx(&wc);

	const int windowHeight = 700;
	HWND hInputWnd = CreateWindowEx(
		WS_EX_LAYERED,
		wc.lpszClassName, L"Settings", WS_POPUP | WS_VISIBLE,
		(GetSystemMetrics(SM_CXSCREEN) - 400) / 2, (GetSystemMetrics(SM_CYSCREEN) - windowHeight) / 2,
		400, windowHeight, nullptr, nullptr, hInstance, nullptr
	);

	SetLayeredWindowAttributes(hInputWnd, 0, 255, LWA_ALPHA);
	HRGN hRgn = CreateRoundRectRgn(0, 0, 400, windowHeight, 20, 20);
	SetWindowRgn(hInputWnd, hRgn, TRUE);

	ShowWindow(hInputWnd, SW_SHOW);
	UpdateWindow(hInputWnd);
}

LRESULT CALLBACK InputWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	static HFONT hTitleFont, hLabelFont, hButtonFont, hCheckFont, hAuthorFont, hEscFont;
	static HWND hWidthEdit_Input, hHeightEdit_Input, hFpsEdit_Input, hResLabel;
	static HWND hFullscreenCheck;

	using namespace Gdiplus;

	switch (msg) {
	case WM_CREATE: {
		hTitleFont = CreateFontA(30, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
		hLabelFont = CreateFontA(28, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI Light");
		hButtonFont = CreateFontA(24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
		hCheckFont = CreateFontA(24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
		hAuthorFont = CreateFontA(18, 0, 0, 0, FW_NORMAL, TRUE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
		hEscFont = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

		g_hbrGlow = CreateSolidBrush(RGB(240, 248, 255));
		g_hbrWhite = CreateSolidBrush(RGB(255, 255, 255));

		HINSTANCE hInstance = (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE);

		CreateWindowA("STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 150, 15, 100, 100, hWnd, (HMENU)IDC_LOGO_STATIC, hInstance, nullptr);

		g_hGpuCombo = CreateWindowA("COMBOBOX", "", CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_VISIBLE | CBS_OWNERDRAWFIXED, 50, 150, 300, 300, hWnd, (HMENU)IDC_GPU_COMBO, hInstance, nullptr);
		SendMessage(g_hGpuCombo, CB_SETITEMHEIGHT, (WPARAM)-1, (LPARAM)28);
		for (const auto& adapter : g_vAdapters) {
			DXGI_ADAPTER_DESC desc;
			adapter->GetDesc(&desc);
			SendMessage(g_hGpuCombo, (UINT)CB_ADDSTRING, (WPARAM)0, (LPARAM)desc.Description);
		}
		if (!g_vAdapters.empty()) {
			SendMessage(g_hGpuCombo, CB_SETCURSEL, (WPARAM)0, (LPARAM)0);
		}

		g_hOutputCombo = CreateWindowA("COMBOBOX", "", CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_VISIBLE | CBS_OWNERDRAWFIXED, 50, 210, 300, 300, hWnd, (HMENU)IDC_OUTPUT_COMBO, hInstance, nullptr);
		SendMessage(g_hOutputCombo, CB_SETITEMHEIGHT, (WPARAM)-1, (LPARAM)28);

		hFullscreenCheck = CreateWindowA("BUTTON", "Borderless Fullscreen", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 90, 280, 220, 30, hWnd, (HMENU)IDC_FULLSCREEN_CHECKBOX, hInstance, nullptr);
		SendMessage(hFullscreenCheck, BM_SETCHECK, g_borderlessFullscreen ? BST_CHECKED : BST_UNCHECKED, 0);

		hResLabel = CreateWindowA("STATIC", "Resolution (W x H)", WS_CHILD | WS_VISIBLE | SS_CENTER, 75, 320, 250, 35, hWnd, (HMENU)IDC_RESOLUTION_LABEL, hInstance, nullptr);
		hWidthEdit_Input = CreateWindowA("EDIT", "800", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER | ES_NUMBER, 100, 360, 80, 40, hWnd, (HMENU)1, hInstance, nullptr);
		hHeightEdit_Input = CreateWindowA("EDIT", "600", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER | ES_NUMBER, 220, 360, 80, 40, hWnd, (HMENU)2, hInstance, nullptr);

		CreateWindowA("STATIC", "Target FPS", WS_CHILD | WS_VISIBLE | SS_CENTER, 75, 410, 250, 35, hWnd, (HMENU)IDC_FPS_LABEL, hInstance, nullptr);
		hFpsEdit_Input = CreateWindowA("EDIT", "60", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_CENTER, 150, 450, 100, 40, hWnd, (HMENU)3, hInstance, nullptr);

		HWND hStartButton = CreateWindowA("BUTTON", "Start Render", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 100, 520, 200, 60, hWnd, (HMENU)IDC_START_BUTTON, hInstance, nullptr);
		HWND hEscText = CreateWindowA("STATIC", "Press 'Esc' to come back to this screen", WS_CHILD | WS_VISIBLE | SS_CENTER, 50, 585, 300, 20, hWnd, (HMENU)IDC_STATIC, hInstance, nullptr);
		HWND hCloseSettingsButton = CreateWindowA("BUTTON", "Close", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 125, 610, 150, 50, hWnd, (HMENU)IDC_CLOSE_SETTINGS_BUTTON, hInstance, nullptr);
		HWND hSageText = CreateWindowA("STATIC", "- by Sage", WS_CHILD | WS_VISIBLE | SS_CENTER, 150, 665, 100, 25, hWnd, (HMENU)IDC_SAGE_TEXT, hInstance, nullptr);

		SendMessage(g_hGpuCombo, WM_SETFONT, (WPARAM)hCheckFont, TRUE);
		SendMessage(g_hOutputCombo, WM_SETFONT, (WPARAM)hCheckFont, TRUE);
		SendMessage(hWidthEdit_Input, WM_SETFONT, (WPARAM)hCheckFont, TRUE);
		SendMessage(hHeightEdit_Input, WM_SETFONT, (WPARAM)hCheckFont, TRUE);
		SendMessage(hFpsEdit_Input, WM_SETFONT, (WPARAM)hCheckFont, TRUE);
		SendMessage(hResLabel, WM_SETFONT, (WPARAM)hLabelFont, TRUE);
		SendMessage(GetDlgItem(hWnd, IDC_FPS_LABEL), WM_SETFONT, (WPARAM)hLabelFont, TRUE);
		SendMessage(hStartButton, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
		SendMessage(hCloseSettingsButton, WM_SETFONT, (WPARAM)hButtonFont, TRUE);
		SendMessage(hFullscreenCheck, WM_SETFONT, (WPARAM)hCheckFont, TRUE);
		SendMessage(hSageText, WM_SETFONT, (WPARAM)hAuthorFont, TRUE);
		SendMessage(hEscText, WM_SETFONT, (WPARAM)hEscFont, TRUE);

		g_pOriginalEditProc = (WNDPROC)SetWindowLongPtr(hFpsEdit_Input, GWLP_WNDPROC, (LONG_PTR)EditProc);
		SetWindowLongPtr(hWidthEdit_Input, GWLP_WNDPROC, (LONG_PTR)EditProc);
		SetWindowLongPtr(hHeightEdit_Input, GWLP_WNDPROC, (LONG_PTR)EditProc);

		EnumerateOutputs(hWidthEdit_Input, hHeightEdit_Input);
		ToggleResolutionControls(hWnd, !g_borderlessFullscreen);
		EnableWindow(hWidthEdit_Input, !g_borderlessFullscreen);
		EnableWindow(hHeightEdit_Input, !g_borderlessFullscreen);

		break;
	}
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);
		Graphics graphics(hdc);
		graphics.SetSmoothingMode(SmoothingModeAntiAlias);
		Rect rc(0, 0, 400, 700);
		LinearGradientBrush linGrBrush(rc, Color(255, 33, 150, 243), Color(255, 13, 71, 161), LinearGradientModeVertical);
		graphics.FillRectangle(&linGrBrush, rc);
		EndPaint(hWnd, &ps);
		return 1;
	}
	case WM_NCHITTEST: {
		LRESULT hit = DefWindowProc(hWnd, msg, wParam, lParam);
		if (hit == HTCLIENT) return HTCAPTION;
		return hit;
	}
	case WM_MEASUREITEM: {
		LPMEASUREITEMSTRUCT lpmis = (LPMEASUREITEMSTRUCT)lParam;
		if (lpmis->CtlID == IDC_GPU_COMBO || lpmis->CtlID == IDC_OUTPUT_COMBO) {
			lpmis->itemHeight = 30;
		}
		return TRUE;
	}
	case WM_DRAWITEM: {
		LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
		if (pdis->CtlID == IDC_LOGO_STATIC && g_pLogoBitmap) {
			Graphics graphics(pdis->hDC);
			graphics.SetSmoothingMode(SmoothingModeAntiAlias);
			graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
			graphics.DrawImage(g_pLogoBitmap, 0, 0, pdis->rcItem.right, pdis->rcItem.bottom);
		}
		else if (pdis->CtlType == ODT_BUTTON) {
			Graphics graphics(pdis->hDC);
			graphics.SetTextRenderingHint(TextRenderingHintAntiAlias);
			graphics.SetSmoothingMode(SmoothingModeAntiAlias);

			RectF rect(REAL(pdis->rcItem.left), REAL(pdis->rcItem.top), REAL(pdis->rcItem.right - pdis->rcItem.left), REAL(pdis->rcItem.bottom - pdis->rcItem.top));
			GraphicsPath path;
			REAL radius = 15.0f;
			path.AddArc(rect.X, rect.Y, radius * 2, radius * 2, 180, 90);
			path.AddArc(rect.GetRight() - (radius * 2), rect.Y, radius * 2, radius * 2, 270, 90);
			path.AddArc(rect.GetRight() - (radius * 2), rect.GetBottom() - (radius * 2), radius * 2, radius * 2, 0, 90);
			path.AddArc(rect.X, rect.GetBottom() - (radius * 2), radius * 2, radius * 2, 90, 90);
			path.CloseFigure();

			Color btnColor = (pdis->itemState & ODS_SELECTED) ? Color(40, 0, 0, 0) : Color(20, 0, 0, 0);
			SolidBrush fillBrush(btnColor);
			graphics.FillPath(&fillBrush, &path);

			HFONT hFont = (HFONT)SendMessage(pdis->hwndItem, WM_GETFONT, 0, 0);
			LOGFONTW lf;
			GetObjectW(hFont, sizeof(LOGFONTW), &lf);
			Font gdiplusFont(pdis->hDC, &lf);

			SolidBrush shadowBrush(Color(60, 0, 0, 0));
			SolidBrush textBrush(Color(255, 255, 255, 255));
			StringFormat stringFormat;
			stringFormat.SetAlignment(StringAlignmentCenter);
			stringFormat.SetLineAlignment(StringAlignmentCenter);

			WCHAR buttonText[256];
			GetWindowTextW(pdis->hwndItem, buttonText, 256);

			RectF shadowRect = rect;
			shadowRect.X += 1.0f;
			shadowRect.Y += 1.0f;
			graphics.DrawString(buttonText, -1, &gdiplusFont, shadowRect, &stringFormat, &shadowBrush);
			graphics.DrawString(buttonText, -1, &gdiplusFont, rect, &stringFormat, &textBrush);

		}
		else if (pdis->CtlID == IDC_GPU_COMBO || pdis->CtlID == IDC_OUTPUT_COMBO) {
			Graphics graphics(pdis->hDC);
			graphics.SetTextRenderingHint(TextRenderingHintAntiAlias);
			graphics.SetSmoothingMode(SmoothingModeAntiAlias);

			RectF rect(REAL(pdis->rcItem.left), REAL(pdis->rcItem.top), REAL(pdis->rcItem.right - pdis->rcItem.left), REAL(pdis->rcItem.bottom - pdis->rcItem.top));

			if ((pdis->itemState & ODS_SELECTED) && (pdis->itemAction & (ODA_SELECT | ODA_DRAWENTIRE))) {
				SolidBrush selectedBrush(Color(220, 235, 255));
				graphics.FillRectangle(&selectedBrush, rect);
			}
			else {
				SolidBrush backgroundBrush(Color(255, 255, 255, 255));
				graphics.FillRectangle(&backgroundBrush, rect);
			}

			if (pdis->itemState & ODS_FOCUS) {
				Pen focusPen(Color(100, 33, 150, 243), 2.0f);
				graphics.DrawRectangle(&focusPen, rect.X, rect.Y, rect.Width - 1, rect.Height - 1);
			}

			WCHAR itemText[256] = L"";
			if (pdis->itemID != (UINT)-1) {
				SendMessageW(pdis->hwndItem, CB_GETLBTEXT, pdis->itemID, (LPARAM)itemText);
			}

			Font font(L"Segoe UI", 16, FontStyleRegular, UnitPixel);
			SolidBrush textBrush(Color(255, 0, 0, 0));
			StringFormat stringFormat;
			stringFormat.SetAlignment(StringAlignmentNear);
			stringFormat.SetLineAlignment(StringAlignmentCenter);
			rect.X += 8;
			graphics.DrawString(itemText, -1, &font, rect, &stringFormat, &textBrush);
		}
		return TRUE;
	}
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLORBTN: {
		HDC hdcStatic = (HDC)wParam;
		SetBkMode(hdcStatic, TRANSPARENT);
		SetTextColor(hdcStatic, RGB(255, 255, 255));
		return (LRESULT)GetStockObject(NULL_BRUSH);
	}
	case WM_COMMAND: {
		if (HIWORD(wParam) == CBN_SELCHANGE) {
			if (LOWORD(wParam) == IDC_OUTPUT_COMBO) {
				int selectedOutputIndex = SendMessage(g_hOutputCombo, CB_GETCURSEL, 0, 0);
				UpdateResolutionFields(hWidthEdit_Input, hHeightEdit_Input, selectedOutputIndex);
			}
		}
		if (LOWORD(wParam) == IDC_FULLSCREEN_CHECKBOX) {
			EnableWindow(hFullscreenCheck, TRUE);
			g_borderlessFullscreen = (SendMessage(hFullscreenCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
			ToggleResolutionControls(hWnd, !g_borderlessFullscreen);
			if (g_borderlessFullscreen) {
				UpdateResolutionFields(hWidthEdit_Input, hHeightEdit_Input, SendMessage(g_hOutputCombo, CB_GETCURSEL, 0, 0));
				EnableWindow(hWidthEdit_Input, FALSE);
				EnableWindow(hHeightEdit_Input, FALSE);
			}
			else {
				EnableWindow(hWidthEdit_Input, TRUE);
				EnableWindow(hHeightEdit_Input, TRUE);
			}
		}

		if (LOWORD(wParam) == IDC_START_BUTTON) {
			char widthBuf[32], heightBuf[32], fpsBuf[32];
			GetWindowTextA(hWidthEdit_Input, widthBuf, sizeof(widthBuf));
			GetWindowTextA(hHeightEdit_Input, heightBuf, sizeof(heightBuf));
			GetWindowTextA(hFpsEdit_Input, fpsBuf, sizeof(fpsBuf));
			int selectedGpuIndex = SendMessage(g_hGpuCombo, CB_GETCURSEL, 0, 0);
			int selectedOutputIndex = SendMessage(g_hOutputCombo, CB_GETCURSEL, 0, 0);

			if (selectedGpuIndex == CB_ERR || selectedOutputIndex == CB_ERR) {
				MessageBox(hWnd, L"Please select a valid GPU and Monitor.", L"Error", MB_OK | MB_ICONERROR);
				break;
			}

			g_pSelectedAdapter = g_vAdapters[selectedGpuIndex];
			g_pDisplayAdapter = g_vOutputs[selectedOutputIndex].pAdapter;
			g_pSelectedOutput = g_vOutputs[selectedOutputIndex].pOutput;

			if (g_vAdapters.size() > 1) {
				g_isMultiGpu = (g_pSelectedAdapter != g_pDisplayAdapter);
			}
			else {
				g_isMultiGpu = false;
			}

			if (!g_pSelectedAdapter || !g_pSelectedOutput) {
				MessageBox(hWnd, L"Please select a valid GPU and Monitor.", L"Error", MB_OK | MB_ICONERROR);
				break;
			}

			try {
				if (!g_borderlessFullscreen) {
					g_currentWidth = std::stoi(widthBuf);
					g_currentHeight = std::stoi(heightBuf);
				}
				g_targetFPS = std::stoi(fpsBuf);
				g_settingsConfirmed = true;
				DestroyWindow(hWnd);
			}
			catch (...) {
				MessageBox(hWnd, L"Please enter valid numbers for all fields.", L"Error", MB_OK | MB_ICONERROR);
			}
		}
		if (LOWORD(wParam) == IDC_CLOSE_SETTINGS_BUTTON) {
			DestroyWindow(hWnd);
		}
		break;
	}
	case WM_DESTROY: {
		DeleteObject(hTitleFont);
		DeleteObject(hLabelFont);
		DeleteObject(hButtonFont);
		DeleteObject(hCheckFont);
		DeleteObject(hAuthorFont);
		DeleteObject(hEscFont);
		DeleteObject(g_hbrGlow);
		DeleteObject(g_hbrWhite);
		PostQuitMessage(0);
		return 0;
	}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

void InitRenderWindow(HINSTANCE hInstance) {
	if (!g_pSelectedOutput) return;

	DWORD style = WS_POPUP | WS_CLIPCHILDREN;
	int x = 100, y = 100;
	LPCWSTR title = L"";

	if (g_borderlessFullscreen) {
		DXGI_OUTPUT_DESC outputDesc;
		g_pSelectedOutput->GetDesc(&outputDesc);
		g_currentWidth = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
		g_currentHeight = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
		x = outputDesc.DesktopCoordinates.left;
		y = outputDesc.DesktopCoordinates.top;
		title = L"";
	}
	else {
		style = WS_OVERLAPPEDWINDOW;
		title = L"CustomFPS";
	}

	g_hbrBackground = CreateSolidBrush(RGB(13, 71, 161));

	WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, RenderWndProc, 0L, 0L, hInstance, LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON)), nullptr, g_hbrBackground, nullptr, L"D3DRenderWindowClass", LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON)) };
	RegisterClassEx(&wc);

	g_hRenderWnd = CreateWindow(wc.lpszClassName, title, style, x, y, g_currentWidth, g_currentHeight, nullptr, nullptr, wc.hInstance, nullptr);

	ShowWindow(g_hRenderWnd, SW_SHOWDEFAULT);
	UpdateWindow(g_hRenderWnd);
}

LRESULT CALLBACK RenderWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE) {
			DestroyWindow(hWnd);
		}
		break;
	case WM_NCHITTEST: {
		LRESULT hit = DefWindowProc(hWnd, msg, wParam, lParam);
		if (hit == HTCLIENT && !g_borderlessFullscreen) return HTCAPTION;
		return hit;
	}
	case WM_DESTROY:
		if (g_hbrBackground) { DeleteObject(g_hbrBackground); g_hbrBackground = nullptr; }
		PostQuitMessage(0);
		return 0;
	case WM_SIZE:
		if (g_pSwapChain && wParam != SIZE_MINIMIZED) {
			g_currentWidth = LOWORD(lParam);
			g_currentHeight = HIWORD(lParam);
			g_resizeRequested = true;
		}
		return 0;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
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
	if (!hResource) return false;
	DWORD imageSize = SizeofResource(hInstance, hResource);
	if (imageSize == 0) return false;
	HGLOBAL hGlobal = LoadResource(hInstance, hResource);
	if (!hGlobal) return false;
	LPVOID pSource = LockResource(hGlobal);
	if (!pSource) return false;
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
	sd.BufferDesc.RefreshRate.Numerator = g_targetFPS;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = g_hRenderWnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	const D3D_FEATURE_LEVEL featureLevelArray[1] = { D3D_FEATURE_LEVEL_11_0 };

	if (!g_isMultiGpu) {
		D3D11CreateDeviceAndSwapChain(g_pSelectedAdapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, featureLevelArray, 1, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pDevice, nullptr, &g_pDeviceContext);
	}
	else {
		D3D11CreateDevice(g_pSelectedAdapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, featureLevelArray, 1, D3D11_SDK_VERSION, &g_pProcessingDevice, nullptr, &g_pProcessingDeviceContext);
		D3D11CreateDevice(g_pDisplayAdapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, featureLevelArray, 1, D3D11_SDK_VERSION, &g_pDevice, nullptr, &g_pDeviceContext);

		CreateSharedResources(g_currentWidth, g_currentHeight);

		IDXGIFactory* pFactory;
		HRESULT hr = g_pDisplayAdapter->GetParent(__uuidof(IDXGIFactory), (void**)&pFactory);
		if (SUCCEEDED(hr)) {
			pFactory->CreateSwapChain(g_pDevice, &sd, &g_pSwapChain);
			pFactory->Release();
		}
	}

	if (g_borderlessFullscreen && g_pSwapChain) {
		IDXGIFactory* pFactory;
		if (SUCCEEDED(g_pSwapChain->GetParent(__uuidof(IDXGIFactory), (void**)&pFactory))) {
			pFactory->MakeWindowAssociation(g_hRenderWnd, DXGI_MWA_NO_ALT_ENTER);
			pFactory->Release();
		}
	}

	CreateRenderTarget();
}

void CreateRenderTarget() {
	ID3D11Texture2D* pBackBuffer;
	if (g_pSwapChain && SUCCEEDED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer)))) {
		g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
		pBackBuffer->Release();
	}
}

void CleanupRenderTarget() {
	if (g_pRenderTargetView) { g_pRenderTargetView->Release(); g_pRenderTargetView = nullptr; }
}

void CleanupSharedResources() {
	if (g_pSharedRTV) { g_pSharedRTV->Release(); g_pSharedRTV = nullptr; }
	if (g_pSharedTexture) { g_pSharedTexture->Release(); g_pSharedTexture = nullptr; }
}

bool CreateSharedResources(int width, int height) {
	if (!g_pProcessingDevice || !g_pDevice) return false;

	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = width;
	texDesc.Height = height;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

	ID3D11Texture2D* pProcessingTexture = nullptr;
	HRESULT hr = g_pProcessingDevice->CreateTexture2D(&texDesc, nullptr, &pProcessingTexture);
	if (FAILED(hr)) return false;

	hr = g_pProcessingDevice->CreateRenderTargetView(pProcessingTexture, nullptr, &g_pSharedRTV);
	if (FAILED(hr)) {
		pProcessingTexture->Release();
		return false;
	}

	IDXGIResource* pDXGIResource = nullptr;
	hr = pProcessingTexture->QueryInterface(__uuidof(IDXGIResource), (void**)&pDXGIResource);
	if (FAILED(hr)) {
		pProcessingTexture->Release();
		return false;
	}

	HANDLE hSharedHandle = nullptr;
	pDXGIResource->GetSharedHandle(&hSharedHandle);
	pDXGIResource->Release();

	if (hSharedHandle == nullptr) {
		pProcessingTexture->Release();
		return false;
	}

	hr = g_pDevice->OpenSharedResource(hSharedHandle, __uuidof(ID3D11Texture2D), (void**)&g_pSharedTexture);
	pProcessingTexture->Release();

	return SUCCEEDED(hr);
}

void CleanupD3D() {
	CleanupRenderTarget();
	if (g_pSwapChain) {
		g_pSwapChain->SetFullscreenState(FALSE, NULL);
		g_pSwapChain->Release();
		g_pSwapChain = nullptr;
	}

	if (g_isMultiGpu) {
		CleanupSharedResources();
		if (g_pProcessingDeviceContext) { g_pProcessingDeviceContext->Release(); g_pProcessingDeviceContext = nullptr; }
		if (g_pProcessingDevice) { g_pProcessingDevice->Release(); g_pProcessingDevice = nullptr; }
	}

	if (g_pDeviceContext) { g_pDeviceContext->Release(); g_pDeviceContext = nullptr; }
	if (g_pDevice) { g_pDevice->Release(); g_pDevice = nullptr; }
	g_isMultiGpu = false;
	g_pSelectedAdapter = nullptr;
	g_pDisplayAdapter = nullptr;
}

void UpdateWindowSize(int width, int height) {
	CleanupRenderTarget();
	if (g_pSwapChain) {
		if (g_isMultiGpu) {
			CleanupSharedResources();
		}
		g_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
		if (g_isMultiGpu) {
			CreateSharedResources(width, height);
		}
	}
	CreateRenderTarget();
	if (!g_borderlessFullscreen) {
		RECT wr = { 0, 0, width, height };
		AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
		SetWindowPos(g_hRenderWnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
	}
}

void RenderFrame() {
	const float clearColor[4] = { 13.0f / 255.0f, 71.0f / 255.0f, 161.0f / 255.0f, 1.0f };

	if (!g_isMultiGpu) {
		if (g_pDeviceContext && g_pRenderTargetView) {
			g_pDeviceContext->OMSetRenderTargets(1, &g_pRenderTargetView, nullptr);
			g_pDeviceContext->ClearRenderTargetView(g_pRenderTargetView, clearColor);
		}
	}
	else {
		if (g_pProcessingDeviceContext && g_pSharedRTV && g_pDeviceContext && g_pSharedTexture) {
			g_pProcessingDeviceContext->OMSetRenderTargets(1, &g_pSharedRTV, nullptr);
			g_pProcessingDeviceContext->ClearRenderTargetView(g_pSharedRTV, clearColor);
			g_pProcessingDeviceContext->Flush();

			ID3D11Texture2D* pBackBuffer = nullptr;
			if (g_pSwapChain && SUCCEEDED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer)))) {
				g_pDeviceContext->CopyResource(pBackBuffer, g_pSharedTexture);
				pBackBuffer->Release();
			}
		}
	}

	if (g_pSwapChain) {
		g_pSwapChain->Present(1, 0);
	}
}