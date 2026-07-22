# 项目协作说明

本项目当前聚焦于 CMB 单模块高速并行数据获取原型。主线方案已经锁定为：MS-01 x86、Ubuntu 24.04 LTS、原生 Linux、TCP 第一版 framing、24h/72h 稳定性测试、PREEMPT_RT 仅做 A/B 备选。

## Skill routing

当用户请求匹配下列场景时，优先调用对应 skill。

- 需求澄清、系统边界收敛、阶段目标讨论
  - 调用 `/office-hours`
  - 适用例子：先做单模块还是多模块、TCP 还是 UDP、是否需要 PREEMPT_RT、先验证什么

- 架构评审、工程方案锁定、测试矩阵审查
  - 调用 `/plan-eng-review`
  - 适用例子：Linux vs PREEMPT_RT、TCP framing、日志与统计、24h/72h soak test、光纤链路拓扑

- 形成可执行任务、阶段性 spec、backlog 项
  - 调用 `/spec`
  - 适用例子：sender/receiver v0.1、环境固化、PREEMPT_RT A/B 测试

- 生成和维护文档
  - 调用 `/document-generate`
  - 适用例子：环境搭建文档、协议文档、测试计划、系统规格说明

- 需要把多份文档汇总、补齐发布级文档
  - 调用 `/document-release`

- 开始实现后，验证程序是否能跑通
  - 调用 `/qa` 或 `/qa-only`
  - 适用例子：单模块 TCP 链路测试、日志检查、metrics 输出检查、24h/72h 稳定性验证

- 出现异常、抖动、断连、延迟长尾，需要定位问题
  - 调用 `/investigate`
  - 适用例子：5 ms 周期不稳定、TCP 长尾、SFP+ 链路异常、CPU/IRQ 干扰

- 代码完成后做 diff review
  - 调用 `/review`

- 代码可运行但想进一步减少重复、降低复杂度、提升可维护性
  - 调用 `/simplify`

- 想自动跑完整的计划评审链路
  - 调用 `/autoplan`

- 需要长时间循环检查状态或定时执行检查任务
  - 调用 `/loop`

## Project-specific guidance

- 当前项目没有 UI，不优先使用 `/plan-design-review`，除非后续增加可视化界面、配置页面或前端工具。
- 当前项目核心不是平均带宽，而是 5 ms 周期稳定性、协议 framing、链路可观测性、长稳测试和后续扩展路径。
- 在没有实测失败之前，不要默认引入 PREEMPT_RT。优先完成原生 Linux 的 24h/72h 实测，再决定是否进入 A/B 对比。
- 对数据链路相关实现，优先保留 frame_id、timestamp、payload_len、CRC、日志和 metrics，不要只做“能通”的裸字节流。

## Stage to skill map

| 阶段 | 目标 | 优先 gstack skills | 优先 superpowers skills |
|---|---|---|---|
| 需求澄清阶段 | 收敛边界、确定先做什么 | `/office-hours` | `superpowers:writing-plans` |
| 系统方案阶段 | 锁定架构、协议方向、测试矩阵 | `/plan-eng-review`、`/autoplan` | `superpowers:writing-plans` |
| 任务拆分阶段 | 把方案拆成 backlog / spec / 子任务 | `/spec` | `superpowers:executing-plans` |
| 文档沉淀阶段 | 生成规格、环境、协议、测试文档 | `/document-generate`、`/document-release` | `superpowers:writing-plans` |
| 协议实现阶段 | sender / receiver / parser / CRC / metrics 开发 | `/review`（阶段性）、`/simplify`（需要时） | `superpowers:test-driven-development`、`superpowers:subagent-driven-development` |
| 原生 Linux 验证阶段 | 验证 5 ms 周期、frame_id、CRC、日志、metrics | `/qa`、`/qa-only` | `superpowers:verification-before-completion` |
| 异常排查阶段 | 定位 TCP 长尾、抖动、断连、SFP+ 异常 | `/investigate` | `superpowers:systematic-debugging` |
| PREEMPT_RT A/B 阶段 | 对比原生 Linux 与 PREEMPT_RT | `/plan-eng-review`、`/qa`、`/investigate` | `superpowers:verification-before-completion`、`superpowers:systematic-debugging`、`superpowers:using-git-worktrees` |
| 长稳测试阶段 | 24h / 72h soak test、周期性检查 | `/qa`、`/qa-only`、`/loop` | `superpowers:verification-before-completion` |
| 代码评审阶段 | diff review、收敛实现质量 | `/review`、`/simplify` | `superpowers:requesting-code-review`、`superpowers:receiving-code-review` |
| 并行实验阶段 | 主线 / A-B 方案并行推进 | `/autoplan`（需要时） | `superpowers:using-git-worktrees`、`superpowers:subagent-driven-development` |

## Recommended default workflow

推荐默认工作流（带 superpowers）：

1. `/office-hours`
   - 并结合 `superpowers:writing-plans`
   - 作用：先把边界、目标、阶段路线讲清楚

2. `/plan-eng-review`
   - 并结合 `superpowers:writing-plans`
   - 作用：锁系统架构、Linux / PREEMPT_RT 策略、TCP framing、测试矩阵

3. `/spec`
   - 并结合 `superpowers:executing-plans`
   - 作用：把 sender / receiver / 环境固化 / A-B 测试拆成可执行工作项

4. `/document-generate`
   - 并结合 `superpowers:writing-plans`
   - 作用：沉淀 system spec、environment、protocol、test plan

5. 开始实现 sender / receiver / parser / metrics
   - 优先使用 `superpowers:test-driven-development`
   - 若任务边界清楚，可加入 `superpowers:subagent-driven-development`

6. `/qa` 或 `/qa-only`
   - 并结合 `superpowers:verification-before-completion`
   - 作用：每完成一个里程碑，先验证 5 ms 周期、frame_id、CRC、日志和 metrics

7. `/review`
   - 如有必要，再配合 `superpowers:requesting-code-review` 或 `superpowers:receiving-code-review`
   - 作用：做 diff review，收敛实现质量

8. 出现异常时 `/investigate`
   - 并优先结合 `superpowers:systematic-debugging`
   - 作用：排查 TCP 长尾、抖动、断连、SFP+ 链路异常、CPU/IRQ 干扰

9. 若原生 Linux 不满足，再进入 PREEMPT_RT A/B 路线
   - 使用 `/plan-eng-review` + `/qa` + `/investigate`
   - 并结合 `superpowers:verification-before-completion`、`superpowers:systematic-debugging`
   - 如果项目已纳入 git，建议配合 `superpowers:using-git-worktrees` 隔离原生 Linux 主线与 PREEMPT_RT 分支

## Superpowers skills（推荐场景）

- `superpowers:writing-plans`
  - 适用：把“单模块原型 → 多模块汇聚 → PREEMPT_RT A/B → 真实前端接入”拆成分阶段执行计划时
  - 原因：适合把长期系统工程拆成清晰阶段，避免实现顺序混乱

- `superpowers:executing-plans`
  - 适用：方案和 spec 已锁定，准备开始按计划逐步实现 sender / receiver / test scripts 时
  - 原因：适合把已批准计划转换成稳定执行节奏

- `superpowers:verification-before-completion`
  - 适用：每完成一个里程碑时
  - 适用例子：单模块 TCP 链路打通后，先验证 5 ms 周期、frame_id、CRC、日志和 metrics，再宣告完成
  - 原因：本项目最怕“看起来能跑，其实长稳和观测性没验证”

- `superpowers:systematic-debugging`
  - 适用：出现周期抖动、TCP 长尾、断连、SFP+ 链路异常、CPU/IRQ 干扰时
  - 原因：适合按假设、证据、实验的方式排查，不会一上来乱改参数

- `superpowers:test-driven-development`
  - 适用：先做协议 parser、frame encoder、CRC、日志统计模块时
  - 原因：适合先把 framing 和边界条件固化成测试，再写实现，降低协议反复改动成本

- `superpowers:subagent-driven-development`
  - 适用：后续把工作拆成互相独立的块时
  - 适用例子：并行处理 sender、receiver、日志分析脚本、环境文档
  - 原因：适合多子任务并行推进，但前提是边界已经清楚

- `superpowers:requesting-code-review` / `superpowers:receiving-code-review`
  - 适用：代码已经形成可 review 的 diff 时
  - 原因：适合在 `/review` 之前或之后补一轮更系统的代码评审流程

- `superpowers:using-git-worktrees`
  - 适用：如果后续把项目纳入 git，并且要并行维护“原生 Linux 主线”和“PREEMPT_RT A/B 分支”
  - 原因：适合隔离不同实验路线，避免互相污染

## Documentation map

- `docs/system-spec.md`：系统范围、硬件基线、操作系统与总体架构
- `docs/environment-setup.md`：Ubuntu 24.04 环境搭建、工具链、调优基线
- `docs/protocol-v0.1.md`：TCP frame 协议、发送端/接收端软件结构
- `docs/test-plan.md`：24h/72h 稳定性测试、验收标准、PREEMPT_RT A/B 方案
