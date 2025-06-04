#!/bin/bash
# Change Dockerfile and rebuild, this is a hack for quick iteration.

buildtime=$(date +%s)

old="hello sh-boot-20010831-1455.tar.gz"
oldfound=$(grep -c $old dreamcast/Dockerfile)

if [ $oldfound = 1 ]; then
	sed -i 's/hello sh-boot-20010831-1455.tar.gz/sh-boot-20010831-1455.tar.gz hello/' dreamcast/Dockerfile
else
	sed -i 's/sh-boot-20010831-1455.tar.gz hello/hello sh-boot-20010831-1455.tar.gz/' dreamcast/Dockerfile
fi

make base dreamcast

cp -vf build/data.iso archive/data.iso.$buildtime
cp -vf build/linux614.cdi archive/linux614.cdi.$buildtime

/opt/toolchains/dc/bin/cdi4dc build/data.iso build/linux614.cdi

modified=$(stat -c %y build/linux614.cdi)
echo $buildtime $modified $* >> log_build.txt
