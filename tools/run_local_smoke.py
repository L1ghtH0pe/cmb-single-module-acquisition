#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

PAYLOAD_BYTES = 1704 * 4


def project_root() -> Path:
    return Path(__file__).resolve().parents[1]


def exe_name(name: str) -> str:
    return f"{name}.exe" if os.name == "nt" else name


def clean_runtime_dirs(root: Path) -> None:
    for rel in ["logs", "captures/raw", "captures/meta"]:
        shutil.rmtree(root / rel, ignore_errors=True)
    (root / "logs").mkdir(parents=True, exist_ok=True)
    (root / "captures" / "raw").mkdir(parents=True, exist_ok=True)
    (root / "captures" / "meta").mkdir(parents=True, exist_ok=True)


def read_text(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def terminate_process(proc: subprocess.Popen[str]) -> None:
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)


def read_last_metrics_row(path: Path) -> dict[str, str]:
    with path.open("r", encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise RuntimeError(f"metrics file has no rows: {path}")
    return rows[-1]


def validate_metrics(root: Path, frames: int) -> None:
    sender_row = read_last_metrics_row(root / "logs" / "sender-metrics.csv")
    receiver_row = read_last_metrics_row(root / "logs" / "receiver-metrics.csv")

    if int(sender_row["frame_count"]) != frames:
        raise RuntimeError("sender metrics frame_count mismatch")
    if int(receiver_row["frame_count"]) != frames:
        raise RuntimeError("receiver metrics frame_count mismatch")
    if int(receiver_row["parse_fail_count"]) != 0:
        raise RuntimeError("receiver parse_fail_count is nonzero")
    if int(receiver_row["crc_error_count"]) != 0:
        raise RuntimeError("receiver crc_error_count is nonzero")
    if frames > 0:
        if int(receiver_row["frame_id_begin"]) != 0:
            raise RuntimeError("receiver frame_id_begin mismatch")
        if int(receiver_row["frame_id_end"]) != frames - 1:
            raise RuntimeError("receiver frame_id_end mismatch")


def validate_capture_segments(root: Path, frames: int) -> int:
    raw_dir = root / "captures" / "raw"
    meta_dir = root / "captures" / "meta"
    segments = sorted(p for p in raw_dir.glob("segment-*.bin") if p.is_file())
    indexes = sorted(p for p in meta_dir.glob("segment-*.csv") if p.is_file())
    if not segments:
        raise RuntimeError("no raw capture segments written")
    if not indexes:
        raise RuntimeError("no capture index files written")

    total_payload_bytes = sum(p.stat().st_size for p in segments)
    expected_payload_bytes = frames * PAYLOAD_BYTES
    if total_payload_bytes != expected_payload_bytes:
        raise RuntimeError(f"raw payload size mismatch: got {total_payload_bytes}, expected {expected_payload_bytes}")

    rows: list[dict[str, str]] = []
    for path in indexes:
        with path.open("r", encoding="utf-8", newline="") as f:
            rows.extend(csv.DictReader(f))
    if len(rows) != frames:
        raise RuntimeError(f"index row count mismatch: got {len(rows)}, expected {frames}")
    for i, row in enumerate(rows):
        if int(row["frame_id"]) != i:
            raise RuntimeError(f"index frame_id mismatch at row {i}")
        if int(row["payload_bytes"]) != PAYLOAD_BYTES:
            raise RuntimeError(f"index payload_bytes mismatch at row {i}")
    return len(rows)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--frames", type=int, default=100)
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    root = project_root()
    build_dir = root / "build"
    clean_runtime_dirs(root)

    receiver = build_dir / exe_name("receiver")
    sender = build_dir / exe_name("sender")

    env = os.environ.copy()
    env["PATH"] = str(Path("/c/msys64/ucrt64/bin")) + os.pathsep + env.get("PATH", "")

    receiver_stdout = root / "logs" / "receiver-stdout.txt"
    receiver_stderr = root / "logs" / "receiver-stderr.txt"
    sender_stdout = root / "logs" / "sender-stdout.txt"
    sender_stderr = root / "logs" / "sender-stderr.txt"

    receiver_out = receiver_stdout.open("w", encoding="utf-8")
    receiver_err = receiver_stderr.open("w", encoding="utf-8")
    receiver_proc: subprocess.Popen[str] | None = None
    sender_exit = 1
    receiver_exit = 1
    captured_frames = 0

    try:
        receiver_proc = subprocess.Popen(
            [str(receiver), str(args.port), str(args.frames)],
            cwd=root,
            env=env,
            stdout=receiver_out,
            stderr=receiver_err,
            text=True,
        )
        time.sleep(1.0)

        with sender_stdout.open("w", encoding="utf-8") as out, sender_stderr.open("w", encoding="utf-8") as err:
            sender_proc = subprocess.Popen(
                [str(sender), "127.0.0.1", str(args.port), str(args.frames)],
                cwd=root,
                env=env,
                stdout=out,
                stderr=err,
                text=True,
            )
            try:
                sender_exit = sender_proc.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                terminate_process(sender_proc)
                raise RuntimeError("sender timed out")

        try:
            receiver_exit = receiver_proc.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            terminate_process(receiver_proc)
            raise RuntimeError("receiver timed out")

        captured_frames = validate_capture_segments(root, args.frames)
        validate_metrics(root, args.frames)
    except Exception as ex:
        if receiver_proc is not None:
            terminate_process(receiver_proc)
        print(f"local_process_smoke failed: {ex}", file=sys.stderr)
        return 1
    finally:
        receiver_out.close()
        receiver_err.close()

    sender_text = read_text(sender_stdout)
    receiver_text = read_text(receiver_stdout)

    print(f"sender_exit={sender_exit}")
    print(f"receiver_exit={receiver_exit}")
    print(f"captured_frames={captured_frames}")
    print(f"sender_metrics={'yes' if (root / 'logs' / 'sender-metrics.csv').exists() else 'no'}")
    print(f"receiver_metrics={'yes' if (root / 'logs' / 'receiver-metrics.csv').exists() else 'no'}")
    print("--- sender stdout ---")
    print(sender_text, end="")
    print("--- receiver stdout ---")
    print(receiver_text, end="")

    ok = sender_exit == 0 and receiver_exit == 0 and "sender sent " in sender_text and "receiver received " in receiver_text
    if not ok:
        print("local_process_smoke failed", file=sys.stderr)
        return 1

    print("local_process_smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
