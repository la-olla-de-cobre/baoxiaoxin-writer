#pragma once
#include <windows.h>

// ── 统一堆管理 ─────────────────────────────────────────────
//
// 全项目只使用这一组函数分配和释放内存，不再混用
// HeapAlloc/HeapFree 与 malloc/free。
//
// 所有权约定：
//   - Mem_Alloc / Mem_AllocZero / Mem_Realloc / Mem_WcsDup 返回的指针，
//     一律由调用方使用 Mem_Free 释放。
//   - 凡是返回堆指针的接口，都必须在声明处注明由调用方 Mem_Free。
//   - Mem_Free(NULL) 是合法的空操作。

#ifdef __cplusplus
extern "C" {
#endif

// 分配 bytes 字节；失败返回 NULL。由调用方 Mem_Free。
void *Mem_Alloc(size_t bytes);

// 分配并清零 bytes 字节；失败返回 NULL。由调用方 Mem_Free。
void *Mem_AllocZero(size_t bytes);

// 调整大小。失败时返回 NULL 且原指针保持有效（调用方仍需释放原指针）。
void *Mem_Realloc(void *ptr, size_t bytes);

// 释放 Mem_* 分配的内存；ptr 为 NULL 时不做任何事。
void Mem_Free(void *ptr);

// 复制宽字符串；失败或 text 为 NULL 时返回 NULL。由调用方 Mem_Free。
wchar_t *Mem_WcsDup(const wchar_t *text);

#ifdef __cplusplus
}
#endif
