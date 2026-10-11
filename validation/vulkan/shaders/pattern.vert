// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#version 450
layout(location = 0) out vec2 vUV;
void main() {
    // Fullscreen triangle from gl_VertexIndex; no vertex buffer.
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    vUV = p;                                   // 0..2, spans 0..1 across the viewport
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
