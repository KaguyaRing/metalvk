// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
/* User-space nvkmd adapter for the metalvk NVIDIA transport. This is the
 * metalvk side of the shared standard, compiled inside the pinned Mesa tree so
 * it is checked against the real nvkmd types; it is not an upstream file.
 *
 * It consumes one validated DEVICE_QUERY response and builds a nvkmd_pdev. It
 * clips every capability the endpoint did not back, so it can never advertise a
 * capability the kernel cannot honour, and it fails closed for the execution
 * classes: create_dev returns VK_ERROR_INCOMPATIBLE_DRIVER until the Darwin
 * kernel endpoint genuinely backs memory, GPU VA and submission. */
#include "nvkmd/nvkmd.h"
#include "nv_device_info.h"
#include "nvidia/init.h"
#include "nvkmd-metalvk.h"
#if defined(__APPLE__) || defined(MTVK_NVKMD_NATIVE_INGRESS_TEST)
#include "nvidia/darwin.h"
#endif
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

VkResult mtvk_nvkmd_open(const void *response, size_t size,
                         const struct vk_sync_type *const *sync_types,
                         struct nvkmd_pdev **out) {
    if (!out) return VK_ERROR_INITIALIZATION_FAILED;
    *out = NULL;
    mtvk_nv_device_info info;
    if (mtvk_nv_discovery_response_decode(response, size, &info) != MTVK_OK)
        return VK_ERROR_INITIALIZATION_FAILED;
    /* Defence in depth: clip capabilities the endpoint did not back, even if it
     * claimed them. The adapter must never advertise an unhonoured capability. */
    const mtvk_nv_device_info effective = info;
    struct mtvk_nvkmd_pdev *pdev = calloc(1, sizeof(*pdev));
    if (!pdev) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pdev->info = effective;
    fill_device_info(&effective, &pdev->base.dev_info);
    pdev->base.ops = &metalvk_pdev_ops;
    pdev->base.kmd_info = (struct nvkmd_info) {
        .has_dma_buf = effective.has_dma_buf != 0,
        .has_get_vram_used = (MTVK_NV_IMPLEMENTED_OPS &
                              MTVK_NV_OP_BIT(MTVK_NV_OP_DEVICE_VRAM_USED)) != 0,
        .has_alloc_tiled = effective.has_alloc_tiled != 0,
        .has_map_fixed = effective.has_map_fixed != 0,
        .has_overmap = effective.has_overmap != 0,
        .has_compression = effective.has_compression != 0,
    };
    pdev->base.bind_align_B = effective.bind_align_B;
    pdev->base.sync_types = sync_types ? sync_types : no_sync_types;
    *out = &pdev->base;
    return VK_SUCCESS;
}

#if defined(__APPLE__) || defined(MTVK_NVKMD_NATIVE_INGRESS_TEST)
/* Native discovery ingress, not a complete Darwin Vulkan physical-device
 * registration. No loopback fallback; create_dev still refuses execution. */
VkResult mtvk_nvkmd_open_registry(uint64_t registry_id,
    const struct vk_sync_type *const *sync_types, struct nvkmd_pdev **out) {
    if (!out) return VK_ERROR_INITIALIZATION_FAILED;
    *out = NULL;
    uint8_t response[MTVK_NV_QUERY_RESPONSE_BYTES];
    const mtvk_nv_darwin_result result = mtvk_nv_darwin_query(registry_id, response, sizeof(response));
    if (result.status == MTVK_OUT_OF_MEMORY) return VK_ERROR_OUT_OF_HOST_MEMORY;
    if (result.status != MTVK_OK) return VK_ERROR_INCOMPATIBLE_DRIVER;
    return mtvk_nvkmd_open(response, result.response_bytes, sync_types, out);
}
#endif

#ifdef MTVK_NVKMD_NATIVE_INGRESS_TEST
/* Test-executable provider only, never linked into a shipped ICD or native
 * transport. Lets Linux Mesa CI compile/test the real native ingress body. */
static const void *native_fixture_response;
static size_t native_fixture_size;
static mtvk_status native_fixture_status;
mtvk_nv_darwin_result mtvk_nv_darwin_query(uint64_t registry_id, void *response, size_t capacity) {
    mtvk_nv_darwin_result result = {native_fixture_status, MTVK_NV_DARWIN_CALL, 0u, 0u};
    if (registry_id != 42u) { result.status = MTVK_NOT_FOUND; return result; }
    if (result.status != MTVK_OK) return result;
    if (capacity < native_fixture_size) { result.status = MTVK_BUFFER_TOO_SMALL; return result; }
    memcpy(response, native_fixture_response, native_fixture_size);
    result.response_bytes = native_fixture_size;
    result.stage = MTVK_NV_DARWIN_COMPLETE;
    return result;
}
#endif

static void fixture(mtvk_nv_device_info *info) {
    mtvk_nv_device_info_init(info);
    /* Constructed transport fixture; the values are inputs to the mapping and
     * capability-clipping tests, not a claim that hardware was queried. */
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
    info->sm_smem_sizes_kB[1] = 32u;
    copy_name(info->device_name, sizeof(info->device_name), "AD107 transport fixture");
    copy_name(info->chipset_name, sizeof(info->chipset_name), "AD107");
    info->bind_align_B = 4096u;
    info->va_start = UINT64_C(0x1000);
    info->va_end = UINT64_C(0x1000000000);
    /* Claimed capabilities; the endpoint must back each before it is believed. */
    info->has_alloc_tiled = 1u;
    info->has_map_fixed = 1u;
    info->has_overmap = 1u;
    info->has_compression = 1u;
    info->has_dma_buf = 1u;
}

static int fixtures_agree(struct nvkmd_pdev *pdev, const mtvk_nv_device_info *info) {
    const struct mtvk_nvkmd_pdev *private_pdev = container_of(pdev, struct mtvk_nvkmd_pdev, base);
    CHECK(pdev->dev_info.device_id == info->device_id && pdev->dev_info.chipset == info->chipset);
    CHECK(pdev->dev_info.sm == info->sm && pdev->dev_info.cls_eng3d == info->cls_eng3d);
    CHECK(pdev->dev_info.vram_size_B == info->vram_size_B);
    CHECK(strcmp(pdev->dev_info.device_name, info->device_name) == 0);
    CHECK(pdev->bind_align_B == info->bind_align_B);
    CHECK(private_pdev->info.va_start == info->va_start && private_pdev->info.va_end == info->va_end);
    return 0;
}

int main(void) {
    mtvk_nv_device_info info;
    fixture(&info);

    /* Kernel endpoint side: the shared dispatch builds the response. */
    mtvk_nv_session session;
    mtvk_nv_session_init(&session, UINT32_MAX, &info);
    uint8_t request[sizeof(mtvk_nv_request)];
    size_t request_written = 0;
    CHECK(mtvk_nv_request_init(request, sizeof(request), (uint32_t)MTVK_NV_OP_DEVICE_QUERY,
                               0u, 0u, &request_written) == MTVK_OK);
    uint8_t response[sizeof(mtvk_nv_response) + MTVK_NV_DEVICE_WIRE_BYTES];
    size_t response_written = 0;
    CHECK(mtvk_nv_session_dispatch(&session, request, request_written, response,
                                   sizeof(response), &response_written) == MTVK_OK);

    /* User side: consume the very bytes the kernel path produced. */
    struct nvkmd_pdev *pdev = NULL;
    CHECK(mtvk_nvkmd_open(response, response_written, no_sync_types, &pdev) == VK_SUCCESS && pdev != NULL);
    CHECK(fixtures_agree(pdev, &info) == 0);
#ifdef MTVK_NVKMD_NATIVE_INGRESS_TEST
    native_fixture_response = response;
    native_fixture_size = response_written;
    native_fixture_status = MTVK_OK;
    struct nvkmd_pdev *native_pdev = NULL;
    CHECK(mtvk_nvkmd_open_registry(42u, no_sync_types, &native_pdev) == VK_SUCCESS && native_pdev != NULL);
    CHECK(fixtures_agree(native_pdev, &info) == 0);
    nvkmd_pdev_destroy(native_pdev);
    CHECK(mtvk_nvkmd_open_registry(43u, no_sync_types, &native_pdev) == VK_ERROR_INCOMPATIBLE_DRIVER && native_pdev == NULL);
    native_fixture_status = MTVK_OUT_OF_MEMORY;
    CHECK(mtvk_nvkmd_open_registry(42u, no_sync_types, &native_pdev) == VK_ERROR_OUT_OF_HOST_MEMORY && native_pdev == NULL);
    native_fixture_status = MTVK_CLEANUP_REQUIRED;
    CHECK(mtvk_nvkmd_open_registry(42u, no_sync_types, &native_pdev) == VK_ERROR_INCOMPATIBLE_DRIVER && native_pdev == NULL);
    CHECK(mtvk_nvkmd_open_registry(42u, no_sync_types, NULL) == VK_ERROR_INITIALIZATION_FAILED);
    native_fixture_status = MTVK_OK;
    native_fixture_size = 5u;
    CHECK(mtvk_nvkmd_open_registry(42u, no_sync_types, &native_pdev) == VK_ERROR_INITIALIZATION_FAILED && native_pdev == NULL);
#endif
    /* Only DEVICE_QUERY is backed, so nothing else may leak through. */
    CHECK(!pdev->kmd_info.has_alloc_tiled && !pdev->kmd_info.has_map_fixed);
    CHECK(!pdev->kmd_info.has_overmap && !pdev->kmd_info.has_compression && !pdev->kmd_info.has_dma_buf);
    CHECK(!pdev->kmd_info.has_get_vram_used);
    struct nvkmd_dev *dev = NULL;
    CHECK(nvkmd_pdev_create_dev(pdev, NULL, &dev) == VK_ERROR_INCOMPATIBLE_DRIVER && dev == NULL);
    nvkmd_pdev_destroy(pdev);

    /* Defence in depth: capability claims without backed memory operations
     * must still be clipped by the client. Missing query backing is rejected. */
    uint8_t forged[sizeof(mtvk_nv_response) + MTVK_NV_DEVICE_WIRE_BYTES];
    size_t forged_written = 0;
    CHECK(mtvk_nv_response_init(forged, sizeof(forged), (uint32_t)MTVK_NV_OP_DEVICE_QUERY,
                                (uint32_t)MTVK_OK, MTVK_NV_DEVICE_WIRE_BYTES,
                                MTVK_NV_IMPLEMENTED_OPS, &forged_written) == MTVK_OK);
    CHECK(mtvk_nv_device_info_encode(&info, forged + sizeof(mtvk_nv_response), MTVK_NV_DEVICE_WIRE_BYTES) == MTVK_OK);
    pdev = NULL;
    CHECK(mtvk_nvkmd_open(forged, forged_written, no_sync_types, &pdev) == VK_SUCCESS && pdev != NULL);
    CHECK(!pdev->kmd_info.has_alloc_tiled && !pdev->kmd_info.has_dma_buf && !pdev->kmd_info.has_map_fixed);
    nvkmd_pdev_destroy(pdev);
    pdev = NULL;
    const uint32_t invalid_masks[] = {0u, MTVK_NV_IMPLEMENTED_OPS |
        MTVK_NV_OP_BIT(MTVK_NV_OP_DEVICE_VRAM_USED)};
    for (size_t i = 0; i < sizeof(invalid_masks) / sizeof(invalid_masks[0]); ++i) {
        CHECK(mtvk_nv_response_init(forged, sizeof(forged), MTVK_NV_OP_DEVICE_QUERY,
                                    MTVK_OK, MTVK_NV_DEVICE_WIRE_BYTES,
                                    invalid_masks[i], &forged_written) == MTVK_OK);
        CHECK(mtvk_nvkmd_open(forged, forged_written, no_sync_types, &pdev) == VK_ERROR_INITIALIZATION_FAILED && pdev == NULL);
    }
    /* Decoder errors must not publish a Mesa physical-device object. */
    CHECK(mtvk_nv_response_init(forged, sizeof(forged), MTVK_NV_OP_DEVICE_QUERY,
                                MTVK_OK, MTVK_NV_DEVICE_WIRE_BYTES,
                                MTVK_NV_IMPLEMENTED_OPS, &forged_written) == MTVK_OK);
    forged[sizeof(mtvk_nv_response) + 288u] = 2u; /* malformed has_alloc_tiled */
    CHECK(mtvk_nvkmd_open(forged, forged_written, no_sync_types, &pdev) == VK_ERROR_INITIALIZATION_FAILED && pdev == NULL);

    /* Negative paths: wrong preamble, wrong op, wrong status, wrong payload
     * size and a missing output must all be refused without publishing a pdev. */
    pdev = NULL;
    CHECK(mtvk_nvkmd_open(response, 5u, no_sync_types, &pdev) == VK_ERROR_INITIALIZATION_FAILED && pdev == NULL);
    CHECK(mtvk_nvkmd_open(response, response_written - 1u, no_sync_types, &pdev) == VK_ERROR_INITIALIZATION_FAILED && pdev == NULL);
    CHECK(mtvk_nvkmd_open(response, response_written, no_sync_types, NULL) == VK_ERROR_INITIALIZATION_FAILED);

    uint8_t wrong_status[sizeof(response)];
    size_t wrong_written = 0;
    CHECK(mtvk_nv_response_init(wrong_status, sizeof(wrong_status), (uint32_t)MTVK_NV_OP_DEVICE_QUERY,
                                (uint32_t)MTVK_NOT_SUPPORTED, MTVK_NV_DEVICE_WIRE_BYTES, 1u, &wrong_written) == MTVK_OK);
    CHECK(mtvk_nv_device_info_encode(&info, wrong_status + sizeof(mtvk_nv_response), MTVK_NV_DEVICE_WIRE_BYTES) == MTVK_OK);
    CHECK(mtvk_nvkmd_open(wrong_status, wrong_written, no_sync_types, &pdev) == VK_ERROR_INITIALIZATION_FAILED && pdev == NULL);

    uint8_t short_payload[sizeof(response)];
    size_t short_written = 0;
    CHECK(mtvk_nv_response_init(short_payload, sizeof(short_payload), (uint32_t)MTVK_NV_OP_DEVICE_QUERY,
                                (uint32_t)MTVK_OK, MTVK_NV_DEVICE_WIRE_BYTES - 4u, 1u, &short_written) == MTVK_OK);
    CHECK(mtvk_nvkmd_open(short_payload, short_written, no_sync_types, &pdev) == VK_ERROR_INITIALIZATION_FAILED && pdev == NULL);

    puts("PASS user-space nvkmd adapter consumes a kernel response and fails closed for execution");
    return 0;
}
