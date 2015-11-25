CFLAGS = -Wall -O2 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -I../UDT4/src/ -pthread
LDFLAGS = -L../UDT4/src/ ../UDT4/src/libudt.a
CC = g++

all: udtfs_server udtfs client

udtfs_server: common.o udtfs_server.cpp
	$(CC) $(CFLAGS) udtfs_server.cpp common.o -o udtfs_server $(LDFLAGS)

client: common.o client.cpp
	$(CC) $(CFLAGS) client.cpp common.o -o client $(LDFLAGS)

udtfs: common.o udtfs.cpp
	$(CC) $(CFLAGS) udtfs.cpp common.o -o udtfs -lfuse $(LDFLAGS)

common.o: common.cpp
	$(CC) -c $(CFLAGS) common.cpp

clean:
	rm -rf *.o client udtfs_server udtfs

install: udtfs_server
	cp udtfs_server /usr/local/bin
	cp udtfs /usr/local/bin
