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
#include "mem.h"
#include "textfile.h"

// MinGW 较老的头文件里可能没有这个消息定义
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

// ── 全局状态 ─────────────────────────────────────────────
static AppUI     g_ui;
static AppConfig g_cfg;
static wchar_t   g_iniPath[MAX_PATH];
static int       g_state = STATE_IDLE;

static WorkerParams *g_worker = NULL;
static HANDLE        g_hThread = NULL;

static DbContext     g_dbCtx;
// 记录启动时数据库初始化的结果，供打开题库时说明失败原因
static int           g_dbInitError = DB_OK;
static wchar_t       g_dbInitPath[MAX_PATH];

// 防止 Trackbar ↔ EditInterval 互相触发
static BOOL g_syncingInterval = FALSE;

// 单实例互斥体，随进程存活
static HANDLE g_hSingleInstance = NULL;

// 托盘图标句柄。只有自己 LoadImageW 出来的才需要 DestroyIcon；
// 回退到系统共享图标时不能销毁。
static HICON  g_hTrayIcon      = NULL;
static BOOL   g_trayIconOwned  = FALSE;

// 热键注册结果。程序以托盘 + 热键为主要交互，注册失败必须让用户知道。
static BOOL g_hotkeyStart  = FALSE;
static BOOL g_hotkeySearch = FALSE;
static BOOL g_hotkeyStop   = FALSE;

// 关闭按钮默认收回托盘；只有托盘菜单「退出」才真正结束进程。
static BOOL g_allowExit      = FALSE;
static BOOL g_hideHintShown  = FALSE;

// ── 从剪切板读取文本并直接开始输入 ───────────────────────

static wchar_t* GetClipboardText(void);  // 前向声明
static void SetState(int newState);       // 前向声明
static int  GetCurrentDelay(void);        // 前向声明

// ── 托盘气泡提示 ─────────────────────────────────────────
// 用气泡而不是 MessageBox：不抢焦点、不阻塞启动，而且出现在
// 程序真正待着的地方（托盘）。
static void ShowTrayBalloon(HWND hwnd, const wchar_t *title,
                            const wchar_t *text, DWORD iconFlag)
{
    NOTIFYICONDATAW nid = {0};

    nid.cbSize = sizeof(nid);
    nid.hWnd   = hwnd;
    nid.uID    = 1;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = iconFlag;
    wcsncpy(nid.szInfoTitle, title,
            sizeof(nid.szInfoTitle) / sizeof(nid.szInfoTitle[0]) - 1);
    wcsncpy(nid.szInfo, text,
            sizeof(nid.szInfo) / sizeof(nid.szInfo[0]) - 1);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// 显示并置前主窗口。托盘左键、托盘菜单和第二个实例都走这里。
static void ShowMainWindow(HWND hwnd)
{
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    } else {
        ShowWindow(hwnd, SW_SHOW);
    }
    SetForegroundWindow(hwnd);
}

// ── 热键注册与状态反馈 ───────────────────────────────────

// 注册三个全局热键，逐个记录成功与否。返回失败个数。
static int RegisterAppHotkeys(HWND hwnd)
{
    int failed = 0;

    // 重试前先撤销已有注册，避免「已注册」本身导致失败
    UnregisterHotKey(hwnd, HOTKEY_START);
    UnregisterHotKey(hwnd, HOTKEY_SEARCH);
    UnregisterHotKey(hwnd, HOTKEY_STOP);

    g_hotkeyStart  = RegisterHotKey(hwnd, HOTKEY_START,  MOD_CONTROL | MOD_ALT, 'V');
    g_hotkeySearch = RegisterHotKey(hwnd, HOTKEY_SEARCH, MOD_CONTROL | MOD_ALT, 'B');
    g_hotkeyStop   = RegisterHotKey(hwnd, HOTKEY_STOP,   MOD_CONTROL | MOD_ALT, 'S');

    if (!g_hotkeyStart)  ++failed;
    if (!g_hotkeySearch) ++failed;
    if (!g_hotkeyStop)   ++failed;

    UI_SetHotkeyHint(&g_ui, g_hotkeyStart, g_hotkeySearch, g_hotkeyStop);
    return failed;
}

// 把注册结果告诉用户。热键失败不致命——主窗口仍可操作，所以是降级 + 警告。
static void ReportHotkeyStatus(HWND hwnd, int failed, BOOL isRetry)
{
    if (failed == 0) {
        UI_SetStatus(&g_ui, L"就绪");
        if (isRetry) {
            ShowTrayBalloon(hwnd, APP_NAME,
                            L"三个热键已全部注册成功。", NIIF_INFO);
        }
        return;
    }

    {
        wchar_t text[512];
        wchar_t list[256] = L"";

        if (!g_hotkeyStart)  wcscat(list, L"Ctrl+Alt+V（开始输入）\n");
        if (!g_hotkeySearch) wcscat(list, L"Ctrl+Alt+B（搜索题库）\n");
        if (!g_hotkeyStop)   wcscat(list, L"Ctrl+Alt+S（停止输入）\n");

        wsprintfW(text,
                  L"以下热键被其他程序占用，暂不可用：\n%s"
                  L"可从托盘打开主窗口手动操作，或关闭占用程序后"
                  L"用托盘菜单「重试注册热键」。",
                  list);
        ShowTrayBalloon(hwnd, APP_NAME, text, NIIF_WARNING);
        UI_SetStatus(&g_ui, L"部分热键被占用，详见托盘提示");
    }
}

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
        text = (wchar_t *)Mem_Alloc((textLen + 1) * sizeof(wchar_t));
        if (!text) return;
        GetWindowTextW(g_ui.hwndEditText, text, textLen + 1);
    } else {
        // 从剪切板读取
        wchar_t *clipText = GetClipboardText();
        if (!clipText || wcslen(clipText) == 0) {
            Mem_Free(clipText);
            UI_SetStatus(&g_ui, L"剪切板为空！");
            return;
        }
        textLen = (int)wcslen(clipText);
        text = (wchar_t *)Mem_Alloc((textLen + 1) * sizeof(wchar_t));
        if (!text) { Mem_Free(clipText); return; }
        wcscpy(text, clipText);
        Mem_Free(clipText);
    }

    g_worker = (WorkerParams *)Mem_AllocZero(sizeof(WorkerParams));
    if (!g_worker) { Mem_Free(text); return; }

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
        text = (wchar_t *)Mem_Alloc((len + 1) * sizeof(wchar_t));
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
        Mem_Free(clipboardText);
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
        Mem_Free(answer);
    } else {
        if (IsWindowVisible(g_ui.hwndMain)) {
            UI_SetStatus(&g_ui, L"暂未搜索到相应答案！");
            MessageBoxW(g_ui.hwndMain, L"暂未搜索到相应答案！", L"提示", MB_ICONINFORMATION);
        }
    }

    Mem_Free(clipboardText);
}

static void OnBtnDatabase(void)
{
    // 如果数据库未初始化，先说明启动时的失败原因，再尝试默认路径
    if (!Db_IsInitialized(&g_dbCtx)) {
        if (g_dbInitError != DB_OK && g_dbInitPath[0]) {
            wchar_t msg[MAX_PATH + 160];
            wsprintfW(msg,
                L"启动时无法打开配置的题库数据库：\n%s\n\n错误代码：%d\n\n"
                L"将改用默认路径重试。",
                g_dbInitPath, g_dbInitError);
            MessageBoxW(g_ui.hwndMain, msg, L"题库数据库", MB_ICONWARNING);
        }

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
            wchar_t msg[MAX_PATH + 128];
            wsprintfW(msg, L"无法初始化数据库！\n%s\n\n错误代码：%d",
                      g_cfg.dbPath, result);
            MessageBoxW(g_ui.hwndMain, msg, L"错误", MB_ICONERROR);
            return;
        }
        g_dbInitError = DB_OK;
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

// 加载文件到文本面板。编码探测与体积上限由 TextFile_Load 统一处理。
static BOOL LoadTextFile(const wchar_t *path)
{
    TextFileStatus status = TEXTFILE_OK;
    wchar_t *text = TextFile_Load(path, &status);

    if (!text) {
        MessageBoxW(g_ui.hwndMain, TextFile_StatusText(status),
                    L"错误", MB_ICONERROR);
        return FALSE;
    }

    SetWindowTextW(g_ui.hwndEditText, text);
    Mem_Free(text);
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
        SendMessageW(g_ui.hwndChkTopmost, BM_SETCHECK,
            g_cfg.alwaysOnTop ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(g_ui.hwndChkCodeMode, BM_SETCHECK,
            g_cfg.codeInputMode ? BST_CHECKED : BST_UNCHECKED, 0);
        if (g_cfg.alwaysOnTop)
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

        // 系统托盘图标。用 LoadImageW 显式取小图标尺寸，
        // 否则 Windows 会把 32x32 硬缩到 16x16，边缘发毛。
        {
            NOTIFYICONDATAW nid = {0};

            g_hTrayIcon = (HICON)LoadImageW(
                GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APP_ICON),
                IMAGE_ICON,
                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                LR_DEFAULTCOLOR);
            g_trayIconOwned = (g_hTrayIcon != NULL);
            if (!g_hTrayIcon) {
                // 系统共享图标，不归我们所有
                g_hTrayIcon = LoadIconW(NULL, IDI_APPLICATION);
            }

            nid.cbSize = sizeof(nid);
            nid.hWnd   = hwnd;
            nid.uID    = 1;
            nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
            nid.uCallbackMessage = WM_TRAYICON;
            nid.hIcon  = g_hTrayIcon;
            wcscpy(nid.szTip, APP_TITLE);
            Shell_NotifyIconW(NIM_ADD, &nid);
        }

        // 按当前 DPI 把窗口放大到等效逻辑尺寸并居中。
        // manifest 声明 PerMonitorV2，Windows 不会替我们拉伸。
        UI_ResizeToScaled(hwnd, &g_ui, WIN_LOGICAL_W, WIN_LOGICAL_H);

        // 接通配置里的深色模式（此前 UI_Create 硬编码为浅色）
        UI_SetDarkMode(&g_ui, g_cfg.darkMode);

        // 托盘图标就绪后再注册热键，失败提示才有地方可弹
        ReportHotkeyStatus(hwnd, RegisterAppHotkeys(hwnd), FALSE);

        return 0;

    case WM_ERASEBKGND:
        return UI_OnEraseBkgnd(&g_ui, (HDC)wParam);

    case WM_DRAWITEM:
        return UI_OnDrawItem(&g_ui, wParam, lParam);

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
        return UI_OnCtlColor(&g_ui, hwnd, msg, wParam, lParam);

    case WM_SIZE:
        UI_Layout(&g_ui, LOWORD(lParam), HIWORD(lParam));
        UI_ApplyWindowStyling(hwnd, g_cfg.darkMode);
        return 0;

    case WM_DPICHANGED:
        // 拖到另一块缩放比例不同的显示器：重建字体，采用系统建议的窗口矩形，
        // 随后的 WM_SIZE 会用新 DPI 重新布局。
        UI_UpdateDpi(&g_ui, HIWORD(wParam));
        {
            RECT *suggested = (RECT *)lParam;
            if (suggested) {
                SetWindowPos(hwnd, NULL,
                             suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
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
            ShowMainWindow(hwnd);
            return 0;
        }
        if (id == IDM_TRAY_RETRY_HOTKEY) {
            ReportHotkeyStatus(hwnd, RegisterAppHotkeys(hwnd), TRUE);
            return 0;
        }
        if (id == IDM_TRAY_QUIT) {
            g_allowExit = TRUE;   // 只有这条路径才真正退出进程
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
        if (id == IDC_CHK_DARK && code == BN_CLICKED) {
            g_cfg.darkMode =
                (SendMessageW(g_ui.hwndChkDark, BM_GETCHECK, 0, 0) == BST_CHECKED);
            UI_SetDarkMode(&g_ui, g_cfg.darkMode);
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
        // 左键单击或双击都打开主窗口（双击时第一下已经打开，第二下无害）
        if (LOWORD(lParam) == WM_LBUTTONUP ||
            LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            ShowMainWindow(hwnd);
            return 0;
        }
        if (LOWORD(lParam) == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_TRAY_SHOW, L"打开主窗口");
            // 只在确实有热键被占用时才显示重试入口
            if (!g_hotkeyStart || !g_hotkeySearch || !g_hotkeyStop) {
                AppendMenuW(hMenu, MF_STRING, IDM_TRAY_RETRY_HOTKEY,
                            L"重试注册热键");
            }
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, IDM_TRAY_QUIT, L"退出");
            SetMenuDefaultItem(hMenu, IDM_TRAY_SHOW, FALSE);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        return 0;

    case WM_CLOSE:
        // 关闭按钮收回托盘而不是结束进程，否则热键会静默失效。
        // 真正退出只走托盘菜单「退出」。
        if (!g_allowExit) {
            ShowWindow(hwnd, SW_HIDE);
            if (!g_hideHintShown) {
                g_hideHintShown = TRUE;
                ShowTrayBalloon(hwnd, APP_NAME,
                    L"已最小化到托盘，仍在后台运行。\n"
                    L"左键单击托盘图标可重新打开窗口；"
                    L"需要完全退出请右键选择「退出」。",
                    NIIF_INFO);
            }
            return 0;
        }
        break;   // 交给 DefWindowProc 走正常销毁流程

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
        if (g_hTrayIcon && g_trayIconOwned) {
            DestroyIcon(g_hTrayIcon);
        }
        g_hTrayIcon = NULL;
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

    // 单实例保护。用 Local\ 前缀（会话级），多用户/远程桌面下各自独立。
    // 重复启动时的真实意图基本是「我想看界面」，所以把已有实例的窗口
    // 显示出来再退出，而不是简单报错。
    g_hSingleInstance = CreateMutexW(NULL, TRUE,
                                    L"Local\\BaoXiaoXinWriter_SingleInstance");
    if (g_hSingleInstance && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"KeyboardSimClass", NULL);
        if (existing) {
            ShowWindow(existing, IsIconic(existing) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(existing);
        }
        CloseHandle(g_hSingleInstance);
        return 0;
    }

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
    // 记录失败原因和路径；此时窗口尚未创建，等用户打开题库时再提示
    g_dbInitError = Db_Init(&g_dbCtx, g_cfg.dbPath);
    if (g_dbInitError != DB_OK) {
        wcsncpy(g_dbInitPath, g_cfg.dbPath, MAX_PATH - 1);
        g_dbInitPath[MAX_PATH - 1] = L'\0';
        OutputDebugStringW(L"启动时数据库初始化失败，已记录原始路径");
    }

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
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hIconSm       = (HICON)LoadImageW(
        hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR);
    if (!wc.hIcon)   wc.hIcon   = LoadIconW(NULL, IDI_APPLICATION);
    if (!wc.hIconSm) wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    // 先按逻辑尺寸居中创建，WM_CREATE 里再按实际 DPI 缩放并重新居中
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winW = WIN_LOGICAL_W, winH = WIN_LOGICAL_H;
    int x = (screenW - winW) / 2;
    int y = (screenH - winH) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"KeyboardSimClass",
        APP_TITLE,
        WS_OVERLAPPEDWINDOW,
        x, y, winW, winH,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) {
        if (g_hSingleInstance) CloseHandle(g_hSingleInstance);
        return 1;
    }

    // 启动时隐藏主窗口，后台运行，通过热键或托盘菜单操作
    ShowWindow(hwnd, SW_HIDE);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hSingleInstance) {
        CloseHandle(g_hSingleInstance);
        g_hSingleInstance = NULL;
    }
    return (int)msg.wParam;
}
