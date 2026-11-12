#!/bin/bash
# Soak test for Debian on the NeXT: network, disk and apt load in a loop,
# checking the data and the kernel log.  Needs scripts/soak-server.sh on the
# host, which serves a 20 MB file and its MD5 sum.
#
# Each round:
#   - download the file from the host and check it (network, receive DMA);
#   - copy it to the disk, drop the page cache and check the copy (SCSI
#     write and read DMA);
#   - read 32 MB straight from sda1;
#   - every APT_EVERY rounds, a simulated apt-get dist-upgrade (CPU, memory,
#     swap);
#   - log new kernel warnings and errors.
# apt-get update runs once at the start.  Results go to /root/soak.log; the
# last line says whether anything failed.
#
# usage: soak.sh [HOURS] [HOST:PORT]   (default 8, 10.0.2.2:8765)
# For a quick check: DURATION=seconds overrides HOURS, SKIP_UPDATE=1 skips
# apt-get update, APT_EVERY=n changes how often apt runs.

hours=${1:-8}
server=${2:-10.0.2.2:8765}
host=${server%:*}
port=${server#*:}
apt_every=${APT_EVERY:-6}
log=/root/soak.log
work=/root/soak
end=$(( $(date +%s) + ${DURATION:-$((hours * 3600))} ))
round=0
failures=0

mkdir -p "$work"
exec >> "$log" 2>&1

note() {
	echo "$(date '+%F %T') $*"
}

# Fetch /NAME from the host into FILE, without the HTTP headers.
fetch() {
	local name=$1 file=$2 len

	exec 3<>"/dev/tcp/$host/$port" || return 1
	printf 'GET /%s HTTP/1.0\r\n\r\n' "$name" >&3
	cat <&3 > "$file.http"
	exec 3<&-
	len=$(sed -n 's/^Content-Length: *\([0-9]*\).*/\1/p' "$file.http" | head -1)
	[ -n "$len" ] || return 1
	tail -c "$len" "$file.http" > "$file"
	rm -f "$file.http"
}

check() {
	local what=$1 ok=$2

	if [ "$ok" = 0 ]; then
		note "round $round: $what ok"
	else
		note "round $round: $what FAILED"
		failures=$((failures + 1))
	fi
}

kernel_problems() {
	dmesg --level=emerg,alert,crit,err,warn | wc -l
}

note "soak: $(( (end - $(date +%s)) / 60 )) minutes, server $server, apt every $apt_every rounds"
fetch big.md5 "$work/big.md5"; check "fetch checksum" $?
expected=$(cut -d' ' -f1 "$work/big.md5")
base=$(kernel_problems)
note "kernel warnings and errors so far: $base"

if [ -z "$SKIP_UPDATE" ]; then
	apt-get update -q > "$work/apt-update.log" 2>&1; check "apt-get update" $?
fi

while [ "$(date +%s)" -lt "$end" ]; do
	round=$((round + 1))
	start=$(date +%s)

	fetch big "$work/big"
	[ "$(md5sum < "$work/big" | cut -d' ' -f1)" = "$expected" ]
	check "network (20 MB)" $?

	cp "$work/big" "$work/copy" && sync &&
		echo 3 > /proc/sys/vm/drop_caches &&
		[ "$(md5sum < "$work/copy" | cut -d' ' -f1)" = "$expected" ]
	check "disk write and read back" $?
	rm -f "$work/copy"

	dd if=/dev/sda1 of=/dev/null bs=64k count=512 2> /dev/null
	check "disk read (32 MB)" $?

	if [ $((round % apt_every)) = 0 ]; then
		apt-get -s -q dist-upgrade > "$work/apt-upgrade.log" 2>&1
		check "apt-get -s dist-upgrade" $?
	fi

	now=$(kernel_problems)
	if [ "$now" != "$base" ]; then
		note "round $round: new kernel warnings or errors:"
		dmesg --level=emerg,alert,crit,err,warn | tail -n $((now - base))
		failures=$((failures + now - base))
		base=$now
	fi
	note "round $round took $(( $(date +%s) - start ))s"
done

note "soak finished: $round rounds, $failures failures"
