#!/bin/sh
set -eu

BASE_URL=${1:-http://192.0.2.20:8102}
BUNDLE=openipc-anyka-sd-bundle-20261008-speaker-fix.tar.gz
EXPECTED_SHA256=b4e986116fee6212721c7dba31614700e11aaf5392a56aadf372347e4db43429
DESTINATION=/mnt/$BUNDLE
PARTIAL=$DESTINATION.part
EXTRACT_DIRECTORY=/mnt/bundle-speaker
EXTRACT_PENDING=$EXTRACT_DIRECTORY.part
SDK_DIRECTORY=/mnt/openipc-persist/sdk-rtsp
FILE_LIST=
sd_rw=0
partial_owned=0

bb() {
	/media/lib/ld-musl-arm.so.1 --library-path /media/lib:/media/usr/lib /media/bin/busybox "$@"
}

runtime_pids() {
	for process in /proc/[0-9]*; do
		process_id=${process##*/}
		case "$process_id" in 1|"$$") continue ;; esac
		bb test -r "$process/cmdline" || continue
		if bb grep -q "$SDK_DIRECTORY/" "$process/maps" 2>/dev/null ||
			bb tr '\000' '\n' <"$process/cmdline" | bb awk -v sdk="$SDK_DIRECTORY/" '
				$0 == "/mnt/stock-probe.sh" || index($0, sdk) == 1 { found=1 }
				END { exit !found }
			'; then
			echo "$process_id"
		fi
	done
}

stop_runtime() {
	echo 'Stopping SDK and web services before replacing SD files...'
	remaining=20
	while bb test "$remaining" -gt 0; do
		process_ids=$(runtime_pids)
		if bb test -z "$process_ids"; then
			return 0
		fi
		bb kill -TERM $process_ids 2>/dev/null || true
		bb sleep 1
		remaining=$((remaining - 1))
	done
	if bb test -n "$(runtime_pids)"; then
		echo 'Runtime still active: installation refused. No SIGKILL sent.' >&2
		return 1
	fi
}

install_file() {
	relative_path=$1
	source_file=$EXTRACT_DIRECTORY/$relative_path
	target_file=/mnt/$relative_path
	bb test -f "$source_file" && ! bb test -L "$source_file" || {
		echo "Refusing non-regular bundle file: $relative_path" >&2
		return 1
	}
	bb mkdir -p "$(bb dirname "$target_file")"
	temporary_file=$(bb mktemp "$target_file.install.XXXXXX")
	if ! bb cp -p "$source_file" "$temporary_file" || ! bb mv -f "$temporary_file" "$target_file"; then
		bb rm -f "$temporary_file"
		return 1
	fi
}

cleanup() {
	exit_code=$?
	trap - 0 HUP INT TERM
	if bb test -n "$FILE_LIST"; then
		bb rm -f "$FILE_LIST" || exit_code=1
	fi
	if bb test "$partial_owned" = 1; then
		bb rm -f "$PARTIAL" || exit_code=1
	fi
	if bb test "$sd_rw" = 1; then
		bb sync || exit_code=1
		if ! bb mount -o remount,ro /mnt; then
			echo 'ERROR: could not restore SD read-only; remount /mnt manually.' >&2
			exit_code=1
		fi
	fi
	exit "$exit_code"
}

bb true || { echo 'OpenIPC musl BusyBox must be mounted under /media.' >&2; exit 1; }
bb awk '$1 == "/dev/mmcblk0p1" && $2 == "/mnt" && $3 == "vfat" { found=1 } END { exit !found }' /proc/mounts || {
	echo 'Expected FAT SD partition /dev/mmcblk0p1 mounted at /mnt.' >&2
	exit 1
}
if bb test -e "$PARTIAL" || bb test -L "$PARTIAL" || bb test -L "$DESTINATION"; then
	echo 'Refusing an existing partial download or a symbolic link.' >&2
	exit 1
fi
if bb test -L "$EXTRACT_DIRECTORY" || bb test -e "$EXTRACT_PENDING" || bb test -L "$EXTRACT_PENDING"; then
	echo "Refusing a symbolic link or an unfinished extraction: $EXTRACT_PENDING." >&2
	exit 1
fi
required_kib=131072
if bb test -d "$EXTRACT_DIRECTORY"; then
	required_kib=65536
elif bb test -e "$EXTRACT_DIRECTORY"; then
	echo "Extraction path is not a directory: $EXTRACT_DIRECTORY" >&2
	exit 1
fi
if bb test -e "$DESTINATION"; then
	bb printf '%s  %s\n' "$EXPECTED_SHA256" "$DESTINATION" | bb sha256sum -c -
	echo "Bundle already downloaded and verified: $DESTINATION"
else
	required_kib=$((required_kib + 24000))
fi
available_kib=$(bb df -Pk /mnt | bb awk 'NR == 2 { print $4 }')
bb test "$available_kib" -ge "$required_kib" || { echo "At least $required_kib KiB of free SD space required." >&2; exit 1; }

trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
sd_rw=1
bb mount -o remount,rw /mnt
if ! bb test -e "$DESTINATION"; then
	partial_owned=1
	bb wget -T 30 -O "$PARTIAL" "${BASE_URL%/}/$BUNDLE"
	bb printf '%s  %s\n' "$EXPECTED_SHA256" "$PARTIAL" | bb sha256sum -c -
	bb mv "$PARTIAL" "$DESTINATION"
	partial_owned=0
	echo "Bundle downloaded and verified: $DESTINATION"
fi

bb gunzip -t "$DESTINATION"
if ! bb test -d "$EXTRACT_DIRECTORY"; then
	bb mkdir "$EXTRACT_PENDING"
	bb gunzip -c "$DESTINATION" | bb tar -xf - -C "$EXTRACT_PENDING"
	(cd "$EXTRACT_PENDING" && bb sha256sum -c SHA256SUMS)
	(cd "$EXTRACT_PENDING/openipc-persist/sdk-rtsp" && bb sha256sum -c SHA256SUMS)
	bb mv "$EXTRACT_PENDING" "$EXTRACT_DIRECTORY"
fi
expected_manifest=$(bb gunzip -c "$DESTINATION" | bb tar -xOf - SHA256SUMS | bb sha256sum | bb awk '{ print $1 }')
bb printf '%s  %s\n' "$expected_manifest" "$EXTRACT_DIRECTORY/SHA256SUMS" | bb sha256sum -c -
(cd "$EXTRACT_DIRECTORY" && bb sha256sum -c SHA256SUMS)
(cd "$EXTRACT_DIRECTORY/openipc-persist/sdk-rtsp" && bb sha256sum -c SHA256SUMS)
echo "Bundle extracted and both manifests verified: $EXTRACT_DIRECTORY"

FILE_LIST=$(bb mktemp /tmp/anyka-bundle-files.XXXXXX)
bb gunzip -c "$DESTINATION" | bb tar -tf - >"$FILE_LIST"
while IFS= read -r relative_path; do
	case "$relative_path" in
		''|/*|..|../*|*/../*|*/..|*wpa_supplicant.conf*|*web-access.json*)
			echo 'Unsafe or private archive entry: installation refused.' >&2
			exit 1
			;;
	esac
	case "$relative_path" in
		*/)
			directory_path=$EXTRACT_DIRECTORY/${relative_path%/}
			bb test -d "$directory_path" && ! bb test -L "$directory_path" || {
				echo "Invalid archive directory: $relative_path" >&2
				exit 1
			}
			continue
			;;
	esac
	bb test -f "$EXTRACT_DIRECTORY/$relative_path" && ! bb test -L "$EXTRACT_DIRECTORY/$relative_path" || {
		echo "Invalid archive entry: $relative_path" >&2
		exit 1
	}
done <"$FILE_LIST"

stop_runtime
while IFS= read -r relative_path; do
	case "$relative_path" in */|SHA256SUMS|openipc-persist/sdk-rtsp/SHA256SUMS) continue ;; esac
	install_file "$relative_path"
done <"$FILE_LIST"
install_file openipc-persist/sdk-rtsp/SHA256SUMS
install_file SHA256SUMS
(cd "$SDK_DIRECTORY" && bb sha256sum -c SHA256SUMS)
(cd /mnt && bb sha256sum -c SHA256SUMS)
bb sync
bb mount -o remount,ro /mnt
sd_rw=0
echo 'New bundle installed at /mnt; both manifests verified and SD read-only.'
echo 'Wi-Fi and web credentials preserved. No NOR writes; services remain stopped.'
echo 'Power-cycle, follow COMMANDS-STOCK-SHELL.txt in U-Boot, then run /bin/sh /mnt/stock-probe.sh.'