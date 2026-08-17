#pragma once
#include <windows.h>
#include "database.h"
#include "ui.h"

// ── 导入模式 ───────────────────────────────────────────────
typedef enum {
    IMPORT_MODE_BATCH,
    IMPORT_MODE_SINGLE
} ImportMode;

// ── 题库管理UI状态 ─────────────────────────────────────────
typedef struct {
    HWND          hwndDlg;
    HWND          hwndParent;
    AppUI        *pMainUI;
    DbContext    *pDbCtx;
    ImportMode    importMode;
    HFONT         hFontUI;
    HFONT         hFontEdit;
    BOOL          darkMode;
    int           dpi;        // 与主窗口一致：布局按此缩放
    RECT          rcCard;     // 内容卡片，由 WM_PAINT 绘制
    // 输入区背景。去掉 3D 边框后，白底输入框落在白卡片上会完全看不见，
    // 所以由 WM_PAINT 在这些位置画出可见的凹槽。
    RECT          rcWell[4];
    int           wellCount;
    HBRUSH        hbrBg;
    HBRUSH        hbrPanel;
    HBRUSH        hbrEdit;
} QAManagerUI;

// ── 导出函数 ───────────────────────────────────────────────

#ifdef __cplusplus
extern "C" {
#endif

// 创建并显示题库管理对话框
BOOL QAManagerUI_Create(HWND hwndParent, AppUI *pMainUI, DbContext *pDbCtx,
                        BOOL darkMode, HFONT hFontUI, HFONT hFontEdit);

// 更新列表显示
void QAManagerUI_UpdateList(QAManagerUI *pUI);

// 导入文件
BOOL QAManagerUI_ImportFile(QAManagerUI *pUI);

// 导出文件
BOOL QAManagerUI_ExportFile(QAManagerUI *pUI);

// 删除选中的题目
BOOL QAManagerUI_DeleteSelected(QAManagerUI *pUI);

#ifdef __cplusplus
}
#endif
