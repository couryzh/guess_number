#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "guess_number.h"

char *host(struct sockaddr_in *saddr, char *addr, int len)
{
    size_t addr_size;

    inet_ntop(AF_INET, &saddr->sin_addr, addr, len);
    addr_size = strlen(addr);
    snprintf(addr+addr_size, len-addr_size, ":%d", htons(saddr->sin_port));
    return addr;
}

int random_range(int max)
{
    return (int)(rand() * 1.0 / RAND_MAX * max);
}

int command_serialize(const guess_number_command_t *command, char *buff, int buff_len)
{
    int cmd, data;

    if (buff_len < sizeof (guess_number_command_t)) return sizeof(guess_number_command_t);
    cmd = htonl(command->cmd);
    memcpy(buff, &cmd, sizeof(int));
    data = htonl(command->data);
    memcpy(&buff[sizeof(int)], &data, sizeof(int));
    return sizeof(guess_number_command_t);
}

int command_deserialize(const char *buff, int len, guess_number_command_t *command)
{
    int cmd, data;

    memcpy(&cmd, buff, sizeof(int));
    command->cmd = ntohl(cmd);
    memcpy(&data, buff+sizeof(int), sizeof(int));
    command->data = ntohl(data);
    return true;
}

int acquire_session(int cli_fd, gn_server_t *srv)
{
    int i;
    socklen_t len;
    guess_number_session_t *session;

    if (srv->num_free_sess == 0)
        return -1;
    
    for (i=0, session = srv->sessions; i<srv->max_session; i++, session++) {
        if (session->state == SESSION_STATE_IDLE) {
            srv->num_free_sess--;
            session->state = SESSION_STATE_ACQ;
            session->cli_fd = cli_fd;
            len = sizeof(session->saddr);
            getpeername(cli_fd, (struct sockaddr *)&session->saddr, &len);
            return i;
        }
    }
    return -1;
}

bool feed_session(gn_server_t *srv, int session_id, const char *recv_buff, int nlen)
{
    guess_number_session_t *session;
    guess_number_command_t command;

    if (session_id >= srv->max_session) return false;
    if (srv->sessions[session_id].state == SESSION_STATE_IDLE) return false;

    if (!command_deserialize(recv_buff, nlen, &command)) return false;

    session = &srv->sessions[session_id];
    switch (command.cmd) {
    case COMMAND_BEGIN:
        session->ack_command.cmd = COMMAND_BEGIN_ACK;
        session->aim_number = random_range(100);
    break;
    case COMMAND_GUESS:
        session->ack_command.cmd = COMMAND_GUESS_ACK;
        if (command.data > session->aim_number) {
            session->ack_command.data = 1;
        }
        else if (command.data < session->aim_number) {
            session->ack_command.data = -1;
        }
        else {
            session->ack_command.data = 0;
        }
    break;
    case COMMAND_END:
        session->ack_command.cmd = COMMAND_END_ACK;
    break;
    default:
    fprintf(stderr, "skip command %d\n", command.cmd);
    break;
    }
    return true;
}

void release_session(gn_server_t *srv, int session_id)
{
    guess_number_session_t *session;

    if (session_id >= srv->max_session)
        return;
    
    session = &srv->sessions[session_id];
    if (session->state == SESSION_STATE_ACQ || session->state == SESSION_STATE_USE) {
        srv->sessions[session_id].state = SESSION_STATE_IDLE;
        srv->num_free_sess++;
    }
}