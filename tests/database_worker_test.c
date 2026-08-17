#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "database.h"
#include "mem.h"
#include "textfile.h"
#include "worker.h"

static int Fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

// 把原始字节写入临时文件，返回是否成功；路径写回 pathOut。
static BOOL WriteTempFile(const void *bytes, DWORD count, wchar_t *pathOut)
{
    wchar_t tempDir[MAX_PATH];
    HANDLE hFile;
    DWORD written = 0;
    BOOL ok;

    if (!GetTempPathW(MAX_PATH, tempDir) ||
        !GetTempFileNameW(tempDir, L"btf", 0, pathOut)) {
        return FALSE;
    }
    hFile = CreateFileW(pathOut, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    ok = (count == 0) ||
         (WriteFile(hFile, bytes, count, &written, NULL) && written == count);
    CloseHandle(hFile);
    return ok;
}

// 返回 0 表示通过，非 0 表示失败（并已打印原因）。
static int RunTextFileTests(void)
{
    wchar_t path[MAX_PATH];
    wchar_t *text;
    TextFileStatus status;

    // UTF-8（无 BOM）
    if (!WriteTempFile("hello \xE4\xB8\xAD\xE6\x96\x87", 12, path)) {
        return Fail("could not stage a UTF-8 sample file");
    }
    text = TextFile_Load(path, &status);
    DeleteFileW(path);
    if (!text || status != TEXTFILE_OK || wcscmp(text, L"hello 中文") != 0) {
        Mem_Free(text);
        return Fail("UTF-8 file did not round-trip");
    }
    Mem_Free(text);

    // UTF-16 LE（带 BOM）
    {
        const unsigned char utf16le[] = {
            0xFF, 0xFE, 'h', 0x00, 'i', 0x00
        };
        if (!WriteTempFile(utf16le, sizeof(utf16le), path)) {
            return Fail("could not stage a UTF-16 LE sample file");
        }
    }
    text = TextFile_Load(path, &status);
    DeleteFileW(path);
    if (!text || status != TEXTFILE_OK || wcscmp(text, L"hi") != 0) {
        Mem_Free(text);
        return Fail("UTF-16 LE file did not round-trip");
    }
    Mem_Free(text);

    // UTF-16 LE 字节数为奇数 → 应判为编码错误而不是读出半个字符
    {
        const unsigned char truncated[] = { 0xFF, 0xFE, 'h', 0x00, 'i' };
        if (!WriteTempFile(truncated, sizeof(truncated), path)) {
            return Fail("could not stage a truncated UTF-16 sample file");
        }
    }
    text = TextFile_Load(path, &status);
    DeleteFileW(path);
    if (text || status != TEXTFILE_ERR_ENCODING) {
        Mem_Free(text);
        return Fail("odd-length UTF-16 file was not rejected");
    }

    // 非法 UTF-8 字节序列 → 编码错误
    {
        const unsigned char invalid[] = { 'a', 0xC3, 0x28, 'b' };
        if (!WriteTempFile(invalid, sizeof(invalid), path)) {
            return Fail("could not stage an invalid UTF-8 sample file");
        }
    }
    text = TextFile_Load(path, &status);
    DeleteFileW(path);
    if (text || status != TEXTFILE_ERR_ENCODING) {
        Mem_Free(text);
        return Fail("invalid UTF-8 was accepted");
    }

    // 空文件
    if (!WriteTempFile("", 0, path)) {
        return Fail("could not stage an empty sample file");
    }
    text = TextFile_Load(path, &status);
    DeleteFileW(path);
    if (text || status != TEXTFILE_ERR_EMPTY) {
        Mem_Free(text);
        return Fail("empty file was not reported as empty");
    }

    // 超过上限的文件必须被拒绝，而不是尝试整块分配
    {
        const DWORD chunk = 1024u * 1024u;
        char *filler = (char *)Mem_Alloc(chunk);
        wchar_t tempDir[MAX_PATH];
        HANDLE hFile;
        BOOL staged = TRUE;
        DWORD written = 0;
        DWORD total = 0;

        if (!filler) {
            return Fail("could not allocate oversize-test buffer");
        }
        memset(filler, 'x', chunk);

        if (!GetTempPathW(MAX_PATH, tempDir) ||
            !GetTempFileNameW(tempDir, L"btf", 0, path)) {
            Mem_Free(filler);
            return Fail("could not stage an oversize sample file");
        }
        hFile = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            Mem_Free(filler);
            return Fail("could not open the oversize sample file");
        }
        while (total <= TEXTFILE_MAX_BYTES) {
            if (!WriteFile(hFile, filler, chunk, &written, NULL) ||
                written != chunk) {
                staged = FALSE;
                break;
            }
            total += chunk;
        }
        CloseHandle(hFile);
        Mem_Free(filler);
        if (!staged) {
            DeleteFileW(path);
            return Fail("could not write the oversize sample file");
        }

        text = TextFile_Load(path, &status);
        DeleteFileW(path);
        if (text || status != TEXTFILE_ERR_TOO_LARGE) {
            Mem_Free(text);
            return Fail("oversize file was not rejected by the size cap");
        }
    }

    return 0;
}

int main(void)
{
    DbContext ctx;
    wchar_t tempPath[MAX_PATH];
    wchar_t dbPath[MAX_PATH];
    wchar_t *longQuestion;
    wchar_t *longAnswer;
    wchar_t *answer;
    wchar_t *fuzzyQuestion;
    int count = 0;
    struct QAPair *pairs;
    int result = 1;

    if (!GetTempPathW(MAX_PATH, tempPath) ||
        !GetTempFileNameW(tempPath, L"bwx", 0, dbPath)) {
        return Fail("could not create a temporary database path");
    }
    DeleteFileW(dbPath);

    if (Db_Init(&ctx, dbPath) != DB_OK) {
        return Fail("Db_Init failed");
    }

    longQuestion = (wchar_t *)Mem_Alloc(6001 * sizeof(wchar_t));
    longAnswer = (wchar_t *)Mem_Alloc(7001 * sizeof(wchar_t));
    if (!longQuestion || !longAnswer) {
        result = Fail("could not allocate long test values");
        goto cleanup;
    }
    wmemset(longQuestion, L'q', 6000);
    longQuestion[6000] = L'\0';
    wmemset(longAnswer, L'a', 7000);
    longAnswer[7000] = L'\0';

    if (Db_Insert(&ctx, longQuestion, longAnswer) != DB_OK) {
        result = Fail("Db_Insert rejected long UTF-16 values");
        goto cleanup;
    }
    answer = Db_Search(&ctx, longQuestion);
    if (!answer || wcscmp(answer, longAnswer) != 0) {
        Mem_Free(answer);
        result = Fail("exact search did not return the long answer");
        goto cleanup;
    }
    Mem_Free(answer);

    fuzzyQuestion = Mem_WcsDup(longQuestion + 1000);
    if (!fuzzyQuestion) {
        result = Fail("could not allocate fuzzy search value");
        goto cleanup;
    }
    answer = Db_SearchFuzzy(&ctx, fuzzyQuestion);
    Mem_Free(fuzzyQuestion);
    if (!answer || wcscmp(answer, longAnswer) != 0) {
        Mem_Free(answer);
        result = Fail("fuzzy search did not return the long answer");
        goto cleanup;
    }
    Mem_Free(answer);

    if (Db_ImportFromText(&ctx,
            L"题目:import-one 答案:answer-one\r\n"
            L"题目:import-two 答案:answer-two\r\n", NULL) != 2) {
        result = Fail("batch import did not report two successful writes");
        goto cleanup;
    }
    answer = Db_Search(&ctx, L"import-two");
    if (!answer || wcscmp(answer, L"answer-two") != 0) {
        Mem_Free(answer);
        result = Fail("batch-imported record could not be read back");
        goto cleanup;
    }
    Mem_Free(answer);

    // 题目含空格时必须完整保留，不能在第一个空格处被截断
    if (Db_ImportFromText(&ctx,
            L"题目:what is python 答案:a programming language\r\n",
            NULL) != 1) {
        result = Fail("import rejected a question containing spaces");
        goto cleanup;
    }
    answer = Db_Search(&ctx, L"what is python");
    if (!answer || wcscmp(answer, L"a programming language") != 0) {
        Mem_Free(answer);
        result = Fail("question containing spaces was truncated on import");
        goto cleanup;
    }
    Mem_Free(answer);
    answer = Db_Search(&ctx, L"what");
    if (answer) {
        Mem_Free(answer);
        result = Fail("truncated question was stored instead of the full text");
        goto cleanup;
    }

    // 题目内部的字面量 "答案:" 不能被当成分隔符
    if (Db_ImportFromText(&ctx,
            L"题目:如何理解答案:这个标记 答案:它只是普通文字\r\n",
            NULL) != 1) {
        result = Fail("import rejected a question containing a literal marker");
        goto cleanup;
    }
    answer = Db_Search(&ctx, L"如何理解答案:这个标记");
    if (!answer || wcscmp(answer, L"它只是普通文字") != 0) {
        Mem_Free(answer);
        result = Fail("literal 答案: inside a question split the line early");
        goto cleanup;
    }
    Mem_Free(answer);

    // 格式不正确的非空行必须被计入 skipped，而不是静默丢弃
    {
        int skipped = -1;
        if (Db_ImportFromText(&ctx,
                L"题目:skip-counted 答案:kept\r\n"
                L"这是一行没有任何标记的文本\r\n"
                L"题目:只有题目没有答案\r\n", &skipped) != 1) {
            result = Fail("import miscounted successful writes among bad lines");
            goto cleanup;
        }
        if (skipped != 2) {
            result = Fail("malformed lines were skipped without being reported");
            goto cleanup;
        }
    }

    pairs = Db_GetAllPairs(&ctx, &count);
    if (!pairs || count != 6) {
        Db_FreeResults(pairs, count);
        result = Fail("expected six readable database records");
        goto cleanup;
    }
    Db_FreeResults(pairs, count);

    if (Worker_Start(NULL) != NULL) {
        result = Fail("Worker_Start(NULL) unexpectedly started a worker");
        goto cleanup;
    }

    if (RunTextFileTests() != 0) {
        result = 1;
        goto cleanup;
    }

    puts("PASS: database, worker and text-file regression tests");
    result = 0;

cleanup:
    Mem_Free(longQuestion);
    Mem_Free(longAnswer);
    Db_Close(&ctx);
    DeleteFileW(dbPath);
    return result;
}
