// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing

#include "gpu/backend.h"
#include <stdio.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #test); return false; \
} } while (0)

#define MOCK_MEMORY (UINT64_C(8) << 30)

typedef struct mock_device { int openings; } mock_device;
static mock_device g_mock;

static mtvk_status mock_enumerate(size_t index, mtvk_gpu_device_info *out)
{
    if (index != 0) { return MTVK_NOT_FOUND; }
    memset(out, 0, sizeof(*out));
    (void)snprintf(out->name, sizeof(out->name), "%s", "Mock GPU");
    out->vendor_id = 0x1234;
    out->device_id = 0x5678;
    out->memory_bytes = MOCK_MEMORY;
    return MTVK_OK;
}

static mtvk_status mock_open(size_t index, mtvk_gpu_device **out)
{
    if (index != 0) { return MTVK_NOT_FOUND; }
    ++g_mock.openings;
    *out = (mtvk_gpu_device *)(void *)&g_mock;
    return MTVK_OK;
}

static void mock_close(mtvk_gpu_device *device)
{
    if (device == (const mtvk_gpu_device *)(const void *)&g_mock) { --g_mock.openings; }
}

static const mtvk_gpu_backend_ops mock_ops = {"mock", mock_enumerate, mock_open, mock_close};
static const mtvk_gpu_backend_ops mock_duplicate = {"mock", mock_enumerate, mock_open, mock_close};

static bool test_registration_validation(void)
{
    mtvk_gpu_backend_ops broken = mock_ops;
    CHECK(mtvk_gpu_register_backend(NULL) == MTVK_INVALID_ARGUMENT);
    broken = mock_ops; broken.name = NULL;
    CHECK(mtvk_gpu_register_backend(&broken) == MTVK_INVALID_ARGUMENT);
    broken = mock_ops; broken.name = "";
    CHECK(mtvk_gpu_register_backend(&broken) == MTVK_INVALID_ARGUMENT);
    broken = mock_ops; broken.enumerate = NULL;
    CHECK(mtvk_gpu_register_backend(&broken) == MTVK_INVALID_ARGUMENT);
    broken = mock_ops; broken.open = NULL;
    CHECK(mtvk_gpu_register_backend(&broken) == MTVK_INVALID_ARGUMENT);
    broken = mock_ops; broken.close = NULL;
    CHECK(mtvk_gpu_register_backend(&broken) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_gpu_backend_count() == 0);
    CHECK(mtvk_gpu_register_backend(&mock_ops) == MTVK_OK);
    CHECK(mtvk_gpu_backend_count() == 1);
    CHECK(mtvk_gpu_register_backend(&mock_duplicate) == MTVK_INVALID_ARGUMENT);
    return true;
}

static bool test_enumerate_and_open(void)
{
    mtvk_gpu_device_info info;
    memset(&info, 0xA5, sizeof(info));
    CHECK(mtvk_gpu_enumerate(0, 0, &info) == MTVK_OK);
    CHECK(strcmp(info.name, "Mock GPU") == 0 && info.vendor_id == 0x1234 && info.device_id == 0x5678);
    CHECK(info.memory_bytes == MOCK_MEMORY);
    uint8_t before[sizeof(info)]; memcpy(before, &info, sizeof(info));
    CHECK(mtvk_gpu_enumerate(0, 1, &info) == MTVK_NOT_FOUND);
    CHECK(memcmp(before, &info, sizeof(info)) == 0);
    CHECK(mtvk_gpu_enumerate(1, 0, &info) == MTVK_NOT_FOUND);
    CHECK(mtvk_gpu_enumerate(0, 0, NULL) == MTVK_INVALID_ARGUMENT);

    mtvk_gpu_device *device = NULL;
    CHECK(mtvk_gpu_open(1, 0, &device) == MTVK_NOT_FOUND && device == NULL);
    CHECK(mtvk_gpu_open(0, 5, &device) == MTVK_NOT_FOUND && device == NULL);
    CHECK(mtvk_gpu_open(0, 0, NULL) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_gpu_open(0, 0, &device) == MTVK_OK && device != NULL && g_mock.openings == 1);
    mtvk_gpu_close(0, device);
    CHECK(g_mock.openings == 0);
    mtvk_gpu_close(1, device); /* wrong backend: must not touch the mock */
    CHECK(g_mock.openings == 0);
    mtvk_gpu_close(0, NULL);
    return true;
}

static bool test_registry_limit(void)
{
    static mtvk_gpu_backend_ops extra[MTVK_GPU_BACKENDS_MAX];
    static char names[MTVK_GPU_BACKENDS_MAX][8];
    size_t added = 0;
    for (size_t i = 1; i < MTVK_GPU_BACKENDS_MAX; ++i) {
        (void)snprintf(names[i], sizeof(names[i]), "b%u", (unsigned)i);
        extra[i] = mock_ops;
        extra[i].name = names[i];
        CHECK(mtvk_gpu_register_backend(&extra[i]) == MTVK_OK);
        ++added;
    }
    CHECK(mtvk_gpu_backend_count() == MTVK_GPU_BACKENDS_MAX);
    static const mtvk_gpu_backend_ops overflow = {"overflow", mock_enumerate, mock_open, mock_close};
    CHECK(mtvk_gpu_register_backend(&overflow) == MTVK_LIMIT_EXCEEDED);
    CHECK(added == MTVK_GPU_BACKENDS_MAX - 1);
    return true;
}

static bool test_status_names(void)
{
    CHECK(strcmp(mtvk_status_name(MTVK_OK), "ok") == 0);
    CHECK(strcmp(mtvk_status_name(MTVK_NOT_FOUND), "not found") == 0);
    CHECK(strcmp(mtvk_status_name((mtvk_status)99), "unknown status") == 0);
    return true;
}

int main(void)
{
    if (!test_status_names() || !test_registration_validation() || !test_enumerate_and_open() ||
        !test_registry_limit()) {
        return 1;
    }
    puts("Generic GPU backend API tests passed");
    return 0;
}
