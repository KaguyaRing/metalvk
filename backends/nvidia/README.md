# NVIDIA 后端

本目录用于 NVIDIA 特定的独立后端，通过 `include/gpu/runtime.h` 的统一合同接入通用层。

`kext/` 当前包含只读 PCI 诊断原型，尚未接入新版 runtime，不提供 GPU 执行或显示功能。

厂商实现与依赖留在本目录，不进入通用库的默认构建。
