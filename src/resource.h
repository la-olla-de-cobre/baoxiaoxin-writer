#pragma once

// ── 应用标识与版本（唯一来源）────────────────────────────
// 窗口标题、托盘提示、关于信息与 exe 版本元数据全部从这里取，
// 发版时只改这一处。
#define APP_VER_MAJOR   5
#define APP_VER_MINOR   2
#define APP_VER_STR     "5.2.0.0"          // 供 res/app.rc 的 VERSIONINFO 使用

#ifdef _UNICODE
#define APP_NAME        L"鲍小新写字"
#define APP_VERSION     L"5.2"
#define APP_TITLE       APP_NAME L"  v" APP_VERSION
#endif

// ── 图标资源 ─────────────────────────────────────────────
#define IDI_APP_ICON        201

// ── 控件 ID ──────────────────────────────────────────────
#define IDC_EDIT_TEXT       1001
#define IDC_BTN_LOAD        1002

// ── 读取源切换复选框 ──────────────────────────────────────
#define IDC_CHK_USE_PANEL   1020
#define IDC_TRACKBAR_SPEED  1007
#define IDC_EDIT_INTERVAL   1008
#define IDC_PROGRESS        1009
#define IDC_STATUS          1010
#define IDC_COMBO_PRESET    1011
#define IDC_STATIC_INTERVAL 1012
#define IDC_STATIC_PRESET   1013
#define IDC_STATIC_CHARS    1014
#define IDC_CHK_TOPMOST     1015
#define IDC_BTN_DATABASE    1016  // 数据库功能按钮

// ── 搜索模式复选框 ────────────────────────────────────────
#define IDC_CHK_FUZZY        1017
#define IDC_CHK_CODE_MODE    1018

// ── 外观 ─────────────────────────────────────────────────
#define IDC_CHK_DARK         1019

// 主窗口逻辑尺寸（96 DPI 下的像素）。实际尺寸按当前 DPI 缩放。
#define WIN_LOGICAL_W        760
#define WIN_LOGICAL_H        520

// ── 题库管理对话框控件 ID ──────────────────────────────────
#define IDD_QA_MANAGER       101
#define IDC_TAB_QA_MANAGER   102
#define IDC_RADIO_BATCH      103
#define IDC_RADIO_SINGLE     104
#define IDC_EDIT_BATCH       105
#define IDC_EDIT_QUESTION    106
#define IDC_EDIT_ANSWER      107
#define IDC_BTN_ADD_SINGLE   108
#define IDC_BTN_IMPORT       109
#define IDC_LIST_QUESTIONS   110
#define IDC_BTN_DELETE       111
#define IDC_BTN_CLOSE        112
#define IDC_STATIC_STATUS    113

// ── 速度预设（毫秒/字符）────────────────────────────────
#define SPEED_SLOW    300
#define SPEED_MEDIUM   80
#define SPEED_FAST     20

// ── Trackbar 范围 ────────────────────────────────────────
#define INTERVAL_MIN   10
#define INTERVAL_MAX  500
#define INTERVAL_DEF   80

// ── 全局热键 ID ──────────────────────────────────────────
#define HOTKEY_START    0xBEEF
#define HOTKEY_SEARCH   0xBEE0
#define HOTKEY_STOP     0xBEE2  // ESC 停止输入

// ── 自定义窗口消息 ───────────────────────────────────────
// wParam = 已输入字符数, lParam = 总字符数
#define WM_WORKER_PROGRESS  (WM_USER + 1)
// wParam = 0 正常完成, 1 = 被停止
#define WM_WORKER_DONE      (WM_USER + 2)
// 托盘图标消息
#define WM_TRAYICON         (WM_USER + 3)

// ── 托盘图标命令 ────────────────────────────────────────
#define IDM_TRAY_SHOW           2001
#define IDM_TRAY_QUIT           2002
#define IDM_TRAY_RETRY_HOTKEY   2003  // 热键被占用时重新注册

// ── 应用状态机 ───────────────────────────────────────────
#define STATE_IDLE      0   // 空闲
#define STATE_RUNNING   1   // 正在输入
#define STATE_PAUSED    2   // 已暂停
