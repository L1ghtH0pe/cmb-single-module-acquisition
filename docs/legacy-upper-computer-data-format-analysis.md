# 旧上位机数据格式提取、对比与统一方案

## 1. 文档目的与结论

本文依据 `上位机数据格式文件（旧）/` 中的三个历史文件进行逆向梳理：

- `lgfT510.h`
- `lgfT510.cpp`
- `packet_parser.cpp`

这些文件能够说明旧项目如何从离线二进制文件中搜索数据包、拆分复数采样并生成分析文件，但不能单独证明旧格式就是当前 CMB 项目的正式网络协议。旧代码没有网络接收部分、协议说明、真实样本和硬件端生成代码，因此本文中未由代码直接证明的含义均标为“待确认”。

从代码可以确定或高概率推断：

- 旧项目按 `uint64_t` 扫描离线文件；
- 旧包头占 8 个 `uint64_t`，即 64 bytes；
- 主程序期望每包至少包含 1024 个 `uint64_t` 数据 word，即至少 8192 bytes payload；
- 每个数据 word 被拆成低 32 位实部和高 32 位虚部，并按二进制补码解释为两个 `int32_t`；
- payload 被当作 64 个通道、每通道 16 个复数采样点；
- 输入布局为采样点优先，输出分析文件被转置为通道优先；
- 旧程序只使用 `package_id` 做连续性检查，`data_id` 的业务含义未体现；
- 旧格式没有代码层面的长度字段、版本、时间戳、header CRC 或 payload CRC。

当前 CMB v1 则是 1704 通道、每帧每通道一个 `uint32_le` 标量值，使用显式长度、版本、模块号、帧号、时间戳和双 CRC。两者不是只差一个包头，payload 语义也完全不同，不能直接互换。

**推荐结论：保留当前 CMB v1，不把旧格式直接覆盖到现有协议中。先与上位机负责人确认旧字段和当前真实硬件数据含义；如果确实需要兼容旧格式，应将其作为独立 legacy schema，通过离线适配器或新协议版本接入。**

---

## 2. 旧文件的处理流程

三个旧文件形成的程序流程如下：

```text
旧原始 .bin 文件
    ↓ mmap，并直接解释为 uint64_t 数组
固定跳过前 8 MiB
    ↓
扫描 8×uint64 的标志头
    ↓
提取 package_id、data_id 和 payload 指针
    ↓
每个 uint64 拆成低/高两个 int32
    ↓
按 64 通道 × 每包 16 点组织复数数据
    ↓
输出：
  <prefix>_complex.bin
  <prefix>_id.bin
```

代码没有 socket、TCP、UDP、DMA 驱动或网卡收包逻辑，所以目前只能称为“旧离线文件解析格式”。还不能据此确认：

- 线上字节流是否与文件完全相同；
- 文件外是否还有网卡、DMA 或应用层封装；
- 前 8 MiB 的正式含义；
- 硬件是否保证固定包长；
- 是否存在未提供代码负责的 CRC、时间戳或其他元数据。

---

## 3. 旧格式的结构推导

### 3.1 基本单位与预期长度

旧代码定义：

```cpp
HEADER_LEN = 8;
DATA_LEN = 1024;
```

所有长度在解析器中均以 `uint64_t` 为单位，因此：

```text
header  = 8 × 8 bytes    = 64 bytes
payload = 1024 × 8 bytes = 8192 bytes
整包预期长度             = 8256 bytes
```

对应 1032 个 `uint64_t` word。

需要特别注意：解析器只要求找到的 `data_len >= 1024`，没有严格要求它等于 1024。因此 8256 bytes 是主程序期待的正常包尺寸，不是旧解析器严格验证的固定尺寸。

### 3.2 64-byte 包头

根据 `is_valid_header()` 和 `parse_packet()`，包头布局如下：

| 字节偏移 | word | 类型 | 代码中的含义或固定值 |
|---:|---:|---|---|
| 0 | 0 | `uint64` | `STARTWORD2 = 0x7766554433221100` |
| 8 | 1 | `uint64` | `STARTWORD1 = 0xFFEEDDCCBBAA9988` |
| 16 | 2 | `uint64` | `package_id` |
| 24 | 3 | `uint64` | `0xFFFFFFFFFFFFFFFF` 或 `0xFFFFFFFF00000000` |
| 32 | 4 | `uint64` | `data_id` |
| 40 | 5 | `uint64` | `0xFFFFFFFFFFFFFFFF` |
| 48 | 6 | `uint64` | `STOPWORD2 = 0x8899AABBCCDDEEFF` |
| 56 | 7 | `uint64` | `STOPWORD1 = 0x0011223344556677` |
| 64 | — | payload | 数据区开始 |

示意图：

```text
┌──────────────────────────── 64-byte header ────────────────────────────┐
│ start2 │ start1 │ package_id │ sync/alt │ data_id │ sync │ stop2 │ stop1 │
│  8 B   │  8 B   │    8 B     │   8 B    │  8 B    │ 8 B  │ 8 B   │ 8 B  │
└─────────────────────────────────────────────────────────────────────────┘
┌──────────────────────── payload ────────────────────────┐
│ 主程序预期 1024 × uint64 = 8192 bytes                   │
└─────────────────────────────────────────────────────────┘
```

`package_id` 被用于相邻包连续性检查，并写入 `_id.bin`。`data_id` 虽然被解析，但后续代码没有使用，所以不能仅凭字段名将其认定为当前 CMB 的 `module_id`。

### 3.3 字节序

旧程序把映射内存直接转换为 `const uint64_t*`，没有显式大小端解码。这意味着文件解释依赖运行主机本机字节序。结合旧代码面向常见 x86 主机的写法，可推测旧文件实际使用 little-endian，但代码没有形成跨平台协议保证。

在 little-endian 主机上：

```text
数值 0x7766554433221100
文件字节 00 11 22 33 44 55 66 77

数值 0xFFEEDDCCBBAA9988
文件字节 88 99 AA BB CC DD EE FF
```

正式统一格式必须显式规定 little-endian，不能继续依赖 C++ 主机类型强制转换。

### 3.4 Payload 数值类型

每个 64-bit word 的拆分方式为：

```text
bits 31..0   → 低 32 位 → real → int32
bits 63..32  → 高 32 位 → imag → int32
```

即：

```text
uint64 word
┌────────────────────────┬────────────────────────┐
│ high 32 bits           │ low 32 bits            │
│ imag: signed int32     │ real: signed int32     │
└────────────────────────┴────────────────────────┘
```

两个 32-bit 值按二进制补码转换成有符号整数，因此每个 word 表示一个：

```text
complex<int32> = real:int32 + imag:int32·i
```

1024 个 word 对应 1024 个复数点，共 8192 bytes。

### 3.5 通道数、采样点数与排列

旧主程序定义：

```text
channel_count       = 64
samples_per_channel = 1024 / 64 = 16
component_count     = 2（real、imag）
```

输入 payload 的遍历顺序是：

```text
for sample = 0..15:
    for channel = 0..63:
        real:int32
        imag:int32
```

即：

```text
payload[sample][channel][component]
```

第 `(sample, channel)` 个复数点的 word 下标为：

```text
word_index = sample × 64 + channel
```

完整数据量为：

```text
64 channels × 16 samples × 2 components × 4 bytes = 8192 bytes
```

### 3.6 固定跳过前 8 MiB

主程序执行：

```cpp
size_t skip = 16 * ZHEN_LEN;
ZHEN_LEN = (512 * 1024) / 8;
```

换算后：

```text
16 × 65536 uint64 × 8 bytes = 8,388,608 bytes = 8 MiB
```

代码没有解释前 8 MiB 是文件头、预热数据、DMA 环形区、固定数量无效帧，还是某次测试特有的偏移。该行为不能直接进入当前协议，必须先向原负责人确认。

---

## 4. 旧程序的输出文件

### 4.1 `<prefix>_id.bin`

该文件连续写入每个已解析包的 `package_id`：

```text
[package_id 0][package_id 1]...[package_id N-1]
```

每项是本机字节序的 `uint64_t`。文件没有：

- 文件头；
- 格式版本；
- 包数量；
- 字节序标志；
- 校验值。

### 4.2 `<prefix>_complex.bin`

旧程序将输入的采样点优先布局转置为通道优先：

```text
输入： [packet][sample][channel][real/imag]
输出： [channel][packet][sample]，每项为 complex<float>
```

具体顺序为：

```text
channel 0:
    packet 0 的 sample 0..15
    packet 1 的 sample 0..15
    ...
channel 1:
    packet 0 的 sample 0..15
    ...
channel 63
```

每项通过 `std::complex<float>` 直接写文件。常见 ABI 下通常是 `float32 real + float32 imag`，但旧代码没有规定可移植的外部二进制布局。此外，`int32` 转成 `float` 后，绝对值超过 `2^24` 的整数不一定能保持精确。

所以 `_complex.bin` 更适合作为旧分析程序的临时输入，不建议作为当前项目长期归档的正式原始格式。

---

## 5. 旧解析代码存在的格式风险

以下问题不一定说明历史数据无效，但说明三个源文件不足以替代正式协议文档。

### 5.1 依赖下一个包头确定当前包尾

旧包头中没有被使用的 payload 长度。解析器从当前包头后开始搜索下一个包头，以二者距离作为当前 payload 长度。风险包括：

- payload 偶然出现完整标志序列时可能误分包；
- 数据损坏后恢复位置不确定；
- 最后一个包没有后继包头时不能稳定确定边界；
- 无法严格验证固定包长。

### 5.2 只验证 `data_len >= 1024`

解析器允许两个包头之间存在多于 1024 个 word，但后续通道组织只读取前 1024 个复数点，多余数据不会进入最终通道输出，也不会报告长度异常。

统一协议应要求：

```text
payload_len 必须精确等于 payload schema 规定的长度
```

### 5.3 文件末尾边界可能遗漏完整包

解析循环使用严格的小于条件，并依赖寻找后续包头。刚好位于文件末尾的完整包可能不被解析，尤其是在没有尾部附加数据或下一个包头的情况下。

### 5.4 没有 CRC 和版本校验

代码只检查固定标记，没有验证：

- header CRC；
- payload CRC；
- 协议版本；
- 精确总长度；
- `data_id` 合法范围；
- 文件整体完整性。

### 5.5 连续性检查不能区分异常类型

旧代码只判断：

```text
package_id != last_package_id + 1
```

所有异常统一打印为“丢包”，不能区分真正缺包、重复、乱序、计数回绕、采集重启和多数据源交织。

### 5.6 缺少输入安全检查

主程序在调用解析器前没有确认文件长度至少大于 8 MiB；也没有完整检查 `fstat`、输出文件打开和 `fwrite` 的结果。这些是旧工具实现问题，不应复制到正式接收链路。

---

## 6. 与当前 CMB v1 的逐项对比

| 项目 | 旧上位机文件格式 | 当前 CMB v1 |
|---|---|---|
| 可确认的载体 | 离线 `.bin` 文件 | TCP 应用层 frame |
| 分帧方式 | 搜索 8 个 `uint64` 标志 word | 显式 `header_len` 和 `payload_len` |
| 包头 | 64 bytes | 4-byte prefix + 40-byte header |
| payload | 预期至少 8192 bytes | 固定 6816 bytes，严格校验 |
| 完整帧 | 正常情况推测 8256 bytes | 固定 6864 bytes |
| 数据类型 | `complex<int32>` | `uint32` 标量 |
| 通道数 | 64 | 1704 |
| 每通道每包采样数 | 16 | 1 |
| 分量数 | 2：real、imag | 1 |
| payload 布局 | `[sample][channel][real,imag]` | `[channel]` |
| 帧/包编号 | `package_id:uint64` | `frame_id:uint64` |
| 数据源字段 | `data_id:uint64`，含义待确认 | `module_id:uint16` |
| 时间戳 | 未发现 | `timestamp_ns:uint64` |
| 采样率字段 | 未发现 | `sample_rate_hz=200` |
| 版本字段 | 未发现 | `version=1` |
| 字节序 | 依赖本机序，推测 little-endian | 明确 little-endian |
| header CRC | 无 | 有 |
| payload CRC | 无 | 有 |
| 长度验证 | 只要求不少于 1024 word | 必须与 header 和 schema 一致 |
| 连续性诊断 | 仅 `last+1` | 缺帧、重复、乱序分别统计 |
| 当前落盘 | 旧工具输出 `complex<float>` 和 ID | 校验后保存原始 payload，并用 CSV 索引 |

两个 payload 的数据量分别为：

```text
旧格式：64 × 16 × 2 × 4 = 8192 bytes
CMB v1：1704 × 1 × 1 × 4 = 6816 bytes
```

因此两者不能通过简单替换包头或修改一个长度常量完成兼容。

`package_id` 与当前 `frame_id` 的角色较接近，可以作为候选映射；`data_id` 的语义没有代码证据，不能直接映射或截断为 16-bit `module_id`。

---

## 7. 推荐的统一方案

### 7.1 将不同层次分开定义

双方对齐“数据格式”时，应明确讨论的是哪一层：

```text
第 1 层：物理与传输层
          光纤、以太网、TCP/UDP、连接和重连

第 2 层：传输帧
          magic、版本、长度、模块号、帧号、时间戳、CRC

第 3 层：payload 科学数据布局
          通道数、每通道采样数、标量/复数、位宽、符号和排列

第 4 层：落盘与分析格式
          raw segment、索引、HDF5 或其他分析文件
```

旧三个文件主要涉及第 3、4 层，并只间接暴露了旧项目的一部分第 2 层格式。

### 7.2 保留当前 CMB v1 帧封装

当前帧封装具有显式长度和完整性保护，建议继续保留：

- 固定 little-endian；
- magic 和 version；
- header size 和 payload length；
- module ID；
- frame ID；
- timestamp；
- header CRC；
- payload CRC。

不建议回退到旧项目的标记扫描、本机字节序、无 CRC 和“搜索下一包头确定长度”的方式。

### 7.3 将 payload 类型定义为 schema

当前 CMB v1 可明确表示为：

#### Schema A：CMB 标量采样

```text
schema_id           = CMB_SCALAR_U32
sample_format       = uint32_le
channel_count       = 1704
samples_per_channel = 1
component_count     = 1
layout              = channel-major
payload_bytes       = 6816
frame_rate_hz       = 200
```

排列：

```text
[channel 0][channel 1]...[channel 1703]
```

如果负责人确认当前或后续系统确实需要旧类复数数据，可以另外定义：

#### Schema B：旧 T510 复数采样

```text
schema_id           = LEGACY_T510_COMPLEX_I32_64X16
sample_format       = int32_le
channel_count       = 64
samples_per_channel = 16
component_count     = 2
component_order     = real, imag
layout              = sample-major, channel-minor
payload_bytes       = 8192
```

排列：

```text
for sample = 0..15:
    for channel = 0..63:
        int32_le real
        int32_le imag
```

正常传输时应直接保留这一原始布局，避免在实时链路中进行通道转置。

### 7.4 需要多 payload 时新增协议版本

不要改变 CMB v1 现有字段的含义。如果真实需求是同一 receiver 支持不同 payload，建议定义 v2，并加入足以自描述的数据字段，例如：

| 建议字段 | 含义 |
|---|---|
| `payload_schema` | payload 格式编号 |
| `sample_format` | `uint32`、`int32`、`float32` 等 |
| `channel_count` | 通道数 |
| `samples_per_channel` | 每帧每通道采样点数 |
| `component_count` | 标量为 1，复数为 2 |
| `layout` | channel-major 或 sample-major |
| `sample_rate_hz` | 每通道采样率，需明确不是帧率 |
| `source_data_id` | 在语义确认前完整保存旧 `data_id:uint64` |
| `clock_id` | 单调时钟、UTC、PTP 等时钟域 |
| `payload_len` | payload 精确字节数 |

receiver 应根据：

```text
magic + version + payload_schema
```

选择解析器，不能仅根据 payload 长度猜测格式。

### 7.5 用独立适配器兼容历史文件

若需要读取旧数据，推荐后续增加独立离线适配器：

```text
Legacy T510 .bin
    ↓ legacy importer
标准化 raw payload + metadata/index
    ↓
统一分析或归档流程
```

适配器应：

1. 显式按 little-endian 逐字段读取，避免直接强制转换指针；
2. 验证 64-byte 标志头；
3. 严格要求 legacy payload 为 8192 bytes；
4. 将 `package_id` 映射为候选 `frame_id`；
5. 在语义确认前完整保留 `data_id:uint64`；
6. 对导入后的 header 和 payload 计算标准 CRC；
7. 不默认跳过 8 MiB，除非负责人确认其正式含义；
8. 正确处理文件末尾最后一个完整包；
9. 分别统计缺包、重复和乱序；
10. 输出导入数量、异常偏移和丢弃原因。

不建议把旧标记扫描代码直接混入实时 TCP receiver。

---

## 8. 推荐落盘方案

### 8.1 原始整数优先

旧复数数据应优先保存：

```text
int32_le real + int32_le imag
```

不要把 `std::complex<float>` 作为正式归档格式，原因是：

- 可能损失大整数精度；
- 依赖 C++ ABI 和主机字节序；
- 文件本身没有 schema、通道数、采样点数和排列说明。

物理量转换应在分析层进行：

```text
physical_value = raw_int32 × scale + offset
```

其中定点格式、比例、单位和零点必须由硬件或算法负责人给出。

### 8.2 扩充 metadata/index

当前索引字段为：

```csv
frame_id,timestamp_ns,offset,payload_bytes
```

若未来支持多 schema，可扩充为：

```csv
module_id,frame_id,timestamp_ns,timestamp_valid,payload_schema,source_data_id,offset,payload_bytes
```

旧数据没有时间戳时应显式标记 `timestamp_valid=false`，不能用 0 冒充有效时间。

---

## 9. 与上位机负责人逐项确认的问题

### 9.1 文件和传输来源

1. 旧 `.bin` 是网卡原始数据、DMA 缓冲数据，还是程序二次处理后的文件？
2. 网络或光纤线上是否也是“64-byte header + 8192-byte payload”？
3. 为什么固定跳过前 8 MiB？
4. 前 8 MiB 是固定文件头、预热区、无效帧、环形缓冲，还是某次测试的临时约定？
5. 是否有未提供的接收端或 FPGA 端代码？

### 9.2 包头字段

6. `package_id` 是否每次采集从 0 开始？
7. `package_id` 是否会回绕，重启后如何识别？
8. 多块板卡或多路数据的 `package_id` 是各自计数还是全局计数？
9. `data_id` 的准确含义是什么，取值范围多大？
10. `data_id` 能否等价于模块号？
11. `SYNC_WORD_ALT = 0xFFFFFFFF00000000` 表示什么状态或数据类型？

### 9.3 Payload 语义

12. 是否确定为 64 个复数通道？
13. 每包是否确定包含每通道 16 个连续采样点？
14. 排列是否确定为 `sample → channel → real/imag`？
15. 低 32 位是否为实部，高 32 位是否为虚部？
16. 两个分量是否均为二进制补码 `int32`？
17. 数据是否使用 Q31、Q24、Q16 等定点格式？
18. 数值转物理量的比例、单位和零点是什么？
19. `int32 → float` 是否只用于旧分析工具，而不是传输要求？

### 9.4 采样和时间

20. 旧数据每通道采样率是多少？
21. 每包 16 个采样点之间的时间间隔是多少？
22. 一包的产生频率是多少？
23. 是否存在未提供的外层时间戳或同步信息？
24. 当前项目所说的 200 Hz 是帧率、每通道采样率，还是控制周期？

### 9.5 当前项目真实要求

25. 当前 CMB 真实硬件最终输出的是 1704 路标量、64 路复数，还是第三种布局？
26. 需要统一的是线上协议、receiver 落盘格式，还是科学分析输入格式？
27. 上位机已有程序需要读取原始 payload，还是只读取 `_complex.bin`？
28. 是否要求继续兼容历史 T510 文件？
29. 能否提供一个包含已知通道值的真实小样本，以逐字节核对？

---

## 10. 双方最终应形成的协议文档

对齐后建议形成正式《CMB 数据接口协议》，至少包括：

1. 传输方式、连接方向、端口及重连行为；
2. 所有多字节字段的固定字节序；
3. 帧头逐字节偏移、长度、类型、范围和固定值；
4. payload schema：通道数、采样点数、实数/复数、位宽、符号、定点比例和排列；
5. frame ID 初值、回绕、重启和多模块计数规则；
6. 时间戳来源、单位、时钟域和有效性；
7. header/payload CRC 算法及覆盖范围；
8. 丢包、重复、乱序、坏帧和截断帧处理；
9. 至少一个完整真实帧的十六进制样例和字段解码；
10. 原始落盘、索引、分段、文件命名和保留策略。

---

## 11. 建议的当前决策

在字段语义和真实硬件布局确认前：

1. 当前 CMB v1 暂不修改；
2. 将旧代码识别为“64 通道、每包每通道 16 点、复数 `int32` 的旧离线格式参考”；
3. 不将 `data_id` 直接映射成 `module_id`；
4. 不将固定跳过 8 MiB、标志扫描和 `std::complex<float>` 输出纳入新协议；
5. 先向上位机负责人确认本文件第 9 节问题，并要求一个真实小样本；
6. 如果当前硬件仍是 1704 路、200 Hz 标量数据，继续使用 CMB v1，历史文件通过独立离线转换器读取；
7. 如果当前硬件实际使用 64 路复数数据，定义新 payload schema 或 CMB v2，同时保留当前显式长度、时间戳和双 CRC 机制。

本文只给出格式提取、差异分析和统一方案，不代表旧字段业务语义已经获得上位机负责人确认。
