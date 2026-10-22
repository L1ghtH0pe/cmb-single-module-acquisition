# 收发一致性校验工具

用来确认“发出去的字节”和“接收端落盘的字节”逐字节相同。

```text
tools/frame_check/
├── frame_gen.cpp          生成源 .bin（模拟数据）
├── frame_compare.cpp      比对两份 .bin
├── verify_bin.py          独立校验器（Python + zlib，不依赖工程代码）
├── fault_injection_test.py 故障注入测试，验证 frame_compare 能发现错误
├── test_frame_check.py    CTest 端到端测试
└── README.md
```

相关改动在工程其他位置：

- `sender --input <file.bin>`：按原样重放源文件中的帧（`sender/main.cpp`）。
- `sender --save-sent <dir>`：把实际交给 socket 的字节另存一份（`sender/main.cpp`）。
- `common/capture_writer.*` / `common/capture_reader.*`：`.bin` 分段写入与顺序读取，sender、receiver 和本工具共用。

构建后可执行文件在 `build/frame_gen` 和 `build/frame_compare`：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

## 1. 校验流程

```text
            frame_gen
                │
                ▼
           source.bin ───────────────────────────────┐
                │                                    │
                ▼  sender --input                    │
             sender ──TCP / 光纤 / 交换机──> receiver  │
                │                              │     │
                ▼  --save-sent                 ▼     │
        captures/sent/*.bin          captures/raw/*.bin
                │                              │     │
                └──── frame_compare ───────────┘     │
                                               └─ frame_compare ─┘
```

两种比对各自回答一个问题：

| 比对 | 证明什么 |
|---|---|
| `source.bin` vs `captures/raw` | 端到端：事先准备的数据，原样到达并落盘 |
| `captures/sent` vs `captures/raw` | 链路：sender 实际发出的字节，原样到达并落盘 |

使用 `--input` 时两者应同时通过。不用 `--input`（sender 实时生成模拟数据、时间戳取当前时钟）时，没有 `source.bin`，只做第二种。

## 2. `.bin` 格式

三处 `.bin`（source、sent、raw）格式完全相同：完整线上帧首尾相连，无文件头、无索引。

```text
[frame 0: 13684 B][frame 1: 13684 B]...[frame N: 13684 B]
```

每帧 = `header_len(4) + header(44) + payload(1704 × 8) + payload_crc(4)`，字段定义见工程根目录 README“数据格式”。第 `k` 帧位于偏移 `k × 13684`，`frame_id` 在帧内偏移 16。

## 3. frame_gen

```bash
./build/frame_gen <output.bin> <frame_count> [--module-id <id>] [--pattern counter|random] [--seed <n>] [--rate-hz <n>]
```

| 参数 | 说明 |
|---|---|
| `frame_count` | 帧数，200 Hz 下 12000 帧 = 1 分钟 |
| `--module-id` | 写入帧头的模块号，默认 0 |
| `--pattern counter` | 默认。`re = frame_id + channel`，`im = ~re`，与 sender 实时模拟一致，便于肉眼核对 |
| `--pattern random` | 用固定种子的伪随机数填满 re/im，每个 bit 位都会被覆盖，更容易暴露位翻转 |
| `--seed` | random 模式种子，同一种子生成的文件逐字节相同 |
| `--rate-hz <n>` | 写入帧头的帧速率，默认 200；须与 sender/receiver 的 `--rate-hz` 一致 |

`frame_id` 从 0 递增，`timestamp_ns = frame_id / rate_hz` 秒，所以同样的参数总是生成同样的文件。

## 4. sender 相关参数

```bash
./build/sender <host> <port> <frame_count> --input source.bin --save-sent captures/sent
```

- `--input`：按 200 Hz 依次发送文件中的帧，不修改任何字节。发送前会先解码并校验 CRC，坏帧直接报错退出（退出码 10），不会发到线上。文件帧数少于 `frame_count` 也会退出。同时指定 `--module-id` 时，会检查文件中的 module_id 是否一致。
- `--save-sent`：每帧 `send()` 成功后，把同一个缓冲写入 `<dir>/segment-*.bin`。

## 5. frame_compare

```bash
./build/frame_compare <expected.bin|dir> <actual.bin|dir> [--max-report <n>]
```

- 每个参数可以是单个 `.bin`，也可以是目录（读取其中所有 `*.bin`，按文件名排序）。
- 两侧都是逐帧流式读取，内存只占两帧大小，与文件长度无关（实测约 4 MB）。

### 实现

1. **读取**：每次读 13684 字节作为一帧；文件末尾不足一帧的部分计入 `trailing_bytes`。
2. **单帧检查**：解码并校验 magic、版本、长度、采样格式、header CRC、payload CRC，失败记为 `INVALID`。帧头无法解码时，直接从偏移 16 读取 `frame_id`，坏帧照样参与配对。
3. **坏帧配对**：CRC 失败的帧，其 `frame_id` 也可能已损坏。若两侧当前帧 `frame_id` 不同且其中一侧是坏帧，按位置配对比较（输出带 `paired by position`），避免一个坏 id 让后续归并全部错位。
4. **顺序检查**：每侧的 `frame_id` 应严格递增，否则记为 `OUT_OF_ORDER`（包括重复帧）。
5. **按 frame_id 归并**：两侧都按 `frame_id` 递增排列，像归并排序一样同时向前推进：
   - 两侧 `frame_id` 相同：逐字节比较整帧 13684 字节，包括帧头、payload 和 CRC。
   - expected 一侧较小：该帧在 actual 中缺失，记为 `MISSING`，expected 前进一帧。
   - actual 一侧较小：该帧只出现在 actual 中，记为 `UNEXPECTED`，actual 前进一帧。
6. **定位差异**：对不一致的帧，报告首个不同字节的偏移、所在字段、两侧的字节值、该帧共有多少字节不同，以及它在两侧文件中的位置。

### 输出

全部一致：

```text
frame_bytes=13684
expected_frames=12000 invalid=0 out_of_order=0 trailing_bytes=0
actual_frames=12000 invalid=0 out_of_order=0 trailing_bytes=0
identical=12000 mismatched=0 missing=0 unexpected=0
compared_bits=1313664000 bit_errors=0 ber=0.000e+00 frame_loss_rate=0.000e+00
result=IDENTICAL
```

有问题时，汇总前会先逐条列出问题（默认最多 20 条）：

```text
INVALID actual frame_id=7 at segment-000000.bin:7: payload crc mismatch
MISMATCH frame_id=7 first_offset=572 field=payload[channel=65].im expected=0x3a actual=0x3b differing_bytes=1 (expected source.bin:7, actual segment-000000.bin:7)
MISSING frame_id=20 (expected source.bin:20)
```

| 类型 | 含义 |
|---|---|
| `MISMATCH` | 同一 frame_id 两侧字节不同 |
| `MISSING` | expected 有、actual 没有（丢帧） |
| `UNEXPECTED` | actual 有、expected 没有 |
| `INVALID` | 解码或 CRC 失败 |
| `OUT_OF_ORDER` | frame_id 不递增或重复 |

`field` 可能的取值：`header_len`、`header.<字段名>`（如 `header.timestamp_ns`）、`payload[channel=N].re/.im`、`payload_crc`。

退出码：`0` 全部一致，`1` 有差异，`2` 输入不存在，`64` 参数错误。

## 6. 在当前光纤回环上测试

当前下位机的两个光口 `enp175s0f0` 和 `enp175s0f1` 用一根光纤直连。`run-optical-loopback.sh` 会把两个口分别放进两个 network namespace（`cmb-tx` / `cmb-rx`）。namespace 之间无法走内核本地回环，数据只能从 TX 口经光纤到 RX 口，所以这可以模拟双机。与真实双机的区别在于：两端共用一台机器的 CPU、内存和时钟，链路中也没有交换机。

需要 root（脚本会自动调用 sudo）。建议先跑 1000 帧确认环境，再跑 10 分钟：

```bash
# 端到端：生成随机数据 -> 光纤发送 -> 接收落盘 -> 两种比对
sudo ./tools/run-optical-loopback.sh --frames 1000 --gen-pattern random

# 10 分钟
sudo ./tools/run-optical-loopback.sh --preset 10m --gen-pattern random

# 使用自己准备的源文件
./build/frame_gen /tmp/source.bin 12000 --pattern counter
sudo ./tools/run-optical-loopback.sh --frames 12000 --input /tmp/source.bin
```

不加 `--gen-pattern`/`--input` 时，sender 实时生成模拟数据，脚本只比对 `sent` 与 `raw`。

结果在本次输出目录（脚本启动时打印的路径）：

```text
captures/source.bin            生成的源文件（--gen-pattern 时）
captures/sent/segment-*.bin    sender 实际发出
captures/raw/segment-*.bin     receiver 落盘
frame-compare.txt              sent vs raw
frame-compare-source.txt       source vs raw
summary.json                   frame_compare.identical 及 NIC 计数器、deadline 等
```

`summary.json` 中 `status=passed` 要求两种比对都是 IDENTICAL，同时要求 NIC 错误计数为 0。

## 7. 手动测试（不经过光纤）

```bash
cd /tmp && mkdir -p fc && cd fc
B=~/桌面/cmb-single-module-acquisition/build
$B/frame_gen source.bin 2000 --pattern random
$B/receiver 9000 2000 127.0.0.1 &
$B/sender 127.0.0.1 9000 2000 --input source.bin --save-sent captures/sent
wait
$B/frame_compare source.bin captures/raw
$B/frame_compare captures/sent captures/raw
```

真实双机时：上位机运行 receiver，下位机运行 `sender --input`，再把 `source.bin` 拷到上位机，执行 `frame_compare source.bin captures/raw`。

## 8. 工具本身的验证

只看到 IDENTICAL 不能说明工具有效（一个永远输出 IDENTICAL 的工具也会通过）。因此分两方面验证：

### 8.1 独立交叉校验

`verify_bin.py` 用 Python 重新实现帧格式，CRC 用 `zlib.crc32`，与 C++ 代码无共享：

```bash
python3 tools/frame_check/verify_bin.py captures/raw --expect-frames 120000
```

检查每帧固定字段、长度、header/payload CRC 和 `frame_id` 连续性，并输出整份数据的 SHA-256。也可直接用系统工具：

```bash
cat captures/raw/segment-*.bin | cmp - captures/source.bin
```

### 8.2 故障注入

`fault_injection_test.py` 拿一份真实数据，按已知方式破坏副本，检查 `frame_compare` 是否准确报出注入的问题（帧号、偏移、字段名、不同字节数都与 Python 独立计算的结果比对）：

```bash
python3 tools/frame_check/fault_injection_test.py build                                  # frame_gen 数据
python3 tools/frame_check/fault_injection_test.py build --source captures/raw/segment-000005.bin --exhaustive
```

| 类别 | 场景 |
|---|---|
| 正向对照 | 完全相同的副本；相同字节但分段边界不同 |
| 单 bit 翻转 | 每个帧头字节、两个 CRC、采样的 payload 字节；`--exhaustive` 时覆盖全部 13684 个偏移 |
| payload 内容 | 末通道虚部符号位；整段清零（核对不同字节数）；通道互换；re/im 互换 |
| CRC 仍有效的篡改 | 改 payload/时间戳/module_id 后重算 CRC；用下一帧 payload 替换。CRC 查不出，只能靠逐字节比较 |
| 帧级 | 丢第一/中间/最后一帧；连丢 5 帧；重复帧；相邻帧互换；多出一帧 |
| 文件级 | 截断 1 字节；尾部多 1 字节；中间插入 1 字节；空文件；路径不存在；expected/actual 对调 |

`ctest --test-dir build` 包含两项：

- `frame_check_tests`：localhost 上 生成 → 发送 → 接收 → 比对，加 bit 翻转、丢帧截断、坏源文件拒发。
- `frame_compare_fault_injection`：上表全部场景（默认模式）。

## 9. 局限

- receiver 遇到 CRC 错误会立即退出，坏帧不会落盘。所以线上出错时，通常表现为 receiver 失败加上 `MISSING`，而不是 `MISMATCH`。`MISMATCH` 主要用来发现落盘后文件被损坏，或者 CRC 漏检的情况。
- CRC 和字段检查与 sender/receiver 共用同一份协议代码。逐字节比较不依赖这份代码。
- `frame_compare` 要求两侧 `frame_id` 递增，这与 sender/receiver 的写入顺序一致。顺序被打乱的文件会报 `OUT_OF_ORDER`，后续配对结果仅供参考。
