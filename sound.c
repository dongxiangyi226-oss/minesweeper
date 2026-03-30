/*
 * sound.c -- Non-blocking sound effects using background threads
 *
 * All sounds are played asynchronously via CreateThread to avoid
 * blocking the UI thread (Beep() is synchronous and would cause lag).
 */

#include <windows.h>
#include "sound.h"

/* Musical note frequencies (Hz) */
#define NOTE_C5  523
#define NOTE_E5  659
#define NOTE_G5  784
#define NOTE_C6  1047

/* ------------------------------------------------------------------ */
/*  Thread functions for each sound                                   */
/* ------------------------------------------------------------------ */

static DWORD WINAPI thread_click(LPVOID p)
{
    (void)p;
    Beep(1200, 30);
    return 0;
}

static DWORD WINAPI thread_flag(LPVOID p)
{
    (void)p;
    Beep(800, 50);
    return 0;
}

static DWORD WINAPI thread_explode(LPVOID p)
{
    (void)p;
    Beep(300, 200);
    return 0;
}

static DWORD WINAPI thread_win(LPVOID p)
{
    (void)p;
    Beep(NOTE_C5, 100);
    Beep(NOTE_E5, 100);
    Beep(NOTE_G5, 100);
    Beep(NOTE_C6, 100);
    return 0;
}

static DWORD WINAPI thread_hint(LPVOID p)
{
    (void)p;
    Beep(1000, 50);
    Sleep(30);
    Beep(1000, 50);
    return 0;
}

/* Fire-and-forget: launch thread, immediately close handle */
static void play_async(LPTHREAD_START_ROUTINE func)
{
    HANDLE h = CreateThread(NULL, 0, func, NULL, 0, NULL);
    if (h) CloseHandle(h);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

void sound_init(void)
{
    /* Nothing to initialize */
}

void sound_click(void)   { play_async(thread_click);   }
void sound_flag(void)    { play_async(thread_flag);     }
void sound_explode(void) { play_async(thread_explode);  }
void sound_win(void)     { play_async(thread_win);      }
void sound_hint(void)    { play_async(thread_hint);     }
