#!/bin/sh
# Report the target of an ELF file from its header, without trusting file names.
#   mic/toolchain/elf-target.sh FILE
# Prints one line "K1OM", "X86_64" or "OTHER e_machine=<n>" and exits 0 only for K1OM.
# Primary evidence is the raw e_machine field (EM_K1OM = 181, EM_X86_64 = 62),
# read with od, so it works with binutils that no longer know K1OM.
set -u
f=${1:?usage: elf-target.sh FILE}
[ -f "$f" ] || { echo "MISSING"; exit 2; }
magic=$(od -An -c -N4 "$f" | tr -d ' ')
[ "$magic" = '177ELF' ] || { echo "NOT_ELF"; exit 1; }
data=$(od -An -tu1 -j5 -N1 "$f" | tr -d ' ')
[ "$data" = 1 ] || { echo "OTHER big-endian"; exit 1; }   # KNC is little-endian
m=$(od -An -tu2 -j18 -N2 "$f" | tr -d ' ')
case "$m" in
    181) echo "K1OM"; exit 0 ;;
    62)  echo "X86_64"; exit 1 ;;
    *)   echo "OTHER e_machine=$m"; exit 1 ;;
esac
