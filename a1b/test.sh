cd /tmp/mnt
ls -al 
mkdir new
stat new 
ls -al 
cd new
mkdir new1
ls -al 
cd ..
mkdir hello
ls -al
touch ./new
touch ./new/new1
cd ./new
touch ../hello
stat ./new1
stat ../hello
stat ../new
cd ..
echo "hello,world" > newfile
cat newfile
echo "the next world" >> newfile
cat newfile
unlink newfile 
ls -al
rmdir hello
ls -al
mkdir moveto
mkdir movefrom
mv movefrom moveto
ls -al
cd moveto
ls -al
cd ..
echo "hello,world" > newfile