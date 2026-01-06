@echo off
setlocal

set CC=gcc
set CFLAGS=-lws2_32
set OUT_DIR=.

title Build Chat System

cls
echo ==========================================
echo       BUILDING CHAT SYSTEM (WINDOWS)      
echo ==========================================
echo.

echo [1/2] Compiling Server...

%CC% server/server.c ^
     server/topic_svc.c ^
     server/history.c ^
     server/file_svc.c ^
     libs/common/net_utils.c ^
     libs/common/sqlite.c ^
     libs/common/sqlite3.c ^
     -o %OUT_DIR%/server_chat.exe ^
     %CFLAGS%

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Server compilation FAILED!
    color 4F
    goto :end
)
echo    - OK: server_chat.exe created.

echo.
echo [2/2] Compiling Client...

%CC% client/client.c ^
     libs/common/net_utils.c ^
     -o %OUT_DIR%/client_chat.exe ^
     %CFLAGS%

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Client compilation FAILED!
    color 4F
    goto :end
)
echo    - OK: client_chat.exe created.

echo.
echo ==========================================
echo           BUILD SUCCESSFUL!
echo ==========================================

:end
echo.
pause