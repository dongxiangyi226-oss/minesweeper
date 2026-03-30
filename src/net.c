/*
 * net.c — Winsock-based networking for two-player Minesweeper.
 *
 * Protocol: each message is a byte stream starting with a 1-byte type tag,
 * followed by type-specific payload.  All multi-byte integers are sent in
 * host byte order (both peers are assumed to be x86 Windows machines).
 */

/* Require Vista+ API for inet_pton / inet_ntop */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "net.h"
#include "board.h"

#pragma comment(lib, "ws2_32.lib")

/* ================================================================== */
/* Winsock init / cleanup                                             */
/* ================================================================== */

int net_init(void)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return 0;
    return 1;
}

void net_cleanup(void)
{
    WSACleanup();
}

/* ================================================================== */
/* Create / destroy / reset                                           */
/* ================================================================== */

NetState *net_create(void)
{
    NetState *ns = (NetState *)calloc(1, sizeof(NetState));
    if (!ns) return NULL;

    ns->listen_sock = INVALID_SOCKET;
    ns->peer_sock   = INVALID_SOCKET;
    ns->role        = NET_NONE;
    ns->connected   = 0;
    ns->mine_data   = NULL;

    return ns;
}

void net_destroy(NetState *ns)
{
    if (!ns) return;

    net_disconnect(ns);

    if (ns->mine_data) {
        free(ns->mine_data);
        ns->mine_data = NULL;
    }

    free(ns);
}

void net_reset(NetState *ns)
{
    if (!ns) return;

    net_disconnect(ns);

    ns->role          = NET_NONE;
    ns->connected     = 0;
    ns->peer_revealed = 0;
    ns->peer_flagged  = 0;
    ns->peer_state    = 0;
    ns->board_w       = 0;
    ns->board_h       = 0;
    ns->board_mines   = 0;
    memset(ns->peer_name, 0, sizeof(ns->peer_name));

    if (ns->mine_data) {
        free(ns->mine_data);
        ns->mine_data = NULL;
    }
}

/* ================================================================== */
/* Disconnect                                                         */
/* ================================================================== */

void net_disconnect(NetState *ns)
{
    if (!ns) return;

    if (ns->peer_sock != INVALID_SOCKET) {
        shutdown(ns->peer_sock, SD_BOTH);
        closesocket(ns->peer_sock);
        ns->peer_sock = INVALID_SOCKET;
    }

    if (ns->listen_sock != INVALID_SOCKET) {
        closesocket(ns->listen_sock);
        ns->listen_sock = INVALID_SOCKET;
    }

    ns->connected = 0;
}

/* ================================================================== */
/* Helper: set socket to non-blocking mode                            */
/* ================================================================== */

static void set_nonblocking(SOCKET s)
{
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
}

/* ================================================================== */
/* Helper: send exactly `len` bytes (handles partial sends)           */
/* ================================================================== */

static int send_all(SOCKET s, const unsigned char *buf, int len)
{
    int total = 0;
    int n;

    while (total < len) {
        n = send(s, (const char *)(buf + total), len - total, 0);
        if (n == SOCKET_ERROR)
            return 0;
        total += n;
    }
    return 1;
}

/* ================================================================== */
/* Helper: receive exactly `len` bytes (blocking)                     */
/* ================================================================== */

static int recv_all(SOCKET s, unsigned char *buf, int len)
{
    int total = 0;
    int n;

    while (total < len) {
        n = recv(s, (char *)(buf + total), len - total, 0);
        if (n <= 0)
            return 0;
        total += n;
    }
    return 1;
}

/* ================================================================== */
/* Server: start hosting                                              */
/* ================================================================== */

int net_host_start(NetState *ns)
{
    struct sockaddr_in addr;
    int opt = 1;

    if (!ns) return 0;

    /* Clean up any previous session */
    net_disconnect(ns);

    /* Create TCP socket */
    ns->listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ns->listen_sock == INVALID_SOCKET)
        return 0;

    /* Allow address reuse */
    setsockopt(ns->listen_sock, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&opt, sizeof(opt));

    /* Bind to any interface on NET_PORT */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(NET_PORT);

    if (bind(ns->listen_sock, (struct sockaddr *)&addr, sizeof(addr))
        == SOCKET_ERROR) {
        closesocket(ns->listen_sock);
        ns->listen_sock = INVALID_SOCKET;
        return 0;
    }

    /* Listen with backlog of 1 (only one opponent) */
    if (listen(ns->listen_sock, 1) == SOCKET_ERROR) {
        closesocket(ns->listen_sock);
        ns->listen_sock = INVALID_SOCKET;
        return 0;
    }

    /* Set listen socket to non-blocking so host_accept can poll */
    set_nonblocking(ns->listen_sock);

    ns->role = NET_SERVER;
    return 1;
}

/* ================================================================== */
/* Server: accept incoming connection (non-blocking)                  */
/* ================================================================== */

int net_host_accept(NetState *ns)
{
    struct sockaddr_in peer_addr;
    int addr_len;
    SOCKET s;

    if (!ns || ns->listen_sock == INVALID_SOCKET)
        return 0;

    addr_len = sizeof(peer_addr);
    s = accept(ns->listen_sock, (struct sockaddr *)&peer_addr, &addr_len);

    if (s == INVALID_SOCKET)
        return 0;   /* no connection yet (WSAEWOULDBLOCK) */

    ns->peer_sock  = s;
    ns->connected  = 1;

    /* Set peer socket to non-blocking for polling */
    set_nonblocking(ns->peer_sock);

    return 1;
}

/* ================================================================== */
/* Client: connect to server                                          */
/* ================================================================== */

int net_connect(NetState *ns, const char *ip)
{
    struct sockaddr_in addr;

    if (!ns || !ip) return 0;

    net_disconnect(ns);

    /* Create TCP socket */
    ns->peer_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ns->peer_sock == INVALID_SOCKET)
        return 0;

    /* Resolve / fill address */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(NET_PORT);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        closesocket(ns->peer_sock);
        ns->peer_sock = INVALID_SOCKET;
        return 0;
    }

    /* Connect (blocking) */
    if (connect(ns->peer_sock, (struct sockaddr *)&addr, sizeof(addr))
        == SOCKET_ERROR) {
        closesocket(ns->peer_sock);
        ns->peer_sock = INVALID_SOCKET;
        return 0;
    }

    /* Switch to non-blocking for polling */
    set_nonblocking(ns->peer_sock);

    ns->role      = NET_CLIENT;
    ns->connected = 1;
    return 1;
}

/* ================================================================== */
/* Send board data (server -> client)                                 */
/*                                                                    */
/* Wire format:                                                       */
/*   [type:1][width:4][height:4][mine_count:4][mine_bits:N]           */
/*   where N = ceil(width * height / 8)                               */
/* ================================================================== */

int net_send_board(NetState *ns, Board *b)
{
    int total_cells, data_bytes;
    unsigned char header[13];
    unsigned char *mine_bits;
    int i;

    if (!ns || !b || ns->peer_sock == INVALID_SOCKET)
        return 0;

    total_cells = b->width * b->height;
    data_bytes  = (total_cells + 7) / 8;

    /* Build header */
    header[0] = MSG_BOARD_DATA;
    memcpy(header + 1, &b->width,      4);
    memcpy(header + 5, &b->height,     4);
    memcpy(header + 9, &b->mine_count, 4);

    /* Build mine bit array */
    mine_bits = (unsigned char *)calloc(data_bytes, 1);
    if (!mine_bits) return 0;

    for (i = 0; i < total_cells; i++) {
        if (b->cells[i].flags & CELL_MINE)
            mine_bits[i / 8] |= (1 << (i % 8));
    }

    /* Send header + data */
    if (!send_all(ns->peer_sock, header, 13)) {
        free(mine_bits);
        return 0;
    }
    if (!send_all(ns->peer_sock, mine_bits, data_bytes)) {
        free(mine_bits);
        return 0;
    }

    free(mine_bits);
    return 1;
}

/* ================================================================== */
/* Receive board data (client <- server)                              */
/* ================================================================== */

int net_recv_board(NetState *ns, Board *b)
{
    unsigned char header[12];   /* width + height + mine_count (no type byte) */
    int width, height, mine_count;
    int total_cells, data_bytes;
    unsigned char *mine_bits;
    unsigned char type_byte;
    int i;

    if (!ns || !b || ns->peer_sock == INVALID_SOCKET)
        return 0;

    /* We may need to temporarily make the socket blocking for reliable recv */
    /* Read type byte first */
    if (!recv_all(ns->peer_sock, &type_byte, 1))
        return 0;

    if (type_byte != MSG_BOARD_DATA)
        return 0;

    /* Read header fields */
    if (!recv_all(ns->peer_sock, header, 12))
        return 0;

    memcpy(&width,      header + 0, 4);
    memcpy(&height,     header + 4, 4);
    memcpy(&mine_count, header + 8, 4);

    /* Sanity check */
    if (width <= 0 || width > 100 || height <= 0 || height > 100)
        return 0;
    if (mine_count < 0 || mine_count > width * height)
        return 0;

    total_cells = width * height;
    data_bytes  = (total_cells + 7) / 8;

    /* Read mine bit array */
    mine_bits = (unsigned char *)calloc(data_bytes, 1);
    if (!mine_bits) return 0;

    if (!recv_all(ns->peer_sock, mine_bits, data_bytes)) {
        free(mine_bits);
        return 0;
    }

    /* Store in NetState for reference */
    ns->board_w     = width;
    ns->board_h     = height;
    ns->board_mines = mine_count;

    if (ns->mine_data)
        free(ns->mine_data);
    ns->mine_data = mine_bits;

    /* Apply mine positions to board */
    /* Clear existing mines first */
    for (i = 0; i < total_cells; i++)
        b->cells[i].flags &= ~CELL_MINE;

    b->mine_count = mine_count;

    for (i = 0; i < total_cells; i++) {
        if (mine_bits[i / 8] & (1 << (i % 8)))
            b->cells[i].flags |= CELL_MINE;
    }

    /* Recompute neighbor numbers */
    board_compute_numbers(b);

    return 1;
}

/* ================================================================== */
/* Send a move: [type:1][x:2][y:2][action:1]                         */
/* ================================================================== */

int net_send_move(NetState *ns, int x, int y, int action)
{
    unsigned char buf[6];
    short sx = (short)x;
    short sy = (short)y;

    if (!ns || ns->peer_sock == INVALID_SOCKET || !ns->connected)
        return 0;

    buf[0] = MSG_MOVE;
    memcpy(buf + 1, &sx, 2);
    memcpy(buf + 3, &sy, 2);
    buf[5] = (unsigned char)action;

    return send_all(ns->peer_sock, buf, 6);
}

/* ================================================================== */
/* Send game state: [type:1][revealed:4][flagged:4][state:4]          */
/* ================================================================== */

int net_send_state(NetState *ns, int revealed, int flagged, int state)
{
    unsigned char buf[13];

    if (!ns || ns->peer_sock == INVALID_SOCKET || !ns->connected)
        return 0;

    buf[0] = MSG_STATE;
    memcpy(buf + 1, &revealed, 4);
    memcpy(buf + 5, &flagged,  4);
    memcpy(buf + 9, &state,    4);

    return send_all(ns->peer_sock, buf, 13);
}

/* ================================================================== */
/* Send game end: [type:1][won:1]                                     */
/* ================================================================== */

int net_send_game_end(NetState *ns, int won)
{
    unsigned char buf[2];

    if (!ns || ns->peer_sock == INVALID_SOCKET || !ns->connected)
        return 0;

    buf[0] = MSG_GAME_END;
    buf[1] = (unsigned char)(won ? 1 : 0);

    return send_all(ns->peer_sock, buf, 2);
}

/* ================================================================== */
/* Non-blocking poll for incoming messages                            */
/*                                                                    */
/* Returns message type, or 0 if no message available.                */
/* For MSG_MOVE: fills *out_x, *out_y, *out_action.                   */
/* For MSG_STATE / MSG_GAME_END: updates ns-> peer fields.            */
/* ================================================================== */

int net_poll(NetState *ns, int *out_x, int *out_y, int *out_action)
{
    fd_set readfds;
    struct timeval tv;
    unsigned char buf[NET_BUF_SIZE];
    int n;

    if (!ns || ns->peer_sock == INVALID_SOCKET || !ns->connected)
        return 0;

    /* Use select with zero timeout for non-blocking check */
    FD_ZERO(&readfds);
    FD_SET(ns->peer_sock, &readfds);

    tv.tv_sec  = 0;
    tv.tv_usec = 0;

    if (select(0, &readfds, NULL, NULL, &tv) <= 0)
        return 0;   /* nothing ready, or error */

    if (!FD_ISSET(ns->peer_sock, &readfds))
        return 0;

    /* Read available data */
    n = recv(ns->peer_sock, (char *)buf, NET_BUF_SIZE, 0);

    if (n <= 0) {
        /* Connection closed or error */
        net_disconnect(ns);
        return 0;
    }

    /* Parse message type */
    switch (buf[0]) {

    case MSG_MOVE:
        if (n >= 6) {
            short sx, sy;
            memcpy(&sx, buf + 1, 2);
            memcpy(&sy, buf + 3, 2);
            if (out_x)      *out_x      = (int)sx;
            if (out_y)      *out_y      = (int)sy;
            if (out_action) *out_action  = (int)buf[5];
            return MSG_MOVE;
        }
        break;

    case MSG_STATE:
        if (n >= 13) {
            memcpy(&ns->peer_revealed, buf + 1, 4);
            memcpy(&ns->peer_flagged,  buf + 5, 4);
            memcpy(&ns->peer_state,    buf + 9, 4);
            return MSG_STATE;
        }
        break;

    case MSG_GAME_END:
        if (n >= 2) {
            ns->peer_state = buf[1] ? STATE_WON : STATE_LOST;
            return MSG_GAME_END;
        }
        break;

    case MSG_CHAT:
        /* Chat messages could be handled here in the future */
        return MSG_CHAT;

    default:
        break;
    }

    return 0;
}

/* ================================================================== */
/* Get local IPv4 address                                             */
/* ================================================================== */

void net_get_local_ip(char *buf, int buflen)
{
    char hostname[256];
    struct addrinfo hints, *result, *ptr;

    if (!buf || buflen <= 0) return;

    buf[0] = '\0';

    if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) {
        strncpy(buf, "127.0.0.1", buflen - 1);
        buf[buflen - 1] = '\0';
        return;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;        /* IPv4 only */
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname, NULL, &hints, &result) != 0) {
        strncpy(buf, "127.0.0.1", buflen - 1);
        buf[buflen - 1] = '\0';
        return;
    }

    /* Walk the list and pick the first non-loopback address */
    for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)ptr->ai_addr;
        unsigned long addr_val = ntohl(ipv4->sin_addr.s_addr);

        /* Skip 127.x.x.x */
        if ((addr_val >> 24) == 127)
            continue;

        inet_ntop(AF_INET, &ipv4->sin_addr, buf, buflen);
        freeaddrinfo(result);
        return;
    }

    /* Fallback: use first address even if loopback */
    if (result) {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)result->ai_addr;
        inet_ntop(AF_INET, &ipv4->sin_addr, buf, buflen);
    } else {
        strncpy(buf, "127.0.0.1", buflen - 1);
        buf[buflen - 1] = '\0';
    }

    freeaddrinfo(result);
}
