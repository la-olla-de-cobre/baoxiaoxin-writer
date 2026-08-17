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

        // Win11：标题栏与窗口圆角。旧系统会直接忽略这个属性，无需判版本。
        {
            DWORD corner = 2;  // DWMWCP_ROUND
            pfn(hwnd, 33, &corner, sizeof(corner)); // DWMWA_WINDOW_CORNER_PREFERENCE
        }
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
    ui->dpi        = UI_GetWindowDpi(hwndParent);
    ui->hFontUI    = CreateUIFont(10, FALSE, ui->dpi);
    ui->hFontEdit  = CreateUIFont(11, FALSE, ui->dpi);
    ui->hFontLabel = CreateUIFont(9,  FALSE, ui->dpi);
    ui->sepCount   = 0;

    // 缓存画刷（浅色初值，UI_SetDarkMode 会按需重建）
    ui->hbrBg    = CreateSolidBrush(CLR_BG_LIGHT);
    ui->hbrPanel = CreateSolidBrush(CLR_PANEL_LIGHT);
    ui->hbrEdit  = CreateSolidBrush(CLR_PANEL_LIGHT);

    // 多行文本框。不用 WS_EX_CLIENTEDGE：那是 Win95 风格的凹陷 3D 边框，
    // 是「粗糙、太方」观感的主要来源。改为无边框，由外层卡片提供圆角描边。
    ui->hwndEditText = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
        0, 0, 0, 0, hwndParent,
        (HMENU)IDC_EDIT_TEXT, NULL, NULL);
    SendMessageW(ui->hwndEditText, WM_SETFONT, (WPARAM)ui->hFontEdit, TRUE);
    SendMessageW(ui->hwndEditText, EM_SETLIMITTEXT, 0, 0);
    // 保留主题：剥掉主题会让滚动条退回 Win95 的 ▲▼ 箭头样式

    // 右侧按钮（owner-draw）
    ui->hwndBtnLoad    = MakeOwnerDrawBtn(hwndParent, L"从文件加载", IDC_BTN_LOAD,    ui->hFontUI, ui, BTN_ROLE_NEUTRAL);
    ui->hwndBtnDatabase= MakeOwnerDrawBtn(hwndParent, L"数据库功能", IDC_BTN_DATABASE, ui->hFontUI, ui, BTN_ROLE_NEUTRAL);

    // 速度预设
    ui->hwndStaticPreset = MakeStatic(hwndParent, L"速度预设", IDC_STATIC_PRESET, ui->hFontUI);
    ui->hwndComboPreset  = CreateWindowExW(0, L"COMBOBOX", NULL,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 0, 0, hwndParent, (HMENU)IDC_COMBO_PRESET, NULL, NULL);
    SendMessageW(ui->hwndComboPreset, WM_SETFONT, (WPARAM)ui->hFontUI, TRUE);
    SendMessageW(ui->hwndComboPreset, CB_ADDSTRING, 0, (LPARAM)L"慢速 (300ms)");
    SendMessageW(ui->hwndComboPreset, CB_ADDSTRING, 0, (LPARAM)L"中速 (80ms)");
    SendMessageW(ui->hwndComboPreset, CB_ADDSTRING, 0, (LPARAM)L"快速 (20ms)");
    SendMessageW(ui->hwndComboPreset, CB_SETCURSEL, 1, 0);

    // 间隔调节
    ui->hwndStaticInterval = MakeStatic(hwndParent, L"字符间隔 (ms)", IDC_STATIC_INTERVAL, ui->hFontUI);

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

    // 分组标题与提示用小一号字体，和内容形成层次
    SendMessageW(ui->hwndStaticPreset,   WM_SETFONT, (WPARAM)ui->hFontLabel, TRUE);
    SendMessageW(ui->hwndStaticInterval, WM_SETFONT, (WPARAM)ui->hFontLabel, TRUE);

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

    // BS_MULTILINE：标签较长，窄栏里必须允许折行，否则会顶到卡片边框
    ui->hwndChkCodeMode = CreateWindowExW(0, L"BUTTON",
        L"在线网站 Python 补偿（含行首缩进过滤）",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_MULTILINE | BS_TOP,
        0, 0, 0, 0, hwndParent, (HMENU)IDC_CHK_CODE_MODE, NULL, NULL);
    SendMessageW(ui->hwndChkCodeMode, WM_SETFONT, (WPARAM)ui->hFontUI, TRUE);

    // 使用提示
    ui->hwndStaticHint = MakeStatic(hwndParent,
        L"Ctrl+Alt+V 开始输入\nCtrl+Alt+B 搜索答案\nCtrl+Alt+S 停止输入",
        0, ui->hFontLabel);

    UI_SetState(ui, STATE_IDLE);
}

void UI_Layout(AppUI *ui, int cx, int cy)
{
    // 所有常数都是「逻辑像素」，经 UI_Scale 换算到当前 DPI，与字体共用
    // 同一个 DPI，保证两者同步缩放。间距统一取 GRID(4) 的整数倍。
    int outer   = UI_Scale(ui, GRID * 4);   // 窗口外边距 16
    int gutter  = UI_Scale(ui, GRID * 3);   // 卡片之间 12
    int inner   = UI_Scale(ui, GRID * 4);   // 卡片内边距 16
    int gapS    = UI_Scale(ui, GRID * 2);   // 组内间距 8
    int gapL    = UI_Scale(ui, GRID * 4);   // 组间间距 16

    int btnH    = UI_Scale(ui, 32);
    int rowH    = UI_Scale(ui, 24);
    int labelH  = UI_Scale(ui, 16);
    int statusH = UI_Scale(ui, 22);
    int barH    = UI_Scale(ui, GRID);       // 细进度条 4

    // 底部条：进度条 + 一行状态与开关
    int bottomH = barH + gapS + statusH + outer;
    int cardsH  = cy - bottomH - outer;
    int textW   = (cx - outer * 2 - gutter) * 58 / 100;

    if (cardsH < UI_Scale(ui, 120)) cardsH = UI_Scale(ui, 120);

    // 两块卡片的位置交给 UI_OnEraseBkgnd 画
    SetRect(&ui->rcCardText, outer, outer, outer + textW, outer + cardsH);
    SetRect(&ui->rcCardPanel, outer + textW + gutter, outer,
            cx - outer, outer + cardsH);

    // 文本框贴在左卡片内侧，卡片描边充当它的边框
    SetWindowPos(ui->hwndEditText, NULL,
        ui->rcCardText.left + inner, ui->rcCardText.top + inner,
        (ui->rcCardText.right - ui->rcCardText.left) - inner * 2,
        (ui->rcCardText.bottom - ui->rcCardText.top) - inner * 2,
        SWP_NOZORDER);

    // ── 右卡片内部 ───────────────────────────────────────
    {
        int px = ui->rcCardPanel.left + inner;
        int pw = (ui->rcCardPanel.right - ui->rcCardPanel.left) - inner * 2;
        int y  = ui->rcCardPanel.top + inner;

        ui->sepCount = 0;

        // 组 1：动作按钮
        SetWindowPos(ui->hwndBtnLoad, NULL, px, y, pw, btnH, SWP_NOZORDER);
        y += btnH + gapS;
        SetWindowPos(ui->hwndBtnDatabase, NULL, px, y, pw, btnH, SWP_NOZORDER);
        y += btnH + gapL;

        if (ui->sepCount < 4) ui->sepY[ui->sepCount++] = y - gapL / 2;

        // 组 2：速度
        SetWindowPos(ui->hwndStaticPreset, NULL, px, y, pw, labelH, SWP_NOZORDER);
        y += labelH + UI_Scale(ui, GRID);
        // 下拉框的高度参数是展开后总高，单独放大
        SetWindowPos(ui->hwndComboPreset, NULL, px, y, pw,
                     UI_Scale(ui, 120), SWP_NOZORDER);
        y += rowH + gapS;

        SetWindowPos(ui->hwndStaticInterval, NULL, px, y, pw, labelH, SWP_NOZORDER);
        y += labelH + UI_Scale(ui, GRID);
        {
            int numW = UI_Scale(ui, 52);
            int slW  = pw - numW - gapS;
            // 滑杆与数字并排，省一行高度也更像一组
            SetWindowPos(ui->hwndTrackbar, NULL, px, y, slW,
                         UI_Scale(ui, 26), SWP_NOZORDER);
            SetWindowPos(ui->hwndEditInterval, NULL,
                         px + slW + gapS, y + UI_Scale(ui, 2),
                         numW, rowH, SWP_NOZORDER);
        }
        y += UI_Scale(ui, 26) + gapL;

        if (ui->sepCount < 4) ui->sepY[ui->sepCount++] = y - gapL / 2;

        // 组 3：输入选项
        SetWindowPos(ui->hwndChkUsePanel, NULL, px, y, pw, rowH, SWP_NOZORDER);
        y += rowH + gapS;
        SetWindowPos(ui->hwndChkCodeMode, NULL, px, y, pw, rowH * 2, SWP_NOZORDER);
        y += rowH * 2 + gapL;

        if (ui->sepCount < 4) ui->sepY[ui->sepCount++] = y - gapL / 2;

        // 组 4：热键提示与计数，贴卡片底部
        {
            int hintH  = UI_Scale(ui, 78);
            int blockH = hintH + labelH;
            int by = ui->rcCardPanel.bottom - inner - blockH;
            if (by < y) by = y;

            SetWindowPos(ui->hwndStaticHint, NULL, px, by, pw, hintH, SWP_NOZORDER);
            SetWindowPos(ui->hwndStaticChars, NULL, px, by + hintH, pw, labelH,
                         SWP_NOZORDER);
        }
    }

    // ── 底部条 ───────────────────────────────────────────
    {
        int barY  = ui->rcCardPanel.bottom + gapL;
        int rowY  = barY + barH + gapS;
        int chkW  = UI_Scale(ui, 84);

        // 进度条内缩到与卡片同宽，不再通栏贴边
        SetWindowPos(ui->hwndProgress, NULL,
                     outer, barY, cx - outer * 2, barH, SWP_NOZORDER);

        SetWindowPos(ui->hwndStatus, NULL,
            outer, rowY,
            cx - outer * 2 - chkW * 3 - gapS * 3, statusH, SWP_NOZORDER);
        SetWindowPos(ui->hwndChkDark, NULL,
            cx - outer - chkW * 3 - gapS * 2, rowY, chkW, statusH, SWP_NOZORDER);
        SetWindowPos(ui->hwndChkFuzzy, NULL,
            cx - outer - chkW * 2 - gapS, rowY, chkW, statusH, SWP_NOZORDER);
        SetWindowPos(ui->hwndChkTopmost, NULL,
            cx - outer - chkW, rowY, chkW, statusH, SWP_NOZORDER);
    }
}

// ── 背景绘制：底色 + 圆角卡片 + 分组分隔线 ───────────────

void UI_DrawCard(HDC hdc, const RECT *rc, int radius,
                 HBRUSH fill, COLORREF border)
{
    HRGN rgn = CreateRoundRectRgn(rc->left, rc->top, rc->right, rc->bottom,
                                  radius * 2, radius * 2);
    if (!rgn) {
        FillRect(hdc, rc, fill);
        return;
    }
    FillRgn(hdc, rgn, fill);
    {
        HBRUSH hbrBorder = CreateSolidBrush(border);
        if (hbrBorder) {
            FrameRgn(hdc, rgn, hbrBorder, 1, 1);
            DeleteObject(hbrBorder);
        }
    }
    DeleteObject(rgn);
}

// 把底色、卡片、分隔线画到给定 DC 上（不含双缓冲）
static void DrawBackgroundInto(AppUI *ui, HDC hdc, const RECT *rcClient)
{
    COLORREF border = ui->darkMode ? CLR_BORDER : CLR_BORDER_LIGHT;
    int radius = UI_Scale(ui, CARD_RADIUS);

    FillRect(hdc, rcClient, ui->hbrBg);

    // 卡片尺寸尚未算出（首次绘制早于 WM_SIZE）就只铺底色
    if (ui->rcCardPanel.right <= ui->rcCardPanel.left) return;

    UI_DrawCard(hdc, &ui->rcCardText,  radius, ui->hbrEdit,  border);
    UI_DrawCard(hdc, &ui->rcCardPanel, radius, ui->hbrPanel, border);

    // 面板内的分组分隔线
    {
        HBRUSH hbrSep = CreateSolidBrush(border);
        int inset = UI_Scale(ui, GRID * 3);
        int i;

        if (hbrSep) {
            for (i = 0; i < ui->sepCount; ++i) {
                RECT line;
                line.left   = ui->rcCardPanel.left + inset;
                line.right  = ui->rcCardPanel.right - inset;
                line.top    = ui->sepY[i];
                line.bottom = ui->sepY[i] + 1;
                if (line.top > ui->rcCardPanel.top &&
                    line.bottom < ui->rcCardPanel.bottom) {
                    FillRect(hdc, &line, hbrSep);
                }
            }
            DeleteObject(hbrSep);
        }
    }
}

void UI_PaintBackground(AppUI *ui, HDC hdcTarget)
{
    RECT rcClient;
    HDC     memDC;
    HBITMAP memBmp, oldBmp;

    GetClientRect(ui->hwndMain, &rcClient);
    if (rcClient.right <= 0 || rcClient.bottom <= 0) return;

    // 离屏画好再整块拷过去：否则「刷底色」和「画卡片」两步都会被看到
    memDC = CreateCompatibleDC(hdcTarget);
    if (!memDC) {
        DrawBackgroundInto(ui, hdcTarget, &rcClient);
        return;
    }
    memBmp = CreateCompatibleBitmap(hdcTarget, rcClient.right, rcClient.bottom);
    if (!memBmp) {
        DeleteDC(memDC);
        DrawBackgroundInto(ui, hdcTarget, &rcClient);
        return;
    }

    oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
    DrawBackgroundInto(ui, memDC, &rcClient);
    BitBlt(hdcTarget, 0, 0, rcClient.right, rcClient.bottom, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}

// ── DPI 变化与窗口尺寸 ───────────────────────────────────

static BOOL CALLBACK ApplyFontProc(HWND hwndChild, LPARAM lParam)
{
    SendMessageW(hwndChild, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

void UI_UpdateDpi(AppUI *ui, int dpi)
{
    HFONT oldUI    = ui->hFontUI;
    HFONT oldEdit  = ui->hFontEdit;
    HFONT oldLabel = ui->hFontLabel;

    if (dpi < 72) dpi = 96;
    if (dpi == ui->dpi) return;

    ui->dpi = dpi;
    ui->hFontUI    = CreateUIFont(10, FALSE, dpi);
    ui->hFontEdit  = CreateUIFont(11, FALSE, dpi);
    ui->hFontLabel = CreateUIFont(9,  FALSE, dpi);
    if (!ui->hFontUI)    ui->hFontUI    = oldUI;
    if (!ui->hFontEdit)  ui->hFontEdit  = oldEdit;
    if (!ui->hFontLabel) ui->hFontLabel = oldLabel;

    // 先给所有子控件套 UI 字体，再把文本框和小号标签单独换回去
    EnumChildWindows(ui->hwndMain, ApplyFontProc, (LPARAM)ui->hFontUI);
    if (ui->hwndEditText) {
        SendMessageW(ui->hwndEditText, WM_SETFONT, (WPARAM)ui->hFontEdit, TRUE);
    }
    {
        HWND labels[] = { ui->hwndStaticPreset, ui->hwndStaticInterval,
                          ui->hwndStaticHint };
        int i;
        for (i = 0; i < (int)(sizeof(labels) / sizeof(labels[0])); ++i) {
            if (labels[i]) {
                SendMessageW(labels[i], WM_SETFONT, (WPARAM)ui->hFontLabel, TRUE);
            }
        }
    }

    if (oldUI    && oldUI    != ui->hFontUI)    DeleteObject(oldUI);
    if (oldEdit  && oldEdit  != ui->hFontEdit)  DeleteObject(oldEdit);
    if (oldLabel && oldLabel != ui->hFontLabel) DeleteObject(oldLabel);

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

// ── 滑杆自绘 ─────────────────────────────────────────────
// 滑道由系统主题绘制，深色下是亮白的，而且没有可用的深色主题类
// （DarkMode_Explorer 对滑杆无效）。所以走 NM_CUSTOMDRAW 自己画：
// 滑道画成两段（已选部分用强调色），滑块画成圆角块。

LRESULT UI_OnTrackbarCustomDraw(AppUI *ui, LPARAM lParam)
{
    LPNMCUSTOMDRAW nmcd = (LPNMCUSTOMDRAW)lParam;

    if (nmcd->hdr.hwndFrom != ui->hwndTrackbar) {
        return CDRF_DODEFAULT;
    }

    switch (nmcd->dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;

    case CDDS_ITEMPREPAINT:
        if (nmcd->dwItemSpec == TBCD_CHANNEL) {
            RECT rc = nmcd->rc;
            int  pos = (int)SendMessageW(ui->hwndTrackbar, TBM_GETPOS, 0, 0);
            int  lo  = (int)SendMessageW(ui->hwndTrackbar, TBM_GETRANGEMIN, 0, 0);
            int  hi  = (int)SendMessageW(ui->hwndTrackbar, TBM_GETRANGEMAX, 0, 0);
            int  h   = UI_Scale(ui, 4);
            int  mid = (rc.top + rc.bottom) / 2;
            HBRUSH hbrTrack, hbrDone;
            RECT rcTrack, rcDone;

            rcTrack.left   = rc.left;
            rcTrack.right  = rc.right;
            rcTrack.top    = mid - h / 2;
            rcTrack.bottom = rcTrack.top + h;

            hbrTrack = CreateSolidBrush(
                ui->darkMode ? RGB(60, 62, 82) : RGB(222, 225, 235));
            if (hbrTrack) {
                FillRect(nmcd->hdc, &rcTrack, hbrTrack);
                DeleteObject(hbrTrack);
            }

            // 已选区间用强调色，让滑杆有明确的读数感
            rcDone = rcTrack;
            if (hi > lo) {
                rcDone.right = rcTrack.left +
                    MulDiv(pos - lo, rcTrack.right - rcTrack.left, hi - lo);
            }
            hbrDone = CreateSolidBrush(CLR_ACCENT);
            if (hbrDone) {
                FillRect(nmcd->hdc, &rcDone, hbrDone);
                DeleteObject(hbrDone);
            }
            return CDRF_SKIPDEFAULT;
        }
        if (nmcd->dwItemSpec == TBCD_THUMB) {
            RECT rc = nmcd->rc;
            int  r  = UI_Scale(ui, 3);
            HRGN rgn = CreateRoundRectRgn(rc.left, rc.top, rc.right, rc.bottom,
                                          r * 2, r * 2);
            HBRUSH hbr = CreateSolidBrush(CLR_ACCENT);

            if (rgn && hbr) {
                FillRgn(nmcd->hdc, rgn, hbr);
            } else if (hbr) {
                FillRect(nmcd->hdc, &rc, hbr);
            }
            if (rgn) DeleteObject(rgn);
            if (hbr) DeleteObject(hbr);
            return CDRF_SKIPDEFAULT;
        }
        if (nmcd->dwItemSpec == TBCD_TICS) {
            return CDRF_SKIPDEFAULT;   // 未启用刻度，直接跳过
        }
        return CDRF_DODEFAULT;
    }
    return CDRF_DODEFAULT;
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

    // 系统主题绘制的控件（滚动条、下拉框、滑杆）不会跟随我们的配色，
    // 深色下会留下几块刺眼的白。用 Windows 的深色主题类名切换：
    // DarkMode_Explorer 管滚动条，DarkMode_CFD 管下拉框。
    // 这两个类名未公开但自 Win10 1809 起可用；取不到时 SetWindowTheme
    // 会安静失败，退回浅色外观，不影响功能。
    if (ui->hwndEditText) {
        SetWindowTheme(ui->hwndEditText, dark ? L"DarkMode_Explorer" : NULL, NULL);
    }
    if (ui->hwndComboPreset) {
        SetWindowTheme(ui->hwndComboPreset, dark ? L"DarkMode_CFD" : NULL, NULL);
    }
    // 滑杆由 UI_OnTrackbarCustomDraw 自绘（见那里的说明），
    // 这里只需要触发重绘。
    if (ui->hwndTrackbar) {
        InvalidateRect(ui->hwndTrackbar, NULL, TRUE);
    }

    // 进度条：启用视觉样式时会忽略 PBM_SETBKCOLOR，
    // 所以深色下先关掉它的主题，颜色设置才生效。
    if (ui->hwndProgress) {
        SetWindowTheme(ui->hwndProgress, dark ? L"" : NULL, dark ? L"" : NULL);
        SendMessageW(ui->hwndProgress, PBM_SETBKCOLOR, 0,
                     dark ? CLR_PANEL_DARK : CLR_PANEL_LIGHT);
        SendMessageW(ui->hwndProgress, PBM_SETBARCOLOR, 0, CLR_ACCENT);
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

void UI_SetHotkeyHint(AppUI *ui, BOOL okStart, BOOL okSearch, BOOL okStop,
                      BOOL okPause)
{
    wchar_t hint[320];

    if (!ui->hwndStaticHint) return;

    wsprintfW(hint,
              L"Ctrl+Alt+V 开始输入%s\n"
              L"Ctrl+Alt+P 暂停/继续%s\n"
              L"Ctrl+Alt+B 搜索答案%s\n"
              L"Ctrl+Alt+S 停止输入%s",
              okStart  ? L"" : L"（被占用）",
              okPause  ? L"" : L"（被占用）",
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
    if (ui->hFontUI)    { DeleteObject(ui->hFontUI);    ui->hFontUI    = NULL; }
    if (ui->hFontEdit)  { DeleteObject(ui->hFontEdit);  ui->hFontEdit  = NULL; }
    if (ui->hFontLabel) { DeleteObject(ui->hFontLabel); ui->hFontLabel = NULL; }
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
                    : hover   ? RGB(52,  54,  72)
                    :           RGB(44,  46,  60);
        } else {
            // 底色不能太浅，太浅会被读成「禁用」而不是「可点」
            clrFill = pressed ? RGB(214, 218, 230)
                    : hover   ? RGB(232, 235, 244)
                    :           RGB(243, 245, 250);
        }
        break;
    }

    if (!enabled) {
        clrFill = ui->darkMode ? RGB(36, 37, 48) : RGB(238, 239, 243);
    }

    HDC  hdc  = dis->hDC;
    RECT rc   = dis->rcItem;
    int  r    = UI_Scale(ui, 6);  // 圆角半径

    // 填充圆角矩形，并描一圈边界让按钮有明确的可点边缘
    HBRUSH hbr = CreateSolidBrush(clrFill);
    HRGN   rgn = CreateRoundRectRgn(rc.left, rc.top, rc.right, rc.bottom, r*2, r*2);
    FillRgn(hdc, rgn, hbr);
    if (enabled && ui->btnRoles[i] == BTN_ROLE_NEUTRAL) {
        HBRUSH hbrEdge = CreateSolidBrush(
            ui->darkMode ? RGB(70, 73, 95) : RGB(205, 209, 222));
        if (hbrEdge) {
            FrameRgn(hdc, rgn, hbrEdge, 1, 1);
            DeleteObject(hbrEdge);
        }
    }
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

    if (hwndCtl == ui->hwndEditText) {
        SetTextColor(hdc, ui->darkMode ? CLR_TEXT_DARK : CLR_TEXT_LIGHT);
        SetBkColor(hdc, ui->darkMode ? CLR_EDIT_DARK : CLR_PANEL_LIGHT);
        return (LRESULT)ui->hbrEdit;
    }
    if (hwndCtl == ui->hwndEditInterval) {
        SetTextColor(hdc, ui->darkMode ? CLR_TEXT_DARK : CLR_TEXT_LIGHT);
        SetBkColor(hdc, ui->darkMode ? CLR_PANEL_DARK : CLR_PANEL_LIGHT);
        return (LRESULT)ui->hbrPanel;
    }

    // 分组标题与热键提示用弱化色，和正文形成层次
    if (hwndCtl == ui->hwndStaticPreset || hwndCtl == ui->hwndStaticInterval ||
        hwndCtl == ui->hwndStaticHint   || hwndCtl == ui->hwndStaticChars) {
        SetTextColor(hdc, ui->darkMode ? CLR_TEXT_DIM : CLR_TEXT_DIM_L);
        SetBkColor(hdc, ui->darkMode ? CLR_PANEL_DARK : CLR_PANEL_LIGHT);
        return (LRESULT)ui->hbrPanel;
    }

    // 卡片内的复选框与滑杆：背景要跟卡片一致，否则会出现一块底色不同的方块
    if (hwndCtl == ui->hwndChkUsePanel || hwndCtl == ui->hwndChkCodeMode ||
        hwndCtl == ui->hwndTrackbar) {
        SetTextColor(hdc, ui->darkMode ? CLR_TEXT_DARK : CLR_TEXT_LIGHT);
        SetBkColor(hdc, ui->darkMode ? CLR_PANEL_DARK : CLR_PANEL_LIGHT);
        return (LRESULT)ui->hbrPanel;
    }

    // 底部条上的状态文字与开关，直接落在窗口底色上
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
