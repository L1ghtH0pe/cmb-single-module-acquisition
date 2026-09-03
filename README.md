# CMB 高速并行数据获取原型

用于验证 CMB 探测器数据采集链路的实验性原型。核心链路支持单模块运行、十模块 localhost 软件模型和分主机十路进程启动，用于逐步验证模块身份、帧号连续性、并发传输和分路落盘。

已确定的目标部署拓扑为：

```text
下位机 PCIe SFP 光网卡（目标 10 口） ── 10 路光纤 ── 交换机 ── 普通电脑（receiver）
```

当前下位机已安装一张 Intel 82599ES 双口 SFP+ 网卡，系统接口为 `enp175s0f0` 和 `enp175s0f1`。2026-09-03 检查时两口均已建立 1 Gb/s 全双工链路；这是当前现场快照，不代表最终十路接口速率。交换机型号、上位机网口、IP/VLAN/路由和最终协商速率由网络负责人确定。

> **定位**：验证原型，不是量产系统。sender 目前生成模拟数据；真实探测器数据源、最终十路网络配置、PTP/硬件时间戳、故障恢复和长期运行策略尚未完成。

## 当前实现

### 数据面与协议

- C++20 `sender` 和 `receiver` 可执行程序。
- TCP 长连接承载应用层 frame；TCP 本身只提供字节流。
- 协议版本为 v1，所有线上整数均按 little-endian 编码。
- 每帧包含 1704 个 `uint32_t` 采样值，即 6816 bytes payload。
- sender 默认以 200 Hz（5 ms）周期生成模拟数据，每路 `frame_id` 从 0 递增。
- receiver 检查 magic、版本、长度、header CRC、payload CRC 和 frame 合法性。

## 数据格式

### TCP 线上帧

每个 TCP 应用层帧固定为 **6864 bytes**：

```text
┌────────────────────┬─────────────────┬──────────────────────┬─────────────────────┐
│ header_len (4 B)   │ header (40 B)   │ payload (6816 B)     │ payload_crc (4 B)   │
└────────────────────┴─────────────────┴──────────────────────┴─────────────────────┘
wire offset: 0        4                 44                     6860                6864
```

TCP 不保留消息边界。receiver 必须先读取 4-byte `header_len`，再读取 header，并根据 header 中的 `payload_len` 收齐 payload 和末尾 CRC；不能假定一次 `recv()` 对应一帧。

#### 帧头字段

`header_len` 当前固定为 40。紧随其后的 40-byte header 布局如下；“帧内偏移”从 `header_len` 的第一个字节开始计算：

| 帧内偏移 | header 内偏移 | 长度 | 类型 | 字段 | v1 含义/固定值 |
|---:|---:|---:|---|---|---|
| 0 | — | 4 | `uint32_le` | `header_len` | `40` |
| 4 | 0 | 4 | `uint32_le` | `magic` | `0x434D4231`（CMB1） |
| 8 | 4 | 2 | `uint16_le` | `version` | `1` |
| 10 | 6 | 2 | `uint16_le` | `header_size` | `40` |
| 12 | 8 | 2 | `uint16_le` | `module_id` | 模块编号，范围 `0..65535` |
| 14 | 10 | 2 | `uint16_le` | `flags` | 当前固定为 `0`，预留 |
| 16 | 12 | 8 | `uint64_le` | `frame_id` | 每个 sender 从 `0` 连续递增 |
| 24 | 20 | 8 | `uint64_le` | `timestamp_ns` | sender 的单调时钟时间戳，单位 ns |
| 32 | 28 | 2 | `uint16_le` | `channel_count` | `1704` |
| 34 | 30 | 2 | `uint16_le` | `sample_rate_hz` | `200` |
| 36 | 32 | 4 | `uint32_le` | `payload_len` | `6816` |
| 40 | 36 | 4 | `uint32_le` | `header_crc` | header CRC-32 |
| 44 | — | 6816 | `1704 × uint32_le` | `payload` | 通道 0～1703 的采样值 |
| 6860 | — | 4 | `uint32_le` | `payload_crc` | payload CRC-32 |

`timestamp_ns` 来自 `std::chrono::steady_clock`，只适合计算同一运行环境内的时间间隔；它不是 UTC/Unix 时间，也不能直接用于跨主机时钟对齐。

#### Payload 通道顺序与模拟值

payload 中第 `i` 个 32-bit 值对应通道 `i`，其中 `0 <= i < 1704`，不存在通道间 padding。当前模拟 sender 生成：

```text
payload[i] = uint32(frame_id + i)
```

例如 `frame_id = 42` 时，前 4 个通道值为 `42, 43, 44, 45`。接入真实探测器后，应保持 1704 通道的顺序和 `uint32_le` 线格式，或通过升级协议版本明确变更。

#### CRC-32

header 和 payload 都使用 reflected CRC-32：多项式 `0xEDB88320`、初值 `0xFFFFFFFF`、结果按位取反。

- `header_crc`：先将 header 中的 `header_crc` 字段置 0，再对完整 40-byte header 计算。
- `payload_crc`：对线上 6816-byte payload 计算。
- 当前开发与验收目标为 x86 little-endian 主机；若移植到 big-endian 平台，应补充 payload CRC 和落盘字节序测试。

#### 带宽

单路 200 Hz 时：

- payload：`6816 × 200 = 1,363,200 bytes/s`。
- 完整线上帧：`6864 × 200 = 1,372,800 bytes/s`，不含 TCP/IP/以太网开销。
- 十路完整帧合计约 `13,728,000 bytes/s`，不含协议栈开销。

### Receiver 落盘格式

receiver 校验通过后只落盘 payload，不把 4-byte prefix、40-byte header 或两个 CRC 写入 `.bin`。默认每 10000 帧形成一个分段：

```text
captures/
├── raw/segment-000000.bin
└── meta/segment-000000.csv
```

`segment-XXXXXX.bin` 是连续 payload 字节：

```text
[frame 0: 6816 B][frame 1: 6816 B]...[frame N: 6816 B]
```

对应 CSV 首行为：

```csv
frame_id,timestamp_ns,offset,payload_bytes
```

每行记录该帧在当前 `.bin` 分段内的字节偏移和长度。例如：

```csv
frame_id,timestamp_ns,offset,payload_bytes
0,123456789,0,6816
1,128456789,6816,6816
```

读取第 `n` 行时，从对应 `.bin` 的 `offset` 位置读取 `payload_bytes`，再按 1704 个 `uint32_le` 解码。当前 x86 目标上的落盘字节与线上 payload 字节序一致；`.bin` 本身不含文件头，因此必须与同名 CSV 索引配套使用。

### 诊断与落盘

- sender/receiver 输出 runtime log 和聚合 metrics CSV。
- sender 记录周期、调度迟到、deadline miss、编码和 socket 发送耗时。
- receiver 记录接收间隔、deadline miss、解析耗时、队列水位、队列溢出和写盘耗时。
- receiver 检测 frame_id 缺帧、重复和乱序；指定 `--module-id` 后还会校验模块身份。
- receiver 使用有界 capture queue 和分段文件落盘：
  - `captures/raw/segment-XXXXXX.bin`：连续 payload 二进制。
  - `captures/meta/segment-XXXXXX.csv`：frame_id、timestamp、偏移和长度索引。
- `--timing-log` 可选开启逐帧 timing CSV；默认只写聚合 metrics，避免诊断 I/O 影响周期。

### 测试能力

- CTest：frame、统计、解析、存储和跨平台进程 smoke 测试。
- Python 单路 smoke：检查进程、frame 数、capture 大小、索引和 metrics。
- Linux 光口 loopback runner：可创建 network namespace，记录 NIC counters，并输出 `summary.json`。
- 十路 localhost runner：读取十路 module/端口映射，启动十组独立 sender/receiver，并按模块保存日志、metrics 和 capture。

## 当前状态

截至 **2026-09-04**，代码已具备单模块 TCP 采集、CRC/解析/连续性检查、指标记录、分段落盘、单机回归、Linux 光口脚本、十路 localhost 软件验证和分主机十路进程启动能力。

已记录的历史结果：

- Windows/MSYS2 本地 10 分钟 smoke 已通过。
- 单路真实双光口 200 Hz / 10 分钟验收已记录：120000 帧完整，连续性、CRC、解析、TCP/NIC 错误正常，sender/receiver deadline miss 为 0。

这些是历史测试记录，不代表当前机器或未来硬件环境自动满足相同结果。以下内容仍未正式实现或验收：

- 网络负责人提供最终交换机、上位机网口、IP/VLAN/路由和链路速率后，完成真实十路物理链路联调与验收。
- PTP、硬件时间戳和正式端到端延迟阈值。
- 真实探测器前端数据源、生产级恢复/重连、权限隔离和 24/72 小时最终验收。

## 快速开始

### 构建与测试

需要 CMake 3.20+、C++20 编译器、Python 3；Linux 光口测试另外需要 `iproute2`、`ethtool` 和 root 权限。使用 Ninja 时：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

不使用 Ninja 时可省略 `-G Ninja`。CMake targets：

```text
sender  receiver
frame_tests  stats_tests  parser_tests  storage_tests
local_smoke_tests  local_process_smoke  multi_host_tests
```

### 单路 localhost smoke

Linux：

```bash
./tools/run-local-smoke.sh 9000 1000 90
```

Windows/MSYS2 cmd：

```bat
tools/run_local_smoke.cmd 9000 1000 90
```

PowerShell：

```powershell
powershell -ExecutionPolicy Bypass -File tools/run-local-smoke.ps1 -Port 9000 -Frames 1000 -Timeout 90
```

10 分钟模型测试为 120000 帧：

```bash
./tools/run-local-smoke.sh 9000 120000 900
```

runner 会清理并重建 `logs/`、`captures/raw/`、`captures/meta/`，启动 receiver/sender，并校验输出、frame 数、CRC/解析错误、capture 大小和索引连续性。

### 十路 localhost smoke

```bash
./tools/run-multi-local-smoke.sh --frames 1000
```

等价命令：

```bash
python3 tools/run_multi_local_smoke.py \
  --config configs/ten-channel.example.conf \
  --frames 1000 \
  --output /tmp/cmb-ten-way-local
```

参数：

```text
--config <path>       配置文件，默认 configs/ten-channel.example.conf
--frames <n>          每路发送帧数，默认 1000
--build-dir <path>   sender/receiver 所在 build 目录
--output <path>      输出目录，默认 /tmp/cmb-ten-way-local
```

配置每行格式：

```text
module_id tx_iface rx_iface tx_ip rx_ip port tx_namespace rx_namespace
```

当前脚本只使用 `module_id` 和 `port`；其余字段作为现场部署映射。summary 会列出每路 exit code、发送/接收 frame 数、原始字节数、分段数量和错误项。该测试验证十路独立 TCP 会话的软件并发，不等价于真实十路光纤验收。

输出示例：

```text
/tmp/cmb-ten-way-local/
├── logs/                         # 每路 stdout/stderr
├── captures/
│   └── module-0000/
│       ├── logs/                 # 该路 runtime/metrics/timing
│       └── captures/             # raw/ 与 meta/
└── summary.json
```

### 十路双机启动

先将 `configs/ten-channel.example.conf` 中的 TEST-NET 示例地址和接口名替换为网络负责人提供的现场配置。上位机普通电脑先启动 receiver 角色：

```bash
./tools/run-multi-host.sh --role receiver --frames 1000
```

然后在下位机启动 sender 角色：

```bash
./tools/run-multi-host.sh --role sender --frames 1000
```

启动前可在两台机器分别执行 dry-run，只校验配置并打印命令：

```bash
./tools/run-multi-host.sh --role receiver --frames 1000 --dry-run
./tools/run-multi-host.sh --role sender --frames 1000 --dry-run
```

每个角色默认写入 `/tmp/cmb-multi-host/<role>/`，每个 module 使用独立工作目录、日志、metrics 和 capture。sender 通过 `--bind-host <tx_ip>` 绑定对应源地址；receiver 绑定 `<rx_ip>:<port>`。脚本不会配置物理接口、地址、VLAN、路由或交换机，也不会创建 namespace。如果配置中填写了 namespace，它必须事先存在；不使用时写 `-`。

`tx_iface` 和 `rx_iface` 用于记录通道与现场端口的映射，启动器实际按 IP 绑定。普通电脑只有一个汇聚网口时，10 行可以使用相同 `rx_iface` 和 `rx_ip`，但端口和 module_id 必须唯一。下位机 10 个接口若处于同一二层或重叠网段，仅绑定源 IP 不足以保证正确选路，网络负责人还需配置独立子网、VLAN、策略路由或 namespace。

### 单路双机运行

receiver 所在上位机：

```bash
./build/receiver 9000 1000 0.0.0.0 --module-id 0
```

sender 所在下位机：

```bash
./build/sender <上位机IP> 9000 1000 --module-id 0 --bind-host <本路下位机IP>
```

参数顺序为：

```text
receiver: <port> <expected_frames> <bind_host>
sender:   <host> <port> <frame_count>
```

可选参数：

```text
--module-id <0..65535>       sender 写入模块编号；receiver 开启身份校验
--bind-host <local-ip>       sender 连接前绑定本地源 IPv4 地址
--timing-log <path>          输出逐帧 timing CSV
--capture-queue-frames <n>   receiver capture queue 容量
--deadline-us <n>            sender deadline 阈值
```

两端的 `module_id` 必须一致；端口必须一致。默认输出路径相对于进程当前工作目录：

```text
logs/sender-runtime.log       logs/receiver-runtime.log
logs/sender-metrics.csv       logs/receiver-metrics.csv
captures/raw/segment-*.bin    captures/meta/segment-*.csv
```

## 验收指标

至少检查：

- sender 和 receiver frame 数相等且等于预期。
- `frame_id_begin` / `frame_id_end` 与连续性检查一致。
- `parse_fail_count = 0`、`crc_error_count = 0`、`tcp_disconnect_count = 0`。
- `capture_queue_overrun_count = 0`。
- `send_period_avg_us` 和 `recv_gap_avg_us` 接近 5000。
- p999、max 和 deadline miss 指标有记录并可解释。
- 真实硬件测试前后记录 `uname -a`、`lscpu`、`lspci -nn`、`ip link`、`ethtool <iface>`、`ethtool -S <iface>` 和 `journalctl -b`。

## Linux 光口 loopback

该测试面向有两块独立网卡/光口的 Linux 主机，需要 root 权限和 `ip`、`ethtool` 等工具。确认接口归属后再运行：

```bash
sudo ./tools/run-optical-loopback.sh --preset 1000
```

常用参数：

```text
--frames <n>              指定帧数
--preset <1000|10min>     使用预设帧数
--tx-iface <iface>        sender 网卡
--rx-iface <iface>        receiver 网卡
--keep-namespaces         测试后保留 namespace 供排查
--skip-build              跳过自动构建
--allow-deadline-miss     调查模式，允许 deadline miss
```

默认严格模式要求 sender 和 receiver deadline miss 均为 0。脚本结束时会清理自己创建的 namespace；使用 `--keep-namespaces` 后需手工清理。

## 磁盘估算

仅计算单路 payload（6816 bytes/frame），不含 header、索引和日志：

| 时长 | 帧数 | 单路 payload |
|---|---:|---:|
| 10 分钟 | 120000 | 约 818 MB |
| 1 小时 | 720000 | 约 4.9 GB |
| 24 小时 | 17280000 | 约 118 GB |
| 72 小时 | 51840000 | 约 353 GB |

十路约为单路十倍，正式测试还需预留文件系统和故障重试空间。

## 生成物与清理

以下均为运行生成物，不应提交到 Git：

```text
build/ logs/ captures/ test-results/ .claude/ **/__pycache__/ *.pyc
```

清理命令：

```bash
rm -rf build logs captures test-results .claude tools/__pycache__
```

长时间 capture 可能占用数百 GB；测试前确认输出路径、磁盘剩余空间和保留策略。根目录参考资料不是运行生成物：

- `PREEMPT_RT实时化技术分析.md`：实时化与内核部署参考。
- `测试大纲-模板.doc`：测试记录模板。
- `浪潮英信服务器 NP5570M4 用户手册 V1.0.pdf`：早期服务器硬件参考，不代表当前上位机配置。

## 目录结构

```text
common/       协议、CRC、日志、metrics、统计
sender/       模拟数据生成、编码和 TCP 发送
receiver/     TCP 接收、解析、连续性、队列和落盘
tests/        CTest 单元与进程 smoke
tools/        单路、十路和光口测试 runner
configs/      多模块配置示例
docs/         多模块验证说明
```

## 后续工作

1. 获取网络负责人确认的交换机、上位机网口、IP/VLAN/路由和最终链路速率配置。
2. 在当前双口基础上联调“下位机 SFP → 光纤 → 交换机 → 普通电脑”，再扩展到目标十路。
3. 在双机环境完成每路 1000 帧、10 分钟、1 小时和 24 小时测试。
4. 根据现场拓扑决定 PTP、硬件时间戳、UDP 数据面和正式延迟指标。
5. 接入真实探测器数据源，再进行 24/72 小时长期验收。
