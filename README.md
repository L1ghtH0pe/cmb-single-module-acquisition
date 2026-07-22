# CMB Single-Module Acquisition Prototype

Single-module prototype for high-speed parallel data acquisition from a detector module.

Current prototype assumptions:
- 1704 channels
- 32-bit sample per channel
- 200 Hz frame rate
- 5 ms frame period
- 6816-byte payload per frame
- TCP framing: `[header_len][header][payload][payload_crc]`
- Explicit little-endian wire encoding

## Build

### Windows MSYS2 UCRT64

```bash
PATH="/c/msys64/ucrt64/bin:$PATH" cmake -S . -B build -G Ninja
PATH="/c/msys64/ucrt64/bin:$PATH" cmake --build build
PATH="/c/msys64/ucrt64/bin:$PATH" ctest --test-dir build --output-on-failure
```

### Ubuntu 24.04

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build python3 git iproute2 ethtool iperf3
bash tools/ubuntu-build-test.sh
```

See `docs/ubuntu-runbook.md` for native Linux validation steps.

## Local smoke

Windows / MSYS2:

```bash
tools/run_local_smoke.cmd 9000 1000 90
```

PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File tools/run-local-smoke.ps1 -Port 9000 -Frames 1000 -Timeout 90
```

Ubuntu:

```bash
bash tools/run-local-smoke.sh 9000 1000 90
```

Ten-minute soak equivalent:

```bash
bash tools/run-local-smoke.sh 9000 120000 900
```

## Outputs

```text
logs/
captures/raw/segment-*.bin
captures/meta/segment-*.csv
```

Each raw segment contains concatenated payloads. The matching meta CSV records:

```csv
frame_id,timestamp_ns,offset,payload_bytes
```

## Docs

- `docs/system-spec.md`
- `docs/protocol-v0.1.md`
- `docs/environment-setup.md`
- `docs/test-plan.md`
- `docs/local-smoke-results.md`
- `docs/ubuntu-runbook.md`
