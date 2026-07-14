#include "worker.h"
#include "resource.h"
#include <process.h>
#include <stdint.h>
#include <stdlib.h>

#define START_DELAY_MS 500
#define SPACE_EXTRA_DELAY_MS 60
#define NEWLINE_EXTRA_DELAY_MS 120

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

static BOOL IsHashCommentStart(const wchar_t *text, int index, int textLen)
{
    int start = index;
    wchar_t word[16];
    int length = 0;

    while (start > 0 && (text[start - 1] == L' ' || text[start - 1] == L'\t')) {
        --start;
    }
    if (start > 0 && text[start - 1] != L'\n' && text[start - 1] != L'\r') {
        return FALSE;
    }

    ++index;
    while (index < textLen &&
           ((text[index] >= L'a' && text[index] <= L'z') ||
            (text[index] >= L'A' && text[index] <= L'Z'))) {
        if (length < (int)(sizeof(word) / sizeof(word[0])) - 1) {
            word[length++] = text[index];
        }
        ++index;
    }
    word[length] = L'\0';

    return wcscmp(word, L"include") != 0 &&
           wcscmp(word, L"define") != 0 &&
           wcscmp(word, L"if") != 0 &&
           wcscmp(word, L"ifdef") != 0 &&
           wcscmp(word, L"ifndef") != 0 &&
           wcscmp(word, L"elif") != 0 &&
           wcscmp(word, L"else") != 0 &&
           wcscmp(word, L"endif") != 0 &&
           wcscmp(word, L"pragma") != 0 &&
           wcscmp(word, L"error") != 0 &&
           wcscmp(word, L"line") != 0 &&
           wcscmp(word, L"undef") != 0;
}

static BOOL SimulateTyping(WorkerParams *params)
{
    int i;
    BOOL atLineStart = FALSE;
    BOOL inString = FALSE;
    BOOL inChar = FALSE;
    BOOL escaped = FALSE;
    BOOL inLineComment = FALSE;
    BOOL inBlockComment = FALSE;

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

        if (params->codeInputMode) {
            wchar_t next = (i + 1 < params->textLen) ? params->text[i + 1] : L'\0';

            if (inLineComment) {
                if (ch != L'\r' && ch != L'\n') {
                    PostMessageW(params->hwndMain, WM_WORKER_PROGRESS,
                                 (WPARAM)(i + 1), (LPARAM)params->textLen);
                    continue;
                }
                inLineComment = FALSE;
            } else if (inBlockComment) {
                if (ch == L'*' && next == L'/') {
                    ++i;
                    PostMessageW(params->hwndMain, WM_WORKER_PROGRESS,
                                 (WPARAM)(i + 1), (LPARAM)params->textLen);
                    inBlockComment = FALSE;
                    continue;
                }
                if (ch != L'\r' && ch != L'\n') {
                    PostMessageW(params->hwndMain, WM_WORKER_PROGRESS,
                                 (WPARAM)(i + 1), (LPARAM)params->textLen);
                    continue;
                }
            } else if (!inString && !inChar && ch == L'/' && next == L'/') {
                ++i;
                inLineComment = TRUE;
                PostMessageW(params->hwndMain, WM_WORKER_PROGRESS,
                             (WPARAM)(i + 1), (LPARAM)params->textLen);
                continue;
            } else if (!inString && !inChar && ch == L'/' && next == L'*') {
                ++i;
                inBlockComment = TRUE;
                PostMessageW(params->hwndMain, WM_WORKER_PROGRESS,
                             (WPARAM)(i + 1), (LPARAM)params->textLen);
                continue;
            } else if (!inString && !inChar && ch == L'#' &&
                       IsHashCommentStart(params->text, i, params->textLen)) {
                inLineComment = TRUE;
                PostMessageW(params->hwndMain, WM_WORKER_PROGRESS,
                             (WPARAM)(i + 1), (LPARAM)params->textLen);
                continue;
            }
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

        if (params->codeInputMode && !inLineComment && !inBlockComment) {
            if (inString || inChar) {
                if (escaped) {
                    escaped = FALSE;
                } else if (ch == L'\\') {
                    escaped = TRUE;
                } else if ((inString && ch == L'"') ||
                           (inChar && ch == L'\'')) {
                    inString = FALSE;
                    inChar = FALSE;
                }
            } else if (ch == L'"') {
                inString = TRUE;
            } else if (ch == L'\'') {
                inChar = TRUE;
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

    params->hEventPause = CreateEventW(NULL, TRUE, TRUE, NULL);
    params->hEventStop  = CreateEventW(NULL, FALSE, FALSE, NULL);
    params->stopped     = 0;

    if (!params->hEventPause || !params->hEventStop) {
        Worker_Free(params);
        return NULL;
    }

    threadHandle = _beginthreadex(NULL, 0, WorkerThread, params, 0, NULL);
    if (threadHandle == 0) {
        Worker_Free(params);
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
        HeapFree(GetProcessHeap(), 0, params->text);
        params->text = NULL;
    }

    HeapFree(GetProcessHeap(), 0, params);
}
