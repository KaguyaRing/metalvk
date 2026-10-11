// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
// Backend spy/fault injection tests, not physical display evidence.
#include "display-fixture.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
typedef struct fixture {
    mtvk_display_manager manager;
    bool locked, violation, unplug;
    unsigned applies, disables, resets;
    mtvk_status apply_result, disable_result, reset_result;
    mtvk_display_config received;
    mtvk_display_mode received_mode;
    bool unplug_reset;
} fixture;
static void lock(void *context) { fixture *f = context; if (f->locked) f->violation = true; f->locked = true; }
static void unlock(void *context) { fixture *f = context; if (!f->locked) f->violation = true; f->locked = false; }
static mtvk_status apply(void *context, uint64_t connector, const mtvk_display_mode *mode, const mtvk_display_config *config) {
    fixture *f = context;
    if (f->locked || connector != 7 || mode->id != config->mode_id) f->violation = true;
    ++f->applies; f->received = *config; f->received_mode = *mode;
    mtvk_display_snapshot snapshot;
    if (mtvk_display_copy(&f->manager, &snapshot) != MTVK_OK || !snapshot.busy ||
        mtvk_display_close(&f->manager) != MTVK_BUSY) f->violation = true;
    if (f->unplug && mtvk_display_notify(&f->manager, MTVK_DISPLAY_SIGNAL_UNPLUG) != MTVK_OK) f->violation = true;
    return f->apply_result;
}
static mtvk_status disable(void *context, uint64_t connector) {
    fixture *f = context; if (f->locked || connector != 7) f->violation = true;
    ++f->disables; return f->disable_result;
}
static mtvk_status reset(void *context, uint64_t connector, mtvk_display_reset_reason reason) {
    fixture *f = context; if (f->locked || connector != 7 || reason > MTVK_DISPLAY_RESET_WAKE) f->violation = true;
    ++f->resets;
    if (f->unplug_reset && mtvk_display_notify(&f->manager, MTVK_DISPLAY_SIGNAL_UNPLUG) != MTVK_OK) f->violation = true;
    return f->reset_result;
}
static int invalid_parameters_never_dispatch(void) {
    fixture f = {0};
    const mtvk_display_backend backend = {&f, lock, unlock, apply, disable, reset};
    mtvk_display_capabilities caps = display_caps();
    const mtvk_display_config good = display_hdr();
    mtvk_display_config config = good;
    CHECK(mtvk_display_init(&f.manager, 7, &backend) == MTVK_OK);
    CHECK(mtvk_display_connect(&f.manager, &caps) == MTVK_OK);
    config.transfer = (mtvk_display_transfer)-1;
    CHECK(mtvk_display_apply(&f.manager, &config) == MTVK_INVALID_ARGUMENT);
    config = good; config.transfer = (mtvk_display_transfer)32;
    CHECK(mtvk_display_apply(&f.manager, &config) == MTVK_INVALID_ARGUMENT);
    config = good; config.depth = 8;
    CHECK(mtvk_display_apply(&f.manager, &config) == MTVK_INVALID_ARGUMENT);
    config = good; config.gamut = MTVK_DISPLAY_BT709;
    CHECK(mtvk_display_apply(&f.manager, &config) == MTVK_INVALID_ARGUMENT);
    config = good; config.metadata.max_fall = 1001;
    CHECK(mtvk_display_apply(&f.manager, &config) == MTVK_INVALID_ARGUMENT);
    config = good; config.metadata.min_luminance_milli = config.metadata.max_luminance_milli;
    CHECK(mtvk_display_apply(&f.manager, &config) == MTVK_INVALID_ARGUMENT);
    config = good; config.metadata.primary_x[0] = UINT16_MAX;
    CHECK(mtvk_display_apply(&f.manager, &config) == MTVK_INVALID_ARGUMENT);
    config = good; config.metadata_present = false;
    CHECK(mtvk_display_apply(&f.manager, &config) == MTVK_INVALID_ARGUMENT);
    config = good; config.transfer = MTVK_DISPLAY_HLG;
    CHECK(mtvk_display_apply(&f.manager, &config) == MTVK_INVALID_ARGUMENT);
    config = good; config.mode_id = 2;
    CHECK(mtvk_display_apply(&f.manager, &config) == MTVK_NOT_SUPPORTED);
    config = good; config.depth = 16;
    CHECK(mtvk_display_apply(&f.manager, &config) == MTVK_NOT_SUPPORTED);
    CHECK(!f.applies && !f.resets && !f.disables);
    caps.mode_count = MTVK_DISPLAY_MAX_MODES + 1;
    CHECK(mtvk_display_connect(&f.manager, &caps) == MTVK_INVALID_ARGUMENT);
    caps = display_caps(); caps.mode_count = 2; caps.modes[1] = caps.modes[0];
    CHECK(mtvk_display_connect(&f.manager, &caps) == MTVK_INVALID_ARGUMENT);
    caps = display_caps(); caps.modes[0].hsync_end = caps.modes[0].htotal + 1;
    CHECK(mtvk_display_connect(&f.manager, &caps) == MTVK_INVALID_ARGUMENT);
    caps = display_caps(); caps.transfers |= 8u;
    CHECK(mtvk_display_connect(&f.manager, &caps) == MTVK_INVALID_ARGUMENT);
    caps = display_caps(); caps.max_depth = 8;
    CHECK(mtvk_display_connect(&f.manager, &caps) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_display_copy(&f.manager, &f.manager.state) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_display_connect(&f.manager, &f.manager.state.capabilities) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_display_apply(&f.manager, &f.manager.state.desired) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_display_init(NULL, 7, &backend) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_display_copy(NULL, NULL) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_display_apply(NULL, &good) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_display_notify(&f.manager, (mtvk_display_signal)99) == MTVK_INVALID_ARGUMENT);
    CHECK(mtvk_display_recover(&f.manager, (mtvk_display_reset_reason)99) == MTVK_INVALID_ARGUMENT);
    uint32_t random = UINT32_C(0x284391b7);
    for (unsigned i = 0; i < 20000; ++i) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        config = good;
        config.transfer = (mtvk_display_transfer)(3u + (random & UINT32_C(0x3fffffff)));
        CHECK(mtvk_display_apply(&f.manager, &config) == MTVK_INVALID_ARGUMENT);
        caps = display_caps();
        caps.modes[0].pixel_clock_khz = MTVK_DISPLAY_MAX_CLOCK_KHZ + 1u + (random % 10000u);
        CHECK(mtvk_display_connect(&f.manager, &caps) == MTVK_INVALID_ARGUMENT);
    }
    CHECK(!f.applies && !f.resets && !f.disables);
    CHECK(mtvk_display_close(&f.manager) == MTVK_OK);
    return 0;
}
int main(void) {
    fixture f = {0};
    const mtvk_display_backend backend = {&f, lock, unlock, apply, disable, reset};
    const mtvk_display_capabilities caps = display_caps();
    const mtvk_display_config sdr = display_sdr(), hdr = display_hdr();
    mtvk_display_snapshot snapshot;
    uint64_t refresh = 0;
    CHECK(mtvk_display_refresh_millihz(&caps.modes[0], &refresh) == MTVK_OK && refresh == 60000);
    CHECK(mtvk_display_init(&f.manager, 7, &backend) == MTVK_OK);
    CHECK(mtvk_display_apply(&f.manager, &sdr) == MTVK_INVALID_CONTEXT && !f.applies);
    CHECK(mtvk_display_connect(&f.manager, &caps) == MTVK_OK);
    CHECK(mtvk_display_apply(&f.manager, &sdr) == MTVK_OK);
    CHECK(mtvk_display_apply(&f.manager, &hdr) == MTVK_OK && f.received.metadata.max_cll == 1000);
    CHECK(f.received.metadata.primary_x[0] == hdr.metadata.primary_x[0] &&
        f.received.metadata.white_y == hdr.metadata.white_y && f.received.depth == hdr.depth &&
        f.received_mode.pixel_clock_khz == caps.modes[0].pixel_clock_khz &&
        f.received_mode.hsync_start == caps.modes[0].hsync_start);
    CHECK(mtvk_display_copy(&f.manager, &snapshot) == MTVK_OK && snapshot.has_applied && snapshot.phase == MTVK_DISPLAY_ACTIVE);
    CHECK(mtvk_display_connect(&f.manager, &caps) == MTVK_INVALID_CONTEXT);
    f.apply_result = MTVK_NOT_SUPPORTED;
    CHECK(mtvk_display_apply(&f.manager, &sdr) == MTVK_NOT_SUPPORTED);
    CHECK(mtvk_display_copy(&f.manager, &snapshot) == MTVK_OK && snapshot.applied.transfer == MTVK_DISPLAY_PQ);
    f.apply_result = MTVK_OK;
    CHECK(mtvk_display_notify(&f.manager, MTVK_DISPLAY_SIGNAL_SUSPEND) == MTVK_OK);
    CHECK(mtvk_display_apply(&f.manager, &sdr) == MTVK_INVALID_CONTEXT);
    CHECK(mtvk_display_recover(&f.manager, MTVK_DISPLAY_RESET_WAKE) == MTVK_OK && f.resets == 1);
    CHECK(f.received.transfer == MTVK_DISPLAY_PQ);
    CHECK(mtvk_display_apply(&f.manager, &sdr) == MTVK_OK && !f.received.metadata_present &&
        !f.received.metadata.max_luminance_milli);
    CHECK(mtvk_display_apply(&f.manager, &hdr) == MTVK_OK);
    f.reset_result = MTVK_NOT_SUPPORTED;
    CHECK(mtvk_display_recover(&f.manager, MTVK_DISPLAY_RESET_LINK) == MTVK_NOT_SUPPORTED);
    CHECK(mtvk_display_copy(&f.manager, &snapshot) == MTVK_OK && snapshot.phase == MTVK_DISPLAY_FAULT && !snapshot.has_applied);
    CHECK(mtvk_display_close(&f.manager) == MTVK_CLEANUP_REQUIRED);
    f.reset_result = MTVK_OK;
    CHECK(mtvk_display_recover(&f.manager, MTVK_DISPLAY_RESET_MANUAL) == MTVK_OK);
    f.unplug = true;
    CHECK(mtvk_display_apply(&f.manager, &sdr) == MTVK_CLEANUP_REQUIRED);
    CHECK(mtvk_display_copy(&f.manager, &snapshot) == MTVK_OK && snapshot.phase == MTVK_DISPLAY_DISCONNECTED && !snapshot.has_desired);
    CHECK(mtvk_display_connect(&f.manager, &caps) == MTVK_CLEANUP_REQUIRED);
    CHECK(mtvk_display_close(&f.manager) == MTVK_CLEANUP_REQUIRED);
    CHECK(mtvk_display_disable(&f.manager) == MTVK_OK);
    CHECK(mtvk_display_close(&f.manager) == MTVK_OK);
    CHECK(!f.locked && !f.violation);
    fixture missing = {0};
    const mtvk_display_backend absent = {&missing, lock, unlock, NULL, NULL, NULL};
    CHECK(mtvk_display_init(&missing.manager, 7, &absent) == MTVK_OK);
    CHECK(mtvk_display_connect(&missing.manager, &caps) == MTVK_OK);
    CHECK(mtvk_display_apply(&missing.manager, &sdr) == MTVK_NOT_SUPPORTED);
    CHECK(mtvk_display_recover(&missing.manager, MTVK_DISPLAY_RESET_MANUAL) == MTVK_NOT_SUPPORTED);
    CHECK(mtvk_display_disable(&missing.manager) == MTVK_NOT_SUPPORTED);
    CHECK(mtvk_display_close(&missing.manager) == MTVK_OK);
    CHECK(invalid_parameters_never_dispatch() == 0);
    // Restoration failure after an actual reset cannot claim prior state survived.
    fixture failure = {0};
    const mtvk_display_backend failing = {&failure, lock, unlock, apply, disable, reset};
    CHECK(mtvk_display_init(&failure.manager, 7, &failing) == MTVK_OK);
    CHECK(mtvk_display_connect(&failure.manager, &caps) == MTVK_OK);
    CHECK(mtvk_display_apply(&failure.manager, &hdr) == MTVK_OK);
    failure.apply_result = MTVK_OUT_OF_MEMORY;
    CHECK(mtvk_display_recover(&failure.manager, MTVK_DISPLAY_RESET_TIMEOUT) == MTVK_OUT_OF_MEMORY);
    CHECK(mtvk_display_copy(&failure.manager, &snapshot) == MTVK_OK && snapshot.phase == MTVK_DISPLAY_FAULT && snapshot.needs_quiesce);
    failure.disable_result = MTVK_BUSY;
    CHECK(mtvk_display_disable(&failure.manager) == MTVK_BUSY);
    CHECK(mtvk_display_close(&failure.manager) == MTVK_CLEANUP_REQUIRED);
    failure.disable_result = MTVK_OK;
    CHECK(mtvk_display_disable(&failure.manager) == MTVK_OK);
    failure.apply_result = MTVK_OK;
    CHECK(mtvk_display_apply(&failure.manager, &hdr) == MTVK_OK);
    failure.unplug_reset = true;
    const unsigned calls = failure.applies;
    CHECK(mtvk_display_recover(&failure.manager, MTVK_DISPLAY_RESET_LINK) == MTVK_CLEANUP_REQUIRED);
    CHECK(failure.applies == calls); // No replay after observed unplug.
    CHECK(mtvk_display_disable(&failure.manager) == MTVK_OK);
    CHECK(mtvk_display_connect(&failure.manager, &caps) == MTVK_OK);
    CHECK(mtvk_display_copy(&failure.manager, &snapshot) == MTVK_OK && !snapshot.has_desired);
    failure.apply_result = (mtvk_status)999;
    CHECK(mtvk_display_apply(&failure.manager, &sdr) == MTVK_CLEANUP_REQUIRED);
    CHECK(mtvk_display_disable(&failure.manager) == MTVK_OK);
    failure.manager.state.generation = UINT64_MAX; // Test-only exhaustion injection.
    CHECK(mtvk_display_apply(&failure.manager, &sdr) == MTVK_LIMIT_EXCEEDED);
    CHECK(mtvk_display_notify(&failure.manager, MTVK_DISPLAY_SIGNAL_LINK_LOST) == MTVK_LIMIT_EXCEEDED);
    CHECK(mtvk_display_copy(&failure.manager, &snapshot) == MTVK_OK && snapshot.phase == MTVK_DISPLAY_FAULT);
    CHECK(mtvk_display_disable(&failure.manager) == MTVK_OK);
    CHECK(mtvk_display_close(&failure.manager) == MTVK_OK);
    CHECK(!failure.violation);
    puts("PASS display parameter/HDR forwarding, reset replay and fail-closed signals (backend spy only)");
    return 0;
}
