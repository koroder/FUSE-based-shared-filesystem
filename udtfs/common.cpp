/*
* The communication interface between client and server
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <utime.h>


#include <udt.h>

#include "udt_congestionctrl.h"
#include "common.h"

#define DEBUG 0
typedef unsigned long long ull_t;

char* path_to_local(char* p)
{
    if (p == NULL) { return NULL; }
    if (p[0] == '/') {
        char np[M_MAX_PATH + M_MAX_FILE];
        np[0] = '.'; np[1] = '\0';
        strncat(np, p, sizeof(np)-1);
        return strdup(np);
    } else {
        return strdup(p);
    }
}

int recv_str(int fd, char* buf, int maxlen, int flags)
{
    char tmp[1]; // could be made larger...
    ssize_t n;
    int nrx=0;
    *buf = '\0';
    while (nrx < maxlen) {
       n = recv(fd, tmp, 1, flags);
       if (n <= 0) { return n; }
       if (n > 0) {
           nrx++;
           *(buf++) = tmp[0];
           if (tmp[0] == '\0') { return nrx; }
       }
    }
    return nrx;
}

int send_cmd(int fd, const char* str)
{
    ssize_t n;
    if (strlen(str) <= 0) { return 0; }
    n = send(fd, str, strlen(str)+1, 0);
    if (n < 0) {
        perror("send_cmd: send");
    }
    return n;
}

//////////////////////////////////////////////////////////////////////
// SOCKET -- TCP
//////////////////////////////////////////////////////////////////////

int server_open_socket()
{
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int yes=1;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, M_PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt reuse");
            exit(1);
        }
        if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(int)) == -1) {
            perror("setsockopt nodelay");
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }
    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        return -1;
    }
    freeaddrinfo(servinfo); // all done with this structure
    if (listen(sockfd, M_BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }
    return sockfd;
}

int client_open_socket(char* hostname)
{
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int yes=1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(hostname, M_PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    // loop through all the results and make a socket
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("talker: socket");
            continue;
        }

        break;
    }
    freeaddrinfo(servinfo);
    if (p == NULL) {
        fprintf(stderr, "client: failed to create socket\n");
        return -1;
    }
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(int)) == -1) {
        perror("setsockopt nodelay");
    }
    if (connect(sockfd, p->ai_addr, p->ai_addrlen)) {
        fprintf(stderr, "client: failed to connect\n");
        return -1;
    }

    return sockfd;
}

void close_socket(int fd)
{
    shutdown(fd, SHUT_RDWR);
    close(fd);
}

//////////////////////////////////////////////////////////////////////
// SOCKET -- UDTv4
//////////////////////////////////////////////////////////////////////

UDTSOCKET server_accept_udt(int tcp_fd, const int port)
{
    /* Send our port# */
    char portstr[24];
    snprintf(portstr, sizeof(portstr)-1, "%d", port);
    send(tcp_fd, portstr, strlen(portstr)+1, 0);

    /* Initial UDT socket */
    UDTSOCKET ufd = UDT::socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in maddr;
    maddr.sin_family = AF_INET;
    maddr.sin_port = htons(port);
    maddr.sin_addr.s_addr = INADDR_ANY;
    memset(&(maddr.sin_zero), '\0', 8);
    if (M_UDT_RATE>0) {
        UDT::setsockopt(ufd, 0, UDT_CC, new CCCFactory<CUDPBlast>, sizeof(CCCFactory<CUDPBlast>));
    }
    UDT::setsockopt(ufd, 0, UDT_MSS, new int(M_MTU_UDT), sizeof(int));
    UDT::setsockopt(ufd, 0, UDT_RCVBUF, new int(10000000), sizeof(int));
    UDT::setsockopt(ufd, 0, UDP_RCVBUF, new int(10000000), sizeof(int));

    /* Bind set to listen mode */
    if (UDT::ERROR == UDT::bind(ufd, (sockaddr*)&maddr, sizeof(maddr))) {
        fprintf(stderr, "server_accept_udt: UDT::bind() failed\n");
        return UDT::INVALID_SOCK;
    }
    if (UDT::ERROR == UDT::listen(ufd, 10)) {
        fprintf(stderr, "server_accept_udt: UDT::listen() failed\n");
        return UDT::INVALID_SOCK;
    }
    struct sockaddr_in raddr;
    int raddr_len = sizeof(raddr);

    /* Wait for a connection */
    UDTSOCKET cli = UDT::accept(ufd, (sockaddr*)&raddr, &raddr_len);
    UDT::close(ufd);
    if (cli == UDT::INVALID_SOCK) {
        fprintf(stderr, "server_accept_udt: UDT::accept() failed\n");
        return UDT::ERROR;
    }

    /* Set the speed for the "server=>client" direction */
    CUDPBlast* cchandle = NULL;
    int temp;
    int rc = UDT::getsockopt(cli, 0, UDT_CC, &cchandle, &temp);
    if (rc != 0) {
         fprintf(stderr, "getsockopt UDT_CC error '%s'\n", UDT::getlasterror().getErrorMessage());
    }
    if (NULL != cchandle) {
         cchandle->setRate(M_UDT_RATE);
    }

    /* Done */
    return cli;
}

UDTSOCKET client_connect_udt(int tcp_fd)
{
    /* Receive port# */
    char portstr[24];
    if (recv_str(tcp_fd, portstr, sizeof(portstr)-1, 0) <= 0) {
        return UDT::INVALID_SOCK;
    }

    /* Basic socket */
    UDTSOCKET ufd = UDT::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in raddr;
    socklen_t raddrlen = sizeof(raddr);
    getpeername(tcp_fd, (struct sockaddr*)&raddr, &raddrlen);
    // inet_pton(AF_INET, "127.0.0.1", &raddr.sin_addr);
    raddr.sin_family = AF_INET;
    raddr.sin_port = htons(atoi(portstr));
    memset(&(raddr.sin_zero), '\0', 8); 
    UDT::setsockopt(ufd, 0, UDT_CC, new CCCFactory<CUDPBlast>, sizeof(CCCFactory<CUDPBlast>));
    UDT::setsockopt(ufd, 0, UDT_MSS, new int(M_MTU_UDT), sizeof(int));
    UDT::setsockopt(ufd, 0, UDT_SNDBUF, new int(10000000), sizeof(int));
    UDT::setsockopt(ufd, 0, UDP_SNDBUF, new int(10000000), sizeof(int));

    /* Connect to server IP & port */
    if (UDT::ERROR == UDT::connect(ufd, (sockaddr*)&raddr, sizeof(raddr))) {
        fprintf(stderr, "client_connect_udt: UDT::connect() failed\n");
        return UDT::INVALID_SOCK;
    }
    fprintf(stdout, "TCP and UDT connected to server.\n");

    /* Set the speed for the "client=>server" direction */
    CUDPBlast* cchandle = NULL;
    int temp;
    int rc = UDT::getsockopt(ufd, 0, UDT_CC, &cchandle, &temp);
    if (rc != 0) {
         fprintf(stderr, "getsockopt UDT_CC error '%s'\n", UDT::getlasterror().getErrorMessage());
    }
    if (NULL != cchandle) {
         cchandle->setRate(M_UDT_RATE);
    }

    return ufd;
}

void close_udt(UDTSOCKET ufd)
{
    UDT::close(ufd);
}

//////////////////////////////////////////////////////////////////////
// HANDSHAKE
//////////////////////////////////////////////////////////////////////

int exchange_versions(int fd)
{
    char str[M_MAX_VAL];
    ssize_t n;
    n = send(fd, M_VERSION, strlen(M_VERSION)+1, 0);
    if (n < 0) {
        perror("send version");
        return -1;
    }
    n = recv_str(fd, str, sizeof(str)-1, 0);
    if ((n <= 0) || (strncmp(str, M_VERSION, strlen(M_VERSION)) != 0)) {
        fprintf(stderr, "version mismatch! remote %s <=> local %s\n", str, M_VERSION);
        return -1;
    }
    return 0;
}

//////////////////////////////////////////////////////////////////////
// READDIR
//////////////////////////////////////////////////////////////////////

int server_senddir(int fd)
{
    char str[M_MAX_PATH + M_MAX_FILE];
    DIR* dir;
    char* path;
    struct dirent* entry;
    ssize_t n;

    n = recv_str(fd, str, sizeof(str)-1, 0);
    if (n <= 0) { return -1; }
    path = path_to_local(str);

    dir = opendir(path);
    if (dir == NULL) {
        perror("opendir");
        n = send(fd, RESERVED_STR_TERM, strlen(RESERVED_STR_TERM)+1, 0);
        free(path);
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp("..", entry->d_name) == 0) { continue; }
        if (strcmp(".", entry->d_name) == 0) { continue; }
        n = send(fd, entry->d_name, strlen(entry->d_name)+1, 0);
    }
    closedir(dir);
    n = send(fd, RESERVED_STR_TERM, strlen(RESERVED_STR_TERM)+1, 0);
    if (n < 0) {
        perror("server_senddir: end marker send()");
    }
    free(path);
    return 0;
}

int client_reqdir(int fd, const char* dirname)
{
    ssize_t n;
    n = send(fd, dirname, strlen(dirname)+1, 0);
    if (n <= 0) {
        perror("client_recvdir: send");
        return -1;
    }
    return 0;
}

char* client_recvdirentry(int fd)
{
    char filename[M_MAX_PATH + M_MAX_FILE];
    ssize_t n;
    n = recv_str(fd, filename, sizeof(filename)-1, 0);
    if (n < 1) {
        return NULL;
    }
    if (strncasecmp(filename, RESERVED_STR_TERM, strlen(RESERVED_STR_TERM)) == 0) {
        return NULL;
    }
    return strdup(filename);
}

//////////////////////////////////////////////////////////////////////
// GETATTR
//////////////////////////////////////////////////////////////////////

int server_sendattr(int fd)
{
    char pathstr[M_MAX_PATH + M_MAX_FILE];
    char str[M_MAX_VAL];
    struct stat statbuf;
    char* path;
    ssize_t n;

    n = recv_str(fd, pathstr, sizeof(pathstr)-1, 0);
    if (n <= 0) { return -1; }
    path = path_to_local(pathstr);
    if (stat(path, &statbuf) < 0) {
        perror("stat");
        free(path);
        send(fd, RESERVED_STR_TERM, strlen(RESERVED_STR_TERM)+1, 0);
        return -1;
    }
    free(path);
    snprintf(str, sizeof(str), "%Lu", (unsigned long long)(statbuf.st_size));
    n = send(fd, str, strlen(str)+1, 0);
    snprintf(str, sizeof(str), "%Lu", (unsigned long long)(statbuf.st_mode));
    n = send(fd, str, strlen(str)+1, 0);
    snprintf(str, sizeof(str), "%Lu", (unsigned long long)(statbuf.st_ctime));
    n = send(fd, str, strlen(str)+1, 0);
    snprintf(str, sizeof(str), "%Lu", (unsigned long long)(statbuf.st_atime));
    n = send(fd, str, strlen(str)+1, 0);
    snprintf(str, sizeof(str), "%Lu", (unsigned long long)(statbuf.st_mtime));
    n = send(fd, str, strlen(str)+1, 0);
    snprintf(str, sizeof(str), "%Lu", (unsigned long long)(statbuf.st_nlink));
    n = send(fd, str, strlen(str)+1, 0);
    snprintf(str, sizeof(str), "%Lu", (unsigned long long)(statbuf.st_dev));
    n = send(fd, str, strlen(str)+1, 0);
    snprintf(str, sizeof(str), "%Lu", (unsigned long long)(statbuf.st_rdev));
    n = send(fd, str, strlen(str)+1, 0);
    return 0;
}

int client_reqattr(int fd, const char *path, struct stat *s)
{
    char info[M_MAX_VAL];
    ssize_t n;
    n = send(fd, path, strlen(path)+1, 0);
    if (n <= 0) {
        perror("client_reqattr: send");
        return -1;
    }
    n = recv_str(fd, info, sizeof(info)-1, 0);
	if (strncasecmp(info, RESERVED_STR_TERM, strlen(RESERVED_STR_TERM)) == 0) {
        return -1;
    }
    if ((s != NULL) && (n > 0)) {
        s->st_size = atoll(info);
    }
    n = recv_str(fd, info, sizeof(info)-1, 0);
    if ((s != NULL) && (n > 0)) {
        s->st_mode = atoll(info);
    }
    n = recv_str(fd, info, sizeof(info)-1, 0);
    if ((s != NULL) && (n > 0)) {
        s->st_ctime = atoll(info);
    }
    n = recv_str(fd, info, sizeof(info)-1, 0);
    if ((s != NULL) && (n > 0)) {
        s->st_atime = atoll(info);
    }
    n = recv_str(fd, info, sizeof(info)-1, 0);
    if ((s != NULL) && (n > 0)) {
        s->st_mtime = atoll(info);
    }
    n = recv_str(fd, info, sizeof(info)-1, 0);
    if ((s != NULL) && (n > 0)) {
        s->st_nlink = atoll(info);
    }
    n = recv_str(fd, info, sizeof(info)-1, 0);
    if ((s != NULL) && (n > 0)) {
        s->st_dev = atoll(info);
    }
    n = recv_str(fd, info, sizeof(info)-1, 0);
    if ((s != NULL) && (n > 0)) {
        s->st_rdev = atoll(info);
    }
    return 0;
}

//////////////////////////////////////////////////////////////////////
// READ, WRITE, MOVE, REMOVE AND COPY
//////////////////////////////////////////////////////////////////////
int server_utime(int fd, UDTSOCKET ufd)
{
    char path[M_MAX_VAL];
    int ret = 0, n = 0;

    n = recv_str(fd, path, sizeof(path)-1, 0);
    if (n <= 0) {
        fprintf(stderr, "server_sendsegment: recv error\n");
        return -1;
    }
    char* path_local  = path_to_local(path);

    char command[256];
    snprintf(command, sizeof command, "touch %s", path_local);
    system(command);
    return ret;
}

int server_unlink(int fd, UDTSOCKET ufd)
{
    char path[M_MAX_VAL];
    int ret = 0, n = 0;

    n = recv_str(fd, path, sizeof(path)-1, 0);
    if (n <= 0) {
        fprintf(stderr, "server_sendsegment: recv error\n");
        return -1;
    }

    char* path_local  = path_to_local(path);
    ret = unlink(path_local);
    return ret;
}

int server_rename(int fd, UDTSOCKET ufd)
{
    char path[M_MAX_VAL];
    char newpath[M_MAX_VAL];
    int ret = 0, n = 0;

    n = recv_str(fd, path, sizeof(path)-1, 0);
    if (n <= 0) {
        fprintf(stderr, "server_sendsegment: recv error\n");
        return -1;
    }

    n = recv_str(fd, newpath, sizeof(newpath)-1, 0);
    if (n <= 0) {
        fprintf(stderr, "server_sendsegment: recv error\n");
        return -1;
    }

    char* path_local  = path_to_local(path);
    char* newpath_local  = path_to_local(newpath);

    rename(path_local,newpath_local);
    return 0;
}

int server_truncate(int fd, UDTSOCKET ufd)
{
    char path[M_MAX_VAL];
    char info[M_MAX_VAL];
    off64_t newsize;
    int ret = 0, n = 0;

    n = recv_str(fd, path, sizeof(path)-1, 0);
    if (n <= 0) {
        fprintf(stderr, "server_sendsegment: recv error\n");
        return -1;
    }

    n = recv_str(fd, info, sizeof(info)-1, 0);
    if (n <= 0) {
        fprintf(stderr, "server_sendsegment: recv error\n");
        return -1;
    }
    newsize = atoll(info);

    ret = truncate(path, newsize);

    return ret;
}

int server_write(int fd, UDTSOCKET ufd)
{
    char path[M_MAX_VAL];
    char data[M_MAX_VAL];
    char info[M_MAX_VAL];
    size_t size;
    off_t offset;
    int ret = 0, n = 0;

    n = recv_str(fd, path, sizeof(path)-1, 0);
    if (n <= 0) {
        fprintf(stderr, "server_sendsegment: recv error\n");
        return -1;
    }

    n = recv_str(fd, data, sizeof(path)-1, 0);
    if (n <= 0) {
        fprintf(stderr, "server_sendsegment: recv error\n");
        return -1;
    }

    n = recv_str(fd, info, sizeof(info)-1, 0);
    if (n <= 0) {
        fprintf(stderr, "server_sendsegment: recv error\n");
        return -1;
    }
    size = atoll(info);

    n = recv_str(fd, info, sizeof(info)-1, 0);
    if (n <= 0) {
        fprintf(stderr, "server_sendsegment: recv error\n");
        return -1;
    }
    offset = atoll(info);

    FILE* fp = fopen(path, "a");
    fputs(data, fp);
    fclose(fp);

    return 0;
}


int server_sendsegment(int fd, UDTSOCKET ufd)
{
    char info[M_MAX_VAL];
    char filename[M_MAX_VAL];
    off64_t offset;
    size_t len, n;

    /* Receive the request */
    n = recv_str(fd, filename, sizeof(filename)-1, 0);
    if (n <= 0) {
        fprintf(stderr, "server_sendsegment: recv error\n");
        return -1;
    }
    n = recv_str(fd, info, sizeof(info)-1, 0);
    if (n <= 0) {
        fprintf(stderr, "server_sendsegment: recv error\n");
        return -1;
    }
    offset = atoll(info);
    n = recv_str(fd, info, sizeof(info)-1, 0);
    if (n <= 0) {
        fprintf(stderr, "server_sendsegment: recv error\n");
        return -1;
    }
    len = atol(info);

    /* Open the file */ 
    FILE* file = fopen(filename, "rb");

    /* UDT send the data */
    UDT::TRACEINFO trace;
    UDT::perfmon(ufd, &trace);
    off64_t sent = UDT::sendfile(ufd, file, offset, len);
    
    fclose(file);
    if (sent < 0) {
        return -1;
    }

    /* UDT send garbage in plae of data that was requested beyond the EOF */
    if (sent < len) {
        fprintf(stderr, "server_sendsegment: UDT short send, %Lu vs %Lu!\n", (ull_t)sent, (ull_t)len);
        std::fstream fzeros("/dev/zero", std::fstream::in |std::fstream::binary);
        UDT::sendfile(ufd, fzeros, 0, len-sent);
        fzeros.close();
    }
    UDT::perfmon(ufd, &trace);

    return 0;
}

int client_reqsegment(int fd, UDTSOCKET ufd, const char* filename, off64_t offset, size_t len)
{
    char info[M_MAX_VAL];
    ssize_t n;
    n = send(fd, filename, strlen(filename)+1, 0);
    snprintf(info, M_MAX_VAL, "%Lu", (unsigned long long)offset);
    n = send(fd, info, strlen(info)+1, 0);
    snprintf(info, M_MAX_VAL, "%Lu", (unsigned long long)len);
    n = send(fd, info, strlen(info)+1, 0);
    return 0;
}

int client_recvsegment(int fd, UDTSOCKET ufd, size_t len, char* buf)
{
    ssize_t n;
    size_t remain = len;
    while (remain > 0) {
        n = UDT::recv(ufd, buf, remain, 0);
        if (UDT::ERROR == n) {
            fprintf(stderr, "client_recvsegment: UDT::recv error\n");
            return -1;
        }
        remain -= n;
        buf += n;
    }
    return 0;
}

int client_reqtruncate(int fd, UDTSOCKET ufd, const char* filename, off64_t newsize)
{
    char info[M_MAX_VAL];
    ssize_t n;
    n = send(fd, filename, strlen(filename)+1, 0);
    snprintf(info, M_MAX_VAL, "%Lu", (unsigned long long)newsize);
    n = send(fd, info, strlen(info)+1, 0);
    return 0;
}

int client_reqwrite (int fd, UDTSOCKET ufd, const char* filename, const char *data, size_t size,
                                                                             off_t offset)
{
    char info[M_MAX_VAL];
    ssize_t n;
    n = send(fd, filename, strlen(filename)+1, 0);
    n = send(fd, data, strlen(data)+1, 0);
    snprintf(info, M_MAX_VAL, "%Lu", (unsigned long long)size);
    n = send(fd, info, strlen(info)+1, 0);
    snprintf(info, M_MAX_VAL, "%Lu", (unsigned long long)offset);
    n = send(fd, info, strlen(info)+1, 0);
    return 0;
}


int client_reqrename (int fd, UDTSOCKET ufd,const char* path, const char* newpath)
{
    ssize_t n;
    n = send(fd, path, strlen(path)+1, 0);
    n = send(fd, newpath, strlen(newpath)+1, 0);
    return 0;
}

int client_requnlink (int fd, UDTSOCKET ufd,const char* path)
{
    ssize_t n;
    n = send(fd, path, strlen(path)+1, 0);
    return 0;
}

int client_requtime (int fd, UDTSOCKET ufd,const char* path)
{
    ssize_t n;
    n = send(fd, path, strlen(path)+1, 0);
    return 0;
}