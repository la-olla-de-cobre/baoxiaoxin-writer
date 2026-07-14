#pragma once
#include <windows.h>

typedef struct {
    int      delayMs;               // 字符间隔（毫秒），默认80
    BOOL     darkMode;              // 暗黑模式
    BOOL     alwaysOnTop;           // 窗口置顶
    BOOL     codeInputMode;         // 在线网站 Python 补偿（含行首缩进过滤）
    wchar_t  lastDir[MAX_PATH];     // 上次打开文件的目录
    wchar_t  dbPath[MAX_PATH];      // 数据库文件路径
    UINT     searchHotkey;          // 搜索热键（默认B）
} AppConfig;

void Config_Load(AppConfig *cfg, const wchar_t *iniPath);
void Config_Save(const AppConfig *cfg, const wchar_t *iniPath);
