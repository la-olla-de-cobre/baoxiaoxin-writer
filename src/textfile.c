#include "textfile.h"
#include "mem.h"

static void SetStatus(TextFileStatus *slot, TextFileStatus value)
{
    if (slot) {
        *slot = value;
    }
}

wchar_t *TextFile_Load(const wchar_t *path, TextFileStatus *status)
{
    HANDLE hFile;
    LARGE_INTEGER fileSize;
    DWORD byteCount;
    DWORD bytesRead = 0;
    DWORD offset = 0;
    BYTE *bytes;
    wchar_t *text = NULL;
    int chars;

    SetStatus(status, TEXTFILE_OK);

    hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        SetStatus(status, TEXTFILE_ERR_OPEN);
        return NULL;
    }

    // 用 64 位接口取大小，避免 32 位截断后再做算术时回绕。
    if (!GetFileSizeEx(hFile, &fileSize)) {
        CloseHandle(hFile);
        SetStatus(status, TEXTFILE_ERR_OPEN);
        return NULL;
    }
    if (fileSize.QuadPart == 0) {
        CloseHandle(hFile);
        SetStatus(status, TEXTFILE_ERR_EMPTY);
        return NULL;
    }
    if (fileSize.QuadPart > (LONGLONG)TEXTFILE_MAX_BYTES) {
        CloseHandle(hFile);
        SetStatus(status, TEXTFILE_ERR_TOO_LARGE);
        return NULL;
    }
    byteCount = (DWORD)fileSize.QuadPart;

    bytes = (BYTE *)Mem_Alloc(byteCount);
    if (!bytes) {
        CloseHandle(hFile);
        SetStatus(status, TEXTFILE_ERR_MEMORY);
        return NULL;
    }
    if (!ReadFile(hFile, bytes, byteCount, &bytesRead, NULL) ||
        bytesRead != byteCount) {
        CloseHandle(hFile);
        Mem_Free(bytes);
        SetStatus(status, TEXTFILE_ERR_READ);
        return NULL;
    }
    CloseHandle(hFile);

    if (byteCount >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        // UTF-16 LE
        offset = 2;
        if ((byteCount - offset) % sizeof(wchar_t) != 0) {
            Mem_Free(bytes);
            SetStatus(status, TEXTFILE_ERR_ENCODING);
            return NULL;
        }
        chars = (int)((byteCount - offset) / sizeof(wchar_t));
        text = (wchar_t *)Mem_Alloc((size_t)(chars + 1) * sizeof(wchar_t));
        if (text) {
            memcpy(text, bytes + offset, (size_t)chars * sizeof(wchar_t));
            text[chars] = L'\0';
        }
    } else if (byteCount >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        // UTF-16 BE — 字节交换
        offset = 2;
        if ((byteCount - offset) % 2 != 0) {
            Mem_Free(bytes);
            SetStatus(status, TEXTFILE_ERR_ENCODING);
            return NULL;
        }
        chars = (int)((byteCount - offset) / 2);
        text = (wchar_t *)Mem_Alloc((size_t)(chars + 1) * sizeof(wchar_t));
        if (text) {
            for (int i = 0; i < chars; ++i) {
                text[i] = (wchar_t)((bytes[offset + i * 2] << 8) |
                                    bytes[offset + i * 2 + 1]);
            }
            text[chars] = L'\0';
        }
    } else {
        // UTF-8（有无 BOM 均可）
        if (byteCount >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB &&
            bytes[2] == 0xBF) {
            offset = 3;
        }
        if (byteCount == offset) {
            Mem_Free(bytes);
            SetStatus(status, TEXTFILE_ERR_EMPTY);
            return NULL;
        }
        chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                    (const char *)(bytes + offset),
                                    (int)(byteCount - offset), NULL, 0);
        if (chars <= 0) {
            Mem_Free(bytes);
            SetStatus(status, TEXTFILE_ERR_ENCODING);
            return NULL;
        }
        text = (wchar_t *)Mem_Alloc((size_t)(chars + 1) * sizeof(wchar_t));
        if (text) {
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                (const char *)(bytes + offset),
                                (int)(byteCount - offset), text, chars);
            text[chars] = L'\0';
        }
    }

    Mem_Free(bytes);
    if (!text) {
        SetStatus(status, TEXTFILE_ERR_MEMORY);
        return NULL;
    }
    return text;
}

const wchar_t *TextFile_StatusText(TextFileStatus status)
{
    switch (status) {
    case TEXTFILE_OK:            return L"读取成功。";
    case TEXTFILE_ERR_OPEN:      return L"无法打开文件。";
    case TEXTFILE_ERR_EMPTY:     return L"文件为空。";
    case TEXTFILE_ERR_TOO_LARGE: return L"文件过大，请选择 16 MB 以内的文本文件。";
    case TEXTFILE_ERR_READ:      return L"读取文件失败。";
    case TEXTFILE_ERR_ENCODING:  return L"不是有效的 UTF-8 或 UTF-16 文本。";
    case TEXTFILE_ERR_MEMORY:    return L"内存不足。";
    }
    return L"未知错误。";
}
