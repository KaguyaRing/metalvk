// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_DISPLAY_CONTROL_H
#define MTVK_DISPLAY_CONTROL_H
#include "base/status.h"
#define MTVK_DISPLAY_MAX_MODES 32u
#define MTVK_DISPLAY_MAX_DIMENSION 16384u
#define MTVK_DISPLAY_MAX_TOTAL 65535u
#define MTVK_DISPLAY_MAX_CLOCK_KHZ 4000000u
#define MTVK_DISPLAY_MAX_LUMINANCE_MILLI 10000000u
#define MTVK_DISPLAY_CHROMATICITY_SCALE 50000u
#define MTVK_DISPLAY_DEPTH_MIN 8u
#define MTVK_DISPLAY_DEPTH_MAX 16u
#define MTVK_DISPLAY_HDR_DEPTH_MIN 10u
typedef enum mtvk_display_transfer {
    MTVK_DISPLAY_SDR = 0, MTVK_DISPLAY_PQ, MTVK_DISPLAY_HLG
} mtvk_display_transfer;
#define MTVK_DISPLAY_TRANSFER_BIT(value) (1u << (value))
#define MTVK_DISPLAY_TRANSFERS_ALL 7u
typedef enum mtvk_display_gamut { MTVK_DISPLAY_BT709 = 0, MTVK_DISPLAY_BT2020 } mtvk_display_gamut;
#define MTVK_DISPLAY_GAMUT_BIT(value) (1u << (value))
#define MTVK_DISPLAY_GAMUTS_ALL 3u
/* Progressive RGB timings only in v1; clocks in kHz, sync endpoints in pixels/
 * lines. Modes are backend-verified sink + hardware capabilities, not EDID bytes. */
typedef struct mtvk_display_mode {
    uint32_t id, width, height, htotal, vtotal;
    uint32_t hsync_start, hsync_end, vsync_start, vsync_end, pixel_clock_khz;
    bool hsync_positive, vsync_positive;
} mtvk_display_mode;
typedef struct mtvk_display_capabilities {
    uint32_t mode_count, transfers, gamuts;
    uint8_t min_depth, max_depth;
    bool static_metadata;
    mtvk_display_mode modes[MTVK_DISPLAY_MAX_MODES];
} mtvk_display_capabilities;
/* CTA static metadata semantics, NOT an HDMI/DP wire packet. xy units 1/50000;
 * luminance in milli-cd/m2, CLL/FALL in cd/m2; zero CLL/FALL means unspecified. */
typedef struct mtvk_display_hdr {
    uint16_t primary_x[3], primary_y[3], white_x, white_y;
    uint32_t min_luminance_milli, max_luminance_milli;
    uint16_t max_cll, max_fall;
} mtvk_display_hdr;
typedef struct mtvk_display_config {
    uint32_t mode_id;
    mtvk_display_transfer transfer;
    mtvk_display_gamut gamut;
    uint8_t depth;
    bool metadata_present;
    mtvk_display_hdr metadata;
} mtvk_display_config;
typedef enum mtvk_display_phase {
    MTVK_DISPLAY_DISCONNECTED = 0, MTVK_DISPLAY_IDLE, MTVK_DISPLAY_ACTIVE,
    MTVK_DISPLAY_APPLYING, MTVK_DISPLAY_SUSPENDED, MTVK_DISPLAY_LOST,
    MTVK_DISPLAY_RECOVERING, MTVK_DISPLAY_FAULT, MTVK_DISPLAY_CLOSED
} mtvk_display_phase;
typedef enum mtvk_display_reset_reason {
    MTVK_DISPLAY_RESET_MANUAL = 0, MTVK_DISPLAY_RESET_LINK,
    MTVK_DISPLAY_RESET_TIMEOUT, MTVK_DISPLAY_RESET_WAKE
} mtvk_display_reset_reason;
typedef enum mtvk_display_signal {
    MTVK_DISPLAY_SIGNAL_UNPLUG = 0, MTVK_DISPLAY_SIGNAL_LINK_LOST,
    MTVK_DISPLAY_SIGNAL_SUSPEND
} mtvk_display_signal;
typedef struct mtvk_display_snapshot {
    uint64_t connector_id, generation;
    mtvk_display_phase phase;
    bool busy, has_desired, has_applied, needs_quiesce;
    mtvk_status last_error;
    mtvk_display_config desired, applied;
    mtvk_display_capabilities capabilities;
} mtvk_display_snapshot;
typedef struct mtvk_display_backend {
    void *context;
    /* Same owner lock for all state access. Never held during hardware calls.
     * The lock/context live until close + caller-side quiescence. */
    void (*lock)(void *context);
    void (*unlock)(void *context);
    /* Synchronous completion: OK means hardware state established, not queued.
     * apply/disable failure must preserve prior hardware state, or return
     * CLEANUP_REQUIRED when rollback cannot be guaranteed. Missing callbacks
     * return NOT_SUPPORTED, never success. Hardware must be bounded by backend
     * timeouts; generic C cannot preempt a hung callback. */
    mtvk_status (*apply)(void *context, uint64_t connector, const mtvk_display_mode *mode,
                         const mtvk_display_config *config);
    mtvk_status (*disable)(void *context, uint64_t connector);
    /* Successful reset leaves output disabled until apply restores it. The
     * backend must serialize sink changes with hardware operations and check
     * link identity/limits again; generic generation checks cannot stop a
     * physical operation racing an unplug. Disable also drains prior work. */
    mtvk_status (*reset)(void *context, uint64_t connector, mtvk_display_reset_reason reason);
} mtvk_display_backend;
/* Caller-owned, fixed-storage manager; fields private by contract. No allocation,
 * no IPC ABI, no GPU command ABI. Do not copy/reinitialize a published manager.
 * Backend pointers/policy are immutable after init. Caller owns lifetime and
 * stops external producers before close/reclamation. One manager per connector. */
typedef struct mtvk_display_manager {
    mtvk_display_backend backend;
    mtvk_display_snapshot state;
} mtvk_display_manager;
MTVK_EXTERN_C_BEGIN
mtvk_status mtvk_display_validate_capabilities(const mtvk_display_capabilities *caps);
mtvk_status mtvk_display_validate_config(const mtvk_display_capabilities *caps,
                                       const mtvk_display_config *config);
mtvk_status mtvk_display_refresh_millihz(const mtvk_display_mode *mode, uint64_t *out);
mtvk_status mtvk_display_init(mtvk_display_manager *manager, uint64_t connector,
                             const mtvk_display_backend *backend);
/* Connection/capability publication requires disconnected or idle, quiescent
 * output. Active capability changes require disable first. Replug drops old
 * desired state, preventing stale mode/HDR replay onto a different sink. */
mtvk_status mtvk_display_connect(mtvk_display_manager *manager, const mtvk_display_capabilities *caps);
mtvk_status mtvk_display_copy(mtvk_display_manager *manager, mtvk_display_snapshot *out);
mtvk_status mtvk_display_apply(mtvk_display_manager *manager, const mtvk_display_config *config);
mtvk_status mtvk_display_disable(mtvk_display_manager *manager);
/* Signals invalidate in-flight publication using generation tokens. IRQ handlers
 * must defer these calls to a thread unless the supplied lock is IRQ-safe. */
mtvk_status mtvk_display_notify(mtvk_display_manager *manager, mtvk_display_signal signal);
/* reset restores desired state only after successful hardware reset. Reset/wake
 * cannot succeed without real reset callback; any restoration failure faults.
 * Backend's parent must coordinate resets affecting multiple outputs/GPU DMA. */
mtvk_status mtvk_display_recover(mtvk_display_manager *manager, mtvk_display_reset_reason reason);
/* No forced cleanup: busy/in-flight -> BUSY; connected/uncertain hardware must
 * be disabled successfully first. Close is not a kext unload barrier. */
mtvk_status mtvk_display_close(mtvk_display_manager *manager);
MTVK_EXTERN_C_END
#endif
