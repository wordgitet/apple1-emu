#!/bin/bash
# Build apple1 as a VxWorks 7 RTP application (.vxe).
#
# Prerequisites:
#   source ~/vxworks-sdk/sdkenv.sh
#
# Usage:
#   bash vxworks_rtp_build.sh
#   # deploy *.vxe via FTP; on VxWorks: cmd -> cd host path -> ./apple1.vxe

set -e

APPLE1_DIR="$(cd "$(dirname "$0")" && pwd)"

if ! command -v wr-cc >/dev/null 2>&1; then
	echo "ERROR: wr-cc not found. Source the VxWorks SDK first:"
	echo "  source ~/vxworks-sdk/sdkenv.sh"
	exit 1
fi

# wr-cc is Clang; avoid -std=c89 (can interact badly with RTP libc).
# -O0: rule out illegal-instruction codegen in cpu.c at -O2.
# -fno-stack-protector: avoid RTP stack-guard faults on large main() frames.
CFLAGS="-rtp -Wall -O0 -fno-stack-protector"
DEFS="-DAPPLE1_PORT_VXWORKS -DAPPLE1_TERM_VT100 -DAPPLE1_OMIT_CHARMAP"
DEFS="$DEFS -DAPPLE1_OMIT_DEBUGGER -DAPPLE1_OMIT_ACI -DAPPLE1_OMIT_KRUSADER"
DEFS="$DEFS -DAPPLE1_STATIC_RAM_SIZE=8192 -DAPPLE1_KBD_BUFFER_SIZE=512"

OBJS=""
for src in main.c cpu.c bus.c io.c cli_config.c term.c port.c; do
	base=$(basename "$src" .c)
	obj="/tmp/apple1-vx-${base}.o"
	wr-cc $CFLAGS $DEFS -c -o "$obj" "$APPLE1_DIR/$src"
	OBJS="$OBJS $obj"
done

wr-cc $CFLAGS -static -o "$APPLE1_DIR/apple1.vxe" $OBJS
wr-cc $CFLAGS -static -o "$APPLE1_DIR/vxsmoke.vxe" "$APPLE1_DIR/vxworks_smoke.c"

echo "Built $APPLE1_DIR/apple1.vxe"
echo "Built $APPLE1_DIR/vxsmoke.vxe (SDK smoke test)"
echo "Deploy via FTP (see documentation/VXWORKS_RTP.md) and run from the VxWorks cmd shell."
