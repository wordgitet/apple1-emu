#!/usr/bin/env python3
"""
gen_harte_fixture.py -- build a compact binary fixture from the local Tom Harte
"65x02 ProcessorTests" JSON suite (tests/tomharte/v1) for the cycle-exact test runner.

Inspired by and credited to the POM1 emulator's tools/gen_harte_fixture.py.

This script reads local JSON test files for all 244 non-JAM opcodes, extracts
up to K cases of each, and writes a single compact binary file.

Usage:
  python3 tests/gen_harte_fixture.py [--cases 10000] [--in-dir tests/tomharte/v1] [--out tests/harte_6502.bin]
"""
import argparse
import json
import struct
import sys
from pathlib import Path

# The 12 NMOS 6502 opcodes that halt/JAM the CPU
JAM_OPCODES = {
    0x02, 0x12, 0x22, 0x32, 0x42, 0x52, 0x62, 0x72,
    0x92, 0xB2, 0xD2, 0xF2
}

# All 244 valid/illegal non-JAM opcodes
ALL_OPCODES = [op for op in range(256) if op not in JAM_OPCODES]

def pack_state(buf, st):
    buf += struct.pack("<HBBBBB", st["pc"], st["s"], st["a"], st["x"], st["y"], st["p"])
    ram = st["ram"]
    buf += struct.pack("<H", len(ram))
    for addr, val in ram:
        buf += struct.pack("<HB", addr, val)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cases", type=int, default=10000, help="cases per opcode (default 10000)")
    ap.add_argument("--in-dir", default="tests/tomharte/v1", help="directory with opcode JSON files")
    ap.add_argument("--out", default="tests/harte_6502.bin", help="output binary file path")
    args = ap.parse_args()

    in_dir = Path(args.in_dir)
    if not in_dir.is_dir():
        print(f"Error: Input directory {args.in_dir} does not exist.", file=sys.stderr)
        sys.exit(1)

    out = bytearray(b"HRT1")
    out += struct.pack("<I", 0)  # placeholder; patched with real count below
    total = 0

    for op in ALL_OPCODES:
        json_path = in_dir / f"{op:02x}.json"
        if not json_path.is_file():
            print(f"Warning: File {json_path} not found. Skipping.", file=sys.stderr)
            continue

        with open(json_path, "r") as f:
            data = json.load(f)

        kept = data[: args.cases]
        for case in kept:
            out += struct.pack("<B", op)
            pack_state(out, case["initial"])
            pack_state(out, case["final"])
            out += struct.pack("<B", len(case["cycles"]))
        total += len(kept)
        print(f"  {op:02X}: +{len(kept):4d}  (total {total})", file=sys.stderr)

    struct.pack_into("<I", out, 4, total)

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(out)
    print(f"Success! Wrote {args.out}: {total} cases over {len(ALL_OPCODES)} opcodes, {len(out)} bytes")

if __name__ == "__main__":
    main()
