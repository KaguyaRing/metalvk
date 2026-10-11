# NVK／NAK 接入：构建基线与 Darwin 范围

用户已授权评估并引入 Mesa NVK／NAK。中立核心仍保持 MIT、SDK-free，不把 Mesa、Vulkan Loader 或 compiler 链接到 kext。当前新增的是可复现的上游 Linux 构建与离线 shader 验收，不是已实现的 Darwin ICD。

## 固定来源与独立构建

`backends/nvidia/mesa-source.json` 固定 Mesa **26.2.4**，官方 release archive 的 SHA-256 为 `bce5f7fbebb934373b86c999a064d52fb5065878dc57f287f95346648ec832e9`。下载要求 HTTPS、禁用代理、最多 128 MiB、180 秒；hash 不一致立即终止，不解包或编译。hash 固定保证取到相同 archive，不等于额外完成了发布者 PGP 签名认证。

`.github/workflows/nvidia-mesa.yml` 独立构建完整 `libvulkan_nouveau.so`、NAK／NIL／NIR、内部 shader 生成器与 CPU 测试。当前目标为 Ubuntu 24.04；Rust 1.90.0、Meson 1.9.1、bindgen-cli 0.72.1、cbindgen 0.29.0 固定，LLVM／Clang 18 及发行版依赖由 apt 提供。Mesa 自带 wrap 固定其 Rust source 与 hash。**发行版 package 的 patch 版本尚未做完整供应链锁定，不宣称 bit-identical reproducible binaries。**

完整依赖不能省略：NVK 内部 OpenCL kernels 需要 mesa-clc／LLVM／SPIRV-LLVM-Translator，NAK 与 NIL 使用 Rust 和 C binding 生成器。GL／Gallium／EGL／GLX 不启用；没有借 llvmpipe 或 MoltenVK 代替 NVIDIA 执行。

构建验收检查真正的共享库与 `vk_icdGetInstanceProcAddr` 导出，并记录动态依赖。CPU 测试套件包括 `nak`、`nil`、`nvk_mme`、`mme_builder`；NAK 默认跳过需要实际 GPU／nvdisasm 的测试，不能把它们计入硬件覆盖。第一次配置因缺少 cbindgen 失败，已补齐；完整基线 CI `38144597556` 随后通过四个 suite。其日志还暴露 bindgen 调用缺失 rustfmt 的非阻塞错误，现已安装 rustfmt，不能仅凭绿色状态声称零诊断。

## 项目 shader 的离线验收

`validation/nvidia/nak-pattern.c` 是自有离线验收客户端；CI 将其附加到**独立解包的 Mesa build graph**，不替换任何上游 implementation。Meson overlay 位于 `validation/nvidia/nak-pattern.meson`，不进入中立库、内核库或生产驱动。

路径为仓库 GLSL → glslang Vulkan 1.2 SPIR-V → spirv-val → Mesa SPIR-V-to-NIR → NAK preprocessing／SM89 machine-code encoding。编译目标的 SM89 与 48 warps/MP 是显式 offline target 参数，不伪称从 PCI／RM 查询到的设备属性。输入按 repository shader 验收使用，最多 1 MiB；CLI 不作为接受任意不可信 shader 的服务或安全沙箱，执行由 CI 的 timeout 限制。

CI `38146109946` 已取得两个非空、16-byte instruction-aligned 的 native stage binaries 与 NAK IR dump：顶点与片元各 **272 字节**，四组 Nouveau CPU suite 全部通过，编译日志无实际 warning／error（Meson 的 `-Wno-error: YES` 参数探测不算诊断）。缺少 rustfmt 的错误已消除。链接失败时发现 NAK 的 graphics constant offsets 由 NVK shader runtime 定义，已通过实际 libnvk 与 idep_vtn 依赖接通，没有自造零值常量消除链接错误。

该 dump 不是独立 nvdisasm 反汇编／指令差分证明。离线编译成功不证明 varying linkage、实际引擎 class、GPU VA、pushbuffer、渲染正确性或 NVIDIA macOS 可执行；它只填补图形编译路线的一项实证。另有参数错误、文件缺失、空／截断／错误 magic／超长输入的失败回归；全部在限时独立进程中执行，不对真实 GPU 做故障操作。

## 实际 Darwin 接入点

固定源码中 `src/nouveau/vulkan/nvkmd/nvkmd.h` 已提供抽象对象，但 `nvkmd.c:nvkmd_try_create_pdev_for_drm` 目前直接调用 Nouveau 实现。不能把 `/dev/dri` ioctl 翻译成空 UserClient 成功返回，更不能认为 NVK 可直接使用官方 NVIDIA RM。

| 上游范围 | Darwin 实际需要 | 当前状态 |
|---|---|---|
| instance／physical discovery | IORegistry 枚举、只匹配已授权设备、可信内核身份与真实能力 | 只有只读 PCI／boot probe；无 NVK 枚举 |
| `nvkmd_pdev`／`create_dev`／`sync_types` | 每 client 设备会话、RM 资源 ownership、原生 Vulkan sync implementation | 未接入 |
| `nvkmd_dev:alloc_mem/alloc_tiled_mem` | GART／VRAM、DMA／IOMMU、真实 PTE kind 与 tile mode、预算及回滚 | wired CPU memory 不能替代 GPU BO |
| `nvkmd_mem:map/unmap/sync_*` | 用户映射、缓存一致性、不可伪造的对象引用与 map 生命周期 | 未接入 |
| `nvkmd_va:bind_mem/unbind` | GPU VA 范围、对齐／overflow 检查、页表与多资源事务 | 未接入 |
| `nvkmd_ctx:wait/exec/bind/signal/flush/sync` | 真实 GPFIFO／pushbuffer 提交、engine classes、fence、超时与设备丢失 | 未接入 |
| timestamp／VRAM usage | 真实计数器与内存使用量，不用 host time 或常数冒充 | 未接入 |
| dma-buf／DRM fd／WSI | 不提供 Linux-only external memory／sync 扩展；后续分别实现 macOS handle／IOSurface／present | 未接入；离屏首验不需要显示 |

路线为 **NVK／NAK 用户态 → Darwin nvkmd 实现 → NVIDIA 设备专用 UserClient → RM／GSP／DMA／GPU VA／提交**。这里的 transport 是 NVIDIA 后端私有实现，不恢复跨厂商 `mtvk_gpu_*` 提交 ABI；跨厂商应用接口仍为 Vulkan。硬件资源层必须先能运行，才发布 physical device／扩展／queue 能力。尚未实现的发现路径应拒绝设备，而不是注册一个“看起来可用”的 Vulkan GPU。

Darwin 编译还必须替换／隔离 libdrm、ELF build-id、DRM sync、POSIX/Linux-only 调用与 WSI 依赖。Linux `.so`、manifest 和 CPU test 不能部署为 macOS `.dylib`；当前没有提交伪 Darwin adapter。

## 许可与发布限制

Mesa 不是统一 MIT。固定版本的 NVK 主目录、NAK 大部分代码、NIL 使用 MIT notices；NVIDIA headers、NIR、util、Vulkan runtime 等有相当数量的完整传统 notice、但没有 SPDX。**没有 SPDX 不等于无许可，更不能直接添加 metalvk 的 MIT 头覆盖。** 上游 Rust wraps 与 LLVM／libdrm 等还需各自 notice／dependency 审查。

当前通过官方 archive 在构建目录取源码，不把整库重新冠以 metalvk MIT。CI 保存原始 source archive 与源码 subprojects 以保留原始 notices；artifact 是开发基线，仍不是经过完整 binary redistribution 审查的正式 release。发布 Darwin ICD 前必须逐文件核对最终 link closure、汇集 notices、记录动态与静态依赖以及各自义务。不得删除上游版权或复制 NVIDIA 闭源用户态实现。

## 双方对齐的传输标准

对齐不是让两侧各自“看起来能编过”，而是让用户态 NVK／nvkmd 与 Darwin 内核端点共享同一份可机器核验的标准。当前已落实的部分：

1. **一份共享源码。** `backends/nvidia/include/nvidia/transport.h` 定义分帧标准（版本、op 目录、类别、请求信封与校验 API），`backends/nvidia/include/nvidia/discovery.h` 定义 `DEVICE_QUERY` 载荷（对齐上游 `nv_device_info` 的固定布局）。`backends/nvidia/src/transport.c` 是唯一的编解码实现。内核库 `libmetalvk-nvidia-kernel.a` 与 Mesa 内的用户态适配器都编译这一个 `.c`，因此两侧不可能各自漂移。
2. **可机器核验的映射。** `backends/nvidia/nvkmd-alignment.json` 逐项记录固定上游每个 `nvkmd` 操作到传输 op 的映射、类别与内核状态。`tools/check_nvkmd_alignment.py` 从固定的 `nvkmd.h` 解析出真实操作集，与清单、`transport.h` 目录、`transport.c` 类别表三方比对。本地与 CI `38146889949` 均已通过：**30 个上游操作、28 个传输操作、无缺漏、无多余、类别一致**。
3. **用户态适配器。** `validation/nvidia/nvkmd-metalvk.c` 在固定 Mesa 树内编译（不是上游文件），把通过校验的 `DEVICE_QUERY` 映射为真实 `nvkmd_pdev`，并对全部执行类别 `create_dev` 返回 `VK_ERROR_INCOMPATIBLE_DRIVER`。链路标准面因此是“可对接但拒绝放行”，不是伪造可用 GPU。
4. **多对一是被允许的。** 例如 `DRM_FD` 同时覆盖 `pdev.get_drm_primary_fd` 与 `dev.get_drm_fd`，以及 `pdev.destroy`、`mem.log_handle` 属于本地处理、无传输 op。检查器要求每个传输 op 至少被覆盖一次，不强制一对一。

**对齐验证不等于执行验证。** 上述全部是标准面一致性证据：它们证明两侧对同一份消息布局、op 目录、类别和错误路径达成一致，并证明用户态在缺执行能力时正确拒绝。它们不使用 GPU、不产生像素，也不升级任何硬件支持状态。

### 两侧仍不对称之处（必须显式记录）

| 传输类别 | 需要的内核能力 | 用户态已对齐 | 内核已实现 |
|---|---|---|---|
| discovery | 只读身份、boot register | 通过（映射 + 拒绝执行） | 只有只读 probe |
| memory | GART／VRAM、DMA／IOMMU、预算与回滚 | 通过 | 无（仅 wired CPU 内存，不能替代 GPU BO） |
| address | GPU VA、页表、对齐与溢出检查 | 通过 | 无 |
| execution | GPFIFO／pushbuffer、引擎 class、fence、超时 | 通过 | 无 |
| sync | fence／semaphore 等待与信号 | 通过 | 无 |
| replaced | Linux fd／dma-buf → macOS 原生句柄 | 通过（明确不支持） | 无 |

结论：**标准已对齐并可机器验证；执行层仍未对齐。** 首个真实 draw 被 memory／address／execution 三类尚未实现所阻塞，而不是被接口不一致所阻塞。下一阶段必须在内核侧把这三类接到真实 RM／GSP／DMA，然后由同一 `vulkan-render` 客户端通过明确的 NVIDIA ICD 验收。

## 后续首个真实 draw 的验收顺序

1. 完成项目 vertex／fragment 的离线 NAK 编译与 IR／binary 记录。（已完成）
2. 固定并机器核验两侧传输标准与操作映射。（已完成）
3. 在 Darwin 内核侧实现 discovery／memory／address／execution／sync：可信匹配固件、RM／GSP、DMA／IOMMU、GPU VA、queue／fence，并让 `nvkmd-alignment.json` 中对应条目的 `kernel` 状态从 `not-implemented` 变为已实现。
4. 在独立 Darwin build graph 编译 NVK／NAK 及真实 nvkmd adapter，禁用尚不可用的扩展。不得把 Linux 构建通过标记为此步通过。
5. 同一 `vulkan-render` 客户端通过明确 NVIDIA ICD 运行 draw → fence → 回读，验证全部像素与零验证诊断；需要隔离 macOS 真机。Windows 官方 ICD 结果只作 oracle，不作新后端结果。
6. 之后再推进 Metal 翻译、目标 registry ID 与系统呈现。当前没有“完整 macOS 渲染管线已跑通”的证据。

Windows 全图回读与 NAK 编译是两份**互相独立**的证据：前者把 SPIR-V 交给官方 Windows ICD，并未执行本文档所述的 NAK 输出；后者只完成编译。二者都不能合并当作自研后端可用。
