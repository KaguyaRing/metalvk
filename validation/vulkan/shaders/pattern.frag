// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
#version 450
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
void main() {
    // Four flat quadrants of exact 8-bit values, robust against interpolation.
    float r = vUV.x < 0.5 ? 0.0 : 1.0;
    float g = vUV.y < 0.5 ? 0.0 : 1.0;
    outColor = vec4(r, g, 0.0, 1.0);
}
