@echo off
setlocal

set CC=gcc
set CFLAGS=-lws2_32
set CFLAGS_GAME_CLI=-lgdi32 -luser32 -lws2_32
set OUT_DIR=.

title Build Chat System

set BUILD_CHAT=0
set BUILD_GAME=0
set ARGS_PROVIDED=0

:parse_args
if "%~1"=="" goto :check_defaults
if /i "%~1"=="--chat" (
    set BUILD_CHAT=1
    set ARGS_PROVIDED=1
    shift
    goto :parse_args
)
if /i "%~1"=="--game" (
    set BUILD_GAME=1
    set ARGS_PROVIDED=1
    shift
    goto :parse_args
)

cls
echo ==========================================
echo [ERROR] Unknown parameter: %~1
echo Usage: %~n0 [--chat] [--game]
echo ==========================================
color 4F
goto :end

:check_defaults
if %ARGS_PROVIDED%==0 (
    set BUILD_CHAT=1
    set BUILD_GAME=1
)

cls
echo ==========================================
echo       BUILDING CHAT SYSTEM (WINDOWS)      
echo ==========================================
echo.

echo [CHAT] Compiling Server...

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
echo [CHAT] Compiling Client...

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
echo [GAME] Compiling Game Server...

%CC% game/game_server.c -o %OUT_DIR%/game_server.exe -lws2_32

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Game server compilation FAILED!
    color 4F
    goto :end
)
echo    - OK: game_server.exe created.

echo [GAME] Compiling Game Client (GDI)...

%CC% game/game_client_win.c -o %OUT_DIR%/game_client.exe %CFLAGS_GAME_CLI%

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Game client compilation FAILED!
    color 4F
    goto :end
)
echo    - OK: game_client.exe created.

echo.
echo ==========================================
echo           BUILD SUCCESSFUL!
echo ==========================================
color 2F

:end
echo.
pause
color 07
cls