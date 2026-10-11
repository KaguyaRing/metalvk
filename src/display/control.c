// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#include "display/init.h"
#include "base/span.h"
static bool valid(const mtvk_display_manager *m) {
    return m && m->backend.lock && m->backend.unlock && m->state.connector_id;
}
static mtvk_status advance(mtvk_display_snapshot *state) {
    if (state->generation == UINT64_MAX) return MTVK_LIMIT_EXCEEDED;
    ++state->generation;
    return MTVK_OK;
}
static mtvk_status backend_result(mtvk_status result) {
    return result >= MTVK_OK && result <= MTVK_CLEANUP_REQUIRED ? result : MTVK_CLEANUP_REQUIRED;
}
mtvk_status mtvk_display_init(mtvk_display_manager *m, uint64_t connector,
                             const mtvk_display_backend *backend) {
    if (!m || !connector || !backend || !backend->lock || !backend->unlock) return MTVK_INVALID_ARGUMENT;
    const mtvk_display_backend copied = *backend;
    const mtvk_display_manager initialized = {copied, {.connector_id = connector, .generation = 1}};
    *m = initialized;
    return MTVK_OK;
}
mtvk_status mtvk_display_copy(mtvk_display_manager *m, mtvk_display_snapshot *out) {
    if (!valid(m) || !out || !mtvk_span_valid(out, sizeof(*out)) ||
        mtvk_spans_overlap(m, sizeof(*m), out, sizeof(*out))) return MTVK_INVALID_ARGUMENT;
    m->backend.lock(m->backend.context);
    *out = m->state;
    m->backend.unlock(m->backend.context);
    return MTVK_OK;
}
mtvk_status mtvk_display_connect(mtvk_display_manager *m, const mtvk_display_capabilities *caps) {
    if (!valid(m) || !caps || !mtvk_span_valid(caps, sizeof(*caps)) ||
        mtvk_spans_overlap(m, sizeof(*m), caps, sizeof(*caps))) return MTVK_INVALID_ARGUMENT;
    const mtvk_display_capabilities copied = *caps;
    mtvk_status result = mtvk_display_validate_capabilities(&copied);
    if (result != MTVK_OK) return result;
    m->backend.lock(m->backend.context);
    if (m->state.busy) result = MTVK_BUSY;
    else if (m->state.needs_quiesce) result = MTVK_CLEANUP_REQUIRED;
    else if (m->state.phase != MTVK_DISPLAY_DISCONNECTED && m->state.phase != MTVK_DISPLAY_IDLE)
        result = MTVK_INVALID_CONTEXT;
    else if ((result = advance(&m->state)) == MTVK_OK) {
        m->state.capabilities = copied;
        m->state.phase = MTVK_DISPLAY_IDLE;
        m->state.has_desired = false;
        m->state.has_applied = false;
        m->state.desired = (mtvk_display_config){0};
        m->state.applied = (mtvk_display_config){0};
        m->state.last_error = MTVK_OK;
    }
    m->backend.unlock(m->backend.context);
    return result;
}
typedef enum operation { APPLY, DISABLE, RECOVER } operation;
static mtvk_status execute(mtvk_display_manager *m, operation op,
                           const mtvk_display_config *config, mtvk_display_reset_reason reason) {
    if (!valid(m)) return MTVK_INVALID_ARGUMENT;
    mtvk_display_config requested = {0};
    if (op == APPLY) {
        if (!config || !mtvk_span_valid(config, sizeof(*config)) ||
            mtvk_spans_overlap(m, sizeof(*m), config, sizeof(*config))) return MTVK_INVALID_ARGUMENT;
        requested = *config;
    }
    if (op == RECOVER && (reason < MTVK_DISPLAY_RESET_MANUAL || reason > MTVK_DISPLAY_RESET_WAKE))
        return MTVK_INVALID_ARGUMENT;
    mtvk_status result = MTVK_OK;
    mtvk_display_mode mode = {0};
    m->backend.lock(m->backend.context);
    const mtvk_display_phase previous = m->state.phase;
    const bool previous_quiesce = m->state.needs_quiesce;
    if (m->state.busy) result = MTVK_BUSY;
    else if (previous == MTVK_DISPLAY_CLOSED ||
        (previous == MTVK_DISPLAY_DISCONNECTED && !(op == DISABLE && m->state.needs_quiesce)))
        result = MTVK_INVALID_CONTEXT;
    else if (op == APPLY && previous != MTVK_DISPLAY_IDLE && previous != MTVK_DISPLAY_ACTIVE) result = MTVK_INVALID_CONTEXT;
    else if (op == APPLY && !m->backend.apply) result = MTVK_NOT_SUPPORTED;
    else if (op == DISABLE && !m->backend.disable) result = MTVK_NOT_SUPPORTED;
    else if (op == RECOVER && !m->backend.reset) result = MTVK_NOT_SUPPORTED;
    if (result == MTVK_OK && op == RECOVER && m->state.has_desired) {
        requested = m->state.desired;
        if (!m->backend.apply) result = MTVK_NOT_SUPPORTED;
    }
    const bool restore = op == APPLY || (op == RECOVER && m->state.has_desired);
    if (result == MTVK_OK && restore) result = mtvk_display_validate_config(&m->state.capabilities, &requested);
    if (result == MTVK_OK && restore)
        for (uint32_t i = 0; i < m->state.capabilities.mode_count; ++i)
            if (m->state.capabilities.modes[i].id == requested.mode_id) mode = m->state.capabilities.modes[i];
    // At generation exhaustion only draining is allowed; phase comparison still
    // invalidates an operation if another signal cannot advance the counter.
    if (result == MTVK_OK && !(op == DISABLE && m->state.generation == UINT64_MAX))
        result = advance(&m->state);
    if (result != MTVK_OK) { m->backend.unlock(m->backend.context); return result; }
    const uint64_t token = m->state.generation;
    const mtvk_display_phase working = op == RECOVER ? MTVK_DISPLAY_RECOVERING : MTVK_DISPLAY_APPLYING;
    m->state.busy = true;
    m->state.needs_quiesce = true;
    m->state.phase = working;
    if (op == RECOVER) {
        m->state.has_applied = false;
        m->state.applied = (mtvk_display_config){0};
    }
    m->backend.unlock(m->backend.context);

    if (op == APPLY) result = backend_result(m->backend.apply(m->backend.context, m->state.connector_id, &mode, &requested));
    else if (op == DISABLE) result = backend_result(m->backend.disable(m->backend.context, m->state.connector_id));
    else {
        result = backend_result(m->backend.reset(m->backend.context, m->state.connector_id, reason));
        // Skip replay when a signal has already invalidated this operation.
        // Hardware must independently serialize sink changes vs mode setting.
        m->backend.lock(m->backend.context);
        const bool current = m->state.generation == token && m->state.phase == working;
        m->backend.unlock(m->backend.context);
        if (result == MTVK_OK && !current) result = MTVK_INVALID_CONTEXT;
        if (result == MTVK_OK && restore)
            result = backend_result(m->backend.apply(m->backend.context, m->state.connector_id, &mode, &requested));
    }
    m->backend.lock(m->backend.context);
    m->state.busy = false;
    if (m->state.generation != token || m->state.phase != working) {
        // A signal won the race. Never publish a stale success. Caller must
        // arrange hardware quiescence; the manager cannot undo an unplug.
        m->state.has_applied = false;
        m->state.applied = (mtvk_display_config){0};
        m->state.last_error = MTVK_CLEANUP_REQUIRED;
        m->state.needs_quiesce = true;
        result = MTVK_CLEANUP_REQUIRED;
    } else {
        m->state.last_error = result;
        if (result == MTVK_OK) {
            m->state.phase = restore ? MTVK_DISPLAY_ACTIVE :
                previous == MTVK_DISPLAY_DISCONNECTED ? MTVK_DISPLAY_DISCONNECTED : MTVK_DISPLAY_IDLE;
            m->state.needs_quiesce = false;
            m->state.has_applied = restore;
            if (restore) {
                m->state.desired = requested;
                m->state.applied = requested;
                m->state.has_desired = true;
            } else {
                m->state.has_desired = false;
                m->state.desired = (mtvk_display_config){0};
                m->state.applied = (mtvk_display_config){0};
            }
        } else if (op == RECOVER || result == MTVK_CLEANUP_REQUIRED) {
            m->state.phase = MTVK_DISPLAY_FAULT;
            m->state.has_applied = false;
            m->state.applied = (mtvk_display_config){0};
        } else {
            m->state.phase = previous;
            m->state.needs_quiesce = previous_quiesce;
        }
    }
    m->backend.unlock(m->backend.context);
    return result;
}
mtvk_status mtvk_display_apply(mtvk_display_manager *m, const mtvk_display_config *config) {
    return execute(m, APPLY, config, MTVK_DISPLAY_RESET_MANUAL);
}
mtvk_status mtvk_display_disable(mtvk_display_manager *m) { return execute(m, DISABLE, NULL, MTVK_DISPLAY_RESET_MANUAL); }
mtvk_status mtvk_display_recover(mtvk_display_manager *m, mtvk_display_reset_reason reason) { return execute(m, RECOVER, NULL, reason); }
mtvk_status mtvk_display_notify(mtvk_display_manager *m, mtvk_display_signal signal) {
    if (!valid(m) || signal < MTVK_DISPLAY_SIGNAL_UNPLUG || signal > MTVK_DISPLAY_SIGNAL_SUSPEND)
        return MTVK_INVALID_ARGUMENT;
    m->backend.lock(m->backend.context);
    mtvk_status result = MTVK_OK;
    if (m->state.phase == MTVK_DISPLAY_CLOSED) result = MTVK_INVALID_CONTEXT;
    else if (m->state.phase == MTVK_DISPLAY_DISCONNECTED && signal != MTVK_DISPLAY_SIGNAL_UNPLUG)
        result = MTVK_INVALID_CONTEXT;
    else {
        result = advance(&m->state);
        m->state.needs_quiesce = m->state.needs_quiesce || m->state.busy || m->state.has_applied;
        m->state.phase = result != MTVK_OK ? MTVK_DISPLAY_FAULT :
            signal == MTVK_DISPLAY_SIGNAL_UNPLUG ? MTVK_DISPLAY_DISCONNECTED :
            signal == MTVK_DISPLAY_SIGNAL_SUSPEND ? MTVK_DISPLAY_SUSPENDED : MTVK_DISPLAY_LOST;
        m->state.has_applied = false;
        m->state.applied = (mtvk_display_config){0};
        if (result != MTVK_OK) {
            m->state.needs_quiesce = true;
            m->state.last_error = result;
        }
        if (signal == MTVK_DISPLAY_SIGNAL_UNPLUG) {
            m->state.has_desired = false;
            m->state.desired = (mtvk_display_config){0};
            m->state.capabilities = (mtvk_display_capabilities){0};
        }
    }
    m->backend.unlock(m->backend.context);
    return result;
}
mtvk_status mtvk_display_close(mtvk_display_manager *m) {
    if (!valid(m)) return MTVK_INVALID_ARGUMENT;
    m->backend.lock(m->backend.context);
    mtvk_status result = MTVK_OK;
    if (m->state.busy) result = MTVK_BUSY;
    else if (m->state.needs_quiesce) result = MTVK_CLEANUP_REQUIRED;
    else if (m->state.phase != MTVK_DISPLAY_IDLE && m->state.phase != MTVK_DISPLAY_DISCONNECTED &&
        m->state.phase != MTVK_DISPLAY_CLOSED) result = MTVK_CLEANUP_REQUIRED;
    else if (m->state.phase != MTVK_DISPLAY_CLOSED) m->state.phase = MTVK_DISPLAY_CLOSED;
    m->backend.unlock(m->backend.context);
    return result;
}
