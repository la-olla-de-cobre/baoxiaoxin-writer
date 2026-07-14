@echo off
setlocal

set CC=gcc
set CFLAGS=-Wall -O2 -DUNICODE -D_UNICODE -I src -I third_party\sqlite
set LDFLAGS=-mwindows -lcomctl32 -lcomdlg32 -luxtheme -lshell32 -luser32 -lgdi32 -lkernel32

echo [1/2] Compiling resources...
windres res/app.rc -O coff -o res/app.res
if %ERRORLEVEL% neq 0 ( echo windres failed & exit /b 1 )

echo [2/2] Compiling sources...
%CC% %CFLAGS% src/main.c src/ui.c src/worker.c src/config.c src/database.c src/qa_ui.c third_party\sqlite\sqlite3.c res/app.res ^
     -o KeyboardSim.exe %LDFLAGS%

if %ERRORLEVEL% equ 0 (
    echo.
    echo Build OK: KeyboardSim.exe
) else (
    echo.
    echo Build FAILED
    exit /b 1
)
