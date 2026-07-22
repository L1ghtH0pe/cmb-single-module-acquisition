# 协议设计 v0.1

## 目标

本文件定义单模块原型阶段的 TCP 数据面 framing、核心字段和发送端/接收端软件结构。

当前策略是：**第一版数据面优先 TCP**，但 TCP 只作为承载帧流的链路，不能直接裸发字节流。

## 设计原则

1. 每个 5 ms 周期形成一个完整 frame
2. 每个 frame 都有显式边界
3. 每个 frame 都有可诊断的元数据
4. 发送端和接收端都必须能检测异常
5. 后续若切换到 UDP，上层语义尽量不变

## TCP framing

推荐格式：

```text
[header_len][header][payload][payload_crc]
```

其中：

- `header_len`：固定长度字段，指示 header 大小
- `header`：帧头元数据
- `payload`：当前 5 ms 周期内的数据块
- `payload_crc`：payload 或整帧校验值

## 推荐 header 字段

至少包含：

- `magic`：固定标识，用于快速识别帧起始
- `version`：协议版本
- `module_id`：模块编号，单模块阶段可固定
- `frame_id`：递增帧号，每个 5 ms 加一
- `timestamp_ns`：发送端生成帧的单调时钟时间戳
- `channel_count`：当前固定 1704
- `sample_rate_hz`：当前固定 200
- `payload_len`：当前 payload 大小
- `flags`：保留状态位
- `header_crc`：header 校验

## payload 语义

每个 5 ms 周期对应一帧 payload。

当前单模块原始规模是：

- 1704 路
- 32 bit / 路
- 200 Hz

因此每帧 payload 约为：

```text
1704 × 4 B = 6816 B
```

## 发送端模块划分

```text
data_simulator
frame_scheduler
frame_encoder
tcp_sender
health_metrics
```

### data_simulator

职责：

- 生成 1704 路模拟数据
- 保证每个周期都能提供完整 payload

### frame_scheduler

职责：

- 以 5 ms 周期驱动发送
- 使用绝对时间基准，避免相对 sleep 漂移累积

### frame_encoder

职责：

- 生成 header
- 拼接 payload
- 计算 CRC
- 输出完整 frame buffer

### tcp_sender

职责：

- 建立和维护 TCP 连接
- 按 frame 边界顺序发送
- 记录断连、重连、发送失败

### health_metrics

职责：

- 记录周期统计、连接状态、错误计数
- 输出到日志和 metrics.csv

## 接收端模块划分

```text
tcp_receiver
frame_parser
loss_detector
storage_writer
metrics_exporter
```

### tcp_receiver

职责：

- 从 TCP socket 读取字节流
- 把字节交给 parser

### frame_parser

职责：

- 按 `header_len` 和 `payload_len` 恢复完整 frame
- 校验 header 与 payload 的结构正确性

### loss_detector

职责：

- 检查 `frame_id` 连续性
- 检查 `timestamp_ns` 单调性
- 检查 CRC
- 统计解析失败和异常延迟

### storage_writer

职责：

- 做第一阶段最小落盘
- 按时间窗口写入原始 frame 数据

### metrics_exporter

职责：

- 输出运行统计
- 生成 `logs/metrics.csv`

## 第一阶段最小诊断能力

接收端必须能记录：

- `frame_id` 连续性
- `timestamp_ns` 单调性
- 帧间到达时间
- CRC 错误数
- 解析失败次数
- TCP 断连次数
- 重连耗时
- socket backlog 异常
- 落盘延迟

## 为什么第一版不用裸 TCP 字节流

如果直接裸发 payload，接收端无法可靠判断：

- 帧从哪里开始
- 帧在哪里结束
- 第几个 5 ms 周期缺失了
- 是网络抖动、解析错误还是上层逻辑问题

所以第一版即便选 TCP，也必须把“帧”作为一等公民。

## 为什么保留 UDP 迁移余地

后续如果出现：

- TCP 队头阻塞
- 重传导致帧延迟长尾不可控
- 多模块汇聚时实时性变差

则数据面可能切换为：

```text
UDP + frame_id + timestamp + payload_len + CRC
```

因此现在的 header 设计要服务未来迁移，而不是把自己锁死在 TCP 专属语义上。
