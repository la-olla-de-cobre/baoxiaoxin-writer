#include "worker.h"
#include "resource.h"
#include "mem.h"
#include <process.h>
#include <stdint.h>
#include <stdlib.h>

#define START_DELAY_MS 500
#define SPACE_EXTRA_DELAY_MS 60
#define NEWLINE_EXTRA_DELAY_MS 120
#define PYTHON_COMPENSATION_DELAY_MS 10

static BOOL SendVKey(WORD vk)
{
    INPUT input[2];
    ZeroMemory(input, sizeof(input));

    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = vk;

    input[1] = input[0];
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;

    return SendInput(2, input, sizeof(INPUT)) == 2;
}

static BOOL SendShiftEnter(void)
{
    INPUT input[4];
    ZeroMemory(input, sizeof(input));

    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_SHIFT;

    input[1].type = INPUT_KEYBOARD;
    input[1].ki.wVk = VK_RETURN;

    input[2].type = INPUT_KEYBOARD;
    input[2].ki.wVk = VK_RETURN;
    input[2].ki.dwFlags = KEYEVENTF_KEYUP;

    input[3].type = INPUT_KEYBOARD;
    input[3].ki.wVk = VK_SHIFT;
    input[3].ki.dwFlags = KEYEVENTF_KEYUP;

    return SendInput(4, input, sizeof(INPUT)) == 4;
}

static BOOL SendUnicodeChar(wchar_t ch)
{
    INPUT input[2];
    ZeroMemory(input, sizeof(input));

    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = 0;
    input[0].ki.wScan = (WORD)ch;
    input[0].ki.dwFlags = KEYEVENTF_UNICODE;

    input[1] = input[0];
    input[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

    return SendInput(2, input, sizeof(INPUT)) == 2;
}

static BOOL SendChar(wchar_t ch)
{
    if (ch == L'\r') {
        return TRUE;
    }
    if (ch == L'\n') {
        return SendShiftEnter();
    }
    if (ch == L'\t') {
        return SendVKey(VK_TAB);
    }
    if (ch == L' ') {
        return SendVKey(VK_SPACE);
    }

    return SendUnicodeChar(ch);
}

static int GetLineIndentUnits(const wchar_t *text, int index)
{
    int columns = 0;

    while (index > 0 && text[index - 1] != L'\n' && text[index - 1] != L'\r') {
        --index;
    }
    while (text[index] == L' ' || text[index] == L'\t') {
        columns += (text[index] == L'\t') ? 4 : 1;
        ++index;
    }
    return (columns + 3) / 4;
}

static int GetLineStartIndex(const wchar_t *text, int index)
{
    while (index > 0 && text[index - 1] != L'\n' && text[index - 1] != L'\r') {
        --index;
    }
    while (text[index] == L' ' || text[index] == L'\t') {
        ++index;
    }
    return index;
}

static BOOL IsPythonCommentOrBlank(const wchar_t *text, int index, int textLen)
{
    int start = GetLineStartIndex(text, index);
    return start >= textLen || text[start] == L'\r' || text[start] == L'\n' ||
           text[start] == L'#';
}

static BOOL IsWhitespaceOnlyLine(const wchar_t *text, int index, int textLen)
{
    while (index < textLen && (text[index] == L' ' || text[index] == L'\t')) {
        ++index;
    }
    return index >= textLen || text[index] == L'\r' || text[index] == L'\n';
}

static int GetNextLineIndentUnits(const wchar_t *text, int index, int textLen)
{
    ++index;
    if (index < textLen && text[index] == L'\n') {
        ++index;
    }
    return GetLineIndentUnits(text, index);
}

static BOOL IsPythonBlockHeader(const wchar_t *text, int index, int textLen)
{
    static const wchar_t *keywords[] = {
        L"def", L"class", L"if", L"elif", L"else", L"for", L"while",
        L"try", L"except", L"finally", L"with", L"match", L"case"
    };
    wchar_t word[16];
    int start = GetLineStartIndex(text, index);
    int length = 0;
    int i;
    wchar_t last = L'\0';

    while (start < textLen &&
           ((text[start] >= L'a' && text[start] <= L'z') ||
            (text[start] >= L'A' && text[start] <= L'Z') || text[start] == L'_')) {
        if (length < (int)(sizeof(word) / sizeof(word[0])) - 1) {
            word[length++] = text[start];
        }
        ++start;
    }
    word[length] = L'\0';

    for (i = 0; i < (int)(sizeof(keywords) / sizeof(keywords[0])); ++i) {
        if (wcscmp(word, keywords[i]) == 0) {
            while (start < textLen && text[start] != L'\r' && text[start] != L'\n') {
                if (text[start] == L'#') {
                    break;
                }
                if (text[start] != L' ' && text[start] != L'\t') {
                    last = text[start];
                }
                ++start;
            }
            return last == L':';
        }
    }
    return FALSE;
}

static BOOL SimulateTyping(WorkerParams *params)
{
    int i;
    BOOL atLineStart = FALSE;
    int pythonStructuralIndent = 0;

    if (WaitForSingleObject(params->hEventStop, START_DELAY_MS) == WAIT_OBJECT_0) {
        InterlockedExchange(&params->stopped, 1);
        return FALSE;
    }

    for (i = 0; i < params->textLen; ++i) {
        DWORD waitMs;
        wchar_t ch = params->text[i];

        if (InterlockedCompareExchange(&params->stopped, 0, 0) != 0) {
            return FALSE;
        }

        if (params->codeInputMode && atLineStart &&
            (ch == L' ' || ch == L'\t') &&
            IsWhitespaceOnlyLine(params->text, i, params->textLen)) {
            while (i < params->textLen && params->text[i] != L'\r' &&
                   params->text[i] != L'\n') {
                ++i;
            }
            if (i < params->textLen && params->text[i] == L'\r') {
                ++i;
                if (i < params->textLen && params->text[i] == L'\n') {
                    ++i;
                }
            } else if (i < params->textLen) {
                ++i;
            }
            PostMessageW(params->hwndMain, WM_WORKER_PROGRESS,
                         (WPARAM)i, (LPARAM)params->textLen);
            --i;
            continue;
        }

        // 暂停时阻塞在这里；继续时事件会恢复为 signaled。
        WaitForSingleObject(params->hEventPause, INFINITE);

        if (InterlockedCompareExchange(&params->stopped, 0, 0) != 0) {
            return FALSE;
        }

        if (params->codeInputMode && atLineStart &&
            (ch == L' ' || ch == L'\t')) {
            PostMessageW(params->hwndMain, WM_WORKER_PROGRESS,
                         (WPARAM)(i + 1), (LPARAM)params->textLen);
            continue;
        }

        if (ch != L'\r') {
            atLineStart = (ch == L'\n');
        }

        if (!SendChar(ch)) {
            InterlockedExchange(&params->stopped, 1);
            return FALSE;
        }

        if (params->codeInputMode && ch == L'\n' &&
            !IsPythonCommentOrBlank(params->text, i > 0 ? i - 1 : 0,
                                     params->textLen)) {
            int currentIndent = GetLineIndentUnits(params->text, i > 0 ? i - 1 : 0);
            int nextIndent = GetNextLineIndentUnits(params->text, i, params->textLen);

            if (!IsPythonCommentOrBlank(params->text, i > 0 ? i - 1 : 0,
                                        params->textLen)) {
                pythonStructuralIndent = IsPythonBlockHeader(
                    params->text, i > 0 ? i - 1 : 0, params->textLen)
                    ? currentIndent + 1 : currentIndent;
            }

            {
                int backspaces = pythonStructuralIndent - nextIndent;

                if (backspaces > 0) {
                    if (WaitForSingleObject(params->hEventStop,
                                            PYTHON_COMPENSATION_DELAY_MS) == WAIT_OBJECT_0) {
                        InterlockedExchange(&params->stopped, 1);
                        return FALSE;
                    }
                }
                while (backspaces > 0) {
                    if (!SendVKey(VK_BACK)) {
                        InterlockedExchange(&params->stopped, 1);
                        return FALSE;
                    }
                    --backspaces;
                }
            }
        }

        PostMessageW(params->hwndMain, WM_WORKER_PROGRESS,
                     (WPARAM)(i + 1), (LPARAM)params->textLen);

        waitMs = (DWORD)params->delayMs;
        if (ch == L' ') {
            waitMs += SPACE_EXTRA_DELAY_MS;
        } else if (ch == L'\n') {
            waitMs += NEWLINE_EXTRA_DELAY_MS;
        }

        if (WaitForSingleObject(params->hEventStop, waitMs) == WAIT_OBJECT_0) {
            InterlockedExchange(&params->stopped, 1);
            return FALSE;
        }
    }

    return TRUE;
}

static unsigned __stdcall WorkerThread(void *arg)
{
    WorkerParams *params = (WorkerParams *)arg;
    BOOL completed = SimulateTyping(params);

    PostMessageW(params->hwndMain, WM_WORKER_DONE,
                 (WPARAM)(completed ? 0 : 1), 0);
    return 0;
}

HANDLE Worker_Start(WorkerParams *params)
{
    uintptr_t threadHandle;

    if (!params) {
        return NULL;
    }

    params->hEventPause = CreateEventW(NULL, TRUE, TRUE, NULL);
    params->hEventStop  = CreateEventW(NULL, FALSE, FALSE, NULL);
    params->stopped     = 0;

    if (!params->hEventPause || !params->hEventStop) {
        if (params->hEventPause) {
            CloseHandle(params->hEventPause);
            params->hEventPause = NULL;
        }
        if (params->hEventStop) {
            CloseHandle(params->hEventStop);
            params->hEventStop = NULL;
        }
        return NULL;
    }

    threadHandle = _beginthreadex(NULL, 0, WorkerThread, params, 0, NULL);
    if (threadHandle == 0) {
        CloseHandle(params->hEventPause);
        CloseHandle(params->hEventStop);
        params->hEventPause = NULL;
        params->hEventStop = NULL;
        return NULL;
    }

    return (HANDLE)threadHandle;
}

void Worker_Pause(WorkerParams *params)
{
    if (params && params->hEventPause) {
        ResetEvent(params->hEventPause);
    }
}

void Worker_Resume(WorkerParams *params)
{
    if (params && params->hEventPause) {
        SetEvent(params->hEventPause);
    }
}

void Worker_Stop(WorkerParams *params)
{
    if (!params) {
        return;
    }

    InterlockedExchange(&params->stopped, 1);

    // 如果当前暂停中，先恢复，保证线程能检查到 stop。
    if (params->hEventPause) {
        SetEvent(params->hEventPause);
    }
    if (params->hEventStop) {
        SetEvent(params->hEventStop);
    }
}

void Worker_Free(WorkerParams *params)
{
    if (!params) {
        return;
    }

    if (params->hEventPause) {
        CloseHandle(params->hEventPause);
        params->hEventPause = NULL;
    }
    if (params->hEventStop) {
        CloseHandle(params->hEventStop);
        params->hEventStop = NULL;
    }
    if (params->text) {
        Mem_Free(params->text);
        params->text = NULL;
    }

    Mem_Free(params);
}
