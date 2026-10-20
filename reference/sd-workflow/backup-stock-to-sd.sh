#!/bin/sh

set -eu

DEST_ROOT=${1:-/mnt/sdcard}
EXPECTED_SIZE=8388608

fail() {
	printf 'ERREUR: %s\n' "$*" >&2
	exit 1
}

[ -r /proc/mtd ] || fail "/proc/mtd est absent"
[ -d "$DEST_ROOT" ] || fail "$DEST_ROOT est absent"
grep -q " $DEST_ROOT " /proc/mounts || fail "$DEST_ROOT n'est pas un point de montage"
grep " $DEST_ROOT " /proc/mounts | grep -q 'rw[, ]' || fail "$DEST_ROOT n'est pas monte en lecture-ecriture"

PROBE="$DEST_ROOT/.ak3918-write-test-$$"
: > "$PROBE" || fail "impossible d'ecrire sur $DEST_ROOT"
rm -f "$PROBE"

STAMP=$(date +%Y%m%d-%H%M%S 2>/dev/null || printf unknown)
OUT="$DEST_ROOT/ak3918-stock-$STAMP-$$"
[ ! -e "$OUT" ] || fail "$OUT existe deja"
mkdir "$OUT"

cleanup_on_error() {
	printf 'Sauvegarde interrompue; fichiers partiels conserves dans %s\n' "$OUT" >&2
}
trap cleanup_on_error HUP INT TERM

cp /proc/mtd "$OUT/proc-mtd.txt"
cat /proc/cmdline > "$OUT/proc-cmdline.txt"
cat /proc/cpuinfo > "$OUT/proc-cpuinfo.txt"
cat /proc/meminfo > "$OUT/proc-meminfo.txt"
cat /proc/modules > "$OUT/proc-modules.txt"
dmesg > "$OUT/dmesg.txt" 2>&1 || true
mount > "$OUT/mount.txt" 2>&1 || true
uname -a > "$OUT/uname.txt" 2>&1 || true

ALL_MTD=$(awk -F: '$2 ~ /"all"/ { print $1; exit }' /proc/mtd)
[ -n "$ALL_MTD" ] || fail "partition MTD nommee all introuvable"
[ -r "/dev/$ALL_MTD" ] || fail "/dev/$ALL_MTD est illisible"

printf 'Lecture 1 de la NOR complete...\n'
dd if="/dev/$ALL_MTD" of="$OUT/full-nor-pass1.bin" bs=65536
printf 'Lecture 2 de la NOR complete...\n'
dd if="/dev/$ALL_MTD" of="$OUT/full-nor-pass2.bin" bs=65536

SIZE1=$(wc -c < "$OUT/full-nor-pass1.bin")
SIZE2=$(wc -c < "$OUT/full-nor-pass2.bin")
[ "$SIZE1" -eq "$EXPECTED_SIZE" ] || fail "premiere image: $SIZE1 octets au lieu de $EXPECTED_SIZE"
[ "$SIZE2" -eq "$EXPECTED_SIZE" ] || fail "seconde image: $SIZE2 octets au lieu de $EXPECTED_SIZE"
cmp "$OUT/full-nor-pass1.bin" "$OUT/full-nor-pass2.bin" || fail "les deux lectures de la NOR different"

while IFS= read -r LINE; do
	case "$LINE" in
	mtd[0-9]*:*)
		DEV=${LINE%%:*}
		NAME=$(printf '%s\n' "$LINE" | sed 's/.*"\([^"]*\)"/\1/')
		printf 'Sauvegarde de %s (%s)...\n' "$DEV" "$NAME"
		dd if="/dev/$DEV" of="$OUT/$DEV-$NAME.bin" bs=65536
		;;
	esac
done < /proc/mtd

if command -v sha256sum >/dev/null 2>&1; then
	(
		cd "$OUT"
		sha256sum ./*.bin > SHA256SUMS
	)
elif command -v md5sum >/dev/null 2>&1; then
	(
		cd "$OUT"
		md5sum ./*.bin > MD5SUMS
	)
else
	fail "aucun outil de somme de controle disponible"
fi

sync
trap - HUP INT TERM
printf 'Sauvegarde valide terminee dans %s\n' "$OUT"
printf 'NOR complete: %s octets, deux lectures identiques.\n' "$EXPECTED_SIZE"