/*
 * main.c  --  WinMain entry point for Win32 GDI Minesweeper
 *
 * All strings are wide-character (L"...").
 * All Win32 APIs use W-suffix variants.
 * No .rc resource files; everything is created in code.
 *
 * Features: login/register, chord press visual feedback, online multiplayer.
 */

#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <winsock2.h>   /* MUST come before <windows.h> */
#include <windows.h>
#include <commdlg.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>

#include "resource.h"
#include "board.h"
#include "game.h"
#include "render.h"
#include "solver.h"
#include "replay.h"
#include "stats.h"
#include "sound.h"
#include "user.h"
#include "net.h"

/* ================================================================== */
/*  File-scope globals                                                 */
/* ================================================================== */

static Game     *g_game     = NULL;
static Renderer *g_renderer = NULL;
static Replay   *g_replay   = NULL;
static Stats    *g_stats    = NULL;
static double   *g_prob_map = NULL;
static int       g_prob_cap = 0;      /* allocated element count */
static HMENU     g_menu     = NULL;
static HWND      g_hwnd     = NULL;

/* Login / user system */
static UserDB      *g_userdb       = NULL;
static CurrentUser  g_current_user = { "Guest", 0 };

/* Networking */
static NetState *g_net = NULL;

/* Chord press (both-button) tracking */
static int g_lmb_down = 0;
static int g_rmb_down = 0;

static const wchar_t CLASS_NAME[] = L"MinesweeperClass";
static const char    USERDB_FILE[] = "minesweeper_users.dat";

/* ================================================================== */
/*  Forward declarations                                               */
/* ================================================================== */

static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static HMENU   create_menu(void);
static void    resize_window(HWND hwnd);
static void    ensure_prob_map(int w, int h);
static void    start_new_game(HWND hwnd, Difficulty diff);
static void    start_new_game_custom(HWND hwnd, int w, int h, int mines);
static void    update_difficulty_check(HMENU menu, Difficulty diff);
static void    show_custom_dialog(HWND hwnd);
static void    show_stats_dialog(HWND hwnd);
static void    show_about_dialog(HWND hwnd);
static void    handle_game_end(HWND hwnd);
static int     show_login_dialog(HINSTANCE hInst);
static void    show_ip_input_dialog(HWND hwnd, wchar_t *out_ip, int buflen);
static void    update_window_title(HWND hwnd);
static void    get_stats_filename(char *buf, int buflen);

/* ================================================================== */
/*  Per-user stats filename                                            */
/* ================================================================== */

static void get_stats_filename(char *buf, int buflen)
{
    if (g_current_user.logged_in && g_current_user.username[0]) {
        _snprintf(buf, buflen, "minesweeper_stats_%s.dat",
                  g_current_user.username);
    } else {
        _snprintf(buf, buflen, "minesweeper_stats.dat");
    }
    buf[buflen - 1] = '\0';
}

/* ================================================================== */
/*  Window title with username                                         */
/* ================================================================== */

static void update_window_title(HWND hwnd)
{
    wchar_t title[128];
    wchar_t wname[32];
    MultiByteToWideChar(CP_UTF8, 0, g_current_user.username, -1, wname, 32);
    /* 扫雷 - 玩家: username */
    _snwprintf(title, 128,
        L"\x626B\x96F7 - \x73A9\x5BB6: %ls", wname);
    title[127] = L'\0';
    if (hwnd) SetWindowTextW(hwnd, title);
}

/* ================================================================== */
/*  Login dialog                                                       */
/* ================================================================== */

#define IDC_LOGIN_LABEL_U   6001
#define IDC_LOGIN_EDIT_U    6002
#define IDC_LOGIN_LABEL_P   6003
#define IDC_LOGIN_EDIT_P    6004
#define IDC_LOGIN_BTN_LOGIN 6005
#define IDC_LOGIN_BTN_REG   6006
#define IDC_LOGIN_BTN_GUEST 6007

typedef struct {
    int result;  /* 0=cancelled, 1=logged in, 2=guest */
} LoginDlgData;

static LRESULT CALLBACK LoginDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    LoginDlgData *data;

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        data = (LoginDlgData *)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)data);

        /* 用户名 (Username) label + edit */
        CreateWindowExW(0, L"STATIC",
            L"\x7528\x6237\x540D:",  /* 用户名: */
            WS_CHILD | WS_VISIBLE,
            20, 20, 80, 22, hwnd, (HMENU)IDC_LOGIN_LABEL_U,
            NULL, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            110, 18, 160, 24, hwnd, (HMENU)IDC_LOGIN_EDIT_U,
            NULL, NULL);

        /* 密码 (Password) label + edit */
        CreateWindowExW(0, L"STATIC",
            L"\x5BC6\x7801:",  /* 密码: */
            WS_CHILD | WS_VISIBLE,
            20, 56, 80, 22, hwnd, (HMENU)IDC_LOGIN_LABEL_P,
            NULL, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_PASSWORD,
            110, 54, 160, 24, hwnd, (HMENU)IDC_LOGIN_EDIT_P,
            NULL, NULL);

        /* 登录 (Login) button */
        CreateWindowExW(0, L"BUTTON",
            L"\x767B\x5F55",  /* 登录 */
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            20, 100, 80, 30, hwnd, (HMENU)IDC_LOGIN_BTN_LOGIN,
            NULL, NULL);

        /* 注册 (Register) button */
        CreateWindowExW(0, L"BUTTON",
            L"\x6CE8\x518C",  /* 注册 */
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            110, 100, 80, 30, hwnd, (HMENU)IDC_LOGIN_BTN_REG,
            NULL, NULL);

        /* 游客 (Guest) button */
        CreateWindowExW(0, L"BUTTON",
            L"\x6E38\x5BA2",  /* 游客 */
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            200, 100, 80, 30, hwnd, (HMENU)IDC_LOGIN_BTN_GUEST,
            NULL, NULL);

        /* Set font for all children */
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND child = GetWindow(hwnd, GW_CHILD);
        while (child) {
            SendMessageW(child, WM_SETFONT, (WPARAM)hFont, TRUE);
            child = GetWindow(child, GW_HWNDNEXT);
        }

        return 0;
    }

    case WM_COMMAND:
        data = (LoginDlgData *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

        if (LOWORD(wp) == IDC_LOGIN_BTN_LOGIN) {
            wchar_t wuser[32], wpass[64];
            char user[32], pass[64];
            GetWindowTextW(GetDlgItem(hwnd, IDC_LOGIN_EDIT_U), wuser, 32);
            GetWindowTextW(GetDlgItem(hwnd, IDC_LOGIN_EDIT_P), wpass, 64);
            WideCharToMultiByte(CP_UTF8, 0, wuser, -1, user, 32, NULL, NULL);
            WideCharToMultiByte(CP_UTF8, 0, wpass, -1, pass, 64, NULL, NULL);

            if (user[0] == '\0') {
                MessageBoxW(hwnd,
                    L"\x8BF7\x8F93\x5165\x7528\x6237\x540D\xFF01",  /* 请输入用户名！ */
                    L"\x63D0\x793A", MB_OK | MB_ICONWARNING);        /* 提示 */
                return 0;
            }

            if (userdb_login(g_userdb, user, pass)) {
                strncpy(g_current_user.username, user, 31);
                g_current_user.username[31] = '\0';
                g_current_user.logged_in = 1;
                if (data) data->result = 1;
                DestroyWindow(hwnd);
            } else {
                MessageBoxW(hwnd,
                    L"\x7528\x6237\x540D\x6216\x5BC6\x7801\x9519\x8BEF\xFF01",  /* 用户名或密码错误！ */
                    L"\x767B\x5F55\x5931\x8D25", MB_OK | MB_ICONERROR);          /* 登录失败 */
            }
            return 0;
        }

        if (LOWORD(wp) == IDC_LOGIN_BTN_REG) {
            wchar_t wuser[32], wpass[64];
            char user[32], pass[64];
            GetWindowTextW(GetDlgItem(hwnd, IDC_LOGIN_EDIT_U), wuser, 32);
            GetWindowTextW(GetDlgItem(hwnd, IDC_LOGIN_EDIT_P), wpass, 64);
            WideCharToMultiByte(CP_UTF8, 0, wuser, -1, user, 32, NULL, NULL);
            WideCharToMultiByte(CP_UTF8, 0, wpass, -1, pass, 64, NULL, NULL);

            if (user[0] == '\0') {
                MessageBoxW(hwnd,
                    L"\x8BF7\x8F93\x5165\x7528\x6237\x540D\xFF01",
                    L"\x63D0\x793A", MB_OK | MB_ICONWARNING);
                return 0;
            }
            if (pass[0] == '\0') {
                MessageBoxW(hwnd,
                    L"\x8BF7\x8F93\x5165\x5BC6\x7801\xFF01",  /* 请输入密码！ */
                    L"\x63D0\x793A", MB_OK | MB_ICONWARNING);
                return 0;
            }

            if (userdb_register(g_userdb, user, pass)) {
                userdb_save(g_userdb, USERDB_FILE);
                /* Auto-login after successful registration */
                strncpy(g_current_user.username, user, 31);
                g_current_user.username[31] = '\0';
                g_current_user.logged_in = 1;
                MessageBoxW(hwnd,
                    L"\x6CE8\x518C\x6210\x529F\xFF0C\x5DF2\x81EA\x52A8\x767B\x5F55\xFF01",
                    /* 注册成功，已自动登录！ */
                    L"\x6210\x529F", MB_OK | MB_ICONINFORMATION);  /* 成功 */
                if (data) data->result = 1;
                DestroyWindow(hwnd);
            } else {
                MessageBoxW(hwnd,
                    L"\x6CE8\x518C\x5931\x8D25\xFF0C\x7528\x6237\x540D\x53EF\x80FD\x5DF2\x5B58\x5728\xFF01",
                    /* 注册失败，用户名可能已存在！ */
                    L"\x6CE8\x518C\x5931\x8D25", MB_OK | MB_ICONERROR);  /* 注册失败 */
            }
            return 0;
        }

        if (LOWORD(wp) == IDC_LOGIN_BTN_GUEST) {
            strncpy(g_current_user.username, "Guest", 31);
            g_current_user.logged_in = 0;
            if (data) data->result = 2;
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        data = (LoginDlgData *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (data) data->result = 0;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        /* Post WM_NULL to wake up the modal message loop so it can
           detect that the dialog has been destroyed via !IsWindow(). */
        PostMessageW(NULL, WM_NULL, 0, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int show_login_dialog(HINSTANCE hInst)
{
    /* Register window class for login dialog */
    static int registered = 0;
    if (!registered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = LoginDlgProc;
        wc.hInstance      = hInst;
        wc.hCursor        = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground  = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName  = L"MinesweeperLoginDlg";
        RegisterClassExW(&wc);
        registered = 1;
    }

    LoginDlgData data;
    data.result = 0;

    /* Center on screen */
    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);
    int dlg_w = 310, dlg_h = 185;
    int x = (sx - dlg_w) / 2;
    int y = (sy - dlg_h) / 2;

    HWND hdlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"MinesweeperLoginDlg",
        L"\x626B\x96F7 - \x767B\x5F55",  /* 扫雷 - 登录 */
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, dlg_w, dlg_h,
        NULL, NULL, hInst, &data);

    if (!hdlg) return 0;

    ShowWindow(hdlg, SW_SHOW);
    UpdateWindow(hdlg);

    /* Run modal message loop */
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsWindow(hdlg)) break;
        if (IsDialogMessageW(hdlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return data.result;
}

/* ================================================================== */
/*  IP input dialog                                                    */
/* ================================================================== */

#define IDC_IP_LABEL    6101
#define IDC_IP_EDIT     6102
#define IDC_IP_OK       6103
#define IDC_IP_CANCEL   6104

typedef struct {
    wchar_t ip[64];
    int accepted;
} IpDlgData;

static LRESULT CALLBACK IpDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    IpDlgData *data;

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        data = (IpDlgData *)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)data);

        /* IP地址 label + edit */
        CreateWindowExW(0, L"STATIC",
            L"IP\x5730\x5740:",  /* IP地址: */
            WS_CHILD | WS_VISIBLE,
            20, 20, 80, 22, hwnd, (HMENU)IDC_IP_LABEL,
            NULL, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"127.0.0.1",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            100, 18, 160, 24, hwnd, (HMENU)IDC_IP_EDIT,
            NULL, NULL);

        /* 确定 (OK) */
        CreateWindowExW(0, L"BUTTON",
            L"\x786E\x5B9A",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            40, 60, 80, 30, hwnd, (HMENU)IDC_IP_OK,
            NULL, NULL);
        /* 取消 (Cancel) */
        CreateWindowExW(0, L"BUTTON",
            L"\x53D6\x6D88",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            160, 60, 80, 30, hwnd, (HMENU)IDC_IP_CANCEL,
            NULL, NULL);

        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND child = GetWindow(hwnd, GW_CHILD);
        while (child) {
            SendMessageW(child, WM_SETFONT, (WPARAM)hFont, TRUE);
            child = GetWindow(child, GW_HWNDNEXT);
        }
        return 0;
    }

    case WM_COMMAND:
        data = (IpDlgData *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (LOWORD(wp) == IDC_IP_OK) {
            if (data) {
                GetWindowTextW(GetDlgItem(hwnd, IDC_IP_EDIT), data->ip, 64);
                data->accepted = 1;
            }
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wp) == IDC_IP_CANCEL) {
            if (data) data->accepted = 0;
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        data = (IpDlgData *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (data) data->accepted = 0;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostMessageW(NULL, WM_NULL, 0, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void show_ip_input_dialog(HWND parent, wchar_t *out_ip, int buflen)
{
    static int registered = 0;
    if (!registered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = IpDlgProc;
        wc.hInstance      = GetModuleHandleW(NULL);
        wc.hCursor        = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground  = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName  = L"MinesweeperIpDlg";
        RegisterClassExW(&wc);
        registered = 1;
    }

    IpDlgData data;
    data.ip[0] = L'\0';
    data.accepted = 0;

    RECT pr;
    GetWindowRect(parent, &pr);
    int dlg_w = 300, dlg_h = 140;
    int x = pr.left + ((pr.right - pr.left) - dlg_w) / 2;
    int y = pr.top  + ((pr.bottom - pr.top) - dlg_h) / 2;

    HWND hdlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"MinesweeperIpDlg",
        L"\x52A0\x5165\x623F\x95F4",  /* 加入房间 */
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, dlg_w, dlg_h,
        parent, NULL, GetModuleHandleW(NULL), &data);

    if (!hdlg) { out_ip[0] = L'\0'; return; }

    ShowWindow(hdlg, SW_SHOW);
    UpdateWindow(hdlg);
    EnableWindow(parent, FALSE);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsWindow(hdlg)) break;
        if (IsDialogMessageW(hdlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);

    if (data.accepted && data.ip[0]) {
        wcsncpy(out_ip, data.ip, buflen - 1);
        out_ip[buflen - 1] = L'\0';
    } else {
        out_ip[0] = L'\0';
    }
}

/* ================================================================== */
/*  Custom difficulty dialog                                           */
/* ================================================================== */

/* Child control IDs for the custom dialog */
#define IDC_CD_LABEL_W    7001
#define IDC_CD_EDIT_W     7002
#define IDC_CD_LABEL_H    7003
#define IDC_CD_EDIT_H     7004
#define IDC_CD_LABEL_M    7005
#define IDC_CD_EDIT_M     7006
#define IDC_CD_OK         7007
#define IDC_CD_CANCEL     7008

typedef struct {
    int width;
    int height;
    int mines;
    int accepted;
} CustomDlgData;

static LRESULT CALLBACK CustomDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    CustomDlgData *data;

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        data = (CustomDlgData *)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)data);

        /* Width label + edit */
        CreateWindowExW(0, L"STATIC", L"\x5BBD\x5EA6 (Width):",
                        WS_CHILD | WS_VISIBLE,
                        20, 20, 120, 22, hwnd, (HMENU)IDC_CD_LABEL_W,
                        NULL, NULL);
        HWND ew = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"9",
                        WS_CHILD | WS_VISIBLE | ES_NUMBER,
                        150, 18, 60, 24, hwnd, (HMENU)IDC_CD_EDIT_W,
                        NULL, NULL);

        /* Height label + edit */
        CreateWindowExW(0, L"STATIC", L"\x9AD8\x5EA6 (Height):",
                        WS_CHILD | WS_VISIBLE,
                        20, 56, 120, 22, hwnd, (HMENU)IDC_CD_LABEL_H,
                        NULL, NULL);
        HWND eh = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"9",
                        WS_CHILD | WS_VISIBLE | ES_NUMBER,
                        150, 54, 60, 24, hwnd, (HMENU)IDC_CD_EDIT_H,
                        NULL, NULL);

        /* Mines label + edit */
        CreateWindowExW(0, L"STATIC", L"\x96F7\x6570 (Mines):",
                        WS_CHILD | WS_VISIBLE,
                        20, 92, 120, 22, hwnd, (HMENU)IDC_CD_LABEL_M,
                        NULL, NULL);
        HWND em = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"10",
                        WS_CHILD | WS_VISIBLE | ES_NUMBER,
                        150, 90, 60, 24, hwnd, (HMENU)IDC_CD_EDIT_M,
                        NULL, NULL);

        /* Pre-fill with current custom values */
        if (data) {
            wchar_t buf[16];
            _snwprintf(buf, 16, L"%d", data->width);
            SetWindowTextW(ew, buf);
            _snwprintf(buf, 16, L"%d", data->height);
            SetWindowTextW(eh, buf);
            _snwprintf(buf, 16, L"%d", data->mines);
            SetWindowTextW(em, buf);
        }

        /* OK and Cancel buttons */
        CreateWindowExW(0, L"BUTTON", L"\x786E\x5B9A",
                        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                        40, 130, 80, 30, hwnd, (HMENU)IDC_CD_OK,
                        NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"\x53D6\x6D88",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        140, 130, 80, 30, hwnd, (HMENU)IDC_CD_CANCEL,
                        NULL, NULL);

        /* Set font for all children */
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND child = GetWindow(hwnd, GW_CHILD);
        while (child) {
            SendMessageW(child, WM_SETFONT, (WPARAM)hFont, TRUE);
            child = GetWindow(child, GW_HWNDNEXT);
        }

        return 0;
    }
    case WM_COMMAND:
        data = (CustomDlgData *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (LOWORD(wp) == IDC_CD_OK) {
            wchar_t buf[16];
            GetWindowTextW(GetDlgItem(hwnd, IDC_CD_EDIT_W), buf, 16);
            int w = _wtoi(buf);
            GetWindowTextW(GetDlgItem(hwnd, IDC_CD_EDIT_H), buf, 16);
            int h = _wtoi(buf);
            GetWindowTextW(GetDlgItem(hwnd, IDC_CD_EDIT_M), buf, 16);
            int m = _wtoi(buf);

            /* Clamp values to sensible ranges */
            if (w < 5)   w = 5;
            if (w > 50)  w = 50;
            if (h < 5)   h = 5;
            if (h > 50)  h = 50;
            if (m < 1)   m = 1;
            if (m > (w * h - 9))  m = w * h - 9;
            if (m < 1)   m = 1;

            if (data) {
                data->width    = w;
                data->height   = h;
                data->mines    = m;
                data->accepted = 1;
            }
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wp) == IDC_CD_CANCEL) {
            if (data) data->accepted = 0;
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        data = (CustomDlgData *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (data) data->accepted = 0;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        /* Post WM_NULL to wake up the modal message loop */
        PostMessageW(NULL, WM_NULL, 0, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void show_custom_dialog(HWND parent)
{
    /* Register a separate window class for the custom dialog */
    static int registered = 0;
    if (!registered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = CustomDlgProc;
        wc.hInstance      = GetModuleHandleW(NULL);
        wc.hCursor        = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground  = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName  = L"MinesweeperCustomDlg";
        RegisterClassExW(&wc);
        registered = 1;
    }

    CustomDlgData data;
    data.width    = g_game->custom_w > 0 ? g_game->custom_w : 9;
    data.height   = g_game->custom_h > 0 ? g_game->custom_h : 9;
    data.mines    = g_game->custom_mines > 0 ? g_game->custom_mines : 10;
    data.accepted = 0;

    /* Center on parent */
    RECT pr;
    GetWindowRect(parent, &pr);
    int dlg_w = 260, dlg_h = 210;
    int x = pr.left + ((pr.right - pr.left) - dlg_w) / 2;
    int y = pr.top  + ((pr.bottom - pr.top) - dlg_h) / 2;

    HWND hdlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"MinesweeperCustomDlg",
        L"\x81EA\x5B9A\x4E49\x96BE\x5EA6 - Custom",  /* 自定义难度 */
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, dlg_w, dlg_h,
        parent, NULL, GetModuleHandleW(NULL), &data);

    if (!hdlg) return;

    ShowWindow(hdlg, SW_SHOW);
    UpdateWindow(hdlg);

    /* Disable parent to simulate modal */
    EnableWindow(parent, FALSE);

    /* Run a local message loop for the dialog */
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        /* Check if dialog was destroyed */
        if (!IsWindow(hdlg)) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);

    if (data.accepted) {
        start_new_game_custom(parent, data.width, data.height, data.mines);
    }
}

/* ================================================================== */
/*  Menu creation                                                      */
/* ================================================================== */

static HMENU create_menu(void)
{
    HMENU bar   = CreateMenu();
    HMENU game  = CreatePopupMenu();
    HMENU func  = CreatePopupMenu();
    HMENU net   = CreatePopupMenu();
    HMENU help  = CreatePopupMenu();

    /* --- Game menu (游戏) --- */
    AppendMenuW(game, MF_STRING,    IDM_NEW_GAME,     L"\x65B0\x6E38\x620F(&N)\tN");  /* 新游戏 */
    AppendMenuW(game, MF_SEPARATOR, 0, NULL);
    AppendMenuW(game, MF_STRING,    IDM_BEGINNER,     L"\x521D\x7EA7(&B)");            /* 初级 */
    AppendMenuW(game, MF_STRING,    IDM_INTERMEDIATE,  L"\x4E2D\x7EA7(&I)");           /* 中级 */
    AppendMenuW(game, MF_STRING,    IDM_EXPERT,        L"\x9AD8\x7EA7(&E)");           /* 高级 */
    AppendMenuW(game, MF_STRING,    IDM_CUSTOM,        L"\x81EA\x5B9A\x4E49(&C)...");  /* 自定义 */
    AppendMenuW(game, MF_SEPARATOR, 0, NULL);
    AppendMenuW(game, MF_STRING,    IDM_EXIT,          L"\x9000\x51FA(&X)");            /* 退出 */

    /* Mark beginner as default checked */
    CheckMenuRadioItem(game, IDM_BEGINNER, IDM_CUSTOM,
                       IDM_BEGINNER, MF_BYCOMMAND);

    /* --- Function menu (功能) --- */
    AppendMenuW(func, MF_STRING,    IDM_NO_GUESS,      L"\x65E0\x731C\x6D4B\x6A21\x5F0F(&G)");  /* 无猜测模式 */
    AppendMenuW(func, MF_SEPARATOR, 0, NULL);
    AppendMenuW(func, MF_STRING,    IDM_HINT,           L"\x63D0\x793A(&H)\tH");       /* 提示 */
    AppendMenuW(func, MF_STRING,    IDM_HEATMAP,        L"\x70ED\x529B\x56FE(&P)\tP"); /* 热力图 */
    AppendMenuW(func, MF_STRING,    IDM_FOG,            L"\x6218\x4E89\x8FF7\x96FE(&F)\tF"); /* 战争迷雾 */
    AppendMenuW(func, MF_SEPARATOR, 0, NULL);
    AppendMenuW(func, MF_STRING,    IDM_REPLAY_SAVE,    L"\x4FDD\x5B58\x56DE\x653E(&S)");  /* 保存回放 */
    AppendMenuW(func, MF_STRING,    IDM_REPLAY_LOAD,    L"\x52A0\x8F7D\x56DE\x653E(&L)");  /* 加载回放 */

    /* No-guess mode defaults to checked (game_create sets it to 1) */
    CheckMenuItem(func, IDM_NO_GUESS, MF_BYCOMMAND | MF_CHECKED);

    /* --- Network menu (联机) --- */
    AppendMenuW(net, MF_STRING,     IDM_NET_HOST,
        L"\x521B\x5EFA\x623F\x95F4(&H)");       /* 创建房间 */
    AppendMenuW(net, MF_STRING,     IDM_NET_JOIN,
        L"\x52A0\x5165\x623F\x95F4(&J)");       /* 加入房间 */
    AppendMenuW(net, MF_SEPARATOR,  0, NULL);
    AppendMenuW(net, MF_STRING,     IDM_NET_DISCONNECT,
        L"\x65AD\x5F00\x8FDE\x63A5(&D)");       /* 断开连接 */

    /* Disconnect disabled by default */
    EnableMenuItem(net, IDM_NET_DISCONNECT, MF_BYCOMMAND | MF_GRAYED);

    /* --- Help menu (帮助) --- */
    AppendMenuW(help, MF_STRING,    IDM_STATS,          L"\x7EDF\x8BA1(&S)");          /* 统计 */
    AppendMenuW(help, MF_STRING,    IDM_ABOUT,          L"\x5173\x4E8E(&A)");          /* 关于 */

    /* --- Assemble menu bar --- */
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)game, L"\x6E38\x620F(&G)");  /* 游戏 */
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)func, L"\x529F\x80FD(&F)");  /* 功能 */
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)net,  L"\x8054\x673A(&M)");  /* 联机 */
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"\x5E2E\x52A9(&H)");  /* 帮助 */

    return bar;
}

/* ================================================================== */
/*  Probability map allocation                                         */
/* ================================================================== */

static void ensure_prob_map(int w, int h)
{
    int need = w * h;
    if (need > g_prob_cap) {
        free(g_prob_map);
        g_prob_map = (double *)calloc(need, sizeof(double));
        g_prob_cap = need;
    }
}

/* ================================================================== */
/*  New game helpers                                                   */
/* ================================================================== */

static void start_new_game(HWND hwnd, Difficulty diff)
{
    game_new(g_game, diff);

    Board *b = g_game->board;
    ensure_prob_map(b->width, b->height);
    replay_reset(g_replay, b->width, b->height, b->mine_count);

    if (g_renderer) {
        int cw, ch;
        render_calc_window_size(b->width, b->height, &cw, &ch);
        render_resize(g_renderer, hwnd, cw, ch);
    }

    /* Reset chord press state */
    g_lmb_down = 0;
    g_rmb_down = 0;
    g_game->press_active = 0;
    g_game->press_cx = -1;
    g_game->press_cy = -1;

    resize_window(hwnd);

    /* Send board to peer if hosting and connected */
    if (g_net && g_net->role == NET_SERVER && g_net->connected) {
        net_send_board(g_net, b);
    }

    InvalidateRect(hwnd, NULL, FALSE);
}

static void start_new_game_custom(HWND hwnd, int w, int h, int mines)
{
    game_new_custom(g_game, w, h, mines);

    Board *b = g_game->board;
    ensure_prob_map(b->width, b->height);
    replay_reset(g_replay, b->width, b->height, b->mine_count);

    if (g_renderer) {
        int cw, ch;
        render_calc_window_size(b->width, b->height, &cw, &ch);
        render_resize(g_renderer, hwnd, cw, ch);
    }

    /* Reset chord press state */
    g_lmb_down = 0;
    g_rmb_down = 0;
    g_game->press_active = 0;
    g_game->press_cx = -1;
    g_game->press_cy = -1;

    resize_window(hwnd);
    update_difficulty_check(g_menu, DIFF_CUSTOM);

    /* Send board to peer if hosting and connected */
    if (g_net && g_net->role == NET_SERVER && g_net->connected) {
        net_send_board(g_net, b);
    }

    InvalidateRect(hwnd, NULL, FALSE);
}

/* ================================================================== */
/*  Resize window to fit board                                         */
/* ================================================================== */

static void resize_window(HWND hwnd)
{
    Board *b = g_game->board;
    if (!b) return;

    int client_w, client_h;
    render_calc_window_size(b->width, b->height, &client_w, &client_h);

    RECT rc = { 0, 0, client_w, client_h };
    DWORD style   = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
    DWORD exstyle = (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    AdjustWindowRectEx(&rc, style, TRUE, exstyle);  /* TRUE = has menu */

    int win_w = rc.right  - rc.left;
    int win_h = rc.bottom - rc.top;

    /* Center on screen */
    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);
    int x  = (sx - win_w) / 2;
    int y  = (sy - win_h) / 2;

    SetWindowPos(hwnd, NULL, x, y, win_w, win_h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

/* ================================================================== */
/*  Menu check helpers                                                 */
/* ================================================================== */

static void update_difficulty_check(HMENU menu, Difficulty diff)
{
    HMENU game_menu = GetSubMenu(menu, 0);  /* first popup = Game */
    UINT id;
    switch (diff) {
    case DIFF_BEGINNER:     id = IDM_BEGINNER;     break;
    case DIFF_INTERMEDIATE: id = IDM_INTERMEDIATE;  break;
    case DIFF_EXPERT:       id = IDM_EXPERT;        break;
    default:                id = IDM_CUSTOM;        break;
    }
    CheckMenuRadioItem(game_menu, IDM_BEGINNER, IDM_CUSTOM,
                       id, MF_BYCOMMAND);
}

/* ================================================================== */
/*  Game end handling                                                   */
/* ================================================================== */

static void handle_game_end(HWND hwnd)
{
    Board *b = g_game->board;
    if (!b) return;

    char stats_file[128];
    get_stats_filename(stats_file, sizeof(stats_file));

    if (b->state == STATE_WON) {
        sound_win();
        Difficulty d = g_game->difficulty;
        int t = g_game->elapsed_seconds;

        stats_record_game(g_stats, d, 1, t);

        /* Send game end to peer */
        if (g_net && g_net->connected) {
            net_send_game_end(g_net, 1);
            net_send_state(g_net, b->revealed_count, b->flagged_count, STATE_WON);
        }

        /* Check leaderboard (only for standard difficulties) */
        if (d != DIFF_CUSTOM) {
            int rank = stats_check_leaderboard(g_stats, d, t);
            if (rank >= 0) {
                wchar_t msg[256];
                _snwprintf(msg, 256,
                    L"\x606D\x559C\x4F60\x8D62\x4E86\xFF01\n"   /* 恭喜你赢了！ */
                    L"\x7528\x65F6: %d \x79D2\n"                 /* 用时: X 秒 */
                    L"\x8FDB\x5165\x6392\x884C\x699C "           /* 进入排行榜 */
                    L"\x7B2C %d \x540D\xFF01",                    /* 第 X 名！ */
                    t, rank + 1);
                MessageBoxW(hwnd, msg,
                    L"\x80DC\x5229",  /* 胜利 */
                    MB_OK | MB_ICONINFORMATION);
                stats_add_leaderboard(g_stats, d,
                    g_current_user.username, t);
            }
        }

        stats_save(g_stats, stats_file);
    }
    else if (b->state == STATE_LOST) {
        sound_explode();
        stats_record_game(g_stats, g_game->difficulty, 0,
                          g_game->elapsed_seconds);
        stats_save(g_stats, stats_file);

        /* Send game end to peer */
        if (g_net && g_net->connected) {
            net_send_game_end(g_net, 0);
            net_send_state(g_net, b->revealed_count, b->flagged_count, STATE_LOST);
        }
    }

    InvalidateRect(hwnd, NULL, FALSE);
}

/* ================================================================== */
/*  Stats dialog                                                       */
/* ================================================================== */

static void show_stats_dialog(HWND hwnd)
{
    static const wchar_t *diff_names[] = {
        L"\x521D\x7EA7",    /* 初级 */
        L"\x4E2D\x7EA7",    /* 中级 */
        L"\x9AD8\x7EA7"     /* 高级 */
    };
    wchar_t buf[1024];
    int off = 0;

    for (int i = 0; i < 3; i++) {
        int played = g_stats->games_played[i];
        int won    = g_stats->games_won[i];
        int pct    = played > 0 ? (won * 100 / played) : 0;
        int best   = g_stats->best_time[i];

        wchar_t best_str[32];
        if (best > 0 && best < 9999)
            _snwprintf(best_str, 32, L"%d \x79D2", best);  /* X 秒 */
        else
            _snwprintf(best_str, 32, L"--");

        off += _snwprintf(buf + off, 1024 - off,
            L"=== %ls ===\n"
            L"\x5DF2\x73A9: %d    "      /* 已玩 */
            L"\x80DC\x5229: %d    "       /* 胜利 */
            L"\x80DC\x7387: %d%%\n"       /* 胜率 */
            L"\x6700\x4F73: %ls\n\n",     /* 最佳 */
            diff_names[i],
            played, won, pct,
            best_str);
    }

    MessageBoxW(hwnd, buf,
        L"\x6E38\x620F\x7EDF\x8BA1",  /* 游戏统计 */
        MB_OK | MB_ICONINFORMATION);
}

/* ================================================================== */
/*  About dialog                                                       */
/* ================================================================== */

static void show_about_dialog(HWND hwnd)
{
    MessageBoxW(hwnd,
        L"\x626B\x96F7 - Minesweeper\n\n"                 /* 扫雷 */
        L"Win32 GDI \x7248\x672C\n\n"                     /* 版本 */
        L"\x529F\x80FD:\n"                                  /* 功能: */
        L"  \x2022 \x65E0\x731C\x6D4B\x6A21\x5F0F\n"     /* 无猜测模式 */
        L"  \x2022 \x6982\x7387\x70ED\x529B\x56FE\n"     /* 概率热力图 */
        L"  \x2022 \x667A\x80FD\x63D0\x793A\n"            /* 智能提示 */
        L"  \x2022 \x6218\x4E89\x8FF7\x96FE\n"            /* 战争迷雾 */
        L"  \x2022 \x6E38\x620F\x56DE\x653E\n"            /* 游戏回放 */
        L"  \x2022 \x6392\x884C\x699C\x4E0E\x7EDF\x8BA1\n"  /* 排行榜与统计 */
        L"  \x2022 \x7528\x6237\x767B\x5F55/\x6CE8\x518C\n"  /* 用户登录/注册 */
        L"  \x2022 \x5728\x7EBF\x8054\x673A\x5BF9\x6218\n\n"  /* 在线联机对战 */
        L"\x5FEB\x6377\x952E:\n"                            /* 快捷键: */
        L"  N - \x65B0\x6E38\x620F\n"                      /* 新游戏 */
        L"  H - \x63D0\x793A\n"                             /* 提示 */
        L"  P - \x70ED\x529B\x56FE\n"                      /* 热力图 */
        L"  F - \x6218\x4E89\x8FF7\x96FE\n",               /* 战争迷雾 */
        L"\x5173\x4E8E\x626B\x96F7",  /* 关于扫雷 */
        MB_OK | MB_ICONINFORMATION);
}

/* ================================================================== */
/*  Replay save / load                                                 */
/* ================================================================== */

static void do_replay_save(HWND hwnd)
{
    OPENFILENAMEW ofn = {0};
    wchar_t path[MAX_PATH] = L"replay.msrep";

    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = hwnd;
    ofn.lpstrFilter  = L"\x56DE\x653E\x6587\x4EF6 (*.msrep)\0*.msrep\0"  /* 回放文件 */
                       L"\x6240\x6709\x6587\x4EF6 (*.*)\0*.*\0";          /* 所有文件 */
    ofn.lpstrFile    = path;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrTitle   = L"\x4FDD\x5B58\x56DE\x653E";  /* 保存回放 */
    ofn.Flags        = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt  = L"msrep";

    if (GetSaveFileNameW(&ofn)) {
        /* Convert wide path to narrow for replay_save */
        char narrow[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, MAX_PATH, NULL, NULL);
        if (!replay_save(g_replay, narrow)) {
            MessageBoxW(hwnd,
                L"\x4FDD\x5B58\x5931\x8D25\xFF01",  /* 保存失败！ */
                L"\x9519\x8BEF",                      /* 错误 */
                MB_OK | MB_ICONERROR);
        }
    }
}

static void do_replay_load(HWND hwnd)
{
    OPENFILENAMEW ofn = {0};
    wchar_t path[MAX_PATH] = L"";

    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = hwnd;
    ofn.lpstrFilter  = L"\x56DE\x653E\x6587\x4EF6 (*.msrep)\0*.msrep\0"
                       L"\x6240\x6709\x6587\x4EF6 (*.*)\0*.*\0";
    ofn.lpstrFile    = path;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrTitle   = L"\x52A0\x8F7D\x56DE\x653E";  /* 加载回放 */
    ofn.Flags        = OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        char narrow[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, MAX_PATH, NULL, NULL);
        Replay *rp = replay_load(narrow);
        if (rp) {
            replay_destroy(g_replay);
            g_replay = rp;
            MessageBoxW(hwnd,
                L"\x56DE\x653E\x52A0\x8F7D\x6210\x529F\xFF01",  /* 回放加载成功！ */
                L"\x56DE\x653E",  /* 回放 */
                MB_OK | MB_ICONINFORMATION);
        } else {
            MessageBoxW(hwnd,
                L"\x52A0\x8F7D\x5931\x8D25\xFF01",  /* 加载失败！ */
                L"\x9519\x8BEF",                      /* 错误 */
                MB_OK | MB_ICONERROR);
        }
    }
}

/* ================================================================== */
/*  Network helper functions                                           */
/* ================================================================== */

static void net_enable_menu_items(int connected)
{
    /* 联机 menu is the 3rd popup (index 2) */
    HMENU net_menu = GetSubMenu(g_menu, 2);
    if (!net_menu) return;

    if (connected) {
        EnableMenuItem(net_menu, IDM_NET_HOST, MF_BYCOMMAND | MF_GRAYED);
        EnableMenuItem(net_menu, IDM_NET_JOIN, MF_BYCOMMAND | MF_GRAYED);
        EnableMenuItem(net_menu, IDM_NET_DISCONNECT, MF_BYCOMMAND | MF_ENABLED);
    } else {
        EnableMenuItem(net_menu, IDM_NET_HOST, MF_BYCOMMAND | MF_ENABLED);
        EnableMenuItem(net_menu, IDM_NET_JOIN, MF_BYCOMMAND | MF_ENABLED);
        EnableMenuItem(net_menu, IDM_NET_DISCONNECT, MF_BYCOMMAND | MF_GRAYED);
    }
}

static void do_net_host(HWND hwnd)
{
    if (!g_net) return;

    /* Reset any existing connection */
    net_reset(g_net);
    g_net->role = NET_SERVER;

    if (!net_host_start(g_net)) {
        MessageBoxW(hwnd,
            L"\x521B\x5EFA\x623F\x95F4\x5931\x8D25\xFF01",  /* 创建房间失败！ */
            L"\x9519\x8BEF", MB_OK | MB_ICONERROR);
        return;
    }

    /* Get local IP to display */
    char ip_buf[64];
    net_get_local_ip(ip_buf, sizeof(ip_buf));

    wchar_t wip[64];
    MultiByteToWideChar(CP_UTF8, 0, ip_buf, -1, wip, 64);

    wchar_t msg_buf[256];
    /* 房间已创建\nIP: xxx\n等待对手连接... */
    _snwprintf(msg_buf, 256,
        L"\x623F\x95F4\x5DF2\x521B\x5EFA\nIP: %ls\n"
        L"\x7B49\x5F85\x5BF9\x624B\x8FDE\x63A5...",
        wip);

    MessageBoxW(hwnd, msg_buf,
        L"\x8054\x673A",  /* 联机 */
        MB_OK | MB_ICONINFORMATION);

    /* Start accept polling timer */
    SetTimer(hwnd, TIMER_NET_ACCEPT, 100, NULL);
    net_enable_menu_items(1);
}

static void do_net_join(HWND hwnd)
{
    if (!g_net) return;

    /* Show IP input dialog */
    wchar_t wip[64];
    show_ip_input_dialog(hwnd, wip, 64);
    if (wip[0] == L'\0') return;  /* cancelled */

    /* Convert to narrow string for net_connect */
    char ip[64];
    WideCharToMultiByte(CP_UTF8, 0, wip, -1, ip, 64, NULL, NULL);

    net_reset(g_net);
    g_net->role = NET_CLIENT;

    if (!net_connect(g_net, ip)) {
        MessageBoxW(hwnd,
            L"\x8FDE\x63A5\x5931\x8D25\xFF01",  /* 连接失败！ */
            L"\x9519\x8BEF", MB_OK | MB_ICONERROR);
        return;
    }

    MessageBoxW(hwnd,
        L"\x5DF2\x8FDE\x63A5\x5230\x4E3B\x673A\xFF01\n"  /* 已连接到主机！ */
        L"\x7B49\x5F85\x5BF9\x65B9\x5F00\x59CB\x6E38\x620F...",  /* 等待对方开始游戏... */
        L"\x8054\x673A",  /* 联机 */
        MB_OK | MB_ICONINFORMATION);

    /* Try to receive board from host */
    Board *b = g_game->board;
    if (net_recv_board(g_net, b)) {
        ensure_prob_map(b->width, b->height);
        replay_reset(g_replay, b->width, b->height, b->mine_count);
        if (g_renderer) {
            int cw, ch;
            render_calc_window_size(b->width, b->height, &cw, &ch);
            render_resize(g_renderer, hwnd, cw, ch);
        }
        resize_window(hwnd);
    }

    /* Start network polling timer */
    SetTimer(hwnd, TIMER_NET_POLL, 50, NULL);
    net_enable_menu_items(1);
    InvalidateRect(hwnd, NULL, FALSE);
}

static void do_net_disconnect(HWND hwnd)
{
    if (!g_net) return;

    KillTimer(hwnd, TIMER_NET_POLL);
    KillTimer(hwnd, TIMER_NET_ACCEPT);
    net_disconnect(g_net);
    net_reset(g_net);
    net_enable_menu_items(0);

    MessageBoxW(hwnd,
        L"\x5DF2\x65AD\x5F00\x8FDE\x63A5",  /* 已断开连接 */
        L"\x8054\x673A",  /* 联机 */
        MB_OK | MB_ICONINFORMATION);
}

/* ================================================================== */
/*  Chord press helper: enter/update/exit chord mode                   */
/* ================================================================== */

static void chord_enter(HWND hwnd, int px, int py)
{
    Board *b = g_game->board;
    if (!b || b->state != STATE_PLAYING) return;

    int cx, cy;
    if (render_pixel_to_cell(b->width, b->height, px, py, &cx, &cy)) {
        g_game->press_active = 1;
        g_game->press_cx = cx;
        g_game->press_cy = cy;
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

static void chord_update(HWND hwnd, int px, int py)
{
    Board *b = g_game->board;
    if (!b) return;

    int cx, cy;
    if (render_pixel_to_cell(b->width, b->height, px, py, &cx, &cy)) {
        if (cx != g_game->press_cx || cy != g_game->press_cy) {
            g_game->press_cx = cx;
            g_game->press_cy = cy;
            InvalidateRect(hwnd, NULL, FALSE);
        }
    }
}

static void chord_release(HWND hwnd)
{
    Board *b = g_game->board;
    if (!b) return;

    if (g_game->press_active) {
        int cx = g_game->press_cx;
        int cy = g_game->press_cy;
        g_game->press_active = 0;
        g_game->press_cx = -1;
        g_game->press_cy = -1;

        if (cx >= 0 && cy >= 0) {
            GameState before = b->state;
            game_chord_click(g_game, cx, cy);
            replay_record(g_replay, cx, cy, ACTION_CHORD);

            /* Send move to peer */
            if (g_net && g_net->connected) {
                net_send_move(g_net, cx, cy, ACTION_CHORD);
                net_send_state(g_net, b->revealed_count,
                               b->flagged_count, b->state);
            }

            if (b->state != before &&
                (b->state == STATE_WON || b->state == STATE_LOST)) {
                handle_game_end(hwnd);
            }
        }

        InvalidateRect(hwnd, NULL, FALSE);
    }

    g_lmb_down = 0;
    g_rmb_down = 0;
}

/* ================================================================== */
/*  Window procedure                                                   */
/* ================================================================== */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {

    /* ---- Window creation ---- */
    case WM_CREATE: {
        Board *b = g_game->board;
        int cw, ch;
        render_calc_window_size(b->width, b->height, &cw, &ch);
        g_renderer = render_create(hwnd, cw, ch);
        ensure_prob_map(b->width, b->height);
        SetTimer(hwnd, TIMER_GAME, 1000, NULL);
        return 0;
    }

    /* ---- Paint ---- */
    case WM_PAINT: {
        /* Compute probabilities if heatmap is on */
        if (g_game->heatmap_on && g_game->board &&
            g_game->board->state == STATE_PLAYING) {
            solver_compute_probabilities(g_game->board, g_prob_map);
        }

        /* render_paint handles BeginPaint/EndPaint internally */
        render_paint(g_renderer, hwnd, g_game,
                     g_game->heatmap_on ? g_prob_map : NULL);
        return 0;
    }

    /* ---- Left mouse button DOWN ---- */
    case WM_LBUTTONDOWN: {
        int px = LOWORD(lp);
        int py = HIWORD(lp);
        Board *b = g_game->board;
        if (!b) break;

        int client_w;
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            client_w = rc.right;
        }

        /* Check face button (restart) */
        if (render_face_hit(b->width, client_w, px, py)) {
            g_lmb_down = 0;
            g_rmb_down = 0;
            g_game->press_active = 0;
            start_new_game(hwnd, g_game->difficulty);
            return 0;
        }

        /* Check toolbar buttons */
        int cw_unused, ch_unused;
        render_calc_window_size(b->width, b->height, &cw_unused, &ch_unused);
        int tb = render_toolbar_hit(b->width, b->height, client_w, px, py);
        if (tb >= 0) {
            switch (tb) {
            case 0:  /* Hint */
                game_request_hint(g_game);
                sound_hint();
                break;
            case 1:  /* Heatmap */
                game_toggle_heatmap(g_game);
                CheckMenuItem(GetSubMenu(g_menu, 1), IDM_HEATMAP,
                    MF_BYCOMMAND | (g_game->heatmap_on ? MF_CHECKED : MF_UNCHECKED));
                break;
            case 2:  /* Fog */
                game_toggle_fog(g_game);
                CheckMenuItem(GetSubMenu(g_menu, 1), IDM_FOG,
                    MF_BYCOMMAND | (g_game->fog_mode ? MF_CHECKED : MF_UNCHECKED));
                break;
            case 3:  /* New game */
                start_new_game(hwnd, g_game->difficulty);
                break;
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        /* --- Chord detection: if right button is already held, enter chord mode --- */
        if (wp & MK_RBUTTON) {
            g_lmb_down = 1;
            chord_enter(hwnd, px, py);
            return 0;
        }

        g_lmb_down = 1;

        int cx, cy;
        if (render_pixel_to_cell(b->width, b->height, px, py, &cx, &cy)) {
            Cell *c = board_cell(b, cx, cy);

            /* --- KEY: Left-click on a revealed numbered cell ---
               Show surrounding cells as pressed (Windows 7 behaviour).
               Don't reveal anything yet — just visual feedback.
               Actual chord happens only with both buttons. */
            if (c && (c->flags & CELL_REVEALED) && c->number > 0) {
                g_game->press_active = 1;
                g_game->press_cx = cx;
                g_game->press_cy = cy;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            /* Normal left click on unrevealed cell: reveal immediately */
            GameState before = b->state;
            game_left_click(g_game, cx, cy);
            replay_record(g_replay, cx, cy, ACTION_LEFT);
            sound_click();

            /* Send move to peer */
            if (g_net && g_net->connected) {
                net_send_move(g_net, cx, cy, ACTION_LEFT);
                net_send_state(g_net, b->revealed_count,
                               b->flagged_count, b->state);
            }

            if (b->state != before &&
                (b->state == STATE_WON || b->state == STATE_LOST)) {
                handle_game_end(hwnd);
            }

            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    /* ---- Left mouse button UP ---- */
    case WM_LBUTTONUP: {
        if (g_game->press_active) {
            /* Was pressing on a revealed number cell.
               Perform chord: if adjacent flags == number,
               game_chord_click reveals the remaining cells.
               If flags don't match, chord_release does nothing
               (game_chord_click checks internally). */
            chord_release(hwnd);
            g_lmb_down = 0;
            return 0;
        }
        g_lmb_down = 0;
        return 0;
    }

    /* ---- Right mouse button DOWN ---- */
    case WM_RBUTTONDOWN: {
        int px = LOWORD(lp);
        int py = HIWORD(lp);
        Board *b = g_game->board;
        if (!b) break;

        /* Check if left button is already held => chord mode */
        if (wp & MK_LBUTTON) {
            g_rmb_down = 1;
            chord_enter(hwnd, px, py);
            return 0;
        }

        /* Normal right click: flag toggle immediately */
        g_rmb_down = 1;

        int cx, cy;
        if (render_pixel_to_cell(b->width, b->height, px, py, &cx, &cy)) {
            game_right_click(g_game, cx, cy);
            replay_record(g_replay, cx, cy, ACTION_RIGHT);
            sound_flag();

            /* Send move to peer */
            if (g_net && g_net->connected) {
                net_send_move(g_net, cx, cy, ACTION_RIGHT);
                net_send_state(g_net, b->revealed_count,
                               b->flagged_count, b->state);
            }

            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    /* ---- Right mouse button UP ---- */
    case WM_RBUTTONUP: {
        if (g_game->press_active) {
            /* Was in chord mode: perform chord action on release */
            chord_release(hwnd);
            return 0;
        }
        g_rmb_down = 0;
        return 0;
    }

    /* ---- Middle mouse button (chord) ---- */
    case WM_MBUTTONDOWN: {
        int px = LOWORD(lp);
        int py = HIWORD(lp);
        Board *b = g_game->board;
        if (!b) break;

        int cx, cy;
        if (render_pixel_to_cell(b->width, b->height, px, py, &cx, &cy)) {
            GameState before = b->state;
            game_chord_click(g_game, cx, cy);
            replay_record(g_replay, cx, cy, ACTION_CHORD);

            /* Send move to peer */
            if (g_net && g_net->connected) {
                net_send_move(g_net, cx, cy, ACTION_CHORD);
                net_send_state(g_net, b->revealed_count,
                               b->flagged_count, b->state);
            }

            if (b->state != before &&
                (b->state == STATE_WON || b->state == STATE_LOST)) {
                handle_game_end(hwnd);
            }

            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    /* ---- Mouse move (for chord press visual feedback) ---- */
    case WM_MOUSEMOVE: {
        if (g_game->press_active) {
            int px = LOWORD(lp);
            int py = HIWORD(lp);
            chord_update(hwnd, px, py);
        }
        return 0;
    }

    /* ---- Timer ---- */
    case WM_TIMER:
        if (wp == TIMER_GAME) {
            game_tick(g_game);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        else if (wp == TIMER_NET_ACCEPT) {
            /* Polling for incoming connection (host mode) */
            if (g_net && g_net->role == NET_SERVER && !g_net->connected) {
                if (net_host_accept(g_net)) {
                    KillTimer(hwnd, TIMER_NET_ACCEPT);

                    wchar_t peer_w[32];
                    MultiByteToWideChar(CP_UTF8, 0, g_net->peer_name, -1,
                                        peer_w, 32);
                    wchar_t msg_buf[128];
                    /* 对手已连接！ */
                    _snwprintf(msg_buf, 128,
                        L"\x5BF9\x624B\x5DF2\x8FDE\x63A5\xFF01\n%ls",
                        peer_w);
                    MessageBoxW(hwnd, msg_buf,
                        L"\x8054\x673A", MB_OK | MB_ICONINFORMATION);

                    /* Send current board to peer */
                    if (g_game->board) {
                        net_send_board(g_net, g_game->board);
                    }

                    /* Start game polling timer */
                    SetTimer(hwnd, TIMER_NET_POLL, 50, NULL);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
        }
        else if (wp == TIMER_NET_POLL) {
            /* Poll for incoming network messages */
            if (g_net && g_net->connected) {
                int out_x, out_y, out_action;
                int result = net_poll(g_net, &out_x, &out_y, &out_action);
                if (result > 0) {
                    /* Peer made a move - update display */
                    InvalidateRect(hwnd, NULL, FALSE);

                    /* Check if peer sent game_end */
                    if (g_net->peer_state == STATE_WON ||
                        g_net->peer_state == STATE_LOST) {
                        wchar_t peer_w[32];
                        MultiByteToWideChar(CP_UTF8, 0, g_net->peer_name, -1,
                                            peer_w, 32);
                        wchar_t msg_buf[128];
                        if (g_net->peer_state == STATE_WON) {
                            /* 对手已完成！已揭开 X 格，标记 Y 雷 */
                            _snwprintf(msg_buf, 128,
                                L"\x5BF9\x624B\x5DF2\x5B8C\x6210\xFF01\n"
                                L"\x63ED\x5F00 %d \x683C\xFF0C"
                                L"\x6807\x8BB0 %d \x96F7",
                                g_net->peer_revealed, g_net->peer_flagged);
                        } else {
                            /* 对手踩雷了！ */
                            _snwprintf(msg_buf, 128,
                                L"\x5BF9\x624B\x8E29\x96F7\x4E86\xFF01");
                        }
                        MessageBoxW(hwnd, msg_buf,
                            L"\x8054\x673A", MB_OK | MB_ICONINFORMATION);
                    }
                }
                else if (result < 0) {
                    /* Connection lost */
                    KillTimer(hwnd, TIMER_NET_POLL);
                    net_reset(g_net);
                    net_enable_menu_items(0);
                    MessageBoxW(hwnd,
                        L"\x8FDE\x63A5\x5DF2\x65AD\x5F00",  /* 连接已断开 */
                        L"\x8054\x673A", MB_OK | MB_ICONWARNING);
                }
            }
        }
        return 0;

    /* ---- Menu commands ---- */
    case WM_COMMAND:
        switch (LOWORD(wp)) {

        case IDM_NEW_GAME:
            start_new_game(hwnd, g_game->difficulty);
            break;

        case IDM_BEGINNER:
            start_new_game(hwnd, DIFF_BEGINNER);
            update_difficulty_check(g_menu, DIFF_BEGINNER);
            break;

        case IDM_INTERMEDIATE:
            start_new_game(hwnd, DIFF_INTERMEDIATE);
            update_difficulty_check(g_menu, DIFF_INTERMEDIATE);
            break;

        case IDM_EXPERT:
            start_new_game(hwnd, DIFF_EXPERT);
            update_difficulty_check(g_menu, DIFF_EXPERT);
            break;

        case IDM_CUSTOM:
            show_custom_dialog(hwnd);
            break;

        case IDM_EXIT:
            DestroyWindow(hwnd);
            break;

        case IDM_NO_GUESS:
            g_game->no_guess_mode = !g_game->no_guess_mode;
            CheckMenuItem(GetSubMenu(g_menu, 1), IDM_NO_GUESS,
                MF_BYCOMMAND |
                (g_game->no_guess_mode ? MF_CHECKED : MF_UNCHECKED));
            break;

        case IDM_HEATMAP:
            game_toggle_heatmap(g_game);
            CheckMenuItem(GetSubMenu(g_menu, 1), IDM_HEATMAP,
                MF_BYCOMMAND |
                (g_game->heatmap_on ? MF_CHECKED : MF_UNCHECKED));
            InvalidateRect(hwnd, NULL, FALSE);
            break;

        case IDM_HINT:
            game_request_hint(g_game);
            sound_hint();
            InvalidateRect(hwnd, NULL, FALSE);
            break;

        case IDM_FOG:
            game_toggle_fog(g_game);
            CheckMenuItem(GetSubMenu(g_menu, 1), IDM_FOG,
                MF_BYCOMMAND |
                (g_game->fog_mode ? MF_CHECKED : MF_UNCHECKED));
            InvalidateRect(hwnd, NULL, FALSE);
            break;

        case IDM_REPLAY_SAVE:
            do_replay_save(hwnd);
            break;

        case IDM_REPLAY_LOAD:
            do_replay_load(hwnd);
            break;

        case IDM_STATS:
            show_stats_dialog(hwnd);
            break;

        case IDM_ABOUT:
            show_about_dialog(hwnd);
            break;

        /* ---- Network menu commands ---- */
        case IDM_NET_HOST:
            do_net_host(hwnd);
            break;

        case IDM_NET_JOIN:
            do_net_join(hwnd);
            break;

        case IDM_NET_DISCONNECT:
            do_net_disconnect(hwnd);
            break;
        }
        return 0;

    /* ---- Keyboard shortcuts ---- */
    case WM_KEYDOWN:
        switch (wp) {
        case 'N':
            start_new_game(hwnd, g_game->difficulty);
            break;
        case 'H':
            game_request_hint(g_game);
            sound_hint();
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        case 'P':
            game_toggle_heatmap(g_game);
            CheckMenuItem(GetSubMenu(g_menu, 1), IDM_HEATMAP,
                MF_BYCOMMAND |
                (g_game->heatmap_on ? MF_CHECKED : MF_UNCHECKED));
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        case 'F':
            game_toggle_fog(g_game);
            CheckMenuItem(GetSubMenu(g_menu, 1), IDM_FOG,
                MF_BYCOMMAND |
                (g_game->fog_mode ? MF_CHECKED : MF_UNCHECKED));
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }
        return 0;

    /* ---- Window destruction ---- */
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_GAME);
        KillTimer(hwnd, TIMER_REPLAY);
        KillTimer(hwnd, TIMER_NET_POLL);
        KillTimer(hwnd, TIMER_NET_ACCEPT);

        if (g_renderer)  { render_destroy(g_renderer);  g_renderer = NULL; }
        if (g_prob_map)  { free(g_prob_map);             g_prob_map = NULL; }

        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ================================================================== */
/*  WinMain                                                            */
/* ================================================================== */

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPWSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;

    /* ---- Initialize Winsock ---- */
    net_init();

    /* ---- Initialize sound ---- */
    sound_init();

    /* ---- Initialize user database ---- */
    g_userdb = userdb_create();
    userdb_load(g_userdb, USERDB_FILE);

    /* ---- Show login dialog ---- */
    int login_result = show_login_dialog(hInstance);
    if (login_result == 0) {
        /* User closed login dialog without logging in */
        userdb_destroy(g_userdb);
        net_cleanup();
        return 0;
    }

    /* ---- Create network state ---- */
    g_net = net_create();

    /* ---- Create game objects ---- */
    g_game  = game_create();
    g_stats = stats_create();

    /* Load per-user stats */
    char stats_file[128];
    get_stats_filename(stats_file, sizeof(stats_file));
    stats_load(g_stats, stats_file);

    /* Initial board is beginner (set by game_create) */
    Board *b = g_game->board;
    g_replay = replay_create(b->width, b->height, b->mine_count);
    ensure_prob_map(b->width, b->height);

    /* Initialize chord press tracking */
    g_game->press_active = 0;
    g_game->press_cx = -1;
    g_game->press_cy = -1;
    g_game->explode_x = -1;
    g_game->explode_y = -1;

    /* ---- Register window class ---- */
    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInstance;
    wc.hCursor        = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground  = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName  = CLASS_NAME;
    wc.hIcon          = LoadIconW(NULL, IDI_APPLICATION);
    wc.hIconSm        = LoadIconW(NULL, IDI_APPLICATION);

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL,
            L"\x7A97\x53E3\x7C7B\x6CE8\x518C\x5931\x8D25\xFF01",  /* 窗口类注册失败！ */
            L"\x9519\x8BEF", MB_ICONERROR);
        return 1;
    }

    /* ---- Calculate window size ---- */
    int client_w, client_h;
    render_calc_window_size(b->width, b->height, &client_w, &client_h);

    DWORD style   = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    DWORD exstyle = 0;

    RECT rc = { 0, 0, client_w, client_h };
    AdjustWindowRectEx(&rc, style, TRUE, exstyle);  /* TRUE = has menu */

    int win_w = rc.right  - rc.left;
    int win_h = rc.bottom - rc.top;

    /* Center on screen */
    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);
    int x  = (sx - win_w) / 2;
    int y  = (sy - win_h) / 2;

    /* ---- Create menu ---- */
    g_menu = create_menu();

    /* ---- Build window title with username ---- */
    wchar_t window_title[128];
    {
        wchar_t wname[32];
        MultiByteToWideChar(CP_UTF8, 0, g_current_user.username, -1, wname, 32);
        _snwprintf(window_title, 128,
            L"\x626B\x96F7 - \x73A9\x5BB6: %ls", wname);
        window_title[127] = L'\0';
    }

    /* ---- Create window ---- */
    g_hwnd = CreateWindowExW(
        exstyle,
        CLASS_NAME,
        window_title,
        style,
        x, y, win_w, win_h,
        NULL,           /* parent */
        g_menu,         /* menu */
        hInstance,
        NULL);

    if (!g_hwnd) {
        MessageBoxW(NULL,
            L"\x7A97\x53E3\x521B\x5EFA\x5931\x8D25\xFF01",  /* 窗口创建失败！ */
            L"\x9519\x8BEF", MB_ICONERROR);
        game_destroy(g_game);
        stats_destroy(g_stats);
        replay_destroy(g_replay);
        net_destroy(g_net);
        userdb_destroy(g_userdb);
        net_cleanup();
        return 1;
    }

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    /* Update title with username */
    update_window_title(g_hwnd);

    /* ---- Message loop ---- */
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    /* ---- Cleanup ---- */
    if (g_net)    { net_destroy(g_net);       g_net = NULL; }
    if (g_game)   game_destroy(g_game);
    if (g_stats)  stats_destroy(g_stats);
    if (g_replay) replay_destroy(g_replay);
    if (g_userdb) { userdb_destroy(g_userdb); g_userdb = NULL; }
    net_cleanup();

    return (int)msg.wParam;
}
