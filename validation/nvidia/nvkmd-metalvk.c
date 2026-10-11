// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
/* User-space nvkmd adapter for the metalvk NVIDIA transport. This is the
 * metalvk side of the shared standard, compiled inside the pinned Mesa tree so
 * it is checked against the real nvkmd types; it is not an upstream file.
 *
 * It converts one validated DEVICE_QUERY message into a nvkmd_pdev and fails
 * closed for the execution classes: create_dev returns VK_ERROR_INCOMPATIBLE_
 * DRIVER until the Darwin kernel endpoint genuinely backs memory, GPU VA and
 * submission. No device is advertised on the strength of a probe. */
#include "nvkmd/nvkmd.h"
#include "nv_device_info.h"
#include "nvidia/discovery.h"
#include "nvidia/transport.h"
#include "util/macros.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

struct mtvk_nvkmd_pdev {
    struct nvkmd_pdev base;
    /* Retained so create_dev can apply the VA range once the kernel endpoint
     * backs GPU VA; nvkmd_pdev itself has no place to publish it. */
    mtvk_nv_device_info info;
};

VkResult mtvk_nvkmd_open(const void *message, size_t size,
                         const struct vk_sync_type *const *sync_types,
                         struct nvkmd_pdev **out);

static void copy_name(char *out, size_t bytes, const char *in) {
    size_t index = 0;
    for (; index + 1u < bytes && in[index] != '\0'; ++index) out[index] = in[index];
    out[index] = '\0';
}

/* Fixed, auditable field mapping; zcull order follows nv_zcull_device_info. */
static void fill_device_info(const mtvk_nv_device_info *in, struct nv_device_info *out) {
    memset(out, 0, sizeof(*out));
    out->type = in->device_type == (uint32_t)MTVK_NV_DEVICE_DISCRETE ? NV_DEVICE_TYPE_DIS :
                in->device_type == (uint32_t)MTVK_NV_DEVICE_SOC ? NV_DEVICE_TYPE_SOC : NV_DEVICE_TYPE_IGP;
    out->device_id = in->device_id;
    out->chipset = in->chipset;
    copy_name(out->device_name, sizeof(out->device_name), in->device_name);
    copy_name(out->chipset_name, sizeof(out->chipset_name), in->chipset_name);
    out->pci.domain = in->pci_domain;
    out->pci.bus = in->pci_bus;
    out->pci.dev = in->pci_device;
    out->pci.func = in->pci_function;
    out->pci.revision_id = in->pci_revision;
    out->sm = in->sm;
    out->gpc_count = in->gpc_count;
    out->tpc_count = in->tpc_count;
    out->mp_per_tpc = in->mp_per_tpc;
    out->max_warps_per_mp = in->max_warps_per_mp;
    out->has_transfer_queue = in->has_transfer_queue != 0;
    out->nc_atom_size_B = in->nc_atom_size_B;
    out->cls_copy = in->cls_copy;
    out->cls_eng2d = in->cls_eng2d;
    out->cls_eng3d = in->cls_eng3d;
    out->cls_m2mf = in->cls_m2mf;
    out->cls_compute = in->cls_compute;
    out->cls_gpfifo = in->cls_gpfifo;
    out->vram_size_B = in->vram_size_B;
    out->bar_size_B = in->bar_size_B;
    out->max_smem_per_wg_kB = in->max_smem_per_wg_kB;
    for (unsigned i = 0; i < MTVK_NV_SMEM_CONFIGS; ++i)
        out->sm_smem_sizes_kB[i] = in->sm_smem_sizes_kB[i];
    out->sm_smem_size_count = in->sm_smem_size_count;
    out->zcull_info.width_align_pixels = in->zcull_info[0];
    out->zcull_info.height_align_pixels = in->zcull_info[1];
    out->zcull_info.pixel_squares_by_aliquots = in->zcull_info[2];
    out->zcull_info.aliquot_total = in->zcull_info[3];
    out->zcull_info.zcull_region_byte_multiplier = in->zcull_info[4];
    out->zcull_info.zcull_region_header_size = in->zcull_info[5];
    out->zcull_info.zcull_subregion_header_size = in->zcull_info[6];
    out->zcull_info.subregion_count = in->zcull_info[7];
    out->zcull_info.subregion_width_align_pixels = in->zcull_info[8];
    out->zcull_info.subregion_height_align_pixels = in->zcull_info[9];
    out->zcull_info.ctxsw_size = in->zcull_info[10];
    out->zcull_info.ctxsw_align = in->zcull_info[11];
    out->has_zcull_info = in->has_zcull_info != 0;
}

static void pdev_destroy(struct nvkmd_pdev *pdev) {
    free(container_of(pdev, struct mtvk_nvkmd_pdev, base));
}

/* No kernel memory accounting exists yet, so this is reported as unavailable in
 * kmd_info rather than publishing a false zero-cost usage figure. */
static uint64_t pdev_get_vram_used(struct nvkmd_pdev *pdev) {
    (void)pdev;
    return 0;
}

static int pdev_get_drm_primary_fd(struct nvkmd_pdev *pdev) {
    (void)pdev;
    return -1; /* No DRM primary node on Darwin. */
}

static VkResult pdev_create_dev(struct nvkmd_pdev *pdev, struct vk_object_base *log_obj,
                                struct nvkmd_dev **dev_out) {
    (void)pdev;
    (void)log_obj;
    if (dev_out) *dev_out = NULL; /* A failed create never publishes a handle. */
    return VK_ERROR_INCOMPATIBLE_DRIVER;
}

static const struct nvkmd_pdev_ops metalvk_pdev_ops = {
    .destroy = pdev_destroy,
    .get_vram_used = pdev_get_vram_used,
    .get_drm_primary_fd = pdev_get_drm_primary_fd,
    .create_dev = pdev_create_dev,
};

static const struct vk_sync_type *const no_sync_types[1] = { NULL };

VkResult mtvk_nvkmd_open(const void *message, size_t size,
                         const struct vk_sync_type *const *sync_types,
                         struct nvkmd_pdev **out) {
    if (!out) return VK_ERROR_INITIALIZATION_FAILED;
    *out = NULL;
    mtvk_nv_request_view view;
    if (mtvk_nv_transport_validate(message, size, &view) != MTVK_OK)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (view.header.op != (uint32_t)MTVK_NV_OP_DEVICE_QUERY)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (view.payload_bytes != (uint32_t)sizeof(mtvk_nv_device_info))
        return VK_ERROR_INITIALIZATION_FAILED;
    struct mtvk_nvkmd_pdev *pdev = calloc(1, sizeof(*pdev));
    if (!pdev) return VK_ERROR_OUT_OF_HOST_MEMORY;
    mtvk_nv_device_info info;
    memcpy(&info, view.payload, sizeof(info));
    pdev->info = info;
    fill_device_info(&info, &pdev->base.dev_info);
    pdev->base.ops = &metalvk_pdev_ops;
    /* Capabilities are passed through from the kernel; it must clear the
     * Linux-only fd/compression flags until the matching op is actually backed. */
    pdev->base.kmd_info = (struct nvkmd_info) {
        .has_dma_buf = info.has_dma_buf != 0,
        .has_get_vram_used = false,
        .has_alloc_tiled = info.has_alloc_tiled != 0,
        .has_map_fixed = info.has_map_fixed != 0,
        .has_overmap = info.has_overmap != 0,
        .has_compression = info.has_compression != 0,
    };
    pdev->base.bind_align_B = info.bind_align_B;
    pdev->base.sync_types = sync_types ? sync_types : no_sync_types;
    *out = &pdev->base;
    return VK_SUCCESS;
}

static void fixture(mtvk_nv_device_info *info) {
    memset(info, 0, sizeof(*info));
    /* Constructed transport fixture; the values are inputs to the mapping test,
     * not a claim that any hardware was queried or executed. */
    info->device_type = (uint32_t)MTVK_NV_DEVICE_DISCRETE;
    info->device_id = 0x28e0u;
    info->chipset = 0x0197u;
    info->pci_bus = 1u;
    info->pci_revision = 0xa1u;
    info->sm = 89u;
    info->gpc_count = 3u;
    info->tpc_count = 12u;
    info->mp_per_tpc = 1u;
    info->max_warps_per_mp = 48u;
    info->has_transfer_queue = 1u;
    info->nc_atom_size_B = 64u;
    info->cls_copy = 0xc7b5u;
    info->cls_eng2d = 0xc5b5u;
    info->cls_eng3d = 0xc997u;
    info->cls_m2mf = 0xc5b0u;
    info->cls_compute = 0xc7c0u;
    info->cls_gpfifo = 0xc46fu;
    info->vram_size_B = UINT64_C(8589934592);
    info->bar_size_B = UINT64_C(268435456);
    info->max_smem_per_wg_kB = 100u;
    info->sm_smem_size_count = 2u;
    info->sm_smem_sizes_kB[0] = 0u;
    info->sm_smem_sizes_kB[1] = 32u;
    copy_name(info->device_name, sizeof(info->device_name), "AD107 transport fixture");
    copy_name(info->chipset_name, sizeof(info->chipset_name), "AD107");
    info->bind_align_B = 4096u;
    info->va_start = UINT64_C(0x1000);
    info->va_end = UINT64_C(0x1000000000);
}

static uint32_t payload_bytes(void) {
    return (uint32_t)((sizeof(mtvk_nv_device_info) + 3u) & ~(size_t)3u);
}

static size_t build_query(uint8_t *message, size_t capacity, uint32_t payload,
                          const mtvk_nv_device_info *info) {
    size_t written = 0;
    if (mtvk_nv_request_init(message, capacity, (uint32_t)MTVK_NV_OP_DEVICE_QUERY,
                             payload, 0u, &written) != MTVK_OK) return 0;
    memcpy(message + sizeof(mtvk_nv_request), info, sizeof(*info));
    return written;
}

int main(void) {
    mtvk_nv_device_info info;
    fixture(&info);
    const uint32_t payload = payload_bytes();
    uint8_t message[sizeof(mtvk_nv_request) + sizeof(mtvk_nv_device_info) + 4u];
    memset(message, 0, sizeof(message));
    const size_t size = build_query(message, sizeof(message), payload, &info);
    CHECK(size == sizeof(mtvk_nv_request) + payload);

    /* Positive path: the standard decodes and maps every field, but the device
     * class still fails closed because no execution op is backed. */
    struct nvkmd_pdev *pdev = NULL;
    CHECK(mtvk_nvkmd_open(message, size, no_sync_types, &pdev) == VK_SUCCESS && pdev != NULL);
    CHECK(pdev->dev_info.device_id == info.device_id && pdev->dev_info.chipset == info.chipset);
    CHECK(pdev->dev_info.sm == info.sm && pdev->dev_info.gpc_count == info.gpc_count &&
          pdev->dev_info.tpc_count == info.tpc_count && pdev->dev_info.cls_eng3d == info.cls_eng3d);
    CHECK(pdev->dev_info.vram_size_B == info.vram_size_B && pdev->dev_info.bar_size_B == info.bar_size_B);
    CHECK(pdev->dev_info.pci.revision_id == info.pci_revision && pdev->dev_info.sm_smem_size_count == 2);
    CHECK(strcmp(pdev->dev_info.device_name, info.device_name) == 0);
    CHECK(strcmp(pdev->dev_info.chipset_name, info.chipset_name) == 0);
    CHECK(pdev->bind_align_B == info.bind_align_B);
    struct mtvk_nvkmd_pdev *private_pdev = container_of(pdev, struct mtvk_nvkmd_pdev, base);
    CHECK(private_pdev->info.va_start == info.va_start && private_pdev->info.va_end == info.va_end);
    CHECK(pdev->sync_types == no_sync_types);
    CHECK(!pdev->kmd_info.has_get_vram_used && !pdev->kmd_info.has_dma_buf);
    struct nvkmd_dev *dev = NULL;
    CHECK(nvkmd_pdev_create_dev(pdev, NULL, &dev) == VK_ERROR_INCOMPATIBLE_DRIVER && dev == NULL);
    nvkmd_pdev_destroy(pdev);

    /* Negative paths: a bad envelope, a wrong op, a wrong payload size and a
     * missing output must all be refused without publishing a pdev. */
    pdev = NULL;
    CHECK(mtvk_nvkmd_open(message, 5u, no_sync_types, &pdev) == VK_ERROR_INITIALIZATION_FAILED && pdev == NULL);
    CHECK(mtvk_nvkmd_open(message, size - 1u, no_sync_types, &pdev) == VK_ERROR_INITIALIZATION_FAILED && pdev == NULL);
    CHECK(mtvk_nvkmd_open(message, size, no_sync_types, NULL) == VK_ERROR_INITIALIZATION_FAILED);

    uint8_t wrong_op[sizeof(message)];
    memcpy(wrong_op, message, sizeof(message));
    size_t written = 0;
    CHECK(mtvk_nv_request_init(wrong_op, sizeof(wrong_op), (uint32_t)MTVK_NV_OP_MEMORY_MAP,
                               payload, 0u, &written) == MTVK_OK);
    memcpy(wrong_op + sizeof(mtvk_nv_request), &info, sizeof(info));
    CHECK(mtvk_nvkmd_open(wrong_op, written, no_sync_types, &pdev) == VK_ERROR_INITIALIZATION_FAILED && pdev == NULL);

    uint8_t short_payload[sizeof(message)];
    memcpy(short_payload, message, sizeof(message));
    CHECK(mtvk_nv_request_init(short_payload, sizeof(short_payload), (uint32_t)MTVK_NV_OP_DEVICE_QUERY,
                               payload - 4u, 0u, &written) == MTVK_OK);
    CHECK(mtvk_nvkmd_open(short_payload, written, no_sync_types, &pdev) == VK_ERROR_INITIALIZATION_FAILED && pdev == NULL);

    puts("PASS user-space nvkmd adapter maps a validated DEVICE_QUERY and fails closed for execution");
    return 0;
}

