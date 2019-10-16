make all
truncate -s 65536 disk.img
./mkfs.a1fs -i 10 disk.img
./a1fs disk.img /tmp/mnt