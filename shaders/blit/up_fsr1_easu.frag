
#version 330 core

// Use hardware gathers when the extension is available.
// Otherwise emulate exactly the same 2x2 gather using texelFetch.
#ifdef GL_ARB_gpu_shader5
    #extension GL_ARB_gpu_shader5 : enable
    #define R3D_FSR_HW_GATHER 1
#endif

#define FSR_EASU_F 1

#include <fsr1/fsr1_compat.glsl>
#include <fsr1/ffx_fsr1.h>

noperspective in vec2 vTexCoord;

uniform sampler2D uSourceTex;

uniform vec4 uCon0;
uniform vec4 uCon1;
uniform vec4 uCon2;
uniform vec4 uCon3;

out vec4 FragColor;

#ifndef R3D_FSR_HW_GATHER

vec4 FsrGatherFallback(vec2 p, int channel)
{
    ivec2 size = textureSize(uSourceTex, 0);
    ivec2 base = ivec2(floor(p * vec2(size) - 0.5));
    ivec2 last = size - ivec2(1);

    // OpenGL textureGather component ordering:
    // upper-left, upper-right, lower-right, lower-left.
    vec4 a = texelFetch(uSourceTex, clamp(base + ivec2(0, 1), ivec2(0), last), 0);
    vec4 b = texelFetch(uSourceTex, clamp(base + ivec2(1, 1), ivec2(0), last), 0);
    vec4 c = texelFetch(uSourceTex, clamp(base + ivec2(1, 0), ivec2(0), last), 0);
    vec4 d = texelFetch(uSourceTex, clamp(base, ivec2(0), last), 0);

    return vec4(a[channel], b[channel], c[channel], d[channel]);
}

#endif

vec4 FsrEasuRF(vec2 p)
{
    #ifdef R3D_FSR_HW_GATHER
    return textureGather(uSourceTex, p, 0);
    #else
    return FsrGatherFallback(p, 0);
    #endif
}

vec4 FsrEasuGF(vec2 p)
{
    #ifdef R3D_FSR_HW_GATHER
    return textureGather(uSourceTex, p, 1);
    #else
    return FsrGatherFallback(p, 1);
    #endif
}

vec4 FsrEasuBF(vec2 p)
{
    #ifdef R3D_FSR_HW_GATHER
    return textureGather(uSourceTex, p, 2);
    #else
    return FsrGatherFallback(p, 2);
    #endif
}

void main()
{
    uvec2 pixel = uvec2(ivec2(gl_FragCoord.xy));

    vec3 color;

    FsrEasuF(
        color,
        pixel,
        floatBitsToUint(uCon0),
        floatBitsToUint(uCon1),
        floatBitsToUint(uCon2),
        floatBitsToUint(uCon3)
    );

    // Preserve the original scene alpha independently of RGB reconstruction.
    float alpha = texture(uSourceTex, vTexCoord).a;

    FragColor = vec4(color, alpha);
}
