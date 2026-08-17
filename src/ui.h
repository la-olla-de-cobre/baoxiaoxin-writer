#pragma once
#include <windows.h>

// ── 颜色方案（暗色） ────────────────────────────────────────
#define CLR_BG_DARK       RGB(22,  23,  30)   // 主窗口背景
#define CLR_PANEL_DARK    RGB(32,  33,  44)   // 控件/面板背景
#define CLR_EDIT_DARK     RGB(28,  29,  38)   // 编辑框背景
#define CLR_TEXT_DARK     RGB(210, 212, 228)  // 主文本
#define CLR_TEXT_DIM      RGB(120, 122, 140)  // 次要文本（标签）
#define CLR_ACCENT        RGB(99,  102, 241)  // 强调色（Indigo）
#define CLR_ACCENT_HOVER  RGB(120, 124, 255)  // 按钮 hover
#define CLR_ACCENT_PRESS  RGB(80,  82,  200)  // 按钮按下
#define CLR_BTN_DANGER    RGB(220,  60,  60)  // 停止按钮
#define CLR_BTN_WARN      RGB(200, 140,  30)  // 暂停按钮
#define CLR_BTN_NEUTRAL   RGB(50,  52,  68)   // 中性按钮（加载、准备）
#define CLR_BTN_NEUTRAL_H RGB(65,  68,  90)   // 中性 hover
#define CLR_BORDER        RGB(55,  57,  75)   // 边框/分隔线

// ── 亮色方案 ─────────────────────────────────────────────
#define CLR_BG_LIGHT      RGB(245, 246, 250)
#define CLR_PANEL_LIGHT   RGB(255, 255, 255)
#define CLR_TEXT_LIGHT    RGB(30,  30,  30)
#define CLR_TEXT_DIM_L    RGB(112, 116, 132)  // 次要文本（分组标题、提示）
#define CLR_BORDER_LIGHT  RGB(226, 228, 236)  // 卡片边框与分隔线

// 卡片圆角与栅格（逻辑像素，实际按 DPI 缩放）
#define CARD_RADIUS       8
#define GRID              4   // 间距基数，所有间距取其整数倍

// 按钮角色枚举
typedef enum {
    BTN_ROLE_ACCENT  = 0,  // 开始输入 — 强调色
    BTN_ROLE_NEUTRAL = 1,  // 加载、准备 — 中性
    BTN_ROLE_WARN    = 2,  // 暂停/继续 — 橙色
    BTN_ROLE_DANGER  = 3,  // 停止 — 红色
} BtnRole;

#define MAX_BTNS 8

typedef struct {
    HWND hwndMain;
    HWND hwndEditText;
    HWND hwndBtnLoad;
    HWND hwndBtnDatabase;
    HWND hwndTrackbar;
    HWND hwndEditInterval;
    HWND hwndProgress;
    HWND hwndStatus;
    HWND hwndComboPreset;
    HWND hwndStaticInterval;
    HWND hwndStaticPreset;
    HWND hwndStaticChars;
    HWND hwndStaticHint;
    HWND hwndChkTopmost;
    HWND hwndChkFuzzy;
    HWND hwndChkUsePanel;
    HWND hwndChkCodeMode;

    HWND hwndChkDark;

    HFONT hFontUI;
    HFONT hFontEdit;
    HFONT hFontLabel;   // 小一号，用于分组标题与热键提示
    BOOL  darkMode;
    int   dpi;          // 当前窗口 DPI；96 = 100% 缩放

    // 由 UI_Layout 算出、供 UI_OnEraseBkgnd 绘制的卡片与分隔线
    RECT  rcCardText;   // 左侧文本卡片
    RECT  rcCardPanel;  // 右侧控制卡片
    int   sepY[4];      // 面板内分隔线的 y 坐标
    int   sepCount;

    // owner-draw 按钮状态
    BOOL     btnHover[MAX_BTNS];
    BOOL     btnPressed[MAX_BTNS];
    BOOL     btnTracking[MAX_BTNS];
    HWND     btnHwnds[MAX_BTNS];
    BtnRole  btnRoles[MAX_BTNS];
    int      btnCount;

    // 缓存画刷
    HBRUSH hbrBg;
    HBRUSH hbrPanel;
    HBRUSH hbrEdit;
} AppUI;

void UI_Create(HWND hwndParent, AppUI *ui);
void UI_Layout(AppUI *ui, int cx, int cy);
void UI_SetState(AppUI *ui, int state);
void UI_UpdateProgress(AppUI *ui, int current, int total);
void UI_SetStatus(AppUI *ui, const wchar_t *text);
void UI_SetIntervalText(AppUI *ui, int delayMs);

// 按实际注册结果刷新热键提示；被占用的热键会标注出来
void UI_SetHotkeyHint(AppUI *ui, BOOL okStart, BOOL okSearch, BOOL okStop,
                      BOOL okPause);
void UI_SetPresetSelection(AppUI *ui, int delayMs);
void UI_ApplyWindowStyling(HWND hwnd, BOOL darkMode);
void UI_Destroy(AppUI *ui);

// ── DPI ─────────────────────────────────────────────────
// 取窗口所在显示器的 DPI（动态解析 GetDpiForWindow，旧系统回退到设备 DC）
int  UI_GetWindowDpi(HWND hwnd);
// 按逻辑像素换算为当前 DPI 下的物理像素
int  UI_Scale(const AppUI *ui, int logical);
// DPI 变化时重建字体并重新套用到所有控件（跨屏拖动时调用）
void UI_UpdateDpi(AppUI *ui, int dpi);
// 把逻辑尺寸的窗口按当前 DPI 缩放并居中
void UI_ResizeToScaled(HWND hwnd, AppUI *ui, int logicalW, int logicalH);

// ── 深色模式 ─────────────────────────────────────────────
// 切换配色：重建画刷并整窗重绘
void UI_SetDarkMode(AppUI *ui, BOOL dark);

// 画一块圆角卡片（填充 + 1px 描边）。题库窗口复用同一实现，
// 避免两处各画一套导致风格再次走样。
void UI_DrawCard(HDC hdc, const RECT *rc, int radius, HBRUSH fill, COLORREF border);

// 滑杆自绘。系统主题画的滑道在深色下是亮白的，且没有可用的深色主题类，
// 只能自己画。返回 CDRF_* 结果，未处理时返回 CDRF_DODEFAULT。
LRESULT UI_OnTrackbarCustomDraw(AppUI *ui, LPARAM lParam);

// WndProc 转发
// 绘制窗口背景：底色 + 两块圆角卡片 + 分组分隔线。
// 内部走离屏位图再一次性 BitBlt，避免「先刷底色再画卡片」的可见两步闪烁。
void UI_PaintBackground(AppUI *ui, HDC hdcTarget);
LRESULT UI_OnDrawItem(AppUI *ui, WPARAM wParam, LPARAM lParam);
LRESULT UI_OnCtlColor(AppUI *ui, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void    UI_OnMouseMove(AppUI *ui, HWND hwndBtn);
void    UI_OnMouseLeave(AppUI *ui, HWND hwndBtn);
