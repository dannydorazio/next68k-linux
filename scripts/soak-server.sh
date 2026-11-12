#!/bin/sh
# Serve the data for tests/soak.sh to the emulated NeXT: a 20 MB random file
# (big), its MD5 sum (big.md5) and soak.sh itself, from out/soak, over HTTP
# on the host's loopback port 8765, which the NeXT reaches as 10.0.2.2:8765.
# Runs in the foreground.
#
# On the NeXT:
#   exec 3<>/dev/tcp/10.0.2.2/8765; printf 'GET /soak.sh HTTP/1.0\r\n\r\n' >&3
#   sed '1,/^\r$/d' <&3 > soak.sh; bash soak.sh 8 &
#
# usage: soak-server.sh
set -e

top=$(cd "$(dirname "$0")/.." && pwd)
dir=$top/out/soak

mkdir -p "$dir"
if [ ! -f "$dir/big" ]; then
	head -c 20000000 /dev/urandom > "$dir/big"
	(cd "$dir" && md5 -q big > big.md5 2>/dev/null || md5sum big > big.md5)
fi
cp "$top/tests/soak.sh" "$dir/"

cd "$dir"
exec python3 -m http.server 8765 --bind 127.0.0.1
