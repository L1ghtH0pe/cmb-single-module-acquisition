# Linux PREEMPT_RT实时化技术分析

## 1 实时系统基本概念

### 1.1 实时性判据与响应时间构成

实时系统的核心并非获得最高计算速度，而是在规定时间内完成指定任务。与通用操作系统主要关注平均性能和系统吞吐量不同，实时系统更加关注响应时间的确定性、最坏延迟以及截止时间违约情况。

根据任务错过截止时间所造成的影响，实时系统通常分为三类：

| 类型 | 特征 | 典型场景 |
|---|---|---|
| 硬实时 | 任何一次截止时间违约均不可接受 | 安全保护、底层飞控、功率电子控制 |
| 固定实时（firm real-time） | 超时结果失去价值，但少量违约可以容忍 | 工业检测、部分通信处理 |
| 软实时 | 超时降低服务质量，但不会导致系统失效 | 音视频、交互系统、部分数据处理 |

从外部事件发生到应用完成处理，其响应时间可以表示为：

$$
T_{\mathrm{response}}
=
T_{\mathrm{hardware}}
+ T_{\mathrm{interrupt}}
+ T_{\mathrm{wakeup}}
+ T_{\mathrm{schedule}}
+ T_{\mathrm{execution}}
+ T_{\mathrm{output}}
$$

其中，$T_{\mathrm{hardware}}$ 表示硬件和固件响应时间，$T_{\mathrm{interrupt}}$ 表示中断响应及处理时间，$T_{\mathrm{wakeup}}$ 表示任务被唤醒所需时间，$T_{\mathrm{schedule}}$ 表示任务从进入就绪状态到实际获得 CPU 的时间，$T_{\mathrm{execution}}$ 表示应用自身执行时间，$T_{\mathrm{output}}$ 表示输出设备及通信链路延迟。

PREEMPT_RT 主要优化中断、任务唤醒、内核抢占、锁阻塞和调度等环节，对硬件固有延迟及应用算法执行时间没有直接加速作用。因此，评价 PREEMPT_RT 时应重点考察：

- 最大调度延迟；
- 中断响应延迟；
- 延迟抖动；
- P99、P99.9 及 P99.99 等尾部延迟；
- 截止时间违约次数；
- 压力负载下的长期稳定性。

平均延迟只能反映系统的一般状态，不能充分说明实时能力。

### 1.2 实时性的分层保障

系统实时性不是由某一个内核选项单独决定的，而是硬件、操作系统、运行框架和应用代码共同作用的端到端属性。即使采用了实时操作系统，如果中间件存在无界阻塞、应用在关键路径中动态分配内存，或者硬件和固件产生较长的不可控延迟，系统仍可能错过截止时间。

| 保障层次 | 主要作用 | 需要控制的关键问题 |
|---|---|---|
| 硬件与固件层 | 提供计时、中断、计算和数据传输的物理基础 | 系统管理中断、定时器精度、缓存与内存竞争、总线阻塞、功耗状态和不可屏蔽固件活动 |
| 实时操作系统层 | 提供具有优先级的任务调度、中断管理和同步机制 | 抢占延迟、中断延迟、优先级反转、临界区、调度策略和资源隔离 |
| 实时框架与中间件层 | 提供组件模型、执行器、通信、生命周期和诊断工具，减少重复开发 | 执行器调度、消息队列上界、内存分配、进程间通信、序列化和日志路径是否确定 |
| 确定性应用层 | 实现最终业务逻辑并满足任务周期和截止时间 | 最坏执行时间、任务优先级、锁依赖、内存使用、I/O 阻塞、超时处理和降级策略 |

实时框架可以包括 ROS 2、Orocos RTT、AimRT 和 Cyber RT 等机器人或控制系统运行时。它们能够提供组件化开发、通信抽象、执行器配置和性能诊断，降低开发者直接处理底层线程与通信细节的成本；但“支持实时配置”并不等于整个框架及其所有插件都具有确定的最坏执行时间。框架只有运行在合适的实时内核之上，并且关键执行器、通信链路及应用回调均遵守实时编程约束时，才能成为实时保障链的一部分。[ROS 2实时系统设计说明](https://design.ros2.org/articles/realtime_background.html) [Orocos RTT说明](https://www.orocos.org/rtt/) [AimRT文档](https://docs.aimrt.org/) [Cyber RT说明](https://developer.apollo.auto/cyber.html)

除技术配置外，还需要建立贯穿开发周期的工程管理措施：在需求阶段分解端到端截止时间预算，在设计阶段审查任务优先级、CPU、IRQ 和共享资源关系，在实现阶段开展实时安全接口检查、静态分析和代码评审，在测试阶段通过压力测试、端到端测量和回归测试验证最大延迟。运行期还应监测截止时间违约、队列积压、缺页和异常阻塞，并由非实时线程记录诊断信息。操作系统和框架提供的是实时能力，应用设计及持续验证决定这些能力能否转化为系统级保证。

## 2 PREEMPT_RT与RT-patch

### 2.1 定义及相互关系

PREEMPT_RT 是 Linux 内核的实时抢占技术体系，对应内核配置项：

```text
CONFIG_PREEMPT_RT=y
```

PREEMPT-RT 是其常见书写形式。RT-patch 或 `-rt patch` 则是将 PREEMPT_RT 功能加入特定 Linux 版本的补丁集合。二者并非不同的实时方案，而是分别表示功能体系和软件交付形式。

其关系可以表示为：

```text
PREEMPT_RT：实时抢占机制
RT-patch：承载该机制的补丁集合
RT kernel：启用 CONFIG_PREEMPT_RT 后生成的内核
```

早期 PREEMPT_RT 主要位于 Linux 主线之外，用户需要将相应版本的 RT-patch 应用到标准内核源码，然后启用 `CONFIG_PREEMPT_RT` 并重新编译。从 Linux 6.12 开始，PREEMPT_RT 的核心能力正式进入主线内核，但不会默认启用。RT 项目目前仍会发布带有 `-rtN` 后缀的版本，用于提供额外修复和尚未合入主线的优化。[Linux Foundation PREEMPT_RT版本说明](https://wiki.linuxfoundation.org/realtime/preempt_rt_versions)

普通可抢占配置与 PREEMPT_RT 之间存在本质区别：

```text
CONFIG_PREEMPT=y
≠ CONFIG_PREEMPT_RT=y
```

`CONFIG_PREEMPT` 只是扩大普通内核进程上下文中的可抢占范围，仍保留硬中断、自旋锁、软中断等不可调度路径；PREEMPT_RT 则进一步改造中断、锁、定时器和内核执行上下文，目标是降低最坏调度延迟。[Linux内核抢占配置源码](https://raw.githubusercontent.com/torvalds/linux/master/kernel/Kconfig.preempt)

### 2.2 支持平台

PREEMPT_RT 属于 Linux 内核功能，其支持平台可以分为三个层次。

首先是处理器体系结构。当前主线内核中声明支持 `ARCH_SUPPORTS_RT` 的主要架构包括：

- x86/x86-64；
- ARM；
- ARM64/AArch64；
- RISC-V；
- LoongArch。

具体支持状态应以所使用内核版本的 Kconfig 为准，可参考主线内核的 [x86](https://raw.githubusercontent.com/torvalds/linux/master/arch/x86/Kconfig)、[ARM](https://raw.githubusercontent.com/torvalds/linux/master/arch/arm/Kconfig)、[ARM64](https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/Kconfig)、[RISC-V](https://raw.githubusercontent.com/torvalds/linux/master/arch/riscv/Kconfig) 和 [LoongArch](https://raw.githubusercontent.com/torvalds/linux/master/arch/loongarch/Kconfig) 配置。

其次是软件发行平台，主要包括：

- 自行编译的主线 Linux；
- Ubuntu Real-time；
- RHEL for Real Time；
- Debian RT kernel；
- Yocto `meta-realtime`；
- 芯片或设备厂商提供的实时 Linux BSP；
- 经过专门配置的实时虚拟机。

最后是具体硬件平台。体系结构支持并不意味着任意处理器或开发板均能获得相同实时性能。BIOS、固件、系统管理中断、设备驱动、缓存竞争、内存控制器、PCIe、USB、DMA 及 IOMMU 均可能引入额外延迟。因此，实时性能必须在具体硬件和目标负载下进行验证。

## 3 Linux实时延迟来源与PREEMPT_RT优化机制

普通 Linux 主要面向通用计算和吞吐量优化。高优先级任务即使已经进入可运行状态，也可能因内核正在执行不可抢占代码、硬中断或锁临界区而无法立即获得 CPU。

PREEMPT_RT 针对不同延迟来源实施相应改造：

| 普通内核中的延迟来源 | PREEMPT_RT优化机制 | 预期作用 |
|---|---|---|
| 内核代码长时间不可抢占 | 扩大内核可抢占范围 | 缩短高优先级任务等待时间 |
| 硬中断不受任务调度优先级控制 | 中断线程化 | 使中断处理可调度、可分配优先级 |
| 自旋锁临界区禁止抢占 | 将多数自旋锁转换为可睡眠实时锁 | 减少禁止抢占时间 |
| 低优先级任务持锁 | `rt_mutex` 优先级继承 | 缓解优先级反转 |
| SoftIRQ 执行时间不确定 | 在线程上下文中处理 | 允许高优先级任务抢占 |
| 定时器和 RCU 回调产生系统噪声 | 回调线程化或迁移 | 减少实时 CPU 受到的干扰 |

### 3.1 内核全面可抢占

PREEMPT_RT 使除少数底层代码外的大部分内核执行路径均可被高优先级任务抢占，从而减少任务因内核态操作造成的等待。

仍不可抢占的路径主要包括：

- 体系结构底层入口代码；
- 调度器自身；
- 部分低层硬中断；
- NMI；
- 使用 `raw_spinlock_t` 保护的关键区域。

因此，PREEMPT_RT 并未完全消除不可抢占代码，而是尽可能缩短并限制其范围。

### 3.2 中断线程化

普通内核中的硬中断可以打断用户线程，而且不受用户线程调度优先级控制。当中断处理程序执行时间过长时，高优先级实时线程只能等待。

PREEMPT_RT 将大多数中断处理过程划分为两个阶段：

```text
硬中断入口
    ↓
完成最小必要操作并唤醒 IRQ 线程
    ↓
调度器按照优先级运行 IRQ 线程
```

中断线程化后，可以设置 IRQ 线程的优先级和 CPU 亲和性，也允许优先级更高的实时线程抢占低优先级 IRQ 线程。调度时钟、中断控制器等少数关键中断仍保留在硬中断上下文。

### 3.3 可睡眠锁

普通内核获取 `spinlock_t` 后通常禁止抢占。若锁临界区较长，高优先级任务会被迫等待。

PREEMPT_RT 将大部分 `spinlock_t` 和 `rwlock_t` 转换为基于 `rt_mutex` 的可睡眠锁。任务等待锁时不再持续占用 CPU 自旋，而是进入睡眠状态，由调度器运行其他任务。真正不能睡眠的底层路径继续使用 `raw_spinlock_t`。

这种改造降低了锁竞争对调度延迟的影响，但也增加了锁管理和上下文切换开销。

### 3.4 优先级继承

当高优先级任务等待低优先级任务持有的锁时，若中优先级任务持续抢占低优先级任务，就会产生优先级反转。

PREEMPT_RT 通过 `rt_mutex` 实现优先级继承。低优先级持锁任务临时继承等待者的高优先级，从而优先完成临界区并释放锁。锁释放后，任务恢复原有优先级。

该机制不能消除所有锁阻塞，但可以避免持锁任务因其他中优先级任务干扰而产生无界延迟。

### 3.5 SoftIRQ、定时器和RCU处理

普通 Linux 中的 SoftIRQ、定时器及 RCU 回调可能在不可抢占或优先级不明确的上下文中集中运行，引起延迟尖峰。

PREEMPT_RT 尽可能将上述工作移入可调度线程，使调度器能够控制其执行优先级。部分回调还可以迁移到非实时 CPU，从而减少实时 CPU 上的操作系统噪声。

PREEMPT_RT 的完整实现机制可参考 Linux 内核官方文档中的 [实时抢占工作原理](https://docs.kernel.org/core-api/real-time/theory.html) 和 [实时内核与普通内核的差异](https://docs.kernel.org/core-api/real-time/differences.html)。

## 4 实时系统配套优化

PREEMPT_RT 解决的是内核可抢占性问题，但仅启用实时内核通常不足以获得稳定的实时性能。完整的实时优化还需要覆盖调度、CPU、内存、驱动和硬件等层次。

### 4.1 实时调度策略

实时线程需要显式使用：

- `SCHED_FIFO`；
- `SCHED_RR`；
- `SCHED_DEADLINE`。

PREEMPT_RT 不会自动将普通应用转换为实时线程。未设置实时调度策略的应用通常仍运行在 `SCHED_OTHER` 策略下。

实时优先级必须统一规划。优先级设置过低会导致关键线程响应不及时；设置过高且线程不主动阻塞，则可能导致系统服务和低优先级任务长期得不到执行。

### 4.2 实时调度带宽与失控保护

`SCHED_FIFO`、`SCHED_RR` 或 `SCHED_DEADLINE` 线程如果持续运行而不阻塞，可能长期占据 CPU。Linux 通过以下参数限制实时调度类在一个周期内可使用的 CPU 时间：

```bash
cat /proc/sys/kernel/sched_rt_period_us
cat /proc/sys/kernel/sched_rt_runtime_us
```

系统常见默认值分别为 1 000 000 μs 和 950 000 μs，即每 1 s 最多向实时和截止时间任务分配 950 ms，并为普通任务保留 5% 的 CPU 时间。实时任务耗尽预算后可能被节流至下一周期，从而产生明显延迟。因此，测试和部署时应记录这两个参数，避免将带宽节流误判为 PREEMPT_RT 本身的调度异常。[Linux内核实时调度带宽说明](https://docs.kernel.org/scheduler/sched-rt-group.html)

将 `sched_rt_runtime_us` 设置为 `-1` 可以关闭该限制，但失控的实时线程可能永久占据 CPU。只有在实时任务使用专用隔离核心、系统保留 housekeeping CPU，并具备看门狗或故障终止机制时，才应根据测试结果考虑该配置；一般系统应保留节流保护。还可以使用 `RLIMIT_RTTIME` 限制单个实时进程连续消耗的 CPU 时间。

### 4.3 CPU和IRQ隔离

可以为实时线程分配专用 CPU，并将以下活动迁移到其他 CPU：

- 普通用户进程；
- 非关键 IRQ；
- 内核工作线程；
- RCU 回调；
- 系统管理与日志任务。

CPU 隔离的目标是减少实时线程与普通任务之间的调度竞争，而不是单纯提高 CPU 利用率。

### 4.4 内存实时化

缺页、内存回收和动态内存分配均可能产生不可预测延迟。实时应用通常需要：

- 使用 `mlockall()` 锁定当前及未来内存；
- 提前分配缓冲区；
- 提前访问线程栈；
- 避免在实时循环中调用 `malloc()` 和 `free()`；
- 避免实时路径触发文件映射和写时复制。

### 4.5 功耗管理和硬件配置

CPU 从深度休眠状态恢复、动态调频及共享执行资源均可能增加延迟。根据平台和实时要求，可以考虑：

- 限制深度 C-state；
- 固定或限制 CPU 频率变化；
- 关闭不必要的节能机制；
- 根据测试结果决定是否关闭 SMT；
- 检查 BIOS 和固件中的实时相关配置。

上述设置会增加系统功耗，必须在实时性和能耗之间进行权衡。

## 5 实时性能优化程度及开销

### 5.1 优化效果

PREEMPT_RT 主要改善最大延迟和尾部延迟，而非单纯降低平均值。

#### 早期 x86 平台的同机对照

Canonical 在 Intel Core i5-5300U、压力负载条件下给出的测试结果如下。由于该处理器发布时间较早，本组数据主要作为普通内核与 PREEMPT_RT 在同一硬件上的历史对照：

| 运行环境 | 普通内核最大延迟 | PREEMPT_RT最大延迟 | 最大延迟降低比例 |
|---|---:|---:|---:|
| 裸机 | 1496 μs | 17 μs | 约98.9% |
| 容器 | 2118 μs | 21 μs | 约99.0% |
| 虚拟机 | 13579 μs | 2282 μs | 约83.2% |

在裸机场景中，最大延迟改善约 88 倍，而平均延迟由 12 μs 降低到 4 μs，仅改善约 3 倍。这说明 PREEMPT_RT 对长尾延迟的优化明显大于对平均延迟的优化。[Canonical实时内核测试](https://ubuntu.com/blog/minimising-latency-in-your-edge-cloud-with-real-time-kernel)

#### 新款 x86 平台的绝对时延

为提高数据的时效性，可进一步参考 OSADL QA Farm 对第 12、13 代 Intel 处理器的持续监测结果。两组测试均采用 `cyclictest`，实时线程优先级为 99，采样 1 亿次，单次测试窗口约为 5 h 33 min：

| 处理器 | 微架构 | PREEMPT_RT 内核 | 最大调度延迟 |
|---|---|---|---:|
| Intel Core i3-12100E | Alder Lake，第 12 代 | Linux 6.1.92-rt32 | 28 μs |
| Intel Core i3-13100E | Raptor Lake，第 13 代 | Linux 6.12.1-rt6 | 19 μs |

i3-12100E 的[平台配置](https://www.osadl.org/Profile-of-system-in-rack-8-slot-7.qa-profile-r8s7.0.html?shadow=0)和[测试记录](https://www.osadl.org/Latency-plot-of-system-in-rack-8-slot.qa-latencyplot-r8s7.0.html?latencies=Show&shadow=0&showno=all)，以及 i3-13100E 的[平台配置](https://www.osadl.org/Profile-of-system-in-rack-8-slot-6.qa-profile-r8s6.0.html?shadow=1)和[测试记录](https://www.osadl.org/Latency-plot-of-system-in-rack-8-slot.qa-latencyplot-r8s6.0.html?latencies=Show&shadow=1&showno=all)，表明经过合理配置的较新 x86 平台可将本组 `cyclictest` 最大调度延迟控制在约 20～30 μs。由于这些记录没有提供相同硬件运行普通内核时的对应数据，因此只能用于说明 PREEMPT_RT 的绝对时延水平，不能据此计算优化倍数。

OSADL 的另一组示例中，非实时内核最大延迟约为 39.7 ms，而 PREEMPT_RT 内核约为 18～22 μs，差距达到三个数量级。但 OSADL 同时指出，短时间或无压力条件下的测试不能证明系统具有可靠实时能力，必须进行长期压力验证。[OSADL PREEMPT_RT测试说明](https://www.osadl.org/Realtime-Preempt-Kernel.kernel-rt.0.html)

#### 新款 ARM 平台的同机对照

Raspberry Pi 5 采用四核 Arm Cortex-A76 处理器，硬件平台比既有 Raspberry Pi 3/4 研究更新。2024 年 OSPERT 研究使用 `cyclictest` 和多种压力负载，对普通 Linux 与 PREEMPT_RT 进行了同平台比较。结果显示，PREEMPT_RT 的最大延迟约降至普通 Linux 的 1/294；换言之，普通内核的最大延迟约为实时内核的 294 倍。[Raspberry Pi 5实时能力评估](https://researchportal.vub.be/nl/publications/a-preliminary-assessment-of-the-real-time-capabilities-of-real-ti/)

2026 年的另一项 Raspberry Pi 5 实验采用 250 Hz 控制任务并施加重载，普通内核的最坏时延超过 9 ms，而 PREEMPT_RT 的最坏时延低于 225 μs。按照论文给出的两端数值计算，PREEMPT_RT 将最坏时延降低到普通内核的约 1/40 以下，使任务响应由毫秒量级进入百微秒量级。该实验测量的是控制任务激活时延，而不是标准 `cyclictest` 调度延迟，因此不能与前述 x86 数据直接横向比较。[Raspberry Pi 5控制任务调度实验](https://arxiv.org/abs/2604.19275)

需要注意的是，上述 2026 年论文摘要所述“降低近 88%”与“超过 9 ms 降至 225 μs 以下”在数学上并不一致。因此，本文引用其原始时延数值，不直接采用该百分比。

ARM 嵌入式平台的相关研究表明，PREEMPT_RT 通常能够显著降低最大响应延迟，但受 SoC、驱动和共享内存竞争影响，其最坏延迟仍可能达到数百微秒，偶发情况下甚至超过 1 ms。[ARM平台PREEMPT_RT性能研究](https://www.mdpi.com/2079-9292/10/11/1331)

#### 结果归纳

上述结果形成了两层证据：较新的 Intel 平台说明 PREEMPT_RT 能够达到的绝对调度时延水平；Raspberry Pi 5 的同平台对照则说明它相对于普通内核能够取得的优化幅度。综合这些数据，可以得出以下结论：

1. 最小延迟可能变化不大；
2. 平均延迟通常改善有限；
3. PREEMPT_RT 的主要收益体现在最大延迟和尾部延迟，最大延迟通常可改善一个至两个数量级，特定条件下可达到三个数量级；
4. 较新 x86 平台经合理配置后，`cyclictest` 最大调度延迟可进入约 20～30 μs 量级；
5. 新款 CPU 并不必然带来更好的最坏时延，BIOS/SMI、C-state、频率调节、IRQ 亲和性、内存竞争和驱动实现仍可能成为主要限制因素；
6. 物理机通常优于虚拟机，但任何优化幅度都必须结合硬件、内核版本、负载和测试方法解释，不能脱离实验条件给出统一数值。

### 5.2 测试方法

实时性能测试应在明确的硬件、内核和负载条件下进行，重点评估调度延迟的分布和最坏观测值，并进一步验证应用是否满足截止时间。测试可分为调度延迟测量、异常来源分析和端到端验证三个层次。

#### 5.2.1 测量内容与指标

| 测量内容 | 推荐工具 | 主要指标 |
|---|---|---|
| 周期线程唤醒延迟 | `cyclictest` | Min、Avg、P99、P99.9、Max |
| IRQ 与线程调度延迟 | `rtla timerlat` | IRQ latency、Thread latency |
| 操作系统与硬件噪声 | `rtla osnoise`、`rtla hwnoise` | Max Single、IRQ、SoftIRQ、NMI/HW |
| 应用端到端延迟 | 应用埋点、GPIO、逻辑分析仪或示波器 | 输入到输出总延迟、截止时间违约率 |

`cyclictest` 通过比较周期线程的预定唤醒时刻与实际运行时刻来测量调度延迟。[Linux Foundation cyclictest说明](https://wiki.linuxfoundation.org/realtime/documentation/howto/tools/cyclictest/start) 结果不应只报告平均值，还应报告高分位数、最大值、超阈值次数和截止时间违约率。最大值必须与测试时长和样本数量同时给出，因为测试时间越长，发现低频长尾延迟的概率越高。

#### 5.2.2 实验条件与负载

普通内核与 PREEMPT_RT 的对照实验应尽量只改变内核抢占模型，并保持以下条件一致：

- CPU、主板、BIOS、微码和外设固件；
- 内核版本、RT 补丁、启动参数和测试工具版本；
- 调度策略、优先级、CPU/IRQ 亲和性及 SMT 状态；
- CPU 调频、Turbo、C-state 和 PCIe ASPM 等电源设置；
- 测试周期、持续时间、样本数及压力负载参数。

测试至少应覆盖以下场景：

| 场景 | 主要负载 | 测试目的 |
|---|---|---|
| 空闲基线 | 仅保留必要服务 | 测量基础调度延迟 |
| 计算压力 | CPU、内存和进程切换 | 检查调度和共享资源竞争 |
| I/O 压力 | 存储读写和双向网络流量 | 检查 IRQ、SoftIRQ 和驱动路径 |
| 混合及实际应用 | 多种压力并发或运行目标程序 | 观察长尾并验证真实部署状态 |

人工压力可由 `stress-ng` 构造，例如：

```bash
stress-ng --cpu 0 --cpu-method all \
  --vm 2 --vm-bytes 70% --iomix 4 \
  --timeout 24h --metrics-brief
```

压力参数应根据核心数和内存容量调整，避免触发 OOM。正式实验宜至少独立重复 3 次；可先进行短时间预试验，再根据系统风险进行 24 h 或更长时间的压力测试。一次固定配置的测试不能覆盖所有延迟来源，因此还应结合实际应用负载。[cyclictest测试设计](https://wiki.linuxfoundation.org/realtime/documentation/howto/tools/cyclictest/test-design)

#### 5.2.3 工具与执行流程

在 SMP 平台上，可使用下列命令进行一线程一核心的调度延迟测试：

```bash
sudo cyclictest --mlockall --smp --priority=95 \
  --interval=200 --distance=0 --duration=24h \
  --histogram=2000 --quiet
```

其中，`--mlockall` 用于避免换页，`--priority` 和 `--interval` 应根据目标任务调整，`--histogram` 的范围应高于预期最大延迟。若实时任务绑定在隔离核心上，还应使用相同 CPU 亲和性进行测试。

本文出现的优先级 99、95 和 80 分别对应外部 OSADL 数据的原始测试条件、长期基准测试示例和部署检查示例，并不构成统一推荐值。正式实验应采用与目标应用相同的优先级，并结合 IRQ 线程和其他实时线程统一规划；单纯提高测试线程优先级可能抢占其所依赖的设备 IRQ 线程，使测量结果偏离实际应用行为。

当出现异常峰值时，可使用以下工具进一步定位：

- `rtla timerlat`：区分定时器 IRQ 延迟和线程调度延迟；[内核文档](https://docs.kernel.org/tools/rtla/rtla-timerlat.html)
- `rtla osnoise`：分析 IRQ、SoftIRQ 和内核线程产生的系统噪声；[内核文档](https://docs.kernel.org/tools/rtla/rtla-osnoise-top.html)
- `rtla hwnoise`：检查 NMI、SMI、固件和硬件造成的不可屏蔽停顿。[内核文档](https://docs.kernel.org/tools/rtla/rtla-hwnoise.html)

推荐流程为：记录软硬件配置，分别完成普通内核和 PREEMPT_RT 的空闲测试、单项压力测试及混合压力测试；每组实验重复运行并保存原始直方图；发现异常后再使用 `rtla` 跟踪，最后进行应用输入到输出的端到端验证。跟踪工具本身会引入额外开销，因此异常分析应与正式基准测试分开进行。

#### 5.2.4 结果报告与方法局限

普通内核与 PREEMPT_RT 的最大延迟改善可表示为：

```text
降低比例 = (L_nonRT,max - L_RT,max) / L_nonRT,max × 100%
改善倍数 = L_nonRT,max / L_RT,max
```

比较时必须保证硬件、负载、周期、优先级、亲和性、测试时长和样本规模一致。论文中至少应报告平台与内核配置、测试命令、压力负载、重复次数、Min、Avg、P99、P99.9、Max，以及超过任务截止时间的次数；不能只选择多次测试中最好的一组结果。

`cyclictest` 测量的是周期线程唤醒延迟，不包含设备采样、驱动处理、数据复制、应用计算、网络传输和执行器响应，因此不能代替端到端测试。此外，压力测试只能说明在给定条件和时间内观测到的结果，不能从数学上证明系统绝不会出现更大的延迟。硬实时或安全关键系统仍需结合最坏执行时间分析和故障保护机制。

### 5.3 性能代价

PREEMPT_RT 通过增加抢占点、IRQ 线程和实时锁，以部分吞吐量换取更稳定的响应时间。其开销主要包括：

- 上下文切换次数增加；
- IRQ 线程调度开销；
- `rt_mutex` 及优先级继承开销；
- 多核锁同步开销；
- 网络和存储批处理效率下降；
- CPU 功耗增加。

性能损失与工作负载密切相关。纯用户态计算可能变化较小，而高锁竞争、网络、文件系统和共享设备访问可能受到更明显影响。有研究报告显示，PREEMPT_RT 在特定 UnixBench 测试中的综合性能下降约 6%，多核共享资源密集场景下降可达 29%。这些数据只能反映相应实验条件，不能作为所有平台的固定开销。[PREEMPT_RT吞吐量研究](https://ipsj.ixsq.nii.ac.jp/record/224382/files/IPSJ-JNL6402033.pdf)

## 6 与其他实时技术的比较

### 6.1 主流方案对比

| 技术方案 | 架构定位 | 主要优势 | 主要局限 | 典型场景 |
|---|---|---|---|---|
| 普通 Linux 或 `CONFIG_PREEMPT` | 通用单内核 | Linux 兼容性完整，部署和开发成本较低 | 最坏延迟和压力下长尾通常较大 | 通用计算、交互、音视频和软实时任务 |
| PREEMPT_RT | Linux 原生实时抢占模型 | 最大限度复用 Linux 驱动、网络、文件系统和应用生态 | 仍受固件、驱动和复杂内核路径影响，严格最坏时间分析困难 | 工业、通信、机器人和实时服务 |
| Xenomai 3 Cobalt 或 Xenomai 4 EVL | Linux 与高优先级实时核心协同的双内核架构 | 关键任务运行在独立的高优先级执行域，可获得较强的隔离和较低的有界延迟 | 内核版本需与 Dovetail/EVL 分支匹配，关键应用和实时 I/O 可能需要专用 API 或驱动适配 | 对 Linux 功能和更严格实时响应均有要求的控制系统 |
| FreeRTOS | 面向 MCU 和小型 MPU 的轻量级 RTOS 内核 | 软件规模小、启动快，任务和中断行为较易分析 | 完整进程隔离、驱动和通用应用生态弱于 Linux 及大型商业 RTOS | 资源受限设备、固定功能控制和边缘节点 |
| VxWorks | 商业嵌入式 RTOS 及安全产品系列 | BSP、开发工具和商业支持成熟，特定版本提供多行业认证证据 | 商业许可和工具链成本较高，认证能力与具体产品版本和目标配置绑定 | 航空航天、工业、交通和医疗等关键系统 |
| QNX Neutrino | 基于消息传递的商业微内核 RTOS | 驱动、文件系统等服务运行在用户空间，故障隔离和模块化能力较强，并支持 POSIX 接口 | 商业许可、BSP 适配和安全认证具有版本及配置约束 | 汽车、交通、医疗、工业和任务关键系统 |

Xenomai 3 的 Cobalt 核心和 Xenomai 4 的 EVL 核心均采用双内核思路：通过 Dovetail 在 Linux 内核中建立高优先级的带外执行阶段，使关键线程优先于普通 Linux 活动运行。该方案并非无条件优于 PREEMPT_RT；只有当关键代码和 I/O 能够留在实时执行域、目标硬件具有适配支持并且额外维护成本可以接受时，其架构优势才能体现。Xenomai 4 将 EVL 作为核心，并提供 `libevl` 用户接口和带外 I/O 能力。[Xenomai 3 Cobalt说明](https://doc.xenomai.org/v3/html/TROUBLESHOOTING.COBALT/index.html) [Xenomai 4 EVL架构](https://v4.xenomai.org/overview/index.html)

VxWorks 和 QNX 都是面向嵌入式及任务关键系统的商业 RTOS。VxWorks 的优势主要是长期积累的 BSP、工具、商业支持和面向安全项目的专用版本；QNX 的突出特点是微内核架构，将大部分系统服务置于内存保护的用户空间，以增强故障隔离和可恢复性。两者均可提供面向特定标准的认证产品或证据包，但采用普通版本并不意味着最终系统自动通过安全认证。[VxWorks产品说明](https://www.windriver.com/products/embedded/vxworks) [VxWorks安全平台](https://www.windriver.com/products/vxworks/safety-platforms) [QNX微内核说明](https://qnx.com/developers/docs/8.0/com.qnx.doc.neutrino.sys_arch/topic/kernel.html) [QNX认证产品说明](https://www.qnx.com/products/neutrino-rtos/certified-plus.html)

### 6.2 比较结论与使用边界

Ubuntu 的 `linux-lowlatency` 是采用低延迟启动配置的内核变体，主要面向音频和交互等低延迟负载，但不能据此认定其启用了 `CONFIG_PREEMPT_RT`。因此，`linux-lowlatency` 不应作为 PREEMPT_RT 的等价替代，实际抢占模型仍应通过 `/boot/config-$(uname -r)` 验证。[Ubuntu linux-lowlatency软件包说明](https://packages.ubuntu.com/linux-lowlatency)

不存在脱离具体硬件、内核版本、驱动、负载和测试方法的固定性能排序。双内核方案通常更有利于隔离普通 Linux 干扰，但专用 API 和驱动适配会增加迁移成本；商业 RTOS 能够提供成熟支持和认证证据，但存在许可、供应商和目标平台约束；PREEMPT_RT 的主要优势是在保留 Linux 生态的同时提供较高的实时确定性，其不足是无法完全消除硬件、固件和复杂内核路径造成的延迟，也难以提供形式化证明的最坏响应时间。选型时应同时比较可验证的最大延迟、生态兼容性、开发与维护成本、故障隔离能力和认证需求，不能只依据平均延迟或厂商宣传中的单项指标。

## 7 FreeRTOS与PREEMPT_RT的对比

FreeRTOS 和 PREEMPT_RT 都用于构建实时系统，但二者并不是同一层次的替代方案。FreeRTOS 是面向微控制器和小型微处理器的轻量级实时操作系统内核；PREEMPT_RT 则是在完整 Linux 内核中增强可抢占性和调度确定性的技术。前者强调较小的软件规模和直接、可控的任务执行，后者强调在保留 Linux 功能与生态的条件下降低最坏调度延迟。[FreeRTOS内核概述](https://www.freertos.org/Documentation/02-Kernel/01-About-the-FreeRTOS-kernel/01-FreeRTOS-kernel)

### 7.1 系统定位与运行环境

| 比较维度 | FreeRTOS | PREEMPT_RT |
|---|---|---|
| 系统形态 | 轻量级 RTOS 内核，与应用及所需组件共同构成固件 | Linux 内核的实时抢占模型，运行完整用户空间 |
| 典型平台 | 资源受限的 MCU 和小型 MPU | 具有较完整内存、存储和外设资源的 x86、ARM64 等平台 |
| 应用组织 | 以任务、ISR、队列和信号量为主要抽象 | 以进程、线程、系统调用和内核驱动为主要抽象 |
| 内存与故障隔离 | 通常共享地址空间；部分端口可利用 MPU 隔离任务 | 通常依靠 MMU、虚拟内存和进程地址空间实现隔离 |
| 软件生态 | 组件精简，外设支持与芯片和 BSP 结合紧密 | 具有完整的驱动、网络、文件系统、容器和应用生态 |
| 启动和资源占用 | 启动路径短，内存和存储需求较低 | 启动链较长，资源占用和后台活动较多 |
| 系统更新方式 | 通常以固件整体构建和升级为主 | 支持软件包、服务和应用的独立部署与更新 |

FreeRTOS 内核采用 MIT 许可证，并可按需组合网络、文件系统等库。其硬件适配范围很广，但具体功能仍取决于处理器端口、BSP 和配置。FreeRTOS 也提供 SMP 内核，因此不能将其绝对描述为“仅支持单核”；不过，多核能力仍需目标端口和硬件共同支持。[FreeRTOS支持设备](https://www.freertos.org/Documentation/02-Kernel/03-Supported-devices/00-Supported-devices) [FreeRTOS SMP说明](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/13-Symmetric-multiprocessing-introduction)

### 7.2 调度与实时机制

| 比较维度 | FreeRTOS | PREEMPT_RT |
|---|---|---|
| 基本调度方式 | 以固定优先级任务调度为核心，可配置为抢占式或协作式 | 使用 Linux 调度框架，实时线程通常采用 `SCHED_FIFO`、`SCHED_RR` 或 `SCHED_DEADLINE` |
| 优先级规则 | 数值越大优先级越高，最高优先级的就绪任务获得处理器 | `SCHED_FIFO/RR` 的实时优先级为 1～99，数值越大优先级越高 |
| 同优先级任务 | 可配置时间片轮转，也可由任务主动阻塞或让出处理器 | `SCHED_RR` 使用时间片，`SCHED_FIFO` 持续运行至阻塞、让出或被更高优先级任务抢占 |
| 中断处理 | ISR 通常直接运行，并通过内核接口唤醒任务；关键区和中断屏蔽时间决定重要延迟上界 | 大多数 IRQ 被线程化，高优先级实时线程能够抢占 IRQ 线程；少数硬中断路径仍不可抢占 |
| 同步通信 | 队列、任务通知、信号量和互斥量等轻量机制 | futex、实时互斥锁、信号量、事件及进程间通信机制 |
| 优先级反转控制 | 互斥量可提供优先级继承，但仍需控制临界区和锁嵌套 | 内核 `rt_mutex` 和用户态优先级继承互斥锁用于缩短无界优先级反转 |

在典型的抢占式配置下，FreeRTOS 调度器优先运行最高优先级的就绪任务，同优先级任务是否轮转由配置决定。因此，其主要延迟来源通常是更高优先级任务执行时间、ISR、临界区、中断屏蔽以及共享资源阻塞。[FreeRTOS任务优先级说明](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/01-Tasks-and-co-routines/03-Task-priorities) PREEMPT_RT 面对的内核路径和系统活动更多，主要通过扩大内核可抢占范围、中断线程化和优先级继承等机制，将原本不可控的长阻塞转换为可调度、可设优先级的执行实体。

### 7.3 实时性与工程能力

| 比较维度 | FreeRTOS | PREEMPT_RT |
|---|---|---|
| 实时确定性 | 系统规模较小，执行路径较短，通常更容易获得紧凑的最坏响应时间 | 干扰源较多，但经过内核、CPU、IRQ 和应用联合配置后可显著压缩长尾延迟 |
| 最坏时间分析 | 在任务和中断集合固定时较容易开展响应时间及最坏执行时间分析 | 内核、驱动和硬件状态复杂，通常更依赖长时间压力测试与端到端测量 |
| 复杂功能承载 | 适合功能固定、资源有限的控制和采集节点 | 适合复杂算法、网络通信、存储、图形界面和多进程服务 |
| 安全隔离 | 普通端口中的任务故障可能影响整个固件；部分 ARM 端口可借助 MPU 加强隔离 | 用户进程具有较成熟的地址空间和权限隔离，但内核或驱动故障仍可能影响全系统 |
| 多核与资源管理 | 支持情况取决于端口；系统服务和资源管理相对精简 | Linux SMP、NUMA、cgroup 和多用户资源管理能力较成熟 |
| 调试与维护 | 易于观察固定任务行为，但问题常与芯片、ISR 和 BSP 紧密相关 | 工具链丰富，但内核、驱动和用户态之间的跨层定位更复杂 |

FreeRTOS 一般更容易实现较小且稳定的响应时间，但不能据此直接断言“FreeRTOS 必然是硬实时，PREEMPT_RT 只能是软实时”。硬实时的判据是所有关键任务在规定条件下均能满足截止时间，这需要结合任务最坏执行时间、阻塞时间、中断响应、硬件行为和故障模式进行验证。类似地，标准 FreeRTOS 与具有功能安全认证证据的 SAFERTOS 不是同一产品，采用 FreeRTOS 或 PREEMPT_RT 本身都不等于系统已经获得安全认证。[FreeRTOS与SAFERTOS许可说明](https://freertos.org/Documentation/02-Kernel/01-About-the-FreeRTOS-kernel/04-Licensing)

### 7.4 选型原则与协同方式

适合优先选择 FreeRTOS 的条件包括：硬件资源有限，任务集合和功能边界稳定；系统需要快速启动；关键控制周期短，且希望对任务、ISR 和共享资源进行细粒度的静态分析；系统不依赖完整 Linux 驱动和用户态软件生态。

适合优先选择 PREEMPT_RT 的条件包括：系统需要复杂网络、文件系统、设备驱动、图形界面或多进程应用；算法和服务需要动态部署；目标截止时间能够通过几十微秒至毫秒量级的实测延迟预算满足；项目更重视 Linux 兼容性、开发效率和长期软件维护。

当系统同时包含严格控制任务和复杂上层功能时，也可以采用分层协同架构：由运行 FreeRTOS 的微控制器承担时间边界明确的底层控制、快速保护和接口时序，由运行 PREEMPT_RT 的应用处理器承担复杂算法、通信、存储和管理。两侧应通过有界队列、共享内存或确定性总线交换带时间戳的数据，并明确通信超时、降级状态和故障隔离策略。

因此，二者的选型不应只比较平均延迟或操作系统名称，而应根据截止时间预算、可接受的最大延迟、硬件资源、隔离要求、软件生态和维护成本综合决定。若目标功能能够在 FreeRTOS 上以较小的软件规模完成，FreeRTOS 通常更利于时序分析；若完整 Linux 能力不可替代，PREEMPT_RT 则是在实时性与通用计算能力之间更合适的折中。

## 8 PREEMPT_RT的部署与应用层使用

前文说明了 PREEMPT_RT 的作用机理和配套优化原则，本节进一步给出从内核安装、系统配置到应用运行和结果验收的实施方法。需要强调的是，安装实时内核只改变了内核的抢占和调度能力；应用只有显式采用实时调度、内存锁定、CPU 亲和性及确定性的周期控制，并经过目标负载下的测试，才能实际利用 PREEMPT_RT。

### 8.1 基于Ubuntu Pro的官方内核部署

在 Ubuntu 22.04 LTS 和 24.04 LTS 中，Canonical 提供的 Real-time Ubuntu 通过 Ubuntu Pro 软件源交付。通用 x86-64 或 ARM64 平台可执行：

```bash
sudo apt update
sudo apt install ubuntu-pro-client
sudo pro attach
sudo pro enable realtime-kernel
sudo reboot
```

其中，`pro attach` 用于关联 Ubuntu Pro 订阅；已经关联的系统可跳过该步骤。不同平台应选择对应的内核变体，不能将通用内核直接用于 Raspberry Pi：

| 目标平台 | 启用方式 |
|---|---|
| 通用 x86-64/ARM64 | `sudo pro enable realtime-kernel` |
| Raspberry Pi 4/5 | `sudo pro enable realtime-kernel --variant=raspi` |
| 第 12 代 Intel Core IOTG | `sudo pro enable realtime-kernel --variant=intel-iotg` |

对于需要支持较新硬件的通用平台，在启用实时内核软件源后还可以安装 HWE 实时内核：

```bash
# Ubuntu 22.04 LTS
sudo apt install linux-realtime-hwe-22.04

# Ubuntu 24.04 LTS
sudo apt install linux-realtime-hwe-24.04
```

截至本文资料核对时，Ubuntu 官方文档给出的上述 HWE 分支分别基于 6.8 和 6.17 内核；具体版本会随维护周期更新，实际部署时应以[Ubuntu实时内核启用说明](https://documentation.ubuntu.com/pro-client/en/docs/howtoguides/enable_realtime_kernel.html)和[实时内核支持版本](https://documentation.ubuntu.com/real-time/latest/reference/releases/)为准。实时内核与 Canonical Livepatch 不兼容，启用过程中 `pro` 会提示停用 Livepatch；生产系统还应保留可启动的通用内核，便于驱动不兼容或实时内核异常时回退。

重启后可从内核名称和配置项确认实际运行的内核，而不能只根据软件包是否安装进行判断：

```bash
uname -a
grep '^CONFIG_PREEMPT_RT=' /boot/config-$(uname -r)
```

正常情况下，`uname -a` 应包含 `realtime` 和 `PREEMPT_RT`，内核配置应显示 `CONFIG_PREEMPT_RT=y`。若系统仍显示 `generic`，说明当前并未启动到实时内核。

### 8.2 基于源码和RT-patch的自主编译部署

Ubuntu Pro 不是使用 PREEMPT_RT 的必要条件。另一种方式是从 kernel.org 下载 Linux 内核源码，自行启用 `CONFIG_PREEMPT_RT`，并将编译结果打包为 Ubuntu 可安装的 `.deb` 文件。该方式不需要订阅，且便于固定实验内核版本，但内核配置、签名、驱动兼容、安全更新和故障回退均由使用者负责。

自主编译需要区分内核版本：Linux 6.12 以前通常需要将严格匹配的 RT-patch 应用于对应源码；从 Linux 6.12 开始，官方主线内核已支持 PREEMPT_RT，在体系结构满足 `ARCH_SUPPORTS_RT` 时可以直接选择 `CONFIG_PREEMPT_RT=y`。6.12 及以后发布的 `-rtN` 补丁主要继续提供尚未进入主线的修复和优化，可根据稳定性、维护周期和实验复现要求决定是否采用。[Linux Foundation PREEMPT_RT版本说明](https://wiki.linuxfoundation.org/realtime/preempt_rt_versions)

以下以截图所示的 Linux 6.8.2 与 `patch-6.8.2-rt11` 为例说明流程。该补丁确实由 kernel.org 发布，但属于 2024 年的历史版本，适合复现实验过程，不宜在生产系统中作为长期维护版本。实际部署应从 [kernel.org RT补丁目录](https://www.kernel.org/pub/linux/kernel/projects/rt/)选择仍在维护且与内核源码版本完全一致的补丁，并校验下载文件的哈希值和签名。

首先安装构建依赖，并下载相互匹配的源码和补丁：

```bash
sudo apt update
sudo apt install build-essential bc bison flex libssl-dev libelf-dev \
  libncurses-dev dwarves fakeroot rsync

wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.8.2.tar.xz
wget https://cdn.kernel.org/pub/linux/kernel/projects/rt/6.8/patch-6.8.2-rt11.patch.xz
tar -xf linux-6.8.2.tar.xz
cd linux-6.8.2
xzcat ../patch-6.8.2-rt11.patch.xz | patch -p1
```

补丁应当能够无冲突地应用；如果出现大量失败块，通常说明源码与 RT-patch 版本不匹配，不应继续编译。然后以当前 Ubuntu 内核配置为基础补齐新配置项：

```bash
cp /boot/config-$(uname -r) .config
make olddefconfig

# 清除原配置中指向Ubuntu专用证书文件的路径
scripts/config --set-str SYSTEM_TRUSTED_KEYS ""
scripts/config --set-str SYSTEM_REVOCATION_KEYS ""

make menuconfig
```

在 `menuconfig` 中选择：

```text
General setup
  → Preemption Model
    → Fully Preemptible Kernel (Real-Time)
```

保存后应确认 `.config` 中存在：

```text
CONFIG_PREEMPT_RT=y
```

截图中通过设置 `CONFIG_SYSTEM_REVOCATION_LIST=n` 或创建空的 `debian/canonical-revoked-certs.pem` 来绕过证书报错。该错误并非 PREEMPT_RT 引起，而是 Ubuntu 的内核配置引用了原生 kernel.org 源码中不存在的 Canonical 证书文件。与创建伪空文件相比，显式清除证书文件路径更容易说明配置含义；但自编译内核仍不具有 Canonical 的官方签名。启用 Secure Boot 的系统需要使用自有密钥签名内核和模块，否则可能无法启动；直接关闭证书校验或 Secure Boot 会降低系统安全性。

配置完成后，应以普通用户编译二进制软件包，不需要执行截图中的 `sudo make`：

```bash
make -j$(nproc) bindeb-pkg LOCALVERSION=-custom-rt
```

Linux 内核官方文档推荐使用 `bindeb-pkg` 生成二进制 Debian 软件包；`git init` 和提交整个源码树并不是启用 PREEMPT_RT 的必要步骤，只会影响构建版本字符串等元数据。[Linux内核编译与打包说明](https://docs.kernel.org/admin-guide/quickly-build-trimmed-linux.html)

编译结果通常生成在源码目录的上一级。安装前应检查包名和架构，再安装内核镜像及头文件：

```bash
cd ..
ls -1 linux-image-*.deb linux-headers-*.deb
sudo apt install ./linux-image-6.8.2-custom-rt*.deb \
  ./linux-headers-6.8.2-custom-rt*.deb
sudo update-grub
sudo reboot
```

各类构建产物的用途如下：

| 软件包 | 用途 | 是否通常需要安装 |
|---|---|---|
| `linux-image-*` | 实时内核和内核模块 | 是 |
| `linux-headers-*` | 编译 DKMS 或其他外部模块 | 建议安装 |
| `linux-libc-dev_*` | 用户态使用的内核接口头文件 | 通常不需要替换系统版本 |
| `linux-image-*-dbg_*` | 内核调试符号 | 仅故障分析时需要 |

重启后的验证方法与第 8.1 节相同。自主编译还应检查外部驱动和 DKMS 模块是否成功重建，并在 GRUB 中保留原有 Ubuntu 通用内核作为回退项。来源不明的预编译 `linux-image` 拥有系统最高执行权限，因此不应仅凭共享链接直接安装；用于论文实验时，宜记录内核和补丁下载地址、哈希值、`.config`、编译器版本及最终软件包校验值，以保证实验可追溯和可复现。

两条部署路线的区别可以概括为：

| 路线 | 订阅要求 | 主要优势 | 主要责任 |
|---|---|---|---|
| Ubuntu Pro官方内核 | Ubuntu 22.04/24.04需关联订阅，个人最多5台物理机免费 | 安装简单，具有官方打包、签名和维护 | 按官方内核周期升级和验证 |
| kernel.org主线源码或源码加RT-patch | 不需要 | 可固定版本并自定义配置 | 自行承担编译、签名、驱动兼容和安全维护 |

### 8.3 系统资源配置

部署后的系统配置应围绕“为实时线程建立低干扰执行环境”展开。第 4 节已经说明各项优化原理，实际配置时可按下表形成对应关系：

| 配置目标 | 实施方法 | 核验方式 |
|---|---|---|
| 减少普通任务竞争 | 使用 CPU 亲和性、cpuset 或 `isolcpus` 为实时任务保留核心 | `taskset -pc <PID>`、`ps -eLo psr,pid,tid,comm` |
| 减少中断干扰 | 将非关键 IRQ 迁移至 housekeeping CPU，必要时设置 `irqaffinity` | 观察 `/proc/interrupts` 各 CPU 计数 |
| 减少调度时钟和 RCU 干扰 | 视测试结果使用 `nohz_full` 和 `rcu_nocbs` | 检查 `/proc/cmdline` 和延迟分布 |
| 避免换页抖动 | 锁定内存，提前分配并访问缓冲区和线程栈 | 检查 `mlockall()` 返回值和运行期缺页 |
| 降低功耗管理抖动 | 根据测试调整调频、C-state、Turbo 和 SMT | 对比调整前后的 P99.9 与最大延迟 |

一种常见划分方式是保留至少一个 housekeeping CPU 处理系统服务、内核线程和普通 IRQ，再将实时线程绑定到其他核心。Ubuntu 官方文档给出的典型启动参数形式为：

```text
isolcpus=<RT-CPU列表> nohz_full=<RT-CPU列表> rcu_nocbs=<RT-CPU列表> irqaffinity=<housekeeping-CPU列表>
```

CPU 编号必须根据处理器拓扑、设备 IRQ 和工作负载确定，不能直接复制固定示例。某些设备的 IRQ 需要与处理线程位于同一 NUMA 节点，过度隔离反而可能增加数据搬运和缓存开销。因此，应先测量基线，再逐项应用 CPU 隔离、[IRQ亲和性](https://documentation.ubuntu.com/real-time/latest/how-to/tune-irq-affinity/)和[RCU/调度时钟配置](https://documentation.ubuntu.com/real-time/latest/how-to/cpu-boot-configs/)，每次修改后重新测试。

手动设置 IRQ 亲和性后，还应检查 `irqbalance` 是否会重新分配中断：

```bash
systemctl status irqbalance
```

可以停用该服务，也可以配置其避开实时 CPU。若选择停用，必须确保所有 IRQ 已被合理分配并能够在非实时核心上得到处理，不能简单关闭服务后忽略中断布局。[Ubuntu IRQ亲和性配置](https://documentation.ubuntu.com/real-time/latest/how-to/tune-irq-affinity/)

### 8.4 应用层实时化

普通应用即使运行在 PREEMPT_RT 内核上，默认仍采用 `SCHED_OTHER`。关键线程通常需要完成以下设置：

1. 使用 `SCHED_FIFO` 或 `SCHED_RR`，并按照任务重要性统一规划优先级；
2. 将关键线程绑定到预留 CPU，避免运行过程中跨核迁移；
3. 使用 `mlockall(MCL_CURRENT | MCL_FUTURE)` 锁定内存，并在进入实时循环前完成内存、缓冲区和栈的预分配与预访问；
4. 周期任务使用 `CLOCK_MONOTONIC` 和绝对时间唤醒，避免相对休眠产生累计漂移；
5. 实时循环中避免动态内存分配、同步文件写入、阻塞式日志输出和不受控的设备访问；共享锁应尽量缩短临界区，必要时采用优先级继承互斥锁。

下面给出一个简化的 C 语言实时线程框架。示例中的 CPU 2、优先级 80 和 1 ms 周期仅用于说明接口，实际值必须由任务周期、线程依赖和 IRQ 布局确定：

```c
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>

static void fail_pthread(const char *operation, int error)
{
    errno = error;
    perror(operation);
    exit(EXIT_FAILURE);
}

static void prefault_stack(void)
{
    volatile unsigned char stack[64 * 1024];
    for (size_t i = 0; i < sizeof(stack); i += 4096)
        stack[i] = 0;
}

static void add_1ms(struct timespec *t)
{
    t->tv_nsec += 1000000L;
    if (t->tv_nsec >= 1000000000L) {
        t->tv_nsec -= 1000000000L;
        ++t->tv_sec;
    }
}

int main(void)
{
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        perror("mlockall");
        return EXIT_FAILURE;
    }
    prefault_stack();

    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(2, &cpus);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpus), &cpus);
    if (rc != 0)
        fail_pthread("pthread_setaffinity_np", rc);

    struct sched_param p = { .sched_priority = 80 };
    rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &p);
    if (rc != 0)
        fail_pthread("pthread_setschedparam", rc);

    struct timespec next;
    if (clock_gettime(CLOCK_MONOTONIC, &next) == -1) {
        perror("clock_gettime");
        return EXIT_FAILURE;
    }

    for (;;) {
        add_1ms(&next);
        do {
            rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
                                 &next, NULL);
        } while (rc == EINTR);
        if (rc != 0)
            fail_pthread("clock_nanosleep", rc);

        /* 在此执行有界、无阻塞的实时任务 */
    }
}
```

Linux 的 `SCHED_FIFO` 优先级范围为 1～99，数值越大优先级越高，但不应简单地将所有线程设为 99；优先级设计应为关键 IRQ 和故障处理保留空间，同时确保实时线程会周期性阻塞或休眠，否则可能使普通进程长期得不到调度。[Ubuntu实时调度教程](https://documentation.ubuntu.com/real-time/latest/tutorial/first-rt-app/1-schedulers/) 内存锁定可避免换页造成的长延迟，但仍需为每个实时线程预先触碰栈和缓冲区，并配置足够的 `RLIMIT_MEMLOCK`。[Ubuntu mlockall手册](https://manpages.ubuntu.com/manpages/jammy/man2/mlock.2.html)

工程应用还应在每次任务执行结束后读取 `CLOCK_MONOTONIC`，将完成时刻与下一周期截止时间比较并累计违约次数。计数过程应保持无阻塞，统计结果由非实时监控线程异步输出，避免在实时循环内直接写日志。

### 8.5 服务化运行与验收

生产环境通常由 `systemd` 启动应用。假设已经创建低权限服务用户 `rtapp`，可在服务单元中设置 CPU 亲和性和资源上限，应用内部再为各线程设置不同的调度策略和优先级：

```ini
[Service]
User=rtapp
ExecStart=/opt/rt/rt_app
CPUAffinity=2
LimitRTPRIO=80
LimitMEMLOCK=infinity
Restart=on-failure
```

这种方式只授予应用所需的实时优先级和内存锁定额度，避免长期以 root 身份运行。启动后可检查线程实际使用的调度类别、实时优先级和 CPU：

```bash
ps -eLo pid,tid,cls,rtprio,psr,comm | grep rt_app
```

部署验收应形成三个层次：首先确认实时内核和启动参数生效；其次使用 `cyclictest` 在空闲及目标压力负载下测量内核调度延迟；最后测量应用从输入、唤醒、计算到输出的端到端延迟和截止时间违约次数。例如可先进行 10 min 的部署检查：

```bash
sudo apt install rt-tests
sudo cyclictest --mlockall --smp --priority=80 \
  --interval=200 --distance=0 --duration=10m
```

该命令只能作为安装和配置的初步检查，正式验收仍应按第 5.2 节进行长期压力测试和重复实验。[Ubuntu最大延迟测量方法](https://documentation.ubuntu.com/real-time/latest/how-to/measure-maximum-latency/) 只有当应用端到端的 P99.9、最大延迟和截止时间违约率均满足设计预算时，才能认为 PREEMPT_RT 的实时能力已经在应用中有效落地。

## 9 应用领域与适用边界

PREEMPT_RT 适用于截止时间通常处于几十微秒至数十毫秒范围、同时需要完整 Linux 软件生态的系统。

典型应用包括：

| 应用领域 | 具体场景 |
|---|---|
| 工业自动化 | PLC、CNC、机械臂、运动控制、工业以太网 |
| 专业音频 | 数字调音台、多轨录音、低缓冲音频处理 |
| 通信系统 | 5G vRAN、Open RAN、TSN、软件无线电 |
| 机器人 | 移动机器人、传感器同步、控制与规划协调 |
| 汽车电子 | HMI、车载网关、实时通信、部分 ADAS 处理 |
| 医疗与科研 | 医疗机器人、实验仪器、硬件在环和同步测量 |
| 金融系统 | 低延迟行情处理、订单处理和实时消息服务 |
| 实时虚拟化 | 工业虚拟机、通信网络功能和混合关键系统 |

PREEMPT_RT 不适合单独承担以下任务：

- 亚微秒级闭环控制；
- 高频精确波形生成；
- 功率电子的底层控制；
- 要求形式化证明最坏执行时间的系统；
- 每次超时均可能引发严重安全后果的最终保护链；
- 对吞吐量的重视程度显著高于尾部延迟的服务器。

对于严格硬实时或安全关键系统，更合理的方式通常是采用异构架构：

```text
MCU、DSP 或 RTOS
负责严格硬实时和安全保护

Linux PREEMPT_RT
负责复杂算法、通信、数据管理和人机交互
```

## 10 本章小结

PREEMPT_RT 是 Linux 内核的实时抢占技术体系，RT-patch 是其补丁交付形式。自 Linux 6.12 起，其核心功能进入主线内核，但仍需通过 `CONFIG_PREEMPT_RT` 显式启用。

PREEMPT_RT 通过扩大内核可抢占范围、中断线程化、可睡眠锁、优先级继承以及 SoftIRQ、定时器和 RCU 处理方式改造，降低高优先级任务受到内核路径阻塞的时间。其主要收益表现为最大调度延迟和尾部延迟下降，而不是应用计算速度提高。

在经过合理配置的物理平台上，PREEMPT_RT 通常能够将普通 Linux 在压力条件下出现的毫秒级长尾延迟降低至几十或数百微秒，但具体结果受处理器、固件、设备驱动、共享资源竞争及系统配置影响。与此同时，PREEMPT_RT 可能增加上下文切换和锁同步成本，导致一定吞吐量损失。

FreeRTOS 与 PREEMPT_RT 的主要差异不在于是否具备实时调度，而在于系统规模和工程目标：FreeRTOS 更适合资源受限、任务边界固定且需要紧凑时序分析的设备，PREEMPT_RT 更适合依赖完整 Linux 驱动和应用生态的复杂系统。二者均不能仅凭操作系统名称判定为硬实时，最终仍需以最坏响应时间和截止时间验证为依据。

从完整系统角度看，实时内核只是保障链中的一层。硬件和固件需要提供可控的中断与存储访问时间，框架和中间件需要避免在关键路径引入无界排队及阻塞，应用代码需要具有可分析的最坏执行时间，开发流程还需要通过设计审查、代码检查、压力测试和运行期监测持续验证这些约束。

因此，PREEMPT_RT 适合需要较高实时确定性且依赖 Linux 软件生态的工业控制、通信、机器人、专业音频和实时服务系统；对于亚微秒控制、形式化硬实时或安全关键保护任务，则仍需结合 RTOS、MCU 或 DSP 等专用实时处理方案。

在工程部署中，实时内核只是基础条件。Ubuntu 22.04/24.04 既可以使用 Ubuntu Pro 提供的官方实时内核，也可以根据版本从 kernel.org 主线源码或源码加 RT-patch 自主编译；后者无需订阅，但必须自行承担内核签名、驱动兼容和安全维护。完成内核部署后，还需要检查实时调度带宽限制，进行 CPU 与 IRQ 资源划分，并在应用中显式配置实时调度、CPU 亲和性、内存锁定和绝对周期定时。最终是否达到实时要求，应以目标负载下的调度延迟和端到端截止时间测试结果为准。
