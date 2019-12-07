#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include "guess_number.h"
#include "guess_util_proto.h"

#define HOST_LEN    20
#define MAX_BUFF    128


void banner(void)
{
    printf("**********************************\n"
           "*          guess a number        *\n"
           "**********************************\n"
           "command:\n"
           "  start     start a new game\n"
           "  end       end game\n"
           "  quit      quit this game\n"
           "  help      print this help message\n"
           "  [number]  you guess this number\n\n");
}

int handle_user_req(int in, int out, int *to_quit)
{
    int i, nread, len;
    char buff[MAX_BUFF+1];
    guess_number_command_t req = {0, 0};

    nread = read(in, buff, MAX_BUFF);
    buff[nread] = '\0';
    if (strstr(buff, "start")) {
        req.cmd = COMMAND_BEGIN;
    }
    else if (strstr(buff, "end")) {
        req.cmd = COMMAND_END;
    }
    else if (strstr(buff, "quit")) {
        close(out);
        *to_quit = 1;
        return 1;
    }
    else if (strstr(buff, "help")) {
        banner();
    }
    else {
        for (i=0; i<nread; i++) {
            if (!(buff[i] == ' ' || buff[i] == '\n' ||
                (buff[i] >= '0' && buff[i] <= '9')))
                break;
        }
        if (i < nread) {
            printf("illegal number format\n");
        }
        else {
            req.cmd = COMMAND_GUESS;
            req.data = atoi(buff);
        }
    }
    if (req.cmd != 0) {
        len = command_serialize(&req, buff, MAX_BUFF);
        write(out, buff, len);
    }
    return 1;
}

int show_srv_rsp(int sock_fd)
{
    int nread;
    char buff[MAX_BUFF];
    guess_number_command_t rsp;

    nread = read(sock_fd, buff, MAX_BUFF);
    if (nread > 0) {
        command_deserialize(buff, nread, &rsp);
        switch (rsp.cmd){
        case COMMAND_BEGIN_ACK:
            printf("welcome\n>");
        break;
        case COMMAND_END_ACK:
            printf("bye\n>");
            close(sock_fd);
        break;
        case COMMAND_GUESS_ACK:
            if (rsp.data == 0)
                printf("congratulations, you are right\n>");
            else if (rsp.data < 0) {
                printf("too small, please try again\n>");
            }
            else {
                printf("too big, please try again\n>");
            }
        break;
        }
    }
    else {
        //if (nread == 0) {
            close(sock_fd);
        //}
    }
    return 1;
}

int start_guess(int sock_fd)
{
    int epoll_fd, nfd, quit;
    struct epoll_event event;

    epoll_fd = epoll_create(2);
    if (epoll_fd < 0) {
        fprintf(stderr, "epoll_create failed: %s\n", strerror(errno));
        return 0;
    }

    event.events = EPOLLIN;
    event.data.fd = sock_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &event) < 0) {
        fprintf(stderr, "epoll_ctl add sock_fd failed: %s\n", strerror(errno));
        close(epoll_fd);
        return 0;
    }

    event.events = EPOLLIN;
    event.data.fd = 0;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, 0, &event) < 0) {
        fprintf(stderr, "epoll_ctl add stdin failed: %s\n", strerror(errno));
        close(epoll_fd);
        return 0;
    }

    banner();
    quit = 0;
    while (!quit) {
        if ((nfd = epoll_wait(epoll_fd, &event, 1, -1)) < 0) {
            break;
        }
        if (event.events & EPOLLIN) {
            if (event.data.fd == 0) {
                handle_user_req(event.data.fd, sock_fd, &quit);
                if (quit)
                    break;
            }
            else {
                show_srv_rsp(event.data.fd);
            }
        }
    }
    
    close(epoll_fd);
    
    return quit;
}

int start_client(const char *host, ushort port)
{
    int fd;
    struct sockaddr_in srv_addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "socket failed: %s\n", strerror(errno));
        return -1;
    }

    srv_addr.sin_family = AF_INET;
    inet_pton(AF_INET, host, &srv_addr.sin_addr);
    srv_addr.sin_port = htons(port);
    if (connect(fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
        fprintf(stderr, "connect %s:%d failed: %s\n", host, port, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

int main(int argc, char *argv[])
{
    char host[HOST_LEN];
    ushort port;
    int sock_fd;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s server-host server-port\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    memcpy(host, argv[1], HOST_LEN);
    port = atoi(argv[2]);

    if ((sock_fd = start_client(host, port)) < 0)
        return EXIT_FAILURE;
    
    start_guess(sock_fd);
    close(sock_fd);

    return EXIT_SUCCESS;
}