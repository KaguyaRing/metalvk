// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
/* Dependency-free discovery initialization and capability projection.
 * Session dispatch and the user-space client use the same policy. */
#include "nvidia/discovery.h"
#include "nvidia/transport.h"
#include "base/span.h"

void mtvk_nv_device_info_init(mtvk_nv_device_info *info) {
    if (!info) return;
    uint8_t *bytes = (uint8_t *)info;
    for (size_t index = 0; index < sizeof(*info); ++index) bytes[index] = 0;
}

mtvk_nv_device_info mtvk_nv_effective_device_info(const mtvk_nv_device_info *device,
                                                uint32_t backed_op_mask) {
    mtvk_nv_device_info out;
    mtvk_nv_device_info_init(&out);
    if (!device) return out;
    out = *device;
    if (!(backed_op_mask & MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_ALLOC_TILED))) out.has_alloc_tiled = 0u;
    if (!(backed_op_mask & MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_MAP))) out.has_map_fixed = 0u;
    if (!(backed_op_mask & MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_OVERMAP))) out.has_overmap = 0u;
    if (!(backed_op_mask & MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_ALLOC_TILED))) out.has_compression = 0u;
    const uint32_t dma_buf_ops = MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_IMPORT) |
                                MTVK_NV_OP_BIT(MTVK_NV_OP_MEMORY_EXPORT);
    if ((backed_op_mask & dma_buf_ops) != dma_buf_ops) out.has_dma_buf = 0u;
    return out;
}

/* Single field order for both directions. Every narrow scalar occupies a
 * dword on the wire; explicit range checks precede narrowing. */
#define DEVICE_FIELDS(U32, U64, ARRAY, NAME) \
    U32(device_type, uint32_t, MTVK_NV_DEVICE_SOC) \
    U32(device_id, uint16_t, UINT16_MAX) \
    U32(chipset, uint16_t, UINT16_MAX) \
    U32(pci_domain, uint16_t, UINT16_MAX) \
    U32(pci_bus, uint8_t, UINT8_MAX) \
    U32(pci_device, uint8_t, 31u) \
    U32(pci_function, uint8_t, 7u) \
    U32(pci_revision, uint8_t, UINT8_MAX) \
    U32(sm, uint8_t, UINT8_MAX) \
    U32(gpc_count, uint8_t, UINT8_MAX) \
    U32(tpc_count, uint16_t, UINT16_MAX) \
    U32(mp_per_tpc, uint8_t, UINT8_MAX) \
    U32(max_warps_per_mp, uint8_t, UINT8_MAX) \
    U32(has_transfer_queue, uint8_t, 1u) \
    U32(has_zcull_info, uint8_t, 1u) \
    U32(nc_atom_size_B, uint16_t, UINT16_MAX) \
    U32(cls_copy, uint16_t, UINT16_MAX) \
    U32(cls_eng2d, uint16_t, UINT16_MAX) \
    U32(cls_eng3d, uint16_t, UINT16_MAX) \
    U32(cls_m2mf, uint16_t, UINT16_MAX) \
    U32(cls_compute, uint16_t, UINT16_MAX) \
    U32(cls_gpfifo, uint16_t, UINT16_MAX) \
    U64(vram_size_B) \
    U64(bar_size_B) \
    U32(max_smem_per_wg_kB, uint16_t, UINT16_MAX) \
    ARRAY(sm_smem_sizes_kB, uint16_t, UINT16_MAX, MTVK_NV_SMEM_CONFIGS) \
    U32(sm_smem_size_count, uint8_t, MTVK_NV_SMEM_CONFIGS) \
    ARRAY(zcull_info, uint32_t, UINT32_MAX, MTVK_NV_ZCULL_FIELDS) \
    NAME(device_name, MTVK_NV_DEVICE_NAME_BYTES) \
    NAME(chipset_name, MTVK_NV_CHIPSET_NAME_BYTES) \
    U32(has_alloc_tiled, uint8_t, 1u) \
    U32(has_map_fixed, uint8_t, 1u) \
    U32(has_overmap, uint8_t, 1u) \
    U32(has_compression, uint8_t, 1u) \
    U32(has_dma_buf, uint8_t, 1u) \
    U32(bind_align_B, uint32_t, UINT32_MAX) \
    U64(va_start) \
    U64(va_end)

#define SIZE32(field, type, maximum) + 4u
#define SIZE64(field) + 8u
#define SIZE_ARRAY(field, type, maximum, count) + 4u * (count)
#define SIZE_NAME(field, count) + (count)
_Static_assert(8u DEVICE_FIELDS(SIZE32, SIZE64, SIZE_ARRAY, SIZE_NAME) == MTVK_NV_DEVICE_WIRE_BYTES,
               "discovery wire size must match the versioned schema");
#undef SIZE32
#undef SIZE64
#undef SIZE_ARRAY
#undef SIZE_NAME

static void put32(uint8_t *bytes, uint32_t value) {
    for (unsigned i = 0; i < 4u; ++i) bytes[i] = (uint8_t)(value >> (i * 8u));
}
static uint32_t get32(const uint8_t *bytes) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4u; ++i) value |= (uint32_t)bytes[i] << (i * 8u);
    return value;
}
static bool name_valid(const char *name, size_t size, bool canonical) {
    bool ended = false;
    for (size_t i = 0; i < size; ++i) {
        const uint8_t value = (uint8_t)name[i];
        if (ended) { if (canonical && value != 0u) return false; }
        else if (value == 0u) ended = true;
        else if (value < 32u || value > 126u) return false;
    }
    return ended;
}
static mtvk_status validate_info(const mtvk_nv_device_info *info) {
#define CHECK32(field, type, maximum) if ((uint64_t)info->field > (uint64_t)(maximum)) return MTVK_INVALID_ARGUMENT;
#define CHECK64(field)
#define CHECK_ARRAY(field, type, maximum, count) \
    for (size_t i = 0; i < (count); ++i) { CHECK32(field[i], type, maximum) }
#define CHECK_NAME(field, count) if (!name_valid(info->field, count, false)) return MTVK_INVALID_ARGUMENT;
    DEVICE_FIELDS(CHECK32, CHECK64, CHECK_ARRAY, CHECK_NAME)
#undef CHECK32
#undef CHECK64
#undef CHECK_ARRAY
#undef CHECK_NAME
    for (size_t i = info->sm_smem_size_count; i < MTVK_NV_SMEM_CONFIGS; ++i)
        if (info->sm_smem_sizes_kB[i] != 0u) return MTVK_INVALID_ARGUMENT;
    if (!info->has_zcull_info)
        for (size_t i = 0; i < MTVK_NV_ZCULL_FIELDS; ++i)
            if (info->zcull_info[i] != 0u) return MTVK_INVALID_ARGUMENT;
    const uint32_t align = info->bind_align_B;
    if ((align && (align & (align - 1u))) ||
        (info->nc_atom_size_B && (info->nc_atom_size_B & (info->nc_atom_size_B - 1u))) ||
        ((info->va_start || info->va_end) && info->va_start >= info->va_end) ||
        (align && ((info->va_start | info->va_end) & (uint64_t)(align - 1u))))
        return MTVK_INVALID_ARGUMENT;
    return MTVK_OK;
}

mtvk_status mtvk_nv_device_info_encode(const mtvk_nv_device_info *info, void *bytes, size_t capacity) {
    if (!info || !bytes || !mtvk_span_valid(info, sizeof(*info)) ||
        !mtvk_span_valid(bytes, capacity)) return MTVK_INVALID_ARGUMENT;
    if (capacity < MTVK_NV_DEVICE_WIRE_BYTES) return MTVK_BUFFER_TOO_SMALL;
    if (mtvk_spans_overlap(info, sizeof(*info), bytes, MTVK_NV_DEVICE_WIRE_BYTES))
        return MTVK_INVALID_ARGUMENT;
    const mtvk_status valid = validate_info(info);
    if (valid != MTVK_OK) return valid;
    uint8_t *out = bytes;
    put32(out, MTVK_NV_DEVICE_WIRE_VERSION);
    put32(out + 4u, MTVK_NV_DEVICE_WIRE_BYTES);
    size_t offset = 8u;
#define WRITE32(field, type, maximum) put32(out + offset, info->field); offset += 4u;
#define WRITE64(field) \
    put32(out + offset, (uint32_t)info->field); \
    put32(out + offset + 4u, (uint32_t)(info->field >> 32u)); offset += 8u;
#define WRITE_ARRAY(field, type, maximum, count) \
    for (size_t i = 0; i < (count); ++i) { WRITE32(field[i], type, maximum) }
#define WRITE_NAME(field, count) \
    { bool ended = false; for (size_t i = 0; i < (count); ++i) { \
        if (info->field[i] == '\0') ended = true; \
        out[offset++] = ended ? 0u : (uint8_t)info->field[i]; } }
    DEVICE_FIELDS(WRITE32, WRITE64, WRITE_ARRAY, WRITE_NAME)
#undef WRITE32
#undef WRITE64
#undef WRITE_ARRAY
#undef WRITE_NAME
    return MTVK_OK;
}

mtvk_status mtvk_nv_device_info_decode(const void *bytes, size_t size, mtvk_nv_device_info *info) {
    if (!bytes || !info || !mtvk_span_valid(bytes, size) || !mtvk_span_valid(info, sizeof(*info)))
        return MTVK_INVALID_ARGUMENT;
    if (size < MTVK_NV_DEVICE_WIRE_BYTES) return MTVK_BUFFER_TOO_SMALL;
    if (size != MTVK_NV_DEVICE_WIRE_BYTES || mtvk_spans_overlap(bytes, size, info, sizeof(*info)))
        return MTVK_INVALID_ARGUMENT;
    const uint8_t *in = bytes;
    if (get32(in) != MTVK_NV_DEVICE_WIRE_VERSION) return MTVK_INCOMPATIBLE_VERSION;
    if (get32(in + 4u) != MTVK_NV_DEVICE_WIRE_BYTES) return MTVK_INVALID_ARGUMENT;
    mtvk_nv_device_info decoded;
    mtvk_nv_device_info_init(&decoded);
    size_t offset = 8u;
#define READ32(field, type, maximum) \
    { const uint32_t value = get32(in + offset); offset += 4u; \
      if ((uint64_t)value > (uint64_t)(maximum)) return MTVK_INVALID_ARGUMENT; \
      decoded.field = (type)value; }
#define READ64(field) \
    decoded.field = (uint64_t)get32(in + offset) | ((uint64_t)get32(in + offset + 4u) << 32u); offset += 8u;
#define READ_ARRAY(field, type, maximum, count) \
    for (size_t i = 0; i < (count); ++i) { READ32(field[i], type, maximum) }
#define READ_NAME(field, count) \
    for (size_t i = 0; i < (count); ++i) decoded.field[i] = (char)in[offset++]; \
    if (!name_valid(decoded.field, count, true)) return MTVK_INVALID_ARGUMENT;
    DEVICE_FIELDS(READ32, READ64, READ_ARRAY, READ_NAME)
#undef READ32
#undef READ64
#undef READ_ARRAY
#undef READ_NAME
    const mtvk_status valid = validate_info(&decoded);
    if (valid == MTVK_OK) *info = decoded;
    return valid;
}
