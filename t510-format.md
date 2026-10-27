根据 `t510gui/cpp` 中的解析代码，原始 `.bin` 是一个按 64 位字组织、由固定标记分隔的数据流。TCP 客户端只是原样保存网络字节，没有增加额外封装。

## 1. 文件整体结构

```text
┌──────────────────────┬────────────────────────────────────────────┐
│ 前 8 MiB             │ 解析器直接跳过，具体语义无法从代码确定     │
├──────────────────────┼────────────────────────────────────────────┤
│ 数据包 0             │ 64 字节包头 + 负载                        │
├──────────────────────┼────────────────────────────────────────────┤
│ 数据包 1             │ 64 字节包头 + 负载                        │
├──────────────────────┼────────────────────────────────────────────┤
│ ...                  │                                            │
└──────────────────────┴────────────────────────────────────────────┘
```

跳过量为：

```text
16 × 512 KiB = 8,388,608 字节
               = 1,048,576 个 uint64
```

依据：[lgfT510.h](/c/Users/changzhi/T510/t510gui/cpp/lgfT510.h:8)、[packet_parser.cpp](/c/Users/changzhi/T510/t510gui/cpp/packet_parser.cpp:74)。

代码没有解释前 8 MiB 的内容，只能确定所有四种解析器都不处理它。

## 2. 数据包头

包头固定为 8 个 `uint64_t`，共 64 字节：

| 字节偏移 | 64 位值 | 含义 |
|---:|---|---|
| `0x00` | `0x7766554433221100` | 起始字 2 |
| `0x08` | `0xFFEEDDCCBBAA9988` | 起始字 1 |
| `0x10` | 任意 `uint64` | `package_id`，连续包序号 |
| `0x18` | `0xFFFFFFFFFFFFFFFF` 或 `0xFFFFFFFF00000000` | 同步字 |
| `0x20` | 任意 `uint64` | `data_id` |
| `0x28` | `0xFFFFFFFFFFFFFFFF` | 同步字 |
| `0x30` | `0x8899AABBCCDDEEFF` | 结束字 2 |
| `0x38` | `0x0011223344556677` | 结束字 1 |
| `0x40` | — | 负载开始 |

依据：[lgfT510.cpp](/c/Users/changzhi/T510/t510gui/cpp/lgfT510.cpp:4)。

在当前 x86 小端环境中，包头实际字节序列开头为：

```text
00 11 22 33 44 55 66 77
88 99 AA BB CC DD EE FF
<package_id，8 字节小端>
FF FF FF FF FF FF FF FF
或 00 00 00 00 FF FF FF FF
<data_id，8 字节小端>
FF FF FF FF FF FF FF FF
FF EE DD CC BB AA 99 88
77 66 55 44 33 22 11 00
```

代码直接把文件映射为 `uint64_t*`，没有字节序转换，因此原始格式实际依赖主机端序；现有代码显然按小端机器设计。

## 3. 通用负载编码

每个 64 位负载字拆成两个有符号 32 位整数：

```text
uint64 word
├─ bits 31..0  → 第一个 int32
└─ bits 63..32 → 第二个 int32
```

两个整数均采用二进制补码。小端文件中的物理顺序就是：

```text
低 32 位 int32 的 4 字节
高 32 位 int32 的 4 字节
```

依据：[packet_parser.cpp](/c/Users/changzhi/T510/t510gui/cpp/packet_parser.cpp:22)、[lgfT510.h](/c/Users/changzhi/T510/t510gui/cpp/lgfT510.h:25)。

## 4. 四种负载格式

### DDC 数据

- 最小负载：`1024 × uint64 = 8192` 字节。
- 拆分后：2048 个 `int32`。
- 表示：`16` 个时间点 × `64` 通道 × 复数实虚部。
- 每个 `uint64` 正好对应一个复数：

```text
word[j × 64 + ch]
├─ 低 32 位：通道 ch 的实部
└─ 高 32 位：通道 ch 的虚部
```

排列顺序：

```text
时间点0: ch0.re, ch0.im, ch1.re, ch1.im, ... ch63.re, ch63.im
时间点1: ch0.re, ch0.im, ...                         ch63.im
...
时间点15
```

依据：[packet_parser.cpp](/c/Users/changzhi/T510/t510gui/cpp/packet_parser.cpp:17)。

### DDC 抽取数据

- 最小负载：`1024 × uint64 = 8192` 字节。
- 表示：1024 通道，每通道一个复数采样。
- `word[ch]` 的低 32 位是实部，高 32 位是虚部。

依据：[ddc_chouqu_packet.cpp](/c/Users/changzhi/T510/t510gui/cpp/ddc_chouqu_packet.cpp:17)。

### SQUID 数据

- 最小负载：`1024 × uint64 = 8192` 字节。
- 拆分后：2048 个标量 `int32`。
- 表示：`16` 个时间点 × `128` 通道。
- 不组成复数：

```text
sample[j][ch] = int32_sequence[j × 128 + ch]
```

即一个 `uint64` 顺序装入两个相邻通道或标量。

依据：[squid_packet.cpp](/c/Users/changzhi/T510/t510gui/cpp/squid_packet.cpp:18)。

### TES / FRD 数据

- 最小负载：`512 × uint64 = 4096` 字节。
- 拆分后：1024 个标量 `int32`。
- 表示：1024 通道，每包每通道一个采样。

```text
word[ch / 2]
├─ 偶数通道：低 32 位
└─ 奇数通道：高 32 位
```

依据：[tes_packet.cpp](/c/Users/changzhi/T510/t510gui/cpp/tes_packet.cpp:18)。

## 5. 分包规则与限制

负载中没有被代码使用的长度字段或校验和。解析器通过“搜索下一个合法 64 字节包头”确定当前负载长度：

- DDC、DDC 抽取和 SQUID 要求负载至少 1024 个 `uint64`。
- TES/FRD 要求负载至少 512 个 `uint64`。
- `DATA_LEN` 是最小长度，不是强制固定长度。
- `package_id` 应逐包加一，用于打印丢包提示。
- `data_id` 会被读取，但后续完全没有使用。
- 搜索按 8 字节步进，无法恢复非 8 字节对齐的数据流。
- 正好结束于文件末尾的最后一个包可能不会被正常输出，因为解析逻辑需要额外的尾部空间或后续包头。

依据：[lgfT510.cpp](/c/Users/changzhi/T510/t510gui/cpp/lgfT510.cpp:22)。

## 6. 代码中的明显异常

[squid_packet.cpp](/c/Users/changzhi/T510/t510gui/cpp/squid_packet.cpp:119) 中，数据容器是 `vector<int32_t>`，但写文件时使用了 `sizeof(fcomplex)`：

```cpp
fwrite(ch_data.data(), sizeof(fcomplex), ch_data.size(), f1);
```

`int32_t` 为 4 字节，`complex<float>` 通常为 8 字节。这会从缓冲区越界读取，并写出约两倍长度的损坏数据。它不改变原始 SQUID 格式，但会影响解析后的 `_squid.bin` 文件。