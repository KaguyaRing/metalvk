// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "thread_support.h"
#include "metalvk.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Thread_local unsigned lock_depth;
typedef struct host { test_mutex mutex; bool fail_allocation; size_t allocations; } host;
static void *allocate(void *context, size_t bytes) {
    host *h = context; assert(!lock_depth);
    if (h->fail_allocation) return NULL;
    void *p = malloc(bytes); if (p) ++h->allocations; return p;
}
static void release(void *context, void *p) {
    host *h = context; assert(!lock_depth && h->allocations); --h->allocations; free(p);
}
static void host_lock(void *context) {
    assert(!lock_depth); mutex_lock(&((host *)context)->mutex); ++lock_depth;
}
static void host_unlock(void *context) {
    assert(lock_depth == 1); --lock_depth; mutex_unlock(&((host *)context)->mutex);
}
typedef struct provider {
    mtvk_gpu_runtime *runtime;
    atomic_uint instances, calls;
    atomic_bool block, close_busy, bad_open, bad_response;
    test_event entered, proceed;
} provider;
static void describe(mtvk_gpu_device_description *d) {
    d->struct_size = sizeof(*d) + 16; d->abi_major = MTVK_GPU_ABI_MAJOR; d->abi_minor = 1;
    (void)snprintf(d->name, sizeof(d->name), "Synthetic protocol fixture");
    d->capabilities = MTVK_GPU_CAP_SUBMIT | MTVK_GPU_CAP_WAIT;
    d->queue_count = 1;
}
static mtvk_status enumerate(void *context, size_t index, mtvk_gpu_device_description *out) {
    provider *p = context; assert(!lock_depth);
    assert(mtvk_gpu_runtime_backend_count(p->runtime) > 0); // callback reentrancy
    if (index) return MTVK_NOT_FOUND;
    describe(out); return MTVK_OK;
}
static mtvk_status open_device(void *context, size_t index, void **out, mtvk_gpu_device_description *info) {
    provider *p = context; assert(!lock_depth);
    if (index) return MTVK_NOT_FOUND;
    *out = malloc(1); if (!*out) return MTVK_OUT_OF_MEMORY;
    atomic_fetch_add(&p->instances, 1u); describe(info);
    return atomic_load(&p->bad_open) ? MTVK_INVALID_CONTEXT : MTVK_OK;
}
static mtvk_status close_device(void *context, void *instance) {
    provider *p = context; assert(!lock_depth && instance);
    assert(mtvk_gpu_runtime_backend_count(p->runtime) > 0);
    if (atomic_load(&p->close_busy)) return MTVK_BUSY;
    free(instance); assert(atomic_fetch_sub(&p->instances, 1u) > 0); return MTVK_OK;
}
static mtvk_status dispatch(void *context, void *instance, const mtvk_gpu_request *q, mtvk_gpu_response *out) {
    provider *p = context; assert(!lock_depth && instance);
    assert(q->struct_size == sizeof(*q) && q->abi_minor == MTVK_GPU_ABI_MINOR);
    atomic_fetch_add(&p->calls, 1u);
    if (atomic_load(&p->block)) { event_signal(&p->entered); event_wait(&p->proceed); }
    out->completion = q->operation == MTVK_GPU_SUBMIT ? 123u : q->completion;
    out->state = atomic_load(&p->bad_response) ? 99u : MTVK_GPU_WORK_COMPLETE;
    out->struct_size += 16; out->abi_minor = 1;
    return MTVK_OK;
}
static mtvk_gpu_backend_descriptor descriptor(provider *p, const char *name) {
    mtvk_gpu_backend_descriptor d = { .struct_size = sizeof(d), .abi_major = MTVK_GPU_ABI_MAJOR,
        .context = p, .enumerate = enumerate, .open = open_device, .close = close_device, .dispatch = dispatch };
    (void)snprintf(d.name, sizeof(d.name), "%s", name); return d;
}
static mtvk_gpu_request request(void) {
    return (mtvk_gpu_request){ .struct_size = sizeof(mtvk_gpu_request), .abi_major = MTVK_GPU_ABI_MAJOR,
        .operation = MTVK_GPU_SUBMIT, .queue = 1, .command = 2 };
}
typedef struct job { mtvk_gpu_runtime *runtime; mtvk_gpu_device_handle device; mtvk_status result; } job;
static void run_dispatch(void *argument) {
    job *j = argument; mtvk_gpu_request q = request(); mtvk_gpu_response response;
    j->result = mtvk_gpu_dispatch(j->runtime, j->device, &q, &response);
}
int main(void) {
    host h = {0}; mutex_init(&h.mutex);
    mtvk_gpu_host_ops platform = { .struct_size = sizeof(platform), .abi_major = MTVK_GPU_ABI_MAJOR,
        .context = &h, .allocate = allocate, .release = release, .lock = host_lock, .unlock = host_unlock };
    mtvk_gpu_runtime *r = NULL, *other = NULL;
    h.fail_allocation = true;
    assert(mtvk_gpu_runtime_create(&platform, &r) == MTVK_OUT_OF_MEMORY && !r);
    h.fail_allocation = false;
    assert(mtvk_gpu_runtime_create(&platform, &r) == MTVK_OK);
    assert(mtvk_gpu_runtime_create(&platform, &other) == MTVK_OK);
    provider p = { .runtime = r };
    atomic_init(&p.instances, 0u); atomic_init(&p.calls, 0u);
    atomic_init(&p.block, false); atomic_init(&p.close_busy, false);
    atomic_init(&p.bad_open, false); atomic_init(&p.bad_response, false);
    event_init(&p.entered); event_init(&p.proceed);
    mtvk_gpu_backend_descriptor a = descriptor(&p, "provider-a"), b = descriptor(&p, "provider-b");
    mtvk_gpu_backend_handle first = 0, second = 0, rejected = 0;
    mtvk_gpu_backend_descriptor incompatible = a;
    incompatible.abi_major++;
    assert(mtvk_gpu_backend_register(r, &incompatible, &rejected) == MTVK_INCOMPATIBLE_VERSION && !rejected);
    incompatible = a; incompatible.struct_size--;
    assert(mtvk_gpu_backend_register(r, &incompatible, &rejected) == MTVK_INCOMPATIBLE_VERSION);
    incompatible = a; incompatible.abi_minor = 1; incompatible.min_host_minor = 1;
    assert(mtvk_gpu_backend_register(r, &incompatible, &rejected) == MTVK_INCOMPATIBLE_VERSION);
    a.abi_minor = 1; // additive extension with a supported known prefix
    assert(mtvk_gpu_backend_register(r, &a, &first) == MTVK_OK);
    assert(mtvk_gpu_backend_register(r, &a, &rejected) == MTVK_INVALID_ARGUMENT);
    a.name[0] = 'x'; a.enumerate = NULL; // registration owns its copy
    assert(mtvk_gpu_backend_register(r, &b, &second) == MTVK_OK);
    assert(mtvk_gpu_runtime_destroy(r) == MTVK_BUSY);
    mtvk_gpu_device_description info = {0}, before_info;
    assert(mtvk_gpu_device_enumerate(r, first, 0, &info) == MTVK_OK && info.queue_count == 1);
    assert(info.struct_size == sizeof(info) && info.abi_minor == MTVK_GPU_ABI_MINOR);
    before_info = info;
    assert(mtvk_gpu_device_enumerate(r, first, 1, &info) == MTVK_NOT_FOUND);
    assert(memcmp(&info, &before_info, sizeof(info)) == 0);
    mtvk_gpu_device_handle device = 0, device_b = 0;
    assert(mtvk_gpu_device_open(r, first, 0, &device) == MTVK_OK && device);
    assert(mtvk_gpu_device_open(r, second, 0, &device_b) == MTVK_OK && device_b != device);
    assert(mtvk_gpu_device_close(other, device) == MTVK_NOT_FOUND); // ownership
    assert(mtvk_gpu_backend_unregister(r, first) == MTVK_BUSY);
    mtvk_gpu_request q = request(); mtvk_gpu_response response = {0}, before_response;
    assert(mtvk_gpu_dispatch(r, device, &q, &response) == MTVK_OK && response.completion == 123u);
    assert(response.struct_size == sizeof(response) && response.abi_minor == MTVK_GPU_ABI_MINOR);
    q.struct_size += 16; // callback must not receive a fictitious extended tail
    assert(mtvk_gpu_dispatch(r, device, &q, &response) == MTVK_OK);
    before_response = response;
    q.operation = MTVK_GPU_CANCEL; q.queue = 0; q.command = 0; q.completion = 123;
    assert(mtvk_gpu_dispatch(r, device, &q, &response) == MTVK_NOT_SUPPORTED);
    assert(memcmp(&response, &before_response, sizeof(response)) == 0);
    q.operation = MTVK_GPU_WAIT;
    assert(mtvk_gpu_dispatch(r, device, &q, &response) == MTVK_OK);
    atomic_store(&p.bad_response, true);
    assert(mtvk_gpu_dispatch(r, device, &q, &response) == MTVK_INVALID_CONTEXT);
    atomic_store(&p.bad_response, false);
    atomic_store(&p.block, true);
    job j = { r, device, MTVK_INVALID_CONTEXT }; test_thread worker;
    thread_start(&worker, run_dispatch, &j); event_wait(&p.entered);
    assert(mtvk_gpu_device_close(r, device) == MTVK_BUSY);
    assert(mtvk_gpu_backend_unregister(r, first) == MTVK_BUSY);
    assert(mtvk_gpu_dispatch(r, device, &q, &response) == MTVK_BUSY);
    atomic_store(&p.block, false);
    assert(mtvk_gpu_dispatch(r, device_b, &q, &response) == MTVK_OK); // independent device
    event_signal(&p.proceed); thread_join(&worker); assert(j.result == MTVK_OK);
    atomic_store(&p.close_busy, true);
    assert(mtvk_gpu_device_close(r, device) == MTVK_BUSY);
    assert(mtvk_gpu_device_describe(r, device, &info) == MTVK_OK);
    atomic_store(&p.close_busy, false);
    assert(mtvk_gpu_device_close(r, device) == MTVK_OK);
    assert(mtvk_gpu_device_close(r, device) == MTVK_NOT_FOUND);
    assert(mtvk_gpu_device_close(r, device_b) == MTVK_OK);
    atomic_store(&p.bad_open, true); atomic_store(&p.close_busy, true);
    assert(mtvk_gpu_device_open(r, first, 0, &device) == MTVK_CLEANUP_REQUIRED && device);
    assert(mtvk_gpu_dispatch(r, device, &q, &response) == MTVK_BUSY);
    assert(mtvk_gpu_backend_unregister(r, first) == MTVK_BUSY);
    atomic_store(&p.close_busy, false);
    assert(mtvk_gpu_device_close(r, device) == MTVK_OK);
    atomic_store(&p.bad_open, false);
    assert(mtvk_gpu_backend_unregister(r, first) == MTVK_OK);
    a = descriptor(&p, "provider-a");
    assert(mtvk_gpu_backend_register(r, &a, &rejected) == MTVK_OK && rejected != first);
    assert(mtvk_gpu_device_enumerate(r, first, 0, &info) == MTVK_NOT_FOUND);
    assert(mtvk_gpu_backend_unregister(r, rejected) == MTVK_OK);
    assert(mtvk_gpu_backend_unregister(r, second) == MTVK_OK);
    // Registry exhaustion is bounded and independent of provider identity.
    mtvk_gpu_backend_handle registered[MTVK_GPU_RUNTIME_BACKENDS_MAX];
    for (size_t i = 0; i < MTVK_GPU_RUNTIME_BACKENDS_MAX; ++i) {
        char name[MTVK_GPU_NAME_MAX];
        (void)snprintf(name, sizeof(name), "capacity-%zu", i);
        a = descriptor(&p, name);
        assert(mtvk_gpu_backend_register(r, &a, &registered[i]) == MTVK_OK);
    }
    a = descriptor(&p, "overflow");
    assert(mtvk_gpu_backend_register(r, &a, &rejected) == MTVK_LIMIT_EXCEEDED);
    for (size_t i = 0; i < MTVK_GPU_RUNTIME_BACKENDS_MAX; ++i)
        assert(mtvk_gpu_backend_unregister(r, registered[i]) == MTVK_OK);
    assert(atomic_load(&p.instances) == 0);
    assert(mtvk_gpu_runtime_destroy(other) == MTVK_OK);
    assert(mtvk_gpu_runtime_destroy(r) == MTVK_OK && h.allocations == 0);
    event_destroy(&p.entered); event_destroy(&p.proceed); mutex_destroy(&h.mutex);
    puts("Versioned vendor-neutral registry, routing and concurrency tests passed");
    return 0;
}
