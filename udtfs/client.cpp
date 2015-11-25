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
 **************************************************************************
 *
 * <client.cpp>
 *
 * A standalone test bench to test any changes made to common.cpp/.h
 * before adding them into the actual FUSE file system udtfs.cpp
 *
 **************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <udt.h>
#include "common.h"

int main(int argc, char** argv)
{
    int fd;
    char input[512];
    int n;
    UDTSOCKET ufd;

    if (argc != 2) {
        fprintf(stderr, "client <hostname>\n");
        return -1;
    }

    fd = client_open_socket(argv[1]);

    /* "auth" */
    if (exchange_versions(fd) != 0) {
        close(fd);
        return 0;
    }
    UDT::startup();
    ufd = client_connect_udt(fd);

    /* commands */
    while (1) {
        if (fgets(input, sizeof(input)-1, stdin) == NULL) { break; }
        n = strlen(input);
        if (n < 1) { continue; }
        if (input[n-1] == '\n') { input[n-1] = '\0'; }

        n = send(fd, input, strlen(input)+1, 0);
        if (n < (int)strlen(input)) {
            perror("talker: send()");
            break;
        }

        fprintf(stderr, "echo: '%s'\n", input);

        if (strcasecmp(input, "dir") == 0) {
            char* entry;
            if (client_reqdir(fd, ".") != 0) { continue; }
            while ((entry = client_recvdirentry(fd)) != NULL) {
                fprintf(stderr, " -- '%s'\n", entry);
                free(entry);
            }
        } else
        if (strcasecmp(input, "getattr") == 0) {
            struct stat s;
            if (client_reqattr(fd, "common.c", &s) != 0) { continue; }
            fprintf(stderr, " -- %lld byte, mode %lld\n", (unsigned long long)s.st_size, (unsigned long long)s.st_mode);
        } else
        if (strcasecmp(input, "read") == 0) {
            std::ifstream testfile("common.c");
            testfile.seekg(0, std::ios::end);
            int testlen = testfile.tellg();
            testlen += 16;
            char *buf = new char[testlen];
            client_reqsegment(fd, ufd, "common.c", 0, testlen);
            client_recvsegment(fd, ufd, testlen, buf);
            delete buf;
        }
    }

    close(fd);
    return 0;
}
