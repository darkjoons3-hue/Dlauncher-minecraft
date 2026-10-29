#pragma execution_character_set("utf-8")
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <urlmon.h> // Для скачивания файлов
#include <shlobj.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cwchar>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "/ENTRY:wWinMainCRTStartup")

// ============================================================
// НАСТРОЙКИ ЗАГРУЗКИ (ССЫЛКА НА ПОРТАТИВНЫЙ MINECRAFT)
// ============================================================
// Прямая ссылка на PortableTL-latest.zip с GitHub
const wchar_t* GAME_ZIP_URL = L"https://github.com/moiSentineL/portabletl/releases/download/latest/PortableTL-latest.zip";
const wchar_t* GAME_ZIP_NAME = L"game.zip";

// ============================================================
// СОСТОЯНИЯ ПРИЛОЖЕНИЯ
// ============================================================
enum AppState {
    STATE_LOGIN = 0,       // Окно входа в Google
    STATE_CREATE_ACC = 1,  // Окно создания профиля
    STATE_MAIN_MENU = 2,   // Главное меню (Играть)
    STATE_DOWNLOADING = 3, // Процесс скачивания
    STATE_READY = 4        // Игра готова
};

HINSTANCE g_hInst = nullptr;
HWND g_hWnd = nullptr;
HWND g_hEdit = nullptr;
AppState g_state = STATE_LOGIN;
int g_downloadProgress = 0;
std::wstring g_username = L"";
std::wstring g_installDir = L"";

// Цвета
const COLORREF BG = RGB(20, 20, 24);
const COLORREF BG_PANEL = RGB(30, 30, 36);
const COLORREF ACCENT = RGB(80, 180, 80); // Minecraft зеленый
const COLORREF TEXT = RGB(240, 240, 240);
const COLORREF TEXT_DIM = RGB(120, 120, 120);

// ============================================================
// УТИЛИТЫ ОТРИСОВКИ
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

void DrawCustomButton(HDC dc, RECT rc, const wchar_t* t, bool hover, bool accent) {
    COLORREF bg = accent ? (hover ? RGB(100, 200, 100) : ACCENT) : (hover ? RGB(50, 50, 60) : BG_PANEL);
    FillRectC(dc, rc, bg);
    DrawTextCentered(dc, t, rc, accent ? RGB(255,255,255) : TEXT, 16, true);
}

// ============================================================
// ПОТОК СКАЧИВАНИЯ И РАСПАКОВКИ
// ============================================================
DWORD WINAPI DownloadThread(LPVOID) {
    g_downloadProgress = 5;
    InvalidateRect(g_hWnd, nullptr, FALSE);

    // 1. Скачиваем архив
    std::wstring zipPath = g_installDir + L"\\" + GAME_ZIP_NAME;
    HRESULT hr = URLDownloadToFileW(nullptr, GAME_ZIP_URL, zipPath.c_str(), 0, nullptr);
    
    if (SUCCEEDED(hr)) {
        g_downloadProgress = 50;
        InvalidateRect(g_hWnd, nullptr, FALSE);
        Sleep(500);

        // 2. Распаковываем через встроенный tar.exe (Windows 10/11)
        // Флаг -xf означает "извлечь из файла"
        std::wstring cmd = L"tar.exe -xf \"" + zipPath + L"\" -C \"" + g_installDir + L"\"";
        STARTUPINFOW si = { sizeof(si) }; 
        si.dwFlags = STARTF_USESHOWWINDOW; 
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi;
        
        if (CreateProcessW(nullptr, (LPWSTR)cmd.c_str(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess); 
            CloseHandle(pi.hThread);
        }

        g_downloadProgress = 90;
        InvalidateRect(g_hWnd, nullptr, FALSE);
        Sleep(800);

        // 3. Удаляем архив, он больше не нужен
        DeleteFileW(zipPath.c_str());

        g_downloadProgress = 100;
        InvalidateRect(g_hWnd, nullptr, FALSE);
        Sleep(500);
        g_state = STATE_READY;
        InvalidateRect(g_hWnd, nullptr, TRUE);
    } else {
        MessageBoxW(g_hWnd, L"Ошибка скачивания. Проверьте подключение к интернету.", L"Ошибка", MB_OK | MB_ICONERROR);
        g_state = STATE_MAIN_MENU;
        InvalidateRect(g_hWnd, nullptr, TRUE);
    }
    return 0;
}

// ============================================================
// ЗАПУСК ИГРЫ
// ============================================================
void LaunchGame() {
    // Ищем исполняемый файл внутри распакованной папки
    std::wstring launcherPath = g_installDir + L"\\TLauncher.exe";
    
    // Если TLauncher.exe нет, пробуем PortableTL.exe (зависит от версии сборки)
    if (GetFileAttributesW(launcherPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        launcherPath = g_installDir + L"\\PortableTL.exe";
    }

    // Запускаем лаунчер
    HINSTANCE result = ShellExecuteW(nullptr, L"open", launcherPath.c_str(), nullptr, g_installDir.c_str(), SW_SHOW);
    
    if ((INT_PTR)result <= 32) {
        MessageBoxW(g_hWnd, L"Не удалось запустить игру. Файлы повреждены.", L"Ошибка", MB_OK | MB_ICONERROR);
    }
}

// ============================================================
// ОКНО
// ============================================================
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    static int hoverBtn = 0;
    switch (m) {
    case WM_CREATE:
        g_hWnd = h;
        // Создаем скрытое поле ввода для никнейма
        g_hEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_BORDER | ES_CENTER, 200, 200, 200, 30, h, (HMENU)100, g_hInst, nullptr);
        {
            HFONT f = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)f, TRUE);
            
            // Создаем папку для игры
            wchar_t appData[MAX_PATH];
            SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData);
            g_installDir = std::wstring(appData) + L"\\Dlauncher\\game";
            CreateDirectoryW((std::wstring(appData) + L"\\Dlauncher").c_str(), nullptr);
            CreateDirectoryW(g_installDir.c_str(), nullptr);
        }
        return 0;

    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w; SetTextColor(dc, TEXT); SetBkColor(dc, BG_PANEL);
        static HBRUSH br = CreateSolidBrush(BG_PANEL); return (LRESULT)br;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        FillRectC(dc, rc, BG);
        
        // Верхняя панель (заголовок)
        FillRectC(dc, { 0, 0, rc.right, 60 }, BG_PANEL);
        DrawTextCentered(dc, L"Dlauncher", { 0, 0, rc.right, 60 }, ACCENT, 24, true);
        
        if (g_state != STATE_CREATE_ACC) ShowWindow(g_hEdit, SW_HIDE);

        if (g_state == STATE_LOGIN) {
            DrawTextCentered(dc, L"Для продолжения войдите через аккаунт Google", { 0, 100, rc.right, 150 }, TEXT, 18, false);
            RECT btn = { rc.right/2 - 100, 200, rc.right/2 + 100, 250 };
            DrawCustomButton(dc, btn, L"Войти через Google", hoverBtn, true);
            DrawTextCentered(dc, L"Ваши данные защищены", { 0, 300, rc.right, 320 }, TEXT_DIM, 12, false);
        }
        else if (g_state == STATE_CREATE_ACC) {
            DrawTextCentered(dc, L"Создание профиля", { 0, 100, rc.right, 150 }, TEXT, 22, true);
            DrawTextCentered(dc, L"Введите ваш игровой никнейм:", { 0, 160, rc.right, 190 }, TEXT_DIM, 16, false);
            ShowWindow(g_hEdit, SW_SHOW); SetWindowPos(g_hEdit, HWND_TOP, rc.right/2 - 100, 200, 200, 35, SWP_NOACTIVATE);
            RECT btn = { rc.right/2 - 100, 260, rc.right/2 + 100, 310 };
            DrawCustomButton(dc, btn, L"Создать аккаунт", hoverBtn, true);
        }
        else if (g_state == STATE_MAIN_MENU) {
            FillRectC(dc, { 0, 60, 200, rc.bottom }, BG_PANEL);
            DrawTextCentered(dc, L"Играть", { 0, 100, 200, 140 }, ACCENT, 18, true);
            DrawTextCentered(dc, L"Настройки", { 0, 140, 200, 180 }, TEXT_DIM, 18, false);
            
            DrawTextCentered(dc, (L"Добро пожаловать, " + g_username).c_str(), { 220, 80, rc.right, 120 }, TEXT, 20, true);
            DrawTextCentered(dc, L"Minecraft 1.12.2", { 220, 150, rc.right, 190 }, TEXT, 18, false);
            DrawTextCentered(dc, L"Состояние: Не установлено", { 220, 190, rc.right, 220 }, TEXT_DIM, 14, false);
            
            RECT btn = { 220, 250, 420, 300 };
            DrawCustomButton(dc, btn, L"Скачать и играть", hoverBtn, true);
        }
        else if (g_state == STATE_DOWNLOADING) {
            DrawTextCentered(dc, L"Загрузка Minecraft 1.12.2...", { 0, 100, rc.right, 140 }, TEXT, 20, true);
            DrawTextCentered(dc, L"Пожалуйста, подождите. Скачивание библиотек и клиента (~160 МБ).", { 0, 150, rc.right, 180 }, TEXT_DIM, 14, false);
            
            RECT pbBg = { 100, 220, rc.right - 100, 240 }; FillRectC(dc, pbBg, BG_PANEL);
            RECT pbFg = { 100, 220, 100 + ((rc.right - 200) * g_downloadProgress / 100), 240 }; FillRectC(dc, pbFg, ACCENT);
            
            wchar_t buf[32]; swprintf_s(buf, L"%d%%", g_downloadProgress);
            DrawTextCentered(dc, buf, { 0, 250, rc.right, 270 }, TEXT, 14, false);
        }
        else if (g_state == STATE_READY) {
            DrawTextCentered(dc, L"Minecraft 1.12.2 готов к запуску!", { 0, 100, rc.right, 140 }, ACCENT, 20, true);
            RECT btn = { rc.right/2 - 100, 200, rc.right/2 + 100, 260 };
            DrawCustomButton(dc, btn, L"▶ ИГРАТЬ", hoverBtn, true);
        }
        EndPaint(h, &ps); return 0;
    }

    case WM_MOUSEMOVE: {
        int x = LOWORD(l), y = HIWORD(l); RECT rc; GetClientRect(h, &rc);
        int hb = 0;
        if (g_state == STATE_LOGIN) { RECT r = { rc.right/2 - 100, 200, rc.right/2 + 100, 250 }; hb = (x>=r.left&&x<=r.right&&y>=r.top&&y<=r.bottom); }
        else if (g_state == STATE_CREATE_ACC) { RECT r = { rc.right/2 - 100, 260, rc.right/2 + 100, 310 }; hb = (x>=r.left&&x<=r.right&&y>=r.top&&y<=r.bottom); }
        else if (g_state == STATE_MAIN_MENU) { RECT r = { 220, 250, 420, 300 }; hb = (x>=r.left&&x<=r.right&&y>=r.top&&y<=r.bottom); }
        else if (g_state == STATE_READY) { RECT r = { rc.right/2 - 100, 200, rc.right/2 + 100, 260 }; hb = (x>=r.left&&x<=r.right&&y>=r.top&&y<=r.bottom); }
        
        if (hb != hoverBtn) { hoverBtn = hb; InvalidateRect(h, nullptr, FALSE); }
        if (hb) SetCursor(LoadCursor(nullptr, IDC_HAND)); else SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = LOWORD(l), y = HIWORD(l); RECT rc; GetClientRect(h, &rc);
        auto ins = [](RECT r, int px, int py) { return px >= r.left && px <= r.right && py >= r.top && py <= r.bottom; };
        
        if (g_state == STATE_LOGIN) {
            RECT btn = { rc.right/2 - 100, 200, rc.right/2 + 100, 250 };
            if (ins(btn, x, y)) { 
                // Открываем браузер (имитация входа)
                ShellExecuteW(nullptr, L"open", L"https://accounts.google.com/", nullptr, nullptr, SW_SHOW); 
                Sleep(3000); 
                g_state = STATE_CREATE_ACC; 
                InvalidateRect(h, nullptr, TRUE); 
            }
        }
        else if (g_state == STATE_CREATE_ACC) {
            RECT btn = { rc.right/2 - 100, 260, rc.right/2 + 100, 310 };
            if (ins(btn, x, y)) {
                wchar_t buf[64] = {}; GetWindowTextW(g_hEdit, buf, 64);
                if (wcslen(buf) > 0) { 
                    g_username = buf; 
                    g_state = STATE_MAIN_MENU; 
                    InvalidateRect(h, nullptr, TRUE); 
                }
            }
        }
        else if (g_state == STATE_MAIN_MENU) {
            RECT btn = { 220, 250, 420, 300 };
            if (ins(btn, x, y)) { 
                g_state = STATE_DOWNLOADING; 
                InvalidateRect(h, nullptr, TRUE); 
                CreateThread(nullptr, 0, DownloadThread, nullptr, 0, nullptr); 
            }
        }
        else if (g_state == STATE_READY) {
            RECT btn = { rc.right/2 - 100, 200, rc.right/2 + 100, 260 };
            if (ins(btn, x, y)) { LaunchGame(); }
        }
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ============================================================
// ТОЧКА ВХОДА
// ============================================================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    g_hInst = hInst;
    WNDCLASSW wc = {}; 
    wc.hInstance = hInst; 
    wc.lpfnWndProc = WndProc; 
    wc.lpszClassName = L"DlauncherWnd"; 
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    
    int w = 800, h = 500, sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    g_hWnd = CreateWindowExW(0, L"DlauncherWnd", L"Dlauncher", WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, (sw-w)/2, (sh-h)/2, w, h, nullptr, nullptr, hInst, nullptr);
    ShowWindow(g_hWnd, SW_SHOW); 
    UpdateWindow(g_hWnd);
    
    MSG msg; 
    while (GetMessageW(&msg, nullptr, 0, 0)) { 
        TranslateMessage(&msg); 
        DispatchMessageW(&msg); 
    }
    return 0;
}
