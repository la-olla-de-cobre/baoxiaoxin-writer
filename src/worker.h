#pragma once
#include <windows.h>

// 传递给工作线程的参数（由调用方在线程结束后释放）
typedef struct {
    wchar_t        *text;           // 文本缓冲区（由调用方释放）
    int             textLen;        // 字符数（不含终止符）
    int             delayMs;        // 每字符间隔（毫秒）
    BOOL            codeInputMode;  // Python 补偿 + 忽略换行后的原文缩进
    HWND            hwndMain;       // 主窗口句柄，用于 PostMessage 回调

    HANDLE          hEventPause;    // 手动重置事件：signaled=运行, non-signaled=暂停
    HANDLE          hEventStop;     // 自动重置事件：signaled=停止
    volatile LONG   stopped;        // 原子标志，1=已请求停止
} WorkerParams;

// 启动工作线程，返回线程句柄（调用方负责 CloseHandle 和 Worker_Free）。
// 失败时只回收本函数创建的事件，参数对象仍归调用方所有。
HANDLE Worker_Start(WorkerParams *params);

// 暂停输入（线程阻塞直到 Resume）
void Worker_Pause(WorkerParams *params);

// 继续输入
void Worker_Resume(WorkerParams *params);

// 请求停止（非阻塞，线程会尽快退出）
void Worker_Stop(WorkerParams *params);

// 释放 params 内部资源（事件句柄 + 文本缓冲区）
// 仅在线程已退出后调用
void Worker_Free(WorkerParams *params);
