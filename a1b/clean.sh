fusermount -u /tmp/mnt
./a1fs disk.img /tmp/mnt
make clean
cd /tmp/mnt
cat newfile
cd 
rmdir /tmp/mnt