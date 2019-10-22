make all
truncate -s 409600 disk.img
./mkfs.a1fs -i 30 disk.img
mkdir /tmp/mnt
./a1fs disk.img /tmp/mnt