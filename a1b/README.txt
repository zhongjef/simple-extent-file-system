We took suggestions from the TA's feedback, and made the following
amendments to our project proposal:
    - Block allocation algorithm:
        - Loop through the bitmap, find the largest number of
        continguous chunk of blocks, make it extent1 for the file,
        if the file need more blocks, loop again.

    - Inode number 0 is reserved for free directory entries, that
    is, every free directory entry would have an inode number of 0