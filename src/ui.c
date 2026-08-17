#define _WIN32_WINNT 0x0601
#define _WIN32_IE    0x0600
#include "ui.h"
#include "resource.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <stdint.h>
#include <stdio.h>

// ── 内部工具 ─────────────────────────────────────────────

typedef HRESULT (WINAPI *PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);

static void ApplyDarkMode(HWND hwnd, BOOL dark)
{
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (!hDwm) return;
    PFN_DwmSetWindowAttribute pfn =
        (PFN_DwmSetWindowAttribute)GetProcAddress(hDwm, "DwmSetWindowAttribute");
    if (pfn) {
        BOOL val = dark ? TRUE : FALSE;
        pfn(hwnd, 20, &val, sizeof(val)); // DWMWA_USE_IMMERSIVE_DARK_MODE
    }
    FreeLibrary(hDwm);
}

// ── DPI ─────────────────────────────────────────────────
// manifest 声明了 PerMonitorV2，即由程序自己负责缩放，Windows 不再拉伸。
// 所以字体和布局必须用同一个 DPI 换算，否则会出现「字放大了、控件没放大」
// 的错位——这曾是界面在高分屏上显示错乱的根因。

typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);

int UI_GetWindowDpi(HWND hwnd)
{
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    int dpi = 96;

    if (hUser) {
        PFN_GetDpiForWindow pfn =
            (PFN_GetDpiForWindow)GetProcAddress(hUser, "GetDpiForWindow");
        if (pfn) {
            UINT value = pfn(hwnd);
            if (value >= 72) return (int)value;
        }
    }

    // 旧系统回退：取窗口设备上下文的逻辑分辨率
    {
        HDC hdc = GetDC(hwnd);
        if (hdc) {
            int value = GetDeviceCaps(hdc, LOGPIXELSY);
            ReleaseDC(hwnd, hdc);
            if (value >= 72) dpi = value;
        }
    }
    return dpi;
}

int UI_Scale(const AppUI *ui, int logical)
{
    int dpi = (ui && ui->dpi >= 72) ? ui->dpi : 96;
    return MulDiv(logical, dpi, 96);
}

static HFONT CreateUIFont(int ptSize, BOOL bold, int dpiY)
{
    return CreateFontW(
        -MulDiv(ptSize, dpiY, 72),
        0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        L"Microsoft YaHei UI"
    );
}

// 在 AppUI 中按 HWND 找按钮槽位，返回 -1 表示未找到
static int FindBtnIdx(AppUI *ui, HWND hwnd)
{
    for (int i = 0; i < ui->btnCount; i++) {
        if (ui->btnHwnds[i] == hwnd) return i;
    }
    return -1;
}

// ── 按钮子类化（捕捉 WM_MOUSELEAVE） ────────────────────

static WNDPROC s_origBtnProc = NULL;

static LRESULT CALLBACK BtnSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    AppUI *ui = (AppUI *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_MOUSEMOVE:
        if (ui) UI_OnMouseMove(ui, hwnd);
        break;
    case WM_MOUSELEAVE:
        if (ui) UI_OnMouseLeave(ui, hwnd);
        break;
    case WM_LBUTTONDOWN:
        if (ui) {
            int i = FindBtnIdx(ui, hwnd);
            if (i >= 0) ui->btnPressed[i] = TRUE;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    case WM_LBUTTONUP:
        if (ui) {
            int i = FindBtnIdx(ui, hwnd);
            if (i >= 0) ui->btnPressed[i] = FALSE;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    }
    return CallWindowProcW(s_origBtnProc, hwnd, msg, wParam, lParam);
}

static HWND MakeOwnerDrawBtn(HWND parent, const wchar_t *text, int id,
                              HFONT font, AppUI *ui, BtnRole role)
{
    HWND hwnd = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, parent, (HMENU)(intptr_t)id, NULL, NULL);
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);

    // 存入 AppUI
    if (ui->btnCount < MAX_BTNS) {
        int i = ui->btnCount++;
        ui->btnHwnds[i]   = hwnd;
        ui->btnRoles[i]   = role;
        ui->btnHover[i]   = FALSE;
        ui->btnPressed[i] = FALSE;
        ui->btnTracking[i]= FALSE;
    }

    // 子类化
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)ui);
    if (!s_origBtnProc)
        s_origBtnProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)BtnSubclassProc);
    else
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)BtnSubclassProc);

    return hwnd;
}

static HWND MakeStatic(HWND parent, const wchar_t *text, int id, HFONT font)
{
    HWND hwnd = CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, parent, (HMENU)(intptr_t)id, NULL, NULL);
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
    return hwnd;
}

// ── 公开接口 ─────────────────────────────────────────────

void UI_Create(HWND hwndParent, AppUI *ui)
{
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    ui->hwndMain  = hwndParent;
    ui->darkMode  = FALSE; // 由 UI_SetDarkMode 按配置切换
    ui->btnCount  = 0;
    ui->dpi       = UI_GetWindowDpi(hwndParent);
    ui->hFontUI   = CreateUIFont(10, FALSE, ui->dpi);
    ui->hFontEdit = CreateUIFont(11, FALSE, ui->dpi);

    // 缓存画刷（浅色初值，UI_SetDarkMode 会按需重建）
    ui->hbrBg    = CreateSolidBrush(CLR_BG_LIGHT);
    ui->hbrPanel = CreateSolidBrush(CLR_PANEL_LIGHT);
    ui->hbrEdit  = CreateSolidBrush(CLR_PANEL_LIGHT);

    // 多行文本框
    ui->hwndEditText = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
        0, 0, 0, 0, hwndParent,
        (HMENU)IDC_EDIT_TEXT, NULL, NULL);
    SendMessageW(ui->hwndEditText, WM_SETFONT, (WPARAM)ui->hFontEdit, TRUE);
    SendMessageW(ui->hwndEditText, EM_SETLIMITTEXT, 0, 0);
    // 移除 WS_EX_CLIENTEDGE 改用纯色背景，稍微扁平
    SetWindowTheme(ui->hwndEditText, L" ", L" ");

    // 右侧按钮（owner-draw）
    ui->hwndBtnLoad    = MakeOwnerDrawBtn(hwndParent, L"从文件加载", IDC_BTN_LOAD,    ui->hFontUI, ui, BTN_ROLE_NEUTRAL);
    ui->hwndBtnDatabase= MakeOwnerDrawBtn(hwndParent, L"数据库功能", IDC_BTN_DATABASE, ui->hFontUI, ui, BTN_ROLE_NEUTRAL);

    // 速度预设
    ui->hwndStaticPreset = MakeStatic(hwndParent, L"速度预设：", IDC_STATIC_PRESET, ui->hFontUI);
    ui->hwndComboPreset  = CreateWindowExW(0, L"COMBOBOX", NULL,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 0, 0, hwndParent, (HMENU)IDC_COMBO_PRESET, NULL, NULL);
    SendMessageW(ui->hwndComboPreset, WM_SETFONT, (WPARAM)ui->hFontUI, TRUE);
    SendMessageW(ui->hwndComboPreset, CB_ADDSTRING, 0, (LPARAM)L"慢速 (300ms)");
    SendMessageW(ui->hwndComboPreset, CB_ADDSTRING, 0, (LPARAM)L"中速 (80ms)");
    SendMessageW(ui->hwndComboPreset, CB_ADDSTRING, 0, (LPARAM)L"快速 (20ms)");
    SendMessageW(ui->hwndComboPreset, CB_SETCURSEL, 1, 0);

    // 间隔调节
    ui->hwndStaticInterval = MakeStatic(hwndParent, L"字符间隔 (ms)：", IDC_STATIC_INTERVAL, ui->hFontUI);

    ui->hwndTrackbar = CreateWindowExW(0, TRACKBAR_CLASSW, NULL,
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS | TBS_BOTH,
        0, 0, 0, 0, hwndParent, (HMENU)IDC_TRACKBAR_SPEED, NULL, NULL);
    SetWindowTheme(ui->hwndTrackbar, L"Explorer", NULL); // 细滑道风格
    SendMessageW(ui->hwndTrackbar, TBM_SETRANGE, TRUE, MAKELPARAM(INTERVAL_MIN, INTERVAL_MAX));
    SendMessageW(ui->hwndTrackbar, TBM_SETPOS, TRUE, INTERVAL_DEF);

    ui->hwndEditInterval = CreateWindowExW(0, L"EDIT", L"80",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
        0, 0, 0, 0, hwndParent, (HMENU)IDC_EDIT_INTERVAL, NULL, NULL);
    SendMessageW(ui->hwndEditInterval, WM_SETFONT, (WPARAM)ui->hFontUI, TRUE);
    SetWindowTheme(ui->hwndEditInterval, L" ", L" ");

    // 进度条 + 颜色
    ui->hwndProgress = CreateWindowExW(0, PROGRESS_CLASSW, NULL,
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        0, 0, 0, 0, hwndParent, (HMENU)IDC_PROGRESS, NULL, NULL);
    SendMessageW(ui->hwndProgress, PBM_SETBARCOLOR, 0, CLR_ACCENT);
    SendMessageW(ui->hwndProgress, PBM_SETBKCOLOR, 0, CLR_PANEL_LIGHT);

    // 字符计数 + 状态栏
    ui->hwndStaticChars = MakeStatic(hwndParent, L"0 / 0 字符", IDC_STATIC_CHARS, ui->hFontUI);
    ui->hwndStatus      = MakeStatic(hwndParent, L"就绪",        IDC_STATUS,       ui->hFontUI);
    ui->hwndChkTopmost  = CreateWindowExW(0, L"BUTTON", L"窗口置顶",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0, hwndParent, (HMENU)IDC_CHK_TOPMOST, NULL, NULL);
    SendMessageW(ui->hwndChkTopmost, WM_SETFONT, (WPARAM)ui->hFontUI, TRUE);

    ui->hwndChkFuzzy = CreateWindowExW(0, L"BUTTON", L"模糊搜索",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0, hwndParent, (HMENU)IDC_CHK_FUZZY, NULL, NULL);
    SendMessageW(ui->hwndChkFuzzy, WM_SETFONT, (WPARAM)ui->hFontUI, TRUE);
    SendMessageW(ui->hwndChkFuzzy, BM_SETCHECK, BST_CHECKED, 0);

    ui->hwndChkDark = CreateWindowExW(0, L"BUTTON", L"深色模式",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0, hwndParent, (HMENU)IDC_CHK_DARK, NULL, NULL);
    SendMessageW(ui->hwndChkDark, WM_SETFONT, (WPARAM)ui->hFontUI, TRUE);

    // 从面板读取（默认不勾选，热键读剪切板）
    ui->hwndChkUsePanel = CreateWindowExW(0, L"BUTTON", L"从面板读取",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0, hwndParent, (HMENU)IDC_CHK_USE_PANEL, NULL, NULL);
    SendMessageW(ui->hwndChkUsePanel, WM_SETFONT, (WPARAM)ui->hFontUI, TRUE);

    ui->hwndChkCodeMode = CreateWindowExW(0, L"BUTTON",
        L"在线网站 Python 补偿（含行首缩进过滤）",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0, hwndParent, (HMENU)IDC_CHK_CODE_MODE, NULL, NULL);
    SendMessageW(ui->hwndChkCodeMode, WM_SETFONT, (WPARAM)ui->hFontUI, TRUE);

    // 使用提示
    ui->hwndStaticHint = MakeStatic(hwndParent,
        L"Ctrl+Alt+V 开始输入\nCtrl+Alt+B 搜索答案\nCtrl+Alt+S 停止输入",
        0, ui->hFontUI);

    UI_SetState(ui, STATE_IDLE);
}

void UI_Layout(AppUI *ui, int cx, int cy)
{
    // 所有常数都是「逻辑像素」，经 UI_Scale 换算到当前 DPI，
    // 与字体使用同一个 DPI，保证两者同步缩放。
    int pad     = UI_Scale(ui, 12);
    int btnH    = UI_Scale(ui, 34);
    int editW   = cx * 60 / 100;
    int panelX  = editW + pad * 2;
    int panelW  = cx - panelX - pad;
    int statusH = UI_Scale(ui, 24);
    int progressH = UI_Scale(ui, 6);
    int bottomH = statusH + progressH + pad * 2 + UI_Scale(ui, 4);

    // 文本框
    SetWindowPos(ui->hwndEditText, NULL,
        pad, pad,
        editW - pad, cy - bottomH - pad * 2,
        SWP_NOZORDER);

    int y = pad;

    int labelH = UI_Scale(ui, 18);
    int labelGap = UI_Scale(ui, 22);
    int rowH   = UI_Scale(ui, 24);
    int hintH  = UI_Scale(ui, 70);

    SetWindowPos(ui->hwndBtnLoad, NULL, panelX, y, panelW, btnH, SWP_NOZORDER);
    y += btnH + UI_Scale(ui, 6);

    SetWindowPos(ui->hwndBtnDatabase, NULL, panelX, y, panelW, btnH, SWP_NOZORDER);
    y += btnH + pad;

    SetWindowPos(ui->hwndStaticPreset, NULL, panelX, y, panelW, labelH, SWP_NOZORDER);
    y += labelGap;
    // 下拉列表的高度参数是展开后的总高，按 DPI 一并放大
    SetWindowPos(ui->hwndComboPreset, NULL, panelX, y, panelW,
                 UI_Scale(ui, 120), SWP_NOZORDER);
    y += btnH + pad;

    SetWindowPos(ui->hwndStaticInterval, NULL, panelX, y, panelW, labelH, SWP_NOZORDER);
    y += labelGap;
    SetWindowPos(ui->hwndTrackbar, NULL, panelX, y, panelW,
                 UI_Scale(ui, 26), SWP_NOZORDER);
    y += UI_Scale(ui, 28);
    SetWindowPos(ui->hwndEditInterval, NULL,
        panelX + panelW / 2 - UI_Scale(ui, 28), y,
        UI_Scale(ui, 56), rowH, SWP_NOZORDER);
    y += UI_Scale(ui, 30) + pad;

    SetWindowPos(ui->hwndChkUsePanel, NULL, panelX, y, panelW, rowH, SWP_NOZORDER);
    y += rowH + pad;

    SetWindowPos(ui->hwndChkCodeMode, NULL, panelX, y, panelW, rowH, SWP_NOZORDER);
    y += rowH + pad;

    SetWindowPos(ui->hwndStaticHint, NULL, panelX, y, panelW, hintH, SWP_NOZORDER);
    y += hintH + pad * 2;

    SetWindowPos(ui->hwndStaticChars, NULL, panelX, y, panelW, labelH, SWP_NOZORDER);

    // 底部：进度条通栏，下面一行是状态文字 + 三个开关
    {
        int bottomY = cy - bottomH;
        int chkW    = UI_Scale(ui, 90);
        int rowY    = bottomY + progressH + pad;

        SetWindowPos(ui->hwndProgress, NULL, 0, bottomY, cx, progressH, SWP_NOZORDER);

        SetWindowPos(ui->hwndStatus, NULL,
            pad, rowY,
            cx - pad * 4 - chkW * 3, statusH, SWP_NOZORDER);
        SetWindowPos(ui->hwndChkDark, NULL,
            cx - pad * 3 - chkW * 3, rowY, chkW, statusH, SWP_NOZORDER);
        SetWindowPos(ui->hwndChkFuzzy, NULL,
            cx - pad * 2 - chkW * 2, rowY, chkW, statusH, SWP_NOZORDER);
        SetWindowPos(ui->hwndChkTopmost, NULL,
            cx - pad - chkW, rowY, chkW, statusH, SWP_NOZORDER);
    }
}

// ── DPI 变化与窗口尺寸 ───────────────────────────────────

static BOOL CALLBACK ApplyFontProc(HWND hwndChild, LPARAM lParam)
{
    SendMessageW(hwndChild, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

void UI_UpdateDpi(AppUI *ui, int dpi)
{
    HFONT oldUI   = ui->hFontUI;
    HFONT oldEdit = ui->hFontEdit;

    if (dpi < 72) dpi = 96;
    if (dpi == ui->dpi) return;

    ui->dpi = dpi;
    ui->hFontUI   = CreateUIFont(10, FALSE, dpi);
    ui->hFontEdit = CreateUIFont(11, FALSE, dpi);
    if (!ui->hFontUI)   ui->hFontUI = oldUI;
    if (!ui->hFontEdit) ui->hFontEdit = oldEdit;

    // 先给所有子控件套 UI 字体，再把文本框单独换成稍大的编辑字体
    EnumChildWindows(ui->hwndMain, ApplyFontProc, (LPARAM)ui->hFontUI);
    if (ui->hwndEditText) {
        SendMessageW(ui->hwndEditText, WM_SETFONT, (WPARAM)ui->hFontEdit, TRUE);
    }

    if (oldUI   && oldUI   != ui->hFontUI)   DeleteObject(oldUI);
    if (oldEdit && oldEdit != ui->hFontEdit) DeleteObject(oldEdit);

    InvalidateRect(ui->hwndMain, NULL, TRUE);
}

void UI_ResizeToScaled(HWND hwnd, AppUI *ui, int logicalW, int logicalH)
{
    int w = UI_Scale(ui, logicalW);
    int h = UI_Scale(ui, logicalH);
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {0};

    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMon, &mi)) {
        int availW = mi.rcWork.right - mi.rcWork.left;
        int availH = mi.rcWork.bottom - mi.rcWork.top;
        // 高缩放的小屏上不要超出工作区
        if (w > availW) w = availW;
        if (h > availH) h = availH;
        SetWindowPos(hwnd, NULL,
                     mi.rcWork.left + (availW - w) / 2,
                     mi.rcWork.top  + (availH - h) / 2,
                     w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        SetWindowPos(hwnd, NULL, 0, 0, w, h,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

// ── 深色模式 ─────────────────────────────────────────────

void UI_SetDarkMode(AppUI *ui, BOOL dark)
{
    HBRUSH oldBg    = ui->hbrBg;
    HBRUSH oldPanel = ui->hbrPanel;
    HBRUSH oldEdit  = ui->hbrEdit;

    ui->darkMode = dark;
    ui->hbrBg    = CreateSolidBrush(dark ? CLR_BG_DARK    : CLR_BG_LIGHT);
    ui->hbrPanel = CreateSolidBrush(dark ? CLR_PANEL_DARK : CLR_PANEL_LIGHT);
    ui->hbrEdit  = CreateSolidBrush(dark ? CLR_EDIT_DARK  : CLR_PANEL_LIGHT);

    // 分配失败就保留旧画刷，避免出现 NULL 画刷导致绘制异常
    if (!ui->hbrBg)    { ui->hbrBg = oldBg;       oldBg = NULL; }
    if (!ui->hbrPanel) { ui->hbrPanel = oldPanel; oldPanel = NULL; }
    if (!ui->hbrEdit)  { ui->hbrEdit = oldEdit;   oldEdit = NULL; }

    if (oldBg    && oldBg    != ui->hbrBg)    DeleteObject(oldBg);
    if (oldPanel && oldPanel != ui->hbrPanel) DeleteObject(oldPanel);
    if (oldEdit  && oldEdit  != ui->hbrEdit)  DeleteObject(oldEdit);

    if (ui->hwndChkDark) {
        SendMessageW(ui->hwndChkDark, BM_SETCHECK,
                     dark ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    // 启用视觉样式的复选框由主题绘制文字，会忽略 WM_CTLCOLORSTATIC 的
    // SetTextColor——深色底上文字仍是深色，等于看不见。
    // 深色模式下关掉这几个控件的主题（改为经典绘制，会遵守我们的颜色），
    // 切回浅色时再恢复主题。
    {
        HWND checks[] = {
            ui->hwndChkTopmost, ui->hwndChkFuzzy, ui->hwndChkUsePanel,
            ui->hwndChkCodeMode, ui->hwndChkDark
        };
        int i;
        for (i = 0; i < (int)(sizeof(checks) / sizeof(checks[0])); ++i) {
            if (!checks[i]) continue;
            SetWindowTheme(checks[i], dark ? L"" : NULL, dark ? L"" : NULL);
            InvalidateRect(checks[i], NULL, TRUE);
        }
    }

    // 进度条背景跟随配色
    if (ui->hwndProgress) {
        SendMessageW(ui->hwndProgress, PBM_SETBKCOLOR, 0,
                     dark ? CLR_PANEL_DARK : CLR_PANEL_LIGHT);
    }

    UI_ApplyWindowStyling(ui->hwndMain, dark);
    InvalidateRect(ui->hwndMain, NULL, TRUE);
    {
        int i;
        for (i = 0; i < ui->btnCount; ++i) {
            InvalidateRect(ui->btnHwnds[i], NULL, TRUE);
        }
    }
}

void UI_SetState(AppUI *ui, int state)
{
    EnableWindow(ui->hwndBtnLoad,      state == STATE_IDLE);
    EnableWindow(ui->hwndBtnDatabase,  TRUE);
    EnableWindow(ui->hwndTrackbar,     state == STATE_IDLE);
    EnableWindow(ui->hwndEditInterval, state == STATE_IDLE);
    EnableWindow(ui->hwndComboPreset,  state == STATE_IDLE);
    EnableWindow(ui->hwndChkUsePanel,  state == STATE_IDLE);
    EnableWindow(ui->hwndChkCodeMode, state == STATE_IDLE);

    // 强制重绘按钮（状态切换后颜色更新）
    for (int i = 0; i < ui->btnCount; i++)
        InvalidateRect(ui->btnHwnds[i], NULL, FALSE);
}

void UI_UpdateProgress(AppUI *ui, int current, int total)
{
    wchar_t buf[64];
    if (total > 0) {
        SendMessageW(ui->hwndProgress, PBM_SETRANGE32, 0, total);
        SendMessageW(ui->hwndProgress, PBM_SETPOS, current, 0);
    }
    wsprintfW(buf, L"%d / %d 字符", current, total);
    SetWindowTextW(ui->hwndStaticChars, buf);
}

void UI_SetStatus(AppUI *ui, const wchar_t *text)
{
    SetWindowTextW(ui->hwndStatus, text);
}

void UI_SetIntervalText(AppUI *ui, int delayMs)
{
    wchar_t buf[16];
    wsprintfW(buf, L"%d", delayMs);
    SetWindowTextW(ui->hwndEditInterval, buf);
}

void UI_SetHotkeyHint(AppUI *ui, BOOL okStart, BOOL okSearch, BOOL okStop)
{
    wchar_t hint[256];

    if (!ui->hwndStaticHint) return;

    wsprintfW(hint,
              L"Ctrl+Alt+V 开始输入%s\n"
              L"Ctrl+Alt+B 搜索答案%s\n"
              L"Ctrl+Alt+S 停止输入%s",
              okStart  ? L"" : L"（被占用）",
              okSearch ? L"" : L"（被占用）",
              okStop   ? L"" : L"（被占用）");
    SetWindowTextW(ui->hwndStaticHint, hint);
}

void UI_SetPresetSelection(AppUI *ui, int delayMs)
{
    if      (delayMs >= SPEED_SLOW)   SendMessageW(ui->hwndComboPreset, CB_SETCURSEL, 0, 0);
    else if (delayMs >= SPEED_MEDIUM) SendMessageW(ui->hwndComboPreset, CB_SETCURSEL, 1, 0);
    else                              SendMessageW(ui->hwndComboPreset, CB_SETCURSEL, 2, 0);
}

void UI_ApplyWindowStyling(HWND hwnd, BOOL darkMode)
{
    ApplyDarkMode(hwnd, darkMode);
}

void UI_Destroy(AppUI *ui)
{
    if (ui->hFontUI)   { DeleteObject(ui->hFontUI);   ui->hFontUI   = NULL; }
    if (ui->hFontEdit) { DeleteObject(ui->hFontEdit); ui->hFontEdit = NULL; }
    if (ui->hbrBg)     { DeleteObject(ui->hbrBg);     ui->hbrBg     = NULL; }
    if (ui->hbrPanel)  { DeleteObject(ui->hbrPanel);  ui->hbrPanel  = NULL; }
    if (ui->hbrEdit)   { DeleteObject(ui->hbrEdit);   ui->hbrEdit   = NULL; }
}

// ── owner-draw 绘制按钮 ──────────────────────────────────

LRESULT UI_OnDrawItem(AppUI *ui, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lParam;
    if (dis->CtlType != ODT_BUTTON) return FALSE;

    int i = FindBtnIdx(ui, dis->hwndItem);
    if (i < 0) return FALSE;

    BOOL enabled = IsWindowEnabled(dis->hwndItem);
    BOOL hover   = ui->btnHover[i]   && enabled;
    BOOL pressed = ui->btnPressed[i] && enabled;

    // 确定底色
    COLORREF clrFill;
    COLORREF clrText = enabled
        ? (ui->darkMode ? CLR_TEXT_DARK : CLR_TEXT_LIGHT)
        : (ui->darkMode ? RGB(110, 112, 128) : RGB(140, 142, 150));

    switch (ui->btnRoles[i]) {
    case BTN_ROLE_ACCENT:
        clrText = RGB(255, 255, 255);
        clrFill = pressed ? CLR_ACCENT_PRESS
                : hover   ? CLR_ACCENT_HOVER
                :           CLR_ACCENT;
        break;
    case BTN_ROLE_WARN:
        clrText = RGB(255, 255, 255);
        clrFill = pressed ? RGB(160, 100, 10)
                : hover   ? RGB(220, 160, 50)
                :           CLR_BTN_WARN;
        break;
    case BTN_ROLE_DANGER:
        clrText = RGB(255, 255, 255);
        clrFill = pressed ? RGB(170, 30, 30)
                : hover   ? RGB(240, 80, 80)
                :           CLR_BTN_DANGER;
        break;
    default: // NEUTRAL
        if (ui->darkMode) {
            clrFill = pressed ? RGB(58,  60,  78)
                    : hover   ? RGB(50,  52,  68)
                    :           RGB(42,  44,  58);
        } else {
            clrFill = pressed ? RGB(190, 194, 205)
                    : hover   ? RGB(220, 224, 234)
                    :           RGB(235, 237, 243);
        }
        break;
    }

    if (!enabled) {
        clrFill = ui->darkMode ? RGB(36, 37, 48) : RGB(225, 227, 232);
    }

    HDC  hdc  = dis->hDC;
    RECT rc   = dis->rcItem;
    int  r    = 6;  // 圆角半径

    // 填充圆角矩形
    HBRUSH hbr = CreateSolidBrush(clrFill);
    HRGN   rgn = CreateRoundRectRgn(rc.left, rc.top, rc.right, rc.bottom, r*2, r*2);
    FillRgn(hdc, rgn, hbr);
    DeleteObject(rgn);
    DeleteObject(hbr);

    // 文字
    wchar_t text[64] = {0};
    GetWindowTextW(dis->hwndItem, text, 63);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, clrText);
    HFONT oldFont = (HFONT)SelectObject(hdc, ui->hFontUI);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);

    return TRUE;
}

// ── 控件背景着色 ──────────────────────────────────────────

LRESULT UI_OnCtlColor(AppUI *ui, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)hwnd;
    (void)msg;
    HDC   hdc     = (HDC)wParam;
    HWND  hwndCtl = (HWND)lParam;

    if (hwndCtl == ui->hwndEditText || hwndCtl == ui->hwndEditInterval) {
        SetTextColor(hdc, ui->darkMode ? CLR_TEXT_DARK : CLR_TEXT_LIGHT);
        SetBkColor(hdc, ui->darkMode ? CLR_EDIT_DARK : CLR_PANEL_LIGHT);
        return (LRESULT)ui->hbrEdit;
    }

    // 所有 STATIC 标签与复选框
    SetTextColor(hdc, ui->darkMode ? CLR_TEXT_DARK : RGB(70, 72, 82));
    SetBkColor(hdc, ui->darkMode ? CLR_BG_DARK : CLR_BG_LIGHT);
    return (LRESULT)ui->hbrBg;
}

// ── 鼠标 hover 跟踪 ──────────────────────────────────────

void UI_OnMouseMove(AppUI *ui, HWND hwndBtn)
{
    int i = FindBtnIdx(ui, hwndBtn);
    if (i < 0) return;

    if (!ui->btnTracking[i]) {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwndBtn, 0 };
        TrackMouseEvent(&tme);
        ui->btnTracking[i] = TRUE;
    }

    if (!ui->btnHover[i]) {
        ui->btnHover[i] = TRUE;
        InvalidateRect(hwndBtn, NULL, FALSE);
    }
}

void UI_OnMouseLeave(AppUI *ui, HWND hwndBtn)
{
    int i = FindBtnIdx(ui, hwndBtn);
    if (i < 0) return;
    ui->btnHover[i]    = FALSE;
    ui->btnTracking[i] = FALSE;
    InvalidateRect(hwndBtn, NULL, FALSE);
}
