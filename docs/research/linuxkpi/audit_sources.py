#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Research-only source metadata generator/checker; never builds or loads a driver."""
import argparse
import hashlib
import json
import re
import subprocess
from collections import Counter
from pathlib import Path

BASELINES = {
    "L": ("linux-v6.14", "38fec10eb60d687e30c8c6b5420d86e8149f7557", "torvalds/linux"),
    "X": ("xnu-macos15", "43a90889846e00bfb5cf1d255cdc0a701a1e05a4", "apple-oss-distributions/xnu"),
    "Y": ("xnu", "f6217f891ac0bb64f3d375211650a4c1ff8ca1ea", "apple-oss-distributions/xnu"),
    "P": ("IOPCIFamily-macos15", "0eeb801b9d471778f5f177bd2e6c395a18f0ad95", "apple-oss-distributions/IOPCIFamily"),
    "Q": ("IOPCIFamily", "4822b27a36e2de70e231ecf2bf3021384fab6ec2", "apple-oss-distributions/IOPCIFamily"),
    "F": ("freebsd-14.3-research", "833d39bd2e38421a14ea489a956261a0fa993fbc", "freebsd/freebsd-src"),
    "D": ("drm-kmod-research", "c16de0f9fa84f70b9551f69fde837a7f3a73c0e9", "freebsd/drm-kmod"),
    "R": ("nvidia-macos-driver", "de9b33aa98dcb0ba6d5bf624fc3817e4a2d8bd66", "nullmoth/nvidia-macos-driver"),
    "N": ("open-gpu-kernel-modules", "e4a5faa2567f28c8eabe0ebb6422b6d0abcf37eb", "NVIDIA/open-gpu-kernel-modules"),
    "M": ("mesa/mesa-26.2.4", "mesa-26.2.4", "mesa"),
}
XE = "drivers/gpu/drm/xe/"
# IDs name specific evidence windows, not an exhaustive binary dependency closure.
ANCHORS = {
    "L01": (XE + "Kconfig", "config DRM_XE", 48),
    "L02": (XE + "Makefile", "xe-y += xe_bb.o", 105),
    "L03": (XE + "xe_module.c", "static const struct init_funcs init_funcs[]", 77),
    "L04": (XE + "xe_pci.c", "static int xe_pci_probe(", 125),
    "L05": (XE + "xe_pci.c", "static struct pci_driver xe_pci_driver", 30),
    "L06": ("include/drm/intel/pciids.h", "MACRO__(0x56A0", 22),
    "L07": (XE + "xe_pci.c", "static const struct xe_device_desc dg2_desc", 14),
    "L08": (XE + "xe_device.c", "int xe_device_probe(struct", 163),
    "L09": (XE + "xe_device.c", "static void xe_file_close(", 132),
    "L10": (XE + "xe_device.c", "void xe_device_remove(struct", 53),
    "L11": (XE + "xe_uc_fw.c", "#define XE_GUC_FIRMWARE_DEFS", 66),
    "L12": (XE + "xe_uc_fw.c", "err = request_firmware(", 78),
    "L13": (XE + "xe_uc.c", "int xe_uc_init_hw(struct", 43),
    "L14": (XE + "xe_uc.c", "void xe_uc_stop_prepare", 63),
    "L15": (XE + "xe_bo.c", "ret = dma_map_sgtable(", 23),
    "L16": (XE + "xe_hmm.c", "ret = hmm_range_fault(", 40),
    "L17": (XE + "xe_vm.c", "static bool vma_userptr_invalidate(", 32),
    "L18": (XE + "xe_vm.c", "err = mmu_interval_notifier_insert(", 43),
    "L19": (XE + "xe_exec.c", "Point of no return", 55),
    "L20": (XE + "xe_guc_submit.c", "int xe_guc_submit_reset_prepare(", 86),
    "L21": (XE + "xe_irq.c", "int xe_irq_install(", 59),
    "L22": (XE + "xe_hw_fence.c", "call_rcu(&dma_fence->rcu", 12),
    "L23": (XE + "xe_gt.c", "void xe_gt_remove(", 15),
    "L24": (XE + "xe_gt.c", "static int gt_reset(", 69),
    "L25": (XE + "xe_pm.c", "int xe_pm_suspend(", 100),
    "L26": (XE + "xe_exec_queue.c", "int xe_exec_queue_create_ioctl(", 130),
    "L27": (XE + "xe_sync.c", "int xe_sync_entry_parse(", 130),
    "L28": (XE + "xe_pt_walk.c", "SPDX-License-Identifier", 18),
    "L29": ("drivers/gpu/drm/drm_managed.c", "SPDX-License-Identifier", 75),
    "L30": ("drivers/gpu/drm/ttm/ttm_tt.c", "swap_storage = shmem_file_setup(", 51),
    "L31": ("drivers/gpu/drm/drm_gpuvm.c", "drm_gpuvm_exec_lock(struct", 35),
    "L32": ("mm/mmu_notifier.c", "mmu_interval_notifier_remove(", 75),
    "L33": ("drivers/gpu/drm/drm_syncobj.c", "Copyright 2017 Red Hat", 26),
    "L34": ("drivers/gpu/drm/scheduler/sched_main.c", "Copyright", 34),
    "L35": ("kernel/dma/mapping.c", "int dma_map_sgtable(", 44),
    "L36": ("drivers/gpu/drm/amd/amdgpu/Kconfig", "config DRM_AMDGPU", 57),
    "L37": (XE + "xe_gen_wa_oob.c", "static void print_usage(", 8),
    "L38": ("include/uapi/drm/xe_drm.h", "#define DRM_XE_DEVICE_QUERY", 38),
    "L39": (XE + "xe_pt.c", "SPDX-License-Identifier", 55),
    "L40": (XE + "xe_guc_submit.c", "guc_exec_queue_run_job(struct", 38),
    "L41": (XE + "xe_guc_submit.c", "static void __guc_exec_queue_fini_async", 48),
    "L42": (XE + "xe_hw_fence.c", "void xe_hw_fence_irq_finish", 42),
    "L43": (XE + "xe_vm.c", "int xe_vm_create_ioctl(", 85),
    "L44": (XE + "xe_bo.c", "#include <linux", 51),
    "L45": (XE + "xe_vm.c", "#include <linux", 46),
    "L46": (XE + "xe_pm.c", "int xe_pm_runtime_suspend(", 70),
    "L47": ("drivers/gpu/drm/drm_gpuvm.c", "SPDX-License-Identifier", 8),
    "L48": ("mm/hmm.c", "SPDX-License-Identifier", 24),
    "L49": ("drivers/pci/pci-driver.c", "int __pci_register_driver(", 24),
    "L50": (XE + "xe_module.c", "struct xe_modparam xe_modparam", 52),
    "L51": (XE + "xe_device.c", "int xe_device_probe_early(", 24),
    "L52": (XE + "xe_exec.c", "int xe_exec_ioctl(", 210),
    "L53": (XE + "xe_gt.c", "int xe_gt_init_hwconfig(", 71),
    "X01": ("iokit/IOKit/IODMACommand.h", "/*! @function setMemoryDescriptor", 47),
    "X02": ("iokit/IOKit/IOMemoryDescriptor.h", "/*! @function prepare", 51),
    "X03": ("iokit/IOKit/IOService.h", "virtual void PMinit", 24),
    "X04": ("config/Mach.exports", "_kernel_thread_start", 49),
    "X05": ("config/Libkern.exports", "_copyin", 62),
    "X06": ("config/BSDKernel.exports", "_cdevsw_add", 44),
    "X07": ("config/Unsupported.exports", "_get_task_map", 105),
    "X08": ("bsd/kern/kern_mman.c", "if (vp->v_type == VCHR", 8),
    "X09": ("iokit/Kernel/IOUserClient.cpp", "IOUserClient::clientDied", 32),
    "X10": ("iokit/Kernel/IODMACommand.cpp", "IODMACommand::prepare(", 90),
    "X11": ("iokit/Kernel/IOMemoryDescriptor.cpp", "IOMemoryDescriptor::createMappingInTask(", 65),
    "X12": ("iokit/Kernel/IOService.cpp", "IOService::handleOpen(", 35),
    "X13": ("iokit/IOKit/IOInterruptEventSource.h", "interruptEventSource(", 22),
    "X14": ("config/IOKit.exports", "__ZN12IODMACommand19setMemoryDescriptor", 15),
    "X15": ("iokit/Kernel/IOInterruptEventSource.cpp", "IOInterruptEventSource::checkForWork", 60),
    "X16": ("config/IOKit.x86_64.exports", "__ZN12IODMACommand17withSpecification", 14),
    "X17": ("config/Unsupported.exports", "_pmap_find_phys", 10),
    "X18": ("config/Libkern.exports", "_lck_mtx_lock", 22),
    "Y01": ("config/Mach.exports", "_kernel_thread_start", 49),
    "Y02": ("config/Unsupported.exports", "_get_task_map", 105),
    "P01": ("IOKit/pci/IOPCIDevice.h", "virtual IOReturn saveDeviceState", 81),
    "P02": ("IOPCIDevice.cpp", "IOPCIDevice::handleOpen(", 85),
    "Q01": ("IOKit/pci/IOPCIDevice.h", "virtual IOReturn saveDeviceState", 81),
    "F01": ("sys/compat/linuxkpi/common/include/linux/module.h", "Redistribution and use", 118),
    "F02": ("sys/compat/linuxkpi/common/include/linux/pci.h", "#include <sys/bus.h>", 52),
    "F03": ("sys/compat/linuxkpi/common/src/linux_rcu.c", "#include <ck_epoch.h>", 173),
    "F04": ("sys/compat/linuxkpi/common/src/linux_work.c", "linux_create_workqueue_common(", 46),
    "F05": ("sys/compat/linuxkpi/common/src/linux_schedule.c", "linux_add_to_sleepqueue(", 35),
    "F06": ("sys/compat/linuxkpi/common/src/linux_pci.c", "linux_dma_tag_init(struct", 60),
    "D01": ("linuxkpi/bsd/include/linux/dma-fence.h", "Copyright", 32),
    "D02": ("README.md", "The DRM drivers ported", 51),
    "R01": ("gpu-port/xkpi/override/src/linux_pci.c", "_linux_pci_register_driver(struct", 41),
    "R02": ("gpu-port/xkpi/override/src/linux_pci.c", "error = pdrv->probe(pdev, id);", 10),
    "R03": ("gpu-port/xkpi/xnu/nmx_kext.cpp", "void NMXGPU::stop(", 38),
    "R04": ("gpu-port/xkpi/override/src/nmx_linux_cdev.c", "static int umap_populate(", 29),
    "R05": ("gpu-port/README.md", "## Tests", 17),
    "R06": ("plugin/NVMTLDevice.m", "static void nvmtl_nvenc_register", 80),
    "R07": ("gpu-port/xkpi/cc.sh", "# cc.sh", 21),
    "R08": ("gpu-port/xkpi/kext/build.sh", "# kext/build.sh", 19),
    "R09": ("gpu-port/xkpi/xnu/nmx_devfs.cpp", "struct UserMap", 70),
    "R10": ("gpu-port/README.md", "Not done yet:", 17),
    "N01": ("README.md", "kernel modules built here must be used", 7),
    "M01": ("src/intel/vulkan/xe/anv_kmd_backend.c", "xe_gem_mmap(struct", 38),
    "M02": ("src/intel/vulkan/xe/anv_kmd_backend.c", "DRM_IOCTL_XE_VM_BIND", 15),
    "M03": ("src/intel/common/xe/intel_queue.c", "DRM_IOCTL_XE_EXEC", 20),
    "M04": ("src/intel/common/intel_bind_timeline.c", "DRM_IOCTL_SYNCOBJ_CREATE", 34),
}
MAX_TEXT_BYTES = 8 * 1024 * 1024
REQUIRED_DOCUMENTS = ("README", "target-baseline", "driver-dependencies", "driver-lifecycle", "darwin-kpi-map",
                      "porting-strategies", "vulkan-boundaries", "licensing", "build-deployment",
                      "verification-gates", "blockers", "decision", "evidence")

def read_text(path):
    if path.stat().st_size > MAX_TEXT_BYTES:
        raise ValueError(f"Oversized research input: {path}")
    return path.read_text(encoding="utf-8")

def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], text=True, timeout=30).strip()

def collect(references):
    for key, (folder, revision, _) in BASELINES.items():
        root = references / folder
        if key == "M":
            continue  # Release archive, not a git checkout; per-file hashes below.
        if git(root, "rev-parse", "HEAD") != revision or git(root, "status", "--porcelain"):
            raise ValueError(f"Reference is not clean at pinned revision: {folder}")
    evidence = {}
    for name, (relative, needle, count) in ANCHORS.items():
        folder, revision, upstream = BASELINES[name[0]]
        path = references / folder / relative
        text = read_text(path)
        lines = text.splitlines()
        matches = [i for i, line in enumerate(lines) if needle in line]
        if not matches:
            raise ValueError(f"Missing source anchor {name}: {needle}")
        start = matches[0] + 1
        end = min(len(lines), start + count - 1)
        url = (f"https://gitlab.freedesktop.org/mesa/mesa/-/blob/{revision}/{relative}#L{start}-{end}"
               if name[0] == "M" else
               f"https://github.com/{upstream}/blob/{revision}/{relative}#L{start}-L{end}")
        evidence[name] = dict(folder=folder, revision=revision, path=relative, needle=needle,
                              start=start, end=end, sha256=hashlib.sha256(text.encode("utf-8")).hexdigest(),
                              url=url)
    driver = references / BASELINES["L"][0] / XE
    files = {}
    licenses = Counter()
    for path in sorted(driver.rglob("*")):
        if path.suffix not in (".c", ".h") or not path.is_file():
            continue
        text = read_text(path)
        spdx = re.search(r"SPDX-License-Identifier:\s*([^\r\n]+)", text)
        license_id = spdx.group(1).removesuffix(" */").strip() if spdx else "NO_SPDX"
        licenses[license_id] += 1
        files[path.relative_to(driver).as_posix()] = dict(
            spdx=license_id, sha256=hashlib.sha256(text.encode("utf-8")).hexdigest(),
            includes=re.findall(r'^#\s*include\s*[<"]([^>"\n]+)', text, re.MULTILINE))
    return dict(baselines=BASELINES, evidence=evidence, driver_files=files,
                license_counts=dict(sorted(licenses.items())),
                hash_format="SHA-256 of UTF-8 text with universal newlines normalized to LF",
                coverage="Lexical C/H include/SPDX inventory, including optional/tests; not preprocessed or link closure")

def check_documents(directory, metadata):
    for name in REQUIRED_DOCUMENTS:
        if not (directory / f"{name}.md").is_file():
            raise ValueError(f"Missing deliverable: {name}")
    count = 0
    for path in directory.glob("*.md"):
        text = read_text(path)
        for evidence_id in re.findall(r"\[([LXYPQFDRNM]\d{2})\]", text):
            if evidence_id not in metadata["evidence"]:
                raise ValueError(f"Unknown evidence ID {evidence_id}: {path}")
        for destination in re.findall(r"\]\(([^)]+)\)", text):
            if destination.startswith(("https://", "http://", "#")):
                continue
            local = destination.split("#", 1)[0]
            if local and not (path.parent / local).exists():
                raise ValueError(f"Broken local link {destination}: {path}")
        if re.search(r"\b(TODO|TBD|FIXME)\b", text):
            raise ValueError(f"Unresolved placeholder: {path}")
        count += 1
    print(f"PASS {count} documents: required deliverables, local file links and evidence IDs")

def render_evidence(metadata):
    rows = ["# 固定来源证据索引", "", "由 `audit_sources.py --write-evidence` 从固定本地源码生成。",
            "只保存路径、版本、行号、hash 和 include 元数据，不复制上游实现。",
            "所有行号以完整 revision 为准；窗口不是完整函数或依赖闭包。", "",
            "| ID | 固定来源与行号 | 核查锚点 |", "|---|---|---|"]
    for name, record in metadata["evidence"].items():
        rows.append(f"| {name} | [{record['folder']}/{record['path']}:{record['start']}-{record['end']}]({record['url']}) | `{record['needle']}` |")
    return "\n".join(rows) + "\n"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--references", type=Path, required=True)
    parser.add_argument("--write-evidence", action="store_true", help="Generate research metadata, no source copies")
    args = parser.parse_args()
    directory = Path(__file__).resolve().parent
    metadata = collect(args.references.resolve())
    target = directory / "source-inventory.json"
    serialized = json.dumps(metadata, ensure_ascii=False, sort_keys=True, indent=2) + "\n"
    evidence_text = render_evidence(metadata)
    if args.write_evidence:
        target.write_text(serialized, encoding="utf-8", newline="\n")
        (directory / "evidence.md").write_text(evidence_text, encoding="utf-8", newline="\n")
    elif read_text(target) != serialized:
        raise ValueError("Pinned source metadata differs; investigate rather than silently regenerate")
    elif read_text(directory / "evidence.md") != evidence_text:
        raise ValueError("Source evidence index differs from verified metadata")
    check_documents(directory, metadata)
    print(f"PASS {len(metadata['evidence'])} source windows and {len(metadata['driver_files'])} xe C/H metadata records")
    print("License counts (includes optional/display/tests):", metadata["license_counts"])
    print("No Darwin compile, kext load, IPC success or hardware execution is tested by this script")

if __name__ == "__main__":
    main()
