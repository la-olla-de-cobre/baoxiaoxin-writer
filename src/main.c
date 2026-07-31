#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#define _WIN32_IE 0x0600

#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "resource.h"
#include "config.h"
#include "ui.h"
#include "worker.h"
#include "database.h"
#include "qa_ui.h"

// ── 全局状态 ─────────────────────────────────────────────
static AppUI     g_ui;
static AppConfig g_cfg;
static wchar_t   g_iniPath[MAX_PATH];
static int       g_state = STATE_IDLE;

static WorkerParams *g_worker = NULL;
static HANDLE        g_hThread = NULL;

static DbContext     g_dbCtx;

// 防止 Trackbar ↔ EditInterval 互相触发
static BOOL g_syncingInterval = FALSE;

// ── 从剪切板读取文本并直接开始输入 ───────────────────────

static wchar_t* GetClipboardText(void);  // 前向声明
static void SetState(int newState);       // 前向声明
static int  GetCurrentDelay(void);        // 前向声明

// 停止输入（ESC 热键调用）
static void StopInput(void)
{
    if (!g_worker) return;
    Worker_Stop(g_worker);
    UI_SetStatus(&g_ui, L"正在停止输入...");
}

// 开始输入（根据复选框选择数据源）
static void StartInput(void)
{
    if (g_state != STATE_IDLE) return;

    wchar_t *text = NULL;
    int textLen = 0;

    BOOL usePanel = (SendMessageW(g_ui.hwndChkUsePanel, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (usePanel) {
        // 从文本框读取
        textLen = GetWindowTextLengthW(g_ui.hwndEditText);
        if (textLen == 0) {
            UI_SetStatus(&g_ui, L"文本框为空！");
            return;
        }
        text = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (textLen + 1) * sizeof(wchar_t));
        if (!text) return;
        GetWindowTextW(g_ui.hwndEditText, text, textLen + 1);
    } else {
        // 从剪切板读取
        wchar_t *clipText = GetClipboardText();
        if (!clipText || wcslen(clipText) == 0) {
            free(clipText);
            UI_SetStatus(&g_ui, L"剪切板为空！");
            return;
        }
        textLen = (int)wcslen(clipText);
        text = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (textLen + 1) * sizeof(wchar_t));
        if (!text) { free(clipText); return; }
        wcscpy(text, clipText);
        free(clipText);
    }

    g_worker = (WorkerParams *)HeapAlloc(GetProcessHeap(),
                                          HEAP_ZERO_MEMORY, sizeof(WorkerParams));
    if (!g_worker) { HeapFree(GetProcessHeap(), 0, text); return; }

    g_worker->text      = text;
    g_worker->textLen   = textLen;
    g_worker->delayMs   = GetCurrentDelay();
    g_worker->codeInputMode =
        (SendMessageW(g_ui.hwndChkCodeMode, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g_worker->hwndMain  = g_ui.hwndMain;

    g_hThread = Worker_Start(g_worker);
    if (!g_hThread) {
        Worker_Free(g_worker);
        g_worker = NULL;
        return;
    }

    UI_UpdateProgress(&g_ui, 0, textLen);
    SetState(STATE_RUNNING);
}

// ── 辅助函数 ─────────────────────────────────────────────

static wchar_t* GetClipboardText(void)
{
    if (!OpenClipboard(NULL)) {
        return NULL;
    }

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) {
        CloseClipboard();
        return NULL;
    }

    wchar_t *text = NULL;
    wchar_t *p = (wchar_t *)GlobalLock(hData);
    if (p) {
        size_t len = wcslen(p);
        text = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
        if (text) {
            wcscpy(text, p);
        }
        GlobalUnlock(hData);
    }
    CloseClipboard();
    return text;
}

static void SearchFromClipboard(void)
{
    wchar_t *clipboardText = GetClipboardText();
    if (!clipboardText) {
        UI_SetStatus(&g_ui, L"无法读取剪切板！");
        return;
    }

    // 去掉首尾空格
    wchar_t *question = clipboardText;
    while (*question && (*question == L' ' || *question == L'\t' || *question == L'\r' || *question == L'\n')) question++;
    size_t len = wcslen(question);
    while (len > 0 && (question[len-1] == L' ' || question[len-1] == L'\t' || question[len-1] == L'\r' || question[len-1] == L'\n')) {
        question[len-1] = L'\0';
        len--;
    }

    if (len == 0) {
        free(clipboardText);
        UI_SetStatus(&g_ui, L"剪切板内容为空！");
        return;
    }

    BOOL useFuzzy = (SendMessageW(g_ui.hwndChkFuzzy, BM_GETCHECK, 0, 0) == BST_CHECKED);
    wchar_t *answer = useFuzzy ? Db_SearchFuzzy(&g_dbCtx, question)
                               : Db_Search(&g_dbCtx, question);
    if (answer) {
        if (IsWindowVisible(g_ui.hwndMain)) {
            SetWindowTextW(g_ui.hwndEditText, answer);
            UI_SetStatus(&g_ui, L"已找到答案并粘贴到输入框！");
        } else {
            // 托盘模式：复制到剪贴板
            if (OpenClipboard(NULL)) {
                EmptyClipboard();
                size_t ansLen = wcslen(answer);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (ansLen + 1) * sizeof(wchar_t));
                if (hMem) {
                    wchar_t *pMem = (wchar_t *)GlobalLock(hMem);
                    if (pMem) {
                        wcscpy(pMem, answer);
                        GlobalUnlock(hMem);
                    }
                    SetClipboardData(CF_UNICODETEXT, hMem);
                }
                CloseClipboard();
            }
        }
        free(answer);
    } else {
        if (IsWindowVisible(g_ui.hwndMain)) {
            UI_SetStatus(&g_ui, L"暂未搜索到相应答案！");
            MessageBoxW(g_ui.hwndMain, L"暂未搜索到相应答案！", L"提示", MB_ICONINFORMATION);
        }
    }

    free(clipboardText);
}

static void OnBtnDatabase(void)
{
    // 如果数据库未初始化，先初始化
    if (!Db_IsInitialized(&g_dbCtx)) {
        // 构建数据库路径：%APPDATA%\KeyboardSim\qa_database.db
        wchar_t appData[MAX_PATH];
        SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData);
        wsprintfW(g_cfg.dbPath, L"%s\\KeyboardSim\\qa_database.db", appData);

        // 确保目录存在
        wchar_t dir[MAX_PATH];
        wsprintfW(dir, L"%s\\KeyboardSim", appData);
        CreateDirectoryW(dir, NULL);

        int result = Db_Init(&g_dbCtx, g_cfg.dbPath);
        if (result != DB_OK) {
            MessageBoxW(g_ui.hwndMain, L"无法初始化数据库！", L"错误", MB_ICONERROR);
            return;
        }
    }

    // 显示题库管理对话框
    QAManagerUI_Create(g_ui.hwndMain, &g_ui, &g_dbCtx, g_cfg.darkMode, g_ui.hFontUI, g_ui.hFontEdit);
}

static int GetCurrentDelay(void)
{
    return (int)SendMessageW(g_ui.hwndTrackbar, TBM_GETPOS, 0, 0);
}

static void SetState(int newState)
{
    g_state = newState;
    UI_SetState(&g_ui, newState);

    switch (newState) {
    case STATE_IDLE:
        UI_SetStatus(&g_ui, L"就绪");
        break;
    case STATE_RUNNING:
        UI_SetStatus(&g_ui, L"正在输入…  Ctrl+Alt+S 停止");
        break;
    case STATE_PAUSED:
        UI_SetStatus(&g_ui, L"已暂停");
        break;
    }
}

// 加载文件并检测编码（UTF-8 BOM / UTF-16 LE / UTF-16 BE / 无BOM默认UTF-8）
static BOOL LoadTextFile(const wchar_t *path)
{
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(g_ui.hwndMain, L"无法打开文件。", L"错误", MB_ICONERROR);
        return FALSE;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return FALSE;
    }

    BYTE *raw = (BYTE *)HeapAlloc(GetProcessHeap(), 0, fileSize + 4);
    if (!raw) { CloseHandle(hFile); return FALSE; }

    DWORD bytesRead = 0;
    if (!ReadFile(hFile, raw, fileSize, &bytesRead, NULL) || bytesRead != fileSize) {
        CloseHandle(hFile);
        HeapFree(GetProcessHeap(), 0, raw);
        MessageBoxW(g_ui.hwndMain, L"读取文件失败。", L"错误", MB_ICONERROR);
        return FALSE;
    }
    CloseHandle(hFile);
    raw[bytesRead] = raw[bytesRead+1] = raw[bytesRead+2] = 0;

    wchar_t *wtext = NULL;
    int wlen = 0;

    if (bytesRead >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        // UTF-16 LE
        wtext = (wchar_t *)(raw + 2);
        wlen  = (int)((bytesRead - 2) / sizeof(wchar_t));
        wchar_t *copy = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (wlen + 1) * sizeof(wchar_t));
        if (copy) {
            memcpy(copy, wtext, wlen * sizeof(wchar_t));
            copy[wlen] = 0;
            SetWindowTextW(g_ui.hwndEditText, copy);
            HeapFree(GetProcessHeap(), 0, copy);
        }
    } else if (bytesRead >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
        // UTF-16 BE — 字节交换
        int chars = (int)((bytesRead - 2) / 2);
        wchar_t *copy = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (chars + 1) * sizeof(wchar_t));
        if (copy) {
            BYTE *src = raw + 2;
            for (int i = 0; i < chars; ++i) {
                copy[i] = (wchar_t)((src[i*2] << 8) | src[i*2+1]);
            }
            copy[chars] = 0;
            SetWindowTextW(g_ui.hwndEditText, copy);
            HeapFree(GetProcessHeap(), 0, copy);
        }
    } else {
        // UTF-8（有无BOM均可）
        BYTE *utf8 = raw;
        DWORD utf8Len = bytesRead;
        if (bytesRead >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
            utf8    = raw + 3;
            utf8Len = bytesRead - 3;
        }
        wlen = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)utf8, (int)utf8Len, NULL, 0);
        wchar_t *copy = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (wlen + 1) * sizeof(wchar_t));
        if (copy) {
            MultiByteToWideChar(CP_UTF8, 0, (LPCCH)utf8, (int)utf8Len, copy, wlen);
            copy[wlen] = 0;
            SetWindowTextW(g_ui.hwndEditText, copy);
            HeapFree(GetProcessHeap(), 0, copy);
        }
    }

    HeapFree(GetProcessHeap(), 0, raw);
    return TRUE;
}

static void OnBtnLoad(void)
{
    OPENFILENAMEW ofn;
    wchar_t filePath[MAX_PATH] = {0};

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_ui.hwndMain;
    ofn.lpstrFilter = L"文本文件\0*.txt;*.md;*.csv\0所有文件\0*.*\0";
    ofn.lpstrFile   = filePath;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrInitialDir = g_cfg.lastDir[0] ? g_cfg.lastDir : NULL;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        LoadTextFile(filePath);
        // 保存目录
        wcsncpy(g_cfg.lastDir, filePath, MAX_PATH - 1);
        g_cfg.lastDir[MAX_PATH - 1] = 0;
        wchar_t *slash = wcsrchr(g_cfg.lastDir, L'\\');
        if (slash) *(slash + 1) = 0;
    }
}


static void OnWorkerDone(BOOL stopped)
{
    if (g_hThread) {
        CloseHandle(g_hThread);
        g_hThread = NULL;
    }
    if (g_worker) {
        Worker_Free(g_worker);
        g_worker = NULL;
    }

    SetState(STATE_IDLE);

    if (!stopped) {
        UI_SetStatus(&g_ui, L"输入完成！");
        MessageBeep(MB_OK);
    }
}

static void OnTrackbarChange(void)
{
    if (g_syncingInterval) return;
    g_syncingInterval = TRUE;
    int pos = GetCurrentDelay();
    UI_SetIntervalText(&g_ui, pos);
    g_cfg.delayMs = pos;
    g_syncingInterval = FALSE;
}

static void OnIntervalEditChange(void)
{
    if (g_syncingInterval) return;
    g_syncingInterval = TRUE;
    wchar_t buf[16];
    GetWindowTextW(g_ui.hwndEditInterval, buf, 16);
    int val = _wtoi(buf);
    if (val >= INTERVAL_MIN && val <= INTERVAL_MAX) {
        SendMessageW(g_ui.hwndTrackbar, TBM_SETPOS, TRUE, val);
        g_cfg.delayMs = val;
    }
    g_syncingInterval = FALSE;
}

static void OnPresetChange(void)
{
    int sel = (int)SendMessageW(g_ui.hwndComboPreset, CB_GETCURSEL, 0, 0);
    int delay = INTERVAL_DEF;
    if (sel == 0) delay = SPEED_SLOW;
    else if (sel == 1) delay = SPEED_MEDIUM;
    else if (sel == 2) delay = SPEED_FAST;

    g_syncingInterval = TRUE;
    SendMessageW(g_ui.hwndTrackbar, TBM_SETPOS, TRUE, delay);
    UI_SetIntervalText(&g_ui, delay);
    g_cfg.delayMs = delay;
    g_syncingInterval = FALSE;
}

// ── 窗口过程 ─────────────────────────────────────────────

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        UI_Create(hwnd, &g_ui);
        UI_SetIntervalText(&g_ui, g_cfg.delayMs);
        SendMessageW(g_ui.hwndTrackbar, TBM_SETPOS, TRUE, g_cfg.delayMs);
        UI_SetPresetSelection(&g_ui, g_cfg.delayMs);
        RegisterHotKey(hwnd, HOTKEY_START, MOD_CONTROL | MOD_ALT, 'V');
        RegisterHotKey(hwnd, HOTKEY_SEARCH, MOD_CONTROL | MOD_ALT, 'B');
        RegisterHotKey(hwnd, HOTKEY_STOP, MOD_CONTROL | MOD_ALT, 'S');
        SendMessageW(g_ui.hwndChkTopmost, BM_SETCHECK,
            g_cfg.alwaysOnTop ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(g_ui.hwndChkCodeMode, BM_SETCHECK,
            g_cfg.codeInputMode ? BST_CHECKED : BST_UNCHECKED, 0);
        if (g_cfg.alwaysOnTop)
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

        // 系统托盘图标
        {
            NOTIFYICONDATAW nid = {0};
            nid.cbSize = sizeof(nid);
            nid.hWnd   = hwnd;
            nid.uID    = 1;
            nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
            nid.uCallbackMessage = WM_TRAYICON;
            nid.hIcon  = LoadIconW(NULL, IDI_APPLICATION);
            wcscpy(nid.szTip, L"鲍小新写字");
            Shell_NotifyIconW(NIM_ADD, &nid);
        }

        return 0;

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_ui.hbrBg);
        return 1;
    }

    case WM_DRAWITEM:
        return UI_OnDrawItem(&g_ui, wParam, lParam);

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
        return UI_OnCtlColor(&g_ui, hwnd, msg, wParam, lParam);

    case WM_SIZE:
        UI_Layout(&g_ui, LOWORD(lParam), HIWORD(lParam));
        UI_ApplyWindowStyling(hwnd, g_cfg.darkMode);
        return 0;

    case WM_HOTKEY:
        if (wParam == HOTKEY_START) {
            StartInput();
        } else if (wParam == HOTKEY_SEARCH) {
            SearchFromClipboard();
        } else if (wParam == HOTKEY_STOP) {
            StopInput();
        }
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        // 托盘菜单命令
        if (id == IDM_TRAY_SHOW) {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            return 0;
        }
        if (id == IDM_TRAY_QUIT) {
            DestroyWindow(hwnd);
            return 0;
        }

        if (id == IDC_BTN_LOAD)    { OnBtnLoad();    return 0; }
        if (id == IDC_BTN_DATABASE) { OnBtnDatabase(); return 0; }
        if (id == IDC_CHK_TOPMOST && code == BN_CLICKED) {
            BOOL checked = (SendMessageW(g_ui.hwndChkTopmost, BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_cfg.alwaysOnTop = checked;
            SetWindowPos(hwnd,
                checked ? HWND_TOPMOST : HWND_NOTOPMOST,
                0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            return 0;
        }
        if (id == IDC_CHK_CODE_MODE && code == BN_CLICKED) {
            g_cfg.codeInputMode =
                (SendMessageW(g_ui.hwndChkCodeMode, BM_GETCHECK, 0, 0) == BST_CHECKED);
            return 0;
        }

        if (id == IDC_COMBO_PRESET && code == CBN_SELCHANGE) {
            OnPresetChange();
            return 0;
        }
        if (id == IDC_EDIT_INTERVAL && code == EN_CHANGE) {
            OnIntervalEditChange();
            return 0;
        }
        break;
    }

    case WM_HSCROLL:
        if ((HWND)lParam == g_ui.hwndTrackbar) {
            OnTrackbarChange();
        }
        return 0;

    case WM_WORKER_PROGRESS:
        UI_UpdateProgress(&g_ui, (int)wParam, (int)lParam);
        return 0;

    case WM_WORKER_DONE:
        OnWorkerDone((BOOL)wParam);
        return 0;

    case WM_TRAYICON:
        if (LOWORD(lParam) == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_TRAY_SHOW, L"打开主窗口");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, IDM_TRAY_QUIT, L"退出");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        return 0;

    case WM_DESTROY:
        UnregisterHotKey(hwnd, HOTKEY_START);
        UnregisterHotKey(hwnd, HOTKEY_SEARCH);
        UnregisterHotKey(hwnd, HOTKEY_STOP);
        // 移除托盘图标
        {
            NOTIFYICONDATAW nid = {0};
            nid.cbSize = sizeof(nid);
            nid.hWnd   = hwnd;
            nid.uID    = 1;
            Shell_NotifyIconW(NIM_DELETE, &nid);
        }
        if (g_worker) {
            Worker_Stop(g_worker);
            if (g_hThread) {
                if (WaitForSingleObject(g_hThread, 2000) == WAIT_OBJECT_0) {
                    CloseHandle(g_hThread);
                    g_hThread = NULL;
                    Worker_Free(g_worker);
                    g_worker = NULL;
                }
            } else {
                Worker_Free(g_worker);
                g_worker = NULL;
            }
        }
        Db_Close(&g_dbCtx);
        Config_Save(&g_cfg, g_iniPath);
        UI_Destroy(&g_ui);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ── 入口点 ───────────────────────────────────────────────

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    // 构建 INI 路径：%APPDATA%\KeyboardSim\config.ini
    wchar_t appData[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData);
    wsprintfW(g_iniPath, L"%s\\KeyboardSim\\config.ini", appData);

    // 确保目录存在
    wchar_t dir[MAX_PATH];
    wsprintfW(dir, L"%s\\KeyboardSim", appData);
    CreateDirectoryW(dir, NULL);

    Config_Load(&g_cfg, g_iniPath);

    // 初始化数据库
    if (g_cfg.dbPath[0] == L'\0') {
        wsprintfW(g_cfg.dbPath, L"%s\\KeyboardSim\\qa_database.db", appData);
    }
    Db_Init(&g_dbCtx, g_cfg.dbPath);

    // 注册窗口类
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"KeyboardSimClass";
    wc.hIcon         = LoadIconW(NULL, IDI_APPLICATION);
    wc.hIconSm       = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);

    // 居中创建窗口
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winW = 760, winH = 520;
    int x = (screenW - winW) / 2;
    int y = (screenH - winH) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"KeyboardSimClass",
        L"鲍小新写字  v3.1",
        WS_OVERLAPPEDWINDOW,
        x, y, winW, winH,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) return 1;

    // 启动时隐藏主窗口，后台运行，通过热键或托盘菜单操作
    ShowWindow(hwnd, SW_HIDE);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
