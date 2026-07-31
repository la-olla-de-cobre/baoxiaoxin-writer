#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "database.h"
#include "worker.h"

static int Fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
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

    longQuestion = (wchar_t *)malloc(6001 * sizeof(wchar_t));
    longAnswer = (wchar_t *)malloc(7001 * sizeof(wchar_t));
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
        free(answer);
        result = Fail("exact search did not return the long answer");
        goto cleanup;
    }
    free(answer);

    fuzzyQuestion = _wcsdup(longQuestion + 1000);
    if (!fuzzyQuestion) {
        result = Fail("could not allocate fuzzy search value");
        goto cleanup;
    }
    answer = Db_SearchFuzzy(&ctx, fuzzyQuestion);
    free(fuzzyQuestion);
    if (!answer || wcscmp(answer, longAnswer) != 0) {
        free(answer);
        result = Fail("fuzzy search did not return the long answer");
        goto cleanup;
    }
    free(answer);

    if (Db_ImportFromText(&ctx,
            L"题目:import-one 答案:answer-one\r\n"
            L"题目:import-two 答案:answer-two\r\n") != 2) {
        result = Fail("batch import did not report two successful writes");
        goto cleanup;
    }
    answer = Db_Search(&ctx, L"import-two");
    if (!answer || wcscmp(answer, L"answer-two") != 0) {
        free(answer);
        result = Fail("batch-imported record could not be read back");
        goto cleanup;
    }
    free(answer);

    pairs = Db_GetAllPairs(&ctx, &count);
    if (!pairs || count != 3) {
        Db_FreeResults(pairs, count);
        result = Fail("expected three readable database records");
        goto cleanup;
    }
    Db_FreeResults(pairs, count);

    if (Worker_Start(NULL) != NULL) {
        result = Fail("Worker_Start(NULL) unexpectedly started a worker");
        goto cleanup;
    }

    puts("PASS: database and worker regression tests");
    result = 0;

cleanup:
    free(longQuestion);
    free(longAnswer);
    Db_Close(&ctx);
    DeleteFileW(dbPath);
    return result;
}
