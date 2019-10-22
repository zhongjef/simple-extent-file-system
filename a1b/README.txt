We took suggestions from the TA's feedback, and made the following
amendments to our project proposal:
    - Block allocation algorithm:
        - Loop through the bitmap, find the largest number of
        continguous chunk of blocks, make it extent1 for the file,
        if the file need more blocks, loop again.

    - Inode number 0 is reserved for free directory entries, that
    is, every free directory entry would have an inode number of 0


We have 3 shell scripts: runit.sh test.sh clean.sh

runit.sh makes the files, truncate a image, format the disk and mount to /tmp/mount

test.sh performs operations and demonstrates some functionality of our system

clean.sh unmount the image and mount it again and shows the state of our system

Please run the shell scripts in the following order: runit.sh test.sh clean.sh