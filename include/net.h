#ifndef NET_H
#define NET_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include "board.h"

#define NET_PORT        27015
#define NET_BUF_SIZE    4096

/* Message types */
#define MSG_BOARD_DATA  0x01    /* Server -> Client: board setup */
#define MSG_MOVE        0x02    /* Bidirectional: player move */
#define MSG_STATE       0x03    /* Bidirectional: game state update */
#define MSG_GAME_END    0x04    /* Bidirectional: game over */
#define MSG_CHAT        0x05    /* Bidirectional: chat message */

/* Network role */
typedef enum {
    NET_NONE,       /* Single player */
    NET_SERVER,     /* Hosting */
    NET_CLIENT      /* Connected to host */
} NetRole;

/* Network state */
typedef struct {
    NetRole role;
    SOCKET  listen_sock;        /* Server only */
    SOCKET  peer_sock;          /* Connected peer */
    int     connected;
    int     peer_revealed;      /* Opponent's revealed count */
    int     peer_flagged;       /* Opponent's flagged count */
    int     peer_state;         /* Opponent's game state */
    char    peer_name[32];      /* Opponent's username */
    /* Board data for sync */
    int     board_w, board_h, board_mines;
    unsigned char *mine_data;   /* Mine positions (bit array) */
} NetState;

/* Initialize / cleanup Winsock */
int     net_init(void);
void    net_cleanup(void);

/* Create / destroy net state */
NetState *net_create(void);
void      net_destroy(NetState *ns);
void      net_reset(NetState *ns);

/* Server: start hosting, returns 1 on success */
int     net_host_start(NetState *ns);
/* Server: check for incoming connection (non-blocking), returns 1 if connected */
int     net_host_accept(NetState *ns);

/* Client: connect to server IP, returns 1 on success */
int     net_connect(NetState *ns, const char *ip);

/* Send board data (server -> client) */
int     net_send_board(NetState *ns, Board *b);
/* Receive board data (client), creates mines on board */
int     net_recv_board(NetState *ns, Board *b);

/* Send a move */
int     net_send_move(NetState *ns, int x, int y, int action);
/* Send game state update */
int     net_send_state(NetState *ns, int revealed, int flagged, int state);
/* Send game end */
int     net_send_game_end(NetState *ns, int won);

/* Non-blocking receive check. Processes incoming messages.
   Returns message type received, or 0 if none.
   Fills out_x, out_y, out_action for MSG_MOVE. */
int     net_poll(NetState *ns, int *out_x, int *out_y, int *out_action);

/* Disconnect */
void    net_disconnect(NetState *ns);

/* Get local IP address string */
void    net_get_local_ip(char *buf, int buflen);

#endif /* NET_H */
