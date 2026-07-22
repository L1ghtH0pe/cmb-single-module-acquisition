#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path


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


def count_raw_frames(raw_dir: Path) -> int:
    if not raw_dir.exists():
        return 0
    return sum(1 for p in raw_dir.iterdir() if p.is_file() and p.name.startswith("frame-"))


def read_text(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def run_process(exe: Path, args: list[str], stdout_path: Path, stderr_path: Path, cwd: Path, env: dict[str, str]) -> int:
    with stdout_path.open("w", encoding="utf-8") as out, stderr_path.open("w", encoding="utf-8") as err:
        proc = subprocess.Popen([str(exe), *args], cwd=cwd, env=env, stdout=out, stderr=err)
        return proc.wait()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--frames", type=int, default=100)
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

    receiver_proc = subprocess.Popen(
        [str(receiver), str(args.port), str(args.frames)],
        cwd=root,
        env=env,
        stdout=receiver_stdout.open("w", encoding="utf-8"),
        stderr=receiver_stderr.open("w", encoding="utf-8"),
    )

    time.sleep(1.0)

    sender_exit = run_process(
        sender,
        ["127.0.0.1", str(args.port), str(args.frames)],
        sender_stdout,
        sender_stderr,
        root,
        env,
    )

    receiver_exit = receiver_proc.wait(timeout=30)

    sender_text = read_text(sender_stdout)
    receiver_text = read_text(receiver_stdout)
    raw_count = count_raw_frames(root / "captures" / "raw")

    print(f"sender_exit={sender_exit}")
    print(f"receiver_exit={receiver_exit}")
    print(f"raw_files={raw_count}")
    print(f"sender_metrics={'yes' if (root / 'logs' / 'sender-metrics.csv').exists() else 'no'}")
    print(f"receiver_metrics={'yes' if (root / 'logs' / 'receiver-metrics.csv').exists() else 'no'}")
    print("--- sender stdout ---")
    print(sender_text, end="")
    print("--- receiver stdout ---")
    print(receiver_text, end="")

    ok = (
        sender_exit == 0
        and receiver_exit == 0
        and raw_count == args.frames
        and (root / "logs" / "sender-metrics.csv").exists()
        and (root / "logs" / "receiver-metrics.csv").exists()
        and "sender sent " in sender_text
        and "receiver received " in receiver_text
    )
    if not ok:
        print("local_process_smoke failed", file=sys.stderr)
        return 1

    print("local_process_smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
