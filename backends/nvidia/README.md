# NVIDIA 后端

本目录是 NVIDIA 特定的独立后端，不进入通用库 `metalvk.h` 的默认构建，也不链接进内核的中立部分。

## 模块布局（`include/` 与 `src/` 镜像）

- `include/nvidia/boot.h` / `src/boot.c`：PCI／D0／capability 校验与 AD107 boot register 解码。
- `include/nvidia/transport.h`：用户态 nvkmd 适配器与 Darwin 内核端点共用的传输分帧标准（版本、op 目录、类别、请求信封与校验 API）。
- `include/nvidia/discovery.h`：`DEVICE_QUERY` 载荷，对齐上游 `nv_device_info` 的固定布局。
- `src/transport.c`：唯一的编解码实现；内核库与 Mesa 内适配器编译同一份源码，两侧无法各自漂移。
- `include/nvidia/init.h`：聚合本模块全部公共头。
- `nvkmd-alignment.json`：固定上游 nvkmd 操作到传输 op 的映射清单，由 `tools/check_nvkmd_alignment.py` 核验。
- `mesa-source.json`：固定 Mesa 26.2.4 来源与 SHA-256。
- `kext/`：只读 PCI 诊断原型与 kext 接线，默认关闭，不提供 GPU 执行或显示。

## 当前状态

传输标准已对齐并可机器核验（30 个上游操作、28 个传输操作）。内核侧 memory／address／execution／sync 尚未实现，因此用户态适配器对执行类别一律拒绝（fail closed），不伪造可用 GPU。范围与后续顺序见 `docs/nvidia-nvk.md`。
