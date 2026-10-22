#!/usr/bin/env python3
"""Fault-injection test for frame_compare.

usage: fault_injection_test.py <build_dir> [--source <file.bin>] [--exhaustive]

Damages copies of a capture in known ways and checks that frame_compare reports
exactly what was injected. The wire format and field map are re-implemented here
(independent of the C++ code) so the reported offsets/fields are cross-checked.

--source      use an existing capture (e.g. an optical-loopback source.bin)
--exhaustive  flip every byte offset of a frame (13684 frames, ~187 MB);
              default flips every header/CRC byte and a spread of payload bytes.
"""
from __future__ import annotations

import argparse
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

HEADER_LEN = 44
CHANNELS = 1704
PAYLOAD_OFFSET = 4 + HEADER_LEN
PAYLOAD_LEN = CHANNELS * 8
FRAME_BYTES = PAYLOAD_OFFSET + PAYLOAD_LEN + 4
HEADER_FIELDS = [  # (end offset within the frame, name)
    (4, "header_len"), (8, "header.magic"), (10, "header.version"), (12, "header.header_size"),
    (14, "header.module_id"), (16, "header.flags"), (24, "header.frame_id"), (32, "header.timestamp_ns"),
    (34, "header.channel_count"), (36, "header.sample_rate_hz"), (38, "header.sample_format"),
    (40, "header.reserved"), (44, "header.payload_len"), (48, "header.header_crc"),
]

failures: list[str] = []


def field_name(offset: int) -> str:
    for end, name in HEADER_FIELDS:
        if offset < end:
            return name
    if offset < PAYLOAD_OFFSET + PAYLOAD_LEN:
        rel = offset - PAYLOAD_OFFSET
        return f"payload[channel={rel // 8}].{'re' if rel % 8 < 4 else 'im'}"
    return "payload_crc"


def frame_id_of(frame: bytes | bytearray) -> int:
    return struct.unpack_from("<Q", frame, 16)[0]


def reseal(frame: bytearray) -> None:
    """Recompute both CRCs so a tampered frame is still internally valid."""
    payload = bytes(frame[PAYLOAD_OFFSET:PAYLOAD_OFFSET + PAYLOAD_LEN])
    struct.pack_into("<I", frame, FRAME_BYTES - 4, zlib.crc32(payload))
    struct.pack_into("<I", frame, 44, 0)
    struct.pack_into("<I", frame, 44, zlib.crc32(bytes(frame[4:48])))


def frames_of(data: bytes) -> list[bytearray]:
    return [bytearray(data[i:i + FRAME_BYTES]) for i in range(0, len(data), FRAME_BYTES)]


class Runner:
    def __init__(self, build: Path, work: Path, expected: Path) -> None:
        self.compare = str(build / "frame_compare")
        self.work = work
        self.expected = expected
        self.count = 0

    def run(self, actual: Path, expected: Path | None = None) -> tuple[int, str]:
        result = subprocess.run([self.compare, str(expected or self.expected), str(actual), "--max-report", "1000000"],
                                capture_output=True, text=True, timeout=600)
        return result.returncode, result.stdout + result.stderr

    def write(self, name: str, frames: list[bytearray] | bytes) -> Path:
        path = self.work / f"{name}.bin"
        path.write_bytes(frames if isinstance(frames, bytes) else b"".join(frames))
        return path

    def case(self, name: str, actual: Path, *, code: int, contains: list[str] = (), counts: dict[str, int] | None = None,
             expected: Path | None = None) -> str:
        self.count += 1
        rc, out = self.run(actual, expected)
        problems = []
        if rc != code:
            problems.append(f"exit {rc} != {code}")
        problems += [f"missing '{text}'" for text in contains if text not in out]
        for key, want in (counts or {}).items():
            # invalid/out_of_order/trailing_bytes appear on both summary lines; check the actual side.
            if key in ("invalid", "out_of_order", "trailing_bytes"):
                pattern = rf"^actual_frames=.*\b{key}=(\d+)"
            else:
                pattern = rf"\b{key}=(\d+)"
            match = re.search(pattern, out, re.MULTILINE)
            if not match or int(match.group(1)) != want:
                problems.append(f"{key}={match.group(1) if match else '?'} != {want}")
        status = "PASS" if not problems else "FAIL"
        print(f"[{status}] {name}" + ("" if not problems else ": " + "; ".join(problems)))
        if problems:
            failures.append(name)
            print("      " + out.strip().replace("\n", "\n      ")[:2000])
        return out


def bit_flip_sweep(r: Runner, base: list[bytearray], exhaustive: bool) -> None:
    """One frame per offset, each with one bit flipped at that offset. Checks every report line."""
    template = base[0]
    if exhaustive:
        offsets = list(range(FRAME_BYTES))
    else:
        payload = list(range(PAYLOAD_OFFSET, PAYLOAD_OFFSET + PAYLOAD_LEN, 37))
        payload += [PAYLOAD_OFFSET + 8 * ch + k for ch in (0, 1, 852, 1703) for k in range(8)]
        offsets = sorted(set(range(PAYLOAD_OFFSET)) | set(payload) | set(range(FRAME_BYTES - 4, FRAME_BYTES)))
    # Expected side: n copies of the template with frame_id = n and resealed CRCs.
    expected_frames, actual_frames = [], []
    for n, offset in enumerate(offsets):
        good = bytearray(template)
        struct.pack_into("<Q", good, 16, n)
        reseal(good)
        bad = bytearray(good)
        bad[offset] ^= 1 << (n % 8)
        expected_frames.append(good)
        actual_frames.append(bad)
    expected = r.write("sweep-expected", expected_frames)
    actual = r.write("sweep-actual", actual_frames)
    r.count += 1
    rc, out = r.run(actual, expected)

    mismatch = {int(m.group(1)): (int(m.group(2)), m.group(3), int(m.group(4)))
                for m in re.finditer(r"MISMATCH frame_id=(\d+) first_offset=(\d+) field=(\S+) .* differing_bytes=(\d+) differing_bits=1 ", out)}
    problems = []
    for n, offset in enumerate(offsets):
        got = mismatch.get(n)
        if got != (offset, field_name(offset), 1):
            problems.append(f"offset {offset}: got {got}, want {(offset, field_name(offset), 1)}")
    if re.search(r"\b(MISSING|UNEXPECTED) ", out):
        problems.append("merge desynchronized: MISSING/UNEXPECTED reported")
    invalid = len(re.findall(r"INVALID actual", out))
    if rc != 1:
        problems.append(f"exit {rc}")
    if f"bit_errors={len(offsets)} " not in out:
        problems.append(f"bit_errors != {len(offsets)} (one flipped bit per frame)")
    if invalid != len(offsets):  # every single-bit flip must also fail a CRC or a fixed-field check
        problems.append(f"INVALID lines {invalid} != {len(offsets)}")
    name = f"single-bit flip at {len(offsets)} offsets ({'all' if exhaustive else 'all header/CRC + sampled payload'})"
    print(f"[{'PASS' if not problems else 'FAIL'}] {name}")
    if problems:
        failures.append(name)
        print("      " + "\n      ".join(problems[:20]))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("build", type=Path)
    parser.add_argument("--source", type=Path)
    parser.add_argument("--exhaustive", action="store_true")
    args = parser.parse_args()
    build = args.build.resolve()

    work = Path(tempfile.mkdtemp(prefix="cmb-fault-"))
    try:
        if args.source:
            data = args.source.read_bytes()[:200 * FRAME_BYTES]
        else:
            subprocess.run([str(build / "frame_gen"), str(work / "gen.bin"), "200", "--pattern", "random"],
                           check=True, capture_output=True)
            data = (work / "gen.bin").read_bytes()
        base = frames_of(data)
        n = len(base)
        assert n >= 50 and len(data) == n * FRAME_BYTES, "need at least 50 whole frames"
        expected = work / "expected.bin"
        expected.write_bytes(data)
        r = Runner(build, work, expected)
        b = frame_id_of(base[0])  # expectations are relative to the first frame_id of the base capture
        assert all(frame_id_of(x) == b + i for i, x in enumerate(base)), "base frame_ids must be consecutive"
        print(f"base: {n} frames, frame_id {b}..{b + n - 1}, from {args.source or 'frame_gen --pattern random'}")

        # --- positive controls
        r.case("identical copy", r.write("copy", data), code=0, contains=["result=IDENTICAL"],
               counts={"identical": n, "mismatched": 0, "missing": 0, "unexpected": 0})
        seg = work / "segments"
        seg.mkdir()
        for i in range(0, n, 64):  # split into segments with a different boundary than the original
            (seg / f"segment-{i // 64:06d}.bin").write_bytes(b"".join(base[i:i + 64]))
        r.case("same bytes split into segments", seg, code=0, counts={"identical": n})

        # --- every header field and CRC, one bit each
        bit_flip_sweep(r, base, args.exhaustive)

        # --- payload values
        f = [bytearray(x) for x in base]
        f[10][PAYLOAD_OFFSET + 8 * 1703 + 7] ^= 0x80  # sign bit of the last channel's im
        r.case("sign bit of last channel im", r.write("sign", f), code=1,
               contains=[f"MISMATCH frame_id={b + 10} ", "field=payload[channel=1703].im", "payload crc mismatch"],
               counts={"mismatched": 1, "identical": n - 1})
        f = [bytearray(x) for x in base]
        f[11][PAYLOAD_OFFSET:PAYLOAD_OFFSET + PAYLOAD_LEN] = bytes(PAYLOAD_LEN)
        out = r.case("whole payload zeroed", r.write("zero", f), code=1, contains=[f"MISMATCH frame_id={b + 11} "],
                     counts={"mismatched": 1})
        nonzero = sum(1 for b in base[11][PAYLOAD_OFFSET:PAYLOAD_OFFSET + PAYLOAD_LEN] if b) + \
            sum(1 for a, b in zip(base[11][-4:], f[11][-4:]) if a != b)
        flipped = sum(bin(x ^ y).count("1") for x, y in zip(base[11], f[11]))
        if f"differing_bits={flipped} " not in out or f"bit_errors={flipped} " not in out:
            failures.append("bit count")
            print(f"[FAIL] differing_bits/bit_errors: want {flipped}")
        else:
            print(f"[PASS] differing_bits=bit_errors={flipped} matches independent count")
        if f"differing_bytes={nonzero}" not in out:
            failures.append("differing_bytes count")
            print(f"[FAIL] differing_bytes count: want {nonzero}")
        else:
            print(f"[PASS] differing_bytes={nonzero} matches independent count")
        f = [bytearray(x) for x in base]
        f[12][PAYLOAD_OFFSET:PAYLOAD_OFFSET + 8], f[12][PAYLOAD_OFFSET + 8:PAYLOAD_OFFSET + 16] = \
            f[12][PAYLOAD_OFFSET + 8:PAYLOAD_OFFSET + 16], f[12][PAYLOAD_OFFSET:PAYLOAD_OFFSET + 8]
        r.case("channels 0 and 1 swapped", r.write("chswap", f), code=1,
               contains=[f"MISMATCH frame_id={b + 12} ", "field=payload[channel=0].re"])
        f = [bytearray(x) for x in base]
        for k in range(4):  # swap re/im of channel 5
            f[13][PAYLOAD_OFFSET + 40 + k], f[13][PAYLOAD_OFFSET + 44 + k] = f[13][PAYLOAD_OFFSET + 44 + k], f[13][PAYLOAD_OFFSET + 40 + k]
        r.case("re/im swapped in channel 5", r.write("reim", f), code=1, contains=["field=payload[channel=5].re"])

        # --- tampering that keeps every CRC valid: only the byte compare can catch it
        f = [bytearray(x) for x in base]
        f[20][PAYLOAD_OFFSET + 8 * 100] ^= 0x01
        reseal(f[20])
        r.case("payload changed + CRCs recomputed", r.write("reseal", f), code=1,
               contains=[f"MISMATCH frame_id={b + 20} ", "field=payload[channel=100].re"],
               counts={"mismatched": 1, "invalid": 0})
        f = [bytearray(x) for x in base]
        struct.pack_into("<Q", f[21], 24, struct.unpack_from("<Q", f[21], 24)[0] + 1)
        reseal(f[21])
        r.case("timestamp changed + CRCs recomputed", r.write("ts", f), code=1,
               contains=[f"MISMATCH frame_id={b + 21} ", "field=header.timestamp_ns"], counts={"invalid": 0})
        f = [bytearray(x) for x in base]
        struct.pack_into("<H", f[22], 12, 9)
        reseal(f[22])
        r.case("module_id changed + CRCs recomputed", r.write("mod", f), code=1,
               contains=["field=header.module_id"], counts={"invalid": 0})
        f = [bytearray(x) for x in base]
        f[23][PAYLOAD_OFFSET:PAYLOAD_OFFSET + PAYLOAD_LEN] = base[24][PAYLOAD_OFFSET:PAYLOAD_OFFSET + PAYLOAD_LEN]
        reseal(f[23])
        r.case("payload copied from next frame + resealed", r.write("dup-payload", f), code=1,
               contains=[f"MISMATCH frame_id={b + 23} "], counts={"invalid": 0})

        # --- frame-level faults
        r.case("frame 30 dropped", r.write("drop", base[:30] + base[31:]), code=1,
               contains=[f"MISSING frame_id={b + 30} "], counts={"missing": 1, "identical": n - 1, "unexpected": 0})
        r.case("frames 40..44 dropped", r.write("drop5", base[:40] + base[45:]), code=1,
               counts={"missing": 5, "identical": n - 5})
        r.case("first frame dropped", r.write("drop-first", base[1:]), code=1, contains=[f"MISSING frame_id={b + 0} "],
               counts={"missing": 1})
        r.case("last frame dropped", r.write("drop-last", base[:-1]), code=1, contains=[f"MISSING frame_id={b + n - 1} "],
               counts={"missing": 1})
        r.case("frame 31 duplicated", r.write("dup", base[:32] + base[31:]), code=1,
               contains=[f"OUT_OF_ORDER actual frame_id={b + 31} after {b + 31}"], counts={"out_of_order": 1, "missing": 0})
        r.case("frames 32 and 33 swapped", r.write("swap", base[:32] + [base[33], base[32]] + base[34:]), code=1,
               contains=[f"OUT_OF_ORDER actual frame_id={b + 32} after {b + 33}"])
        extra = bytearray(base[-1])
        struct.pack_into("<Q", extra, 16, frame_id_of(base[-1]) + 1)
        reseal(extra)
        r.case("extra frame at end", r.write("extra", base + [extra]), code=1,
               contains=[f"UNEXPECTED frame_id={b + n} "], counts={"unexpected": 1, "identical": n})
        r.case("truncated by 1 byte", r.write("trunc1", data[:-1]), code=1,
               contains=[f"MISSING frame_id={b + n - 1} "], counts={"trailing_bytes": FRAME_BYTES - 1})
        r.case("1 junk byte appended", r.write("junk", data + b"\0"), code=1, counts={"identical": n})
        r.case("1 byte inserted mid-stream", r.write("insert", data[:5 * FRAME_BYTES + 100] + b"\x55" + data[5 * FRAME_BYTES + 100:]),
               code=1, contains=["INVALID actual"])
        r.case("empty actual file", r.write("empty", b""), code=1, counts={"identical": 0, "missing": n})
        r.case("actual path missing", r.work / "nope.bin", code=2)
        r.case("expected and actual swapped roles", expected, code=1, contains=[f"UNEXPECTED frame_id={b + 30} "],
               expected=r.write("drop-again", base[:30] + base[31:]))

        print(f"\n{r.count - len(failures)}/{r.count} cases passed")
        return 1 if failures else 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
