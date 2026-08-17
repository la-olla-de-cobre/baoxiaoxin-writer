#define _CRT_SECURE_NO_WARNINGS
// 必须在 windows.h 之前：否则 uxtheme.h 不会声明 SetWindowTheme
#define _WIN32_WINNT 0x0601
#define _WIN32_IE    0x0600
#include <windows.h>
#include <uxtheme.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <string.h>
#include <stdint.h>
#include "qa_ui.h"
#include "mem.h"
#include "textfile.h"
#include "database.h"
#include "resource.h"
#include "ui.h"

// ── 全局状态 ──────────────────────────────────────────────
static QAManagerUI *g_pUI = NULL;
static HWND g_hwndQAManager = NULL;

// ── 控件ID定义 ───────────────────────────────────────────
#define CTRL_ID_RADIO_BATCH      1001
#define CTRL_ID_RADIO_SINGLE     1002
#define CTRL_ID_EDIT_BATCH       1003
#define CTRL_ID_EDIT_QUESTION    1004
#define CTRL_ID_EDIT_ANSWER      1005
#define CTRL_ID_BTN_ADD_SINGLE   1006
#define CTRL_ID_BTN_IMPORT       1007
#define CTRL_ID_LIST_QA          1008
#define CTRL_ID_BTN_DELETE       1009
#define CTRL_ID_BTN_CLOSE        1010
#define CTRL_ID_STATIC_STATUS    1011
#define CTRL_ID_BTN_REFRESH      1012
#define CTRL_ID_BTN_EXPORT       1013

// 逻辑像素 → 当前 DPI 的物理像素。与主窗口共用同一换算方式。
static int QS(const QAManagerUI *pUI, int logical)
{
    int dpi = (pUI && pUI->dpi >= 72) ? pUI->dpi : 96;
    return MulDiv(logical, dpi, 96);
}

// 统一创建控件并套用字体，避免个别控件漏设字体退回系统默认字体
static HWND QAMake(HWND parent, const wchar_t *cls, const wchar_t *text,
                   DWORD style, DWORD exStyle, int id, HFONT font,
                   int x, int y, int w, int h)
{
    HWND hwnd = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
                                x, y, w, h, parent, (HMENU)(intptr_t)id, NULL, NULL);
    if (hwnd && font) {
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
    }
    return hwnd;
}

// ── 对话框消息处理 ───────────────────────────────────────
static LRESULT CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT *pCreate = (CREATESTRUCT *)lParam;
        QAManagerUI *pUI = (QAManagerUI *)pCreate->lpCreateParams;
        g_pUI = pUI;
        g_hwndQAManager = hwnd;
        pUI->hwndDlg = hwnd;
        pUI->dpi = UI_GetWindowDpi(hwnd);

        // 与主窗口同一套栅格：外边距 16、卡片内边距 16、组内 8
        {
            int outer = QS(pUI, 16);
            int inner = QS(pUI, 16);
            int gapS  = QS(pUI, 8);
            int rowH  = QS(pUI, 26);
            int btnH  = QS(pUI, 30);
            int lblW  = QS(pUI, 44);
            RECT rcClient;
            int cardW, contentW, x, y;

            GetClientRect(hwnd, &rcClient);
            SetRect(&pUI->rcCard, outer, outer,
                    rcClient.right - outer, rcClient.bottom - outer);
            cardW    = pUI->rcCard.right - pUI->rcCard.left;
            contentW = cardW - inner * 2;
            x = pUI->rcCard.left + inner;
            y = pUI->rcCard.top + inner;

            // 导入模式
            QAMake(hwnd, L"BUTTON", L"批量导入",
                   BS_AUTORADIOBUTTON | WS_GROUP, 0,
                   CTRL_ID_RADIO_BATCH, pUI->hFontUI,
                   x, y, QS(pUI, 96), rowH);
            QAMake(hwnd, L"BUTTON", L"单条导入",
                   BS_AUTORADIOBUTTON, 0,
                   CTRL_ID_RADIO_SINGLE, pUI->hFontUI,
                   x + QS(pUI, 108), y, QS(pUI, 96), rowH);
            y += rowH + gapS;

            // 批量文本框（无 3D 凹陷边框，与主窗口一致）
            pUI->wellCount = 0;
            SetRect(&pUI->rcWell[pUI->wellCount++],
                    x, y, x + contentW, y + QS(pUI, 108));
            QAMake(hwnd, L"EDIT", L"",
                   WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL, 0,
                   CTRL_ID_EDIT_BATCH, pUI->hFontEdit,
                   x + QS(pUI, 6), y + QS(pUI, 5),
                   contentW - QS(pUI, 12), QS(pUI, 98));
            y += QS(pUI, 108) + gapS;

            QAMake(hwnd, L"BUTTON", L"导入文件", BS_PUSHBUTTON, 0,
                   CTRL_ID_BTN_IMPORT, pUI->hFontUI,
                   x, y, QS(pUI, 108), btnH);
            y += btnH + QS(pUI, 16);

            // 单条导入：题目 / 答案 / 添加
            {
                int fieldW = (contentW - lblW * 2 - QS(pUI, 100) - gapS * 3) / 2;
                int fx = x;

                QAMake(hwnd, L"STATIC", L"题目", SS_LEFT, 0,
                       0, pUI->hFontUI, fx, y + QS(pUI, 4), lblW, rowH);
                fx += lblW;
                SetRect(&pUI->rcWell[pUI->wellCount++],
                        fx, y, fx + fieldW, y + rowH);
                QAMake(hwnd, L"EDIT", L"", ES_AUTOHSCROLL, 0,
                       CTRL_ID_EDIT_QUESTION, pUI->hFontEdit,
                       fx + QS(pUI, 6), y + QS(pUI, 4),
                       fieldW - QS(pUI, 12), rowH - QS(pUI, 8));
                fx += fieldW + gapS;

                QAMake(hwnd, L"STATIC", L"答案", SS_LEFT, 0,
                       0, pUI->hFontUI, fx, y + QS(pUI, 4), lblW, rowH);
                fx += lblW;
                SetRect(&pUI->rcWell[pUI->wellCount++],
                        fx, y, fx + fieldW, y + rowH);
                QAMake(hwnd, L"EDIT", L"", ES_AUTOHSCROLL, 0,
                       CTRL_ID_EDIT_ANSWER, pUI->hFontEdit,
                       fx + QS(pUI, 6), y + QS(pUI, 4),
                       fieldW - QS(pUI, 12), rowH - QS(pUI, 8));
                fx += fieldW + gapS;

                QAMake(hwnd, L"BUTTON", L"添加", BS_PUSHBUTTON, 0,
                       CTRL_ID_BTN_ADD_SINGLE, pUI->hFontUI,
                       fx, y, QS(pUI, 100), btnH);
            }
            y += btnH + QS(pUI, 16);

            // 题目列表
            {
                int listH = pUI->rcCard.bottom - inner - y
                            - btnH - gapS - rowH - gapS;
                if (listH < QS(pUI, 80)) listH = QS(pUI, 80);
                SetRect(&pUI->rcWell[pUI->wellCount++],
                        x, y, x + contentW, y + listH);
                QAMake(hwnd, L"LISTBOX", L"", WS_VSCROLL | LBS_NOTIFY, 0,
                       CTRL_ID_LIST_QA, pUI->hFontEdit,
                       x + QS(pUI, 6), y + QS(pUI, 5),
                       contentW - QS(pUI, 12), listH - QS(pUI, 10));
                y += listH + gapS;
            }

            // 操作按钮一行
            {
                int bw = QS(pUI, 100);
                QAMake(hwnd, L"BUTTON", L"刷新列表", BS_PUSHBUTTON, 0,
                       CTRL_ID_BTN_REFRESH, pUI->hFontUI, x, y, bw, btnH);
                QAMake(hwnd, L"BUTTON", L"删除选中", BS_PUSHBUTTON, 0,
                       CTRL_ID_BTN_DELETE, pUI->hFontUI,
                       x + bw + gapS, y, bw, btnH);
                QAMake(hwnd, L"BUTTON", L"导出题库", BS_PUSHBUTTON, 0,
                       CTRL_ID_BTN_EXPORT, pUI->hFontUI,
                       x + (bw + gapS) * 2, y, bw, btnH);
                QAMake(hwnd, L"BUTTON", L"关闭", BS_PUSHBUTTON, 0,
                       CTRL_ID_BTN_CLOSE, pUI->hFontUI,
                       x + contentW - bw, y, bw, btnH);
                y += btnH + gapS;
            }

            // 状态行
            QAMake(hwnd, L"STATIC", L"就绪", SS_LEFT, 0,
                   CTRL_ID_STATIC_STATUS, pUI->hFontUI,
                   x, y, contentW, rowH);
        }

        // 初始化默认选中批量导入
        SendMessageW(GetDlgItem(hwnd, CTRL_ID_RADIO_BATCH), BM_SETCHECK, BST_CHECKED, 0);
        g_pUI->importMode = IMPORT_MODE_BATCH;

        // 刷新列表显示
        QAManagerUI_UpdateList(g_pUI);

        return 0;
    }

    case WM_ERASEBKGND:
        return 1;   // 真正的绘制在 WM_PAINT 里双缓冲完成

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (hdc && g_pUI) {
            RECT rcClient;
            HDC memDC;
            HBITMAP memBmp, oldBmp;

            GetClientRect(hwnd, &rcClient);
            memDC = CreateCompatibleDC(hdc);
            memBmp = memDC ? CreateCompatibleBitmap(hdc, rcClient.right,
                                                    rcClient.bottom) : NULL;
            if (memDC && memBmp) {
                oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
                FillRect(memDC, &rcClient, g_pUI->hbrBg);
                UI_DrawCard(memDC, &g_pUI->rcCard, QS(g_pUI, CARD_RADIUS),
                            g_pUI->hbrPanel,
                            g_pUI->darkMode ? CLR_BORDER : CLR_BORDER_LIGHT);
                {
                    int i;
                    for (i = 0; i < g_pUI->wellCount; ++i) {
                        UI_DrawCard(memDC, &g_pUI->rcWell[i], QS(g_pUI, 5),
                                    g_pUI->hbrEdit,
                                    g_pUI->darkMode ? CLR_BORDER
                                                    : CLR_BORDER_LIGHT);
                    }
                }
                BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom,
                       memDC, 0, 0, SRCCOPY);
                SelectObject(memDC, oldBmp);
            } else {
                FillRect(hdc, &rcClient, g_pUI->hbrBg);
                UI_DrawCard(hdc, &g_pUI->rcCard, QS(g_pUI, CARD_RADIUS),
                            g_pUI->hbrPanel,
                            g_pUI->darkMode ? CLR_BORDER : CLR_BORDER_LIGHT);
            }
            if (memBmp) DeleteObject(memBmp);
            if (memDC)  DeleteDC(memDC);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        switch (id) {
        case CTRL_ID_RADIO_BATCH:
            if (code == BN_CLICKED) {
                g_pUI->importMode = IMPORT_MODE_BATCH;
                EnableWindow(GetDlgItem(hwnd, CTRL_ID_EDIT_BATCH), TRUE);
                EnableWindow(GetDlgItem(hwnd, CTRL_ID_BTN_IMPORT), TRUE);
                EnableWindow(GetDlgItem(hwnd, CTRL_ID_EDIT_QUESTION), FALSE);
                EnableWindow(GetDlgItem(hwnd, CTRL_ID_EDIT_ANSWER), FALSE);
                EnableWindow(GetDlgItem(hwnd, CTRL_ID_BTN_ADD_SINGLE), FALSE);
            }
            break;

        case CTRL_ID_RADIO_SINGLE:
            if (code == BN_CLICKED) {
                g_pUI->importMode = IMPORT_MODE_SINGLE;
                EnableWindow(GetDlgItem(hwnd, CTRL_ID_EDIT_BATCH), FALSE);
                EnableWindow(GetDlgItem(hwnd, CTRL_ID_BTN_IMPORT), FALSE);
                EnableWindow(GetDlgItem(hwnd, CTRL_ID_EDIT_QUESTION), TRUE);
                EnableWindow(GetDlgItem(hwnd, CTRL_ID_EDIT_ANSWER), TRUE);
                EnableWindow(GetDlgItem(hwnd, CTRL_ID_BTN_ADD_SINGLE), TRUE);
            }
            break;

        case CTRL_ID_BTN_ADD_SINGLE: {
            int qLen = GetWindowTextLengthW(GetDlgItem(hwnd, CTRL_ID_EDIT_QUESTION));
            int aLen = GetWindowTextLengthW(GetDlgItem(hwnd, CTRL_ID_EDIT_ANSWER));

            if (qLen > 0 && aLen > 0) {
                wchar_t *question = (wchar_t *)Mem_Alloc((qLen + 1) * sizeof(wchar_t));
                wchar_t *answer = (wchar_t *)Mem_Alloc((aLen + 1) * sizeof(wchar_t));
                if (!question || !answer) {
                    Mem_Free(question);
                    Mem_Free(answer);
                    SetDlgItemTextW(hwnd, CTRL_ID_STATIC_STATUS, L"内存不足，无法添加题目！");
                    break;
                }
                GetWindowTextW(GetDlgItem(hwnd, CTRL_ID_EDIT_QUESTION), question, qLen + 1);
                GetWindowTextW(GetDlgItem(hwnd, CTRL_ID_EDIT_ANSWER), answer, aLen + 1);

                int result = Db_Insert(g_pUI->pDbCtx, question, answer);
                if (result == DB_OK) {
                    SetDlgItemTextW(hwnd, CTRL_ID_STATIC_STATUS, L"成功添加题目！");
                    SetWindowTextW(GetDlgItem(hwnd, CTRL_ID_EDIT_QUESTION), L"");
                    SetWindowTextW(GetDlgItem(hwnd, CTRL_ID_EDIT_ANSWER), L"");
                } else {
                    SetDlgItemTextW(hwnd, CTRL_ID_STATIC_STATUS, L"添加失败！");
                }

                Mem_Free(question);
                Mem_Free(answer);
            } else {
                SetDlgItemTextW(hwnd, CTRL_ID_STATIC_STATUS, L"请输入题目和答案！");
            }
            break;
        }

        case CTRL_ID_BTN_IMPORT:
            QAManagerUI_ImportFile(g_pUI);
            break;

        case CTRL_ID_BTN_REFRESH:
            QAManagerUI_UpdateList(g_pUI);
            break;

        case CTRL_ID_BTN_DELETE:
            QAManagerUI_DeleteSelected(g_pUI);
            break;

        case CTRL_ID_BTN_EXPORT:
            QAManagerUI_ExportFile(g_pUI);
            break;

        case CTRL_ID_BTN_CLOSE:
            DestroyWindow(hwnd);
            break;
        }
        break;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wParam;

        if (!g_pUI) break;

        // 浅色和深色都要显式着色：控件都落在卡片上，背景必须是卡片色，
        // 否则会出现一块块底色不同的方块。
        SetTextColor(hdc, g_pUI->darkMode ? CLR_TEXT_DARK : CLR_TEXT_LIGHT);
        if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) {
            SetBkColor(hdc, g_pUI->darkMode ? CLR_EDIT_DARK : CLR_PANEL_LIGHT);
            return (LRESULT)g_pUI->hbrEdit;
        }
        SetBkColor(hdc, g_pUI->darkMode ? CLR_PANEL_DARK : CLR_PANEL_LIGHT);
        return (LRESULT)g_pUI->hbrPanel;
    }

    case WM_DESTROY:
        g_pUI = NULL;
        g_hwndQAManager = NULL;
        PostQuitMessage(0);
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

BOOL QAManagerUI_Create(HWND hwndParent, AppUI *pMainUI, DbContext *pDbCtx,
                        BOOL darkMode, HFONT hFontUI, HFONT hFontEdit)
{
    if (g_hwndQAManager && IsWindow(g_hwndQAManager)) {
        ShowWindow(g_hwndQAManager, SW_RESTORE);
        SetForegroundWindow(g_hwndQAManager);
        return TRUE;
    }

    QAManagerUI *pUI = (QAManagerUI *)Mem_Alloc(sizeof(QAManagerUI));
    if (!pUI) return FALSE;

    ZeroMemory(pUI, sizeof(QAManagerUI));
    pUI->hwndParent = hwndParent;
    pUI->pMainUI = pMainUI;
    pUI->pDbCtx = pDbCtx;
    pUI->hFontUI = hFontUI;
    pUI->hFontEdit = hFontEdit;
    pUI->darkMode = darkMode;
    pUI->importMode = IMPORT_MODE_BATCH;

    // 初始化画刷。注意浅色下的编辑框底色原先误用了 CLR_EDIT_DARK，
    // 导致亮色主题里输入框是深色的。
    // 输入区用比卡片略深的底色，否则白底输入框落在白卡片上完全看不见。
    pUI->hbrBg = CreateSolidBrush(darkMode ? CLR_BG_DARK : CLR_BG_LIGHT);
    pUI->hbrPanel = CreateSolidBrush(darkMode ? CLR_PANEL_DARK : CLR_PANEL_LIGHT);
    pUI->hbrEdit = CreateSolidBrush(darkMode ? CLR_EDIT_DARK : RGB(248, 249, 252));
    if (!pUI->hbrBg || !pUI->hbrPanel || !pUI->hbrEdit) {
        if (pUI->hbrBg) DeleteObject(pUI->hbrBg);
        if (pUI->hbrPanel) DeleteObject(pUI->hbrPanel);
        if (pUI->hbrEdit) DeleteObject(pUI->hbrEdit);
        Mem_Free(pUI);
        return FALSE;
    }

    // 注册窗口类
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = DlgProc;
    wc.hInstance     = NULL;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"QAManagerWindowClass";
    wc.hIcon         = LoadIconW(GetModuleHandleW(NULL),
                                 MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hIconSm       = (HICON)LoadImageW(
        GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR);
    if (!wc.hIcon)   wc.hIcon   = LoadIconW(NULL, IDI_APPLICATION);
    if (!wc.hIconSm) wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    // 居中创建窗口。尺寸按父窗口所在显示器的 DPI 缩放，
    // 否则高分屏上窗口会偏小、内容挤在一起。
    int dpi = UI_GetWindowDpi(hwndParent);
    int winW = MulDiv(700, dpi, 96);
    int winH = MulDiv(620, dpi, 96);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - winW) / 2;
    int y = (screenH - winH) / 2;

    pUI->dpi = dpi;

    HWND hwndDlg = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"QAManagerWindowClass",
        L"题库管理",
        WS_OVERLAPPEDWINDOW,
        x, y, winW, winH,
        hwndParent, NULL, NULL, pUI);

    if (!hwndDlg) {
        DeleteObject(pUI->hbrBg);
        DeleteObject(pUI->hbrPanel);
        DeleteObject(pUI->hbrEdit);
        Mem_Free(pUI);
        return FALSE;
    }

    // 深色下让滚动条与列表跟随配色，标题栏也用深色
    if (darkMode) {
        SetWindowTheme(GetDlgItem(hwndDlg, CTRL_ID_EDIT_BATCH),
                       L"DarkMode_Explorer", NULL);
        SetWindowTheme(GetDlgItem(hwndDlg, CTRL_ID_LIST_QA),
                       L"DarkMode_Explorer", NULL);
    }
    UI_ApplyWindowStyling(hwndDlg, darkMode);

    ShowWindow(hwndDlg, SW_SHOW);
    UpdateWindow(hwndDlg);

    // 进入消息循环
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(hwndDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // 清理资源
    if (pUI->hbrBg) DeleteObject(pUI->hbrBg);
    if (pUI->hbrPanel) DeleteObject(pUI->hbrPanel);
    if (pUI->hbrEdit) DeleteObject(pUI->hbrEdit);
    Mem_Free(pUI);

    return TRUE;
}

void QAManagerUI_UpdateList(QAManagerUI *pUI)
{
    if (!pUI || !pUI->hwndDlg) return;

    HWND hwndList = GetDlgItem(pUI->hwndDlg, CTRL_ID_LIST_QA);
    if (!hwndList) return;

    // 清空列表
    SendMessageW(hwndList, LB_RESETCONTENT, 0, 0);

    // 获取所有记录
    int count = 0;
    struct QAPair *pairs = Db_GetAllPairs(pUI->pDbCtx, &count);
    if (!pairs || count == 0) {
        SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, L"题库为空！");
        return;
    }

    // 添加到列表
    for (int i = 0; i < count; i++) {
        size_t questionLen = wcslen(pairs[i].question);
        size_t answerLen = wcslen(pairs[i].answer);
        size_t itemChars;
        wchar_t *item;

        if (questionLen > (size_t)-1 - answerLen - 64) {
            Db_FreeResults(pairs, count);
            SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, L"题库内容过长，无法显示！");
            return;
        }
        itemChars = questionLen + answerLen + 64;
        if (itemChars > (size_t)-1 / sizeof(wchar_t)) {
            Db_FreeResults(pairs, count);
            SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, L"题库内容过长，无法显示！");
            return;
        }
        item = (wchar_t *)Mem_Alloc(itemChars * sizeof(wchar_t));
        if (!item) {
            Db_FreeResults(pairs, count);
            SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, L"内存不足，无法显示题库！");
            return;
        }
        if (_snwprintf(item, itemChars, L"[%d] 题目：%ls | 答案：%ls",
                       i + 1, pairs[i].question, pairs[i].answer) < 0) {
            Mem_Free(item);
            Db_FreeResults(pairs, count);
            SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, L"题库内容格式化失败！");
            return;
        }
        item[itemChars - 1] = L'\0';
        SendMessageW(hwndList, LB_ADDSTRING, 0, (LPARAM)item);
        Mem_Free(item);
    }

    Db_FreeResults(pairs, count);
    wchar_t status[128];
    wsprintfW(status, L"成功加载 %d 条记录！", count);
    SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, status);
}

BOOL QAManagerUI_ImportFile(QAManagerUI *pUI)
{
    OPENFILENAMEW ofn;
    wchar_t filePath[MAX_PATH] = {0};

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = pUI->hwndDlg;
    ofn.lpstrFilter = L"文本文件\0*.txt;*.csv\0所有文件\0*.*\0";
    ofn.lpstrFile   = filePath;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameW(&ofn)) return FALSE;

    TextFileStatus status = TEXTFILE_OK;
    wchar_t *text = TextFile_Load(filePath, &status);
    if (!text) {
        SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS,
                        TextFile_StatusText(status));
        return FALSE;
    }

    int skipped = 0;
    int imported = Db_ImportFromText(pUI->pDbCtx, text, &skipped);
    Mem_Free(text);

    if (imported > 0) {
        wchar_t status[128];
        if (skipped > 0) {
            wsprintfW(status, L"成功导入 %d 条记录，%d 行格式不正确已跳过！",
                      imported, skipped);
        } else {
            wsprintfW(status, L"成功导入 %d 条记录！", imported);
        }
        SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, status);
        QAManagerUI_UpdateList(pUI);
    } else {
        SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, L"导入格式不正确！");
    }

    return TRUE;
}

BOOL QAManagerUI_ExportFile(QAManagerUI *pUI)
{
    if (!pUI || !pUI->pDbCtx) return FALSE;

    OPENFILENAMEW ofn;
    wchar_t filePath[MAX_PATH] = {0};

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = pUI->hwndDlg;
    ofn.lpstrFilter = L"文本文件\0*.txt;*.csv\0所有文件\0*.*\0";
    ofn.lpstrFile   = filePath;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn)) return FALSE;

    wchar_t *text = Db_ExportToText(pUI->pDbCtx);
    if (!text) {
        SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, L"没有数据可导出！");
        return FALSE;
    }

    size_t textChars = wcslen(text);
    if (textChars > MAXDWORD / sizeof(wchar_t)) {
        Mem_Free(text);
        SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, L"题库过大，无法导出！");
        return FALSE;
    }
    size_t textBytes = textChars * sizeof(wchar_t);

    HANDLE hFile = CreateFileW(filePath, GENERIC_WRITE, FILE_SHARE_READ,
                               NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        Mem_Free(text);
        SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, L"无法打开文件！");
        return FALSE;
    }

    DWORD bytesWritten = 0;
    const WORD bom = 0xFEFF;
    if (!WriteFile(hFile, &bom, sizeof(bom), &bytesWritten, NULL) ||
        bytesWritten != sizeof(bom) ||
        !WriteFile(hFile, text, (DWORD)textBytes, &bytesWritten, NULL) ||
        bytesWritten != (DWORD)textBytes) {
        CloseHandle(hFile);
        Mem_Free(text);
        SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, L"写入文件失败！");
        return FALSE;
    }
    CloseHandle(hFile);
    Mem_Free(text);

    size_t statusChars = wcslen(filePath) + 32;
    wchar_t *status = (wchar_t *)Mem_Alloc(statusChars * sizeof(wchar_t));
    if (status) {
        if (_snwprintf(status, statusChars, L"成功导出到文件：%ls！", filePath) >= 0) {
            status[statusChars - 1] = L'\0';
            SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, status);
        }
        Mem_Free(status);
    } else {
        SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, L"导出成功！");
    }

    return TRUE;
}

BOOL QAManagerUI_DeleteSelected(QAManagerUI *pUI)
{
    if (!pUI || !pUI->hwndDlg) return FALSE;

    HWND hwndList = GetDlgItem(pUI->hwndDlg, CTRL_ID_LIST_QA);
    if (!hwndList) return FALSE;

    int selIndex = (int)SendMessageW(hwndList, LB_GETCURSEL, 0, 0);
    if (selIndex == LB_ERR) {
        SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, L"请先选择一条记录！");
        return FALSE;
    }

    // 获取所有记录，找到被选中的题目
    int count = 0;
    struct QAPair *pairs = Db_GetAllPairs(pUI->pDbCtx, &count);
    if (!pairs || selIndex >= count) {
        Db_FreeResults(pairs, count);
        SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, L"获取数据失败！");
        return FALSE;
    }

    // 删除记录
    int result = Db_Delete(pUI->pDbCtx, pairs[selIndex].question);
    Db_FreeResults(pairs, count);

    if (result == DB_OK) {
        SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, L"删除成功！");
        QAManagerUI_UpdateList(pUI);
        return TRUE;
    } else {
        SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, L"删除失败！");
        return FALSE;
    }
}
