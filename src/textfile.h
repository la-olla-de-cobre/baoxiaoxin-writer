#pragma once
#include <windows.h>

// ── 文本文件加载 ───────────────────────────────────────────
//
// 主界面与题库导入共用同一套编码探测，避免两处实现逐渐走样。
// 支持 UTF-16 LE/BE（带 BOM）与 UTF-8（带或不带 BOM）。

// 单个文件的大小上限。模拟键盘输入与题库导入都不需要超大文件，
// 设上限是为了避免误选大文件时一次性分配导致界面卡死或内存耗尽。
#define TEXTFILE_MAX_BYTES (16u * 1024u * 1024u)

typedef enum {
    TEXTFILE_OK = 0,
    TEXTFILE_ERR_OPEN,      // 打开失败
    TEXTFILE_ERR_EMPTY,     // 空文件
    TEXTFILE_ERR_TOO_LARGE, // 超过 TEXTFILE_MAX_BYTES
    TEXTFILE_ERR_READ,      // 读取失败
    TEXTFILE_ERR_ENCODING,  // 不是有效的 UTF-8 / UTF-16 文本
    TEXTFILE_ERR_MEMORY     // 内存不足
} TextFileStatus;

#ifdef __cplusplus
extern "C" {
#endif

// 读取整个文本文件并转换为宽字符串。
// 成功返回以 '\0' 结尾的缓冲区，由调用方 Mem_Free；失败返回 NULL。
// status 可为 NULL；否则写回具体失败原因。
wchar_t *TextFile_Load(const wchar_t *path, TextFileStatus *status);

// 将失败原因转换为可直接展示给用户的中文提示。
const wchar_t *TextFile_StatusText(TextFileStatus status);

#ifdef __cplusplus
}
#endif
