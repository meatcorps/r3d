
#version 330 core

#define FSR_RCAS_F 1
#define FSR_RCAS_PASSTHROUGH_ALPHA 1

#include <fsr1/fsr1_compat.glsl>
#include <fsr1/ffx_fsr1.h>

noperspective in vec2 vTexCoord;

uniform sampler2D uSourceTex;
uniform sampler2D uDepthTex;

uniform vec4 uRcasCon;
uniform vec2 uOutputOffset;

out vec4 FragColor;

vec4 FsrRcasLoadF(ivec2 p)
{
    ivec2 last = textureSize(uSourceTex, 0) - ivec2(1);
    return texelFetch(uSourceTex, clamp(p, ivec2(0), last), 0);
}

void FsrRcasInputF(inout float r, inout float g, inout float b)
{
    // Scene is already tonemapped and encoded for display.
}

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy - uOutputOffset);

    float r, g, b, a;

    FsrRcasF(
        r, g, b, a,
        uvec2(pixel),
        floatBitsToUint(uRcasCon)
    );

    FragColor = vec4(r, g, b, a);

    // Preserve R3D's existing final-blit depth output.
    gl_FragDepth = texture(uDepthTex, vTexCoord).r;
}
