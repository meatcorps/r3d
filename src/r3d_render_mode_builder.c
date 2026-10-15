#include <r3d/r3d_render_mode_builder.h>

#include "./modules/r3d_render.h"

void R3D_PushBuiltRenderMode(R3D_BuiltRenderMode built)
{
    r3d_render_built_mode_push(built);
}

void R3D_PopBuiltRenderMode(void)
{
    r3d_render_built_mode_pop();
}

void R3D_UnloadBuiltRenderMode(R3D_BuiltRenderMode* built)
{
    if (!built) return;

    if (built->effect)
        R3D_UnloadSurfaceEffect(built->effect);

    *built = (R3D_BuiltRenderMode){0};
}

R3D_BuiltRenderMode R3D_RenderModeBuildDefault(void)
{
    return (R3D_BuiltRenderMode){0};
}

R3D_BuiltRenderMode R3D_RenderModeBuildUnlit(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask = R3D_RENDER_MODE_OVERRIDE_UNLIT;
    built.mode.unlit = true;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildAlphaBlend(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY |
        R3D_RENDER_MODE_OVERRIDE_BLEND;

    built.mode.transparencyMode = R3D_TRANSPARENCY_BLEND;
    built.mode.blendMode = R3D_BLEND_ALPHA;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildAdditive(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY |
        R3D_RENDER_MODE_OVERRIDE_BLEND;

    built.mode.transparencyMode = R3D_TRANSPARENCY_BLEND;
    built.mode.blendMode = R3D_BLEND_ADDITIVE;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildMultiply(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY |
        R3D_RENDER_MODE_OVERRIDE_BLEND;

    built.mode.transparencyMode = R3D_TRANSPARENCY_BLEND;
    built.mode.blendMode = R3D_BLEND_MULTIPLY;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildDoubleSided(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask = R3D_RENDER_MODE_OVERRIDE_CULL;
    built.mode.cullMode = R3D_CULL_NONE;

    return built;
}

static float r3d_mode_clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static R3D_BuiltRenderMode r3d_mode_build_colored_forward(Color color, float opacity, R3D_CompareMode depthCompare)
{
    R3D_BuiltRenderMode built = {0};

    color.a = (unsigned char)(r3d_mode_clamp01(opacity) * 255.0f + 0.5f);

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY |
        R3D_RENDER_MODE_OVERRIDE_BLEND |
        R3D_RENDER_MODE_OVERRIDE_DEPTH_COMPARE |
        R3D_RENDER_MODE_OVERRIDE_UNLIT |
        R3D_RENDER_MODE_OVERRIDE_ALBEDO |
        R3D_RENDER_MODE_OVERRIDE_SHADOW;

    built.mode.transparencyMode = R3D_TRANSPARENCY_BLEND;
    built.mode.blendMode = R3D_BLEND_ALPHA;
    built.mode.depthCompare = depthCompare;
    built.mode.unlit = true;
    built.mode.albedoColor = color;
    built.mode.shadowCastMode = R3D_SHADOW_CAST_DISABLED;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildXRay(Color color, float opacity)
{
    return r3d_mode_build_colored_forward(color, opacity, R3D_COMPARE_ALWAYS);
}

R3D_BuiltRenderMode R3D_RenderModeBuildOccludedOnly(Color color, float opacity)
{
    return r3d_mode_build_colored_forward(color, opacity, R3D_COMPARE_GREATER);
}

R3D_BuiltRenderMode R3D_RenderModeBuildWireframe(Color color)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_PASS |
        R3D_RENDER_MODE_OVERRIDE_WIRE_COLOR |
        R3D_RENDER_MODE_OVERRIDE_CULL;

    built.mode.renderPass = R3D_RENDER_PASS_WIREFRAME;
    built.mode.wireColor = color;
    built.mode.cullMode = R3D_CULL_NONE;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildWireframeOverlay(Color color)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_PASS |
        R3D_RENDER_MODE_OVERRIDE_WIRE_COLOR;

    built.mode.renderPass = R3D_RENDER_PASS_WIREFRAME_OVERLAY;
    built.mode.wireColor = color;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildSilhouette(Color color)
{
    static const char source[] =
        "uniform vec3 silhouetteColor = vec3(1.0, 1.0, 1.0);\n"
        "void fragment() {\n"
        "    ALBEDO = silhouetteColor;\n"
        "}\n";

    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_UNLIT |
        R3D_RENDER_MODE_OVERRIDE_ALBEDO |
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY;

    built.mode.unlit = true;
    built.mode.albedoColor = color;
    built.mode.transparencyMode =
        color.a == 255 ? R3D_TRANSPARENCY_OPAQUE : R3D_TRANSPARENCY_BLEND;

    built.effect = R3D_LoadSurfaceEffectFromMemory(source);

    if (built.effect)
    {
        Vector3 rgb = {
            color.r / 255.0f,
            color.g / 255.0f,
            color.b / 255.0f
        };

        R3D_SetSurfaceEffectUniform(built.effect, "silhouetteColor", &rgb);
    }

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildScreen(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY |
        R3D_RENDER_MODE_OVERRIDE_BLEND;

    built.mode.transparencyMode = R3D_TRANSPARENCY_BLEND;
    built.mode.blendMode = R3D_BLEND_SCREEN;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildOverlay(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_PASS |
        R3D_RENDER_MODE_OVERRIDE_DEPTH_COMPARE |
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY |
        R3D_RENDER_MODE_OVERRIDE_BLEND;

    built.mode.renderPass = R3D_RENDER_PASS_OVERLAY;
    built.mode.depthCompare = R3D_COMPARE_ALWAYS;
    built.mode.transparencyMode = R3D_TRANSPARENCY_BLEND;
    built.mode.blendMode = R3D_BLEND_ALPHA;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildDepthOnly(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_PASS |
        R3D_RENDER_MODE_OVERRIDE_DEPTH_WRITE;

    built.mode.renderPass = R3D_RENDER_PASS_DEPTH_ONLY;
    built.mode.depthWrite = true;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildDitheredTransparency(float coverage)
{
    static const char source[] =
        "uniform float coverage = 1.0;\n"
        "void fragment() {\n"
        "    int x = int(mod(floor(gl_FragCoord.x), 4.0));\n"
        "    int y = int(mod(floor(gl_FragCoord.y), 4.0));\n"
        "    const float bayer[16] = float[16](\n"
        "        0.0, 8.0, 2.0, 10.0,\n"
        "        12.0, 4.0, 14.0, 6.0,\n"
        "        3.0, 11.0, 1.0, 9.0,\n"
        "        15.0, 7.0, 13.0, 5.0);\n"
        "    if (coverage <= (bayer[x + y * 4] + 0.5) / 16.0)\n"
        "        discard;\n"
        "}\n";

    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask = R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY;
    built.mode.transparencyMode = R3D_TRANSPARENCY_OPAQUE;

    built.effect = R3D_LoadSurfaceEffectFromMemory(source);

    if (built.effect)
    {
        coverage = r3d_mode_clamp01(coverage);
        R3D_SetSurfaceEffectUniform(built.effect, "coverage", &coverage);
    }

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildStencilMask(uint8_t reference)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_PASS |
        R3D_RENDER_MODE_OVERRIDE_STENCIL |
        R3D_RENDER_MODE_OVERRIDE_DEPTH_COMPARE;

    built.mode.renderPass = R3D_RENDER_PASS_STENCIL_MASK;
    built.mode.depthCompare = R3D_COMPARE_ALWAYS;

    built.mode.stencil.mode = R3D_COMPARE_ALWAYS;
    built.mode.stencil.ref = reference;
    built.mode.stencil.mask = 0xFF;
    built.mode.stencil.opPass = R3D_STENCIL_REPLACE;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildStencilTest(uint8_t reference)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_PASS |
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY |
        R3D_RENDER_MODE_OVERRIDE_STENCIL;

    built.mode.renderPass = R3D_RENDER_PASS_STENCIL_TEST;
    built.mode.transparencyMode = R3D_TRANSPARENCY_BLEND;

    built.mode.stencil.mode = R3D_COMPARE_EQUAL;
    built.mode.stencil.ref = reference;
    built.mode.stencil.mask = 0xFF;
    built.mode.stencil.opFail = R3D_STENCIL_KEEP;
    built.mode.stencil.opZFail = R3D_STENCIL_KEEP;
    built.mode.stencil.opPass = R3D_STENCIL_KEEP;

    return built;
}