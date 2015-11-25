/*
* The server part of filesystem
* Handles command from clients
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>

#include "common.h"
#include <udt.h>


//////////////////////////////////////////////////////////////////////
// SIGNAL/KILL HANDLERS
//////////////////////////////////////////////////////////////////////

static volatile int _num_clients = 0;
static struct sigaction _sa_int_orig;

void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
    _num_clients--;
    fprintf(stderr, "client handler terminated\n");
}

void sigint_handler(int s)
{
    if (_num_clients <= 0) {
        fprintf(stderr, "SIGINT: no clients, done!\n");
        sigaction(SIGINT, &_sa_int_orig, NULL);
        kill(0, SIGINT);
    }
    fprintf(stderr, "SIGINT: %d client connections still open\n", _num_clients);
}


//////////////////////////////////////////////////////////////////////
// CLIENT SERVING LOOP
//////////////////////////////////////////////////////////////////////

int client_handler(int fd, int udtport)
{
    char rcvbuf[512];
    ssize_t n;
    UDTSOCKET ufd;

    /* Ignore SIGINTs, the parent handles them */
    struct sigaction sa_ignore;
    sa_ignore.sa_handler = SIG_IGN;
    sa_ignore.sa_flags = SA_SIGINFO;
    sigemptyset(&sa_ignore.sa_mask);
    sigaction(SIGINT, &sa_ignore, NULL);

    /* Do "auth" */
    if (exchange_versions(fd) != 0) {
       close_socket(fd); 
       exit(0);
    }

    /* Tell our UDT port number and wait for connection */  
    fprintf(stderr, "Waiting for UDT client connection on port %d\n", udtport);
    ufd = server_accept_udt(fd, udtport);
    fprintf(stderr, "Now accepting client commands.\n");

    /* Handle commands */
    while (1) {
        n = recv_str(fd, rcvbuf, sizeof(rcvbuf), 0);
        if (n <= 0) {
            perror("recv");
            break;
        }
        if (strcasecmp(rcvbuf, "dir") == 0) {
            server_senddir(fd);
        } else
        if (strcasecmp(rcvbuf, "getattr") == 0) {
            server_sendattr(fd);
        } else
        if (strcasecmp(rcvbuf, "read") == 0) {
            server_sendsegment(fd, ufd);
        } else
        if (strcasecmp(rcvbuf, "truncate") == 0) {
            server_truncate(fd,ufd);
        } else
        if (strcasecmp(rcvbuf, "write") == 0) {
            server_write(fd,ufd);
        } else
        if (strcasecmp(rcvbuf, "rename") == 0) {
            server_rename(fd,ufd);
        } else
        if (strcasecmp(rcvbuf, "unlink") == 0) {
            server_unlink(fd,ufd);
        }
        if (strcasecmp(rcvbuf, "utime") == 0) {
            server_utime(fd,ufd);
        }
    }
    close_socket(fd);
    exit(0);
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


//////////////////////////////////////////////////////////////////////
// MAIN -- CLIENT LISTENER
//////////////////////////////////////////////////////////////////////

int main(int argc, char** argv)
{
    socklen_t sin_size;
    int child_fd;
    int sockfd;
    int udtport = M_PORT_UDTBASE;
    struct sockaddr_storage remoteaddr;
    char s[INET6_ADDRSTRLEN];
    struct sigaction sa_chld;
    struct sigaction sa_int;

    /* Library init */
    UDT::startup();

    /* Server init */
    sockfd = server_open_socket();

    /* Signal handlers -- SIGCHLD: reap all dead processes */
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        perror("sigaction SIGCHLD");
        exit(1);
    }

    /* Signal handlers -- SIGINT: prevent Control-C with open remote mounts */
    sa_int.sa_handler = sigint_handler;
    sa_int.sa_flags = SA_SIGINFO;
    sigemptyset(&sa_int.sa_mask);
    if (sigaction(SIGINT, &sa_int, &_sa_int_orig) == -1) {
        perror("sigaction SIGINT");
        exit(1);
    }

    /* Path to share */
    char sharedpath[PATH_MAX+1] = "";
    if (argc >= 2) {
        chdir(argv[1]);
    }
    getcwd(sharedpath, PATH_MAX);
    fprintf(stderr, "Sharing directory %s\n", sharedpath);

    /* Client wait */
    while(1) {
        sin_size = sizeof(remoteaddr);
        child_fd = accept(sockfd, (struct sockaddr *)&remoteaddr, &sin_size);
        if (child_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(remoteaddr.ss_family, get_in_addr((struct sockaddr *)&remoteaddr), s, sizeof(s));

        udtport = M_PORT_UDTBASE + (_num_clients % M_MAX_CLIENTS);
         _num_clients++;

        if (!fork()) {
            /* child */
            close(sockfd);
            client_handler(child_fd, udtport);
        } else {
            /* parent */
            close(child_fd);
        }
    }
    return 0;
}
