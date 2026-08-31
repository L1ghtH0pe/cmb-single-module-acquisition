
### 十模块并行验证

`configs/ten-channel.example.conf` 是十路配置模板，每行依次为：

`module_id tx_iface rx_iface tx_ip rx_ip port tx_namespace rx_namespace`

真实硬件测试时，把接口、IP、端口和 network namespace 替换成现场值；`module_id` 必须在 sender 与 receiver 两端一致。程序现在支持 `--module-id <0..65535>`，receiver 启用该选项后会拒绝模块身份不匹配的帧。

本机先构建，再运行十路并行 smoke：

```bash
cmake --build build -j2
./tools/run-multi-local-smoke.sh --frames 1000
```

结果默认写入 `/tmp/cmb-ten-way-local/summary.json`（可用 `--output` 改变），每路的 stdout、stderr、逐帧 timing 和 capture 文件分开保存。该脚本用于验证十路独立 TCP 会话、模块身份校验和并行落盘；它不等价于真实十路光纤链路验收。
