#!/bin/sh
# vramblk integrity helper, runs on the Dreamcast.
#   vbtest.sh new                 new 1 MiB random pattern in $T/pat
#   vbtest.sh write W [direct|buffered]   write pattern to all 6 slots, wmode W
#   vbtest.sh verify R [direct|buffered]  read all 6 slots back, rmode R
P=/sys/module/vramblk/parameters
D=/dev/vramblk0
T=/tmp/vt
cmd=$1 mode=$2 io=${3:-direct}
case $cmd in
new)
	[ -f $T/pat ] && mv $T/pat $T/prev
	dd if=/dev/urandom of=$T/pat bs=64k count=16 2>/dev/null
	md5sum $T/pat ;;
write)
	echo $mode > $P/wmode
	i=0
	while [ $i -lt 6 ]; do
		if [ $io = direct ]; then
			dd if=$T/pat of=$D bs=64k seek=$((i*16)) oflag=direct 2>/dev/null
		else
			dd if=$T/pat of=$D bs=64k seek=$((i*16)) conv=notrunc 2>/dev/null
		fi
		i=$((i+1))
	done
	[ $io = direct ] || { sync; echo 3 > /proc/sys/vm/drop_caches; }
	echo "wrote 6 slots wmode=$(cat $P/wmode) $io" ;;
verify)
	echo $mode > $P/rmode
	[ $io = direct ] || echo 3 > /proc/sys/vm/drop_caches
	i=0 res=""
	while [ $i -lt 6 ]; do
		if [ $io = direct ]; then
			dd if=$D of=$T/rd bs=64k skip=$((i*16)) count=16 iflag=direct 2>/dev/null
		else
			dd if=$D of=$T/rd bs=64k skip=$((i*16)) count=16 2>/dev/null
		fi
		if cmp -s $T/pat $T/rd; then
			res="$res ok"
		else
			n=$(cmp -l $T/pat $T/rd | wc -l)
			res="$res BAD$i:$n"
			cp $T/rd $T/bad$i
		fi
		i=$((i+1))
	done
	echo "verify rmode=$(cat $P/rmode) $io:$res" ;;
analyze)
	# which content do the bad 32-byte lines of bad$2 hold?
	b=$T/bad$2
	cmp -l $T/pat $b | awk '{print int(($1-1)/32)}' | uniq > $T/l.bad
	echo "bad lines: $(wc -l < $T/l.bad)"
	dd if=/dev/zero of=$T/ref bs=64k count=1 2>/dev/null
	dd if=$T/pat bs=64k count=15 2>/dev/null >> $T/ref
	for r in prev ref zero; do
		[ $r = zero ] && dd if=/dev/zero of=$T/ref bs=64k count=16 2>/dev/null
		f=$T/$r; [ $r = zero ] && f=$T/ref
		cmp -l $f $b | awk '{print int(($1-1)/32)}' | uniq > $T/l.ref
		# bad lines that do NOT differ from the reference = match it
		m=$(grep -v -x -F -f $T/l.ref $T/l.bad | wc -l)
		echo "bad lines equal to $r: $m"
	done
	rm -f $T/ref $T/l.ref ;;
esac
