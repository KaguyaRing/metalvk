# 内核执行上下文：工作队列、延后释放与时钟

本模块是 RM 平台服务前置能力，不是 GPU 队列／命令 ABI。尚未绑定 NVIDIA OS 回调，也未由诊断设备自动创建或向 UserClient 暴露。

## 分层与限制

- `include/runtime`／`src/runtime`：SDK-free、无分配的 256 槽 FIFO、单消费者状态与纳秒 deadline 溢出检查，进入 `libmetalvk.a`。
- `kexts/Platform/.../work.hpp`／`work.cpp`：IOSimpleLock、IOInterruptState、公开 Mach KPI `thread_call_allocate_with_options`／`thread_call_enter`／`thread_call_free`。
- `context.hpp`／`context.cpp`：集中可睡眠必要条件检查；`clock_get_uptime` → `absolutetime_to_nanoseconds`。不是墙上时间，不保证跨系统睡眠连续计时；本轮没有定时投递或 busy-wait delay 服务。

FIFO 上限包括正在执行的一项。满队列返回 NoResources；停止接受后返回 Offline。裸回调及 context 只允许可信内核调用者使用，不能从 IPC 传入函数指针或将这些指针导出给用户态。

## 所有权与线程

create、close、最终 release 必须在可睡眠且不持 workloop gate 的线程；enqueue、copyStats 可从普通中断上下文使用，但不支持 NMI。调用者在调用期间必须持有队列引用。create 成功返回一个调用者引用，同时队列保留自身 anchor，直到成功 close；忘记 close 会保留资源，不能自动取消仍拥有上下文的任务。

enqueue 成功接受任务，调用者保证代码／context 在执行前一直有效；失败没有转移所有权。固定队列投递不分配内存，不调用睡眠锁。回调在线程上下文运行，回调线程不持有队列锁或控制锁；可重入 enqueue，但回调调用自己的 close 返回 Busy，避免等待自身。其他 closer 可持控制锁等待回调排空，worker 不获取该锁。回调必须最终返回，不能抛出异常，也不能销毁仍被其他任务引用的上下文。

`MetalVKWiredBuffer::deferRelease(queue)` **转移一个已拥有的缓冲区引用**，不隐式新增引用：成功后该引用由回调释放；失败时调用者仍持有它，必须保留并在线程上下文重试／释放，不能因投递失败在 IRQ 中执行最后 release。同一引用不能重复转移。回调可能在 enqueue 返回前执行，因此成功后不能再通过已转移引用访问对象。

## 调度与关闭

ONCE 选项串行化同一 thread_call，运行期间再投递会合并为一次后续调用；`thread_call_enter` 的返回值表示是否已经 pending，而不是成功与否。每次新调用在中断屏蔽的队列锁内建立 invocation 引用和计数，回调尾部归还；新回调只能在这些状态建立后拿到同一锁。

close 将同锁保护的 FIFO 切换为 DRAINING，拒绝新任务，保留并执行已接受任务。默认等待预算 5 秒，最大 60 秒；0 表示只尝试一次。竞争中的另一个 closer 返回 Busy，不在控制锁上无限阻塞。工作未排空、回调挂起或 thread_call 仍有后续调用时，超时返回 Timeout，保留 call、anchor 与上下文，调用者保留引用并重试。1 ms 轮询不提供实时调度期限保证，系统调度可使实际返回晚于预算。

任务、invocation 计数清空后才尝试 thread_call_free；FALSE 表示仍不能释放，并非可以忽略。公开 KPI 支持当前回调尚在调度器尾部时延后释放 call；队列 invocation 引用保护 callback 对对象的最后访问。成功 close 表示已接受的任务执行完成、call 所有者释放完成，**不是第三方回调代码／kext 的全局卸载屏障**。在驱动 stop／卸载之前，仍必须停止 IRQ／其他生产者并建立模块级调用静止协议；本轮不宣称实现了可安全加载／卸载的完整驱动生命周期。没有导入私有 `thread_call_cancel_wait` 来掩盖此缺口。

## 安全复核与验证边界

资产：任务 context、wired backing、callback 生命周期、spinlock 与 thread_call。入口：可信内核分配、任务投递、关闭、延后引用释放。审查覆盖 FIFO 上界、active 计费、deadline 溢出、拒绝后所有权、关闭／投递原子性、回调锁外执行、尾部引用和超时不回收在途任务。内部异常通过 Error／copyStats.fault 暴露，并停止接受新任务；错误上下文不会被强制丢弃。

宿主测试覆盖满／空、FIFO、环形索引、多轮排空、active 期间投递、关闭拒绝、损坏状态和时间上界；四生产者提交 12,000 次，单消费者验证只执行一次，并让回调重入所有者锁。ASan／UBSan／TSan 在 CI 中运行的是宿主锁契约，不是 IOSimpleLock 或真实 thread_call。

本轮 [core／sanitizer CI](https://github.com/KaguyaRing/metalvk/actions/runs/38113990515)、[macOS 15/26 原生编译及静态分析](https://github.com/KaguyaRing/metalvk/actions/runs/38113990548)、[Vulkan 编译回归](https://github.com/KaguyaRing/metalvk/actions/runs/38113990400) 已通过。原生任务没有加载 kext，Apple 虚拟 Metal 全图测试不是 metalvk 执行证据；本机 RTX 4060 的 Windows Vulkan 计算／全图回归仍为零验证错误和警告。

仍需隔离 macOS 真机验证：create 分配失败、真实 IRQ burst／满队列后的释放保留、ONCE 重调度、最后 release 的线程位置、callback 挂起后的 close 超时及恢复、生产者停机与模块卸载。没有新的用户态入口，不执行生产环境攻击测试。未完成 IRQ 可用内存池、定时工作、RM 锁／等待 ABI 适配，不能把当前线程内存服务称为完整 RM 原子分配器。
