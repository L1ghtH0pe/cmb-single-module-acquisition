#!/usr/bin/env python3
"""Run ten independent sender/receiver processes on localhost."""
from __future__ import annotations

import argparse
import csv
import json
import shutil
import signal
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Channel:
    module_id: int
    port: int


def parse_config(path: Path) -> list[Channel]:
    channels: list[Channel] = []
    seen_ids: set[int] = set()
    seen_ports: set[int] = set()
    for line_number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        fields = line.split()
        if len(fields) != 8:
            raise ValueError(f"{path}:{line_number}: expected 8 fields")
        module_id = int(fields[0])
        port = int(fields[5])
        if not 0 <= module_id <= 65535:
            raise ValueError(f"{path}:{line_number}: module_id out of range")
        if not 1 <= port <= 65535:
            raise ValueError(f"{path}:{line_number}: port out of range")
        if module_id in seen_ids or port in seen_ports:
            raise ValueError(f"{path}:{line_number}: duplicate module_id or port")
        seen_ids.add(module_id)
        seen_ports.add(port)
        channels.append(Channel(module_id, port))
    if len(channels) != 10:
        raise ValueError(f"{path}: expected exactly 10 channels, got {len(channels)}")
    return channels


def wait_for_listener(runtime_log: Path, process: subprocess.Popen[str], deadline: float) -> None:
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"receiver exited before ready: {runtime_log}")
        if runtime_log.exists() and "receiver listening on" in runtime_log.read_text(encoding="utf-8", errors="replace"):
            return
        time.sleep(0.02)
    raise TimeoutError(f"receiver did not become ready: {runtime_log}")


def read_metrics(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}
    with path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    return rows[-1] if rows else {}


def metric_int(metrics: dict[str, str], key: str, default: int = -1) -> int:
    try:
        return int(metrics.get(key, str(default)))
    except (TypeError, ValueError):
        return default


def validate_channel(output: Path, channel: Channel, frames: int) -> dict[str, object]:
    module = f"module-{channel.module_id:04d}"
    module_dir = output / "captures" / module
    receiver_metrics = read_metrics(module_dir / "logs" / "receiver-metrics.csv")
    sender_metrics = read_metrics(module_dir / "logs" / "sender-metrics.csv")
    raw_files = sorted((module_dir / "captures" / "raw").glob("segment-*.bin"))
    index_files = sorted((module_dir / "captures" / "meta").glob("segment-*.csv"))
    raw_bytes = sum(path.stat().st_size for path in raw_files)
    receiver_frames = metric_int(receiver_metrics, "frame_count")
    sender_frames = metric_int(sender_metrics, "frame_count")
    errors = [key for key in ("parse_fail_count", "crc_error_count", "tcp_disconnect_count", "capture_queue_overrun_count")
              if metric_int(receiver_metrics, key) != 0]
    if sender_frames != frames or receiver_frames != frames:
        errors.append("frame_count")
    if raw_bytes != frames * 6816:
        errors.append("raw_bytes")
    return {"module_id": channel.module_id, "port": channel.port,
            "sender_exit": None, "receiver_exit": None,
            "sender_frames": sender_frames, "receiver_frames": receiver_frames,
            "raw_bytes": raw_bytes, "raw_segments": len(raw_files),
            "index_segments": len(index_files), "errors": errors}


def stop_all(processes: list[subprocess.Popen[str]]) -> None:
    for process in processes:
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
    for process in processes:
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def main() -> int:
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parents[1]
    parser.add_argument("--config", type=Path, default=root / "configs" / "ten-channel.example.conf")
    parser.add_argument("--frames", type=int, default=1000)
    parser.add_argument("--build-dir", type=Path, default=root / "build")
    parser.add_argument("--output", type=Path, default=Path("/tmp/cmb-ten-way-local"))
    args = parser.parse_args()
    args.config = args.config.resolve()
    args.build_dir = args.build_dir.resolve()
    args.output = args.output.resolve()
    if args.frames <= 0:
        parser.error("--frames must be positive")

    try:
        channels = parse_config(args.config)
    except (OSError, ValueError) as exc:
        print(f"configuration error: {exc}", file=sys.stderr)
        return 2

    sender = args.build_dir / "sender"
    receiver = args.build_dir / "receiver"
    if not sender.exists() or not receiver.exists():
        print("build/sender and build/receiver are required; run cmake --build build first", file=sys.stderr)
        return 2

    shutil.rmtree(args.output, ignore_errors=True)
    args.output.mkdir(parents=True)
    (args.output / "logs").mkdir()
    (args.output / "captures").mkdir()
    receiver_processes: list[subprocess.Popen[str]] = []
    receiver_ready: list[tuple[Path, subprocess.Popen[str]]] = []
    sender_processes: list[subprocess.Popen[str]] = []
    log_handles: list[object] = []
    results: list[dict[str, object]] = []
    try:
        for channel in channels:
            module = f"module-{channel.module_id:04d}"
            module_dir = args.output / "captures" / module
            module_dir.mkdir()
            (module_dir / "logs").mkdir()
            receiver_log_path = args.output / "logs" / f"{module}-receiver.stdout"
            receiver_log_path.parent.mkdir(parents=True, exist_ok=True)
            receiver_log = receiver_log_path.open("w", buffering=1)
            receiver_err = (args.output / "logs" / f"{module}-receiver.stderr").open("w")
            log_handles.extend([receiver_log, receiver_err])
            receiver_process = subprocess.Popen(
                [str(receiver), str(channel.port), str(args.frames), "127.0.0.1",
                 "--module-id", str(channel.module_id),
                 "--timing-log", str(module_dir / "logs" / "receiver-timing.csv")],
                cwd=module_dir, stdout=receiver_log,
                stderr=receiver_err, text=True, bufsize=1)
            receiver_processes.append(receiver_process)
            receiver_ready.append((receiver_log_path, receiver_process))
        for receiver_log_path, receiver_process in receiver_ready:
            wait_for_listener(receiver_log_path, receiver_process, time.monotonic() + 10)
        for channel in channels:
            module = f"module-{channel.module_id:04d}"
            module_dir = args.output / "captures" / module
            sender_log = (args.output / "logs" / f"{module}-sender.stdout").open("w")
            sender_err = (args.output / "logs" / f"{module}-sender.stderr").open("w")
            log_handles.extend([sender_log, sender_err])
            sender_processes.append(subprocess.Popen(
                [str(sender), "127.0.0.1", str(channel.port), str(args.frames),
                 "--module-id", str(channel.module_id),
                 "--timing-log", str(module_dir / "logs" / "sender-timing.csv")],
                cwd=module_dir, stdout=sender_log,
                stderr=sender_err, text=True))
        sender_codes = [process.wait(timeout=max(30, args.frames // 100 + 10)) for process in sender_processes]
        receiver_codes = [process.wait(timeout=30) for process in receiver_processes]
        for channel, sender_code, receiver_code in zip(channels, sender_codes, receiver_codes):
            result = validate_channel(args.output, channel, args.frames)
            result["sender_exit"] = sender_code
            result["receiver_exit"] = receiver_code
            if sender_code != 0:
                result["errors"].append("sender_exit")
            if receiver_code != 0:
                result["errors"].append("receiver_exit")
            results.append(result)
    except Exception as exc:
        results.append({"error": str(exc)})
        return_code = 1
    else:
        return_code = 0 if all(r.get("sender_exit") == 0 and r.get("receiver_exit") == 0 and not r.get("errors") for r in results) else 1
    finally:
        stop_all(sender_processes + receiver_processes)
        for handle in log_handles:
            handle.close()
        summary = {"frames": args.frames, "channels": results,
                   "status": "passed" if return_code == 0 else "failed"}
        (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    print(json.dumps(summary, indent=2))
    return return_code


if __name__ == "__main__":
    raise SystemExit(main())
