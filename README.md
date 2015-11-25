# FUSE-based-shared-filesystem
This is a FUSE based shared filesystem. The server needs to be mounted on one system. All the other clients then connect to this server for mounting this file system. It provides real time consistency across nodes.

USAGE:
$ make
AT SERVER END:
$ udtfs_server <shared_dir>
eg. /home/shakir/Desktop/asgnt3/udtfs/udtfs_server rootdir/

AT CLIENT END
$ udtfs <host_address> <local_mount_dir> -f
/home/shakir/Desktop/asgnt3/udtfs/udtfs localhost clientdir1/ -f
