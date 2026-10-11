// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#ifndef MTVK_VK_CONTEXT_INTERNAL_H
#define MTVK_VK_CONTEXT_INTERNAL_H
#include "vk/context.h"
/* Same owner-thread serialization as the public context/queue contract. */
VkResult mtvk_vk_context_hold_target(mtvk_vk_context *context);
void mtvk_vk_context_release_target(mtvk_vk_context *context);
bool mtvk_vk_context_output_valid(const mtvk_vk_context *context, const void *out, size_t size);
#endif
