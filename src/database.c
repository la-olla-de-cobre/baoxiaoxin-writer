#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <string.h>
#include <limits.h>
#include "database.h"

// 辅助函数：显示错误消息
static void ShowError(const wchar_t *msg)
{
    MessageBoxW(NULL, msg, L"数据库错误", MB_ICONERROR);
}

static char *WideToUtf8(const wchar_t *text)
{
    int size;
    char *utf8;

    if (!text) return NULL;
    size = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (size <= 0) return NULL;

    utf8 = (char *)malloc((size_t)size);
    if (!utf8) return NULL;
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, size, NULL, NULL) <= 0) {
        free(utf8);
        return NULL;
    }
    return utf8;
}

static wchar_t *Utf8ToWide(const char *text)
{
    int size;
    wchar_t *wide;

    if (!text) return NULL;
    size = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (size <= 0) return NULL;

    wide = (wchar_t *)malloc((size_t)size * sizeof(wchar_t));
    if (!wide) return NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, size) <= 0) {
        free(wide);
        return NULL;
    }
    return wide;
}

static wchar_t *DuplicateLower(const wchar_t *text)
{
    wchar_t *copy = _wcsdup(text);
    if (copy) _wcslwr(copy);
    return copy;
}

int Db_Init(DbContext *ctx, const wchar_t *dbPath)
{
    if (!ctx || !dbPath) {
        return DB_ERROR_OPEN;
    }
    if (wcslen(dbPath) >= MAX_PATH) {
        return DB_ERROR_OPEN;
    }

    memset(ctx, 0, sizeof(DbContext));
    wcscpy(ctx->dbPath, dbPath);

    wchar_t debugMsg[512];
    wsprintfW(debugMsg, L"初始化数据库，路径: %s", dbPath);
    OutputDebugStringW(debugMsg);

    int result;

    // 打开或创建数据库
    char utf8Path[MAX_PATH * 3];
    int len = WideCharToMultiByte(CP_UTF8, 0, dbPath, -1, utf8Path, sizeof(utf8Path), NULL, NULL);
    if (len <= 0) {
        OutputDebugStringW(L"路径转换失败!");
        return DB_ERROR_OPEN;
    }

    result = sqlite3_open(utf8Path, &ctx->db);
    if (result != 0) {
        wsprintfW(debugMsg, L"无法打开数据库! SQLite错误: %d", result);
        ShowError(debugMsg);
        if (ctx->db) {
            sqlite3_close(ctx->db);
            ctx->db = NULL;
        }
        return DB_ERROR_OPEN;
    }

    OutputDebugStringW(L"数据库打开成功!");

    // 创建表
    const char *createTable =
        "CREATE TABLE IF NOT EXISTS qa_pairs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "question TEXT NOT NULL UNIQUE,"
        "answer TEXT NOT NULL,"
        "create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "update_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "UNIQUE(question) ON CONFLICT REPLACE"
        ");";

    char *errMsg = NULL;
    result = sqlite3_exec(ctx->db, createTable, NULL, NULL, &errMsg);
    if (result != 0) {
        wsprintfW(debugMsg, L"创建表失败! 错误: %S", errMsg ? errMsg : "未知错误");
        ShowError(debugMsg);
        if (errMsg) sqlite3_free(errMsg);
        sqlite3_close(ctx->db);
        ctx->db = NULL;
        return DB_ERROR_CREATE;
    }

    OutputDebugStringW(L"表创建/检查成功!");

    // 为题目字段创建索引，提高搜索速度
    const char *createIndex = "CREATE INDEX IF NOT EXISTS idx_question ON qa_pairs(question);";
    result = sqlite3_exec(ctx->db, createIndex, NULL, NULL, &errMsg);
    if (result != 0) {
        OutputDebugStringW(L"创建索引失败 (但表已成功创建)");
        if (errMsg) sqlite3_free(errMsg);
    }

    OutputDebugStringW(L"数据库初始化完成!");
    return DB_OK;
}

int Db_Close(DbContext *ctx)
{
    OutputDebugStringW(L"关闭数据库...");
    if (ctx->db) {
        sqlite3_close(ctx->db);
        ctx->db = NULL;
    }
    memset(ctx->dbPath, 0, sizeof(ctx->dbPath));
    OutputDebugStringW(L"数据库已关闭");
    return DB_OK;
}

int Db_Insert(DbContext *ctx, const wchar_t *question, const wchar_t *answer)
{
    if (!ctx->db) {
        OutputDebugStringW(L"数据库未初始化!");
        return DB_ERROR_OPEN;
    }

    char *utf8Question = WideToUtf8(question);
    char *utf8Answer = WideToUtf8(answer);

    if (!utf8Question || !utf8Answer) {
        free(utf8Question);
        free(utf8Answer);
        OutputDebugStringW(L"文本转换失败!");
        return DB_ERROR_MEMORY;
    }

    // 准备SQL语句
    const char *sql = "REPLACE INTO qa_pairs (question, answer, update_time) VALUES (?, ?, CURRENT_TIMESTAMP);";
    sqlite3_stmt *stmt;
    int result = sqlite3_prepare_v2(ctx->db, sql, -1, &stmt, NULL);
    if (result != 0) {
        free(utf8Question);
        free(utf8Answer);
        OutputDebugStringW(L"SQL准备失败!");
        return DB_ERROR_EXEC;
    }

    sqlite3_bind_text(stmt, 1, utf8Question, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, utf8Answer, -1, SQLITE_TRANSIENT);

    result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    free(utf8Question);
    free(utf8Answer);

    if (result != SQLITE_DONE) {
        OutputDebugStringW(L"SQL执行失败!");
        return DB_ERROR_EXEC;
    }

    OutputDebugStringW(L"数据插入成功!");
    return DB_OK;
}

wchar_t* Db_Search(DbContext *ctx, const wchar_t *question)
{
    if (!ctx->db) {
        OutputDebugStringW(L"数据库未初始化!");
        return NULL;
    }

    char *utf8Question = WideToUtf8(question);
    if (!utf8Question) {
        OutputDebugStringW(L"文本转换失败!");
        return NULL;
    }

    const char *sql = "SELECT answer FROM qa_pairs WHERE question = ?;";
    sqlite3_stmt *stmt;
    int result = sqlite3_prepare_v2(ctx->db, sql, -1, &stmt, NULL);
    if (result != 0) {
        free(utf8Question);
        OutputDebugStringW(L"SQL准备失败!");
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, utf8Question, -1, SQLITE_TRANSIENT);
    free(utf8Question);

    result = sqlite3_step(stmt);
    if (result != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        OutputDebugStringW(L"未找到匹配的题目");
        return NULL;
    }

    const unsigned char *utf8Answer = sqlite3_column_text(stmt, 0);
    if (!utf8Answer) {
        sqlite3_finalize(stmt);
        OutputDebugStringW(L"答案为空!");
        return NULL;
    }

    wchar_t *answer = Utf8ToWide((const char *)utf8Answer);
    if (!answer) {
        sqlite3_finalize(stmt);
        OutputDebugStringW(L"内存分配失败!");
        return NULL;
    }

    sqlite3_finalize(stmt);

    OutputDebugStringW(L"找到匹配的答案!");
    return answer;
}

wchar_t* Db_SearchFuzzy(DbContext *ctx, const wchar_t *question)
{
    // 先尝试精确匹配
    wchar_t *answer = Db_Search(ctx, question);
    if (answer) return answer;

    if (!ctx->db) return NULL;

    // 获取所有题目
    int count = 0;
    struct QAPair *pairs = Db_GetAllPairs(ctx, &count);
    if (!pairs || count == 0) return NULL;

    // 将搜索词转为小写
    wchar_t *qLower = DuplicateLower(question);
    if (!qLower) {
        Db_FreeResults(pairs, count);
        return NULL;
    }

    wchar_t *bestAnswer = NULL;
    double bestScore = 0;

    for (int i = 0; i < count; i++) {
        wchar_t *stored = DuplicateLower(pairs[i].question);
        if (!stored) continue;

        if (wcsstr(stored, qLower)) {
            double score = (double)wcslen(question) / (double)wcslen(pairs[i].question);
            if (score > bestScore) {
                bestScore = score;
                if (bestAnswer) free(bestAnswer);
                bestAnswer = _wcsdup(pairs[i].answer);
            }
        }
        free(stored);
    }

    free(qLower);
    Db_FreeResults(pairs, count);
    return bestAnswer;
}

int Db_Delete(DbContext *ctx, const wchar_t *question)
{
    if (!ctx->db) {
        return DB_ERROR_OPEN;
    }

    char *utf8Question = WideToUtf8(question);
    if (!utf8Question) {
        return DB_ERROR_MEMORY;
    }

    const char *sql = "DELETE FROM qa_pairs WHERE question = ?;";
    sqlite3_stmt *stmt;
    int result = sqlite3_prepare_v2(ctx->db, sql, -1, &stmt, NULL);
    if (result != 0) {
        free(utf8Question);
        return DB_ERROR_EXEC;
    }

    sqlite3_bind_text(stmt, 1, utf8Question, -1, SQLITE_TRANSIENT);
    free(utf8Question);

    result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (result != SQLITE_DONE) {
        return DB_ERROR_EXEC;
    }

    return DB_OK;
}

struct QAPair* Db_GetAllPairs(DbContext *ctx, int *count)
{
    if (!count) {
        return NULL;
    }
    *count = 0;
    if (!ctx || !ctx->db) return NULL;

    const char *sql = "SELECT question, answer FROM qa_pairs ORDER BY id ASC;";
    sqlite3_stmt *stmt;
    int result = sqlite3_prepare_v2(ctx->db, sql, -1, &stmt, NULL);
    if (result != 0) {
        return NULL;
    }

    size_t capacity = 16;
    int rowCount = 0;
    struct QAPair *pairs = (struct QAPair *)malloc(capacity * sizeof(struct QAPair));
    if (!pairs) {
        sqlite3_finalize(stmt);
        return NULL;
    }

    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (rowCount == INT_MAX) {
            sqlite3_finalize(stmt);
            Db_FreeResults(pairs, rowCount);
            return NULL;
        }
        if ((size_t)rowCount == capacity) {
            if (capacity > (size_t)-1 / 2 / sizeof(struct QAPair)) {
                sqlite3_finalize(stmt);
                Db_FreeResults(pairs, rowCount);
                return NULL;
            }
            capacity *= 2;
            struct QAPair *resized = (struct QAPair *)realloc(
                pairs, capacity * sizeof(struct QAPair));
            if (!resized) {
                sqlite3_finalize(stmt);
                Db_FreeResults(pairs, rowCount);
                return NULL;
            }
            pairs = resized;
        }

        const unsigned char *utf8Question = sqlite3_column_text(stmt, 0);
        const unsigned char *utf8Answer = sqlite3_column_text(stmt, 1);
        wchar_t *question = Utf8ToWide((const char *)utf8Question);
        wchar_t *answer = Utf8ToWide((const char *)utf8Answer);

        if (!question || !answer) {
            free(question);
            free(answer);
            sqlite3_finalize(stmt);
            Db_FreeResults(pairs, rowCount);
            return NULL;
        }

        pairs[rowCount].question = question;
        pairs[rowCount].answer = answer;
        rowCount++;
    }

    sqlite3_finalize(stmt);
    if (result != SQLITE_DONE) {
        Db_FreeResults(pairs, rowCount);
        return NULL;
    }
    if (rowCount == 0) {
        free(pairs);
        return NULL;
    }

    *count = rowCount;
    return pairs;
}

void Db_FreeResults(struct QAPair *pairs, int count)
{
    if (!pairs) return;

    for (int i = 0; i < count; i++) {
        if (pairs[i].question) free(pairs[i].question);
        if (pairs[i].answer) free(pairs[i].answer);
    }
    free(pairs);
}

int Db_ImportFromText(DbContext *ctx, const wchar_t *text)
{
    if (!ctx->db) {
        return DB_ERROR_OPEN;
    }

    int importedCount = 0;
    BOOL failed = FALSE;
    wchar_t *line = _wcsdup(text);
    if (!line) {
        return DB_ERROR_MEMORY;
    }

    if (sqlite3_exec(ctx->db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK) {
        free(line);
        return DB_ERROR_EXEC;
    }

    // 按行分割
    wchar_t *p = line;
    wchar_t *next;

    while (*p) {
        // 找到换行符
        next = wcsstr(p, L"\n");
        if (next) *next = L'\0';

        // 去掉首尾空格
        wchar_t *trimmed = p;
        while (*trimmed && (*trimmed == L' ' || *trimmed == L'\t' || *trimmed == L'\r')) trimmed++;
        wchar_t *end = trimmed;
        while (*end) end++;
        while (end > trimmed && (*(end - 1) == L' ' || *(end - 1) == L'\t' || *(end - 1) == L'\r')) end--;
        *end = L'\0';

        if (*trimmed) {
            // 解析格式：题目:题目内容 答案:答案内容
            wchar_t *qMark = wcsstr(trimmed, L"题目:");
            wchar_t *aMark = wcsstr(trimmed, L"答案:");

            if (qMark && aMark && qMark < aMark) {
                wchar_t *question = qMark + 3; // 跳过"题目:"
                wchar_t *answer = aMark + 3;  // 跳过"答案:"

                // 找到题目和答案之间的空格分割
                wchar_t *space = question;
                while (*space && space < aMark) {
                    if (*space == L' ' || *space == L'\t') {
                        *space = L'\0';
                        // 跳过多余的空格
                        while (*(space + 1) == L' ' || *(space + 1) == L'\t') space++;
                        if (wcslen(question) > 0 && wcslen(answer) > 0) {
                            if (Db_Insert(ctx, question, answer) == DB_OK) {
                                importedCount++;
                            } else {
                                failed = TRUE;
                            }
                        }
                        break;
                    }
                    space++;
                }
            }
        }

        if (!next) break;
        p = next + 1;
    }

    free(line);
    if (failed) {
        sqlite3_exec(ctx->db, "ROLLBACK;", NULL, NULL, NULL);
        return DB_ERROR_EXEC;
    }
    if (sqlite3_exec(ctx->db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_exec(ctx->db, "ROLLBACK;", NULL, NULL, NULL);
        return DB_ERROR_EXEC;
    }
    return importedCount;
}

wchar_t* Db_ExportToText(DbContext *ctx)
{
    if (!ctx->db) {
        return NULL;
    }

    int count = 0;
    struct QAPair *pairs = Db_GetAllPairs(ctx, &count);
    if (!pairs || count == 0) {
        return NULL;
    }

    // 计算需要的缓冲区大小
    size_t totalLen = 0;
    for (int i = 0; i < count; i++) {
        totalLen += wcslen(pairs[i].question) + wcslen(pairs[i].answer) + 20; // 题目:答案:和换行
    }
    totalLen += 1; // 结束符

    wchar_t *text = (wchar_t *)malloc(totalLen * sizeof(wchar_t));
    if (!text) {
        Db_FreeResults(pairs, count);
        return NULL;
    }
    text[0] = L'\0';

    for (int i = 0; i < count; i++) {
        wcscat(text, L"题目:");
        wcscat(text, pairs[i].question);
        wcscat(text, L" 答案:");
        wcscat(text, pairs[i].answer);
        wcscat(text, L"\r\n");
    }

    Db_FreeResults(pairs, count);
    return text;
}

BOOL Db_IsInitialized(DbContext *ctx)
{
    return (ctx->db != NULL);
}
