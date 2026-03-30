@echo off
echo ============================================
echo   Minesweeper Build
echo ============================================

set CC=gcc
set CFLAGS=-Wall -Wextra -O2
set UFLAGS=-DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0600
set LIBS=-lgdi32 -lwinmm -lcomdlg32 -lws2_32 -mwindows -municode

%CC% %CFLAGS% -c board.c -o board.o          && echo   [OK] board.c    || goto :err
%CC% %CFLAGS% -c solver.c -o solver.o        && echo   [OK] solver.c   || goto :err
%CC% %CFLAGS% -c game.c -o game.o            && echo   [OK] game.c     || goto :err
%CC% %CFLAGS% %UFLAGS% -c render.c -o render.o && echo   [OK] render.c  || goto :err
%CC% %CFLAGS% -c replay.c -o replay.o        && echo   [OK] replay.c   || goto :err
%CC% %CFLAGS% -c stats.c -o stats.o          && echo   [OK] stats.c    || goto :err
%CC% %CFLAGS% -c sound.c -o sound.o          && echo   [OK] sound.c    || goto :err
%CC% %CFLAGS% -c user.c -o user.o            && echo   [OK] user.c     || goto :err
%CC% %CFLAGS% %UFLAGS% -c net.c -o net.o     && echo   [OK] net.c      || goto :err
%CC% %CFLAGS% %UFLAGS% -c main.c -o main.o   && echo   [OK] main.c     || goto :err

echo   Linking...
%CC% -o minesweeper.exe board.o solver.o game.o render.o replay.o stats.o sound.o user.o net.o main.o %LIBS% || goto :err

del *.o 2>nul
echo.
echo   Build OK! Run minesweeper.exe
echo ============================================
goto :eof

:err
echo   BUILD FAILED!
pause
