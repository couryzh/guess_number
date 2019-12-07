#ifndef GUESS_UTILS_PROTO_H
#define GUESS_UTILS_PROTO_H

#include "guess_number.h"

char *host(struct sockaddr_in *saddr, char *addr, int len);

int command_serialize(const guess_number_command_t *command, char *buff, int buff_len);

int command_deserialize(const char *buff, int len, guess_number_command_t *command);

int acquire_session(int cli_fd, gn_server_t *srv);

bool feed_session(gn_server_t *srv, int session_id, const char *recv_buff, int nlen);

void release_session(gn_server_t *srv, int session_id);

#endif