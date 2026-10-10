// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing

#include "gpu/init.h"
#include <string.h>

static const mtvk_gpu_backend_ops *g_backends[MTVK_GPU_BACKENDS_MAX];
static size_t g_backend_count;

mtvk_status mtvk_gpu_register_backend(const mtvk_gpu_backend_ops *ops)
{
    if (ops == NULL || !mtvk_span_valid(ops, sizeof(*ops)) ||
        (uintptr_t)ops % _Alignof(mtvk_gpu_backend_ops) != 0 || ops->name == NULL ||
        ops->name[0] == '\0' || ops->enumerate == NULL || ops->open == NULL || ops->close == NULL) {
        return MTVK_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < g_backend_count; ++i) {
        if (strcmp(g_backends[i]->name, ops->name) == 0) { return MTVK_INVALID_ARGUMENT; }
    }
    if (g_backend_count >= MTVK_GPU_BACKENDS_MAX) { return MTVK_LIMIT_EXCEEDED; }
    g_backends[g_backend_count++] = ops;
    return MTVK_OK;
}

size_t mtvk_gpu_backend_count(void)
{
    return g_backend_count;
}

mtvk_status mtvk_gpu_enumerate(size_t backend, size_t index, mtvk_gpu_device_info *out)
{
    if (out == NULL || !mtvk_span_valid(out, sizeof(*out)) ||
        (uintptr_t)out % _Alignof(mtvk_gpu_device_info) != 0) {
        return MTVK_INVALID_ARGUMENT;
    }
    if (backend >= g_backend_count) { return MTVK_NOT_FOUND; }
    mtvk_gpu_device_info info;
    memset(&info, 0, sizeof(info));
    mtvk_status status = g_backends[backend]->enumerate(index, &info);
    if (status != MTVK_OK) { return status; }
    info.name[MTVK_GPU_NAME_MAX - 1u] = '\0';
    *out = info;
    return MTVK_OK;
}

mtvk_status mtvk_gpu_open(size_t backend, size_t index, mtvk_gpu_device **out)
{
    if (out == NULL || !mtvk_span_valid(out, sizeof(*out)) ||
        (uintptr_t)out % _Alignof(mtvk_gpu_device *) != 0) {
        return MTVK_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (backend >= g_backend_count) { return MTVK_NOT_FOUND; }
    return g_backends[backend]->open(index, out);
}

void mtvk_gpu_close(size_t backend, mtvk_gpu_device *device)
{
    if (device == NULL || backend >= g_backend_count) { return; }
    g_backends[backend]->close(device);
}
