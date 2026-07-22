# CMB 单模块高速并行数据获取原型规格文档

## 1. 目标与范围

本规格文档定义第一阶段单模块原型系统的环境、系统方案、实时性评估策略和软件方案。

当前范围严格限定为：

- 单模块闭环跑通优先
- 硬件主线为 MS-01 x86 平台，使用原生 SFP+ 光口
- 操作系统先使用原生 Linux
- 第一版数据面优先 TCP
- 重点验证 200 Hz / 5 ms 周期下的链路稳定性、帧完整性、日志与统计能力
- 仅在原生 Linux 实测不满足指标时，才引入 PREEMPT_RT 做 A/B 对比

当前明确不纳入第一阶段范围：

- 19 模块同步
- 严格 PTP/IEEE 1588 同步闭环
- 最终量产硬件定型
- 长期归档系统
- 复杂回放平台
- FPGA/MPSoC 前端集成

## 2. 硬件基线

### 2.1 发送端

推荐硬件：MS-01 x86 主机

建议配置：

- CPU：i5-12600H 优先，i9 不是必需
- 内存：32 GB 起步，建议 2 × 16 GB
- 系统盘：1 TB NVMe
- 数据盘：可选第 2 块 NVMe，若第一阶段就要做长稳测试建议加上
- 光口：原生 10G SFP+

### 2.2 上位机

推荐配置：

- x86 Linux 主机
- 32 GB 内存起步
- NVMe SSD
- 10GbE SFP+ 网口优先

### 2.3 光纤链路

当前场景已确定为短距离机房内链路，第一阶段优先采用：

- 多模光纤
- 1000BASE-SX 或 10GBASE-SR 模块
- LC-LC 双芯光纤跳线

原则：光模块、网口、交换机三者速率必须匹配。

## 3. 操作系统与版本

### 3.1 主线版本

推荐：**Ubuntu 24.04 LTS**

原因：

- 对较新的 x86 平台兼容更好
- 新内核对网卡、ACPI、调度器支持更稳
- 开发工具、网络工具、性能工具、追踪工具齐全
- 后续如切换 PREEMPT_RT，更容易找到匹配资料和内核路径

### 3.2 为什么不优先选 22.04

Ubuntu 22.04 也能使用，但在 MS-01 这类较新硬件上，24.04 更省去驱动、内核和电源管理兼容问题。除非已有成熟镜像，否则不建议把 22.04 作为第一优先级。

### 3.3 不推荐方案

第一阶段不推荐：

- Windows + WSL 作为主链路环境
- 滚动发行版
- 混合桌面环境做主测试环境

应优先使用原生 Ubuntu Server 或最小化 Ubuntu Desktop 环境。

## 4. 系统环境清单

## 4.1 基础开发环境

建议安装：

```bash
sudo apt update
sudo apt install -y \
  build-essential gcc g++ clang make cmake ninja-build pkg-config git \
  gdb strace ltrace \
  python3 python3-venv python3-pip
```

### 4.2 网络与光纤调试工具

```bash
sudo apt install -y \
  iproute2 ethtool net-tools tcpdump tshark iperf3 socat nmap \
  bridge-utils lldpd pciutils usbutils
```

### 4.3 性能与实时性测试工具

```bash
sudo apt install -y \
  linux-tools-common linux-tools-generic \
  sysstat htop iotop dstat trace-cmd rt-tests stress-ng bpftrace
```

补充说明：

- `cyclictest` 来自 `rt-tests`，用于验证周期调度抖动
- `trace-cmd`、`perf`、`bpftrace` 用于定位延迟长尾
- `stress-ng` 用于制造 CPU / IO 干扰环境

### 4.4 时间与同步工具

第一阶段先用 chrony 即可：

```bash
sudo apt install -y chrony
```

第二阶段如果需要多模块时间同步，再加：

```bash
sudo apt install -y linuxptp
```

### 4.5 服务与日志环境

默认 systemd + journald 即可，第一阶段不强制引入额外日志系统。

如后续需要固定服务化运行，可为 sender / receiver 各写一个 systemd unit。

## 5. BIOS 与系统调优基线

### 5.1 BIOS 建议

在 BIOS 中建议：

- 优先 Performance 模式
- 关闭不必要的深度省电状态
- 固定关键测试项，避免边测边改
- 记录 Turbo、SMT、C-State、ASPM 设置

### 5.2 Linux 调优建议

启动后建议统一做这些调优：

- CPU governor 固定为 `performance`
- 关键线程绑核
- 网卡 IRQ 绑核
- 增大 socket buffer
- 尽量减少后台服务
- 关键内存尽量预分配

例如：

```bash
sudo apt install -y linux-tools-common
sudo cpupower frequency-set -g performance
```

建议后续将以下配置脚本化：

- `taskset`
- `systemd CPUAffinity`
- `sysctl`
- `ethtool` 参数

## 6. PREEMPT_RT 策略

### 6.1 当前策略

当前明确策略：

- 第一阶段先用原生 Linux
- 不先安装 PREEMPT_RT
- 先测 24 h / 72 h 实际表现
- 若不满足，再做 PREEMPT_RT A/B 对比

### 6.2 PREEMPT_RT 的作用

PREEMPT_RT 的核心作用不是提升吞吐，而是降低最坏调度延迟和长尾抖动。

它更适合解决：

- 5 ms 周期任务偶发超时
- 高负载下调度长尾变大
- 中断和线程抢占导致的实时性不稳定

它不直接解决：

- 光链路问题
- TCP 队头阻塞
- 应用层 framing 设计不合理
- 存储系统本身过慢

### 6.3 启用 PREEMPT_RT 的条件

只有满足以下任一情况，才进入 PREEMPT_RT 测试：

1. 原生 Linux 下出现不可接受的 5 ms deadline miss
2. 99.9 分位调度抖动明显超出阈值
3. 接收侧 frame 延迟长尾异常，且定位为调度层问题
4. CPU / IRQ 干扰导致周期任务稳定性不足

### 6.4 A/B 测试设计

A 组：Ubuntu 24.04 原生内核

B 组：Ubuntu 24.04 对应 PREEMPT_RT 内核

两组必须保证：

- 相同硬件
- 相同程序版本
- 相同绑核策略
- 相同网络配置
- 相同日志与统计项
- 相同 24 h / 72 h 测试流程

对比指标：

- 发送周期平均值
- 发送周期最大值
- 发送周期 99.9 分位
- 接收端 frame 到达间隔平均值
- 接收端 frame 到达间隔最大值
- 接收端 frame 到达间隔 99.9 分位
- TCP 断连次数
- 重连耗时
- CPU 使用率
- 上下文切换次数
- IRQ 干扰迹象

## 7. 软件方案

## 7.1 语言选择

当前主线语言已确定为 **C++**。

推荐：

- 发送端与接收端核心程序：C++17 / C++20
- 测试脚本、日志分析、辅助工具：Python 3.12

理由：

- 便于控制 socket、内存布局、线程调度
- 适合长稳测试
- 更容易做 framing、CRC、绑核和低层优化

## 7.2 第一版网络协议

第一版数据面优先 TCP，但必须做明确帧边界。

不允许“裸 TCP 字节流直接拼数据”。

推荐帧结构：

```text
[header_len][header][payload][crc]
```

其中 header 至少包含：

- magic
- version
- module_id
- frame_id
- timestamp_ns
- channel_count
- sample_rate_hz
- payload_len
- flags
- header_crc

payload 为单个 5 ms 周期对应的数据块。

payload 后加 payload_crc 或整帧 CRC。

### 7.3 发送端模块划分

```text
data_simulator
frame_scheduler
frame_encoder
tcp_sender
health_metrics
```

模块职责：

- `data_simulator`：生成 1704 路模拟数据
- `frame_scheduler`：以 5 ms 周期驱动发送
- `frame_encoder`：组装 header + payload + CRC
- `tcp_sender`：维护连接并发送帧
- `health_metrics`：记录统计值和状态

### 7.4 接收端模块划分

```text
tcp_receiver
frame_parser
loss_detector
storage_writer
metrics_exporter
```

模块职责：

- `tcp_receiver`：读取 TCP 字节流
- `frame_parser`：按 framing 恢复完整 frame
- `loss_detector`：检查 `frame_id` 连续性、CRC、异常延迟
- `storage_writer`：做最小落盘
- `metrics_exporter`：输出日志和统计

## 8. 日志、统计与原型期存储策略

当前已确定：

- 原型期只关心链路稳定
- 暂不做完整长期归档系统
- 落盘文件格式先不锁死
- 但必须有部分日志和统计

### 8.1 最小输出集合

建议至少保留：

```text
logs/runtime.log
logs/metrics.csv
captures/raw/
captures/meta/
```

说明：

- `runtime.log`：程序事件日志
- `metrics.csv`：周期统计、连接统计、错误统计
- `captures/raw/`：原始 frame 滚动文件
- `captures/meta/`：测试配置、版本、时间、网口信息

### 8.2 推荐统计项

至少记录：

- `frame_id` 起止值
- frame 总数
- 解析失败数
- CRC 错误数
- TCP 断连次数
- 重连耗时
- 发送周期平均值 / 最大值 / 99.9 分位
- 接收帧间隔平均值 / 最大值 / 99.9 分位
- CPU 使用率
- 内存使用率
- 网卡错误计数

### 8.3 推荐日志级别

- INFO：启动、停止、连接建立、连接断开、配置摘要、文件切换
- WARN：单次重连、CRC 错误、短时抖动超阈值
- ERROR：持续断连、解析失败超阈值、落盘失败、测试中止

## 9. Phase 0 里程碑

### Milestone 1：装机与基线采集

完成：

- 安装 Ubuntu 24.04 LTS
- 记录 BIOS 配置
- 导出 `uname -a`、`lscpu`、`lspci -nn`
- 记录 `ip link`、`ethtool <iface>`、`journalctl -b`

### Milestone 2：链路验证

完成：

- SFP+ 光模块与光纤接通
- `ip link` 确认 link up
- `ethtool` 确认速率与协商状态
- `iperf3` 验证基础吞吐

### Milestone 3：TCP frame sender/receiver v0.1

完成：

- 每 5 ms 发送 1 个完整 frame
- 接收端正确解析 frame
- `frame_id` 连续
- CRC 可校验
- 最小落盘生效

### Milestone 4：原生 Linux 长稳测试

完成：

- 连续运行 24 小时
- 输出日志和统计
- 分析周期抖动、连接状态、解析错误、CRC 错误

### Milestone 5：PREEMPT_RT A/B 对比

仅在 Milestone 4 不达标时执行。

## 10. 原生 Linux 验收标准

以下标准用于判断原生 Linux 是否继续作为主线：

1. 单模块连续运行 24 小时，`frame_id` 无未解释缺口
2. 所有异常必须被日志清楚标识
3. 发送周期平均值接近 5 ms
4. 最大值与 99.9 分位抖动被完整记录
5. 接收端 frame 间隔抖动被完整记录
6. 无未解释 TCP 连接中断
7. 无未解释解析失败
8. 无未解释 CRC 错误
9. 最小日志和统计输出完整可回看
10. 若 72 小时测试仍满足指标，原生 Linux 继续作为主线

## 11. 当前未决项

以下事项不阻塞第一阶段单模块启动，但要在后续阶段尽快确认：

- 端到端延迟的正式验收阈值
- 真实探测器前端最终接口类型
- 是否需要国产化约束
- 是否需要工业温度、无风扇、长期供货、固定 BOM
- 多模块阶段是否引入 PTP / 硬件时间戳 / UDP 数据面
