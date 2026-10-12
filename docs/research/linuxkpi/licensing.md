# 文件级许可与再分发审查

范围：研究来源的文件notice、SPDX、依赖与固件条款；**不是法律意见，也不是最终二进制link-closure许可认证**。本任务不复制/修改/集成第三方实现到metalvk。

## 已核对事实

| 组件 | 固定来源与实际notice | 不能推导的结论 |
|---|---|---|
| xe核心多数C/H | xe_module/PCI/device等为MIT；module metadata为`GPL and additional rights` [L03][L04] | MODULE_LICENSE不是所有文件统一MIT/GPL声明；须file-level审查 |
| xe页表遍历 | xe_pt_walk.c/.h `GPL-2.0-only`，原版Makefile编译walker [L02][L28] | 不可忽略这两个文件再说完整原版MIT |
| xe trace/VSEC | trace多文件GPL-2.0-only；VSEC GPL-2.0；元数据全目录列出 | 正则删GPL源或不编译trace不证明原功能等价；必须单独合法替代及测试 |
| xe resource cursor | `GPL-2.0-only OR MIT`，可选择许可分支；以源notice为准 | 不应因包含GPL文字就误判GPL-only |
| DRM managed | drm_managed.c `GPL-2.0` [L29] | 忽略它会破坏实际drmm寿命机制 |
| DRM GPUVM/TTM | GPUVM `GPL-2.0-only OR MIT` [L47]；TTM `GPL-2.0 OR MIT` [L30] | Linux EXPORT_SYMBOL_GPL是内核符号政策，不自动改写文件双许可；代码再利用与Linux链接政策分开 |
| MMU/HMM/DMA Linux core | mmu_notifier `GPL-2.0-only`；hmm `GPL-2.0-or-later`；DMA mapping `GPL-2.0` [L32][L35][L48] | 即使API适配独立编写，直接复制原core会带来该许可义务 |
| DRM syncobj/scheduler | file notice的MIT式授权，不能仅因无SPDX判成无许可 [L33][L34] | 缺SPDX不等于public domain |
| FreeBSD LinuxKPI样本 | module.h/pci.h为两条款BSD式notice，保留notice/disclaimer；还依赖其他BSD文件 [F01][F02] | FreeBSD整个源码和drm-kmod不是统一BSD；需要所用文件闭包 |
| drm-kmod样本 | dma-fence header notice单独审查 [D01]；Linux移植来源混合许可 | 不能将“FreeBSD版本”当GPL自动消失 |
| XNU/IOPCIFamily | Apple文件APSL/其他notice，以具体文件及发布LICENSE为准 | 阅读API不是复制实现；自行适配仍需Apple SDK/分发条款审查 |
| Mesa ANV | 本轮读到Intel文件MIT式notice，其他compiler/libdrm/dependency未闭包认证 | metalvk MIT不覆盖Mesa所有依赖 |

`source-inventory.json` 记录所有xe C/H SPDX及hash，并包含optional/display/test，所以license_counts是**源码目录计数，不是所选产物组成**。`NO_SPDX`必须人工读notice，不由工具自动判许可。最终需Kbuild/compiler dependency文件+链接map逐文件对应license。

全目录统计：MIT 380、GPL-2.0 8、GPL-2.0-only 10、GPL-2.0 AND MIT 13、GPL-2.0-only OR MIT 1、NO_SPDX 1。`AND`不是可自由二选一的`OR`。唯一无SPDX文件为`compat-i915-headers/gem/i915_gem_stolen.h`，人工检查未见独立许可notice；不能填成MIT，需结合仓库总许可及来源历史由专业审查。它属于display兼容头，是否进入最终产物需配置依赖图证明。

## 固件：存在独立法律阻塞

固定linux-firmware提交 `afabaf773c4c2e2c841429933a6a084a5af4d14d`：

- [WHENCE](https://kernel.googlesource.com/pub/scm/linux/kernel/git/firmware/linux-firmware/+/afabaf773c4c2e2c841429933a6a084a5af4d14d/WHENCE#5609) 列出DG2 GuC/HuC/DMC及许可指向；当前`dg2_guc_70.bin`版本70.53.0。
- [LICENSES/LICENSE.i915](https://kernel.googlesource.com/pub/scm/linux/kernel/git/firmware/linux-firmware/+/afabaf773c4c2e2c841429933a6a084a5af4d14d/LICENSES/LICENSE.i915) 允许无修改二进制再分发，要求保留notice/disclaimer、不得借Intel名称背书、禁止reverse engineering/decompilation/disassembly。
- **专利许可限定为软件单独使用，或与OSI批准开源许可的操作系统组合。** 在专有macOS中调用Darwin内核是否符合此条件，不能由工程师自行断言；需要Intel澄清或专业法律意见。

本轮只读取许可与WHENCE，没有下载/反汇编固件，没有导入blob。将来自行下载而不是捆绑，也不自动解决“使用组合”的专利问题。没有完成该审核前，不建议发布带DG2 GuC的Darwin执行组件。

## 不同行为与义务

| 行为 | 本任务状态 | 后续要求 |
|---|---|---|
| 阅读/记录接口和架构 | 已执行 | 原创分析，引用固定来源 |
| 将参考适配实现复制进metalvk | 禁止，未执行 | 用户已有明确不引用约束，即使许可可用也不得绕过 |
| 修改或静态链接Linux driver/core | 未执行 | 文件级许可、完整对应源码/构建脚本与GPL义务；专业审查XNU链接/派生作品兼容性 |
| 独立编写兼容接口 | 未执行 | 记录接口来源与独立实现边界；不能由“clean room”标签自行保证法律成立 |
| 分发固件/第三方binary | 未执行 | 固定hash/notice/条款、商标/专利和对应源码义务审核 |

metalvk核心保持MIT；研究未来独立GPL kext并不表示“分目录即可免除所有链接义务”。GPL与XNU/SDK组合、修改后源码提供范围、APSL covered code和Firmware patent均需法律门。路线A/B都不能消除这些问题。
