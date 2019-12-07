#ifndef GUESS_NUMBER_H
#define GUESS_NUMBER_H

#include "guess_number_def.h"

#include <netinet/in.h>
#include <arpa/inet.h>

enum {
    SESSION_STATE_IDLE = 0,
    SESSION_STATE_ACQ,
    SESSION_STATE_USE
};

enum {
    COMMAND_BEGIN = 1,
    COMMAND_BEGIN_ACK,
    COMMAND_GUESS,
    COMMAND_GUESS_ACK,
    COMMAND_END,
    COMMAND_END_ACK
};

typedef struct {
    int cmd;
    int data;
} guess_number_command_t;

typedef struct {
    int session_id;
    int cli_fd;
    int active_time;
    struct sockaddr_in saddr;

    int state;
    int aim_number;
    int guess_count;
    int score;
    guess_number_command_t ack_command;
} guess_number_session_t;


typedef struct gn_server{
    int listen_port;
    int listen_fd;
    int epoll_fd;
    int num_free_sess;
    int max_session;
    guess_number_session_t *sessions;
} gn_server_t;

#endif
