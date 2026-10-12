# 目标、来源与研究配置

## 仓库审计

metalvk 初始 HEAD `c3f2de308d004ffb4c08d898358adc37fd504cb9`，分支 `main`，工作区干净；origin `https://github.com/KaguyaRing/metalvk.git`。历史 `v0.1.0^{}` 为 `318d24a4e2340c95ee5d1e9ce5b41143079f2145`，不得移动。

`backends/nvidia/darwin/Discovery.cpp`、discovery codec、UserClient、adapter native ingress 与测试源码确实存在；本轮没有重验历史 CI。README 某些旧描述仍落后于只读 IPC 接线，不能将旧描述或新源码存在作为成功往返证据。

实际 Example 在工作空间根目录，不在 metalvk 内：`C:/Users/kaguyaring/Desktop/Projects/Example/`。

| 来源目录 | 固定基线 | 用途 |
|---|---|---|
| linux-v6.14 | `38fec10eb60d687e30c8c6b5420d86e8149f7557` | 官方 torvalds/linux `v6.14`，本轮新增稀疏克隆 |
| xnu-macos15 | `43a90889846e00bfb5cf1d255cdc0a701a1e05a4` / 11417.140.69 | 主 Darwin 来源 |
| xnu | `f6217f891ac0bb64f3d375211650a4c1ff8ca1ea` / 12377.1.9 | macOS 26 对照，不是第二验收目标 |
| IOPCIFamily-macos15 | `0eeb801b9d471778f5f177bd2e6c395a18f0ad95` / 681.140.3 | 主 PCI 来源 |
| IOPCIFamily | `4822b27a36e2de70e231ecf2bf3021384fab6ec2` / 726.0.5 | PCI 26 对照 |
| freebsd-14.3-research | `833d39bd2e38421a14ea489a956261a0fa993fbc` | releng/14.3 样本固定后审查；不声称适配 Linux 6.14 |
| drm-kmod-research | `c16de0f9fa84f70b9551f69fde837a7f3a73c0e9` | 官方 FreeBSD DRM/LinuxKPI 样本；与上一行配对未验收 |
| nvidia-macos-driver | `de9b33aa98dcb0ba6d5bf624fc3817e4a2d8bd66` / v1.14.1 | 仅架构观察，不复用实现 |
| open-gpu-kernel-modules | `e4a5faa2567f28c8eabe0ebb6422b6d0abcf37eb` / 610.57.04 | 候选对照，现有 RM 主线保持 |
| mesa/mesa-26.2.4 | 官方 26.2.4 archive，SHA-256 `bce5f7fbebb934373b86c999a064d52fb5065878dc57f287f95346648ec832e9` | ANV 源码观察；本轮未重下载/重验整个 archive |

Linux annotated tag object `35ff3b0f4596c82ec0a3e1cc43e2a85f5e976023` 解引用到上述 commit。不得将 tag object 当成源码提交。新 reference checkout 无修改；稀疏目录不是完整可构建 Linux 树。

## 候选比较与选择

| 候选 | 实际检查 | 选择判断 |
|---|---|---|
| xe 6.14 | Kconfig/Makefile、PCI/probe、BO/VM/exec/GuC/fence/PM，全部目标目录可获取 | 选为主目标：固定版本、较集中驱动、ANV xe backend/UAPI 可对照；HMM/GuC 是明确难点，不是假定最好移植 |
| amdgpu 6.14 | 官方 `amdgpu/Kconfig` 与 `amdgpu_drv.c` 注册入口，已有 TTM/scheduler/display 选择及广泛硬件路径 | 范围更广；本轮不扩展成多个 IP 代际完整审计。[L36] |
| NVIDIA RM 610.57.04 | 官方 README 要求配套同版本 GSP/userland；现有 project 已选 RM，而 NVK Nouveau 与 RM 不能直接互换 | 继续独立主线，不因 LinuxKPI 研究重选。[N01] |

## 唯一主目标

| 字段 | 值及限定 |
|---|---|
| PRIMARY_DRIVER | Linux DRM xe |
| UPSTREAM_VERSION / COMMIT | v6.14 / `38fec10eb60d687e30c8c6b5420d86e8149f7557` |
| SOURCE_REPOSITORY | https://github.com/torvalds/linux |
| SOURCE_TREE_PATH | `Example/linux-v6.14/drivers/gpu/drm/xe` |
| GPU_VENDOR / FAMILY | Intel / DG2-G10，单卡离散 GPU 研究目标，不是已拥有硬件 |
| DEVICE_ID_OR_IDS | `8086:56a0`，ID 宏包含此值；revision/subsystem/BDF/VRAM UNKNOWN。[L06][L07] |
| REQUIRED_FIRMWARE | `i915/dg2_guc_70.bin`；驱动期望 70.29.2、major-only 文件规则。DG2 不在此版本 HuC/GSC 固件表，不能擅加必需 HuC/GSC 文件。[L11][L12] |
| TARGET_MACOS_VERSION / BUILD | macOS 15 / UNKNOWN；15.8.1 仅已有项目来源背景，不代替目标 build |
| DARWIN_XNU_BASELINE | 上表 XNU15/PCI15；公开来源不保证精确对应所有安装 build |
| TARGET_ARCHITECTURE | x86_64；不含 Apple silicon |
| COMPILER_TOOLCHAIN | Darwin Apple Clang/xcrun/Kernel SDK PROVISIONAL，实际版本 UNKNOWN；宿主 LLVM-MinGW Clang 22.1.8 |
| BUILD_CONFIGURATION | 研究逻辑 profile：DRM_XE=m、DRM_XE_DISPLAY=n、PCI_IOV=n、HWMON=n、KUnit/debug/PGO=n；保留 SHMEM/TTM/GPUVM/SCHED/MMU_NOTIFIER/HMM 所需配置 |

DG2 descriptor `require_force_probe=true`。[L07] 因此 vanilla 6.14 默认不接管此卡；未来隔离试验必须显式定义 opt-in 策略。本轮**不改 boot args**，不默认抢占 i915 或 macOS driver。配置关闭显示减少 i915 display 共享对象，不自动消除 Kconfig 选择的显示 helper 或始终编译的 GSC/auxiliary/SR-IOV 通用代码。[L01][L02]

固件许可调查固定 linux-firmware `afabaf773c4c2e2c841429933a6a084a5af4d14d`；WHENCE 的当前 `dg2_guc_70.bin` 为 70.53.0，不是本轮已选/校验的执行 blob。真正上线前还须固定内容 hash、认证结果与 driver ABI，不把同 major 当充分验证。
