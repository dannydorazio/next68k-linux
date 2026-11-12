#!/bin/sh
# Time a download from the host to the running NeXT over HTTP: FILE from
# out/www (a 20 MB random file "big" by default, made on first use) goes to
# the NeXT's /dev/shm through bash's /dev/tcp, since the Debian image has
# no wget or curl.  Prints the time, checks the MD5 and shows the eth0
# counters.  The NeXT reaches the host as 10.0.2.2; this serves out/www on
# 127.0.0.1:8765 if nothing does yet.
#
# usage: time-download.sh [FILE]
set -e

top=$(cd "$(dirname "$0")/.." && pwd)
cd "$top"
www=$top/out/www
f=${1:-big}
mkdir -p "$www"
if [ "$f" = big ] && [ ! -f "$www/big" ]; then
	head -c 20000000 /dev/urandom > "$www/big"
fi
if ! curl -sf -o /dev/null "http://127.0.0.1:8765/$f"; then
	nohup python3 -m http.server 8765 --bind 127.0.0.1 -d "$www" \
		> out/www.log 2>&1 &
	sleep 2
fi

echo "host md5: $( (md5 -q "$www/$f" 2>/dev/null || md5sum "$www/$f") | cut -d' ' -f1)"
python3 scripts/serial-run.py --login --timeout 900 \
	"time (exec 3<>/dev/tcp/10.0.2.2/8765; printf 'GET /$f HTTP/1.0\r\n\r\n' >&3; sed '1,/^\r\$/d' <&3 > /dev/shm/dl; exec 3<&-); md5sum /dev/shm/dl; rm /dev/shm/dl" \
	"ip -s -s link show eth0 | sed -n 3,10p" 2>&1 |
	sed 's/\x1b\][^\\]*\\//g; s/\x1b\[?2004[hl]//g; s/\x1b\[[0-9;]*m//g' |
	grep -E "real|user|sys|dl$|RX|TX|^ +[0-9]"
