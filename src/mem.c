#include "mem.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// 统一后端：CRT 堆。选择 CRT 而非 HeapAlloc，是因为项目中
// 字符串处理与第三方接口的拷出缓冲区大多本就走 CRT 分配。

void *Mem_Alloc(size_t bytes)
{
    if (bytes == 0) {
        bytes = 1;
    }
    return malloc(bytes);
}

void *Mem_AllocZero(size_t bytes)
{
    void *ptr;

    if (bytes == 0) {
        bytes = 1;
    }
    ptr = malloc(bytes);
    if (ptr) {
        memset(ptr, 0, bytes);
    }
    return ptr;
}

void *Mem_Realloc(void *ptr, size_t bytes)
{
    if (bytes == 0) {
        bytes = 1;
    }
    return realloc(ptr, bytes);
}

void Mem_Free(void *ptr)
{
    free(ptr);
}

wchar_t *Mem_WcsDup(const wchar_t *text)
{
    size_t chars;
    wchar_t *copy;

    if (!text) {
        return NULL;
    }
    chars = wcslen(text) + 1;
    if (chars > (size_t)-1 / sizeof(wchar_t)) {
        return NULL;
    }
    copy = (wchar_t *)Mem_Alloc(chars * sizeof(wchar_t));
    if (copy) {
        memcpy(copy, text, chars * sizeof(wchar_t));
    }
    return copy;
}
