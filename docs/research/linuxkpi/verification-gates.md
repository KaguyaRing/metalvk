# 硬件验证门与证据要求

唯一验收目标：固定Linux xe、Intel `8086:56a0`、未来固定x86_64 macOS 15 build。以下状态属于本目标，不挪用NVIDIA/Windows/模拟结果。

状态定义：PASS=实际满足该门；FAIL=实际测试未满足；BLOCKED=缺前置/授权/能力；NOT_RUN=未测试；NOT_APPLICABLE=当前合同明确不适用，不得用来宣称完整支持。

**当前G0 PASS，限定范围G1 PASS；G2–G13 BLOCKED。** G1是可行性研究分析门，不是已提供全部API或穷尽链接闭包。原始driver配置/完整传递符号闭包仍待真正编译。

## 共用安全前置

G4以后需独立获批：非生产隔离Mac、完整build/硬件ID、明确现有PCI owner、恢复启动方案、数据备份、故障日志、测试时间窗口与停止条件。不得更改SIP/AMFI/OpenCore/boot args，禁止自动install/load/seize。缺安全前置即BLOCKED，不是FAIL。

未来只读采集可用（本轮Windows未执行）：

```sh
sw_vers
uname -m
csrutil status
kmutil showloaded
ioreg -r -c IOPCIDevice -l
```

采集日志需脱敏，不索取密码/私钥。尚无xe Darwin工具，所以后续操作用测试程序合同描述，不编造现有可执行命令或日志。

| 门/当前状态 | 前置与测试程序/产物 | 期望可观察结果 | 失败条件与必需证据 |
|---|---|---|---|
| G0 PASS：固定源 | clean metalvk/reference Git状态；`git rev-parse HEAD`；metadata检查 | driver、XNU、PCI、对照源固定完整revision；GPU ID来自固定表 | 来源移动/hash不符即失败；保留revision、tag解引用、source-inventory。目标build未知已标明，不计部署基线完成 |
| G1 PASS（限定研究）：依赖语义分析 | 依赖表/调用链/配置/分类与证据脚本；人工核对关键链 | 主要执行子系统、Darwin差异、风险与未覆盖传递范围明确 | 以grep声称穷尽、隐藏必要依赖、缺引用即不通过；当前没有完整compiler call/link closure |
| G2 BLOCKED：目标编译 | 路线/合同批准，合法源闭包、固定Darwin SDK/toolchain/generated头、完整实现；执行真实driver构建 | 所选x86_64 Darwin配置全对象编译，零诊断，无stub | 假成功wrapper、删功能绕开错误、错误target或未固定config不通过；保存argv/config/compiler logs/object hashes。宿主生成器不计本门 |
| G3 BLOCKED：Darwin链接/KC | G2、精确build的合法Kernel SDK/KC、许可审查；离线link及KC test集合 | Mach-O kext、imports与OSBundleLibraries完全对应，KC解析成功 | unresolved/private未经接受、SDK/target不匹配、链接仅靠weak unresolved不通过；保存link map、imports allowlist、日志和KC hash；不安装 |
| G4 BLOCKED：授权加载 | G3、共用安全前置、适用签名/管理员批准 | 系统确认特定hash/bundle加载；无panic/自动抢设备 | 未授权、签名/政策不接受、panic即停止；保存系统build/安全状态、签名、加载日志。不自动调整安全政策 |
| G5 BLOCKED：物理match/probe | G4、明确选卡/owner、DG2 opt-in策略获准 | 日志关联registry/BDF/vendor/device/revision与初始化阶段、firmware状态 | default GPU/错误ID/已有owner/probe失败/仅service出现不通过；保存MMIO/firmware错误但不泄露地址、资源回收日志 |
| G6 BLOCKED：真实IPC | G5、权限控制的接口及显式device选择 | 本连接成功query、schema/size/identity验证，close排空 | loopback/mock/跨卡/清理失败不通过；记录请求摘要、响应版本/长度、close/进程退出/并发压力；仅诊断不证明执行 |
| G7 BLOCKED：DMA/BO/VA | G6、mapper/cache/地址宽度与memory lifetime验证 | BO分配/map、device segments、PT bind/unbind及错误rollback，进程退出不留资源 | physical=IOVA假设、越界、提前free、无revocation证据不通过；保存配额/计数、patterns、映射生命周期，不暴露物理地址 |
| G8 BLOCKED：实际提交 | G7、GuC/engine/IRQ ready；指定GPU的最小write/compute batch | 硬件queue消费job，设备实际写非预置pattern | noop/零batch同步/CPU写入/default GPU/MoltenVK均不通过；保存shader/batch hash、提交编号、硬件推进日志 |
| G9 BLOCKED：完成 | G8、可信completion与timeout/error路径 | GPU seqno/IRQ或fence对应本次提交，错误位为零 | 软件error fence或无硬件因果不通过；保存IRQ计数、seqno、fence status、timeout/reset注入结果；不用上次signal充数 |
| G10 BLOCKED：正确回读 | G9、cache sync、独立输入及全量期望 | 多次不同seed计算/图像全缓冲一致，guard区无改写 | 单像素/抽样、CPU回退生成或cache旧数据不通过；保存seed、输入/输出hash、完整比较、显式identity和提交链 |
| G11 BLOCKED：ICD执行 | G10、固定ANV/libdrm/loader/ICD manifest与feature矩阵 | 同一物理GPU通过Vulkan枚举→compute/graphics→fence→全图回读，validation无错误/警告 | 仅pipeline编译/device create、其他ICD/default GPU、虚假扩展广告不通过；保存API日志、manifest/dependency hashes与设备身份 |
| G12 BLOCKED：呈现/显示 | G11、显示合同另行批准、真实EDID/DisplayID、link/flip/vblank与恢复路径 | 指定输出实际scanout，多模式/高刷/HDR及sleep/reset逐项证实 | framebuffer发布或IOSurface copy不代替scanout；保存EDID、链路状态、flip/vblank/模式与恢复结果；无display实现，非PASS/N/A |
| G13 BLOCKED：Metal应用 | G11及所需G12、实际Metal翻译/shader/resource系统接口 | 真Metal app经metalvk→指定ICD→同卡GPU执行，必要时实际显示 | Apple默认GPU/CPU/MoltenVK或私有路径未接通不通过；保存应用/API/shader/ICD/device/submit/readback完整因果证据 |

## 错误、资源与安全验收

每个内核前置增加negative tests：OOM/quota、恶意size/extension/reserved、跨连接handle、超时、close与submit竞争、进程死亡、reset/suspend与mapping竞争。只在授权隔离环境运行。错误不得返回execution capability或已清理成功；无法证明停DMA时保留安全隔离/进入wedged，不释放仍可被设备访问的backing。

此前41/41 noop pipeline、host regression、Linux shader离线编译、Windows官方NVIDIA执行、参考作者KC link均不是本目标G4以后证据。各门产物须关联driver/build/device/session/submission；不能拼接不同机器结果。
