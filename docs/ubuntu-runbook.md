# Ubuntu 24.04 Runbook

This runbook is the native-Linux path for reproducing the single-module CMB acquisition prototype outside the Windows/MSYS2 development environment.

## 1. Install dependencies

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build python3 git iproute2 ethtool iperf3
```

Recommended versions:
- Ubuntu 24.04 LTS
- GCC 13 or newer
- CMake 3.20 or newer
- Ninja 1.11 or newer

## 2. Build and run tests

From the project root:

```bash
bash tools/ubuntu-build-test.sh
```

Equivalent manual commands:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected tests:
- `frame_tests`
- `stats_tests`
- `parser_tests`
- `storage_tests`
- `local_smoke_tests`
- `local_process_smoke`

## 3. Localhost smoke test

Short smoke:

```bash
bash tools/run-local-smoke.sh 9000 1000 90
```

Ten-minute soak equivalent:

```bash
bash tools/run-local-smoke.sh 9000 120000 900
```

The runner validates:
- sender and receiver exit codes
- sender and receiver metrics files
- `parse_fail_count == 0`
- `crc_error_count == 0`
- frame count and frame ID range
- raw segment total bytes
- meta index row count and continuity
- per-frame payload size

## 4. Binding to a real interface

The Python runner defaults to:

```text
receiver bind_host = 0.0.0.0
sender host       = 127.0.0.1
```

For a real NIC, first identify the receiver-side address:

```bash
ip addr
ip link
ethtool <iface>
```

Then run directly with explicit bind/connect addresses, for example:

```bash
python3 tools/run_local_smoke.py \
  --bind-host 192.168.10.2 \
  --host 192.168.10.2 \
  --port 9000 \
  --frames 120000 \
  --timeout 900
```

For a two-host test, run the receiver on the acquisition host and sender on the simulator host with the receiver's NIC IP. The current helper script starts both processes on one host; split-host orchestration should be added before final hardware validation.

## 5. Network baseline capture

Before native Linux link tests, archive:

```bash
uname -a > captures/meta/uname.txt
lscpu > captures/meta/lscpu.txt
ip link > captures/meta/ip-link.txt
ip addr > captures/meta/ip-addr.txt
ethtool <iface> > captures/meta/ethtool-<iface>.txt
ethtool -i <iface> > captures/meta/ethtool-i-<iface>.txt
journalctl -b > captures/meta/journalctl-b.txt
```

For throughput sanity:

```bash
iperf3 -s
iperf3 -c <receiver-ip> -t 60
```

## 6. Output locations

Runtime outputs are recreated by each smoke run:

```text
logs/
captures/raw/segment-*.bin
captures/meta/segment-*.csv
```

At 200 Hz:
- 10 minutes = 120000 frames = 817,920,000 raw bytes
- 1 hour = 720000 frames = 4,907,520,000 raw bytes

Make sure the target filesystem has enough free space before long runs.

## 7. Current limitations

- CPU/RSS metrics are placeholders and currently written as `0`.
- The helper runner starts sender and receiver on one host only.
- Windows p999/max jitter is not a final timing verdict; native Ubuntu timing should be used for deployment decisions.
- PREEMPT_RT should only be introduced after native Ubuntu baseline measurements show unacceptable jitter or deadline misses.
