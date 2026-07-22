# 环境搭建说明

## 目标

本文件定义单模块原型的 Ubuntu 版本、开发工具、网络/光纤工具、实时性测试工具，以及 BIOS 和 OS 调优基线。

## 开发与测试工作流

当前工作流不是“立即在 MS-01 上开发”，而是：

1. 在 Windows 环境先完成软件编码
2. 在 Windows 上做基础编译和逻辑验证
3. 再把程序搬到 MS-01 + Ubuntu 24.04 LTS 上做链路、周期、日志、长稳测试

推荐分工：

- Windows：用于写代码、改协议结构、补日志、做基础功能开发
- Ubuntu 24.04：用于光纤链路验证、SFP+ 协商、周期抖动测试、24h/72h soak test、PREEMPT_RT A/B 对比

注意：Windows 不是最终验收环境。任何涉及 5 ms 周期稳定性、网卡中断、系统调度和 PREEMPT_RT 的结论，都必须以 Ubuntu 原生测试结果为准。

## 操作系统版本

推荐版本：**Ubuntu 24.04 LTS**

推荐理由：

- 对较新的 x86 平台兼容更好
- 新内核对网卡、ACPI、调度器支持更稳
- 开发工具、网络工具、性能工具、追踪工具齐全
- 后续如切换 PREEMPT_RT，更容易找到匹配内核路径

不推荐作为第一阶段主线：

- Windows + WSL
- 滚动发行版
- 混合桌面环境做主测试环境

## 基础开发环境

```bash
sudo apt update
sudo apt install -y \
  build-essential gcc g++ clang make cmake ninja-build pkg-config git \
  gdb strace ltrace \
  python3 python3-venv python3-pip
```

说明：

- C++ 是发送端/接收端主线语言
- Python 用于测试脚本、日志分析、辅助工具

## 网络与光纤调试工具

```bash
sudo apt install -y \
  iproute2 ethtool net-tools tcpdump tshark iperf3 socat nmap \
  bridge-utils lldpd pciutils usbutils
```

推荐用途：

- `ip link`：看接口状态
- `ethtool`：看速率、协商、驱动、错误计数
- `tcpdump` / `tshark`：抓包
- `iperf3`：链路吞吐验证
- `lldpd`：交换机/链路邻居识别

## 性能与实时性测试工具

```bash
sudo apt install -y \
  linux-tools-common linux-tools-generic \
  sysstat htop iotop dstat trace-cmd rt-tests stress-ng bpftrace
```

补充说明：

- `cyclictest` 来自 `rt-tests`，用于测调度抖动
- `trace-cmd`、`perf`、`bpftrace` 用于定位延迟长尾
- `stress-ng` 用于制造 CPU / IO 干扰场景

## 时间与同步工具

第一阶段仅需：

```bash
sudo apt install -y chrony
```

第二阶段若进入多模块时间同步，再安装：

```bash
sudo apt install -y linuxptp
```

## 服务与日志环境

第一阶段默认：

- systemd
- journald

即可满足要求。

如后续要服务化运行 sender / receiver，可补 systemd unit 文件。

## BIOS 调优基线

建议在 BIOS 中：

- 优先 Performance 模式
- 关闭不必要的深度省电状态
- 记录 Turbo、SMT、C-State、ASPM 设置
- 固定配置，避免边测边改

## Linux 调优基线

建议统一做以下调优：

- CPU governor 固定为 `performance`
- 关键线程绑核
- 网卡 IRQ 绑核
- 增大 socket buffer
- 尽量减少后台服务
- 关键内存预分配

示例：

```bash
sudo apt install -y linux-cpupower
sudo cpupower frequency-set -g performance
```

后续建议脚本化以下配置：

- `taskset`
- `systemd CPUAffinity`
- `sysctl`
- `ethtool` 参数

## 基线信息采集

装机完成后，至少保存以下输出：

```bash
uname -a
lscpu
lspci -nn
ip link
ethtool <iface>
ethtool -i <iface>
journalctl -b
```

建议把这些输出归档到：

```text
captures/meta/
```

## 链路验证步骤

1. 接通 SFP+ 光模块与光纤
2. 用 `ip link` 确认接口 up
3. 用 `ethtool <iface>` 确认协商速率
4. 用 `iperf3` 做基础吞吐验证
5. 记录链路信息到 `captures/meta/`

## PREEMPT_RT 使用策略

第一阶段不先启用 PREEMPT_RT。

只有在原生 Linux 的 24 h / 72 h 实测中发现：

- 5 ms 周期有不可接受的 deadline miss
- 99.9 分位抖动明显超标
- 接收端 frame 延迟长尾异常且定位到调度层
- CPU / IRQ 干扰使周期任务不稳定

才进入 PREEMPT_RT A/B 对比。
