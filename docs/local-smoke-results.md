# Local Smoke Results

## 2026-07-22 MSYS2 UCRT64 localhost

Environment:
- Windows 11 + MSYS2 UCRT64 GCC/CMake/Ninja
- Sender and receiver run as separate localhost processes
- Protocol: `[header_len][header][payload][payload_crc]`
- Payload: 1704 channels x 32-bit samples = 6816 bytes/frame

### 1000 frames

Command:

```bash
PATH="/c/msys64/ucrt64/bin:$PATH" python tools/run_local_smoke.py --port 9000 --frames 1000 --timeout 90
```

Result:
- sender_exit: 0
- receiver_exit: 0
- captured_frames: 1000
- sender frame_id_begin/end: 0/999
- receiver frame_id_begin/end: 0/999
- parse_fail_count: 0
- crc_error_count: 0
- sender send_period_avg_us: 5019
- receiver recv_gap_avg_us: 5019
- raw bytes: 6,816,000
- index rows: 1000

### 10000 frames

Command:

```bash
PATH="/c/msys64/ucrt64/bin:$PATH" python tools/run_local_smoke.py --port 9000 --frames 10000 --timeout 120
```

Result:
- sender_exit: 0
- receiver_exit: 0
- captured_frames: 10000
- sender frame_id_begin/end: 0/9999
- receiver frame_id_begin/end: 0/9999
- parse_fail_count: 0
- crc_error_count: 0
- sender send_period_avg_us: 4999
- receiver recv_gap_avg_us: 5000
- sender send_period_p999_us: 36343
- receiver recv_gap_p999_us: 36298
- raw segments: 1
- raw bytes: 68,160,000
- meta index rows: 10000
- index contiguous: true

### 10-minute soak: 120000 frames

Command:

```bash
PATH="/c/msys64/ucrt64/bin:$PATH" python tools/run_local_smoke.py --port 9000 --frames 120000 --timeout 900
```

Result:
- sender_exit: 0
- receiver_exit: 0
- captured_frames: 120000
- sender frame_id_begin/end: 0/119999
- receiver frame_id_begin/end: 0/119999
- parse_fail_count: 0
- crc_error_count: 0
- tcp_disconnect_count: 0
- sender send_period_avg_us: 4999
- receiver recv_gap_avg_us: 4999
- sender send_period_max_us: 46490
- receiver recv_gap_max_us: 46497
- sender send_period_p999_us: 36910
- receiver recv_gap_p999_us: 36900
- raw segments: 12
- raw bytes: 817,920,000
- meta index rows: 120000
- index contiguous: true
- payload bytes per frame: 6816
- segment offsets valid: true

Assessment:
- The happy-path two-process localhost path is stable through 120000 frames / 10 minutes.
- Average 5 ms cadence remains on target.
- Windows scheduling jitter remains visible in max/p999 latency and should be expected for a non-real-time localhost smoke test.
- No parse, CRC, disconnect, frame-count, raw-size, payload-size, segment-offset, or index-continuity failures were observed.
