#!/usr/bin/env python3
"""CTest for frame_gen / sender --input / frame_compare over localhost TCP."""
from __future__ import annotations

import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

FRAME_BYTES = 4 + 44 + 1704 * 8 + 4
PAYLOAD_OFFSET = 4 + 44


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def run(command: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, capture_output=True, text=True, timeout=60, **kwargs)


def transfer(build: Path, work: Path, frames: int, extra_sender_args: list[str]) -> None:
    port = str(free_port())
    receiver = subprocess.Popen([str(build / "receiver"), port, str(frames), "127.0.0.1"], cwd=work,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.5)
    sender = run([str(build / "sender"), "127.0.0.1", port, str(frames), *extra_sender_args], cwd=work)
    assert sender.returncode == 0, sender.stdout + sender.stderr
    assert receiver.wait(timeout=30) == 0


def main() -> int:
    build = Path(sys.argv[1]).resolve()
    compare = str(build / "frame_compare")
    frames = 50
    work = Path(tempfile.mkdtemp(prefix="cmb-frame-check-"))
    try:
        # 1. Generate a source file, replay it over TCP, compare source vs receiver capture.
        source = work / "source.bin"
        gen = run([str(build / "frame_gen"), str(source), str(frames), "--pattern", "random", "--seed", "7"])
        assert gen.returncode == 0, gen.stderr
        assert source.stat().st_size == frames * FRAME_BYTES

        transfer(build, work, frames, ["--input", str(source), "--save-sent", "captures/sent"])
        raw = work / "captures" / "raw"
        ok = run([compare, str(source), str(raw)])
        assert ok.returncode == 0 and f"identical={frames} mismatched=0" in ok.stdout, ok.stdout + ok.stderr
        ok = run([compare, str(work / "captures" / "sent"), str(raw)])
        assert ok.returncode == 0, ok.stdout

        # 2. Flip one bit in frame 7, channel 65, imaginary part.
        corrupted = work / "corrupted"
        shutil.copytree(raw, corrupted)
        segment = next(corrupted.glob("segment-*.bin"))
        data = bytearray(segment.read_bytes())
        data[7 * FRAME_BYTES + PAYLOAD_OFFSET + 8 * 65 + 4] ^= 0x01
        segment.write_bytes(bytes(data))
        bad = run([compare, str(source), str(corrupted)])
        assert bad.returncode == 1, bad.stdout
        assert "MISMATCH frame_id=7" in bad.stdout and "payload[channel=65].im" in bad.stdout, bad.stdout
        assert "INVALID actual frame_id=7" in bad.stdout and "payload crc mismatch" in bad.stdout, bad.stdout

        # 3. Drop frame 20 and truncate the tail: reported as MISSING and trailing bytes.
        del data[20 * FRAME_BYTES:21 * FRAME_BYTES]
        data[7 * FRAME_BYTES + PAYLOAD_OFFSET + 8 * 65 + 4] ^= 0x01
        segment.write_bytes(bytes(data[:-100]))
        bad = run([compare, str(source), str(corrupted)])
        assert bad.returncode == 1, bad.stdout
        assert "MISSING frame_id=20" in bad.stdout and "MISSING frame_id=49" in bad.stdout, bad.stdout
        assert f"trailing_bytes={FRAME_BYTES - 100}" in bad.stdout and "mismatched=0" in bad.stdout, bad.stdout

        # 4. sender refuses an input file that fails CRC.
        broken = work / "broken.bin"
        data = bytearray(source.read_bytes())
        data[3 * FRAME_BYTES + PAYLOAD_OFFSET] ^= 0xFF
        broken.write_bytes(bytes(data))
        port = str(free_port())
        receiver = subprocess.Popen([str(build / "receiver"), port, str(frames), "127.0.0.1"], cwd=work,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.5)
        sender = run([str(build / "sender"), "127.0.0.1", port, str(frames), "--input", str(broken)], cwd=work)
        receiver.wait(timeout=30)
        assert sender.returncode == 10 and "payload crc mismatch" in sender.stderr, sender.stderr

        print("frame_check tests passed")
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
