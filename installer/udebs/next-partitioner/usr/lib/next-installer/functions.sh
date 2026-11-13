# Shared by next-partitioner and next-installer (which depends on it).

NEXT_LIB=/usr/lib/next-installer

# The NeXT label name of the installer's own disk (build-installer.sh)
INSTALLER_LABEL="Debian installer"

# The name in the NeXT disk label of disk $1 (sda, ...), if it has one
label_name() {
	[ "$(dd if=/dev/$1 bs=4 count=1 2>/dev/null)" = dlV3 ] || return 0
	dd if=/dev/$1 bs=1 skip=12 count=24 2>/dev/null | tr -d '\000'
}

# The SCSI disks, in the kernel's order
scsi_disks() {
	for d in /sys/block/sd*; do
		[ -e "$d" ] && echo "${d##*/}"
	done | sort
}

installer_disk() {
	[ "$(label_name $1)" = "$INSTALLER_LABEL" ]
}

# The name disk $1 will have when the installer's disk is gone: the kernel
# names SCSI disks in the order of their IDs.
final_name() {
	local d i=0
	for d in $(scsi_disks); do
		installer_disk $d && continue
		[ $d = $1 ] && break
		i=$((i + 1))
	done
	echo sd$(echo abcdefghijklmnopqrstuvwxyz | cut -c$((i + 1)))
}

scsi_id() {
	local t
	t=$(readlink /sys/block/$1/device)
	echo "${t##*/}" | cut -d: -f3
}

# "2048 MB, VENDOR MODEL"
disk_info() {
	local mb vendor model
	mb=$(($(cat /sys/block/$1/size) / 2048))
	vendor=$(cat /sys/block/$1/device/vendor 2>/dev/null | sed 's/ *$//')
	model=$(cat /sys/block/$1/device/model 2>/dev/null | sed 's/ *$//')
	echo "$mb MB, $vendor $model"
}
