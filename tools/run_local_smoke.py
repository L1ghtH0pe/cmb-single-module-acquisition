#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import struct
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

FRAME_BYTES = 4 + 44 + 1704 * 8 + 4  # wire frame, stored as-is in .bin
FRAME_ID_OFFSET = 16


def project_root() -> Path:
    return Path(__file__).resolve().parents[1]


def exe_name(name: str) -> str:
    return f"{name}.exe" if os.name == "nt" else name


def clean_runtime_dirs(root: Path) -> None:
    for rel in ["logs", "captures/raw", "captures/sent", "captures/meta"]:
        shutil.rmtree(root / rel, ignore_errors=True)
    (root / "logs").mkdir(parents=True, exist_ok=True)
    (root / "captures" / "raw").mkdir(parents=True, exist_ok=True)


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


def read_frame_ids(segments: list[Path]) -> list[int]:
    ids: list[int] = []
    for path in segments:
        data = path.read_bytes()
        if len(data) % FRAME_BYTES != 0:
            raise RuntimeError(f"{path.name} size {len(data)} is not a multiple of {FRAME_BYTES}")
        ids.extend(struct.unpack_from("<Q", data, offset + FRAME_ID_OFFSET)[0] for offset in range(0, len(data), FRAME_BYTES))
    return ids


def validate_capture_segments(root: Path, frames: int) -> int:
    segments = sorted(p for p in (root / "captures" / "raw").glob("segment-*.bin") if p.is_file())
    if not segments:
        raise RuntimeError("no raw capture segments written")
    ids = read_frame_ids(segments)
    if ids != list(range(frames)):
        raise RuntimeError(f"captured frame_id sequence mismatch: got {len(ids)} frames, expected 0..{frames - 1}")
    return len(ids)


def compare_sent_received(root: Path, build_dir: Path, env: dict[str, str]) -> None:
    command = [str(build_dir / exe_name("frame_compare")), str(root / "captures" / "sent"), str(root / "captures" / "raw")]
    result = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True)
    (root / "logs" / "frame-compare.txt").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode != 0:
        raise RuntimeError("frame_compare reported differences; see logs/frame-compare.txt")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--bind-host", default="0.0.0.0")
    parser.add_argument("--frames", type=int, default=100)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--prepend-path", action="append", default=[])
    args = parser.parse_args()

    root = project_root()
    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = root / build_dir
    clean_runtime_dirs(root)

    receiver = build_dir / exe_name("receiver")
    sender = build_dir / exe_name("sender")

    env = os.environ.copy()
    prepend_paths = list(args.prepend_path)
    if os.name == "nt" and not prepend_paths:
        prepend_paths.append("C:/msys64/ucrt64/bin")
    if prepend_paths:
        env["PATH"] = os.pathsep.join(prepend_paths + [env.get("PATH", "")])

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
            [str(receiver), str(args.port), str(args.frames), args.bind_host],
            cwd=root,
            env=env,
            stdout=receiver_out,
            stderr=receiver_err,
            text=True,
        )
        time.sleep(1.0)

        with sender_stdout.open("w", encoding="utf-8") as out, sender_stderr.open("w", encoding="utf-8") as err:
            sender_proc = subprocess.Popen(
                [str(sender), args.host, str(args.port), str(args.frames), "--save-sent", "captures/sent"],
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
        compare_sent_received(root, build_dir, env)
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
    print("frame_compare=identical")
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
