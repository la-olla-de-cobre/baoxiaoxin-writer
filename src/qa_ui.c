#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <string.h>
#include "qa_ui.h"
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

static wchar_t *LoadTextFile(const wchar_t *filePath)
{
    HANDLE hFile;
    DWORD fileSize, bytesRead = 0;
    BYTE *bytes;
    wchar_t *text;
    int chars;
    DWORD offset = 0;

    hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return NULL;

    fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE || fileSize == 0) {
        CloseHandle(hFile);
        return NULL;
    }

    bytes = (BYTE *)malloc(fileSize);
    if (!bytes) {
        CloseHandle(hFile);
        return NULL;
    }
    if (!ReadFile(hFile, bytes, fileSize, &bytesRead, NULL) || bytesRead != fileSize) {
        CloseHandle(hFile);
        free(bytes);
        return NULL;
    }
    CloseHandle(hFile);

    if (fileSize >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        offset = 2;
        if ((fileSize - offset) % sizeof(wchar_t) != 0) {
            free(bytes);
            return NULL;
        }
        chars = (int)((fileSize - offset) / sizeof(wchar_t));
        text = (wchar_t *)malloc((size_t)(chars + 1) * sizeof(wchar_t));
        if (text) {
            memcpy(text, bytes + offset, (size_t)chars * sizeof(wchar_t));
            text[chars] = L'\0';
        }
    } else if (fileSize >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        offset = 2;
        if ((fileSize - offset) % 2 != 0) {
            free(bytes);
            return NULL;
        }
        chars = (int)((fileSize - offset) / 2);
        text = (wchar_t *)malloc((size_t)(chars + 1) * sizeof(wchar_t));
        if (text) {
            for (int i = 0; i < chars; ++i) {
                text[i] = (wchar_t)((bytes[offset + i * 2] << 8) | bytes[offset + i * 2 + 1]);
            }
            text[chars] = L'\0';
        }
    } else {
        if (fileSize >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) offset = 3;
        chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                    (const char *)(bytes + offset), (int)(fileSize - offset), NULL, 0);
        text = chars > 0 ? (wchar_t *)malloc((size_t)(chars + 1) * sizeof(wchar_t)) : NULL;
        if (text) {
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                (const char *)(bytes + offset), (int)(fileSize - offset), text, chars);
            text[chars] = L'\0';
        }
    }

    free(bytes);
    return text;
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

        RECT rc;
        GetClientRect(hwnd, &rc);

        int x = 10, y = 10, w, h;

        // 导入模式单选按钮
        w = 100; h = 20;
        CreateWindowW(L"BUTTON", L"批量导入",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
            x, y, w, h, hwnd, (HMENU)CTRL_ID_RADIO_BATCH, NULL, NULL);

        x += w + 20;
        CreateWindowW(L"BUTTON", L"单条导入",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            x, y, w, h, hwnd, (HMENU)CTRL_ID_RADIO_SINGLE, NULL, NULL);

        // 批量导入区域
        x = 10; y += h + 10;
        w = 680; h = 120;
        HWND hwndBatchEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
            x, y, w, h, hwnd, (HMENU)CTRL_ID_EDIT_BATCH, NULL, NULL);
        SendMessageW(hwndBatchEdit, WM_SETFONT, (WPARAM)g_pUI->hFontEdit, TRUE);

        x = 10; y += h + 5;
        w = 120; h = 30;
        HWND hwndBtnImport = CreateWindowW(L"BUTTON", L"导入文件",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, y, w, h, hwnd, (HMENU)CTRL_ID_BTN_IMPORT, NULL, NULL);
        SendMessageW(hwndBtnImport, WM_SETFONT, (WPARAM)g_pUI->hFontUI, TRUE);

        // 单条导入区域
        x = 10; y += h + 10;
        w = 200; h = 25;
        CreateWindowW(L"STATIC", L"题目：", WS_CHILD | WS_VISIBLE,
            x, y, 40, h, hwnd, NULL, NULL, NULL);

        x += 45;
        HWND hwndEditQuestion = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            x, y, w, h, hwnd, (HMENU)CTRL_ID_EDIT_QUESTION, NULL, NULL);
        SendMessageW(hwndEditQuestion, WM_SETFONT, (WPARAM)g_pUI->hFontEdit, TRUE);

        x += w + 20;
        CreateWindowW(L"STATIC", L"答案：", WS_CHILD | WS_VISIBLE,
            x, y, 40, h, hwnd, NULL, NULL, NULL);

        x += 45;
        HWND hwndEditAnswer = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            x, y, w, h, hwnd, (HMENU)CTRL_ID_EDIT_ANSWER, NULL, NULL);
        SendMessageW(hwndEditAnswer, WM_SETFONT, (WPARAM)g_pUI->hFontEdit, TRUE);

        x += w + 20;
        w = 100;
        HWND hwndBtnAdd = CreateWindowW(L"BUTTON", L"添加",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, y, w, h, hwnd, (HMENU)CTRL_ID_BTN_ADD_SINGLE, NULL, NULL);
        SendMessageW(hwndBtnAdd, WM_SETFONT, (WPARAM)g_pUI->hFontUI, TRUE);

        // 列表显示区域
        x = 10; y += h + 10;
        w = 680; h = 150;
        HWND hwndList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
            x, y, w, h, hwnd, (HMENU)CTRL_ID_LIST_QA, NULL, NULL);
        SendMessageW(hwndList, WM_SETFONT, (WPARAM)g_pUI->hFontEdit, TRUE);
        g_pUI->hwndDlg = hwnd;

        // 刷新按钮
        x = 10; y += h + 10;
        w = 100; h = 30;
        CreateWindowW(L"BUTTON", L"刷新列表",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, y, w, h, hwnd, (HMENU)CTRL_ID_BTN_REFRESH, NULL, NULL);

        // 删除按钮
        x += w + 20;
        CreateWindowW(L"BUTTON", L"删除选中",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, y, w, h, hwnd, (HMENU)CTRL_ID_BTN_DELETE, NULL, NULL);

        // 导出按钮
        x += w + 20;
        CreateWindowW(L"BUTTON", L"导出题库",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, y, w, h, hwnd, (HMENU)CTRL_ID_BTN_EXPORT, NULL, NULL);

        // 状态标签
        x = 10; y += h + 10;
        w = 460; h = 30;
        CreateWindowW(L"STATIC", L"就绪", WS_CHILD | WS_VISIBLE,
            x, y, w, h, hwnd, (HMENU)CTRL_ID_STATIC_STATUS, NULL, NULL);

        // 关闭按钮
        x = 10; y += h + 10;
        w = 100; h = 30;
        HWND hwndBtnClose = CreateWindowW(L"BUTTON", L"关闭",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, y, w, h, hwnd, (HMENU)CTRL_ID_BTN_CLOSE, NULL, NULL);
        SendMessageW(hwndBtnClose, WM_SETFONT, (WPARAM)g_pUI->hFontUI, TRUE);

        // 初始化默认选中批量导入
        SendMessageW(GetDlgItem(hwnd, CTRL_ID_RADIO_BATCH), BM_SETCHECK, BST_CHECKED, 0);
        g_pUI->importMode = IMPORT_MODE_BATCH;

        // 刷新列表显示
        QAManagerUI_UpdateList(g_pUI);

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
                wchar_t *question = (wchar_t *)malloc((qLen + 1) * sizeof(wchar_t));
                wchar_t *answer = (wchar_t *)malloc((aLen + 1) * sizeof(wchar_t));
                if (!question || !answer) {
                    free(question);
                    free(answer);
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

                free(question);
                free(answer);
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
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;

        if (g_pUI->darkMode) {
            SetTextColor(hdc, CLR_TEXT_DARK);
            SetBkMode(hdc, TRANSPARENT);
            if (msg == WM_CTLCOLOREDIT) {
                return (LRESULT)g_pUI->hbrEdit;
            } else if (msg == WM_CTLCOLORSTATIC) {
                return (LRESULT)g_pUI->hbrPanel;
            }
        }
        break;
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

    QAManagerUI *pUI = (QAManagerUI *)malloc(sizeof(QAManagerUI));
    if (!pUI) return FALSE;

    ZeroMemory(pUI, sizeof(QAManagerUI));
    pUI->hwndParent = hwndParent;
    pUI->pMainUI = pMainUI;
    pUI->pDbCtx = pDbCtx;
    pUI->hFontUI = hFontUI;
    pUI->hFontEdit = hFontEdit;
    pUI->darkMode = darkMode;
    pUI->importMode = IMPORT_MODE_BATCH;

    // 初始化画刷
    pUI->hbrBg = CreateSolidBrush(darkMode ? CLR_BG_DARK : CLR_BG_LIGHT);
    pUI->hbrPanel = CreateSolidBrush(darkMode ? CLR_PANEL_DARK : CLR_PANEL_LIGHT);
    pUI->hbrEdit = CreateSolidBrush(darkMode ? CLR_EDIT_DARK : CLR_EDIT_DARK);
    if (!pUI->hbrBg || !pUI->hbrPanel || !pUI->hbrEdit) {
        if (pUI->hbrBg) DeleteObject(pUI->hbrBg);
        if (pUI->hbrPanel) DeleteObject(pUI->hbrPanel);
        if (pUI->hbrEdit) DeleteObject(pUI->hbrEdit);
        free(pUI);
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
    wc.hIcon         = LoadIconW(NULL, IDI_APPLICATION);
    wc.hIconSm       = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);

    // 居中创建窗口
    int winW = 700, winH = 600;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - winW) / 2;
    int y = (screenH - winH) / 2;

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
        free(pUI);
        return FALSE;
    }

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
    free(pUI);

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
        item = (wchar_t *)malloc(itemChars * sizeof(wchar_t));
        if (!item) {
            Db_FreeResults(pairs, count);
            SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, L"内存不足，无法显示题库！");
            return;
        }
        if (_snwprintf(item, itemChars, L"[%d] 题目：%ls | 答案：%ls",
                       i + 1, pairs[i].question, pairs[i].answer) < 0) {
            free(item);
            Db_FreeResults(pairs, count);
            SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, L"题库内容格式化失败！");
            return;
        }
        item[itemChars - 1] = L'\0';
        SendMessageW(hwndList, LB_ADDSTRING, 0, (LPARAM)item);
        free(item);
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

    wchar_t *text = LoadTextFile(filePath);
    if (!text) {
        SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS,
                        L"无法读取文件，需为有效的 UTF-8 或 UTF-16 文本！");
        return FALSE;
    }

    int imported = Db_ImportFromText(pUI->pDbCtx, text);
    free(text);

    if (imported > 0) {
        wchar_t status[128];
        wsprintfW(status, L"成功导入 %d 条记录！", imported);
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
        free(text);
        SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, L"题库过大，无法导出！");
        return FALSE;
    }
    size_t textBytes = textChars * sizeof(wchar_t);

    HANDLE hFile = CreateFileW(filePath, GENERIC_WRITE, FILE_SHARE_READ,
                               NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        free(text);
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
        free(text);
        SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, L"写入文件失败！");
        return FALSE;
    }
    CloseHandle(hFile);
    free(text);

    size_t statusChars = wcslen(filePath) + 32;
    wchar_t *status = (wchar_t *)malloc(statusChars * sizeof(wchar_t));
    if (status) {
        if (_snwprintf(status, statusChars, L"成功导出到文件：%ls！", filePath) >= 0) {
            status[statusChars - 1] = L'\0';
            SetDlgItemTextW(pUI->hwndDlg, CTRL_ID_STATIC_STATUS, status);
        }
        free(status);
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
