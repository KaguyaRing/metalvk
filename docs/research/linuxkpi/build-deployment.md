# 构建、链接、KC与部署

## 实际环境与源获取

当前Windows x86_64、Python 3.13、LLVM-MinGW Clang 22.1.8（Windows GNU target）。`xcrun`未安装；WSL命令存在但未安装可用Linux子系统。本轮不安装环境或改系统设置。

取得官方Linux annotated v6.14后固定为commit；初次sparse命令错误地将`kernel/workqueue.c`当cone目录，失败后改用`kernel`目录成功。首次metadata临时探查受Windows默认GBK影响，改用显式UTF-8后成功。上述失败未被记为成功测试。

原Linux目标需要Kbuild、所选Kconfig及生成的autoconf/架构头、asm offsets、compiler attributes、trace/generated headers、xe workaround host生成器。[L01][L02] 克隆是稀疏研究树，没有生成Linux配置；**不能直接把这个目录称为已可编译driver SDK**。

## build profile与原生目标区别

研究profile见 [目标基线](target-baseline.md)。它只说明预期禁用条件，不是已生成`.config`。无display仍含core MMU/HMM、GuC、scheduler/TTM及未按条件排除的通用SR-IOV/auxiliary源码。[L01][L02]

Darwin port未来需要：

1. 固定Apple SDK/Kernel.framework/IOPCIFamily headers、编译器与deployment target，单独C/C++ translation units；不能混入Linux宏污染Apple头。
2. 处理GNU C类型/statement expressions/attributes、Linux inline asm/barriers、trace宏、generated头和架构配置；不把Windows clang可运行当Apple compiler兼容。
3. kext C++不依赖用户态运行库/异常/RTTI；Linux ELF module/init sections需映射到Mach-O生命周期注册，不依赖全局构造器自动完成初始化。
4. Linux core的GPL-only源与所需inline实现须许可审查；不得空stub来凑链接。
5. 输出完整link map/undefined imports，逐一对精确build KC/依赖bundle核查，不只读公开exports文件。

参考构建脚本使用作者绝对路径和多组object链接，[R07][R08] 说明实现方式但不构成本项目可复现构建。FreeBSD-src与drm-kmod必须固定配对；本轮样本没有证明配对可用。[D02]

## 已执行的安全离线实验

未修改上游`xe_gen_wa_oob.c`，用标准宿主compiler构建真实生成器；仅通过预处理define替换glibc的程序名变量，用常量名称提供host平台差异，不是driver stub。[L37]

Python subprocess argv（避免PowerShell引号歧义）：

```text
clang -std=gnu11 -Wall -Wextra -Werror -O2
  -Dprogram_invocation_short_name="xe_gen_wa_oob"
  Example/linux-v6.14/drivers/gpu/drm/xe/xe_gen_wa_oob.c
  -o <approved-temp>/linuxkpi-phase2-host/xe_gen_wa_oob.exe
xe_gen_wa_oob.exe <absolute-source>/xe_wa_oob.rules wa-a.c wa-a.h
xe_gen_wa_oob.exe <absolute-source>/xe_wa_oob.rules wa-b.c wa-b.h
```

结果：compile exit=0，无compiler diagnostics；两次generate exit=0，输出逐字节一致。

| 临时产物 | 字节 | SHA-256 |
|---|---:|---|
| wa-a.c | 2822 | `09d385e3db5ed37c97bbf41ba8b7889da63eb412704e3fdfbee2c029a788b28b` |
| wa-a.h | 1174 | `612237c4870a00c1259b146b9a4b4bfcd313a7ec58a79320ace369e76396dddc` |

输出包含生成时绝对input路径，以上hash与本次路径绑定；改变工作空间路径可能改变hash，不等于逻辑不确定。产物保存在 `C:/Users/kaguyaring/AppData/Local/Temp/opencode/linuxkpi-phase2-host/`，不分发、不计G2。未执行Linux完整driver build、Darwin-target build、KC链接或设备访问。

## 部署门与授权

本轮不提供可直接执行的安装/load命令。目标build未知、无授权隔离Mac、无可加载研究kext。

- Apple公开流程：用户批准第三方kext、适用的签名/开发测试政策、Kernel Collection接纳和重启，不能由ad-hoc签名或LinuxELF链接替代。
- Intel与Apple silicon授权流程不同；本目标x86_64，不擅自套用1TR/Reduced Security流程。
- kext plist依赖/签名与用户态UserClient entitlement/caller policy分开；所选用户态接口未定，不能编造必需entitlement值。
- KC符号解析成功只是G3的一部分，不证明G4授权/G5 probe/G8执行。
- 不改SIP/AMFI/OpenCore/boot args，不seize现有owner。未来必须有恢复介质/旁路启动、数据备份和管理员明确授权。

平台依据：[Apple Securely extending the kernel](https://support.apple.com/guide/security/securely-extending-the-kernel-sec8e454101b/web) 与 [项目既有kext研究](../../kext-lifecycle-research.md)。Apple网页可变，固定源码表才是本轮ABI参考；部署条件最终以目标机实际政策为准。

## 研究交付QA记录

2026-10-12运行，不涉及生产驱动测试：

- `python -B docs/research/linuxkpi/audit_sources.py --references ../../Example`：检查固定干净Git来源、源规范化hash、证据行号、必需文档及本地文件链接；成功。最终索引窗口数以`source-inventory.json`和脚本输出为准。
- `python -B -m unittest discover -s docs/research/linuxkpi -p test_audit_sources.py -v`：八项全部通过；临时文件只在批准的opencode temp子目录。
- `git diff --check`：无空白错误；Git报告现有autocrlf配置的LF→CRLF提示，不是compiler诊断，不修改用户Git配置。
- `git fetch origin main`、`git rev-list --left-right --count HEAD...origin/main`：交付前基线`0 0`；main与origin/main都在`c3f2de3`，无已检测分叉。
- `git rev-parse 'v0.1.0^{}'`：保持`318d24a4e2340c95ee5d1e9ce5b41143079f2145`。

文档/源检查、host生成器和Git检查都不能作为Darwin driver/GPU验收证据。未运行原有production build/CI，因为仅研究文档与自检工具改变，旧CI状态保持历史范围。
