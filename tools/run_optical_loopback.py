#!/usr/bin/env python3
"""Run a single-machine, two-port optical Ethernet loopback test."""
from __future__ import annotations

import argparse
import csv
import struct
import datetime as dt
import json
import os
import pwd
import subprocess
import sys
import time
from pathlib import Path

FRAME_BYTES = 4 + 44 + 1704 * 8 + 4  # wire frame, stored as-is in .bin
FRAME_ID_OFFSET = 16
DEFAULT_TX_IFACE = "enp175s0f0"
DEFAULT_RX_IFACE = "enp175s0f1"
DEFAULT_TX_NS = "cmb-tx"
DEFAULT_RX_NS = "cmb-rx"
ERROR_COUNTERS = (
    "rx_errors", "tx_errors", "rx_dropped", "tx_dropped",
    "rx_crc_errors", "rx_frame_errors", "rx_missed_errors",
    "tx_carrier_errors", "tx_timeout_count",
)


def run(cmd: list[str], *, check: bool = True, capture: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, check=check, text=True, capture_output=capture)


def sudo_reexec_if_needed() -> None:
    if os.geteuid() == 0:
        return
    print("This test needs root for network namespaces; requesting sudo...", flush=True)
    os.execvp("sudo", ["sudo", sys.executable, *sys.argv])


def project_root() -> Path:
    return Path(__file__).resolve().parents[1]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", choices=("1000", "10m", "1h"), help="test duration preset (default: 10m when --frames is not given)")
    parser.add_argument("--frames", type=int, help="number of frames (overrides --preset)")
    parser.add_argument("--rate-hz", type=int, default=200, help="frame rate (default 200; 240 for the margin test)")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--tx-iface", default=DEFAULT_TX_IFACE)
    parser.add_argument("--rx-iface", default=DEFAULT_RX_IFACE)
    parser.add_argument("--tx-ns", default=DEFAULT_TX_NS)
    parser.add_argument("--rx-ns", default=DEFAULT_RX_NS)
    parser.add_argument("--tx-ip", default="192.168.210.1/30")
    parser.add_argument("--rx-ip", default="192.168.210.2/30")
    parser.add_argument("--build-dir", type=Path, default=project_root() / "build")
    parser.add_argument("--skip-build", action="store_true", help="reuse existing sender and receiver binaries")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--startup-timeout", type=float, default=10.0)
    parser.add_argument("--timeout-slack", type=float, default=60.0)
    parser.add_argument("--keep-namespaces", action="store_true")
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--input", type=Path, help="replay this pre-built .bin (from frame_gen) instead of live simulation")
    source.add_argument("--gen-pattern", choices=("counter", "random"), help="generate captures/source.bin with frame_gen, then replay it")
    timing = parser.add_mutually_exclusive_group()
    timing.add_argument("--sender-timing", dest="sender_timing", action="store_true", default=True, help="write per-frame sender timing diagnostics (default)")
    timing.add_argument("--no-sender-timing", dest="sender_timing", action="store_false", help="disable per-frame sender timing diagnostics")
    deadline = parser.add_mutually_exclusive_group()
    deadline.add_argument("--fail-on-deadline-miss", dest="fail_on_deadline_miss", action="store_true", default=True, help="fail on sender or receiver deadline misses (default)")
    deadline.add_argument("--allow-deadline-miss", dest="fail_on_deadline_miss", action="store_false", help="record deadline misses without failing the test")
    args = parser.parse_args()
    if args.frames is not None and args.preset:
        parser.error("use either --frames or --preset, not both")
    if args.frames is None:
        seconds = {"1000": None, "10m": 600, "1h": 3600}[args.preset or "10m"]
        args.frames = 1000 if seconds is None else seconds * args.rate_hz
    if args.frames is None or args.frames <= 0:
        parser.error("provide --frames or --preset")
    if not 1 <= args.rate_hz <= 1000:
        parser.error("--rate-hz must be in 1..1000")
    if not 1 <= args.port <= 65535:
        parser.error("--port must be in 1..65535")
    return args


def ns_exec(namespace: str, command: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    return run(["ip", "netns", "exec", namespace, *command], check=check)


def iface_exists(iface: str) -> bool:
    return run(["ip", "link", "show", "dev", iface], check=False).returncode == 0


def ns_exists(namespace: str) -> bool:
    result = run(["ip", "netns", "list"], check=False)
    return any(line.split() and line.split()[0] == namespace for line in result.stdout.splitlines())


def setup_namespace(namespace: str, iface: str, address: str) -> bool:
    created = False
    if not ns_exists(namespace):
        run(["ip", "netns", "add", namespace])
        created = True
    if iface_exists(iface):
        run(["ip", "link", "set", iface, "netns", namespace])
    links = ns_exec(namespace, ["ip", "link", "show"], check=False).stdout
    if iface not in links:
        raise RuntimeError(f"{iface} is not available in namespace {namespace}")
    ns_exec(namespace, ["ip", "link", "set", "lo", "up"])
    ns_exec(namespace, ["ip", "addr", "flush", "dev", iface])
    ns_exec(namespace, ["ip", "addr", "add", address, "dev", iface])
    ns_exec(namespace, ["ip", "link", "set", iface, "up"])
    return created


def filtered_stats(namespace: str, iface: str) -> dict[str, int]:
    result = ns_exec(namespace, ["ethtool", "-S", iface], check=False)
    values: dict[str, int] = {}
    for line in result.stdout.splitlines():
        if ":" not in line:
            continue
        key, value = (part.strip() for part in line.split(":", 1))
        if key in ERROR_COUNTERS:
            try:
                values[key] = int(value)
            except ValueError:
                pass
    return values


def save_command(output: Path, name: str, command: list[str], result: subprocess.CompletedProcess[str]) -> None:
    (output / f"{name}.command.txt").write_text(" ".join(command) + "\n", encoding="utf-8")
    (output / f"{name}.stdout").write_text(result.stdout, encoding="utf-8")
    (output / f"{name}.stderr").write_text(result.stderr, encoding="utf-8")


def ensure_runtime_dirs(output: Path, user_name: str) -> None:
    for relative in ("logs", "captures/raw", "captures/sent"):
        (output / relative).mkdir(parents=True, exist_ok=True)
    user = pwd.getpwnam(user_name)
    for child in (output, output / "logs", output / "captures", output / "captures/raw", output / "captures/sent"):
        os.chown(child, user.pw_uid, user.pw_gid)


def command_as_user(user_name: str, command: list[str]) -> list[str]:
    if os.geteuid() == 0 and user_name != "root":
        return ["runuser", "-u", user_name, "--", *command]
    return command


def ensure_built(root: Path, build_dir: Path, output: Path, run_user: str, skip_build: bool) -> dict[str, object]:
    required = [build_dir / name for name in ("sender", "receiver", "frame_gen", "frame_compare")]
    result: dict[str, object] = {"skipped": skip_build, "build_dir": str(build_dir)}
    if skip_build:
        if not all(path.is_file() for path in required):
            raise RuntimeError(f"--skip-build requested but binaries are missing under {build_dir}")
        return result

    configure_command = command_as_user(run_user, ["cmake", "-S", str(root), "-B", str(build_dir)])
    print("Configuring project before optical test...", flush=True)
    configure_result = run(configure_command, check=False)
    save_command(output, "build-configure", configure_command, configure_result)
    result["configure_exit"] = configure_result.returncode
    if configure_result.returncode != 0:
        raise RuntimeError("CMake configure failed; see build-configure.stdout and build-configure.stderr")

    parallelism = str(os.cpu_count() or 1)
    build_command = command_as_user(run_user, ["cmake", "--build", str(build_dir), "--parallel", parallelism])
    print("Building project before optical test...", flush=True)
    build_result = run(build_command, check=False)
    save_command(output, "build", build_command, build_result)
    result["build_exit"] = build_result.returncode
    if build_result.returncode != 0:
        raise RuntimeError("CMake build failed; see build.stdout and build.stderr")

    if not all(path.is_file() for path in required):
        raise RuntimeError(f"build completed but sender/receiver/frame_gen/frame_compare is missing under {build_dir}")
    return result


def capture_baseline(output: Path, label: str, tx_ns: str, rx_ns: str, tx_iface: str, rx_iface: str) -> dict[str, dict[str, int]]:
    stats = {
        "tx": filtered_stats(tx_ns, tx_iface),
        "rx": filtered_stats(rx_ns, rx_iface),
    }
    (output / f"stats-{label}.json").write_text(json.dumps(stats, indent=2) + "\n", encoding="utf-8")
    for side, namespace, iface in (("tx", tx_ns, tx_iface), ("rx", rx_ns, rx_iface)):
        for name, command in (("link", ["ethtool", iface]), ("driver", ["ethtool", "-i", iface]), ("dom", ["ethtool", "-m", iface])):
            command_result = ns_exec(namespace, command, check=False)
            (output / f"{label}-{side}-{name}.txt").write_text(
                (command_result.stdout or "") + (command_result.stderr or ""), encoding="utf-8"
            )
    return stats


def wait_for_listener(process: subprocess.Popen[str], runtime_log: Path, deadline: float) -> None:
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError("receiver exited before becoming ready")
        if runtime_log.exists() and "listening on " in runtime_log.read_text(encoding="utf-8", errors="replace"):
            return
        time.sleep(0.2)
    raise RuntimeError("timed out waiting for receiver")


def read_metrics(path: Path) -> dict[str, int | str]:
    if not path.exists():
        return {}
    with path.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        return {}
    row = rows[-1]
    result: dict[str, int | str] = {}
    for key, value in row.items():
        try:
            result[key] = int(value)
        except (ValueError, TypeError):
            result[key] = value
    return result


def validate_captures(output: Path, expected_frames: int) -> dict[str, object]:
    segment_results = []
    all_ids: list[int] = []
    for raw in sorted((output / "captures" / "raw").glob("segment-*.bin")):
        data = raw.read_bytes()
        size_ok = len(data) % FRAME_BYTES == 0
        ids = [struct.unpack_from("<Q", data, offset + FRAME_ID_OFFSET)[0] for offset in range(0, len(data) - FRAME_BYTES + 1, FRAME_BYTES)]
        contiguous = bool(ids) and all(b == a + 1 for a, b in zip(ids, ids[1:]))
        segment_results.append({"file": raw.name, "frames": len(ids), "first": ids[0] if ids else None, "last": ids[-1] if ids else None, "contiguous": contiguous, "raw_bytes": len(data), "raw_size_ok": size_ok})
        all_ids.extend(ids)
    continuous = all_ids == list(range(expected_frames))
    return {"segments": segment_results, "frame_count": len(all_ids), "expected_frames": expected_frames, "continuous": continuous, "raw_sizes_ok": bool(segment_results) and all(bool(segment["raw_size_ok"]) for segment in segment_results), "raw_bytes": sum(int(segment["raw_bytes"]) for segment in segment_results)}


def compare_frames(output: Path, build_dir: Path, expected: Path, name: str) -> dict[str, object]:
    command = [str(build_dir / "frame_compare"), str(expected), str(output / "captures" / "raw")]
    result = run(command, check=False)
    (output / f"{name}.txt").write_text(" ".join(command) + "\n" + result.stdout + result.stderr, encoding="utf-8")
    return {"expected": str(expected), "exit": result.returncode, "identical": result.returncode == 0, "report": f"{name}.txt"}


def main() -> int:
    args = parse_args()
    sudo_reexec_if_needed()
    root = project_root()
    build_dir = args.build_dir if args.build_dir.is_absolute() else root / args.build_dir
    build_dir = build_dir.resolve()
    timestamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output = (args.output_dir or root / "test-results" / f"optical-{timestamp}").resolve()
    output.mkdir(parents=True, exist_ok=False)
    run_user = os.environ.get("SUDO_USER") or os.environ.get("USER") or "root"
    ensure_runtime_dirs(output, run_user)
    (output / "commands.log").write_text(" ".join(sys.argv) + "\n", encoding="utf-8")

    created: list[tuple[str, bool]] = []
    receiver_proc: subprocess.Popen[str] | None = None
    sender_proc: subprocess.Popen[str] | None = None
    result: dict[str, object] = {
        "frames_expected": args.frames,
        "output_dir": str(output),
        "status": "failed",
        "sender_timing_enabled": args.sender_timing,
        "receiver_timing_enabled": True,
        "fail_on_deadline_miss": args.fail_on_deadline_miss,
    }
    try:
        print(
            f"Preparing {args.frames} frames at {args.rate_hz} Hz; TX={args.tx_iface}, RX={args.rx_iface}, "
            f"sender_timing={'on' if args.sender_timing else 'off'}, deadline_policy="
            f"{'strict' if args.fail_on_deadline_miss else 'record-only'}.",
            flush=True,
        )
        result["build"] = ensure_built(root, build_dir, output, run_user, args.skip_build)
        sender = build_dir / "sender"
        receiver = build_dir / "receiver"
        source_file: Path | None = None
        if args.gen_pattern:
            source_file = output / "captures" / "source.bin"
            gen_command = command_as_user(run_user, [str(build_dir / "frame_gen"), str(source_file), str(args.frames), "--pattern", args.gen_pattern, "--rate-hz", str(args.rate_hz)])
            gen_result = run(gen_command, check=False)
            save_command(output, "frame-gen", gen_command, gen_result)
            if gen_result.returncode != 0:
                raise RuntimeError("frame_gen failed; see frame-gen.stderr")
        elif args.input:
            source_file = args.input.resolve()
            if not source_file.exists():
                raise RuntimeError(f"--input not found: {source_file}")
        result["source_file"] = str(source_file) if source_file else None
        if not iface_exists(args.tx_iface) and not ns_exists(args.tx_ns):
            raise RuntimeError(f"missing TX interface {args.tx_iface}")
        if not iface_exists(args.rx_iface) and not ns_exists(args.rx_ns):
            raise RuntimeError(f"missing RX interface {args.rx_iface}")

        created.append((args.tx_ns, setup_namespace(args.tx_ns, args.tx_iface, args.tx_ip)))
        created.append((args.rx_ns, setup_namespace(args.rx_ns, args.rx_iface, args.rx_ip)))
        tx_ip = args.tx_ip.split("/", 1)[0]
        rx_ip = args.rx_ip.split("/", 1)[0]
        if ns_exec(args.tx_ns, ["ping", "-c", "3", "-W", "1", rx_ip], check=False).returncode != 0:
            raise RuntimeError("namespace ping failed")

        before = capture_baseline(output, "before", args.tx_ns, args.rx_ns, args.tx_iface, args.rx_iface)
        duration = args.frames / args.rate_hz
        timeout = duration + args.timeout_slack
        receiver_out = (output / "receiver.stdout").open("w")
        receiver_err = (output / "receiver.stderr").open("w")
        receiver_command = ["ip", "netns", "exec", args.rx_ns, "runuser", "-u", run_user, "--", str(receiver), str(args.port), str(args.frames), rx_ip, "--rate-hz", str(args.rate_hz), "--timing-log", "logs/receiver-timing.csv"]
        receiver_proc = subprocess.Popen(receiver_command, cwd=output, stdout=receiver_out, stderr=receiver_err, text=True)
        wait_for_listener(receiver_proc, output / "logs" / "receiver-runtime.log", time.monotonic() + args.startup_timeout)

        sender_out = (output / "sender.stdout").open("w")
        sender_err = (output / "sender.stderr").open("w")
        sender_command = ["ip", "netns", "exec", args.tx_ns, "runuser", "-u", run_user, "--", str(sender), rx_ip, str(args.port), str(args.frames), "--rate-hz", str(args.rate_hz), "--save-sent", "captures/sent"]
        if source_file:
            sender_command.extend(["--input", str(source_file)])
        if args.sender_timing:
            sender_command.extend(["--timing-log", "logs/sender-timing.csv"])
        sender_proc = subprocess.Popen(sender_command, cwd=output, stdout=sender_out, stderr=sender_err, text=True)
        try:
            sender_exit = sender_proc.wait(timeout=timeout)
            receiver_exit = receiver_proc.wait(timeout=max(10.0, args.timeout_slack))
        except subprocess.TimeoutExpired:
            raise RuntimeError("business test timed out")
        finally:
            sender_out.close()
            sender_err.close()
            receiver_out.close()
            receiver_err.close()

        after = capture_baseline(output, "after", args.tx_ns, args.rx_ns, args.tx_iface, args.rx_iface)
        sender_metrics = read_metrics(output / "logs" / "sender-metrics.csv")
        receiver_metrics = read_metrics(output / "logs" / "receiver-metrics.csv")
        captures = validate_captures(output, args.frames)
        comparison = {"sent_vs_received": compare_frames(output, build_dir, output / "captures" / "sent", "frame-compare")}
        if source_file:
            comparison["source_vs_received"] = compare_frames(output, build_dir, source_file, "frame-compare-source")
        comparison["identical"] = all(item["identical"] for item in comparison.values() if isinstance(item, dict))
        deltas = {side: {key: after[side].get(key, 0) - before[side].get(key, 0) for key in set(before[side]) | set(after[side])} for side in ("tx", "rx")}
        nic_errors_zero = all(value == 0 for counters in deltas.values() for value in counters.values())
        hard_ok = sender_exit == 0 and receiver_exit == 0 and sender_metrics.get("frame_count") == args.frames and receiver_metrics.get("frame_count") == args.frames and receiver_metrics.get("parse_fail_count", 1) == 0 and receiver_metrics.get("crc_error_count", 1) == 0 and receiver_metrics.get("tcp_disconnect_count", 1) == 0 and captures["continuous"] and captures["raw_sizes_ok"] and comparison["identical"] and nic_errors_zero
        receiver_deadline_misses = int(receiver_metrics.get("recv_deadline_miss_count", 0))
        sender_deadline_misses = int(sender_metrics.get("send_deadline_miss_count", 0))
        deadline_misses = receiver_deadline_misses + sender_deadline_misses
        result.update({
            "status": "passed" if hard_ok and (not args.fail_on_deadline_miss or deadline_misses == 0) else "failed",
            "sender_exit": sender_exit,
            "receiver_exit": receiver_exit,
            "sender_metrics": sender_metrics,
            "receiver_metrics": receiver_metrics,
            "captures": captures,
            "frame_compare": comparison,
            "counter_deltas": deltas,
            "nic_errors_zero": nic_errors_zero,
            "deadline_misses": receiver_deadline_misses,
            "receiver_deadline_misses": receiver_deadline_misses,
            "sender_deadline_misses": sender_deadline_misses,
        })
    except Exception as exc:
        result["error"] = str(exc)
        print(f"optical loopback failed: {exc}", file=sys.stderr)
    finally:
        if sender_proc and sender_proc.poll() is None:
            sender_proc.kill()
            sender_proc.wait()
        if receiver_proc and receiver_proc.poll() is None:
            receiver_proc.kill()
            receiver_proc.wait()
        if not args.keep_namespaces:
            for namespace, was_created in reversed(created):
                if was_created:
                    run(["ip", "netns", "del", namespace], check=False)
        (output / "summary.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")

    print(json.dumps(result, indent=2))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
