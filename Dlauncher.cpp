#pragma execution_character_set("utf-8")
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <urlmon.h>
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
// НАСТРОЙКИ ЗАГРУЗКИ
// ============================================================
const wchar_t* GAME_ZIP_URL = L"https://github.com/moiSentineL/portabletl/releases/download/latest/PortableTL-latest.zip";
const wchar_t* GAME_ZIP_NAME = L"game.zip";

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
int g_activeTab = 0; // 0 = Играть, 1 = Настройки

int g_ramValue = 2048; // От 1024 до 8192 МБ
bool g_showFpsOverlay = false; // Включен ли оверлей FPS
HWND g_fpsWnd = nullptr;

const COLORREF BG = RGB(20, 20, 24);
const COLORREF BG_PANEL = RGB(30, 30, 36);
const COLORREF ACCENT = RGB(80, 180, 80);
const COLORREF TEXT = RGB(240, 240, 240);
const COLORREF TEXT_DIM = RGB(120, 120, 120);

// ============================================================
// УТИЛИТЫ ОТРИСОВКИ
// ============================================================
void FillRectC(HDC dc, RECT rc, COLORREF c) { HBRUSH b = CreateSolidBrush(c); FillRect(dc, &rc, b); DeleteObject(b); }
void DrawTextCentered(HDC dc, const wchar_t* t, RECT rc, COLORREF c, int fs, bool b) {
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, c);
    HFONT f = CreateFontW(fs, 0, 0, 0, b ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT o = (HFONT)SelectObject(dc, f); DrawTextW(dc, t, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, o); DeleteObject(f);
}
void DrawTextLeft(HDC dc, const wchar_t* t, RECT rc, COLORREF c, int fs, bool b) {
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, c);
    HFONT f = CreateFontW(fs, 0, 0, 0, b ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT o = (HFONT)SelectObject(dc, f); DrawTextW(dc, t, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, o); DeleteObject(f);
}
void DrawCustomButton(HDC dc, RECT rc, const wchar_t* t, bool hover, bool accent) {
    COLORREF bg = accent ? (hover ? RGB(100, 200, 100) : ACCENT) : (hover ? RGB(60, 60, 70) : BG_PANEL);
    FillRectC(dc, rc, bg);
    DrawTextCentered(dc, t, rc, accent ? RGB(255,255,255) : TEXT, 16, true);
}
void DrawSlider(HDC dc, RECT rc, int value, int minVal, int maxVal, bool hover) {
    int my = (rc.top + rc.bottom) / 2;
    RECT tr = { rc.left, my - 4, rc.right, my + 4 }; FillRectC(dc, tr, BG_PANEL);
    int pct = (value - minVal) * 100 / (maxVal - minVal);
    int fw = (rc.right - rc.left) * pct / 100;
    RECT fr = { rc.left, my - 4, rc.left + fw, my + 4 }; FillRectC(dc, fr, ACCENT);
    int tx = rc.left + fw, r = hover ? 12 : 10;
    HBRUSH tb = CreateSolidBrush(hover ? RGB(100, 200, 100) : ACCENT);
    HBRUSH o = (HBRUSH)SelectObject(dc, tb); HPEN op = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, tx - r, my - r, tx + r, my + r);
    SelectObject(dc, o); SelectObject(dc, op); DeleteObject(tb);
}

// ============================================================
// ОВЕРЛЕЙ FPS (Отдельное окно)
// ============================================================
LRESULT CALLBACK FpsWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(0, 255, 0)); // Ярко-зеленый, как в MSI Afterburner
        HFONT f = CreateFontW(18, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
        HFONT o = (HFONT)SelectObject(dc, f);
        DrawTextW(dc, L"FPS: 144", -1, &rc, DT_LEFT | DT_TOP | DT_SINGLELINE);
        SelectObject(dc, o); DeleteObject(f);
        EndPaint(h, &ps); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

DWORD WINAPI FpsThread(LPVOID) {
    WNDCLASSW wc = {}; wc.hInstance = g_hInst; wc.lpfnWndProc = FpsWndProc; wc.lpszClassName = L"FpsOverlayWnd";
    RegisterClassW(&wc);
    
    // Создаем прозрачное окно поверх всех
    g_fpsWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        L"FpsOverlayWnd", L"", WS_POPUP, 20, 20, 120, 30, nullptr, nullptr, g_hInst, nullptr);
    
    SetLayeredWindowAttributes(g_fpsWnd, RGB(0,0,0), 0, LWA_COLORKEY); // Делаем черный фон прозрачным
    ShowWindow(g_fpsWnd, SW_SHOW);
    
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    return 0;
}

// ============================================================
// СКАЧИВАНИЕ И ЗАПУСК
// ============================================================
DWORD WINAPI DownloadThread(LPVOID) {
    g_downloadProgress = 5; InvalidateRect(g_hWnd, nullptr, FALSE);
    std::wstring zipPath = g_installDir + L"\\" + GAME_ZIP_NAME;
    HRESULT hr = URLDownloadToFileW(nullptr, GAME_ZIP_URL, zipPath.c_str(), 0, nullptr);
    
    if (SUCCEEDED(hr)) {
        g_downloadProgress = 50; InvalidateRect(g_hWnd, nullptr, FALSE); Sleep(500);
        std::wstring cmd = L"tar.exe -xf \"" + zipPath + L"\" -C \"" + g_installDir + L"\"";
        STARTUPINFOW si = { sizeof(si) }; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi;
        if (CreateProcessW(nullptr, (LPWSTR)cmd.c_str(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        }
        g_downloadProgress = 90; InvalidateRect(g_hWnd, nullptr, FALSE); Sleep(800);
        DeleteFileW(zipPath.c_str());
        
        // Создаем папку mods по умолчанию, если её нет
        CreateDirectoryW((g_installDir + L"\\.minecraft\\mods").c_str(), nullptr);
        
        g_downloadProgress = 100; InvalidateRect(g_hWnd, nullptr, FALSE); Sleep(500);
        g_state = STATE_READY; InvalidateRect(g_hWnd, nullptr, TRUE);
    } else {
        MessageBoxW(g_hWnd, L"Ошибка скачивания. Проверьте интернет.", L"Ошибка", MB_OK | MB_ICONERROR);
        g_state = STATE_MAIN_MENU; InvalidateRect(g_hWnd, nullptr, TRUE);
    }
    return 0;
}

void LaunchGame() {
    std::wstring launcherPath = g_installDir + L"\\TLauncher.exe";
    if (GetFileAttributesW(launcherPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        launcherPath = g_installDir + L"\\PortableTL.exe";
    }
    ShellExecuteW(nullptr, L"open", launcherPath.c_str(), nullptr, g_installDir.c_str(), SW_SHOW);
}

// ============================================================
// ОКНО
// ============================================================
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    static int hoverBtn = 0, hoverSkip = 0, hoverTabPlay = 0, hoverTabSettings = 0;
    static int hoverSlider = 0, hoverBtnFolder = 0, hoverBtnMods = 0, hoverCheckFps = 0;

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
            RECT btnSkip = { rc.right/2 - 100, 270, rc.right/2 + 100, 310 };
            DrawCustomButton(dc, btnSkip, L"Пропустить (для теста)", hoverSkip, false);
        }
        else if (g_state == STATE_CREATE_ACC) {
            DrawTextCentered(dc, L"Создание профиля", { 0, 100, rc.right, 150 }, TEXT, 22, true);
            DrawTextCentered(dc, L"Введите ваш игровой никнейм:", { 0, 160, rc.right, 190 }, TEXT_DIM, 16, false);
            ShowWindow(g_hEdit, SW_SHOW); SetWindowPos(g_hEdit, HWND_TOP, rc.right/2 - 100, 200, 200, 35, SWP_NOACTIVATE);
            RECT btn = { rc.right/2 - 100, 260, rc.right/2 + 100, 310 };
            DrawCustomButton(dc, btn, L"Создать аккаунт", hoverBtn, true);
            RECT btnSkip = { rc.right/2 - 100, 330, rc.right/2 + 100, 370 };
            DrawCustomButton(dc, btnSkip, L"Пропустить (для теста)", hoverSkip, false);
        }
        else if (g_state == STATE_MAIN_MENU || g_state == STATE_DOWNLOADING || g_state == STATE_READY) {
            FillRectC(dc, { 0, 60, 200, rc.bottom }, BG_PANEL);
            RECT tabPlay = { 0, 100, 200, 140 };
            RECT tabSettings = { 0, 140, 200, 180 };
            
            DrawTextCentered(dc, L"Играть", tabPlay, (g_activeTab == 0) ? ACCENT : (hoverTabPlay ? TEXT : TEXT_DIM), 18, g_activeTab == 0);
            DrawTextCentered(dc, L"Настройки", tabSettings, (g_activeTab == 1) ? ACCENT : (hoverTabSettings ? TEXT : TEXT_DIM), 18, g_activeTab == 1);

            if (g_activeTab == 0) {
                DrawTextCentered(dc, (L"Добро пожаловать, " + g_username).c_str(), { 220, 80, rc.right, 120 }, TEXT, 20, true);
                RECT versionBox = { 220, 150, 450, 190 };
                FillRectC(dc, versionBox, BG_PANEL);
                DrawTextLeft(dc, L"Minecraft 1.12.2", { versionBox.left + 15, versionBox.top, versionBox.right - 40, versionBox.bottom }, TEXT, 16, false);
                DrawTextCentered(dc, L"▼", { versionBox.right - 30, versionBox.top, versionBox.right, versionBox.bottom }, TEXT_DIM, 12, false);
                
                if (g_state == STATE_MAIN_MENU) DrawTextLeft(dc, L"Состояние: Не установлено", { 220, 210, rc.right, 230 }, TEXT_DIM, 14, false);
                else if (g_state == STATE_READY) DrawTextLeft(dc, L"Состояние: Готово к запуску", { 220, 210, rc.right, 230 }, ACCENT, 14, false);

                RECT btn = { rc.right - 220, rc.bottom - 80, rc.right - 20, rc.bottom - 30 };
                if (g_state == STATE_MAIN_MENU) DrawCustomButton(dc, btn, L"Скачать и играть", hoverBtn, true);
                else if (g_state == STATE_READY) DrawCustomButton(dc, btn, L"▶ ИГРАТЬ", hoverBtn, true);
                else if (g_state == STATE_DOWNLOADING) {
                    RECT pbBg = { rc.right - 220, rc.bottom - 70, rc.right - 20, rc.bottom - 50 };
                    FillRectC(dc, pbBg, BG_PANEL);
                    RECT pbFg = { pbBg.left, pbBg.top, pbBg.left + ((pbBg.right - pbBg.left) * g_downloadProgress / 100), pbBg.bottom };
                    FillRectC(dc, pbFg, ACCENT);
                    wchar_t buf[32]; swprintf_s(buf, L"Загрузка: %d%%", g_downloadProgress);
                    DrawTextCentered(dc, buf, { rc.right - 220, rc.bottom - 45, rc.right - 20, rc.bottom - 20 }, TEXT, 12, false);
                }
            } else {
                // ВКЛАДКА НАСТРОЕК
                DrawTextCentered(dc, L"Настройки", { 220, 70, rc.right, 100 }, TEXT, 20, true);
                
                // Слайдер RAM
                DrawTextLeft(dc, L"Выделение памяти (RAM):", { 220, 120, rc.right, 140 }, TEXT_DIM, 14, false);
                wchar_t ramBuf[32]; swprintf_s(ramBuf, L"%d MB", g_ramValue);
                DrawTextLeft(dc, ramBuf, { 400, 120, rc.right, 140 }, ACCENT, 14, true);
                RECT slider = { 220, 150, 500, 170 };
                DrawSlider(dc, slider, g_ramValue, 1024, 8192, hoverSlider);

                // Кнопки папок
                RECT btnFolder = { 220, 210, 400, 245 };
                DrawCustomButton(dc, btnFolder, L"📂 Папка с игрой", hoverBtnFolder, false);
                
                RECT btnMods = { 420, 210, 600, 245 };
                DrawCustomButton(dc, btnMods, L"📂 Папка с модами", hoverBtnMods, false);

                // Чекбокс FPS
                RECT cbFps = { 220, 280, 240, 300 };
                FillRectC(dc, cbFps, hoverCheckFps ? RGB(60,60,70) : BG_PANEL);
                HBRUSH br = CreateSolidBrush(TEXT_DIM); FrameRect(dc, &cbFps, br); DeleteObject(br);
                if (g_showFpsOverlay) {
                    RECT in = { cbFps.left + 3, cbFps.top + 3, cbFps.right - 3, cbFps.bottom - 3 };
                    FillRectC(dc, in, ACCENT);
                }
                DrawTextLeft(dc, L"Включить оверлей FPS (поверх игры)", { 250, 280, rc.right, 300 }, TEXT, 14, false);
            }
        }
        EndPaint(h, &ps); return 0;
    }

    case WM_MOUSEMOVE: {
        int x = LOWORD(l), y = HIWORD(l); RECT rc; GetClientRect(h, &rc);
        int hb = 0, hs = 0, htp = 0, hts = 0, hsl = 0, hbf = 0, hbm = 0, hcf = 0;
        
        if (g_state == STATE_LOGIN) { 
            RECT r = { rc.right/2 - 100, 200, rc.right/2 + 100, 250 }; hb = (x>=r.left&&x<=r.right&&y>=r.top&&y<=r.bottom); 
            RECT rs = { rc.right/2 - 100, 270, rc.right/2 + 100, 310 }; hs = (x>=rs.left&&x<=rs.right&&y>=rs.top&&y<=rs.bottom);
        }
        else if (g_state == STATE_CREATE_ACC) { 
            RECT r = { rc.right/2 - 100, 260, rc.right/2 + 100, 310 }; hb = (x>=r.left&&x<=r.right&&y>=r.top&&y<=r.bottom); 
            RECT rs = { rc.right/2 - 100, 330, rc.right/2 + 100, 370 }; hs = (x>=rs.left&&x<=rs.right&&y>=rs.top&&y<=rs.bottom);
        }
        else if (g_state == STATE_MAIN_MENU || g_state == STATE_READY || g_state == STATE_DOWNLOADING) {
            RECT tabPlay = { 0, 100, 200, 140 };
            RECT tabSettings = { 0, 140, 200, 180 };
            htp = (x>=tabPlay.left&&x<=tabPlay.right&&y>=tabPlay.top&&y<=tabPlay.bottom);
            hts = (x>=tabSettings.left&&x<=tabSettings.right&&y>=tabSettings.top&&y<=tabSettings.bottom);

            if (g_activeTab == 0 && (g_state == STATE_MAIN_MENU || g_state == STATE_READY)) {
                RECT r = { rc.right - 220, rc.bottom - 80, rc.right - 20, rc.bottom - 30 }; 
                hb = (x>=r.left&&x<=r.right&&y>=r.top&&y<=r.bottom);
            } else if (g_activeTab == 1) {
                RECT r = { 220, 150, 500, 170 }; hsl = (x>=r.left&&x<=r.right&&y>=r.top-10&&y<=r.bottom+10);
                RECT bf = { 220, 210, 400, 245 }; hbf = (x>=bf.left&&x<=bf.right&&y>=bf.top&&y<=bf.bottom);
                RECT bm = { 420, 210, 600, 245 }; hbm = (x>=bm.left&&x<=bm.right&&y>=bm.top&&y<=bm.bottom);
                RECT cf = { 220, 280, 240, 300 }; hcf = (x>=cf.left&&x<=cf.right&&y>=cf.top&&y<=cf.bottom);
            }
        }
        
        if (hb != hoverBtn || hs != hoverSkip || htp != hoverTabPlay || hts != hoverTabSettings || hsl != hoverSlider || hbf != hoverBtnFolder || hbm != hoverBtnMods || hcf != hoverCheckFps) { 
            hoverBtn = hb; hoverSkip = hs; hoverTabPlay = htp; hoverTabSettings = hts; hoverSlider = hsl; hoverBtnFolder = hbf; hoverBtnMods = hbm; hoverCheckFps = hcf;
            InvalidateRect(h, nullptr, FALSE); 
        }
        if (hb || hs || htp || hts || hsl || hbf || hbm || hcf) SetCursor(LoadCursor(nullptr, IDC_HAND)); 
        else SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = LOWORD(l), y = HIWORD(l); RECT rc; GetClientRect(h, &rc);
        auto ins = [](RECT r, int px, int py) { return px >= r.left && px <= r.right && py >= r.top && py <= r.bottom; };
        
        if (g_state == STATE_LOGIN) {
            RECT btn = { rc.right/2 - 100, 200, rc.right/2 + 100, 250 };
            RECT btnSkip = { rc.right/2 - 100, 270, rc.right/2 + 100, 310 };
            if (ins(btn, x, y)) { ShellExecuteW(nullptr, L"open", L"https://accounts.google.com/", nullptr, nullptr, SW_SHOW); Sleep(3000); g_state = STATE_CREATE_ACC; InvalidateRect(h, nullptr, TRUE); }
            if (ins(btnSkip, x, y)) { g_username = L"Player"; g_state = STATE_MAIN_MENU; InvalidateRect(h, nullptr, TRUE); }
        }
        else if (g_state == STATE_CREATE_ACC) {
            RECT btn = { rc.right/2 - 100, 260, rc.right/2 + 100, 310 };
            RECT btnSkip = { rc.right/2 - 100, 330, rc.right/2 + 100, 370 };
            if (ins(btn, x, y)) {
                wchar_t buf[64] = {}; GetWindowTextW(g_hEdit, buf, 64);
                if (wcslen(buf) > 0) { g_username = buf; g_state = STATE_MAIN_MENU; InvalidateRect(h, nullptr, TRUE); }
            }
            if (ins(btnSkip, x, y)) { g_username = L"Player"; g_state = STATE_MAIN_MENU; InvalidateRect(h, nullptr, TRUE); }
        }
        else if (g_state == STATE_MAIN_MENU || g_state == STATE_READY) {
            RECT tabPlay = { 0, 100, 200, 140 };
            RECT tabSettings = { 0, 140, 200, 180 };
            if (ins(tabPlay, x, y)) { g_activeTab = 0; InvalidateRect(h, nullptr, TRUE); return 0; }
            if (ins(tabSettings, x, y)) { g_activeTab = 1; InvalidateRect(h, nullptr, TRUE); return 0; }

            if (g_activeTab == 0) {
                RECT btn = { rc.right - 220, rc.bottom - 80, rc.right - 20, rc.bottom - 30 };
                if (ins(btn, x, y)) {
                    if (g_state == STATE_MAIN_MENU) { g_state = STATE_DOWNLOADING; InvalidateRect(h, nullptr, TRUE); CreateThread(nullptr, 0, DownloadThread, nullptr, 0, nullptr); }
                    else if (g_state == STATE_READY) LaunchGame();
                }
            } else {
                // Слайдер RAM
                RECT slider = { 220, 150, 500, 170 };
                if (ins(slider, x, y)) {
                    int pct = (x - slider.left) * 100 / (slider.right - slider.left);
                    g_ramValue = 1024 + (8192 - 1024) * pct / 100;
                    // Округляем до 256
                    g_ramValue = (g_ramValue / 256) * 256;
                    InvalidateRect(h, nullptr, TRUE);
                }

                // Кнопка Папка с игрой
                RECT btnFolder = { 220, 210, 400, 245 };
                if (ins(btnFolder, x, y)) {
                    CreateDirectoryW(g_installDir.c_str(), nullptr);
                    ShellExecuteW(nullptr, L"open", g_installDir.c_str(), nullptr, nullptr, SW_SHOW);
                }

                // Кнопка Папка с модами
                RECT btnMods = { 420, 210, 600, 245 };
                if (ins(btnMods, x, y)) {
                    std::wstring modsPath = g_installDir + L"\\.minecraft\\mods";
                    CreateDirectoryW((g_installDir + L"\\.minecraft").c_str(), nullptr);
                    CreateDirectoryW(modsPath.c_str(), nullptr);
                    ShellExecuteW(nullptr, L"open", modsPath.c_str(), nullptr, nullptr, SW_SHOW);
                }

                // Чекбокс FPS
                RECT cf = { 220, 280, 240, 300 };
                if (ins(cf, x, y)) {
                    g_showFpsOverlay = !g_showFpsOverlay;
                    if (g_showFpsOverlay) {
                        CreateThread(nullptr, 0, FpsThread, nullptr, 0, nullptr);
                    } else {
                        if (g_fpsWnd) { PostMessageW(g_fpsWnd, WM_CLOSE, 0, 0); g_fpsWnd = nullptr; }
                    }
                    InvalidateRect(h, nullptr, TRUE);
                }
            }
        }
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    g_hInst = hInst;
    WNDCLASSW wc = {}; wc.hInstance = hInst; wc.lpfnWndProc = WndProc; wc.lpszClassName = L"DlauncherWnd"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    int w = 800, h = 500, sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    g_hWnd = CreateWindowExW(0, L"DlauncherWnd", L"Dlauncher", WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, (sw-w)/2, (sh-h)/2, w, h, nullptr, nullptr, hInst, nullptr);
    ShowWindow(g_hWnd, SW_SHOW); UpdateWindow(g_hWnd);
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}
