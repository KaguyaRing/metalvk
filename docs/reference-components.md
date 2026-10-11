# 独立平台与验证组件

这些组件不属于 `libmetalvk.a`，默认通用层 CI 不构建它们。

## 共享内核平台库、诊断 App 与 kext

`kexts/Platform` 提供厂商中立的 PCI 快照、IOService 生命周期与只读 UserClient，构建为 `libmetalvk-kernel-platform.a`。`backends/nvidia/kext` 保留精确匹配 `10DE:28E0` 的薄层；链接共享库形成 `MetalVK.kext`。没有 GPU 执行／显示功能，也不映射 MMIO、启用 DMA 或写 PCI 寄存器。AppKit App 提供后台诊断、错误展示与报告导出。

在 macOS 上，使用 Xcode 命令行工具、Python 3 和 MacKernelSDK：

```sh
python3 tools/build_macos.py --kernel-sdk /path/to/MacKernelSDK
target/macos/MetalVK.app/Contents/MacOS/MetalVK --diagnose
```

CLI 状态为 0：连接成功；1：错误；2：没有服务。输出 App/kext 与开发 DMG，使用 ad-hoc 签名；不是生产签名或安装批准。尚无特权安装器、固件启动或系统 Metal 插件。构建成功不能证明实机启动／加载成功。

手动 CI 在 `macos-15-intel` 与 `macos-26-intel` 构建，检查共享模块已链接、没有已知用户态运行时导入，并执行 App 无设备路径。不加载 kext；构建宿主版本不等于目标内核 ABI 验收。

## Vulkan 验证工具

`include/vk`／`src/vk` 的可选共享上下文库使用官方 Vulkan API 与系统 Loader/ICD；默认基础库不依赖它。`validation/vulkan` 是此库的验证客户端。需要 Vulkan SDK（含头文件、Loader import library、glslangValidator、spirv-val、Khronos Validation Layer）、Clang、llvm-ar/ar。

```sh
python tools/build_vulkan.py --sdk C:/VulkanSDK/1.4.363.0 --run --run-render --vendor 0x10de
python tools/build_vulkan.py --sdk C:/VulkanSDK/1.4.363.0 --run --run-render --vendor 0x1002
```

构建还执行无 GPU 的输入回归测试：厂商 ID、SPIR-V 结构与长度、文件所有权、全图逐像素／逐通道损坏。CI 在 Linux、macOS 15/26 编译并以 ASan/UBSan 执行这些测试；不把软件渲染器当硬件证据。

共享上下文要求真实集显或独显，拒绝软件设备与循环的 MoltenVK 路径；`vendor=0` 表示任意厂商，计算／图形分别请求相应队列能力。验证客户端启用普通／同步验证，提交真实命令、barrier 和 fence。计算比较 1、63、64、65、257、4097 元素及未写尾部；图形比较全部 4096 个 RGBA8 像素。GLSL→SPIR-V 的验证着色器不是 Metal 转换器。

超时不代表 GPU 已完成。提交后的失败先排空设备；非 device-lost 的异常排空失败会保留仍在使用的对象，交给验证进程退出回收，不销毁在途资源。这是验证工具的失败边界，不是生产设备复位协议。

`--run-render` 在全图成功之后还运行独立的分配 OOM、fence 超时和排空失败注入进程；测试拦截仅编译进测试可执行文件。其 `PASS injected ...` 表示错误清理符合预期，不表示 GPU 成功渲染；预期错误日志不是基线验证层错误。

Windows 实际 ICD 的计算回读结果不证明 macOS 后端、Metal 语义翻译、系统设备发现、内屏或 HDR。该工具也不构成通用层的厂商接入验收。

## 系统 Metal 离屏验收客户端

`validation/metal/render.mm` 仅调用系统 Metal API，不调用 metalvk 内部函数。原生构建生成 `target/macos/metal-render`：

```sh
target/macos/metal-render --list-devices
target/macos/metal-render --registry-id <目标设备的十进制或十六进制registry-ID>
```

必须显式选择目标设备。无该设备返回 2，参数错误返回 64，资源／编译／命令失败返回 1；成功仅在实际 Metal shader compile、render、blit、command completion 与全部 4096 像素一致后返回 0。五秒未完成不算成功。Vulkan 与 Metal 客户端使用 `validation/common` 的同一像素 oracle。

CI 仅把 Apple Paravirtual 设备用于验收客户端自身的参考测试，不能作为 metalvk、NVIDIA kext 或物理 GPU 支持证据。目标 kext 实机测试还需独立确认该 registry ID 对应自己的 accelerator 服务，并保留设备、OS build、驱动与执行证据。
