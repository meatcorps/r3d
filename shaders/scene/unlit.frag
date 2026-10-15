/* unlit.frag -- Fragment shader used for unlit objects
 *
 * Copyright (c) 2025-2026 Le Juez Victor
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#version 330 core

// ================================
// Includes
// ================================

#include <ubo/frame.glsl>
#include <ubo/view.glsl>
#include <wrap/fog.glsl>

// ================================
// In - Varyings
// ================================

smooth in vec3 vPosition;       //< For custom shaders
smooth in vec2 vTexCoord;
smooth in vec4 vColor;
smooth in mat3 vTBN;

smooth in float vLinearDepth;

// ================================
// Out - Fragments
// ================================

layout(location = 0) out vec4 FragColor;

// ================================
// Samplers & Uniforms
// ================================

uniform sampler2D uAlbedoMap;
uniform float uAlphaCutoff;
uniform float uCutoffSign;
uniform bool uSilhouetteMask;

// ================================
// User Override
// ================================

#include <user/scene.frag>

// ================================
// Main Function
// ================================

vec3 R3D_PackSilhouetteDepth(float value)
{
    vec3 p = fract(min(value, 0.99999994) * vec3(1.0, 255.0, 65025.0));
    p.xy -= p.yz / 255.0;
    return p;
}

void main()
{
    SceneFragment(vTexCoord, vTBN, uAlphaCutoff, uCutoffSign);

    if (uSilhouetteMask)
    {
        FragColor = vec4(R3D_PackSilhouetteDepth(gl_FragCoord.z), ALPHA);
    }
    else
    {
        FragColor = vec4(ALBEDO, ALPHA);
        FragColor = FogColorMix(FragColor, vLinearDepth);
    }
}
