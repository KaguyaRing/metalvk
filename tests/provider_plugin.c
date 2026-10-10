// SPDX-License-Identifier: MIT
// Test-only binary provider, compiled twice without any core implementation.
#include "gpu/runtime.h"
#include <stdlib.h>
#include <string.h>
#define STRINGIFY_INNER(x) #x
#define STRINGIFY(x) STRINGIFY_INNER(x)
#ifndef PROVIDER_NUMBER
#error PROVIDER_NUMBER must identify this fixture
#endif
static mtvk_status metadata(void *context, size_t index, mtvk_gpu_device_description *out) {
    (void)context;
    if (index) return MTVK_NOT_FOUND;
    memcpy(out->name, "Binary provider " STRINGIFY(PROVIDER_NUMBER), sizeof("Binary provider " STRINGIFY(PROVIDER_NUMBER)));
    out->capabilities = MTVK_GPU_CAP_SUBMIT | MTVK_GPU_CAP_WAIT;
    out->queue_count = 1;
    return MTVK_OK;
}
static mtvk_status open_device(void *context, size_t index, void **instance, mtvk_gpu_device_description *out) {
    mtvk_status result = metadata(context, index, out);
    if (result != MTVK_OK) return result;
    *instance = malloc(1);
    return *instance ? MTVK_OK : MTVK_OUT_OF_MEMORY;
}
static mtvk_status close_device(void *context, void *instance) {
    (void)context; free(instance); return MTVK_OK;
}
static mtvk_status forward(void *context, void *instance, const mtvk_gpu_request *q, mtvk_gpu_response *out) {
    (void)context;
    if (!instance || (q->operation == MTVK_GPU_SUBMIT && (q->queue != 1 || q->command != 2)))
        return MTVK_INVALID_ARGUMENT;
    out->completion = q->operation == MTVK_GPU_SUBMIT ? 100u + PROVIDER_NUMBER : q->completion;
    out->state = MTVK_GPU_WORK_COMPLETE;
    return MTVK_OK;
}
#if defined(_WIN32)
__declspec(dllexport)
#endif
const mtvk_gpu_backend_descriptor *mtvk_test_provider(void) {
    static const mtvk_gpu_backend_descriptor ops = { .struct_size = sizeof(ops),
        .abi_major = MTVK_GPU_ABI_MAJOR, .name = "binary-provider-" STRINGIFY(PROVIDER_NUMBER),
        .enumerate = metadata, .open = open_device, .close = close_device, .dispatch = forward };
    return &ops;
}
