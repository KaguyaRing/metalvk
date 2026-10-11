# NVIDIA 后端：硬件入口与实施边界

## 首批实现

`backends/nvidia/` 现有目录开始承担 NVIDIA 硬件入口，不新建仓库，不导入归档的 KaguyaNV 或 nvidia-macos-driver 实现。

- `include/nvidia/boot.h`、`src/boot.c`：独立的固定 PCI 快照校验、NVIDIA boot register 解码；宿主产物 `libmetalvk-nvidia.a`，不进入厂商中立 `libmetalvk.a` 或公共 `metalvk.h`。
- `kext/Probe.cpp`：在 IOService start 阶段取得 provider open，取得 BAR0 的 OS 描述符，映射**首个 4096 字节**，只读且禁止缓存，读取 `PMC_BOOT_0`（0）、`PMC_BOOT_1`（4）、`PMC_BOOT_42`（0xa00）。映射与 open 都在返回前释放，不保留裸 MMIO 指针。
- `kext/Driver.cpp`：按明确选项接入上述真实读取路径，成功后在 registry 发布由寄存器解码的 architecture／implementation／revision 以及**所需** RM／固件名称。字段不是启动成功或固件认证结果。
- 原生模块独立归档为 `libmetalvk-nvidia-kernel.a`，与公共平台库一起链接诊断 kext；仍无 GPU 执行能力。

首台适配硬件仍限定 `10DE:28E0`、AD107，不能靠增加 PCI match 宣称支持其他 NVIDIA 卡。只接受 boot registers 自洽的 Ada architecture 0x19／implementation 7，不从 PCI device 名称伪造芯片寄存器值，也不猜测其他架构的固件。

## 默认关闭与失败策略

kext personality 的 `MetalVKNvidiaReadOnlyProbe` 默认 Boolean false；缺失也取 false，错误类型取 false 并报告 `disabled-invalid-option`。关闭时维持既有只读 PCI 诊断行为，registry phase 为 `diagnostics-only`。

只有显式 true 才执行 BAR0 读取。该开发选项须由隔离测试环境的管理员在构建配置中选择并重新签名；不提供用户态切换、任意寄存器 read/write 或自动安装／加载操作，本轮不修改用户启动介质。

开启后 probe 失败则 IOService start 失败，不悄悄退回诊断服务并报告启动成功。成功 phase 为 `boot-identified-no-execution`；公共诊断协议的 phase／capability **仍只表示 PCI_READY／PCI**，没有伪造 execution／display bit。

PCI 校验要求：

- 固定 256 字节 little-endian snapshot；精确 vendor／device、type-0 header、VGA 或 3D display class。
- BAR0 为已分配、非 prefetch 的普通 32／64 位 memory aperture，OS 描述符至少 4096 字节。
- memory decode **已经由平台启用**；不调用 `setMemoryEnable`、`setBusMasterEnable`，不写 PCI command、BAR、PMCSR 或其他配置。
- capability chain 有界、对齐、不循环；拒绝非法 cap ID、重复／重叠 PM capability，存在 PM 时要求有效版本且为 D0。没有 PM capability 不意味着已实现电源管理。

映射只使用 `getDeviceMemoryWithRegister(0x10)` 取得的 OS descriptor，不根据外部地址创建 mapping。核验 mapping 长度、地址对齐／整数范围、readonly／cache 属性。读取两轮 boot tuple，前后复核完整 config、descriptor、长度和 inactive 状态；变化则拒绝发布。

`OSReadLittleInt32` 与 `OSSynchronizeIO` 用于 MMIO。**不能用 `IOPCIDevice::ioRead32` 替代**：Apple 的 x86 实现使用 `inl`，它是端口 I/O，不是 BAR0 memory read。架构 fallback 为拒绝非 x86_64，不编造其他平台实现。

## 固件／RM 选择边界

本阶段只为已识别的 AD107 报告与固定 NVIDIA 开放源码相符的依赖：

- RM release `610.57.04`。
- 外层 GSP 容器 `gsp_ga10x.bin`。
- 对应 signature section `.fwsignature_ad10x`。

没有读取／分发／认证／上传固件，没有启动 GSP，没有导入 RM 或改变 MIT 许可。版本字符串、容器结构或 signature section 的存在都不等于签名验证。后续须从可信官方材料取得匹配 release，并由真正安全启动链认证。

## 后续执行顺序

1. 实现 NVIDIA RM 所需的 XNU OS 适配，复用现有内存、锁、uptime、工作队列；所有必需回调必须真实实现，禁止空返回成功。
2. 接入电源／所有权管理、可信固件来源与完整生命周期；在开启硬件写入前建立独占与断电／终止协调。
3. 接 IODMACommand／IOMMU 的设备地址映射、显存与 GPU VA；不能把 CPU pointer 或物理地址直接当 GPU DMA 地址。
4. 启动匹配的 RM／GSP，实现队列、提交、fence、超时与排空；在 NVIDIA 真机完成首个回读闭环。
5. 接真实 DDC／AUX、显示链路、寄存器、flip／vblank、HDR 与整卡恢复。
6. 按已获用户授权的 [Mesa NVK／NAK 路线](nvidia-nvk.md)推进 Darwin Vulkan ICD／图形编译，再接 Metal 翻译与系统呈现。Windows ICD 不能在 macOS 复用；NVK 的 Linux 构建也不是 Darwin ICD。

后续已将真正的离屏资源／提交／fence／回读做成[可调用 Vulkan 库](offscreen-rendering.md)，并在 Windows 官方 NVIDIA ICD 上通过多帧及资源生命周期实测。它提供将来 Darwin ICD 的同一验收入口；不是这里的 RM／GSP／ICD 已完成，不能升级 macOS NVIDIA 支持状态。

## 验证证据与安全审查

宿主 `make test-nvidia CONFIG=Debug|Release|Alpha`：固定配置／boot tuple **构造夹具**，覆盖 capability 边界／环／PM 重叠、D3／decode disabled／错误 BAR／错误型号、boot identity 不一致、virtual GPU 拒绝、参数／别名、失败输出不变，以及 20,000 次输入变异。它不模拟硬件成功，也不构成 MMIO 实测。

核心 CI 为 NVIDIA 独立库运行回归、ASan／UBSan 与静态分析；手动 macOS 15／26 CI 编译／分析原生 probe、链接／签名 kext，核验 vendor 与公共库边界。**编译与链接不等于加载；截至本阶段没有 NVIDIA macOS 真机 MMIO 读取记录。**

威胁边界：不可信 PCI／MMIO 值 → 校验和芯片选择 → registry 元数据。资产是内核映射、provider 引用与后续固件决策；新路径无用户态入口、任意地址、DMA、固件执行或硬件写入。配置大小、cap iteration 和映射大小都受限，失败输出保持不变；map failure、provider-open failure、config／boot 变化均直接失败，并由 RAII 释放成功取得的资源。真实 map OOM／退出竞态尚需隔离真机故障测试，源码检查不能代替它。

残余风险明确保留：`open` 和 D0 snapshot **不是物理断电／hot-unplug 屏障**。检查与 MMIO transaction 之间仍可能发生断电，CPU／总线 fault 无法靠普通返回值处理；尚未实现 IOKit 电源状态转换和硬件终止的同步。因此默认禁用，仅允许受控、稳定供电、不与其他 GPU 驱动竞争的开发测试，不适用于生产或 hotplug 环境。也不保证目标 OS 的全部内核 KPI／私有 ABI 已验证。

公开字段依据：NVIDIA commit `e4a5faa2567f28c8eabe0ebb6422b6d0abcf37eb` 的 `src/common/inc/swref/published/nv_ref.h`、`nv_arch.h` 及 `src/nvidia/arch/nvalloc/unix/src/osinit.c`；映射与端口 I/O 根据 Apple IOMemoryDescriptor／IOPCIFamily 公开接口核对。本轮独立实现，不复制其他驱动实现。
