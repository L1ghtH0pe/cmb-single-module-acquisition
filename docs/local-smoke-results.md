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

Assessment:
- The happy-path two-process localhost path is stable through 10000 frames.
- Average 5 ms cadence is on target.
- Windows scheduling jitter is visible in max/p999 latency and should be expected for a non-real-time localhost smoke test.
- No parse, CRC, disconnect, frame-count, raw-size, or index-continuity failures were observed.
