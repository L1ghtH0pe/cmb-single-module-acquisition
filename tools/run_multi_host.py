#!/usr/bin/env python3
"""Run all configured CMB channels for a two-host deployment.

Roles:
  prepare   sender host: generate one source .bin per channel with frame_gen
  receiver  control host: start one receiver per channel
  sender    sender host: start one sender per channel (optionally replaying source files)
  report    control host: compare received data with the source files, write report.csv/report.md
"""
from __future__ import annotations

import argparse
import csv
import ipaddress
import json
import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict, dataclass
from pathlib import Path

FRAME_BYTES = 4 + 44 + 1704 * 8 + 4  # CMB v2 wire frame
DEFAULT_RATE_HZ = 200
RATE_TOLERANCE = 0.02  # frame rate must be within +-2% of --rate-hz


@dataclass(frozen=True)
class Channel:
    module_id: int
    tx_iface: str
    rx_iface: str
    tx_ip: str
    rx_ip: str
    port: int
    tx_namespace: str | None
    rx_namespace: str | None


def project_root() -> Path:
    return Path(__file__).resolve().parents[1]


def exe_name(name: str) -> str:
    return f"{name}.exe" if os.name == "nt" else name


def optional_field(value: str) -> str | None:
    return None if value in {"-", "none", "None"} else value


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
            raise ValueError(f"{path}:{line_number}: module_id must be in 0..65535")
        if not 1 <= port <= 65535:
            raise ValueError(f"{path}:{line_number}: port must be in 1..65535")
        if module_id in seen_ids:
            raise ValueError(f"{path}:{line_number}: duplicate module_id {module_id}")
        if port in seen_ports:
            raise ValueError(f"{path}:{line_number}: duplicate port {port}")
        for label, value in (("tx_ip", fields[3]), ("rx_ip", fields[4])):
            try:
                ipaddress.IPv4Address(value)
            except ipaddress.AddressValueError as exc:
                raise ValueError(f"{path}:{line_number}: invalid {label} {value!r}") from exc
        seen_ids.add(module_id)
        seen_ports.add(port)
        channels.append(Channel(
            module_id=module_id,
            tx_iface=fields[1],
            rx_iface=fields[2],
            tx_ip=fields[3],
            rx_ip=fields[4],
            port=port,
            tx_namespace=optional_field(fields[6]),
            rx_namespace=optional_field(fields[7]),
        ))
    if not channels:
        raise ValueError(f"{path}: no channels configured")
    return channels


def source_file(source_dir: Path, channel: Channel) -> Path:
    return source_dir / f"source-m{channel.module_id:03d}.bin"


def command_for(channel: Channel, role: str, executable: Path, frames: int, timing_log: bool, save_sent: bool = False,
                rate_hz: int = DEFAULT_RATE_HZ, source_dir: Path | None = None) -> list[str]:
    if role == "receiver":
        command = [
            str(executable), str(channel.port), str(frames), channel.rx_ip,
            "--module-id", str(channel.module_id), "--rate-hz", str(rate_hz),
        ]
        namespace = channel.rx_namespace
    else:
        command = [
            str(executable), channel.rx_ip, str(channel.port), str(frames),
            "--module-id", str(channel.module_id), "--bind-host", channel.tx_ip, "--rate-hz", str(rate_hz),
        ]
        namespace = channel.tx_namespace
        if source_dir is not None:
            command.extend(["--input", str(source_file(source_dir, channel))])
        if save_sent:
            command.extend(["--save-sent", "captures/sent"])
    if timing_log:
        command.extend(["--timing-log", "logs/timing.csv"])
    if namespace:
        command = ["ip", "netns", "exec", namespace, *command]
    return command


def parse_args() -> argparse.Namespace:
    root = project_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--role", choices=("prepare", "receiver", "sender", "report"), required=True)
    parser.add_argument("--config", type=Path, default=root / "configs" / "ten-channel.example.conf")
    parser.add_argument("--frames", type=int, default=1000, help="frames per channel (12 h at 200 Hz: 8640000)")
    parser.add_argument("--rate-hz", type=int, default=DEFAULT_RATE_HZ, help="frame rate per channel (default 200)")
    parser.add_argument("--source-dir", type=Path,
                        help="prepare: write source files here; sender: replay them; report: compare against them")
    parser.add_argument("--build-dir", type=Path, default=root / "build")
    parser.add_argument("--output", type=Path, default=Path("/tmp/cmb-multi-host"))
    parser.add_argument("--timing-log", action="store_true", help="write per-frame timing CSV files")
    parser.add_argument("--save-sent", action="store_true", help="sender: also save sent frames to captures/sent for frame_compare")
    parser.add_argument("--dry-run", action="store_true", help="validate and print commands without starting processes")
    return parser.parse_args()


def stop_processes(processes: list[subprocess.Popen[str]]) -> None:
    for process in processes:
        if process.poll() is None:
            process.terminate()
    deadline = time.monotonic() + 3.0
    for process in processes:
        if process.poll() is None:
            try:
                process.wait(timeout=max(0.0, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


def check_disk(path: Path, needed_bytes: int, what: str) -> None:
    path.mkdir(parents=True, exist_ok=True)
    free = shutil.disk_usage(path).free
    if free < needed_bytes:
        raise OSError(f"{what} needs {needed_bytes / 1e9:.1f} GB under {path}, only {free / 1e9:.1f} GB free")


def run_parallel(jobs: list, workers: int) -> list:
    with ThreadPoolExecutor(max_workers=max(1, min(workers, len(jobs)))) as pool:
        return list(pool.map(lambda job: job(), jobs))


def prepare(args: argparse.Namespace, channels: list[Channel]) -> int:
    """Generate one reproducible source file per channel (random pattern, seed = module_id + 1)."""
    if args.source_dir is None:
        raise ValueError("--role prepare requires --source-dir")
    source_dir = args.source_dir.resolve()
    gen = (args.build_dir / exe_name("frame_gen")).resolve()
    commands = [[str(gen), str(source_file(source_dir, c)), str(args.frames), "--module-id", str(c.module_id),
                 "--pattern", "random", "--seed", str(c.module_id + 1), "--rate-hz", str(args.rate_hz)]
                for c in channels]
    if args.dry_run:
        print("\n".join(shlex.join(c) for c in commands))
        return 0
    if not gen.is_file():
        raise FileNotFoundError(f"missing executable: {gen}")
    check_disk(source_dir, args.frames * FRAME_BYTES * len(channels), "source files")
    print(f"generating {len(channels)} source files of {args.frames} frames into {source_dir}", flush=True)
    results = run_parallel([lambda c=c: subprocess.run(c, capture_output=True, text=True) for c in commands], 4)
    failed = [(cmd, r) for cmd, r in zip(commands, results) if r.returncode != 0]
    for cmd, r in failed:
        print(f"frame_gen failed: {shlex.join(cmd)}\n{r.stderr}", file=sys.stderr)
    print(f"prepared {len(channels) - len(failed)}/{len(channels)} source files")
    return 0 if not failed else 1


def parse_compare(text: str) -> dict[str, str]:
    """Collect key=value pairs from the frame_compare summary lines."""
    values: dict[str, str] = {}
    for line in text.splitlines():
        if line.startswith(("frame_bytes=", "expected_frames=", "actual_frames=", "identical=", "compared_bits=", "result=")):
            prefix = "actual_" if line.startswith("actual_frames=") else "expected_" if line.startswith("expected_frames=") else ""
            for key, value in re.findall(r"(\w+)=(\S+)", line):
                values[key if key.endswith("_frames") or not prefix else prefix + key] = value
    return values


def last_metrics(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}
    with path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    return rows[-1] if rows else {}


def report(args: argparse.Namespace, channels: list[Channel]) -> int:
    """Compare every channel's received data with its source file and write the test record."""
    if args.source_dir is None:
        raise ValueError("--role report requires --source-dir")
    source_dir = args.source_dir.resolve()
    receiver_dir = args.output.resolve() / "receiver"
    compare = (args.build_dir / exe_name("frame_compare")).resolve()
    if not compare.is_file():
        raise FileNotFoundError(f"missing executable: {compare}")
    exits: dict[int, int] = {}
    summary_path = receiver_dir / "summary.json"
    if summary_path.exists():
        exits = {r["module_id"]: r["exit"] for r in json.loads(summary_path.read_text(encoding="utf-8")).get("results", [])}

    def check(channel: Channel) -> dict[str, object]:
        workdir = receiver_dir / f"module-{channel.module_id:03d}"
        result = subprocess.run([str(compare), str(source_file(source_dir, channel)), str(workdir / "captures" / "raw")],
                                capture_output=True, text=True)
        (workdir / "frame-compare.txt").write_text(result.stdout + result.stderr, encoding="utf-8")
        cmp = parse_compare(result.stdout)
        metrics = last_metrics(workdir / "logs" / "receiver-metrics.csv")
        frames = int(metrics.get("frame_count") or 0)
        span_us = int(metrics.get("recv_span_us") or 0)
        rate = (frames - 1) * 1e6 / span_us if frames > 1 and span_us > 0 else 0.0
        sent = int(cmp.get("expected_frames", 0))
        received = int(cmp.get("actual_frames", 0))
        missing = int(cmp.get("missing", sent))
        bit_errors = int(cmp.get("bit_errors", -1))
        checks = {
            "receiver_exit": exits.get(channel.module_id, 0) == 0,
            "frame_rate": abs(rate - args.rate_hz) <= RATE_TOLERANCE * args.rate_hz,
            "frame_count": sent == args.frames and received == sent,
            "frame_loss": missing == 0,
            "bit_errors": bit_errors == 0,
            "frame_compare": result.returncode == 0,
        }
        return {
            "module_id": channel.module_id, "rate_hz": round(rate, 3), "sent_frames": sent, "received_frames": received,
            "missing_frames": missing, "frame_loss_rate": missing / sent if sent else 1.0, "bit_errors": bit_errors,
            "ber": cmp.get("ber", ""), "failed_checks": ",".join(k for k, ok in checks.items() if not ok),
            "pass": all(checks.values()),
        }

    rows = run_parallel([lambda c=c: check(c) for c in channels], 4)
    out = args.output.resolve()
    fields = list(rows[0].keys())
    with (out / "report.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    total_bits = sum(r["received_frames"] for r in rows) * FRAME_BYTES * 8
    total_errors = sum(max(0, r["bit_errors"]) for r in rows)
    total_sent = sum(r["sent_frames"] for r in rows)
    total_missing = sum(r["missing_frames"] for r in rows)
    passed = all(r["pass"] for r in rows)
    lines = [
        f"# 数据模拟软件测试记录（{len(rows)} 路，{args.rate_hz} Hz，每路 {args.frames} 帧）", "",
        "| 模块号 | 帧速率 (Hz) | 发出帧数 | 接收帧数 | 丢帧率 | 误码比特数 | 误码率 | 是否符合 |",
        "|---:|---:|---:|---:|---:|---:|---:|---|",
    ]
    for r in rows:
        lines.append(f"| {r['module_id']} | {r['rate_hz']:.3f} | {r['sent_frames']} | {r['received_frames']} | "
                     f"{r['frame_loss_rate']:.3e} | {r['bit_errors']} | {r['ber']} | "
                     f"{'符合' if r['pass'] else '不符合（' + r['failed_checks'] + '）'} |")
    lines += ["", f"合计：发出 {total_sent} 帧，接收 {total_sent - total_missing} 帧，丢帧率 "
              f"{(total_missing / total_sent if total_sent else 1):.3e}；接收 {total_bits} 比特，误码 {total_errors} 比特，误码率 "
              f"{(total_errors / total_bits if total_bits else 0):.3e}", "", f"结论：{'通过' if passed else '不通过'}"]
    (out / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    return 0 if passed else 1


def wait_receivers_ready(workdirs: list[Path], processes: list[subprocess.Popen[str]], timeout_s: float = 15.0) -> None:
    deadline = time.monotonic() + timeout_s
    pending = list(zip(workdirs, processes))
    while pending and time.monotonic() < deadline:
        for item in list(pending):
            workdir, process = item
            if process.poll() is not None:
                raise OSError(f"receiver exited during startup: {workdir}")
            log = workdir / "logs" / "receiver-runtime.log"
            if log.exists() and "listening on" in log.read_text(encoding="utf-8", errors="replace"):
                pending.remove(item)
        time.sleep(0.05)
    if pending:
        raise OSError(f"{len(pending)} receivers not listening after {timeout_s:.0f} s")
    print(f"ready: {len(workdirs)} receivers listening; start the sender role now", flush=True)


def main() -> int:
    args = parse_args()
    if args.frames <= 0:
        raise ValueError("--frames must be positive")

    if not 1 <= args.rate_hz <= 1000:
        raise ValueError("--rate-hz must be in 1..1000")

    channels = parse_config(args.config)
    if args.role == "prepare":
        return prepare(args, channels)
    if args.role == "report":
        return report(args, channels)
    source_dir = args.source_dir.resolve() if args.source_dir and args.role == "sender" else None
    executable = (args.build_dir / exe_name(args.role)).resolve()
    commands = [command_for(channel, args.role, executable, args.frames, args.timing_log, args.save_sent,
                            args.rate_hz, source_dir) for channel in channels]
    plan = {
        "role": args.role,
        "frames": args.frames,
        "rate_hz": args.rate_hz,
        "config": str(args.config.resolve()),
        "channels": [
            {**asdict(channel), "command": shlex.join(command)}
            for channel, command in zip(channels, commands)
        ],
    }
    if args.dry_run:
        print(json.dumps(plan, indent=2))
        return 0
    if not executable.is_file():
        raise FileNotFoundError(f"missing executable: {executable}")
    if source_dir is not None:
        missing = [str(source_file(source_dir, c)) for c in channels if not source_file(source_dir, c).is_file()]
        if missing:
            raise FileNotFoundError("missing source files (run --role prepare first): " + ", ".join(missing))

    output = args.output.resolve() / args.role
    previous = [p for c in channels for p in [output / f"module-{c.module_id:03d}" / "captures"] if p.exists()]
    if previous:
        raise OSError(f"{previous[0]} already exists; use a new --output so earlier results are not overwritten")
    output.mkdir(parents=True, exist_ok=True)
    if args.role == "receiver":
        check_disk(output, args.frames * FRAME_BYTES * len(channels), "receiver captures")
    elif args.save_sent:
        check_disk(output, args.frames * FRAME_BYTES * len(channels), "sender --save-sent captures")
    processes: list[subprocess.Popen[str]] = []
    handles = []
    results: list[dict[str, object]] = []
    return_code = 1
    launch_error: str | None = None

    def stop_handler(_signum: int, _frame: object) -> None:
        stop_processes(processes)
        raise KeyboardInterrupt

    previous_sigterm = signal.signal(signal.SIGTERM, stop_handler)
    try:
        for channel, command in zip(channels, commands):
            workdir = output / f"module-{channel.module_id:03d}"
            (workdir / "logs").mkdir(parents=True, exist_ok=True)
            stdout = (workdir / f"{args.role}.stdout.log").open("w", encoding="utf-8")
            stderr = (workdir / f"{args.role}.stderr.log").open("w", encoding="utf-8")
            handles.extend((stdout, stderr))
            processes.append(subprocess.Popen(command, cwd=workdir, stdout=stdout, stderr=stderr, text=True))
        if args.role == "receiver":
            wait_receivers_ready([output / f"module-{c.module_id:03d}" for c in channels], processes)

        for channel, command, process in zip(channels, commands, processes):
            exit_code = process.wait()
            results.append({
                "module_id": channel.module_id,
                "port": channel.port,
                "exit": exit_code,
                "command": shlex.join(command),
                "status": "passed" if exit_code == 0 else "failed",
            })
        return_code = 0 if all(result["exit"] == 0 for result in results) else 1
    except KeyboardInterrupt:
        stop_processes(processes)
        return_code = 130
    except OSError as exc:
        stop_processes(processes)
        launch_error = str(exc)
        return_code = 2
    finally:
        signal.signal(signal.SIGTERM, previous_sigterm)
        for handle in handles:
            handle.close()
        summary = {
            **plan,
            "status": "passed" if return_code == 0 else "interrupted" if return_code == 130 else "failed",
            "results": results,
        }
        if launch_error is not None:
            summary["error"] = launch_error
        (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    print(json.dumps(summary, indent=2))
    return return_code


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as exc:
        print(f"multi-host launch failed: {exc}", file=sys.stderr)
        raise SystemExit(2)
