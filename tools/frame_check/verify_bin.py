#!/usr/bin/env python3
"""Independent .bin checker: re-implements the wire format in Python and uses zlib.crc32.

usage: verify_bin.py <file.bin|dir> [--expect-frames N] [--rate-hz N]

Shares no code with sender/receiver/frame_compare, so it can cross-check them.
Checks every frame's fixed fields, payload_len, header CRC, payload CRC and that
frame_id runs 0, 1, 2, ... Prints a SHA-256 of the concatenated bytes.
"""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys
import zlib
from pathlib import Path

HEADER_LEN = 44
EXPECTED_RATE = 200  # overridden by --rate-hz
CHANNELS = 1704
PAYLOAD_LEN = CHANNELS * 8
FRAME_BYTES = 4 + HEADER_LEN + PAYLOAD_LEN + 4
# magic, version, header_size, module_id, flags, frame_id, timestamp_ns,
# channel_count, sample_rate_hz, sample_format, reserved, payload_len, header_crc
HEADER = struct.Struct("<IHHHHQQHHHHII")
assert HEADER.size == HEADER_LEN


def inputs(path: Path) -> list[Path]:
    return sorted(path.glob("*.bin")) if path.is_dir() else [path]


def check_frame(frame: bytes) -> tuple[int | None, str]:
    (header_len,) = struct.unpack_from("<I", frame, 0)
    if header_len != HEADER_LEN:
        return None, f"header_len={header_len}"
    header = frame[4:4 + HEADER_LEN]
    (magic, version, header_size, _module, _flags, frame_id, _ts,
     channels, rate, fmt, _reserved, payload_len, header_crc) = HEADER.unpack(header)
    expected = {"magic": (magic, 0x434D4231), "version": (version, 2), "header_size": (header_size, 44),
                "channel_count": (channels, CHANNELS), "sample_rate_hz": (rate, EXPECTED_RATE),
                "sample_format": (fmt, 1), "payload_len": (payload_len, PAYLOAD_LEN)}
    for name, (got, want) in expected.items():
        if got != want:
            return frame_id, f"{name}={got} (want {want})"
    if zlib.crc32(header[:40] + b"\0\0\0\0") != header_crc:
        return frame_id, "header crc mismatch"
    payload = frame[4 + HEADER_LEN:4 + HEADER_LEN + PAYLOAD_LEN]
    (payload_crc,) = struct.unpack_from("<I", frame, FRAME_BYTES - 4)
    if zlib.crc32(payload) != payload_crc:
        return frame_id, "payload crc mismatch"
    return frame_id, ""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path)
    parser.add_argument("--expect-frames", type=int)
    parser.add_argument("--rate-hz", type=int, default=200)
    args = parser.parse_args()
    global EXPECTED_RATE
    EXPECTED_RATE = args.rate_hz

    digest = hashlib.sha256()
    frames = bad = order_errors = trailing = 0
    next_id = 0
    for path in inputs(args.path):
        with path.open("rb") as stream:
            while chunk := stream.read(FRAME_BYTES):
                digest.update(chunk)
                if len(chunk) < FRAME_BYTES:
                    trailing += len(chunk)
                    break
                frame_id, problem = check_frame(chunk)
                if problem:
                    bad += 1
                    if bad <= 10:
                        print(f"BAD {path.name}:{frames} frame_id={frame_id}: {problem}")
                if frame_id != next_id:
                    order_errors += 1
                    if order_errors <= 10:
                        print(f"ORDER {path.name}:{frames} frame_id={frame_id} want {next_id}")
                next_id = (frame_id if frame_id is not None else next_id) + 1
                frames += 1

    ok = bad == 0 and order_errors == 0 and trailing == 0 and frames > 0
    if args.expect_frames is not None and frames != args.expect_frames:
        ok = False
    print(f"frames={frames} bad={bad} order_errors={order_errors} trailing_bytes={trailing}")
    print(f"sha256={digest.hexdigest()}")
    print("result=" + ("OK" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
