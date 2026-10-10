// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_GPU_RUNTIME_H
#define MTVK_GPU_RUNTIME_H
#include "base/init.h"

#define MTVK_GPU_ABI_MAJOR 1u
#define MTVK_GPU_ABI_MINOR 0u
#define MTVK_GPU_CAP_SUBMIT UINT64_C(1)
#define MTVK_GPU_CAP_WAIT UINT64_C(2)
#define MTVK_GPU_CAP_CANCEL UINT64_C(4)
#define MTVK_GPU_CAP_COMPUTE UINT64_C(8)
#define MTVK_GPU_CAP_RENDER UINT64_C(16)
#define MTVK_GPU_CAP_DISPLAY UINT64_C(32)

MTVK_EXTERN_C_BEGIN
typedef struct mtvk_gpu_runtime mtvk_gpu_runtime;
typedef uint64_t mtvk_gpu_backend_handle;
typedef uint64_t mtvk_gpu_device_handle;

/* The embedding platform owns the allocator and mutex. lock/unlock must use
 * one shared, non-recursive mutex; they cannot fail. The allocator must return
 * storage aligned for all C fundamental types. All GPU callbacks and memory
 * allocation/release run outside this mutex. API calls require thread context,
 * not a hardware interrupt handler. The host context outlives the runtime. */
typedef struct mtvk_gpu_host_ops {
    uint32_t struct_size;
    uint16_t abi_major, abi_minor;
    void *context;
    void *(*allocate)(void *context, size_t bytes);
    void (*release)(void *context, void *allocation);
    void (*lock)(void *context);
    void (*unlock)(void *context);
} mtvk_gpu_host_ops;

typedef struct mtvk_gpu_device_description {
    uint32_t struct_size;
    uint16_t abi_major, abi_minor;
    char name[MTVK_GPU_NAME_MAX];
    uint8_t identity[16]; /* opaque stable identity, never a pointer */
    uint64_t capabilities;
    uint64_t memory_bytes;
    uint32_t queue_count;
    uint32_t reserved;
} mtvk_gpu_device_description;

typedef enum mtvk_gpu_operation {
    MTVK_GPU_SUBMIT = 1,
    MTVK_GPU_WAIT = 2,
    MTVK_GPU_CANCEL = 3
} mtvk_gpu_operation;

/* Tokens below belong to the selected backend's device. Commands/resources
 * are prepared by that backend/frontend contract, never by this routing layer.
 * No raw ioctl payloads, register values or addresses cross this interface.
 * SUBMIT: queue/command nonzero, completion/timeout zero.
 * WAIT: completion nonzero, queue/command zero; timeout=0 means polling.
 * CANCEL: completion nonzero; all other tokens and timeout zero. */
typedef struct mtvk_gpu_request {
    uint32_t struct_size;
    uint16_t abi_major, abi_minor;
    uint32_t operation, reserved;
    uint64_t queue, command, completion, timeout_ns;
} mtvk_gpu_request;

typedef enum mtvk_gpu_work_state {
    MTVK_GPU_WORK_PENDING = 1,
    MTVK_GPU_WORK_COMPLETE = 2,
    MTVK_GPU_WORK_CANCELLED = 3,
    MTVK_GPU_WORK_FAILED = 4
} mtvk_gpu_work_state;
typedef struct mtvk_gpu_response {
    uint32_t struct_size;
    uint16_t abi_major, abi_minor;
    uint32_t state, reserved;
    uint64_t completion;
} mtvk_gpu_response;

/* Major changes break ABI; minor versions only append fields. A larger table
 * is accepted if its known prefix is present and min_host_minor is supported.
 * The table/name are copied. Function pointers and context must remain valid
 * until unregister succeeds. Unregister returns BUSY while calls/devices live.
 * open reports metadata and its private instance atomically for the same device.
 * close releases the instance on OK, retains it on any error. A failed open
 * must return no instance; the core quarantines violations for explicit cleanup.
 * dispatch may be NULL when no dispatch operation is supported. It validates
 * all device-local tokens and implements all actual GPU operations.
 * Callbacks on the same backend may overlap across devices or enumeration/open
 * calls; the backend synchronizes its own shared state and bounds blocking work.
 * Callbacks must honor the output struct_size supplied by the core, return a
 * defined mtvk_status, and never throw/unwind/longjmp across the C ABI.
 * Handles are lifetime identifiers, not security/authorization capabilities;
 * external IPC adapters must authorize callers before using this trusted API. */
typedef struct mtvk_gpu_backend_descriptor {
    uint32_t struct_size;
    uint16_t abi_major, abi_minor;
    uint16_t min_host_minor, reserved;
    char name[MTVK_GPU_NAME_MAX];
    void *context;
    mtvk_status (*enumerate)(void *, size_t, mtvk_gpu_device_description *);
    mtvk_status (*open)(void *, size_t, void **, mtvk_gpu_device_description *);
    mtvk_status (*close)(void *, void *);
    mtvk_status (*dispatch)(void *, void *, const mtvk_gpu_request *, mtvk_gpu_response *);
} mtvk_gpu_backend_descriptor;

uint32_t mtvk_gpu_abi_version(void); /* major:31..16, minor:15..0 */
mtvk_status mtvk_gpu_runtime_create(const mtvk_gpu_host_ops *, mtvk_gpu_runtime **);
/* Requires external quiescence: never race destroy with API entry. Registered
 * backends, devices or calls make destroy return BUSY without freeing anything. */
mtvk_status mtvk_gpu_runtime_destroy(mtvk_gpu_runtime *);
mtvk_status mtvk_gpu_backend_register(mtvk_gpu_runtime *, const mtvk_gpu_backend_descriptor *, mtvk_gpu_backend_handle *);
mtvk_status mtvk_gpu_backend_unregister(mtvk_gpu_runtime *, mtvk_gpu_backend_handle);
size_t mtvk_gpu_runtime_backend_count(mtvk_gpu_runtime *);
mtvk_status mtvk_gpu_device_enumerate(mtvk_gpu_runtime *, mtvk_gpu_backend_handle, size_t, mtvk_gpu_device_description *);
/* Initializes out to zero. CLEANUP_REQUIRED returns a nonzero cleanup-only
 * handle if a contract-violating backend returns an instance it cannot close.
 * The caller must close that handle; it cannot be used for dispatch. */
mtvk_status mtvk_gpu_device_open(mtvk_gpu_runtime *, mtvk_gpu_backend_handle, size_t, mtvk_gpu_device_handle *);
mtvk_status mtvk_gpu_device_describe(mtvk_gpu_runtime *, mtvk_gpu_device_handle, mtvk_gpu_device_description *);
mtvk_status mtvk_gpu_device_close(mtvk_gpu_runtime *, mtvk_gpu_device_handle);
/* Same-device dispatch is serialized by returning BUSY to concurrent calls;
 * independent devices can dispatch concurrently. Error output is untouched. */
mtvk_status mtvk_gpu_dispatch(mtvk_gpu_runtime *, mtvk_gpu_device_handle, const mtvk_gpu_request *, mtvk_gpu_response *);
MTVK_EXTERN_C_END
#endif
