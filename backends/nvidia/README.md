# NVIDIA 后端

本目录是 NVIDIA 特定的独立后端，不进入通用库 `metalvk.h` 的默认构建，也不链接进内核的中立部分。

## 模块布局（`include/` 与 `src/` 镜像）

- `include/nvidia/boot.h` / `src/boot.c`：PCI／D0／capability 校验与 AD107 boot register 解码。
- `include/nvidia/transport.h`：用户态 nvkmd 适配器与 Darwin 内核端点共用的传输分帧标准（版本、op 目录、类别、请求／响应信封与校验 API）。
- `include/nvidia/discovery.h` / `src/discovery.c`：discovery 数据模型、初始化、能力裁剪与固定小端编码；不负责 session 分发。传输版本 2 拒绝旧主机结构载荷，详见[字节标准](../../docs/nvidia-discovery-wire.md)。
- `include/nvidia/endpoint.h` / `src/endpoint.c`：已采集 PCI 快照到 discovery 的保守字段投影；不按 PCI ID 猜测 SM、chipset、VRAM 或 RM class。
- `kext/Client.hpp/.cpp`：独立 NVIDIA 只读 UserClient（connect type 1）；公共 PCI 诊断仍使用 type 0。`validation/nvidia/discovery.cpp` 是真实 IOKit 查询工具，不是 loopback。
- `include/nvidia/session.h` / `src/session.c`：内核端点的能力门控分发核心；响应携带 `backed_op_mask`，未实现的 op 一律拒绝，两侧各自裁剪未被支持的能力位。
- `src/transport.c`：请求／响应信封编解码与操作目录校验；内核库与 Mesa 内适配器编译同一份源码，减少重复实现，不代表运行时或 RM 字段语义已完全对齐。
- `include/nvidia/init.h`：聚合本模块全部公共头。
- `nvkmd-alignment.json`：固定上游 nvkmd 操作到传输 op 的映射清单，由 `tools/check_nvkmd_alignment.py` 核验。
- `mesa-source.json`：固定 Mesa 26.2.4 来源与 SHA-256。
- `kext/`：只读 PCI 诊断原型与 kext 接线，默认关闭，不提供 GPU 执行或显示。

## 当前状态

操作名称／类别映射可机器核验（30 个上游操作、28 个传输操作），不是完整参数、资源或 RM 语义对齐证明。共享分发器实现能力门控与拒绝路径，DEVICE_QUERY 当前使用调用者提供的数据；尚未接入真实 probe／UserClient 往返。内核侧 memory／address／execution／sync 尚未实现，用户态验收适配器拒绝创建执行设备，不伪造可用 GPU。范围与后续顺序见 `docs/nvidia-nvk.md`。
