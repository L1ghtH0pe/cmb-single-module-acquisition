# 测试计划

## 目标

本文件定义单模块原型在原生 Linux 下的测试路线、日志与统计要求、24 h / 72 h 验收标准，以及 PREEMPT_RT A/B 对比条件。

## 测试总原则

当前采用 **Windows 开发、Ubuntu 验证** 的模式。

因此测试分成两层：

- Windows 本地测试：验证代码能编译、sender / receiver 的基本逻辑正确、frame 结构和解析流程没有明显错误
- Ubuntu 24.04 原生测试：验证光纤链路、TCP framing、5 ms 周期稳定性、日志统计、24h/72h 长稳，以及 PREEMPT_RT A/B

最终是否“满足需求”，只能以 Ubuntu 原生测试为准。

第一阶段不是为了证明最终架构全部成立，而是为了回答一个更具体的问题：

**MS-01 + Ubuntu 24.04 + 原生 Linux + TCP framing，能否稳定支撑单模块 200 Hz / 5 ms 周期链路。**

## 测试分层

### 1. 功能正确性测试

验证：

- 发送端每 5 ms 生成一帧
- 接收端能正确恢复 frame
- `frame_id` 连续递增
- CRC 校验可通过
- 最小落盘生效

### 2. 链路测试

验证：

- SFP+ 模块与光纤链路可协商
- 接口持续 up
- 基础吞吐稳定
- 无异常网卡错误计数增长

### 3. 时序与抖动测试

验证：

- 发送周期平均值接近 5 ms
- 发送周期最大值被记录
- 发送周期 99.9 分位被记录
- 接收端 frame 到达间隔平均值、最大值、99.9 分位被记录

### 4. 长稳测试

验证：

- 24 小时连续运行是否稳定
- 72 小时连续运行是否稳定
- TCP 连接是否出现异常断开
- CRC、解析错误、资源占用是否稳定

## 日志与统计要求

第一阶段至少保留：

```text
logs/runtime.log
logs/metrics.csv
captures/raw/
captures/meta/
```

### runtime.log

必须记录：

- 启动时间
- 退出时间
- 连接建立/断开
- 文件切换
- 配置摘要
- 错误事件

### metrics.csv

建议至少包含列：

- timestamp
- frame_id_begin
- frame_id_end
- frame_count
- parse_fail_count
- crc_error_count
- tcp_disconnect_count
- reconnect_ms
- send_period_avg_us
- send_period_max_us
- send_period_p999_us
- recv_gap_avg_us
- recv_gap_max_us
- recv_gap_p999_us
- cpu_percent
- rss_mb

## Phase 0 里程碑测试

### Milestone 1：装机与基线采集

执行：

- 保存 `uname -a`
- 保存 `lscpu`
- 保存 `lspci -nn`
- 保存 `ip link`
- 保存 `ethtool <iface>`
- 保存 `journalctl -b`

输出到：

```text
captures/meta/
```

### Milestone 2：链路验证

执行：

- 检查 `ip link` 是否 up
- 用 `ethtool` 检查速率与协商状态
- 用 `iperf3` 验证基础吞吐

验收：

- 链路持续 up
- 协商速率符合预期
- 无明显错误包计数增长

### Milestone 3：TCP frame sender/receiver v0.1

执行：

- 启动 sender
- 启动 receiver
- 连续运行短时间 smoke test

验收：

- 每 5 ms 一帧
- 接收端成功解析
- `frame_id` 连续
- CRC 可通过
- 落盘文件生成

### Milestone 4：24 小时原生 Linux 长稳测试

执行：

- 连续运行 24 h
- 不做中途人工干预
- 保留全部日志与统计

验收：

- 无未解释 `frame_id` 缺口
- 无未解释 TCP 断连
- 无未解释解析失败
- 无未解释 CRC 错误
- 周期抖动数据完整
- CPU / 内存 / 网卡统计完整

### Milestone 5：72 小时原生 Linux 长稳测试

只有在 24 h 通过后执行。

验收：

- 满足 24 h 全部条件
- 长时间运行下资源曲线稳定
- 统计输出完整可回看

## 原生 Linux 验收标准

以下标准用于判断原生 Linux 是否继续作为主线：

1. 单模块连续运行 24 h，`frame_id` 无未解释缺口
2. 所有异常都能被日志清楚标识
3. 发送周期平均值接近 5 ms
4. 发送周期最大值与 99.9 分位被完整记录
5. 接收端 frame 间隔抖动被完整记录
6. 无未解释 TCP 连接中断
7. 无未解释解析失败
8. 无未解释 CRC 错误
9. 最小日志和统计输出完整可回看
10. 若 72 h 测试仍满足指标，则原生 Linux 继续作为主线

## PREEMPT_RT A/B 触发条件

只有在以下情况出现时，才进入 PREEMPT_RT A/B：

- 原生 Linux 下出现不可接受的 5 ms deadline miss
- 99.9 分位调度抖动明显超出阈值
- 接收侧 frame 延迟长尾异常，且定位到调度层问题
- CPU / IRQ 干扰导致周期任务稳定性不足

## PREEMPT_RT A/B 测试设计

A 组：Ubuntu 24.04 原生内核

B 组：Ubuntu 24.04 对应 PREEMPT_RT 内核

必须保证：

- 相同硬件
- 相同程序版本
- 相同绑核策略
- 相同网络配置
- 相同日志与统计项
- 相同 24 h / 72 h 测试流程

### 对比指标

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
- 长稳测试失败次数

## 当前未决项

以下事项不阻塞第一阶段启动，但需要在后续尽快确认：

- 端到端延迟的正式阈值
- 真实探测器前端接口类型
- 是否需要国产化约束
- 是否需要工业温度、无风扇、长期供货、固定 BOM
- 多模块阶段是否引入 PTP、硬件时间戳、UDP 数据面
