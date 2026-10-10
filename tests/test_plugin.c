// SPDX-License-Identifier: MIT
// The loader lives in the embedding application, never in the generic core.
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "thread_support.h"
#include "metalvk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <dlfcn.h>
#endif
typedef const mtvk_gpu_backend_descriptor *(*provider_entry)(void);
static void *allocate(void *context, size_t size) { (void)context; return malloc(size); }
static void release(void *context, void *p) { (void)context; free(p); }
static void lock(void *context) { mutex_lock(context); }
static void unlock(void *context) { mutex_unlock(context); }
int main(int argc, char **argv) {
    assert(argc == 3);
    test_mutex mutex; mutex_init(&mutex);
    mtvk_gpu_host_ops host = { .struct_size = sizeof(host), .abi_major = MTVK_GPU_ABI_MAJOR,
        .context = &mutex, .allocate = allocate, .release = release, .lock = lock, .unlock = unlock };
    mtvk_gpu_runtime *runtime = NULL;
    assert(mtvk_gpu_runtime_create(&host, &runtime) == MTVK_OK);
    mtvk_gpu_backend_handle previous = 0;
    for (int i = 1; i <= 2; ++i) {
        provider_entry entry = NULL;
#if defined(_WIN32)
        HMODULE module = LoadLibraryA(argv[i]); assert(module);
        FARPROC symbol = GetProcAddress(module, "mtvk_test_provider");
#else
        void *module = dlopen(argv[i], RTLD_NOW | RTLD_LOCAL); assert(module);
        void *symbol = dlsym(module, "mtvk_test_provider");
#endif
        _Static_assert(sizeof(entry) == sizeof(symbol), "Platform symbol representation unsupported");
        memcpy(&entry, &symbol, sizeof(entry)); assert(entry);
        mtvk_gpu_backend_handle backend = 0;
        assert(mtvk_gpu_backend_register(runtime, entry(), &backend) == MTVK_OK && backend != previous);
        mtvk_gpu_device_description description;
        if (previous) assert(mtvk_gpu_device_enumerate(runtime, previous, 0, &description) == MTVK_NOT_FOUND);
        assert(mtvk_gpu_device_enumerate(runtime, backend, 0, &description) == MTVK_OK);
        mtvk_gpu_device_handle device = 0;
        assert(mtvk_gpu_device_open(runtime, backend, 0, &device) == MTVK_OK);
        assert(mtvk_gpu_backend_unregister(runtime, backend) == MTVK_BUSY);
        mtvk_gpu_request q = { .struct_size = sizeof(q), .abi_major = MTVK_GPU_ABI_MAJOR,
            .operation = MTVK_GPU_SUBMIT, .queue = 1, .command = 2 };
        mtvk_gpu_response response;
        assert(mtvk_gpu_dispatch(runtime, device, &q, &response) == MTVK_OK);
        assert(response.completion == (uint64_t)(100 + i));
        assert(mtvk_gpu_device_close(runtime, device) == MTVK_OK);
        assert(mtvk_gpu_backend_unregister(runtime, backend) == MTVK_OK);
        previous = backend;
#if defined(_WIN32)
        assert(FreeLibrary(module));
#else
        assert(dlclose(module) == 0);
#endif
    }
    assert(mtvk_gpu_runtime_destroy(runtime) == MTVK_OK); mutex_destroy(&mutex);
    puts("Independent binary providers replaced successfully without rebuilding the core");
    return 0;
}
