# Darwin KPI 可见性与语义映射

主来源XNU15/PCI15，XNU26/PCI26仅抽样比较。以下分类指研究可用性，不表示通过原生链接、KC或加载。

## 声明、实现、导出与限制

| Darwin候选 | 声明/实现/可见性证据 | 分类与运行契约 | 目标验证缺口 |
|---|---|---|---|
| IOPCIDevice、config/BAR | PCI15 header与handleOpen实现 [P01][P02]；PCI26 header [Q01] | PUBLIC_USABLE底座；Linux PCI语义SEMANTIC_ADAPTER_REQUIRED。open有owner/session/mapper副作用，retain不保证D0或仍连接 | header/源不替代目标SDK/IOPCIFamily C++导出与虚表 ABI核对；不seize现有owner |
| IOService PMinit/joinPMtree/registerPowerDriver | header [X03]；详细生命周期来源见现有研究 | SEMANTIC_ADAPTER_REQUIRED；PM ack必须与实际硬件电源操作和drain绑定 | Linux runtime PM usage/autosuspend/系统sleep不可只转发setPowerState |
| IOBufferMemoryDescriptor/IOMemoryDescriptor | declaration prepare/complete [X02]，mapping implementation [X11] | PUBLIC_USABLE底座；wired内存/任务mapping与ownership需A；prepare/complete成对 | physical backing不能等价Linux shmem/swap/HMM；精确cache/default行为需目标验证 |
| IODMACommand / IOMapper | declaration [X01]、implementation [X10]、common/x86_64 exports样本 [X14][X16] | PUBLIC_USABLE底座；DMA semantic adapter必需。device IOVM segments、mask/direction/cache、阻塞规则 | C++ imports完整allowlist、目标device mapper、bounce/分段、错误回滚；无实机证明 |
| IOInterruptEventSource / IOWorkLoop | header [X13]、checkForWork实现 [X15] | SEMANTIC_ADAPTER_REQUIRED：action在workloop context，不是Linux hardirq语义 | DG2 mask/ack时机、多vector并发、drain和排队事件必须逐项证实 |
| kernel_thread_start / assert_wait / thread_block | XNU15 Mach exports [X04]；26同类exports [Y01] | PUBLIC_USABLE底座；Linux kthread/current/waitqueue需A。等待登记与条件检查防丢唤醒，退出排空 | 导出不保证Linux TASK_*／interruptible语义；worker/timer cancellation设计未实现 |
| lck_mtx 等 | Libkern exports [X18]包含锁样本；XNU头/实现可读 | PUBLIC_USABLE底座；ww_mutex/RCU/spin_irq语义需A | 不能全用sleeping mutex或串行gate；RCU公开等价未建立 |
| copyin/copyout | Libkern exports [X05] | PUBLIC_USABLE底座；current-task用户指针拷贝，错误向上传递 | 异步worker不能默认借用用户mm；跨任务copy需持有任务与mapping寿命 |
| cdevsw_add / devfs clone | BSDKernel exports [X06] | PUBLIC_USABLE底座；per-open对象、fd lifetime/ioctl布局需A | Linux file_ops/private_data不等价XNU dev_t；主节点授权与连接配额 |
| get_task_map / pmap_find_phys / vm_deallocate | Unsupported.exports [X07][X17]；26样本仍有unsupported [Y02] | PRIVATE_OR_UNSUPPORTED；不能依靠“有导出”称稳定public KPI | 需要隔离模块、逐build allowlist和授权；优先公开descriptor方案，不依赖其满足MMU通知 |
| Linux mmu_interval_notifier / HMM | xe实际回调/序列 [L16][L17][L18]，XNU mapping能力 [X02][X11] | NOT_CURRENTLY_SUPPORTABLE：没有已证实公开kext语义等价 | 负结论是当前接口审查范围，不是证明XNU内部不可能；需CPU mapping invalidation→GPU撤销协议 |
| Linux dma-buf/sync_file fd共享 | driver/UAPI依赖 [L09][L27]；参考实现仅有限kernel object | UNVERIFIED完整fd bridge；当前受限profile拒绝外部功能 | 设计真实fd/IPC共享与安全性；拒绝功能必须同步用户态不广告 |

仅拿到公开XNU源码不能把内核内部函数链接进任意第三方kext。项目已有的旧符号匹配结果也不验证新LinuxKPI产物。`KERNEL_PRIVATE`宏只改变头文件视图，不授予调用权限；`com.apple.kpi.unsupported`依赖不是API稳定性的承诺。

## DMA需单独的owner对象

推荐研究模型：BO backing owner→descriptor准备→IODMACommand map→device segments→GPU job references→完成/错误drain→unmap/complete→释放backing。中间每层都有独立寿命。

禁止：CPU VA直接作GPU地址、identity IOMMU假设、map后马上release backing、在spinlock/gated action自动prepare、clear失败仍声称全清理。头文件直接注明autoprepare可阻塞、prepared期间不可重置。[X01]

## mmap与userptr是两个问题

XNU15 `bsd/kern/kern_mman.c` 对该VCHR/VSTR路径直接返回ENODEV。[X08] `cdevsw`头里存在`d_mmap`字段不代表普通DRM字符设备mmap已可用。

IOKit descriptor可映射进任务，但Linux DRM合同还包括offset授权、保护、cache、fault、迁移、revocation和close后的寿命。参考的fault预填充+TTM pin只是一种适配策略，[R04][R09] **不自动保持Linux ABI或TTM可迁移语义**。相应libdrm/ANV也必须改变；固定映射还触及Unsupported KPI清理。

userptr/HMM则监听应用CPU地址变更、seq重试与blockable invalidation，不能以“prepare锁页成功”替代。[L17][L18][L32] 两条路线都面对这一缺口。

## 所有权、PM与终止

IOService默认open与IOPCIDevice handleOpen是真实owner路径，[X12][P02] 但不是电源锁或热拔插屏障。IOUserClient::clientDied可通知连接关闭，[X09] 不替驱动等待GPU访问停止。

参考NMXGPU stop的有界轮询attach、PM ack转发等不能直接当生产保证。[R03] 本轮只指出机制边界，不对其完整实现作安全认证。metalvk后续仍须准入/在途/generation/资源管理器；详见 [现有生命周期研究](../../kext-lifecycle-research.md)。

## 对照26的范围

确认26样本仍有Mach thread入口、Unsupported VM符号和PCI state方法。[Y01][Y02][Q01] 未逐函数比较全部布局、导出、mapper和PM语义；**不宣称15的适配自动可运行26**。最终两个build各自生成imports、KC验证与实机证据。
