### 多模块验证与真实部署

已确定的目标链路为：下位机主板上的 PCIe SFP 光网卡提供 10 个接口，每路经光纤接入交换机；上位机是一台运行 receiver 的普通电脑，通过交换机汇聚接收。当前下位机安装了 Intel 82599ES 双口 SFP+ 网卡，`enp175s0f0`、`enp175s0f1` 在 2026-09-03 检查时均为 1 Gb/s 全双工且 carrier up。该状态只是当前双口快照；最终交换机、上位机网口、IP/VLAN/路由和速率以网络负责人配置为准。

`configs/ten-channel.example.conf` 每行依次为：

```text
module_id tx_iface rx_iface tx_ip rx_ip port tx_namespace rx_namespace
```

配置中的地址使用文档示例网段，真实运行前必须替换。`module_id` 和 `port` 必须唯一；普通电脑通过一个网口汇聚时，10 行可以重复使用 `rx_iface` 和 `rx_ip`。不用 namespace 时填写 `-`。`tx_iface`/`rx_iface` 记录物理通道映射，程序实际按 IP 绑定。

#### 1. 十路 localhost 软件验证

```bash
cmake --build build -j2
./tools/run-multi-local-smoke.sh --frames 1000
```

结果默认写入 `/tmp/cmb-ten-way-local/summary.json`，每路的 stdout、stderr、逐帧 timing 和 capture 文件分开保存。该测试验证十路独立 TCP 会话、模块身份校验和并行落盘，不等价于真实十路光纤链路验收。

#### 2. 当前双口单机光回环

```bash
sudo ./tools/run-optical-loopback.sh --preset 1000
```

该脚本会创建临时 network namespace、配置指定的两张光口并记录 NIC counters，适合当前双口硬件回归。它会修改测试接口的网络配置，应只在确认接口可被测试占用时运行。

#### 3. 真实双机十路启动

先由网络负责人完成接口地址、交换机、VLAN、路由或 namespace 配置，再把现场值写入配置文件。上位机普通电脑先启动：

```bash
./tools/run-multi-host.sh --role receiver --frames 1000
```

下位机随后启动：

```bash
./tools/run-multi-host.sh --role sender --frames 1000
```

可先使用 `--dry-run` 只校验并打印十路命令。每个角色默认写入 `/tmp/cmb-multi-host/<role>/`。receiver 每路绑定配置中的 `rx_ip:port`；sender 使用 `--bind-host <tx_ip>` 绑定对应源地址后连接 `rx_ip:port`。脚本不创建或修改接口、IP、VLAN、路由、namespace 或交换机配置。

下位机 10 个接口如果处于同一二层或重叠网段，绑定源 IP 仍不足以消除路由和 ARP 歧义，需要网络负责人配置独立子网、VLAN、策略路由或预建 network namespace。两端 `module_id` 必须一致，receiver 会拒绝模块身份不匹配的帧。
