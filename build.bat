@echo off
echo ============================================
echo   Minesweeper Build
echo ============================================

set CC=gcc
set CFLAGS=-Wall -Wextra -O2 -Iinclude
set UFLAGS=-DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0600
set LIBS=-lgdi32 -lwinmm -lcomdlg32 -lws2_32 -mwindows -municode
set OUT=minesweeper.exe

if not exist build mkdir build

%CC% %CFLAGS% -c src/board.c   -o build/board.o   && echo   [OK] board.c    || goto :err
%CC% %CFLAGS% -c src/solver.c  -o build/solver.o  && echo   [OK] solver.c   || goto :err
%CC% %CFLAGS% -c src/game.c    -o build/game.o    && echo   [OK] game.c     || goto :err
%CC% %CFLAGS% %UFLAGS% -c src/render.c -o build/render.o && echo   [OK] render.c  || goto :err
%CC% %CFLAGS% -c src/replay.c  -o build/replay.o  && echo   [OK] replay.c   || goto :err
%CC% %CFLAGS% -c src/stats.c   -o build/stats.o   && echo   [OK] stats.c    || goto :err
%CC% %CFLAGS% -c src/sound.c   -o build/sound.o   && echo   [OK] sound.c    || goto :err
%CC% %CFLAGS% -c src/user.c    -o build/user.o    && echo   [OK] user.c     || goto :err
%CC% %CFLAGS% %UFLAGS% -c src/net.c -o build/net.o && echo   [OK] net.c      || goto :err
%CC% %CFLAGS% %UFLAGS% -c src/main.c -o build/main.o && echo   [OK] main.c     || goto :err

echo   Linking...
%CC% -o %OUT% build/board.o build/solver.o build/game.o build/render.o build/replay.o build/stats.o build/sound.o build/user.o build/net.o build/main.o %LIBS% || goto :err

rd /s /q build 2>nul
echo.
echo   Build OK!  Run %OUT%
echo ============================================
goto :eof

:err
echo.
echo   BUILD FAILED!
pause
