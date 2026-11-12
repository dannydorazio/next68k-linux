#!/bin/sh
# Time how fast the framebuffer console scrolls: write 104 lines (two
# screens at 140x52) of full-width text, then of short text, to a virtual
# terminal, and print the time per line of each.
#
# usage: scrollbench.sh [TTY]   (default /dev/tty1)
tty=${1:-/dev/tty1}
dir=$(mktemp -d)
long="The quick brown fox jumps over the lazy dog; 0123456789 PACK MY BOX WITH FIVE DOZEN LIQUOR JUGS. abcdefghijklmnopqrstuvwxyz ++ -- == ** //"
i=0
while [ $i -lt 104 ]; do
	echo "$long" >> "$dir/long"
	echo "line $i" >> "$dir/short"
	i=$((i + 1))
done

for f in long short; do
	start=$(date +%s%N)
	cat "$dir/$f" > "$tty"
	end=$(date +%s%N)
	echo "$f: $(( (end - start) / 104000000 )) ms per line"
done
rm -r "$dir"
