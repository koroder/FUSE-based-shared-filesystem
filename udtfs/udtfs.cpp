/*
* A simple fuse based shared filesystem
* Author: Abdul Hadi Shakir & Bharat Ratan
* Email: cs5100202@cse.iitd.ac.in
*/

#include <stdlib.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>

#include <udt.h>
#include "common.h"

//////////////////////////////////////////////////////////////////////
// FUSE
//////////////////////////////////////////////////////////////////////

#define FUSE_USE_VERSION 26
extern "C" {
  #include <fuse.h>
}

static struct fuse_operations _udtfs_oper;
typedef unsigned long long ull_t;

//////////////////////////////////////////////////////////////////////
// GLOBALS
//////////////////////////////////////////////////////////////////////

#define MAX_CACHE_SIZE (32*1024*1024)

static int _file_is_open = 0;
static char _file_name[M_MAX_VAL];
static struct stat _file_stats;

static char* _cache;
static size_t _cache_len;
static off_t  _cache_offset;

static int _fd = 0;
static UDTSOCKET _ufd;

static pthread_mutex_t _tcpmutex;
static pthread_mutex_t _udtmutex;

//////////////////////////////////////////////////////////////////////
// FILE SYSTEM - GENERAL STUFF
//////////////////////////////////////////////////////////////////////

static int udtfs_getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));
    pthread_mutex_lock(&_tcpmutex);
    if (send_cmd(_fd, "getattr") < 0) {
        pthread_mutex_unlock(&_tcpmutex);
        return -ENOENT;
    }
    if (client_reqattr(_fd, path, stbuf) != 0) { 
        pthread_mutex_unlock(&_tcpmutex);
        return -ENOENT;
    }
    pthread_mutex_unlock(&_tcpmutex);
//    stbuf->st_mode &= ~(S_IWUSR|S_IWGRP|S_IWOTH);
    return 0;
}

static int udtfs_opendir(const char *path, struct fuse_file_info *fi)
{
    // what should this return...?
    return 0;
}

static int udtfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;
    char* entry;
    pthread_mutex_lock(&_tcpmutex);
    if (send_cmd(_fd, "dir") < 0) { 
        pthread_mutex_unlock(&_tcpmutex);
        return -ENOENT; 
    }
    if (client_reqdir(_fd, path) != 0) { 
        pthread_mutex_unlock(&_tcpmutex);
        return -ENOENT; 
    }
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    while ((entry = client_recvdirentry(_fd)) != NULL) {
        filler(buf, entry, NULL, 0);
        free(entry);
    }
    pthread_mutex_unlock(&_tcpmutex);
    return 0;
}

static int udtfs_open(const char *path, struct fuse_file_info *fi)
{
    if (_file_is_open) {
        fprintf(stderr, "udtfs_open: another file (%20s) is currently open\n", _file_name);
        return -EIO;
    }
    if (path[0]=='/') { path++; }
    if (udtfs_getattr(path, &_file_stats) != 0) { return -ENOENT; }
    strncpy(_file_name, path, sizeof(_file_name));
    _file_is_open = 1;
    return 0;
}

static int udtfs_release(const char *path, struct fuse_file_info *fi)
{
    if (_file_is_open) {
        if (path[0]=='/') { path++; }
        if (strncmp(path, _file_name, sizeof(_file_name)) == 0) { 
            // fprintf(stderr, "udtfs_release: file closed (%20s)\n", _file_name);
            _file_is_open = 0; 
            _cache_offset = 0;
            _cache_len = 0;
        }
    }
    return 0;
}

//////////////////////////////////////////////////////////////////////
// FILE SYSTEM - READING AND WRITING A FILE
//////////////////////////////////////////////////////////////////////

static int udtfs_read(const char *path, char *buf, size_t size, off64_t offset,
                      struct fuse_file_info *fi)
{
    /* Crop at EOF */
    size_t actualsize = size;
    if ((_file_stats.st_size - offset) < size) {
        actualsize = _file_stats.st_size - offset;
    }
    if (actualsize <= 0) {
        return 0;
    }

    /* Refill the cache if necessary */
    if ((offset < _cache_offset) || ((offset + actualsize) > (_cache_offset + _cache_len))) {

        size_t fetchsize = MAX_CACHE_SIZE;
        if (_file_stats.st_size < MAX_CACHE_SIZE) {
            fetchsize = _file_stats.st_size;
        }

        pthread_mutex_lock(&_tcpmutex);
        if (send_cmd(_fd, "read") < 0) { 
            fprintf(stderr,"udtfs_read: cmd send error"); 
            pthread_mutex_unlock(&_tcpmutex);
            return -ENOENT; 
        }
        pthread_mutex_lock(&_udtmutex);
        client_reqsegment(_fd, _ufd, _file_name, offset, fetchsize);
        pthread_mutex_unlock(&_tcpmutex); 
        client_recvsegment(_fd, _ufd, fetchsize, _cache);
        pthread_mutex_unlock(&_udtmutex);
        _cache_offset = offset;
        _cache_len = fetchsize;

    }

    /* Return the data */
    memcpy(buf, _cache + (offset-_cache_offset), actualsize);
    return actualsize;
}

static int udtfs_truncate(const char *path, off_t newsize)
{
    if (send_cmd(_fd, "truncate") < 0) { 
            fprintf(stderr,"udtfs_read: cmd send error"); 
            pthread_mutex_unlock(&_tcpmutex);
            return -ENOENT; 
    }

    client_reqtruncate(_fd, _ufd, _file_name, newsize);

    return newsize;
}

static int udtfs_write (const char *path, const char *data, size_t size, off_t offset,
              struct fuse_file_info *fi)
{
    if (send_cmd(_fd, "write") < 0) { 
            fprintf(stderr,"udtfs_read: cmd send error"); 
            pthread_mutex_unlock(&_tcpmutex);
            return -ENOENT; 
    }

    client_reqwrite(_fd, _ufd, _file_name, data, size, offset);

    return size;
}

/////////////////////////////////////////////////////////////////////
// MOVE COMMAND
/////////////////////////////////////////////////////////////////////

static int udtfs_rename (const char *path, const char *newpath)
{
    printf("rename called!!\n");
    printf("path: %s, newpath: %s, _file_name: %s\n",path, newpath, _file_name);
    if (send_cmd(_fd, "rename") < 0) { 
            fprintf(stderr,"udtfs_read: cmd send error"); 
            pthread_mutex_unlock(&_tcpmutex);
            return -ENOENT; 
    }
    client_reqrename(_fd, _ufd, path, newpath);
    return 0;
}

/////////////////////////////////////////////////////////////////////
// REMOVE COMMAND
/////////////////////////////////////////////////////////////////////

static int udtfs_unlink (const char *path)
{
    printf("unlink called!!\n");
    printf("path: %s, _file_name: %s\n",path, _file_name);
    if (send_cmd(_fd, "unlink") < 0) { 
            fprintf(stderr,"udtfs_read: cmd send error"); 
            pthread_mutex_unlock(&_tcpmutex);
            return -ENOENT; 
    }
    client_requnlink(_fd, _ufd, path);
    return 0;
}

/////////////////////////////////////////////////////////////////////
// TOUCH COMMAND
/////////////////////////////////////////////////////////////////////

static int udtfs_utimens(const char *path, const struct timespec tv[2])
{
    printf("utime called!!\n");
    if (send_cmd(_fd, "utime") < 0) { 
            fprintf(stderr,"udtfs_read: cmd send error"); 
            pthread_mutex_unlock(&_tcpmutex);
            return -ENOENT; 
    }
    client_requtime(_fd, _ufd, path);
    return 0;
}

//////////////////////////////////////////////////////////////////////
// MAIN
//////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
    char* fuseargv[64];
    int fuseargc = 0;
    char* hostname = NULL;
    int i, rc;

    /* Fill the operations struct */
    memset(&_udtfs_oper, 0, sizeof(struct fuse_operations));
    _udtfs_oper.getattr = udtfs_getattr;
    _udtfs_oper.readdir = udtfs_readdir;
    _udtfs_oper.opendir = udtfs_opendir;
    _udtfs_oper.open = udtfs_open;
    _udtfs_oper.release = udtfs_release;
    _udtfs_oper.read = udtfs_read;
    _udtfs_oper.truncate = udtfs_truncate;
    _udtfs_oper.write = udtfs_write;
    _udtfs_oper.rename = udtfs_rename;
    _udtfs_oper.unlink = udtfs_unlink;
    _udtfs_oper.utimens = udtfs_utimens;

    /* Handle the arguments for tfs and fuse */
    if ((argc < 3) || (argv[1][0]=='-') || (argv[2][0]=='-')) {
        printf("VERY FEW ARGUMENTS\n");
        return -1;
    }
    fuseargv[fuseargc++] = strdup(argv[0]);
    hostname = strdup(argv[1]);
    for (i = 2; i < argc; i++) {
        fuseargv[fuseargc++] = strdup(argv[i]);
    }

    /* Allocate our cache(s) */
    _cache = (char*)memalign(128, MAX_CACHE_SIZE);
    if (_cache == NULL) {
        printf("Malloc for %d-byte cache failed!\n", MAX_CACHE_SIZE);
        return -1;
    }
    _cache_offset = 0;
    _cache_len = 0;    

    /* Prepare the connection */
    if ((_fd = client_open_socket(hostname)) < 0) {
        return -1;
    }
    if (exchange_versions(_fd) != 0) {
        close(_fd);
        return -1;
    }
    UDT::startup();
    _ufd = client_connect_udt(_fd);
    pthread_mutex_init(&_tcpmutex, NULL);
    pthread_mutex_init(&_udtmutex, NULL);

    /* Provide the file system */
    rc = fuse_main(fuseargc, fuseargv, &_udtfs_oper, NULL);
    
    close_socket(_fd);
    UDT::cleanup();
    pthread_mutex_destroy(&_tcpmutex);
    pthread_mutex_destroy(&_udtmutex);
    return rc;
}
