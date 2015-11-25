/************************************************************************
 * UDTFS -- A FUSE file system based on UDTv4 
 * Copyright (C) 2009 Jan Wagner, Metsahovi Radio Observatory, Aalto
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 **************************************************************************/
#ifndef COMMON_H
#define COMMON_H

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
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <udt.h> // C++

#define M_VERSION "UDTFS V1.0 - A file system based on FUSE and UDTv4"
#define M_COPYRIGHT "(C) 2009 Jan Wagner, Metsahovi Radio Observatory, Aalto"
#define M_LICENSE "Licensed under GNU GPL v3"

#define M_PORT "1432"
#define M_PORT_UDTBASE 9000
#define M_MTU_UDT 1500 // 9000
#define M_UDT_RATE 1000 // Mbps

#define M_BACKLOG 10
#define M_MAX_CLIENTS 16
#define M_MAX_PATH 512
#define M_MAX_FILE 128
#define M_MAX_VAL  128

#define RESERVED_STR_TERM "--TERM--"

int recv_str(int fd, char* buf, int maxlen, int flags);
int send_cmd(int fd, const char* str);

int server_open_socket();
int client_open_socket(char* hostname);
void close_socket(int fd);

UDTSOCKET server_accept_udt(int tcp_fd, const int udtport);
UDTSOCKET client_connect_udt(int tcp_fd);

int exchange_versions(int fd);

int client_reqdir(int fd, const char* pathname);
int server_senddir(int fd);
char* client_recvdirentry(int fd);

int client_reqattr(int fd, const char *path, struct stat *stbuf);
int server_sendattr(int fd);

int server_sendsegment(int fd, UDTSOCKET ufd);
int client_reqsegment(int fd, UDTSOCKET ufd, const char* filename, off64_t offset, size_t len);
int client_recvsegment(int fd, UDTSOCKET ufd, size_t len, char* buf);

int server_truncate(int fd, UDTSOCKET ufd);
int server_write(int fd, UDTSOCKET ufd);
int client_reqtruncate(int fd, UDTSOCKET ufd, const char* filename, off64_t newsize);
int client_reqwrite (int fd, UDTSOCKET ufd, const char* filename, const char *data, size_t size,
                                                                             off_t offset);
int server_rename(int fd, UDTSOCKET ufd);
int client_reqrename (int fd, UDTSOCKET ufd, const char* path, const char* newpath);     

int server_unlink(int fd, UDTSOCKET ufd);
int client_requnlink(int fd, UDTSOCKET ufd, const char* path);

int server_utime(int fd, UDTSOCKET ufd);
int client_requtime (int fd, UDTSOCKET ufd,const char* path);

#endif // COMMON_H
