#!/usr/bin/env python3
"""Launch all configured CMB channels for one side of a two-host deployment."""
from __future__ import annotations

import argparse
import ipaddress
import json
import os
import shlex
import signal
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from pathlib import Path


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


def command_for(channel: Channel, role: str, executable: Path, frames: int, timing_log: bool) -> list[str]:
    if role == "receiver":
        command = [
            str(executable), str(channel.port), str(frames), channel.rx_ip,
            "--module-id", str(channel.module_id),
        ]
        namespace = channel.rx_namespace
    else:
        command = [
            str(executable), channel.rx_ip, str(channel.port), str(frames),
            "--module-id", str(channel.module_id), "--bind-host", channel.tx_ip,
        ]
        namespace = channel.tx_namespace
    if timing_log:
        command.extend(["--timing-log", "logs/timing.csv"])
    if namespace:
        command = ["ip", "netns", "exec", namespace, *command]
    return command


def parse_args() -> argparse.Namespace:
    root = project_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--role", choices=("receiver", "sender"), required=True)
    parser.add_argument("--config", type=Path, default=root / "configs" / "ten-channel.example.conf")
    parser.add_argument("--frames", type=int, default=1000)
    parser.add_argument("--build-dir", type=Path, default=root / "build")
    parser.add_argument("--output", type=Path, default=Path("/tmp/cmb-multi-host"))
    parser.add_argument("--timing-log", action="store_true", help="write per-frame timing CSV files")
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


def main() -> int:
    args = parse_args()
    if args.frames <= 0:
        raise ValueError("--frames must be positive")

    channels = parse_config(args.config)
    executable = (args.build_dir / exe_name(args.role)).resolve()
    commands = [command_for(channel, args.role, executable, args.frames, args.timing_log) for channel in channels]
    plan = {
        "role": args.role,
        "frames": args.frames,
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

    output = args.output.resolve() / args.role
    output.mkdir(parents=True, exist_ok=True)
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
