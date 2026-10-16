#!/usr/bin/env bash
#
# Disassemble a raw 68000 binary loaded at a given address, with the m68k
# objdump of Diego's atarist-toolkit Docker image.
#
# Usage: tools/re/disasm.sh <bin> <vma> <out.lst> [arch]
#
#   bin   raw bytes as they sit in memory (no header)
#   vma   address of the first byte, e.g. 0x70000
#   out   the listing to write (objdump -D -z, GNU syntax)
#   arch  BFD architecture, default m68k:68000 (bytes that are no 68000
#         instruction show as .short)
#
# The binary's folder is mounted read-only into the container; the listing is
# written by the host.
set -euo pipefail

IMAGE="${M68K_IMAGE:-logronoide/atarist-toolkit-docker-x86_64:1.4.0}"

if [[ $# -lt 3 || $# -gt 4 ]]; then
  echo "usage: $0 <bin> <vma> <out.lst> [arch]" >&2
  exit 2
fi
bin=$1 vma=$2 out=$3 arch=${4:-m68k:68000}
[[ -f "$bin" ]] || { echo "error: no such file: $bin" >&2; exit 1; }
command -v docker >/dev/null || { echo "error: docker not found" >&2; exit 1; }
docker image inspect "$IMAGE" >/dev/null 2>&1 || { echo "error: image missing: docker pull $IMAGE" >&2; exit 1; }

dir=$(cd "$(dirname "$bin")" && pwd)
name=$(basename "$bin")
docker run --rm --platform linux/x86_64 -v "$dir:/in:ro" "$IMAGE" \
  m68k-atari-mint-objdump -D -z -b binary -m "$arch" --adjust-vma="$vma" "/in/$name" \
  | sed "s|/in/$name|$bin|" > "$out"
echo "$out: $(grep -c '^ *[0-9a-f]*:' "$out") lines"
