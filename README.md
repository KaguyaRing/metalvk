# metalvk

[English](#english) | 中文

一个**厂商无关的通用 GPU 库**：上层供 Metal→Vulkan 前端使用，下层由厂商后端实现（NVIDIA 为第一个后端）。目标是让同一套通用机制复用于不同 GPU，而把厂商细节隔离在后端。

## 目标与原则

- **机制与厂商分离**：公共头不含任何厂商类型、寄存器或私有 ABI。
- **可独立测试**：核心为纯 C11，无平台依赖，用 mock 后端在主机上测试。
- **明确所有权**：句柄式、有所有者；不暴露裸内核指针或 GPU 地址。
- **错误向上传播**：统一 `mtvk_status`，缺能力明确返回不支持，不假成功。
- **后端可替换**：后端通过 OPS 表注册。

## 目录

```
include/
  platform.h           平台与语言宏
  metalvk.h     顶层聚合头
  base/                status / limits / span
  gpu/                 backend OPS 与设备接口
src/                   与 include/ 镜像
tests/                 主机测试（含 mock 后端）
Makefile               Debug / Release / Alpha
```

## 构建与测试

```sh
make test CONFIG=Debug
make test CONFIG=Release
make test CONFIG=Alpha        # UBSan
```

Windows 使用 `mingw32-make`、`AR=llvm-ar`。

## 后端接口

后端实现 `mtvk_gpu_backend_ops`（`enumerate` / `open` / `close`），通过 `mtvk_gpu_register_backend` 注册；上层用 `mtvk_gpu_enumerate` / `mtvk_gpu_open` / `mtvk_gpu_close` 访问，无厂商类型跨界。

## 状态

G0 骨架：通用后端 API + 注册/分发 + mock 后端 + 主机测试。显示、着色器、命令、同步等模块与 NVIDIA 后端待后续实现。

新增 macOS 开发组件：

- `MetalVK.kext`：限定匹配 `10DE:28E0`，只读取 PCI 配置和资源长度，提供版本化诊断客户端；不写 MMIO、不启动 GPU、不控制显示。
- `MetalVK.app`：AppKit 诊断界面、后台连接、错误展示和报告导出；可用 `--diagnose` 运行 CLI（0：已连接，1：错误，2：未找到服务）。
- `include/ipc` / `src/ipc`：显式小端固定长度协议，不依赖 C 结构体布局；导出资源长度而非内核/物理地址。
- `.github/workflows/macos.yml`：原生构建、主机测试、ad-hoc 签名检查、无设备诊断路径及开发 DMG 产物。

构建（macOS，Xcode 命令行工具及 Python 3）：

```sh
python3 tools/build_macos.py --kernel-sdk /path/to/MacKernelSDK
target/macos/MetalVK.app/Contents/MacOS/MetalVK --diagnose
```

CI 固定 MacKernelSDK 提交 `3f750085caa17ec3a7880f11c11bf4f48cd6a164`。
其头文件按 system headers 处理；原生 kext 目标有意使用 IOKit PCI 接口，单独关闭该已弃用 API 的告警，项目其余告警视为错误。

产物是 **diagnostic-only 开发版本**：ad-hoc 签名不是生产签名或安装批准。App 携带 kext，但尚未提供特权安装器、RM/GSP、Metal 用户态插件、GPU 执行或显示接管；不可当作已经可用的显卡驱动。CI 不加载内核扩展，也不证明 NVIDIA 硬件可用。正式可用后才发布 `0.1.0`。

---

## English

`metalvk` is a vendor-neutral generic GPU library core. A Metal→Vulkan front end uses it; vendor backends (NVIDIA first) implement it through an OPS table. Public headers carry no vendor types. The C11 core is host-testable with a mock backend. Build with `make test CONFIG=Debug|Release|Alpha`.
