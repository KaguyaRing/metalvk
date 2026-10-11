# 开发产物构建与发布

`release.yml` 复用 `reference-macos.yml` 的原生构建，不复制第二套 App／kext 编译逻辑。
每次发布解析已有 Git tag 到固定 SHA，两个 macOS Intel 构建都通过后才进入发布 job。
该流程只发布诊断开发产物，不宣称 GPU 执行／Metal／显示支持。

## 版本标准

- 历史 `v0.1.0` 保持指向 `318d24a`，不移动或删除；此标签不表示驱动生产可用。
- 后续开发标签采用 `vMAJOR.MINOR.PATCH-<最后已推送提交的前七位>`。
  新建标签的版本、基线短哈希和源码 SHA 在发布记录中明确；不修改已经公开的标签。
- 流程将开发产物标为 GitHub **prerelease**，不设为 latest。
  `v0.1.0` 是保留的历史标签特例，不作为未来无后缀开发版本的命名模板。
- 标签存在不代表 Release／二进制已发布；状态以 GitHub Release 和 CI 成功结果为准。

## 触发与产物

新标签 push 可触发；历史 `v0.1.0` 通过默认分支的 workflow_dispatch 发布：

```sh
gh workflow run release.yml --ref main -f tag=v0.1.0
```

它构建标签源码，不构建 main，也不包含后来的未提交修复。发布编排来自所触发的 workflow 版本，
包内 `BUILD.json` 记录真正构建的源码 SHA、宿主 build、架构、固定 kernel SDK 与能力限制。

每个宿主提供一个 ZIP 及其 SHA-256；ZIP 包括 App、kext、开发 DMG、LICENSE、两份 README、
`BUILD.json` 与 `DEVELOPMENT.txt`。原生 `ditto` 压缩发生在上传 artifact 前，保留 Unix 权限；
只分发自有诊断产物，不将完整 Mesa Linux ICD／NAK 或上游依赖拼进 macOS 包。

签名是 ad-hoc，不是 Developer ID／notarization。包不自动安装、加载、改 SIP 或 OpenCore；
目标机是否批准、Kernel Collection 是否接纳和 PCI start／UserClient 是否成功仍需独立验收。

## 发布安全范围

- Actions 版本固定到 commit；构建 job 只有 contents:read，只有最后发布 job 有 contents:write。
  checkout 不保留 Git credential，不在 pull_request 上发布。
- 标签字符串严格校验后解析，拒绝不存在或不支持的标签；同标签 concurrency 不取消在途发布。
- 下载仅本次运行的两个 native package。发布器校验文件集合、大小、SHA-256、ZIP 路径／重复项、
  受限 metadata 与源码 SHA／宿主／能力一致性；不解包外部文件，不执行包内程序。
- 先建 draft 并上传资产，再公开为 prerelease；上传失败不会自动公开部分产物。
  已有 release 不覆写。失败遗留 draft 需核对后人工处理，不自动删除用户已有发布。
- 校验和不是签名／发布者认证；metadata 与 hash 验证依赖本次受信任 CI 来源，
  不能当作对任意外部 ZIP 的真实性证明或 kext 可加载保证。
- 本地主机验证器测试覆盖正确双宿主包、缺包、篡改 hash、错误 source SHA、危险路径及错误标签。
  native 打包／签名与真正上传须由 CI 验证；Windows 本地测试不替代这些证据。

源码、构建、加载、执行与显示状态分别报告。当前 NVIDIA RM／GSP、DMA、GPU VA、提交／同步，
以及 Metal 翻译和系统显示尚未完成，不以“Release 已发布”升级支持范围。
