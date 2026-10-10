# 独立平台与验证组件

这些组件不属于 `libmetalvk.a`，默认通用层 CI 不构建它们。

## 旧诊断 App 与 kext

`backends/nvidia/kext` 是限定匹配 `10DE:28E0` 的只读 PCI 诊断原型，尚未接入新版通用后端注册，也没有 GPU 执行／显示功能。AppKit App 提供后台诊断、错误展示与报告导出。

在 macOS 上，使用 Xcode 命令行工具、Python 3 和 MacKernelSDK：

```sh
python3 tools/build_macos.py --kernel-sdk /path/to/MacKernelSDK
target/macos/MetalVK.app/Contents/MacOS/MetalVK --diagnose
```

CLI 状态为 0：连接成功；1：错误；2：没有服务。输出 App/kext 与开发 DMG，使用 ad-hoc 签名；不是生产签名或安装批准。尚无特权安装器、固件启动或系统 Metal 插件。构建成功不能证明实机启动／加载成功。

## Vulkan 验证工具

`validation/vulkan` 使用官方 Vulkan API 与系统 Loader/ICD。需要 Vulkan SDK（含头文件、Loader import library、glslangValidator、spirv-val、Khronos Validation Layer）、Clang、llvm-ar/ar。

```sh
python tools/build_vulkan.py --sdk C:/VulkanSDK/1.4.363.0 --run
python tools/build_vulkan.py --sdk C:/VulkanSDK/1.4.363.0 --run --vendor 0x1002
```

探针要求硬件设备，启用普通／同步验证，提交真实计算命令、barrier 和 fence，比较 1、63、64、65、257、4097 元素及未写尾部。默认设备过滤只属于验证工具，不属于通用层。GLSL→SPIR-V 的验证着色器不是 Metal 转换器。

Windows 实际 ICD 的计算回读结果不证明 macOS 后端、Metal 语义翻译、系统设备发现、内屏或 HDR。该工具也不构成通用层的厂商接入验收。
