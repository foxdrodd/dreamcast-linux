#!/bin/bash
# Change Dockerfile and rebuild, this is a hack for quick iteration.

buildtime=$(date +%s)

old="hello chroot.sh"
oldfound=$(grep -c "$old" dreamcast/Dockerfile)

if [ $oldfound = 1 ]; then
	sed -i 's/hello chroot.sh/chroot.sh hello/' dreamcast/Dockerfile
else
	sed -i 's/chroot.sh hello/hello chroot.sh/' dreamcast/Dockerfile
fi

make base dreamcast

# cp -vf build/data.iso archive/data.iso.$buildtime
# cp -vf build/linux614.cdi archive/linux616.cdi.$buildtime

/opt/toolchains/dc/bin/cdi4dc build/data.iso build/linux616.cdi

modified=$(stat -c %y build/linux614.cdi)
echo $buildtime $modified $* >> log_build.txt
