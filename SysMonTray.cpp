// SysMonTray.cpp — Win32 (CPU & RAM Gauges) — Task Manager–like CPU, auto-fit gauges
// VS2022, Unicode. Link: pdh.lib; comctl32.lib
// Features: CPU % (Utility counter preferred), RAM %, circular gauges, minimize-to-tray.
// No CSV, no temps, no GPU.

#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <strsafe.h>
#include <pdh.h>
#include <vector>
#include <string>
#include <math.h>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "comctl32.lib")

// -----------------------------
// Config
// -----------------------------
static const UINT TIMER_ID_SAMPLE = 1;
static const UINT SAMPLE_INTERVAL_MS = 1000;
static const UINT WM_TRAYICON = WM_APP + 1;
static const UINT TRAY_ICON_ID = 1001;

// -----------------------------
// Globals & UI
// -----------------------------
HINSTANCE       g_hInst = nullptr;
HWND            g_hWnd = nullptr;
HFONT           g_hFontTitle = nullptr;
HFONT           g_hFontBody = nullptr;
NOTIFYICONDATA  g_nid = { 0 };

// Latest values (+ EMA for smoother CPU)
double g_cpu = -1.0;
double g_ram = -1.0;
double g_cpuEMA = -1.0;

// PDH CPU
PDH_HQUERY   g_cpuQuery = nullptr;
PDH_HCOUNTER g_cpuTotal = nullptr;

// -----------------------------
// Colors & Fonts
// -----------------------------
static COLORREF C_BG = RGB(20, 22, 26);
static COLORREF C_CARD = RGB(32, 35, 42);
static COLORREF C_TEXT = RGB(230, 232, 238);
static COLORREF C_MUT = RGB(160, 165, 175);
static COLORREF C_ACC1 = RGB(0, 120, 215);   // CPU
static COLORREF C_ACC2 = RGB(30, 200, 160);  // RAM

static void EnsureFonts()
{
    if (!g_hFontTitle) {
        LOGFONTW lf{}; lf.lfHeight = -28; lf.lfWeight = FW_SEMIBOLD; StringCchCopyW(lf.lfFaceName, 32, L"Segoe UI");
        g_hFontTitle = CreateFontIndirectW(&lf);
        lf = LOGFONTW{}; lf.lfHeight = -16; lf.lfWeight = FW_NORMAL; StringCchCopyW(lf.lfFaceName, 32, L"Segoe UI");
        g_hFontBody = CreateFontIndirectW(&lf);
    }
}

// -----------------------------
// PDH helpers (English path when available)
// -----------------------------
typedef PDH_STATUS(WINAPI* PFN_PdhAddEnglishCounterW)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
static PFN_PdhAddEnglishCounterW g_pPdhAddEnglishCounterW = nullptr;

static void InitPdhEnglish()
{
    HMODULE h = GetModuleHandleW(L"pdh.dll");
    if (!h) h = LoadLibraryW(L"pdh.dll");
    if (h) g_pPdhAddEnglishCounterW = (PFN_PdhAddEnglishCounterW)GetProcAddress(h, "PdhAddEnglishCounterW");
}

static PDH_STATUS AddCounterAnyLang(PDH_HQUERY q, LPCWSTR path, PDH_HCOUNTER* c)
{
    if (g_pPdhAddEnglishCounterW) {
        return g_pPdhAddEnglishCounterW(q, path, 0, c);
    }
    return PdhAddCounterW(q, path, 0, c);
}

// -----------------------------
// Metrics
// -----------------------------
static void InitCpuPdh()
{
    InitPdhEnglish();
    if (PdhOpenQuery(nullptr, 0, &g_cpuQuery) != ERROR_SUCCESS) return;

    // Prefer "% Processor Utility" (closer to Task Manager overall CPU) then fallback to "% Processor Time"
    LPCWSTR kPathUtility = L"\\Processor Information(_Total)\\% Processor Utility";
    LPCWSTR kPathLegacy = L"\\Processor(_Total)\\% Processor Time";

    PDH_STATUS st = AddCounterAnyLang(g_cpuQuery, kPathUtility, &g_cpuTotal);
    if (st != ERROR_SUCCESS) {
        st = AddCounterAnyLang(g_cpuQuery, kPathLegacy, &g_cpuTotal);
    }
    if (st == ERROR_SUCCESS) {
        PdhCollectQueryData(g_cpuQuery);
    }
    else {
        // leave g_cpuTotal null; ReadCpuUsage will return -1
        g_cpuTotal = nullptr;
    }
}

static double ReadCpuUsage()
{
    if (!g_cpuQuery || !g_cpuTotal) return -1.0;
    PdhCollectQueryData(g_cpuQuery);
    PDH_FMT_COUNTERVALUE v{}; DWORD type = 0;
    if (PdhGetFormattedCounterValue(g_cpuTotal, PDH_FMT_DOUBLE, &type, &v) == ERROR_SUCCESS) return v.doubleValue;
    return -1.0;
}

static double ReadRamUsedPercent()
{
    MEMORYSTATUSEX ms = { sizeof(ms) };
    if (GlobalMemoryStatusEx(&ms)) {
        double used = (double)(ms.ullTotalPhys - ms.ullAvailPhys);
        return (used / (double)ms.ullTotalPhys) * 100.0;
    }
    return -1.0;
}

// -----------------------------
// Drawing helpers
// -----------------------------
static void FillRoundRect(HDC hdc, RECT r, int radius, COLORREF c)
{
    HBRUSH br = CreateSolidBrush(c);
    HPEN   pn = CreatePen(PS_NULL, 0, 0);
    HGDIOBJ oldB = SelectObject(hdc, br);
    HGDIOBJ oldP = SelectObject(hdc, pn);
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(hdc, oldB); SelectObject(hdc, oldP);
    DeleteObject(br); DeleteObject(pn);
}

static void DrawTextW2(HDC hdc, HFONT f, COLORREF c, int x, int y, const wchar_t* s)
{
    HGDIOBJ old = SelectObject(hdc, f);
    SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, c);
    TextOutW(hdc, x, y, s, (int)wcslen(s));
    SelectObject(hdc, old);
}

// Draw a thick arc using a geometric pen and polyline approximation.
static void DrawArc(HDC hdc, int cx, int cy, int radius, double startDeg, double sweepDeg, COLORREF color, int thickness)
{
    LOGBRUSH lb{ BS_SOLID, color, 0 };
    HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_ENDCAP_ROUND | PS_JOIN_ROUND, thickness, &lb, 0, nullptr);
    HGDIOBJ oldP = SelectObject(hdc, pen);

    const int STEPS = 180;
    std::vector<POINT> pts;
    pts.reserve(STEPS + 1);
    double startRad = startDeg * 3.141592653589793 / 180.0;
    double stepRad = (sweepDeg / STEPS) * 3.141592653589793 / 180.0;
    for (int i = 0; i <= STEPS; ++i) {
        double ang = startRad + i * stepRad;
        int x = cx + (int)lround(radius * cos(ang));
        int y = cy + (int)lround(radius * sin(ang));
        pts.push_back(POINT{ x, y });
    }
    if (!pts.empty()) Polyline(hdc, pts.data(), (int)pts.size());

    SelectObject(hdc, oldP);
    DeleteObject(pen);
}

static void DrawGauge(HDC hdc, int x, int y, int size, const wchar_t* title, double value, COLORREF accent)
{
    RECT card{ x, y, x + size, y + size };
    FillRoundRect(hdc, card, 16, C_CARD);

    int cx = x + size / 2;
    int cy = y + size / 2 + 8;
    int radius = (size / 2) - 26;
    int thickness = (int)(size * 0.12); // thickness relative to size
    if (thickness < 12) thickness = 12;

    // Background ring
    DrawArc(hdc, cx, cy, radius, -90, 360, RGB(55, 60, 68), thickness);

    // Foreground ring from top (-90 deg)
    double v = value; if (v < 0) v = 0; if (v > 100) v = 100;
    DrawArc(hdc, cx, cy, radius, -90, 360.0 * (v / 100.0), accent, thickness);

    // Text
    EnsureFonts();
    DrawTextW2(hdc, g_hFontBody, C_MUT, x + 16, y + 12, title);

    wchar_t vbuf[64];
    if (value < 0) StringCchCopyW(vbuf, 64, L"N/A");
    else StringCchPrintfW(vbuf, 64, L"%.1f%%", value);
    SIZE ts{};
    HGDIOBJ old = SelectObject(hdc, g_hFontTitle);
    GetTextExtentPoint32W(hdc, vbuf, (int)wcslen(vbuf), &ts);
    SetTextColor(hdc, C_TEXT); SetBkMode(hdc, TRANSPARENT);
    TextOutW(hdc, cx - ts.cx / 2, cy - ts.cy / 2, vbuf, (int)wcslen(vbuf));
    SelectObject(hdc, old);
}

// -----------------------------
// Tray
// -----------------------------
static void AddTrayIcon(HWND hWnd)
{
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = hWnd; g_nid.uID = TRAY_ICON_ID;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    StringCchCopyW(g_nid.szTip, ARRAYSIZE(g_nid.szTip), L"SysMonTray running - double-click to restore");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}
static void RemoveTrayIcon() { if (g_nid.hWnd) Shell_NotifyIconW(NIM_DELETE, &g_nid); }

// -----------------------------
// Painting & Sampling
// -----------------------------
static void PaintUI(HDC hdc)
{
    RECT rc; GetClientRect(g_hWnd, &rc);
    HBRUSH bg = CreateSolidBrush(C_BG); FillRect(hdc, &rc, bg); DeleteObject(bg);

    EnsureFonts();
    const int pad = 16;
    const wchar_t* title = L"SysMonTray - CPU & RAM";

    // Measure title height to reserve exact space
    SIZE titleSz{};
    HGDIOBJ old = SelectObject(hdc, g_hFontTitle);
    GetTextExtentPoint32W(hdc, title, (int)wcslen(title), &titleSz);
    SelectObject(hdc, old);
    int yTop = pad + titleSz.cy + 8;

    DrawTextW2(hdc, g_hFontTitle, C_TEXT, pad, pad, title);

    const int clientW = rc.right - rc.left;
    const int clientH = rc.bottom - rc.top;

    // Compute the largest square gauge size that fits exactly in remaining space
    // [pad][g][pad][g][pad] horizontally; vertically: [yTop][g][pad]
    int maxWPerGauge = (clientW - pad * 3) / 2;
    int maxH = (clientH - yTop - pad);
    int gaugeSize = (maxWPerGauge < maxH) ? maxWPerGauge : maxH;
    if (gaugeSize < 1) gaugeSize = 1;

    // Center the gauges in the available rectangle (clientW x (clientH - yTop))
    int totalWidth = gaugeSize * 2 + pad;
    int startX = (clientW - totalWidth) / 2;
    if (startX < pad) startX = pad;

    int availableH = clientH - yTop - pad;
    int startY = yTop + (availableH - gaugeSize) / 2;
    if (startY < yTop) startY = yTop;

    DrawGauge(hdc, startX, startY, gaugeSize, L"CPU Usage", g_cpu, C_ACC1);
    DrawGauge(hdc, startX + gaugeSize + pad, startY, gaugeSize, L"RAM Used", g_ram, C_ACC2);
}

static void TakeSample()
{
    double rawCpu = ReadCpuUsage();
    if (g_cpuEMA < 0) g_cpuEMA = rawCpu;
    else g_cpuEMA = 0.4 * rawCpu + 0.6 * g_cpuEMA;
    g_cpu = g_cpuEMA;

    g_ram = ReadRamUsedPercent();
    InvalidateRect(g_hWnd, nullptr, FALSE);
}

// -----------------------------
// Window proc
// -----------------------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        InitCpuPdh();
        SetTimer(hWnd, TIMER_ID_SAMPLE, SAMPLE_INTERVAL_MS, nullptr);
        return 0;

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) { AddTrayIcon(hWnd); ShowWindow(hWnd, SW_HIDE); }
        else { InvalidateRect(hWnd, nullptr, TRUE); }
        return 0;

    case WM_TRAYICON:
        if (LOWORD(lParam) == WM_LBUTTONDBLCLK || LOWORD(lParam) == WM_LBUTTONUP) {
            ShowWindow(hWnd, SW_SHOW); ShowWindow(hWnd, SW_RESTORE); SetForegroundWindow(hWnd); RemoveTrayIcon();
        }
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_ID_SAMPLE) { TakeSample(); }
        return 0;

    case WM_ERASEBKGND: return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        PaintUI(hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_CLOSE: DestroyWindow(hWnd); return 0;
    case WM_DESTROY:
        KillTimer(hWnd, TIMER_ID_SAMPLE);
        RemoveTrayIcon();
        if (g_cpuQuery) { PdhCloseQuery(g_cpuQuery); g_cpuQuery = nullptr; }
        if (g_hFontTitle) { DeleteObject(g_hFontTitle); g_hFontTitle = nullptr; }
        if (g_hFontBody) { DeleteObject(g_hFontBody);  g_hFontBody = nullptr; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// -----------------------------
// WinMain
// -----------------------------
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    g_hInst = hInstance;
    EnsureFonts();

    const wchar_t* kCls = L"SysMonTrayWndClass";
    WNDCLASSEX wc{ sizeof(WNDCLASSEX) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kCls;
    RegisterClassEx(&wc);

    // Fixed size stylish window
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX; // no resize/maximize
    g_hWnd = CreateWindowExW(WS_EX_APPWINDOW, kCls, L"SysMonTray - CPU & RAM Gauges",
        style, CW_USEDEFAULT, CW_USEDEFAULT, 700, 360, nullptr, nullptr, hInstance, nullptr);

    if (!g_hWnd) return 0;
    ShowWindow(g_hWnd, nCmdShow); UpdateWindow(g_hWnd);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

/*
Build notes (VS2022):
- New -> Windows Desktop Application -> Empty Project -> Add SysMonTray.cpp
- Properties (All Configurations, x64 recommended)
  - C/C++ -> Language -> C++ Language Standard: /std:c++17 (or later)
  - General -> Character Set: Use Unicode Character Set
  - Linker -> Input -> Additional Dependencies: pdh.lib; comctl32.lib
- Subsystem: Windows (wWinMain)

Notes:
- CPU: prefers "\Processor Information(_Total)\% Processor Utility" (via PdhAddEnglishCounterW if available),
        falls back to "\Processor(_Total)\% Processor Time".
- RAM: (Total - Available) / Total * 100.
- Gauges auto-fit width/height under the title with consistent padding.
*/
