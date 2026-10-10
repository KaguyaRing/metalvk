// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "gpu/runtime.h"
#include <stdatomic.h>
#include <string.h>

enum device_state { SLOT_EMPTY, SLOT_OPENING, SLOT_READY, SLOT_CLOSING, SLOT_QUARANTINED };
typedef struct backend_slot {
    uint64_t handle;
    mtvk_gpu_backend_descriptor ops;
    size_t calls, devices;
} backend_slot;
typedef struct device_slot {
    uint64_t handle;
    size_t backend;
    void *instance;
    mtvk_gpu_device_description description;
    enum device_state state;
    bool dispatching;
} device_slot;
struct mtvk_gpu_runtime {
    mtvk_gpu_host_ops host;
    backend_slot backends[MTVK_GPU_RUNTIME_BACKENDS_MAX];
    device_slot devices[MTVK_GPU_RUNTIME_DEVICES_MAX];
};
/* Never reuse tokens, including across runtimes; stale/wrong-runtime handles
 * cannot accidentally select a new backend/device. Exhaustion fails closed. */
static _Atomic(uint64_t) next_handle = 1;
static uint64_t new_handle(void) {
    uint64_t value = atomic_load_explicit(&next_handle, memory_order_relaxed);
    while (value != UINT64_MAX) {
        if (atomic_compare_exchange_weak_explicit(&next_handle, &value, value + 1,
                memory_order_relaxed, memory_order_relaxed)) return value;
    }
    return 0;
}
static void lock(mtvk_gpu_runtime *r) { r->host.lock(r->host.context); }
static void unlock(mtvk_gpu_runtime *r) { r->host.unlock(r->host.context); }
static size_t backend_index(mtvk_gpu_runtime *r, uint64_t handle) {
    for (size_t i = 0; i < MTVK_GPU_RUNTIME_BACKENDS_MAX; ++i)
        if (handle && r->backends[i].handle == handle) return i;
    return MTVK_GPU_RUNTIME_BACKENDS_MAX;
}
static size_t device_index(mtvk_gpu_runtime *r, uint64_t handle) {
    for (size_t i = 0; i < MTVK_GPU_RUNTIME_DEVICES_MAX; ++i)
        if (handle && r->devices[i].handle == handle) return i;
    return MTVK_GPU_RUNTIME_DEVICES_MAX;
}
static bool valid_name(const char *name) {
    if (!name[0]) return false;
    for (size_t i = 1; i < MTVK_GPU_NAME_MAX; ++i) if (!name[i]) return true;
    return false;
}
static bool valid_prefix(uint32_t size, size_t minimum, uint16_t major) {
    return size >= minimum && size <= MTVK_GPU_ABI_STRUCT_MAX && major == MTVK_GPU_ABI_MAJOR;
}
static mtvk_gpu_device_description empty_description(void) {
    mtvk_gpu_device_description d = {0};
    d.struct_size = sizeof(d); d.abi_major = MTVK_GPU_ABI_MAJOR;
    return d;
}
static bool valid_description(const mtvk_gpu_device_description *d, bool dispatch) {
    const uint64_t scheduling = MTVK_GPU_CAP_SUBMIT | MTVK_GPU_CAP_WAIT | MTVK_GPU_CAP_CANCEL;
    return valid_prefix(d->struct_size, sizeof(*d), d->abi_major) && !d->reserved && valid_name(d->name) &&
        (!(d->capabilities & scheduling) || dispatch) &&
        (!(d->capabilities & MTVK_GPU_CAP_SUBMIT) || d->queue_count != 0);
}
static mtvk_status callback_status(mtvk_status status) {
    return status >= MTVK_OK && status <= MTVK_CLEANUP_REQUIRED ? status : MTVK_INVALID_CONTEXT;
}
static void normalize_description(mtvk_gpu_device_description *d) {
    d->struct_size = sizeof(*d); d->abi_minor = MTVK_GPU_ABI_MINOR;
}
uint32_t mtvk_gpu_abi_version(void) { return (MTVK_GPU_ABI_MAJOR << 16) | MTVK_GPU_ABI_MINOR; }
mtvk_status mtvk_gpu_runtime_create(const mtvk_gpu_host_ops *host, mtvk_gpu_runtime **out) {
    if (!host || !out) return MTVK_INVALID_ARGUMENT;
    if (!valid_prefix(host->struct_size, sizeof(*host), host->abi_major)) return MTVK_INCOMPATIBLE_VERSION;
    if (!host->allocate || !host->release || !host->lock || !host->unlock)
        return MTVK_INVALID_ARGUMENT;
    mtvk_gpu_host_ops h = *host;
    h.struct_size = sizeof(h); h.abi_minor = MTVK_GPU_ABI_MINOR;
    mtvk_gpu_runtime *r = h.allocate(h.context, sizeof(*r));
    if (!r) return MTVK_OUT_OF_MEMORY;
    if ((uintptr_t)r % _Alignof(mtvk_gpu_runtime)) { h.release(h.context, r); return MTVK_INVALID_CONTEXT; }
    memset(r, 0, sizeof(*r)); r->host = h; *out = r;
    return MTVK_OK;
}
mtvk_status mtvk_gpu_runtime_destroy(mtvk_gpu_runtime *r) {
    if (!r) return MTVK_INVALID_ARGUMENT;
    lock(r);
    for (size_t i = 0; i < MTVK_GPU_RUNTIME_BACKENDS_MAX; ++i)
        if (r->backends[i].handle) { unlock(r); return MTVK_BUSY; }
    for (size_t i = 0; i < MTVK_GPU_RUNTIME_DEVICES_MAX; ++i)
        if (r->devices[i].state != SLOT_EMPTY) { unlock(r); return MTVK_BUSY; }
    mtvk_gpu_host_ops h = r->host;
    unlock(r); h.release(h.context, r);
    return MTVK_OK;
}
mtvk_status mtvk_gpu_backend_register(mtvk_gpu_runtime *r, const mtvk_gpu_backend_descriptor *ops,
                                     mtvk_gpu_backend_handle *out) {
    if (!r || !ops || !out) return MTVK_INVALID_ARGUMENT;
    if (!valid_prefix(ops->struct_size, sizeof(*ops), ops->abi_major) ||
        ops->min_host_minor > MTVK_GPU_ABI_MINOR || ops->min_host_minor > ops->abi_minor)
        return MTVK_INCOMPATIBLE_VERSION;
    if (ops->reserved || !valid_name(ops->name) || !ops->enumerate || !ops->open || !ops->close)
        return MTVK_INVALID_ARGUMENT;
    mtvk_gpu_backend_descriptor copy = *ops;
    lock(r);
    size_t vacant = MTVK_GPU_RUNTIME_BACKENDS_MAX;
    for (size_t i = 0; i < MTVK_GPU_RUNTIME_BACKENDS_MAX; ++i) {
        if (!r->backends[i].handle) { if (vacant == MTVK_GPU_RUNTIME_BACKENDS_MAX) vacant = i; }
        else if (!strcmp(r->backends[i].ops.name, copy.name)) { unlock(r); return MTVK_INVALID_ARGUMENT; }
    }
    if (vacant == MTVK_GPU_RUNTIME_BACKENDS_MAX) { unlock(r); return MTVK_LIMIT_EXCEEDED; }
    const uint64_t handle = new_handle();
    if (!handle) { unlock(r); return MTVK_LIMIT_EXCEEDED; }
    r->backends[vacant].ops = copy; r->backends[vacant].handle = handle;
    *out = handle;
    unlock(r); return MTVK_OK;
}
mtvk_status mtvk_gpu_backend_unregister(mtvk_gpu_runtime *r, mtvk_gpu_backend_handle handle) {
    if (!r || !handle) return MTVK_INVALID_ARGUMENT;
    lock(r); size_t i = backend_index(r, handle);
    if (i == MTVK_GPU_RUNTIME_BACKENDS_MAX) { unlock(r); return MTVK_NOT_FOUND; }
    if (r->backends[i].calls || r->backends[i].devices) { unlock(r); return MTVK_BUSY; }
    memset(&r->backends[i], 0, sizeof(r->backends[i]));
    unlock(r); return MTVK_OK;
}
size_t mtvk_gpu_runtime_backend_count(mtvk_gpu_runtime *r) {
    if (!r) return 0;
    size_t count = 0; lock(r);
    for (size_t i = 0; i < MTVK_GPU_RUNTIME_BACKENDS_MAX; ++i) count += r->backends[i].handle != 0;
    unlock(r); return count;
}
mtvk_status mtvk_gpu_device_enumerate(mtvk_gpu_runtime *r, mtvk_gpu_backend_handle handle,
                                     size_t index, mtvk_gpu_device_description *out) {
    if (!r || !handle || !out) return MTVK_INVALID_ARGUMENT;
    lock(r); size_t i = backend_index(r, handle);
    if (i == MTVK_GPU_RUNTIME_BACKENDS_MAX) { unlock(r); return MTVK_NOT_FOUND; }
    if (r->backends[i].calls >= MTVK_GPU_CALLBACKS_MAX) { unlock(r); return MTVK_LIMIT_EXCEEDED; }
    ++r->backends[i].calls; mtvk_gpu_backend_descriptor ops = r->backends[i].ops;
    unlock(r);
    mtvk_gpu_device_description d = empty_description();
    mtvk_status status = callback_status(ops.enumerate(ops.context, index, &d));
    if (status == MTVK_OK && !valid_description(&d, ops.dispatch != NULL)) status = MTVK_INVALID_CONTEXT;
    lock(r); --r->backends[i].calls; unlock(r);
    if (status == MTVK_OK) { normalize_description(&d); *out = d; }
    return status;
}
mtvk_status mtvk_gpu_device_open(mtvk_gpu_runtime *r, mtvk_gpu_backend_handle handle, size_t index,
                                mtvk_gpu_device_handle *out) {
    if (!r || !handle || !out) return MTVK_INVALID_ARGUMENT;
    *out = 0; lock(r); size_t b = backend_index(r, handle);
    if (b == MTVK_GPU_RUNTIME_BACKENDS_MAX) { unlock(r); return MTVK_NOT_FOUND; }
    if (r->backends[b].calls >= MTVK_GPU_CALLBACKS_MAX) { unlock(r); return MTVK_LIMIT_EXCEEDED; }
    size_t d = 0;
    while (d < MTVK_GPU_RUNTIME_DEVICES_MAX && r->devices[d].state != SLOT_EMPTY) ++d;
    if (d == MTVK_GPU_RUNTIME_DEVICES_MAX) { unlock(r); return MTVK_LIMIT_EXCEEDED; }
    uint64_t token = new_handle();
    if (!token) { unlock(r); return MTVK_LIMIT_EXCEEDED; }
    r->devices[d].state = SLOT_OPENING;
    ++r->backends[b].calls; mtvk_gpu_backend_descriptor ops = r->backends[b].ops;
    unlock(r);
    void *instance = NULL; mtvk_gpu_device_description info = empty_description();
    mtvk_status status = callback_status(ops.open(ops.context, index, &instance, &info));
    bool usable = status == MTVK_OK && instance && valid_description(&info, ops.dispatch != NULL);
    if (!usable && status == MTVK_OK) status = MTVK_INVALID_CONTEXT;
    bool quarantine = false;
    if (!usable && instance) {
        mtvk_status cleanup = callback_status(ops.close(ops.context, instance));
        if (cleanup == MTVK_OK) instance = NULL;
        else { quarantine = true; status = MTVK_CLEANUP_REQUIRED; }
    }
    lock(r);
    --r->backends[b].calls;
    if (usable || quarantine) {
        normalize_description(&info);
        r->devices[d].handle = token; r->devices[d].backend = b;
        r->devices[d].instance = instance; r->devices[d].description = info;
        r->devices[d].state = usable ? SLOT_READY : SLOT_QUARANTINED;
        ++r->backends[b].devices; *out = token;
    } else memset(&r->devices[d], 0, sizeof(r->devices[d]));
    unlock(r); return status;
}
mtvk_status mtvk_gpu_device_describe(mtvk_gpu_runtime *r, mtvk_gpu_device_handle handle,
                                    mtvk_gpu_device_description *out) {
    if (!r || !handle || !out) return MTVK_INVALID_ARGUMENT;
    lock(r); size_t i = device_index(r, handle);
    if (i == MTVK_GPU_RUNTIME_DEVICES_MAX) { unlock(r); return MTVK_NOT_FOUND; }
    if (r->devices[i].state != SLOT_READY) { unlock(r); return MTVK_BUSY; }
    *out = r->devices[i].description;
    unlock(r); return MTVK_OK;
}
mtvk_status mtvk_gpu_device_close(mtvk_gpu_runtime *r, mtvk_gpu_device_handle handle) {
    if (!r || !handle) return MTVK_INVALID_ARGUMENT;
    lock(r); size_t d = device_index(r, handle);
    if (d == MTVK_GPU_RUNTIME_DEVICES_MAX) { unlock(r); return MTVK_NOT_FOUND; }
    device_slot *slot = &r->devices[d];
    if ((slot->state != SLOT_READY && slot->state != SLOT_QUARANTINED) || slot->dispatching) {
        unlock(r); return MTVK_BUSY;
    }
    enum device_state previous = slot->state;
    size_t b = slot->backend;
    if (r->backends[b].calls >= MTVK_GPU_CALLBACKS_MAX) { unlock(r); return MTVK_LIMIT_EXCEEDED; }
    slot->state = SLOT_CLOSING;
    ++r->backends[b].calls;
    mtvk_gpu_backend_descriptor ops = r->backends[b].ops; void *instance = slot->instance;
    unlock(r);
    mtvk_status status = callback_status(ops.close(ops.context, instance));
    lock(r); --r->backends[b].calls;
    if (status == MTVK_OK) { --r->backends[b].devices; memset(slot, 0, sizeof(*slot)); }
    else slot->state = previous;
    unlock(r); return status;
}
mtvk_status mtvk_gpu_dispatch(mtvk_gpu_runtime *r, mtvk_gpu_device_handle handle,
                             const mtvk_gpu_request *request, mtvk_gpu_response *out) {
    if (!r || !handle || !request || !out) return MTVK_INVALID_ARGUMENT;
    if (!valid_prefix(request->struct_size, sizeof(*request), request->abi_major) ||
        request->abi_minor > MTVK_GPU_ABI_MINOR) return MTVK_INCOMPATIBLE_VERSION;
    mtvk_gpu_request q = *request;
    // Only the known prefix is copied. Never tell a callback that this local
    // buffer contains an appended tail which the caller alone owns.
    q.struct_size = sizeof(q); q.abi_minor = MTVK_GPU_ABI_MINOR;
    uint64_t capability;
    if (q.reserved) return MTVK_INVALID_ARGUMENT;
    switch (q.operation) {
    case MTVK_GPU_SUBMIT:
        if (!q.queue || !q.command || q.completion || q.timeout_ns) return MTVK_INVALID_ARGUMENT;
        capability = MTVK_GPU_CAP_SUBMIT; break;
    case MTVK_GPU_WAIT:
    case MTVK_GPU_CANCEL:
        if (q.queue || q.command || !q.completion || (q.operation == MTVK_GPU_CANCEL && q.timeout_ns))
            return MTVK_INVALID_ARGUMENT;
        capability = q.operation == MTVK_GPU_WAIT ? MTVK_GPU_CAP_WAIT : MTVK_GPU_CAP_CANCEL; break;
    default: return MTVK_NOT_SUPPORTED;
    }
    lock(r); size_t d = device_index(r, handle);
    if (d == MTVK_GPU_RUNTIME_DEVICES_MAX) { unlock(r); return MTVK_NOT_FOUND; }
    device_slot *slot = &r->devices[d];
    if (slot->state != SLOT_READY || slot->dispatching) { unlock(r); return MTVK_BUSY; }
    size_t b = slot->backend; mtvk_gpu_backend_descriptor ops = r->backends[b].ops;
    if (!(slot->description.capabilities & capability) || !ops.dispatch) { unlock(r); return MTVK_NOT_SUPPORTED; }
    if (r->backends[b].calls >= MTVK_GPU_CALLBACKS_MAX) { unlock(r); return MTVK_LIMIT_EXCEEDED; }
    slot->dispatching = true; ++r->backends[b].calls; void *instance = slot->instance;
    unlock(r);
    mtvk_gpu_response response = { .struct_size = sizeof(response), .abi_major = MTVK_GPU_ABI_MAJOR };
    mtvk_status status = callback_status(ops.dispatch(ops.context, instance, &q, &response));
    if (status == MTVK_OK && (!valid_prefix(response.struct_size, sizeof(response), response.abi_major) ||
        response.reserved || response.state < MTVK_GPU_WORK_PENDING || response.state > MTVK_GPU_WORK_FAILED ||
        !response.completion || (q.operation != MTVK_GPU_SUBMIT && response.completion != q.completion)))
        status = MTVK_INVALID_CONTEXT;
    lock(r); slot->dispatching = false; --r->backends[b].calls; unlock(r);
    if (status == MTVK_OK) {
        response.struct_size = sizeof(response); response.abi_minor = MTVK_GPU_ABI_MINOR;
        *out = response;
    }
    return status;
}
