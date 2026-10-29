#pragma execution_character_set("utf-8")
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cwchar>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "/ENTRY:wWinMainCRTStartup")

// ID нашего ресурса
#define IDR_DLAUNCHER 101

// ============================================================
// ГЛОБАЛЫ
// ============================================================
HINSTANCE g_hInst = nullptr;
HWND g_hWnd = nullptr;

int g_state = 0; // 0 = Приветствие, 1 = Установка, 2 = Готово
int g_progress = 0;
std::wstring g_installPath;
bool g_launchAfterInstall = true;

// Цвета
const COLORREF BG = RGB(24, 24, 28);
const COLORREF BG_DARK = RGB(16, 16, 20);
const COLORREF ACCENT = RGB(80, 180, 80); // Minecraft зеленый
const COLORREF TEXT = RGB(240, 240, 240);
const COLORREF TEXT_DIM = RGB(150, 150, 150);

// ============================================================
// УТИЛИТЫ
// ============================================================
void FillRectC(HDC dc, RECT rc, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &rc, b);
    DeleteObject(b);
}

void DrawTextCentered(HDC dc, const wchar_t* t, RECT rc, COLORREF c, int fs, bool b) {
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, c);
    HFONT f = CreateFontW(fs, 0, 0, 0, b ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT o = (HFONT)SelectObject(dc, f);
    DrawTextW(dc, t, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, o); DeleteObject(f);
}

void DrawTextLeft(HDC dc, const wchar_t* t, RECT rc, COLORREF c, int fs, bool b) {
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, c);
    HFONT f = CreateFontW(fs, 0, 0, 0, b ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT o = (HFONT)SelectObject(dc, f);
    DrawTextW(dc, t, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, o); DeleteObject(f);
}

// Создание ярлыка
void CreateShortcut(const std::wstring& target, const std::wstring& linkPath, const std::wstring& desc) {
    CoInitialize(nullptr);
    IShellLinkW* psl;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (LPVOID*)&psl))) {
        psl->SetPath(target.c_str());
        psl->SetDescription(desc.c_str());
        IPersistFile* ppf;
        if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf))) {
            ppf->Save(linkPath.c_str(), TRUE);
            ppf->Release();
        }
        psl->Release();
    }
    CoUninitialize();
}

// Извлечение файла из ресурсов
bool ExtractResource(int resId, const std::wstring& outPath) {
    HRSRC hRes = FindResourceW(g_hInst, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!hRes) return false;
    HGLOBAL hData = LoadResource(g_hInst, hRes);
    if (!hData) return false;
    void* pData = LockResource(hData);
    DWORD size = SizeofResource(g_hInst, hRes);
    if (!pData || size == 0) return false;

    HANDLE hFile = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    DWORD written = 0;
    WriteFile(hFile, pData, size, &written, nullptr);
    CloseHandle(hFile);
    return (written == size);
}

// Процесс установки (в отдельном потоке, чтобы окно не зависало)
DWORD WINAPI InstallThread(LPVOID) {
    Sleep(500);
    g_progress = 10; InvalidateRect(g_hWnd, nullptr, FALSE);
    
    // 1. Создаем папку
    wchar_t appData[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData);
    g_installPath = std::wstring(appData) + L"\\Dlauncher";
    CreateDirectoryW(g_installPath.c_str(), nullptr);
    
    Sleep(500);
    g_progress = 30; InvalidateRect(g_hWnd, nullptr, FALSE);

    // 2. Извлекаем exe
    std::wstring exePath = g_installPath + L"\\Dlauncher.exe";
    ExtractResource(IDR_DLAUNCHER, exePath);

    Sleep(800);
    g_progress = 60; InvalidateRect(g_hWnd, nullptr, FALSE);

    // 3. Создаем ярлыки
    wchar_t desktop[MAX_PATH], startMenu[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desktop);
    SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, startMenu);

    CreateShortcut(exePath, std::wstring(desktop) + L"\\Dlauncher.lnk", L"Dlauncher Game Launcher");
    CreateShortcut(exePath, std::wstring(startMenu) + L"\\Dlauncher.lnk", L"Dlauncher Game Launcher");

    Sleep(500);
    g_progress = 80; InvalidateRect(g_hWnd, nullptr, FALSE);

    // 4. Запись в реестр (Uninstall)
    HKEY hKey;
    std::wstring regPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Dlauncher";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, regPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"DisplayName", 0, REG_SZ, (const BYTE*)L"Dlauncher", sizeof(L"Dlauncher"));
        RegSetValueExW(hKey, L"UninstallString", 0, REG_SZ, (const BYTE*)exePath.c_str(), (DWORD)((exePath.length()+1)*sizeof(wchar_t)));
        RegSetValueExW(hKey, L"DisplayIcon", 0, REG_SZ, (const BYTE*)exePath.c_str(), (DWORD)((exePath.length()+1)*sizeof(wchar_t)));
        RegSetValueExW(hKey, L"Publisher", 0, REG_SZ, (const BYTE*)L"Dlauncher Team", sizeof(L"Dlauncher Team"));
        RegCloseKey(hKey);
    }

    Sleep(500);
    g_progress = 100; InvalidateRect(g_hWnd, nullptr, FALSE);
    Sleep(500);

    g_state = 2;
    InvalidateRect(g_hWnd, nullptr, TRUE);

    if (g_launchAfterInstall) {
        Sleep(1000);
        ShellExecuteW(nullptr, L"open", exePath.c_str(), nullptr, nullptr, SW_SHOW);
        PostQuitMessage(0);
    }

    return 0;
}

// ============================================================
// ОКНО УСТАНОВЩИКА
// ============================================================
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    static int hoverBtn = 0;
    static int hoverCheck = 0;

    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);

        // Фон
        FillRectC(dc, rc, BG);
        
        // Левая панель (декор)
        RECT left = { 0, 0, 200, rc.bottom };
        FillRectC(dc, left, BG_DARK);

        // Заголовок в левой панели
        DrawTextCentered(dc, L"Dlauncher", { 0, 40, 200, 80 }, ACCENT, 24, true);
        DrawTextCentered(dc, L"Setup", { 0, 80, 200, 110 }, TEXT_DIM, 14, false);

        // Правая часть (контент)
        RECT content = { 220, 0, rc.right, rc.bottom };

        if (g_state == 0) {
            DrawTextLeft(dc, L"Установка Dlauncher", { content.left, 60, content.right, 100 }, TEXT, 22, true);
            
            RECT pathRc = { content.left, 120, content.right - 20, 160 };
            FillRectC(dc, pathRc, BG_DARK);
            DrawTextLeft(dc, g_installPath.c_str(), { pathRc.left + 10, pathRc.top, pathRc.right, pathRc.bottom }, TEXT_DIM, 12, false);

            // Кнопка "Установить"
            RECT btn = { content.left, 250, content.left + 160, 290 };
            FillRectC(dc, btn, hoverBtn ? RGB(100, 200, 100) : ACCENT);
            DrawTextCentered(dc, L"Установить", btn, RGB(255,255,255), 16, true);

        } else if (g_state == 1) {
            DrawTextLeft(dc, L"Установка...", { content.left, 60, content.right, 100 }, TEXT, 22, true);
            
            // Прогресс-бар
            RECT pBarBg = { content.left, 150, content.right - 40, 170 };
            FillRectC(dc, pBarBg, BG_DARK);
            RECT pBarFg = { content.left, 150, content.left + ((content.right - 40 - content.left) * g_progress / 100), 170 };
            FillRectC(dc, pBarFg, ACCENT);

            wchar_t buf[32];
            swprintf_s(buf, L"%d%%", g_progress);
            DrawTextLeft(dc, buf, { content.left, 180, content.right, 210 }, TEXT_DIM, 12, false);

        } else if (g_state == 2) {
            DrawTextLeft(dc, L"Установка завершена!", { content.left, 60, content.right, 100 }, ACCENT, 22, true);
            DrawTextLeft(dc, L"Dlauncher успешно установлен на ваш компьютер.", { content.left, 120, content.right, 150 }, TEXT, 14, false);

            // Кнопка "Завершить"
            RECT btn = { content.left, 250, content.left + 160, 290 };
            FillRectC(dc, btn, hoverBtn ? RGB(100, 200, 100) : ACCENT);
            DrawTextCentered(dc, L"Завершить", btn, RGB(255,255,255), 16, true);
        }

        // Чекбокс (если не идет установка)
        if (g_state != 1) {
            RECT cb = { content.left, 210, content.left + 20, 230 };
            FillRectC(dc, cb, hoverCheck ? RGB(80,80,80) : BG_DARK);
            HBRUSH br = CreateSolidBrush(TEXT_DIM); FrameRect(dc, &cb, br); DeleteObject(br);
            if (g_launchAfterInstall) {
                RECT in = { cb.left + 3, cb.top + 3, cb.right - 3, cb.bottom - 3 };
                FillRectC(dc, in, ACCENT);
            }
            DrawTextLeft(dc, L"Запустить Dlauncher", { cb.right + 10, cb.top, content.right, cb.bottom }, TEXT, 13, false);
        }

        EndPaint(h, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = LOWORD(l), y = HIWORD(l);
        RECT rc; GetClientRect(h, &rc);
        RECT content = { 220, 0, rc.right, rc.bottom };
        RECT btn = { content.left, 250, content.left + 160, 290 };
        RECT cb = { content.left, 210, content.left + 20, 230 };

        auto ins = [](RECT r, int px, int py) { return px >= r.left && px <= r.right && py >= r.top && py <= r.bottom; };
        
        int hb = ins(btn, x, y);
        int hc = ins(cb, x, y);

        if (hb != hoverBtn || hc != hoverCheck) {
            hoverBtn = hb; hoverCheck = hc;
            InvalidateRect(h, nullptr, FALSE);
        }
        if (hb || hc) SetCursor(LoadCursor(nullptr, IDC_HAND));
        else SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = LOWORD(l), y = HIWORD(l);
        RECT rc; GetClientRect(h, &rc);
        RECT content = { 220, 0, rc.right, rc.bottom };
        RECT btn = { content.left, 250, content.left + 160, 290 };
        RECT cb = { content.left, 210, content.left + 20, 230 };

        auto ins = [](RECT r, int px, int py) { return px >= r.left && px <= r.right && py >= r.top && py <= r.bottom; };

        if (ins(cb, x, y)) {
            g_launchAfterInstall = !g_launchAfterInstall;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }

        if (ins(btn, x, y)) {
            if (g_state == 0) {
                g_state = 1;
                InvalidateRect(h, nullptr, TRUE);
                CreateThread(nullptr, 0, InstallThread, nullptr, 0, nullptr);
            } else if (g_state == 2) {
                PostQuitMessage(0);
            }
            return 0;
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ============================================================
// ТОЧКА ВХОДА
// ============================================================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    g_hInst = hInst;

    // Задаем начальный путь
    wchar_t appData[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData);
    g_installPath = std::wstring(appData) + L"\\Dlauncher";

    WNDCLASSW wc = {};
    wc.hInstance = hInst;
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = L"SetupWnd";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    RegisterClassW(&wc);

    // Создаем окно (фиксированный размер)
    int w = 600, h = 360;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    
    // Убираем стандартную рамку и заголовок для стиля
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU; // Если хочешь совсем без рамки, убери WS_CAPTION
    g_hWnd = CreateWindowExW(0, L"SetupWnd", L"Dlauncher Setup", style, (sw-w)/2, (sh-h)/2, w, h, nullptr, nullptr, hInst, nullptr);
    
    ShowWindow(g_hWnd, SW_SHOW);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
