#pragma once
#include <windows.h>
#include "sqlite3.h"

// ── 错误代码 ────────────────────────────────────────────────
#define DB_OK               0
#define DB_ERROR_OPEN      -1
#define DB_ERROR_CREATE    -2
#define DB_ERROR_EXEC      -3
#define DB_ERROR_QUERY     -4
#define DB_ERROR_MEMORY    -5
#define DB_ERROR_NOT_FOUND -6

// ── 数据库操作上下文 ───────────────────────────────────────
typedef struct {
    sqlite3    *db;
    wchar_t     dbPath[MAX_PATH];
} DbContext;

// ── 导出函数声明 ───────────────────────────────────────────

#ifdef __cplusplus
extern "C" {
#endif

// 初始化数据库
int Db_Init(DbContext *ctx, const wchar_t *dbPath);

// 关闭数据库
int Db_Close(DbContext *ctx);

// 插入题目和答案
int Db_Insert(DbContext *ctx, const wchar_t *question, const wchar_t *answer);

// 搜索答案（通过题目）
wchar_t* Db_Search(DbContext *ctx, const wchar_t *question);

// 模糊搜索答案（先精确匹配，再子串匹配）
wchar_t* Db_SearchFuzzy(DbContext *ctx, const wchar_t *question);

// 删除记录（通过题目）
int Db_Delete(DbContext *ctx, const wchar_t *question);

// 获取所有记录（用于列表显示）
struct QAPair {
    wchar_t *question;
    wchar_t *answer;
};
struct QAPair* Db_GetAllPairs(DbContext *ctx, int *count);

// 释放查询结果
void Db_FreeResults(struct QAPair *pairs, int count);

// 从文本内容导入（格式：题目:题目内容 答案:答案内容）
// 题目可以包含空格；"答案:" 分隔符必须位于空白之后。
// 返回成功导入的条数，或负的错误码。
// skippedCount 可为 NULL；否则写回格式不正确而被跳过的非空行数。
int Db_ImportFromText(DbContext *ctx, const wchar_t *text, int *skippedCount);

// 将所有记录导出到文本（格式：题目:题目内容 答案:答案内容）
wchar_t* Db_ExportToText(DbContext *ctx);

// 检查数据库是否已初始化
BOOL Db_IsInitialized(DbContext *ctx);

#ifdef __cplusplus
}
#endif
