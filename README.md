# CMB 单模块高速并行数据获取原型

这是一个单模块高速并行数据获取原型，用来验证一条最小链路：

```text
下位机 x86(sender) → TCP/以太网/光纤链路 → 上位机服务器(receiver)
```

当前目标不是最终量产系统，而是先证明单模块在 **200 Hz / 5 ms 周期**下可以稳定完成：数据生成、组帧、网络传输、接收解析、CRC 校验、连续性检查、日志统计和最小落盘。

## 当前状态

已完成：

- `sender`：下位机程序，按 5 ms 周期生成并发送模拟探测器数据
- `receiver`：上位机程序，监听 TCP 端口、接收 frame、校验 CRC、检查 `frame_id`、写日志和原始数据
- TCP framing：`[header_len][header][payload][payload_crc]`
- payload：`1704 × 32-bit = 6816 bytes/frame`
- 本地 smoke test：可在一台机器上同时启动 sender 和 receiver
- Windows/MSYS2 本地 10 分钟 smoke 已通过

未完成验收：

- 真实上位机服务器环境确认
- 下位机 x86 到上位机服务器的双机部署验证
- 真实网卡、光模块、光纤、交换机或光电转换器链路验证
- 24 小时 / 72 小时长稳测试
- 真实探测器前端接入
- CPU / 内存真实采样，目前 metrics 字段可写但不是完整资源监控

上位机操作系统目前**待确认**。不要把它默认写成某个具体发行版。若最终确认为 Linux，优先用原生 Linux 做链路、周期和长稳验收。

## 项目结构

```text
common/                 公共协议、CRC、日志、metrics、延迟统计
sender/                 下位机发送端
receiver/               上位机接收端
tests/                  单元测试和本地集成测试
tools/run_local_smoke.py 本机 sender/receiver smoke test 主脚本
tools/run-local-smoke.sh Linux shell 包装脚本
tools/run-local-smoke.ps1 PowerShell 包装脚本
tools/run_local_smoke.cmd Windows cmd 包装脚本
```

运行时会生成：

```text
logs/
captures/raw/segment-*.bin
captures/meta/segment-*.csv
```

这些是本地运行产物，已在 `.gitignore` 中忽略。需要清理时可以删除：

```bash
rm -rf build logs captures
```

## 构建

### Windows / MSYS2 UCRT64

```bash
PATH="/c/msys64/ucrt64/bin:$PATH" cmake -S . -B build -G Ninja
PATH="/c/msys64/ucrt64/bin:$PATH" cmake --build build
PATH="/c/msys64/ucrt64/bin:$PATH" ctest --test-dir build --output-on-failure
```

### Linux

先安装基础工具。不同发行版包管理命令不同，但至少需要：

- C++ 编译器
- CMake
- Python 3
- Git
- 网络诊断工具：`iproute2` / `iproute`、`ethtool`、`iperf3`
- 可选：Ninja

构建：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

如果已安装 Ninja，也可以用：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## 本机 smoke test

Windows / MSYS2：

```bash
tools/run_local_smoke.cmd 9000 1000 90
```

PowerShell：

```powershell
powershell -ExecutionPolicy Bypass -File tools/run-local-smoke.ps1 -Port 9000 -Frames 1000 -Timeout 90
```

Linux：

```bash
bash tools/run-local-smoke.sh 9000 1000 90
```

10 分钟本机测试：

```bash
bash tools/run-local-smoke.sh 9000 120000 900
```

本机 smoke test 会自动：

1. 清理旧 `logs/`、`captures/raw/`、`captures/meta/`
2. 启动 `receiver`
3. 启动 `sender`
4. 校验发送帧数、接收帧数、CRC、解析错误、capture 文件大小和索引连续性

## 双机部署

### 1. 确认两台机器信息

上位机服务器：

```bash
uname -m
cat /etc/os-release
ip addr
ip link
```

下位机 x86：

```bash
uname -m
cat /etc/os-release
ip addr
ip link
```

如果上位机是 `x86_64` Linux，直接在服务器上构建 `receiver`。如果上位机不是 x86，也不要复制下位机编译出的二进制，应在上位机本地构建，或之后再做交叉编译方案。

### 2. 先测网络

下位机 ping 上位机：

```bash
ping <上位机IP>
```

上位机启动 iperf3 服务端：

```bash
iperf3 -s
```

下位机测试到上位机的吞吐：

```bash
iperf3 -c <上位机IP> -t 60
```

如果有光口或光电转换器，测试前后记录网卡状态：

```bash
ethtool <网卡名>
ethtool -S <网卡名>
```

重点看错误计数是否增长，例如 `rx_errors`、`tx_errors`、`rx_crc_errors`、`rx_dropped`、`tx_dropped`。

### 3. 上位机运行 receiver

```bash
./build/receiver 9000 1000 0.0.0.0
```

参数含义：

```text
9000       TCP 监听端口
1000       期望接收帧数
0.0.0.0    监听所有网卡
```

如果可执行文件在子目录，用实际路径，例如：

```bash
./build/receiver/receiver 9000 1000 0.0.0.0
```

### 4. 下位机运行 sender

```bash
./build/sender <上位机IP> 9000 1000
```

参数含义：

```text
<上位机IP>  receiver 所在服务器地址
9000        TCP 端口，必须和 receiver 一致
1000        发送帧数
```

如果可执行文件在子目录，用实际路径，例如：

```bash
./build/sender/sender <上位机IP> 9000 1000
```

## 端口约定

当前建议先固定使用 TCP `9000`。端口不是写死的，是启动参数。

上位机监听：

```bash
./build/receiver 9000 1000 0.0.0.0
```

下位机连接同一个端口：

```bash
./build/sender <上位机IP> 9000 1000
```

检查端口是否被占用：

```bash
ss -lntp | grep 9000
```

如果上位机防火墙拦截了 9000，需要放行 TCP 9000。

## 协议 v0.1

每个 5 ms 周期形成一个 frame。TCP 只是承载帧流，不能裸发 payload。

线格式：

```text
[header_len][header][payload][payload_crc]
```

header 至少表达：

- `magic`：固定标识，用于识别帧起始
- `version`：协议版本
- `header_len`：header 长度
- `frame_id`：单调递增帧号
- `timestamp_ns`：发送端时间戳
- `module_id`：模块编号
- `channel_count`：当前为 1704
- `sample_bytes`：当前为 4
- `payload_len`：当前为 6816
- `header_crc`：header 校验

payload 当前模拟格式：

```text
1704 channels × uint32 little-endian samples
```

接收端必须检查：

- `magic`
- `version`
- `header_len`
- `payload_len`
- header CRC
- payload CRC
- `frame_id` 连续性

## 测试路线

不要一上来跑 24 小时。按这个顺序推进。

### 1. 本机 1000 帧

```bash
bash tools/run-local-smoke.sh 9000 1000 90
```

1000 帧约 5 秒。

### 2. 本机 10 分钟

```bash
bash tools/run-local-smoke.sh 9000 120000 900
```

120000 帧约 10 分钟。

### 3. 双机 1000 帧

上位机：

```bash
./build/receiver 9000 1000 0.0.0.0
```

下位机：

```bash
./build/sender <上位机IP> 9000 1000
```

### 4. 双机 10 分钟

```text
10 分钟 × 60 秒 × 200 帧/秒 = 120000 帧
```

上位机：

```bash
./build/receiver 9000 120000 0.0.0.0
```

下位机：

```bash
./build/sender <上位机IP> 9000 120000
```

### 5. 双机 1 小时

```text
1 小时 × 3600 秒 × 200 帧/秒 = 720000 帧
```

上位机：

```bash
./build/receiver 9000 720000 0.0.0.0
```

下位机：

```bash
./build/sender <上位机IP> 9000 720000
```

### 6. 双机 24 小时

```text
24 小时 × 86400 秒 × 200 帧/秒 = 17280000 帧
```

上位机：

```bash
./build/receiver 9000 17280000 0.0.0.0
```

下位机：

```bash
./build/sender <上位机IP> 9000 17280000
```

### 7. 双机 72 小时

只在 24 小时通过后再跑。

```text
72 小时 × 259200 秒 × 200 帧/秒 = 51840000 帧
```

上位机：

```bash
./build/receiver 9000 51840000 0.0.0.0
```

下位机：

```bash
./build/sender <上位机IP> 9000 51840000
```

## 验收指标

重点看 `logs/sender-metrics.csv` 和 `logs/receiver-metrics.csv`。

基础合格条件：

- `frame_count` 等于发送帧数
- `frame_id_begin` / `frame_id_end` 连续
- `parse_fail_count = 0`
- `crc_error_count = 0`
- `tcp_disconnect_count = 0`
- `send_period_avg_us` 接近 5000
- `recv_gap_avg_us` 接近 5000
- `send_period_p999_us`、`recv_gap_p999_us`、max 长尾有记录并可解释

长稳测试还要记录：

- `uname -a`
- `lscpu`
- `lspci -nn`
- `ip link`
- `ethtool <iface>`
- `ethtool -S <iface>`
- `journalctl -b`

## 磁盘估算

当前 payload 为 6816 bytes/frame，200 Hz 下约 1.36 MB/s。

| 时长 | 帧数 | 原始 payload 规模 |
|---|---:|---:|
| 10 分钟 | 120000 | 约 818 MB |
| 1 小时 | 720000 | 约 4.9 GB |
| 24 小时 | 17280000 | 约 118 GB |
| 72 小时 | 51840000 | 约 353 GB |

实际还会有 header、索引和日志。建议：

- 24 小时测试至少预留 200 GB
- 72 小时测试至少预留 600 GB

## 已有本机测试记录

2026-07-22，Windows 11 + MSYS2 UCRT64，本机 sender/receiver 双进程 smoke：

| 测试 | 结果 |
|---|---|
| 1000 帧 | 通过，收满 1000 帧，CRC 0，parse fail 0 |
| 10000 帧 | 通过 |
| 120000 帧 / 10 分钟 | 通过，收满 120000 帧，CRC 0，parse fail 0，tcp disconnect 0 |

10 分钟测试记录：

- `sender send_period_avg_us`: 4999
- `receiver recv_gap_avg_us`: 4999
- `sender send_period_max_us`: 46490
- `receiver recv_gap_max_us`: 46497
- `sender send_period_p999_us`: 36910
- `receiver recv_gap_p999_us`: 36900
- raw bytes: 817,920,000
- meta index rows: 120000
- index contiguous: true

结论：本机 happy path 已经稳定到 10 分钟，但 Windows 调度长尾明显，不能替代真实上位机服务器和真实链路验收。

## 当前未决项

- 上位机服务器 CPU 架构和操作系统
- 真实网卡、光模块、光纤、交换机或光电转换器型号
- 端到端延迟正式阈值
- 真实探测器前端接口类型
- 是否需要国产化、工业温度、无风扇、长期供货、固定 BOM 约束
- 多模块阶段是否引入 PTP、硬件时间戳、UDP 数据面

## 当前判断

这个项目现在是**单模块链路原型**，不是最终完成版。下一步应该先完成：

1. 上位机服务器信息确认
2. 下位机 x86 和上位机网络连通
3. 双机 1000 帧
4. 双机 10 分钟
5. 双机 1 小时
6. 双机 24 小时
7. 24 小时通过后再跑 72 小时
