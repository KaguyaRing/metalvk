# 宿主内存域：RM 平台服务前置模块

这是普通 wired **CPU 宿主内存**，不是 DMA、物理连续内存、GPU VA 或显存。没有定义 NVIDIA `os_alloc_mem`／`os_free_mem` 全局回调，没有启动 RM／GSP，没有用户态分配入口。

## 分层

- `include/memory`／`src/memory`：无 SDK 的配额、页粒度计费与 OPEN → DRAINING → CLOSED 状态，进入 `libmetalvk.a`。
- `kexts/Platform/.../memory.hpp`／`memory.cpp`：原生 `MetalVKMemoryDomain` 与 `MetalVKWiredBuffer`，使用 IOLock、OSObject 所有权和 `IOBufferMemoryDescriptor`，进入共享内核库并由 kext 链接。

纯 C 配额不是任意指针注册表：所有者必须用同一锁保护验证与记账，每次预留只有一次对应归还。初始化仅可用于未发布／已关闭且静止的存储，不能重置存活配额。原生域将账本保持为私有状态；每个缓冲区记录自己的域与 charge，由 OSObject 最后一次 release 归还，不接受用户提交的裸指针。

## 策略与计费

NULL limits 采用默认：单分配最多 128 MiB、域总计最多 512 MiB、最多 1024 个预留／缓冲区。`copyBudget` 报告具体限制与 `default_limits`。显式策略不能超过这些硬上限，非法策略返回 BadArgument，不静默替换。

预算按 PAGE_SIZE 向上取整后的 backing bytes 计费，包含尚未发布的分配和正在释放的 backing；不是 payload 精确长度。IOBuffer／OSObject 元数据不计入字节数，由对象数量上限另行限制。拒绝零长度与整数溢出。

## 所有权与关闭

1. `MetalVKMemoryDomain::create(limits, &domain)` 返回一个调用者持有的引用，失败时清空输出并返回具体 IOReturn。
2. `allocate(bytes, &buffer)` 在锁内预留配额与域引用，锁外分配、清零 backing。
3. 发布前重新在同一锁内检查域仍 OPEN。若关闭已先发生，释放 backing 并归还配额，返回 Offline；不发布迟到的缓冲区。
4. 成功返回一个缓冲区引用。`bytes()` 是借用的 CPU 地址，长度为 `length()`；所有访问者须持有引用，并自行同步 payload 读写。
5. 缓冲区最终 release 先释放 backing，再归还配额及域引用，全部 OSObject 释放不在域锁内执行。
6. `close()` 拒绝新预留；有已存在缓冲区／待发布分配时返回 Busy，调用者保留域引用，待使用者静止后重试。关闭不强制回收仍使用的内存。

每个预留保留父域，父域不保留子缓冲区，避免引用环；最终域释放前不再有分配／释放线程使用域锁。内部记账异常会报告错误并将域置为 DRAINING，保留引用而非继续分配或释放不确定的父状态。

## 线程限制

create／allocate／close／copyBudget 检查主中断、抢占关闭与中断关闭状态。所有调用以及缓冲区／域的最终 release 均须在可睡眠线程上下文，不能从 spinlock 或 workloop gated 回调调用；gated 状态不能仅由上述三项检测推断，调用者仍须遵守约定。

OSObject 的最后 release 回调无法返回“请稍后释放”，所以中断路径必须在进入该路径前持有引用，并将最后的释放移交线程上下文。后续已实现[有界延后释放队列](kernel-runtime.md)，成功投递转移一个已有引用，失败必须保留并在线程重试；尚未实现 IRQ 内存池。不能据此将线程分配器当作 RM 的完整原子分配服务。

## 依据与验证范围

- NVIDIA 官方 `open-gpu-kernel-modules` 的 `kernel-open/common/inc/os-interface.h` 声明了 RM 内存、锁、工作队列与时间服务；本轮只落实宿主内存前置模块，不宣称已完成其整个 OS 适配。
- Apple／固定 MacKernelSDK 的 `IOBufferMemoryDescriptor.h` 明确 kernel 默认 wired、可能阻塞、不鼓励强求物理连续。本轮没有复制参考驱动代码，也没有导入 archived KaguyaNV 的实现。
- 纯配额已有 Debug／Release／Alpha 测试、20,000 次 ticket 模型操作、四线程 reservation／close／待发布 rollback 检查；线程测试只验证所有者锁标准，不执行 XNU 分配器。
- 原生库／kext 编译、链接与符号检查已有 [CI 证据](https://github.com/KaguyaRing/metalvk/actions/runs/38112082517)。不是加载或真实 wired 分配回收的运行证据。

## 安全复核与下一步

资产：wired 内存、预算与对象／锁生命周期。信任范围：内核调用者提供的大小与策略到原生 backing；当前没有新用户态特权入口。审查覆盖尺寸取整、预算／计数溢出、关闭与发布竞争、失败归还、子对象保留父对象、锁外释放与禁止 IRQ 阻塞。

不映射 MMIO，不取得或导出 DMA 地址，不启用 bus mastering。配额不是认证／权限系统；若以后允许用户态分配，必须另加每客户端所有权、权限、配额、句柄代次与退出回收，不能直接暴露这里的内核对象。

仍需隔离的 macOS 真机执行：真实 IOBuffer OOM、初始化失败、并发分配／关闭、最后 release 线程位置与卸载。确定性 host 模型不能代替这些原生故障注入。工作队列、uptime 与有界延后释放已实现，但尚需 RM 锁／等待适配与 IRQ 内存池，再处理不可变官方固件快照与 RM 生命周期；DMA 管理保持独立，不把 CPU 地址复用成 GPU 总线地址。
