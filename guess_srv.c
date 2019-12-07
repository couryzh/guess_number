#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <assert.h>
#include "guess_number.h"
#include "guess_util_proto.h"

#define SRV_PORT        24000
#define MAX_BACK_LOG    10
#define MAX_BUFF        128


volatile int run = 1;
enum {
    max_wait_timeout_in_secs = 10
};

bool handle_read_event(gn_server_t *srv, uint id)
{
    bool ret;
    char addr[32];
    struct epoll_event event;

    ret = true;
    if (id == -1) {
        int sess_id;
        struct sockaddr_in cli_addr;
        socklen_t len = sizeof(cli_addr);
        int client_fd = accept(srv->listen_fd, (struct sockaddr *)&cli_addr, &len);
        if (client_fd > 0) {
            fprintf(stdout, "accept host %s\n", host(&cli_addr, addr, 32));
            if ((sess_id = acquire_session(client_fd, srv)) >= 0) {
                printf("got session %d\n", sess_id);
                event.events = EPOLLIN;
                event.data.u32 = sess_id;
                epoll_ctl(srv->epoll_fd, EPOLL_CTL_ADD, client_fd, &event);
            }
        }
    }
    else {
        char buff[MAX_BUFF];
        guess_number_session_t *session;
        assert(id < srv->max_session);
        session = &srv->sessions[id];
        
        int nr = read(session->cli_fd, buff, MAX_BUFF);
        if (nr > 0) {
            printf("find session %d\n", id);
            if (feed_session(srv, id, buff, nr)) {
                printf(" monitor EPOLLOUT\n");
                event.events = EPOLLOUT;
                event.data.u32 = id;
                epoll_ctl(srv->epoll_fd, EPOLL_CTL_MOD, session->cli_fd, &event);
            }
        }
        else if (nr == 0) {
            fprintf(stderr, "client %s unexpect disconnect\n", host(&session->saddr, addr, 32));
            epoll_ctl(srv->epoll_fd, EPOLL_CTL_DEL, session->cli_fd, NULL);
            release_session(srv, id);
            close(session->cli_fd);
        }
        else {
            if (errno != EINTR) {
                fprintf(stderr, "client %s read failed: %s\n", host(&session->saddr, addr, 32), strerror(errno));
                epoll_ctl(srv->epoll_fd, EPOLL_CTL_DEL, session->cli_fd, NULL);
                release_session(srv, id);
                close(session->cli_fd);
                ret = false;   
            }
        }
    }

    return ret;
}

bool handle_write_event(gn_server_t *srv, uint id)
{
    int nbytes;
    char buff[MAX_BUFF], addr[32];
    struct epoll_event event;
    guess_number_session_t *session;

    if (id >= srv->max_session) return false;
    session = &srv->sessions[id];
    nbytes = command_serialize(&session->ack_command, buff, MAX_BUFF);
    if (write(session->cli_fd, buff, nbytes) < nbytes)
        fprintf(stderr, "client %s write failed: %s\n", host(&session->saddr, addr, 32), strerror(errno));
    event.events = EPOLLIN;
    event.data.u32 = id;
    epoll_ctl(srv->epoll_fd, EPOLL_CTL_MOD, session->cli_fd, &event);
    printf("write for session %d, monitor EPOLLIN\n", id);
    return true;
}

gn_server_t *srv_init(ushort port)
{
    int reuse = 1;
    struct sockaddr_in sa;
    gn_server_t *srv = NULL;
    struct epoll_event ev;

    srv = calloc(1, sizeof(*srv));
    srv->listen_fd = socket(AF_INET, SOCK_STREAM, 0); 
    if (srv->listen_fd < 0) {
        goto release_srv;
    }

    if (setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        fprintf(stderr, "setsockopt SO_REUSEADDR failed: %s\n", strerror(errno));
        goto close_listen;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(port);
    if (bind(srv->listen_fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        fprintf(stderr, "bind failed: %s\n", strerror(errno));
        goto close_listen;
    }
    
    if (listen(srv->listen_fd, MAX_BACK_LOG) < 0) {
        fprintf(stderr, "listen failed: %s\n", strerror(errno));
        goto close_listen;
    }

    srv->epoll_fd = epoll_create(1);
    if (srv->epoll_fd < 0) {
        fprintf(stderr, "epoll_create failed: %s\n", strerror(errno));
        goto close_listen;
    }
    ev.events = EPOLLIN;
    ev.data.u32 = -1; 
    if (epoll_ctl(srv->epoll_fd, EPOLL_CTL_ADD, srv->listen_fd, &ev) < 0) {
        fprintf(stderr, "epoll_ctl failed to add listen fd: %s\n", strerror(errno));
        close(srv->epoll_fd);
        goto close_listen;
    }
    srv->listen_port = port;
    srv->num_free_sess = srv->max_session = 10;
    srv->sessions = calloc(srv->max_session, sizeof(guess_number_session_t));
    assert(srv->sessions);

    return srv;

close_listen:
    close(srv->listen_fd);
release_srv:
    free(srv);
    return NULL;
}

int srv_run(gn_server_t *srv)
{
    int ret;
    struct epoll_event events;
    int timeout = max_wait_timeout_in_secs * 1000;

    while (run) {
        ret = epoll_wait(srv->epoll_fd, &events, 1, timeout);
        if (ret < 0) {
            if (errno != EINTR)
                break;
        }
        else if (ret == 0) {
            char now[32];
            time_t t;
            struct tm tm;
            time(&t);
            localtime_r(&t, &tm);
            strftime(now, 32, "%F %T", &tm);
            fprintf(stdout, "%s wait new requests ...\n", now);
        }
        else {
            if (events.events & EPOLLIN) {
                handle_read_event(srv, events.data.u32);
            }
            else if (events.events & EPOLLOUT) {
                handle_write_event(srv, events.data.u32);
            }
        }
    }

    return 1;
}

void srv_destroy(gn_server_t *srv)
{
    if (srv) {
        if (srv->sessions) {
            free(srv->sessions);
        }
        close(srv->listen_fd);
        close(srv->epoll_fd);
        free(srv);
    }
}

int main(int argc, char *argv[])
{
    ushort srv_port;
    gn_server_t *srv;

    if (argc > 1) {
        srv_port = atoi(argv[1]);
    }
    else {
        srv_port = SRV_PORT;
    }

    srv = srv_init(srv_port);
    srv_run(srv);

    srv_destroy(srv);

    return EXIT_SUCCESS;
}