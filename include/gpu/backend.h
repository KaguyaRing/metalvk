// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing

#ifndef MTVK_GPU_BACKEND_H
#define MTVK_GPU_BACKEND_H

#include "base/init.h"

MTVK_EXTERN_C_BEGIN

/* Opaque, backend-owned device. No vendor type crosses this boundary. */
typedef struct mtvk_gpu_device mtvk_gpu_device;

typedef struct mtvk_gpu_device_info {
    char name[MTVK_GPU_NAME_MAX];
    uint16_t vendor_id;
    uint16_t device_id;
    uint64_t memory_bytes;
} mtvk_gpu_device_info;

/* A vendor backend implements this table. enumerate() reports device metadata;
 * open() returns an opaque device that close() releases. All callbacks are
 * optional except for the ones a caller invokes; registration requires
 * enumerate, open and close. The ops table must stay valid for the process
 * lifetime and must be reentrant for independent devices. */
typedef struct mtvk_gpu_backend_ops {
    const char *name;
    mtvk_status (*enumerate)(size_t index, mtvk_gpu_device_info *out);
    mtvk_status (*open)(size_t index, mtvk_gpu_device **out);
    void       (*close)(mtvk_gpu_device *device);
} mtvk_gpu_backend_ops;

/* Register a backend (up to MTVK_GPU_BACKENDS_MAX). Rejects NULL, empty names,
 * missing callbacks and duplicate names. Not thread-safe during registration;
 * query/dispatch below are safe once registration has quiesced. */
mtvk_status mtvk_gpu_register_backend(const mtvk_gpu_backend_ops *ops);
size_t mtvk_gpu_backend_count(void);

/* backend/enumerate/open indices are validated; on failure *out is untouched. */
mtvk_status mtvk_gpu_enumerate(size_t backend, size_t index, mtvk_gpu_device_info *out);
mtvk_status mtvk_gpu_open(size_t backend, size_t index, mtvk_gpu_device **out);
void mtvk_gpu_close(size_t backend, mtvk_gpu_device *device);

MTVK_EXTERN_C_END
#endif
